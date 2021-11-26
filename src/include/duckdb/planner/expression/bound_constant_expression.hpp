//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/planner/expression/bound_constant_expression.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/types/value.hpp"
#include "duckdb/planner/expression.hpp"

namespace duckdb {

class BoundConstantExpression : public Expression {
public:
	explicit BoundConstantExpression(Value value);

	//! Datum, 包含的具体的值.
	Value value;

public:
	string ToString() const override;

	bool Equals(const BaseExpression *other) const override;
	hash_t Hash() const override;

	unique_ptr<Expression> Copy() override;
};
} // namespace duckdb
