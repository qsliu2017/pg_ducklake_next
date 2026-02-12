/*
 * pg_ducklake_duckdb.cpp — DuckDB-facing translation unit.
 *
 * This file includes DuckDB and ducklake headers but NEVER PostgreSQL headers.
 * It provides the bridge functions declared in pg_ducklake_bridge.h.
 *
 * Access to the DuckDB instance is via GetDuckDBDatabase(), an extern "C"
 * function exported by pg_duckdb with __attribute__((visibility("default"))).
 */
#include "duckdb.hpp"
#include "duckdb/main/database.hpp"
#include "ducklake_extension.hpp"

#include "pg_ducklake_bridge.h"

/* Exported by pg_duckdb — returns duckdb::DuckDB* as void* */
extern "C" void *GetDuckDBDatabase(void);

extern "C" void
ducklake_ensure_loaded(void) {
	auto *db = static_cast<duckdb::DuckDB *>(GetDuckDBDatabase());
	db->LoadStaticExtension<duckdb::DucklakeExtension>();
}

static thread_local std::string last_error;

extern "C" int
ducklake_execute_query(const char *query, const char **errmsg_out) {
	try {
		auto *db = static_cast<duckdb::DuckDB *>(GetDuckDBDatabase());
		auto conn = duckdb::make_uniq<duckdb::Connection>(*db);
		auto result = conn->Query(query);
		if (result->HasError()) {
			last_error = result->GetError();
			if (errmsg_out) {
				*errmsg_out = last_error.c_str();
			}
			return 1;
		}
		return 0;
	} catch (const std::exception &e) {
		last_error = e.what();
		if (errmsg_out) {
			*errmsg_out = last_error.c_str();
		}
		return 1;
	}
}
