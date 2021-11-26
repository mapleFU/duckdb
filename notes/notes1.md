# DuckDB System Notes(1): Query To LogicalPlan

```c++
#include "duckdb.hpp"

using namespace duckdb;

int main() {
	DuckDB db(nullptr);
	Connection con(db);

	con.Query("CREATE TABLE integers(i INTEGER)");
	con.Query("INSERT INTO integers VALUES (3)");
	auto result = con.Query("SELECT * FROM integers");
	result->Print();
}
```

这是一个最简单的查询，那么，我们应该关注下面的对象：

`DuckDB`: 

```c++
//! The database object. This object holds the catalog and all the
//! database-specific meta information.
class DuckDB;
```

`Connection`:

```c++
//! A connection to a database. This represents a (client) connection that can
//! be used to query the database.
class Connection;
```

那么，我们沿着 Query 看看吧：

```c++
unique_ptr<MaterializedQueryResult> Connection::Query(const string &query) {
	auto result = context->Query(query, false);
	D_ASSERT(result->type == QueryResultType::MATERIALIZED_RESULT);
	return unique_ptr_cast<QueryResult, MaterializedQueryResult>(move(result));
}
```

这里要说明的是，QueryResult 可以参考 monet/X100 的 Execution。

里面的逻辑先做 Parsing，再丢去 `Run`:

## Parsing 阶段

```c++
unique_ptr<QueryResult> ClientContext::Query(const string &query, bool allow_stream_result) {
	auto lock = LockContext();
	LogQueryInternal(*lock, query);

	vector<unique_ptr<SQLStatement>> statements;
	try {
		InitialCleanup(*lock);
		// parse the query and transform it into a set of statements
		statements = ParseStatementsInternal(*lock, query);
	} catch (std::exception &ex) {
		return make_unique<MaterializedQueryResult>(ex.what());
	}

	if (statements.empty()) {
		// no statements, return empty successful result
		return make_unique<MaterializedQueryResult>(StatementType::INVALID_STATEMENT);
	}

	return RunStatements(*lock, query, statements, allow_stream_result);
}
```

`parse` 之后的结果是 `SQLStatement` 的数组。 DuckDB 的 parser 是 PG 的 parser，这里写代码应该包了一层：

```c++
//! SQLStatement is the base class of any type of SQL statement.
class SQLStatement {
public:
	explicit SQLStatement(StatementType type) : type(type) {};
	virtual ~SQLStatement() {
	}

	//! The statement type
	StatementType type;
	//! The statement location within the query string
	idx_t stmt_location;
	//! The statement length within the query string
	idx_t stmt_length;
	//! The number of prepared statement parameters (if any)
	idx_t n_param;
	//! The query text that corresponds to this SQL statement
	string query;

public:
	//! Create a copy of this SelectStatement
	virtual unique_ptr<SQLStatement> Copy() const = 0;
};
```

DuckDB 的 parser 模块里面把表达式做了很好的处理，我总觉得我也可以封装一套。SQLStatement 是从 PG 这里 Transform 过去的，做了一些表达式的迁移，然后转成了一个 `SQLStatement` 的列表，我们举例看看 Insert 和 Select 的 Statement:

```c++
//! According to https://duckdb.org/docs/sql/statements/insert .
class InsertStatement : public SQLStatement {
public:
	InsertStatement();

	//! The select statement to insert from
	//! Can be null.
	unique_ptr<SelectStatement> select_statement;
	//! Column names to insert into
	vector<string> columns;

	//! Table name to insert to
	string table;
	//! Schema name to insert to
	string schema;

public:
	unique_ptr<SQLStatement> Copy() const override;
};
```

查询这里比较好玩，有个 `QueryNode` 类型：

```c++
//! SelectStatement is a typical SELECT clause
class SelectStatement : public SQLStatement {
public:
	SelectStatement() : SQLStatement(StatementType::SELECT_STATEMENT) {
	}

	//! The main query node
	//! QueryNode, 即查询. 这里可以是 Select 等.
	unique_ptr<QueryNode> node;

public:
	//! Create a copy of this SelectStatement
	unique_ptr<SQLStatement> Copy() const override;
	//! Serializes a SelectStatement to a stand-alone binary blob
	void Serialize(Serializer &serializer);
	//! Deserializes a blob back into a SelectStatement, returns nullptr if
	//! deserialization is not possible
	static unique_ptr<SelectStatement> Deserialize(Deserializer &source);
	//! Whether or not the statements are equivalent
	bool Equals(const SQLStatement *other) const;
};
```

