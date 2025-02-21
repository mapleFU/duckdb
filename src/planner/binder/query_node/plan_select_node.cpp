#include "duckdb/planner/binder.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/expression/bound_reference_expression.hpp"
#include "duckdb/planner/operator/list.hpp"
#include "duckdb/planner/operator/logical_expression_get.hpp"
#include "duckdb/planner/operator/logical_limit.hpp"
#include "duckdb/planner/query_node/bound_select_node.hpp"
#include "duckdb/planner/operator/logical_dummy_scan.hpp"

namespace duckdb {

unique_ptr<LogicalOperator> Binder::PlanFilter(unique_ptr<Expression> condition, unique_ptr<LogicalOperator> root) {
	PlanSubqueries(&condition, &root);
	auto filter = make_unique<LogicalFilter>(move(condition));
	filter->AddChild(move(root));
	return move(filter);
}

unique_ptr<LogicalOperator> Binder::CreatePlan(BoundSelectNode &statement) {
	unique_ptr<LogicalOperator> root;
	D_ASSERT(statement.from_table);
	// 拿到扫表的方法.
	root = CreatePlan(*statement.from_table);
	D_ASSERT(root);

	// plan the sample clause
	if (statement.sample_options) {
		root = make_unique<LogicalSample>(move(statement.sample_options), move(root));
	}

	// 对 condition 创建 Filter.
	if (statement.where_clause) {
		root = PlanFilter(move(statement.where_clause), move(root));
	}

	if (!statement.aggregates.empty() || !statement.groups.group_expressions.empty()) {
		if (!statement.groups.group_expressions.empty()) {
			// visit the groups
			for (auto &group : statement.groups.group_expressions) {
				PlanSubqueries(&group, &root);
			}
		}
		// now visit all aggregate expressions
		for (auto &expr : statement.aggregates) {
			PlanSubqueries(&expr, &root);
		}
		// finally create the aggregate node with the group_index and aggregate_index as obtained from the binder
		auto aggregate =
		    make_unique<LogicalAggregate>(statement.group_index, statement.aggregate_index, move(statement.aggregates));
		aggregate->groups = move(statement.groups.group_expressions);
		aggregate->groupings_index = statement.groupings_index;
		aggregate->grouping_sets = move(statement.groups.grouping_sets);
		aggregate->grouping_functions = move(statement.grouping_functions);

		aggregate->AddChild(move(root));
		root = move(aggregate);
	} else if (!statement.groups.grouping_sets.empty()) {
		// edge case: we have grouping sets but no groups or aggregates
		// this can only happen if we have e.g. select 1 from tbl group by ();
		// just output a dummy scan
		root = make_unique_base<LogicalOperator, LogicalDummyScan>(statement.group_index);
	}

	if (statement.having) {
		PlanSubqueries(&statement.having, &root);
		auto having = make_unique<LogicalFilter>(move(statement.having));

		having->AddChild(move(root));
		root = move(having);
	}

	if (!statement.windows.empty()) {
		auto win = make_unique<LogicalWindow>(statement.window_index);
		win->expressions = move(statement.windows);
		// visit the window expressions
		for (auto &expr : win->expressions) {
			PlanSubqueries(&expr, &root);
		}
		D_ASSERT(!win->expressions.empty());
		win->AddChild(move(root));
		root = move(win);
	}

	if (!statement.unnests.empty()) {
		auto unnest = make_unique<LogicalUnnest>(statement.unnest_index);
		unnest->expressions = move(statement.unnests);
		// visit the unnest expressions
		for (auto &expr : unnest->expressions) {
			PlanSubqueries(&expr, &root);
		}
		D_ASSERT(!unnest->expressions.empty());
		unnest->AddChild(move(root));
		root = move(unnest);
	}

	for (auto &expr : statement.select_list) {
		PlanSubqueries(&expr, &root);
	}

	for (size_t i = 0; i < statement.modifiers.size(); i++) {
		auto &modifier = statement.modifiers[i];
		if (modifier->type != ResultModifierType::LIMIT_MODIFIER) {
			continue;
		}
		auto &limit_modifier = (BoundLimitModifier &)*modifier;
		if (limit_modifier.limit || limit_modifier.offset) {
			PlanSubqueries(&limit_modifier.limit, &root);
			PlanSubqueries(&limit_modifier.offset, &root);
			auto limit = make_unique<LogicalLimit>(limit_modifier.limit_val, limit_modifier.offset_val,
			                                       move(limit_modifier.limit), move(limit_modifier.offset));
			limit->AddChild(move(root));
			root = move(limit);
			// Delete from modifiers
			std::swap(statement.modifiers[i], statement.modifiers.back());
			statement.modifiers.erase(statement.modifiers.end() - 1);
			i--;
		}
	}

	// create the projection
	auto proj = make_unique<LogicalProjection>(statement.projection_index, move(statement.select_list));
	auto &projection = *proj;
	proj->AddChild(move(root));
	root = move(proj);

	// finish the plan by handling the elements of the QueryNode
	root = VisitQueryNode(statement, move(root));

	// add a prune node if necessary
	if (statement.need_prune) {
		D_ASSERT(root);
		vector<unique_ptr<Expression>> prune_expressions;
		for (idx_t i = 0; i < statement.column_count; i++) {
			prune_expressions.push_back(make_unique<BoundColumnRefExpression>(
			    projection.expressions[i]->return_type, ColumnBinding(statement.projection_index, i)));
		}
		auto prune = make_unique<LogicalProjection>(statement.prune_index, move(prune_expressions));
		prune->AddChild(move(root));
		root = move(prune);
	}
	return root;
}

} // namespace duckdb
