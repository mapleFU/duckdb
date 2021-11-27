# DuckDB System Notes(2): Logical Plan Optimizer

在 `CreatePreparedStatement` 中，CreatePlan 之后，拿到了一个逻辑 Plan，下面要去拿物理 Plan, 或者优化：

```c++
	// extract the result column names from the plan
	result->read_only = planner.read_only;
	result->requires_valid_transaction = planner.requires_valid_transaction;
	result->allow_stream_result = planner.allow_stream_result;
	result->names = planner.names;
	result->types = planner.types;
	result->value_map = move(planner.value_map);
	// 拿到 catalog 版本.
	result->catalog_version = Transaction::GetTransaction(*this).catalog_version;

	// 走 Optimizer 优化.
	if (enable_optimizer) {
		profiler->StartPhase("optimizer");
		Optimizer optimizer(*planner.binder, *this);
		// Optimize 拿到的还是个 Logical Plan.
		plan = optimizer.Optimize(move(plan));
		D_ASSERT(plan);
		profiler->EndPhase();

#ifdef DEBUG
		plan->Verify();
#endif
	}

	profiler->StartPhase("physical_planner");
	// now convert logical query plan into a physical query plan
	PhysicalPlanGenerator physical_planner(*this);
	auto physical_plan = physical_planner.CreatePlan(move(plan));
	profiler->EndPhase();

#ifdef DEBUG
	D_ASSERT(!physical_plan->ToString().empty());
#endif
	// 拿到物理的执行计划.
	result->plan = move(physical_plan);
	return result;

```

与 System-R Optimizer 不同，这里的 Optimizer 只会进行一些规则优化，不会拿统计信息优化...反正很 sb

```c++
unique_ptr<LogicalOperator> Optimizer::Optimize(unique_ptr<LogicalOperator> plan) {
	// first we perform expression rewrites using the ExpressionRewriter
	// this does not change the logical plan structure, but only simplifies the expression trees
	RunOptimizer(OptimizerType::EXPRESSION_REWRITER, [&]() { rewriter.VisitOperator(*plan); });

	// perform filter pullup
	RunOptimizer(OptimizerType::FILTER_PULLUP, [&]() {
		FilterPullup filter_pullup;
		plan = filter_pullup.Rewrite(move(plan));
	});

	// perform filter pushdown
	RunOptimizer(OptimizerType::FILTER_PUSHDOWN, [&]() {
		FilterPushdown filter_pushdown(*this);
		plan = filter_pushdown.Rewrite(move(plan));
	});

	RunOptimizer(OptimizerType::REGEX_RANGE, [&]() {
		RegexRangeFilter regex_opt;
		plan = regex_opt.Rewrite(move(plan));
	});

	RunOptimizer(OptimizerType::IN_CLAUSE, [&]() {
		InClauseRewriter rewriter(*this);
		plan = rewriter.Rewrite(move(plan));
	});

	// then we perform the join ordering optimization
	// this also rewrites cross products + filters into joins and performs filter pushdowns
	RunOptimizer(OptimizerType::JOIN_ORDER, [&]() {
		JoinOrderOptimizer optimizer(context);
		plan = optimizer.Optimize(move(plan));
	});

	// removes any redundant DelimGets/DelimJoins
	RunOptimizer(OptimizerType::DELIMINATOR, [&]() {
		Deliminator deliminator;
		plan = deliminator.Optimize(move(plan));
	});

	RunOptimizer(OptimizerType::UNUSED_COLUMNS, [&]() {
		RemoveUnusedColumns unused(binder, context, true);
		unused.VisitOperator(*plan);
	});

	// perform statistics propagation
	RunOptimizer(OptimizerType::STATISTICS_PROPAGATION, [&]() {
		StatisticsPropagator propagator(context);
		propagator.PropagateStatistics(plan);
	});

	// then we extract common subexpressions inside the different operators
	RunOptimizer(OptimizerType::COMMON_SUBEXPRESSIONS, [&]() {
		CommonSubExpressionOptimizer cse_optimizer(binder);
		cse_optimizer.VisitOperator(*plan);
	});

	RunOptimizer(OptimizerType::COMMON_AGGREGATE, [&]() {
		CommonAggregateOptimizer common_aggregate;
		common_aggregate.VisitOperator(*plan);
	});

	RunOptimizer(OptimizerType::COLUMN_LIFETIME, [&]() {
		ColumnLifetimeAnalyzer column_lifetime(true);
		column_lifetime.VisitOperator(*plan);
	});

	// transform ORDER BY + LIMIT to TopN
	RunOptimizer(OptimizerType::TOP_N, [&]() {
		TopN topn;
		plan = topn.Optimize(move(plan));
	});

	// apply simple expression heuristics to get an initial reordering
	RunOptimizer(OptimizerType::REORDER_FILTER, [&]() {
		ExpressionHeuristics expression_heuristics(*this);
		plan = expression_heuristics.Rewrite(move(plan));
	});

	return plan;
}
```

我们来看看 Optimizer:

```c++
class Optimizer {
public:
	Optimizer(Binder &binder, ClientContext &context);

	unique_ptr<LogicalOperator> Optimize(unique_ptr<LogicalOperator> plan);

	ClientContext &context;
	Binder &binder;
	ExpressionRewriter rewriter;

private:
	void RunOptimizer(OptimizerType type, const std::function<void()> &callback);
};
```

嘿，这里有 Binder 和 Rewriter!