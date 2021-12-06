//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/types/vector_cache.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/types.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/common/types/vector_buffer.hpp"

namespace duckdb {
class Vector;

//! The VectorCache holds cached data for
//! 这里的内容应该是给 csv 之类的拿到一个有所有权的地方. 然后方便各种 Reset.
//! 所以感觉这里应该是 "多种逻辑类型都支持的 enum + 持有所有权的 cache".
class VectorCache {
public:
	//! Instantiate a vector cache with the given type
	explicit VectorCache(const LogicalType &type);

	buffer_ptr<VectorBuffer> buffer;

public:
	void ResetFromCache(Vector &result) const;

	const LogicalType &GetType() const;
};

} // namespace duckdb
