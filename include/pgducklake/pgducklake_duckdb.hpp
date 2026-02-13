#pragma once

/*
 * pgducklake_duckdb.hpp — C interface for DuckDB/DuckLake operations
 *
 * Provides extern "C" functions for DuckLake extension lifecycle management.
 * Query execution against DuckDB is done via pg_duckdb's raw_query() UDF
 * through PostgreSQL's SPI, not through direct DuckDB instance access.
 */

extern "C" {

/* Called once during _PG_init() to register the DuckLake metadata manager. */
void ducklake_init_extension(void);

/*
 * Callback invoked by pg_duckdb during DuckDBManager::Initialize().
 * Receives a pointer to the DuckDB instance (duckdb::DuckDB*) and loads
 * the DuckLake static extension into it.
 */
void ducklake_load_extension(void *db, void *context);

} // extern "C"
