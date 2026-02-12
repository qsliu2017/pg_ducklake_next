/*
 * pg_ducklake_bridge.h — Pure C bridge between PostgreSQL and DuckDB translation units.
 *
 * This header must NOT include any PostgreSQL or DuckDB headers.
 * It provides extern "C" function declarations that allow the PostgreSQL-facing
 * code (pg_ducklake_pg.cpp) to call into the DuckDB-facing code
 * (pg_ducklake_duckdb.cpp) without header contamination.
 */
#ifndef PG_DUCKLAKE_BRIDGE_H
#define PG_DUCKLAKE_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ensure the ducklake DuckDB extension is loaded into pg_duckdb's DuckDB
 * instance. This is idempotent — safe to call multiple times.
 *
 * Must be called after pg_duckdb is initialized (i.e., from a SQL function
 * context, not from _PG_init).
 */
void ducklake_ensure_loaded(void);

/*
 * Execute a DuckDB query through the DuckDB C++ API (bypassing SPI).
 * Returns 0 on success, non-zero on error.
 * On error, errmsg_out (if non-NULL) points to a static error string.
 */
int ducklake_execute_query(const char *query, const char **errmsg_out);

#ifdef __cplusplus
}
#endif

#endif /* PG_DUCKLAKE_BRIDGE_H */