那么 `QueryNode` 是什么呢，答案是查询的包装器材. 这个是只有查询才有的哦：

```c++
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
```

暂时不管 CTE（感觉也没法管，因为我也不懂）。`QueryNode` 可能包含：`SelectNode` + `SetNode`, modifier 是某些通用的东西，比如 LIMIT，ORDER：

```c++
enum ResultModifierType : uint8_t { LIMIT_MODIFIER = 1, ORDER_MODIFIER = 2, DISTINCT_MODIFIER = 3 };
```

然后我们看看 `SelectNode`:

```c++
enum class AggregateHandling : uint8_t {
	STANDARD_HANDLING,     // standard handling as in the SELECT clause
	NO_AGGREGATES_ALLOWED, // no aggregates allowed: any aggregates in this node will result in an error
	FORCE_AGGREGATES       // force aggregates: any non-aggregate select list entry will become a GROUP
};

//! SelectNode represents a standard SELECT statement
//! Select 的具体逻辑
class SelectNode : public QueryNode {
public:
	SelectNode() : QueryNode(QueryNodeType::SELECT_NODE), aggregate_handling(AggregateHandling::STANDARD_HANDLING) {
	}

	//! The projection list
	vector<unique_ptr<ParsedExpression>> select_list;
	//! The FROM clause
	unique_ptr<TableRef> from_table;
	//! The WHERE clause
	unique_ptr<ParsedExpression> where_clause;
	//! list of groups
	GroupByNode groups;
	//! HAVING clause
	unique_ptr<ParsedExpression> having;
	//! Aggregate handling during binding
	AggregateHandling aggregate_handling;
	//! The SAMPLE clause
	unique_ptr<SampleOptions> sample;

	const vector<unique_ptr<ParsedExpression>> &GetSelectList() const override {
		return select_list;
	}

public:
	bool Equals(const QueryNode *other) const override;
	//! Create a copy of this SelectNode
	unique_ptr<QueryNode> Copy() override;
	//! Serializes a SelectNode to a stand-alone binary blob
	void Serialize(Serializer &serializer) override;
	//! Deserializes a blob back into a SelectNode
	static unique_ptr<QueryNode> Deserialize(Deserializer &source);
};
```

这里又涉及到几种对象（不管 sample）：

1. `TableRef`
2. `ParsedExpression`
3. `GroupByNode`
4. `AggregateHandling`

#### TableRef

```c++
//===--------------------------------------------------------------------===//
// Table Reference Types
//
// 可以是 BaseTable, 也可以是各种 Join(CROSS_PRODUCT, JOIN).
// 这里还支持了 EXPRESSION_LIST, CTE.
//===--------------------------------------------------------------------===//
enum class TableReferenceType : uint8_t {
	INVALID = 0,         // invalid table reference type
	BASE_TABLE = 1,      // base table reference
	SUBQUERY = 2,        // output of a subquery
	JOIN = 3,            // output of join
	CROSS_PRODUCT = 4,   // out of cartesian product
	TABLE_FUNCTION = 5,  // table producing function
	EXPRESSION_LIST = 6, // expression list
	CTE = 7,             // Recursive CTE
	EMPTY = 8            // placeholder for empty FROM
};
```

这里感觉主要的问题是 JOIN/CTE/NULL

然后 `TableRef` 本身定义如下:

```c++
//! Represents a generic expression that returns a table.
class TableRef {
public:
	explicit TableRef(TableReferenceType type) : type(type) {
	}
	virtual ~TableRef() {
	}

	//! 各种类型的 type, 可以是一个 Join, CTE 等.
	TableReferenceType type;
	//! 每个 table 拥有一个 Alias.
	string alias;
	//! Sample options (if any)
	//! SQL 的 sample.
	unique_ptr<SampleOptions> sample;
	//! The location in the query (if any)
	//! 这个有啥意义吗...好像是在 SQL 中给报错信息准备的.
	//! https://www.postgresql.org/docs/9.5/sql-syntax-lexical.html
	idx_t query_location = INVALID_INDEX;

public:
	//! Convert the object to a string
	virtual string ToString() const;
	void Print();

	virtual bool Equals(const TableRef *other) const;

	virtual unique_ptr<TableRef> Copy() = 0;

	//! Serializes a TableRef to a stand-alone binary blob
	virtual void Serialize(Serializer &serializer);
	//! Deserializes a blob back into a TableRef
	static unique_ptr<TableRef> Deserialize(Deserializer &source);

	//! Copy the properties of this table ref to the target
	void CopyProperties(TableRef &target) const;
};
```

