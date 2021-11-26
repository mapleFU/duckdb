//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/parser/tableref.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/enums/tableref_type.hpp"
#include "duckdb/parser/parsed_data/sample_options.hpp"

namespace duckdb {
class Deserializer;
class Serializer;

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
} // namespace duckdb
