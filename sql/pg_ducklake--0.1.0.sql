CREATE SCHEMA ducklake;

GRANT USAGE ON SCHEMA ducklake TO PUBLIC;

CREATE FUNCTION ducklake._am_handler(internal)
    RETURNS table_am_handler
    SET search_path = pg_catalog, pg_temp
    AS 'MODULE_PATHNAME', 'ducklake_am_handler'
    LANGUAGE C;

CREATE ACCESS METHOD ducklake
    TYPE TABLE
    HANDLER ducklake._am_handler;

CREATE FUNCTION pg_ducklake_next_verify()
RETURNS text
LANGUAGE C
AS 'MODULE_PATHNAME', 'pg_ducklake_next_verify';

COMMENT ON FUNCTION pg_ducklake_next_verify() IS
'Verifies pg_duckdb dependency by referencing its C interface, installs/loads DuckLake in DuckDB via duckdb.raw_query(), then executes DuckLake operations.';
