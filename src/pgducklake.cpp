extern "C" {
#include "postgres.h"

#include "fmgr.h"
#include "miscadmin.h"

#ifdef PG_MODULE_MAGIC_EXT
#ifndef PG_DUCKDB_VERSION
// Should always be defined via build system, but keep a fallback here for
// static analysis tools etc.
#define PG_DUCKDB_VERSION "unknown"
#endif
PG_MODULE_MAGIC_EXT(.name = "pg_duckdb", .version = PG_DUCKDB_VERSION);
#else
PG_MODULE_MAGIC;
#endif

// Forward declaration of C interface function
void ducklake_load_extension(void);

void
_PG_init(void) {
	// Load DuckLake extension into pg_duckdb's DuckDB instance
	// This is called once per backend when the shared library is first loaded
	ducklake_load_extension();
}

/*
 * Get PostgreSQL data directory path.
 * Can be called from DuckDB-facing code.
 */
const char *
pgducklake_get_data_dir(void) {
	return DataDir;
}

} // extern "C"
