//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/common/vector_size.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/types.hpp"

namespace duckdb {

//! The vector size used in the execution engine
//! 1024 正如 MonetX/100 所提, 要把数据持有在 cache 中.
#ifndef STANDARD_VECTOR_SIZE
#define STANDARD_VECTOR_SIZE 1024
#endif

#if ((STANDARD_VECTOR_SIZE & (STANDARD_VECTOR_SIZE - 1)) != 0)
#error Vector size should be a power of two
#endif

//! Zero selection vector: completely filled with the value 0 [READ ONLY]
extern const sel_t ZERO_VECTOR[STANDARD_VECTOR_SIZE];

} // namespace duckdb
