//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/planner/operator/logical_get.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/planner/logical_operator.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/planner/table_filter.hpp"

namespace duckdb {

//! LogicalGet represents a scan operation from a data source.
//! 对 Table 的 Scan, 这里重要的是带有了 names/column_ids 等.
//! 这个方法是可以被裁剪下推的.
class LogicalGet : public LogicalOperator {
public:
	LogicalGet(idx_t table_index, TableFunction function, unique_ptr<FunctionData> bind_data,
	           vector<LogicalType> returned_types, vector<string> returned_names);

	//! The table index in the current bind context
	idx_t table_index;
	//! The function that is called
	TableFunction function;
	//! The bind data of the function
	unique_ptr<FunctionData> bind_data;
	//! The types of ALL columns that can be returned by the table function
	vector<LogicalType> returned_types;
	//! The names of ALL columns that can be returned by the table function
	vector<string> names;
	//! Bound column IDs
	vector<column_t> column_ids;
	//! Filters pushed down for table scan
	//! 表访问的 Filter.
	TableFilterSet table_filters;

	string GetName() const override;
	string ParamsToString() const override;

public:
	vector<ColumnBinding> GetColumnBindings() override;

	idx_t EstimateCardinality(ClientContext &context) override;

protected:
	void ResolveTypes() override;
};
} // namespace duckdb
