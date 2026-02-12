/*
 * pg_ducklake_pg.cpp — PostgreSQL-facing translation unit.
 *
 * This file includes PostgreSQL headers but NEVER DuckDB or ducklake headers.
 * All DuckDB interaction goes through the C bridge in pg_ducklake_bridge.h.
 */
extern "C" {
#include "postgres.h"

#include "executor/spi.h"
#include "miscadmin.h"
#include "utils/builtins.h"

PG_FUNCTION_INFO_V1(pg_ducklake_next_verify);
}

#include "pg_ducklake_bridge.h"

/*
 * Ensure ducklake is loaded into DuckDB, then execute a query via the bridge.
 * Reports errors through PostgreSQL's ereport mechanism.
 */
static void
ExecuteDuckDBQuery(const char *query) {
	const char *errmsg = NULL;
	int ret;

	ducklake_ensure_loaded();

	ret = ducklake_execute_query(query, &errmsg);
	if (ret != 0) {
		ereport(ERROR,
		        (errmsg_internal("pg_ducklake_next: DuckDB query failed: %s",
		                         errmsg ? errmsg : "unknown error"),
		         errdetail("Query: %s", query)));
	}
}

extern "C" Datum
pg_ducklake_next_verify(PG_FUNCTION_ARGS) {
	/* Generate unique names for this session */
	char *alias = psprintf("pg_ducklake_next_%d", MyProcPid);
	char *metadata_path = psprintf("%s.ducklake", alias);
	char *data_path = psprintf("%s_data", alias);

	PG_TRY();
	{
		/*
		 * ducklake is already loaded statically via the bridge —
		 * no INSTALL/LOAD needed. Just ATTACH and use it.
		 */
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
		pfree(alias);
		pfree(metadata_path);
		pfree(data_path);
		PG_RE_THROW();
	}
	PG_END_TRY();

	pfree(alias);
	pfree(metadata_path);
	pfree(data_path);

	PG_RETURN_TEXT_P(cstring_to_text(
	    "ok: ducklake loaded statically into DuckDB, "
	    "executed DuckLake operations via C++ bridge"));
}
