#include <string>
#include <vector>
#include <bitset>
#include <fstream>
#include <cstring>
#include <iostream>
#include <sstream>

#include "tiledb-extension.hpp"

#include <tiledb>

#ifndef DUCKDB_AMALGAMATION
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parsed_data/create_copy_function_info.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "utf8proc_wrapper.hpp"
#endif

using namespace duckdb;
using namespace std;

struct TileDBScanFunctionData : public TableFunctionData {
	bool finished;
	tiledb::Context ctx;
	unique_ptr<tiledb::Array> array;
	unique_ptr<tiledb::Query> query;
	vector<LogicalType> sql_types;
	vector<string> names;
};

class TileDBScanFunction : public TableFunction {
public:
	TileDBScanFunction() : TableFunction("tiledb_scan", {LogicalType::VARCHAR}, tiledb_scan, tiledb_bind){};

private:
	static LogicalType type_from_tiledb(tiledb_datatype_t t) {
		switch (t) {
			// TODO add more types here
		case TILEDB_INT8:
			return LogicalType::TINYINT;
		case TILEDB_INT16:
			return LogicalType::SMALLINT;
		case TILEDB_INT32:
			return LogicalType::INTEGER;
		case TILEDB_INT64:
			return LogicalType::BIGINT;
		case TILEDB_FLOAT32:
			return LogicalType::FLOAT;
		case TILEDB_FLOAT64:
			return LogicalType::DOUBLE;
		default:
			throw NotImplementedException("Unsupported TileDB Datatype");
		}
	}

	static unique_ptr<FunctionData> tiledb_bind(ClientContext &context, vector<Value> &inputs,
		unordered_map<string, Value> &named_parameters,
		vector<LogicalType> &input_table_types,
		vector<string> &input_table_names,
		vector<LogicalType> &return_types, vector<string> &names) {

		auto file_name = inputs[0].GetValue<string>();
		auto res = make_unique<TileDBScanFunctionData>();
		auto &data = *res;

		data.array = make_unique<tiledb::Array>(data.ctx, file_name, TILEDB_READ);
		tiledb::ArraySchema schema(data.ctx, file_name);
		// TODO: what happens if a dimension is not an int32 or if they are of mixed type?
		vector<int32_t> subarray;

		// add all the domains as columns
		auto domain = schema.domain();
		for (idx_t dim_idx = 0; dim_idx < domain.ndim(); dim_idx++) {
			auto dim = domain.dimension(dim_idx);
			names.push_back(dim.name());
			return_types.push_back(type_from_tiledb(dim.type()));
			// some gunky hack to define a catch-all subarray
			switch (dim.type()) {
			case TILEDB_INT32: {
				auto domain = dim.domain<int32_t>();
				subarray.push_back(domain.first);
				subarray.push_back(domain.second);
				break;
			}
			default:
				throw NotImplementedException("Unsupported TileDB Datatype");
			}
		}

		// now add all the attributes
		for (idx_t attr_idx = 0; attr_idx < schema.attribute_num(); attr_idx++) {
			auto attr = schema.attribute(attr_idx);
			names.push_back(attr.name());
			return_types.push_back(type_from_tiledb(attr.type()));
		}

		data.query = make_unique<tiledb::Query>(data.ctx, *data.array);
		data.query->set_subarray(subarray);
		data.sql_types = return_types;
		data.names = names;

		return move(res);
	}

	static void tiledb_scan(ClientContext &context, const FunctionData *bind_data,
		FunctionOperatorData *operator_state, DataChunk *input, DataChunk &output) {


		auto &data = *((TileDBScanFunctionData *)bind_data);
		if (data.finished) {
			return;
		}

		// this already removes columns that are not used in a query (column projection pushdown)
		idx_t out_idx = 0;
		for (auto col_idx : data.column_ids) {
			switch (data.sql_types[col_idx].id()) {
			case LogicalTypeId::INTEGER:
				data.query->set_buffer(data.names[col_idx], FlatVector::GetData<int32_t>(output.data[out_idx]),
				                       STANDARD_VECTOR_SIZE);
				break;
			default:
				throw NotImplementedException(data.sql_types[col_idx].ToString());
			}
			out_idx++;
		}

		// this actually executes
		auto status = data.query->submit();

		// now figure out how many values were read for the output cardinality
		auto result_el = data.query->result_buffer_elements();
		output.SetCardinality(result_el[data.names[data.column_ids[0]]].second);

		// check if we are done
		if (status == tiledb::Query::Status::COMPLETE) {
			data.query->finalize();
			data.array->close();
			data.finished = true;
		}
	}
};

void TileDBExtension::Load(DuckDB &db) {
	TileDBScanFunction scan_fun;
	CreateTableFunctionInfo cinfo(scan_fun);
	cinfo.name = "tiledb_scan";

	Connection conn(db);
	conn.context->transaction.BeginTransaction();
	db.instance->GetCatalog().CreateTableFunction(*conn.context, &cinfo);

	conn.context->transaction.Commit();
}
