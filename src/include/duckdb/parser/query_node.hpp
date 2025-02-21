//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/parser/query_node.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/serializer.hpp"
#include "duckdb/parser/parsed_expression.hpp"
#include "duckdb/parser/result_modifier.hpp"
#include "duckdb/parser/common_table_expression_info.hpp"

namespace duckdb {

enum QueryNodeType : uint8_t {
	SELECT_NODE = 1,
	SET_OPERATION_NODE = 2,
	BOUND_SUBQUERY_NODE = 3,
	RECURSIVE_CTE_NODE = 4
};

//! Parser 解析除的 Query, 可以是 `QueryNodeType` 中的, 直观来说就是:
//! Select, Set, CTE, bound(这是什么?).
class QueryNode {
public:
	explicit QueryNode(QueryNodeType type) : type(type) {
	}
	virtual ~QueryNode() {
	}

	//! The type of the query node, either SetOperation or Select
	//! 描述的类型.
	QueryNodeType type;
	//! The set of result modifiers associated with this query node
	//! Order/Unique 之类的 modifier
	vector<unique_ptr<ResultModifier>> modifiers;
	//! CTEs (used by SelectNode and SetOperationNode)
	unordered_map<string, unique_ptr<CommonTableExpressionInfo>> cte_map;

	virtual const vector<unique_ptr<ParsedExpression>> &GetSelectList() const = 0;

public:
	virtual bool Equals(const QueryNode *other) const;

	//! Create a copy of this QueryNode
	virtual unique_ptr<QueryNode> Copy() = 0;
	//! Serializes a QueryNode to a stand-alone binary blob
	virtual void Serialize(Serializer &serializer);
	//! Deserializes a blob back into a QueryNode, returns nullptr if
	//! deserialization is not possible
	static unique_ptr<QueryNode> Deserialize(Deserializer &source);

protected:
	//! Copy base QueryNode properties from another expression to this one,
	//! used in Copy method
	void CopyProperties(QueryNode &other) const;
};

} // namespace duckdb
