-- Example: Custom Metadata Manager Extension
-- This shows how to use DuckLake's metadata manager registration API

-- Function to register a custom metadata manager
CREATE FUNCTION register_custom_metadata_manager()
RETURNS text
LANGUAGE C
AS 'MODULE_PATHNAME', 'register_custom_metadata_manager';

COMMENT ON FUNCTION register_custom_metadata_manager() IS
'Registers a custom JSON-based metadata manager with DuckLake using DuckLakeMetadataManager::Register()';

-- Function to test the custom metadata manager
CREATE FUNCTION test_custom_metadata_manager()
RETURNS text
LANGUAGE C
AS 'MODULE_PATHNAME', 'test_custom_metadata_manager';

COMMENT ON FUNCTION test_custom_metadata_manager() IS
'Tests using the custom metadata manager by attaching a DuckLake catalog with METADATA_TYPE ''json''';

-- Example usage:
-- SELECT register_custom_metadata_manager();
-- SELECT duckdb.raw_query('ATTACH ''ducklake:/tmp/test.db'' AS json_catalog (METADATA_TYPE ''json'')');
