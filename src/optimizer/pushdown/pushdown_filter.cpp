#include "duckdb/optimizer/filter_pushdown.hpp"
#include "duckdb/planner/operator/logical_empty_result.hpp"
#include "duckdb/planner/operator/logical_filter.hpp"

namespace duckdb {

using Filter = FilterPushdown::Filter;

unique_ptr<LogicalOperator> FilterPushdown::PushdownFilter(unique_ptr<LogicalOperator> op) {
	D_ASSERT(op->type == LogicalOperatorType::LOGICAL_FILTER);
	auto &filter = (LogicalFilter &)*op;
	// filter: gather the filters and remove the filter from the set of operations
	//
	// filter 可能有多个 Expression.
	for (auto &expression : filter.expressions) {
		if (AddFilter(move(expression)) == FilterResult::UNSATISFIABLE) {
			// filter statically evaluates to false, strip tree
			return make_unique<LogicalEmptyResult>(move(op));
		}
	}
	GenerateFilters();
	return Rewrite(move(filter.children[0]));
}

} // namespace duckdb
