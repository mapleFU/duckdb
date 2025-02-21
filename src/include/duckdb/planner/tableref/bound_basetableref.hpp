//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/planner/tableref/bound_basetableref.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/planner/bound_tableref.hpp"
#include "duckdb/planner/logical_operator.hpp"

namespace duckdb {
class TableCatalogEntry;

//! Represents a TableReference to a base table in the schema
//! 绑定到 Table 上
class BoundBaseTableRef : public BoundTableRef {
public:
	BoundBaseTableRef(TableCatalogEntry *table, unique_ptr<LogicalOperator> get)
	    : BoundTableRef(TableReferenceType::BASE_TABLE), table(table), get(move(get)) {
	}

	TableCatalogEntry *table;
	unique_ptr<LogicalOperator> get;
};
} // namespace duckdb