各种 TableRef 都是他的子类了。`ParsedExpression` 包括 `Constant`, `Between`, `Comparasion` 等，具体实现如下：

```c++
//! ConstantExpression represents a constant value in the query
class ConstantExpression : public ParsedExpression {
public:
	explicit ConstantExpression(Value val);

	//! The constant value referenced
	Value value;

public:
	string ToString() const override;

	static bool Equals(const ConstantExpression *a, const ConstantExpression *b);
	hash_t Hash() const override;

	unique_ptr<ParsedExpression> Copy() const override;

	void Serialize(Serializer &serializer) override;
	static unique_ptr<ParsedExpression> Deserialize(ExpressionType type, Deserializer &source);
};

//! ComparisonExpression represents a boolean comparison (e.g. =, >=, <>). Always returns a boolean
//! and has two children.
class ComparisonExpression : public ParsedExpression {
public:
	ComparisonExpression(ExpressionType type, unique_ptr<ParsedExpression> left, unique_ptr<ParsedExpression> right);

	unique_ptr<ParsedExpression> left;
	unique_ptr<ParsedExpression> right;

public:
	string ToString() const override;

	static bool Equals(const ComparisonExpression *a, const ComparisonExpression *b);

	unique_ptr<ParsedExpression> Copy() const override;

	void Serialize(Serializer &serializer) override;
	static unique_ptr<ParsedExpression> Deserialize(ExpressionType type, Deserializer &source);
};

```

这里遵循递归的定义来描述内容。

## 如何处理 SQLStatement

构建完 Statements 之后，这里调用了 `RunStatements`, 然后会走到 `RunStatement`，再走到 `RunStatementOrPreparedStatement`，然后调用具体的逻辑：

```c++
unique_ptr<QueryResult> RunStatementOrPreparedStatement(ClientContextLock &lock, const string &query, unique_ptr<SQLStatement> statement, shared_ptr<PreparedStatementData> &prepared, vector<Value> *values, bool allow_stream_result);
```

这里面调用了：`RunStatementInternal`:

````c++
unique_ptr<QueryResult> ClientContext::RunStatementInternal(ClientContextLock &lock, const string &query,
                                                            unique_ptr<SQLStatement> statement,
                                                            bool allow_stream_result) {
	// prepare the query for execution
	// 准备执行, 拿到 expression.
	auto prepared = CreatePreparedStatement(lock, query, move(statement));
	// by default, no values are bound
	// 在非 Prepared 的情况下，这里是空的.
	vector<Value> bound_values;
	// execute the prepared statement
	return ExecutePreparedStatement(lock, query, move(prepared), move(bound_values), allow_stream_result);
}
````

1. `CreatePreparedStatement` 完成了 Plan + Optimized 的过程，返回了 `PreparedStatementData`
2. `ExecutePreparedStatement` 执行逻辑

创建 Plan 过程如下：

```c++
	Planner planner(*this);
	// 创建 LogicalPlan
	planner.CreatePlan(move(statement));
```

其中，CreatePlan 操作是个递归操作：

```c++
void Planner::CreatePlan(unique_ptr<SQLStatement> statement) {
	D_ASSERT(statement);
	switch (statement->type) {
	case StatementType::SELECT_STATEMENT:
	case StatementType::INSERT_STATEMENT:
	case StatementType::COPY_STATEMENT:
	case StatementType::DELETE_STATEMENT:
	case StatementType::UPDATE_STATEMENT:
	case StatementType::CREATE_STATEMENT:
	case StatementType::DROP_STATEMENT:
	case StatementType::ALTER_STATEMENT:
	case StatementType::TRANSACTION_STATEMENT:
	case StatementType::EXPLAIN_STATEMENT:
	case StatementType::VACUUM_STATEMENT:
	case StatementType::RELATION_STATEMENT:
	case StatementType::CALL_STATEMENT:
	case StatementType::EXPORT_STATEMENT:
	case StatementType::PRAGMA_STATEMENT:
	case StatementType::SHOW_STATEMENT:
	case StatementType::SET_STATEMENT:
	case StatementType::LOAD_STATEMENT:
		CreatePlan(*statement);
		break;
	case StatementType::EXECUTE_STATEMENT:
		PlanExecute(move(statement));
		break;
	case StatementType::PREPARE_STATEMENT:
		PlanPrepare(move(statement));
		break;
	default:
		throw NotImplementedException("Cannot plan statement of type %s!", StatementTypeToString(statement->type));
	}
}
```

可以看到，正常的表达式会走到下面：

