#include "duckdb/catalog/catalog_entry/view_catalog_entry.hpp"
#include "duckdb/parser/tableref/basetableref.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/planner/tableref/bound_basetableref.hpp"
#include "duckdb/planner/tableref/bound_subqueryref.hpp"
#include "duckdb/planner/tableref/bound_cteref.hpp"
#include "duckdb/planner/operator/logical_get.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/function/table/table_scan.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/main/config.hpp"

namespace duckdb {

unique_ptr<BoundTableRef> Binder::Bind(BaseTableRef &ref) {
	QueryErrorContext error_context(root_statement, ref.query_location);
	// CTEs and views are also referred to using BaseTableRefs, hence need to distinguish here
	// check if the table name refers to a CTE
	auto cte = FindCTE(ref.table_name, ref.table_name == alias);
	if (cte) {
		// Check if there is a CTE binding in the BindContext
		auto ctebinding = bind_context.GetCTEBinding(ref.table_name);
		if (!ctebinding) {
			if (CTEIsAlreadyBound(cte)) {
				throw BinderException("Circular reference to CTE \"%s\", use WITH RECURSIVE to use recursive CTEs",
				                      ref.table_name);
			}
			// Move CTE to subquery and bind recursively
			SubqueryRef subquery(unique_ptr_cast<SQLStatement, SelectStatement>(cte->query->Copy()));
			subquery.alias = ref.alias.empty() ? ref.table_name : ref.alias;
			subquery.column_name_alias = cte->aliases;
			for (idx_t i = 0; i < ref.column_name_alias.size(); i++) {
				if (i < subquery.column_name_alias.size()) {
					subquery.column_name_alias[i] = ref.column_name_alias[i];
				} else {
					subquery.column_name_alias.push_back(ref.column_name_alias[i]);
				}
			}
			return Bind(subquery, cte);
		} else {
			// There is a CTE binding in the BindContext.
			// This can only be the case if there is a recursive CTE present.
			auto index = GenerateTableIndex();
			auto result = make_unique<BoundCTERef>(index, ctebinding->index);
			auto b = ctebinding;
			auto alias = ref.alias.empty() ? ref.table_name : ref.alias;
			auto names = BindContext::AliasColumnNames(alias, b->names, ref.column_name_alias);

			bind_context.AddGenericBinding(index, alias, names, b->types);
			// Update references to CTE
			auto cteref = bind_context.cte_references[ref.table_name];
			(*cteref)++;

			result->types = b->types;
			result->bound_columns = move(names);
			return move(result);
		}
	}
	// not a CTE
	// extract a table or view from the catalog
	// 从 Catalog 里面找到 Table or View, 类型是 CatalogEntry.
	auto table_or_view =
	    Catalog::GetCatalog(context).GetEntry(context, CatalogType::TABLE_ENTRY, ref.schema_name, ref.table_name,
	                                          ref.schema_name.empty() ? true : false, error_context);
	if (!table_or_view) {
		// TODO(mwish): 这段我没看太懂.
		// table could not be found: try to bind a replacement scan
		auto &config = DBConfig::GetConfig(context);
		for (auto &scan : config.replacement_scans) {
			auto replacement_function = scan.function(ref.table_name, scan.data);
			if (replacement_function) {
				replacement_function->alias = ref.alias.empty() ? ref.table_name : ref.alias;
				replacement_function->column_name_alias = ref.column_name_alias;
				return Bind(*replacement_function);
			}
		}
		// could not find an alternative: bind again to get the error
		table_or_view = Catalog::GetCatalog(context).GetEntry(context, CatalogType::TABLE_ENTRY, ref.schema_name,
		                                                      ref.table_name, false, error_context);
	}
	switch (table_or_view->type) {
	case CatalogType::TABLE_ENTRY: {
		// base table: create the BoundBaseTableRef node
		// 需要生成一个 TableIndex.
		auto table_index = GenerateTableIndex();
		auto table = (TableCatalogEntry *)table_or_view;

		auto scan_function = TableScanFunction::GetFunction();
		// 需要 bind 的 TblScan.
		auto bind_data = make_unique<TableScanBindData>(table);
		vector<LogicalType> table_types; // 列的逻辑类型.

		// 遍历所有 Column
		vector<string> table_names;
		for (auto &col : table->columns) {
			table_types.push_back(col.type);
			table_names.push_back(col.name);
		}
		// Table 的名称.
		auto alias = ref.alias.empty() ? ref.table_name : ref.alias;
		// 把 tables_names 绑定到 alias 上, 然后生成唯一的 column_name, 这个只是指导要访问哪些列.
		// 假设没有 `column_name_alias`
		table_names = BindContext::AliasColumnNames(alias, table_names, ref.column_name_alias);

		// 添加一个 TableScan.
		auto logical_get =
		    make_unique<LogicalGet>(table_index, scan_function, move(bind_data), table_types, table_names);
		// 根据 inc 生成的 table_index, alias, types, 和表访问方法.
		// 这里会添加一个 full scan
		bind_context.AddBaseTable(table_index, alias, table_names, table_types, *logical_get);
		return make_unique_base<BoundTableRef, BoundBaseTableRef>(table, move(logical_get));
	}
	case CatalogType::VIEW_ENTRY: {
		// the node is a view: get the query that the view represents
		auto view_catalog_entry = (ViewCatalogEntry *)table_or_view;
		// We need to use a new binder for the view that doesn't reference any CTEs
		// defined for this binder so there are no collisions between the CTEs defined
		// for the view and for the current query
		bool inherit_ctes = false;
		auto view_binder = Binder::CreateBinder(context, this, inherit_ctes);
		view_binder->can_contain_nulls = true;
		SubqueryRef subquery(unique_ptr_cast<SQLStatement, SelectStatement>(view_catalog_entry->query->Copy()));
		subquery.alias = ref.alias.empty() ? ref.table_name : ref.alias;
		subquery.column_name_alias =
		    BindContext::AliasColumnNames(subquery.alias, view_catalog_entry->aliases, ref.column_name_alias);
		// bind the child subquery
		auto bound_child = view_binder->Bind(subquery);
		D_ASSERT(bound_child->type == TableReferenceType::SUBQUERY);
		// verify that the types and names match up with the expected types and names
		auto &bound_subquery = (BoundSubqueryRef &)*bound_child;
		if (bound_subquery.subquery->types != view_catalog_entry->types) {
			throw BinderException("Contents of view were altered: types don't match!");
		}
		bind_context.AddSubquery(bound_subquery.subquery->GetRootIndex(), subquery.alias, subquery,
		                         *bound_subquery.subquery);
		return bound_child;
	}
	default:
		throw InternalException("Catalog entry type");
	}
}
} // namespace duckdb
