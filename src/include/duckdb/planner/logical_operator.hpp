//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/planner/logical_operator.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/common/common.hpp"
#include "duckdb/common/enums/logical_operator_type.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/planner/logical_operator_visitor.hpp"
#include "duckdb/planner/column_binding.hpp"

#include <functional>
#include <algorithm>

namespace duckdb {

//! LogicalOperator is the base class of the logical operators present in the
//! logical query tree.
//!
//! LogicalOperator 应该单纯表示怎么处理数据, 提供了逻辑上的指令. 它不等于物理上的 Plan.
//!
//! LogicalOperator 是指令式的:
//! 1. 用户根据请求构建了一整颗 Parsed 结构 SQLStatement, 对应一个 ast.
//! 2. 完成 Binding 之后, 系统被处理为 Logical 的 GetTable 等语句.
//! 3. 在 TiDB 中, 有的东西是直接已处理的. 这里还会转一次 Physical.
//!
//! DuckDB 的优化基本上是逻辑优化, 所以不会和 System-R 那样走优化.
class LogicalOperator {
public:
	explicit LogicalOperator(LogicalOperatorType type) : type(type) {
	}
	LogicalOperator(LogicalOperatorType type, vector<unique_ptr<Expression>> expressions)
	    : type(type), expressions(move(expressions)) {
	}
	virtual ~LogicalOperator() {
	}

	//! The type of the logical operator
	LogicalOperatorType type;
	//! The set of children of the operator
	vector<unique_ptr<LogicalOperator>> children;
	//! The set of expressions contained within the operator, if any
	vector<unique_ptr<Expression>> expressions;
	//! The types returned by this logical operator. Set by calling LogicalOperator::ResolveTypes.
	//! 在 ResolveTypes 调用的时候才获知.
	vector<LogicalType> types;
	//! Estimated Cardinality
	idx_t estimated_cardinality = 0;

public:
	virtual vector<ColumnBinding> GetColumnBindings() {
		return {ColumnBinding(0, 0)};
	}
	static vector<ColumnBinding> GenerateColumnBindings(idx_t table_idx, idx_t column_count);
	static vector<LogicalType> MapTypes(const vector<LogicalType> &types, const vector<idx_t> &projection_map);
	static vector<ColumnBinding> MapBindings(const vector<ColumnBinding> &types, const vector<idx_t> &projection_map);

	//! Resolve the types of the logical operator and its children
	void ResolveOperatorTypes();

	virtual string GetName() const;
	virtual string ParamsToString() const;
	virtual string ToString(idx_t depth = 0) const;
	void Print();
	//! Debug method: verify that the integrity of expressions & child nodes are maintained
	virtual void Verify();

	void AddChild(unique_ptr<LogicalOperator> child) {
		children.push_back(move(child));
	}

	virtual idx_t EstimateCardinality(ClientContext &context) {
		// simple estimator, just take the max of the children
		idx_t max_cardinality = 0;
		for (auto &child : children) {
			max_cardinality = MaxValue(child->EstimateCardinality(context), max_cardinality);
		}
		return max_cardinality;
	}

protected:
	//! Resolve types for this specific operator
	virtual void ResolveTypes() = 0;
};
} // namespace duckdb