```c++
void Planner::CreatePlan(SQLStatement &statement) {
	vector<BoundParameterExpression *> bound_parameters;

	// first bind the tables and columns to the catalog
	context.profiler->StartPhase("binder");
	binder->parameters = &bound_parameters;
	// 把 SQLStatement 的结构换成结构化的内容.
	// 这里 Statement 会转成 BoundStatement.
	auto bound_statement = binder->Bind(statement);
	context.profiler->EndPhase();

	this->read_only = binder->read_only;
	this->requires_valid_transaction = binder->requires_valid_transaction;
	this->allow_stream_result = binder->allow_stream_result;
	this->names = bound_statement.names;
	this->types = bound_statement.types;
	this->plan = move(bound_statement.plan);

	// set up a map of parameter number -> value entries
	for (auto &expr : bound_parameters) {
		// check if the type of the parameter could be resolved
		if (expr->return_type.id() == LogicalTypeId::INVALID || expr->return_type.id() == LogicalTypeId::UNKNOWN) {
			throw BinderException("Could not determine type of parameters");
		}
		auto value = make_unique<Value>(expr->return_type);
		expr->value = value.get();
		// check if the parameter number has been used before
		if (value_map.find(expr->parameter_nr) == value_map.end()) {
			// not used before, create vector
			value_map[expr->parameter_nr] = vector<unique_ptr<Value>>();
		} else if (value_map[expr->parameter_nr].back()->type() != value->type()) {
			// used before, but types are inconsistent
			throw BinderException("Inconsistent types found for parameter with index %llu", expr->parameter_nr);
		}
		value_map[expr->parameter_nr].push_back(move(value));
	}
}
```

原本的 Statement 类型是 `SQLStatement`, 在 Bind 之后会变成 `BoundStatement`. 这有什么区别呢？我们看看插入和查找的：

```c++
struct BoundStatement {
	unique_ptr<LogicalOperator> plan;
	// 返回的类型, 和名称.
	vector<LogicalType> types;
	vector<string> names;
};
```

Binder 会联动 System 的 Catalog，然后把表达式确定访问哪些地方：

```c++
BoundStatement Binder::Bind(SQLStatement &statement) {
	root_statement = &statement;
	switch (statement.type) {
	case StatementType::SELECT_STATEMENT:
		return Bind((SelectStatement &)statement);
	case StatementType::INSERT_STATEMENT:
		return Bind((InsertStatement &)statement);
	case StatementType::COPY_STATEMENT:
		return Bind((CopyStatement &)statement);
	case StatementType::DELETE_STATEMENT:
		return Bind((DeleteStatement &)statement);
	case StatementType::UPDATE_STATEMENT:
		return Bind((UpdateStatement &)statement);
	case StatementType::RELATION_STATEMENT:
		return Bind((RelationStatement &)statement);
	case StatementType::CREATE_STATEMENT:
		return Bind((CreateStatement &)statement);
	case StatementType::DROP_STATEMENT:
		return Bind((DropStatement &)statement);
	case StatementType::ALTER_STATEMENT:
		return Bind((AlterStatement &)statement);
	case StatementType::TRANSACTION_STATEMENT:
		return Bind((TransactionStatement &)statement);
	case StatementType::PRAGMA_STATEMENT:
		return Bind((PragmaStatement &)statement);
	case StatementType::EXPLAIN_STATEMENT:
		return Bind((ExplainStatement &)statement);
	case StatementType::VACUUM_STATEMENT:
		return Bind((VacuumStatement &)statement);
	case StatementType::SHOW_STATEMENT:
		return Bind((ShowStatement &)statement);
	case StatementType::CALL_STATEMENT:
		return Bind((CallStatement &)statement);
	case StatementType::EXPORT_STATEMENT:
		return Bind((ExportStatement &)statement);
	case StatementType::SET_STATEMENT:
		return Bind((SetStatement &)statement);
	case StatementType::LOAD_STATEMENT:
		return Bind((LoadStatement &)statement);
	default: // LCOV_EXCL_START
		throw NotImplementedException("Unimplemented statement type \"%s\" for Bind",
		                              StatementTypeToString(statement.type));
	} // LCOV_EXCL_STOP
}
```

这个有点鬼畜，但是我一定要贴出来...因为不贴就递归傻了。

### Bind SelectStatement

```c++
BoundStatement Binder::Bind(SelectStatement &stmt) {
	this->allow_stream_result = true; // TODO(mwish): why?
	return Bind(*stmt.node);
}
```

走到 Bind `QueryNode` 了（奇怪，Select 最终还是只查 Node 吗 2333）：

