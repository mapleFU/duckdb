//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/parser/statement/insert_statement.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/parser/parsed_expression.hpp"
#include "duckdb/parser/statement/select_statement.hpp"

namespace duckdb {

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

} // namespace duckdb
