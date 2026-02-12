extern "C" {
#include "postgres.h"

#include "fmgr.h"

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

void _PG_init(void) {}
} // extern "C"
