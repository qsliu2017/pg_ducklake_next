/*
 * pgducklake_duckdb.cpp — DuckDB-facing translation unit
 *
 * This file includes DuckDB and DuckLake headers but NEVER PostgreSQL headers.
 * It provides the high-level C++ interface for DuckDB/DuckLake operations.
 *
 * Access to pg_duckdb's DuckDB instance is via GetDuckDBDatabase(), an extern "C"
 * function exported by pg_duckdb with __attribute__((visibility("default"))).
 */

#include "pgducklake/pgducklake_duckdb.hpp"
#include "pgducklake/pgducklake_metadata_manager.hpp"
#include "pgducklake/utility/cpp_wrapper.hpp"

#include "duckdb/main/database.hpp"
#include "ducklake_extension.hpp"
#include "storage/ducklake_metadata_manager.hpp"

// Imported from pg_duckdb — returns duckdb::DuckDB* as void*
extern "C" void *GetDuckDBDatabase(void);

// Declared in pgducklake.cpp — returns PostgreSQL DataDir
extern "C" const char *pgducklake_get_data_dir(void);

// Called once
extern "C" void ducklake_init_extension(void);

// Called every time a new duckdb backend is created
extern "C" void ducklake_load_extension(void);

namespace pgducklake {

// Thread-local storage for error messages
static thread_local std::string last_error;

// Per-backend state for catalog attachment
static bool catalog_attached = false;

duckdb::DuckDB &
DuckLakeManager::GetDatabase() {
	auto *db_ptr = static_cast<duckdb::DuckDB *>(GetDuckDBDatabase());
	return *db_ptr;
}

duckdb::unique_ptr<duckdb::Connection>
DuckLakeManager::CreateConnection() {
	auto conn = duckdb::make_uniq<duckdb::Connection>(GetDatabase());

	// Note: DuckLake extension is already loaded globally during _PG_init()
	// We just need to attach the catalog for this backend

	// Try to attach the DuckLake catalog once per backend
	// The data directory is created during extension initialization and persists
	if (!catalog_attached) {
		const char *data_dir = pgducklake_get_data_dir();
		if (data_dir && data_dir[0] != '\0') {
			std::string catalog_data_path = std::string(data_dir) + "/pg_ducklake";

			// ATTACH with IF NOT EXISTS - will succeed if catalog exists or can be attached
			std::string attach_query =
				"ATTACH IF NOT EXISTS 'ducklake:pgducklake:' AS pgducklake "
				"(METADATA_SCHEMA 'ducklake', DATA_PATH '" + catalog_data_path + "')";

			auto result = conn->Query(attach_query);
			if (!result->HasError()) {
				catalog_attached = true;
			} else {
				// Store error - don't mark as attached so we can retry
				last_error = std::string("ATTACH failed: ") + result->GetError();
			}
		}
	}

	return conn;
}

int
DuckLakeManager::ExecuteQuery(const char *query, const char **errmsg_out) {
	try {
    	PostgresScopedStackReset scoped_stack_reset;
		auto conn = CreateConnection();
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

} // namespace pgducklake

/*
 * C interface functions for PostgreSQL-facing code.
 * These provide extern "C" linkage for calling from PostgreSQL translation units.
 */

extern "C" void
ducklake_init_extension(void){
    duckdb::DuckLakeMetadataManager::Register("pgducklake", pgducklake::PgDuckLakeMetadataManager::Create);
}

extern "C" void
ducklake_load_extension(void) {
	auto &db = pgducklake::DuckLakeManager::GetDatabase();

	// Load DuckLake extension (can be called multiple times safely)
	db.LoadStaticExtension<duckdb::DucklakeExtension>();
}

extern "C" int
ducklake_execute_query(const char *query, const char **errmsg_out) {
	return pgducklake::DuckLakeManager::ExecuteQuery(query, errmsg_out);
}
