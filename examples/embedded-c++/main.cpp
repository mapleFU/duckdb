#include "duckdb.hpp"

using namespace duckdb;

int main() {
	DuckDB db(nullptr);
	Connection con(db);

	con.Query("CREATE TABLE integers(i INTEGER, v INTEGER)");
	con.Query("INSERT INTO integers VALUES (3, 5), (4, 10)");
	auto result = con.Query("SELECT v as v_a FROM integers");
	result->Print();
}