```c++
BoundStatement Binder::Bind(QueryNode &node) {
	auto bound_node = BindNode(node);

  // 实际上在这个地方才创建 Statement
	BoundStatement result;
	result.names = bound_node->names;
	result.types = bound_node->types;

	// and plan it
	// Binder 最后阶段会 CreatePlan, 创建一个 Operator.
	result.plan = CreatePlan(*bound_node);
	return result;
}
```

很重要的是，这里转了一层 `BindNode`，来给这个 `QueryNode` 转化：

```c++
//! 将 QueryNode 进行 Binding.
//! node 是一个多态的结构, 这里的绑定很奇怪, 就是会手动转子类 2333.
unique_ptr<BoundQueryNode> Binder::BindNode(QueryNode &node) {
	// first we visit the set of CTEs and add them to the bind context
	// 先访问 CTE.
	for (auto &cte_it : node.cte_map) {
		AddCTE(cte_it.first, cte_it.second.get());
	}
	// now we bind the node
	unique_ptr<BoundQueryNode> result;
	switch (node.type) {
	case QueryNodeType::SELECT_NODE:
		// 如果是 Select 的话
		result = BindNode((SelectNode &)node);
		break;
	case QueryNodeType::RECURSIVE_CTE_NODE:
		result = BindNode((RecursiveCTENode &)node);
		break;
	default:
		D_ASSERT(node.type == QueryNodeType::SET_OPERATION_NODE);
		result = BindNode((SetOperationNode &)node);
		break;
	}
	return result;
}
```

额，我们还是不管 CTE... 继续访问：

```c++
/! 对 SelectNode 进行 binding.
unique_ptr<BoundQueryNode> Binder::BindNode(SelectNode &statement) {
	auto result = make_unique<BoundSelectNode>();
	...

	// visit the select list and expand any "*" statements
	// 这里是展开 '*' 的 ParsedExpression.
	vector<unique_ptr<ParsedExpression>> new_select_list;
	for (auto &select_element : statement.select_list) {
		if (select_element->GetExpressionType() == ExpressionType::STAR) {
			// * statement, expand to all columns from the FROM clause
			// 如果是 * 的话, 用 `GenerateAllColumnExpressions` 来专程 SelectList.
			bind_context.GenerateAllColumnExpressions((StarExpression &)*select_element, new_select_list);
		} else {
			// regular statement, add it to the list
			new_select_list.push_back(move(select_element));
		}
	}
	// SelList 是空的, 不管了.
	if (new_select_list.empty()) {
		throw BinderException("SELECT list is empty after resolving * expressions!");
	}
	// swap & rebuild.
	// select_list 还是个 ParsedExpression.
	statement.select_list = move(new_select_list);

	// create a mapping of (alias -> index) and a mapping of (Expression -> index) for the SELECT list
	// alias_map: (alias -> index).
	// projection_map: (Expression -> index).
	unordered_map<string, idx_t> alias_map;
	expression_map_t<idx_t> projection_map;
	for (idx_t i = 0; i < statement.select_list.size(); i++) {
		auto &expr = statement.select_list[i];
		// 拿到对应的名称, 进行 binding, 这个流程可能是递归的.
		// 比如 2*x, 会递归处理 2 * x 和 (2), (x).
		result->names.push_back(expr->GetName());
		ExpressionBinder::BindTableNames(*this, *expr);
		if (!expr->alias.empty()) {
			alias_map[expr->alias] = i;
			result->names[i] = expr->alias;
		}
		projection_map[expr.get()] = i;
		result->original_expressions.push_back(expr->Copy());
	}
	result->column_count = statement.select_list.size();
```

这里我们可以看到，这里 binding 是有顺序的：

* Table
* SelectList
* ...

我们可以找到几个 Bind 之后的 Expression:

```c++
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
```

看，这样我们就完成了一个 binding 了，还记得之前吗：

```c++
	// and plan it
	// Binder 最后阶段会 CreatePlan, 创建一个 Operator.
	result.plan = CreatePlan(*bound_node);
```

这里我们会对 `BoundSelectNode` 调用 `CreatePlan`:

```c++
unique_ptr<LogicalOperator> Binder::CreatePlan(BoundSelectNode &statement)
```

它的类型是个 Logical Operator:

```c++
//! LogicalOperator is the base class of the logical operators present in the
//! logical query tree
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
```

我们同样可以自底向上构建

1. 对 `from_table` 来构建 Plan, 这里会构建 ScanTable, 尝试硬扫
2. 如果有 Where, 那么会构建 `Filter`
3. 尝试构建 agg 等
4. 构建  modifier

那么，我们就拿到一个 Logical Plan 了！