/*
 * Minimal PostgreSQL extension demonstrating pg_duckdb + DuckLake integration.
 *
 * This shows the elegant approach for building extensions on top of pg_duckdb:
 * 1. Reference pg_duckdb's exported C interface (RegisterDuckdbTableAm)
 * 2. Use duckdb.raw_query() via SPI for DuckDB/DuckLake operations
 */
extern "C" {
#include "postgres.h"

#include "executor/spi.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "utils/builtins.h"

/* Reference pg_duckdb's exported C interface to verify linkage */
extern bool RegisterDuckdbTableAm(const char *name, const void *am);

PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(pg_ducklake_next_verify);
}

static void
ExecuteDuckDBQuery(const char *query) {
	char *sql = psprintf("SELECT duckdb.raw_query(%s)", quote_literal_cstr(query));
	int ret = SPI_execute(sql, false, 0);
	pfree(sql);

	if (ret < 0) {
		ereport(ERROR,
		        (errmsg("pg_ducklake_next: failed to execute DuckDB query"),
		         errdetail("Query: %s", query)));
	}
}

extern "C" Datum
pg_ducklake_next_verify(PG_FUNCTION_ARGS) {
	/*
	 * Verify we can reference pg_duckdb's C interface.
	 * We don't actually call it, just reference it to prove linkage.
	 */
	volatile void *ptr = (void *)RegisterDuckdbTableAm;
	(void)ptr; /* Suppress unused warning */

	/* Connect to SPI for executing SQL */
	if (SPI_connect() != SPI_OK_CONNECT) {
		ereport(ERROR, (errmsg("pg_ducklake_next: SPI_connect failed")));
	}

	/* Generate unique names for this session */
	char *alias = psprintf("pg_ducklake_next_%d", MyProcPid);
	char *metadata_path = psprintf("%s.ducklake", alias);
	char *data_path = psprintf("%s_data", alias);

	PG_TRY();
	{
		/* Install and load DuckLake extension in DuckDB */
		ExecuteDuckDBQuery("INSTALL ducklake");
		ExecuteDuckDBQuery("LOAD ducklake");

		/* Attach a DuckLake catalog */
		char *attach_query = psprintf(
		    "ATTACH 'ducklake:%s' AS %s (DATA_PATH '%s')",
		    metadata_path, alias, data_path);
		ExecuteDuckDBQuery(attach_query);
		pfree(attach_query);

		/* DuckLake operations: create table, insert, select */
		char *create_query = psprintf(
		    "CREATE TABLE %s.verify_table (i INTEGER)", alias);
		ExecuteDuckDBQuery(create_query);
		pfree(create_query);

		char *insert_query = psprintf(
		    "INSERT INTO %s.verify_table VALUES (1), (2)", alias);
		ExecuteDuckDBQuery(insert_query);
		pfree(insert_query);

		char *select_query = psprintf(
		    "SELECT i FROM %s.verify_table ORDER BY i", alias);
		ExecuteDuckDBQuery(select_query);
		pfree(select_query);

		/* Detach the catalog */
		char *detach_query = psprintf("DETACH %s", alias);
		ExecuteDuckDBQuery(detach_query);
		pfree(detach_query);
	}
	PG_CATCH();
	{
		SPI_finish();
		pfree(alias);
		pfree(metadata_path);
		pfree(data_path);
		PG_RE_THROW();
	}
	PG_END_TRY();

	/* Clean up */
	SPI_finish();
	pfree(alias);
	pfree(metadata_path);
	pfree(data_path);

	PG_RETURN_TEXT_P(cstring_to_text(
	    "ok: referenced pg_duckdb C interface and executed "
	    "DuckLake operations via duckdb.raw_query()"));
}
