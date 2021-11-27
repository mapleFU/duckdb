#include "duckdb/optimizer/filter_pullup.hpp"
#include "duckdb/planner/operator/logical_filter.hpp"
#include "duckdb/planner/expression/bound_comparison_expression.hpp"
#include "duckdb/planner/expression_iterator.hpp"
#include "duckdb/planner/expression/bound_between_expression.hpp"

namespace duckdb {

//! 递归 Rewrite, 然后把 Expression 丢到 filters_expr_pullup.
//! op 是一个 filter, 这里:
//! 1. 对子节点(只有一个) 拉出来.
//! 2. Rewrite 子节点.
//! 3. 处理到 filter_expr_pullup 中.
unique_ptr<LogicalOperator> FilterPullup::PullupFilter(unique_ptr<LogicalOperator> op) {
	D_ASSERT(op->type == LogicalOperatorType::LOGICAL_FILTER);

	if (can_pullup) {
		unique_ptr<LogicalOperator> child = move(op->children[0]);
		// 对子节点 Rewrite(调用 pullup),
		child = Rewrite(move(child));
		// moving filter's expressions
		for (idx_t i = 0; i < op->expressions.size(); ++i) {
			filters_expr_pullup.push_back(move(op->expressions[i]));
		}
		return child;
	}
	op->children[0] = Rewrite(move(op->children[0]));
	return op;
}

} // namespace duckdb
