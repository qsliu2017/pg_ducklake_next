-- Test script for pg_ducklake_next extension
-- Prerequisites:
--   1. pg_duckdb must be installed
--   2. shared_preload_libraries must include 'pg_duckdb'
--   3. pg_ducklake_next must be installed

-- Load extensions
CREATE EXTENSION IF NOT EXISTS pg_duckdb;
CREATE EXTENSION IF NOT EXISTS pg_ducklake_next;

-- Run verification function
-- This will:
--   1. Verify pg_duckdb symbol can be loaded
--   2. Install and load ducklake extension in DuckDB
--   3. Attach a DuckLake catalog
--   4. Create a table in DuckLake
--   5. Insert data and query it back
SELECT pg_ducklake_next_verify();
