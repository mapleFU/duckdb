//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/planner/column_binding.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include <functional>

namespace duckdb {

//! 对列的 binding
struct ColumnBinding {
	// 对应的 table_id.
	idx_t table_index;
	// 对应的 column_id.
	idx_t column_index;

	ColumnBinding() : table_index(INVALID_INDEX), column_index(INVALID_INDEX) {
	}
	ColumnBinding(idx_t table, idx_t column) : table_index(table), column_index(column) {
	}

	bool operator==(const ColumnBinding &rhs) const {
		return table_index == rhs.table_index && column_index == rhs.column_index;
	}
};

} // namespace duckdb
