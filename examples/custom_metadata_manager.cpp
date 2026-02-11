/*
 * Example: Custom Metadata Manager for DuckLake
 *
 * This demonstrates how to register a custom metadata manager with DuckLake
 * using the DuckLakeMetadataManager::Register API.
 *
 * REQUIREMENTS:
 * - DuckDB built with ducklake as a static extension
 * - pg_duckdb built against that custom DuckDB
 * - This extension built with -I to ducklake headers
 *
 * This file shows the CONCEPT - it won't compile without the proper build setup.
 */

#ifdef ENABLE_DUCKLAKE_CPP_API

// Include DuckDB and DuckLake headers (in this order!)
#include "duckdb.hpp"
#include "duckdb/main/connection.hpp"
#include "storage/ducklake_metadata_manager.hpp"
#include "storage/ducklake_transaction.hpp"
#include "storage/ducklake_catalog.hpp"

// NOW include PostgreSQL headers
extern "C" {
#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(register_custom_metadata_manager);
PG_FUNCTION_INFO_V1(test_custom_metadata_manager);
}

namespace custom {

/*
 * Example custom metadata manager that uses JSON files
 */
class JSONMetadataManager : public duckdb::DuckLakeMetadataManager {
public:
	explicit JSONMetadataManager(duckdb::DuckLakeTransaction &transaction)
	    : DuckLakeMetadataManager(transaction) {
		// Initialize JSON backend
		json_path = "/tmp/ducklake_metadata.json";
	}

	~JSONMetadataManager() override = default;

	void Initialize() override {
		// Create JSON file if it doesn't exist
		ereport(NOTICE, (errmsg("Initializing JSON metadata manager at %s", json_path.c_str())));
	}

	void LoadCatalogInfo(duckdb::DuckLakeCatalogInfo &info) override {
		// Load catalog info from JSON
		ereport(NOTICE, (errmsg("Loading catalog info from JSON")));
		// ... JSON parsing logic
	}

	// ... Implement other required virtual methods

	static duckdb::unique_ptr<duckdb::DuckLakeMetadataManager>
	Create(duckdb::DuckLakeTransaction &transaction) {
		return duckdb::make_uniq<JSONMetadataManager>(transaction);
	}

private:
	std::string json_path;
};

} // namespace custom

/*
 * Register our custom metadata manager with DuckLake
 */
extern "C" Datum
register_custom_metadata_manager(PG_FUNCTION_ARGS) {
	try {
		// Register the JSON metadata manager
		duckdb::DuckLakeMetadataManager::Register("json",
		    &custom::JSONMetadataManager::Create);

		ereport(NOTICE,
		    (errmsg("Registered 'json' metadata manager with DuckLake")));

		PG_RETURN_TEXT_P(cstring_to_text("ok: registered json metadata manager"));
	} catch (const std::exception &ex) {
		ereport(ERROR,
		    (errmsg("Failed to register metadata manager: %s", ex.what())));
	}
}

/*
 * Test using the custom metadata manager
 */
extern "C" Datum
test_custom_metadata_manager(PG_FUNCTION_ARGS) {
	// This would require accessing pg_duckdb's DuckDB connection
	// and creating a DuckLake catalog with metadata_type='json'

	// Conceptual usage:
	// ATTACH 'ducklake:/tmp/my_catalog' AS test_catalog (METADATA_TYPE 'json');

	PG_RETURN_TEXT_P(cstring_to_text("ok: use ATTACH with METADATA_TYPE 'json'"));
}

#else /* !ENABLE_DUCKLAKE_CPP_API */

/*
 * Fallback when ducklake C++ API is not available
 */
extern "C" {
#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;
PG_FUNCTION_INFO_V1(register_custom_metadata_manager);
PG_FUNCTION_INFO_V1(test_custom_metadata_manager);
}

extern "C" Datum
register_custom_metadata_manager(PG_FUNCTION_ARGS) {
	ereport(ERROR,
	    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
	     errmsg("DuckLake C++ API not available"),
	     errhint("Rebuild with ENABLE_DUCKLAKE_CPP_API and proper dependencies")));
}

extern "C" Datum
test_custom_metadata_manager(PG_FUNCTION_ARGS) {
	ereport(ERROR,
	    (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
	     errmsg("DuckLake C++ API not available")));
}

#endif /* ENABLE_DUCKLAKE_CPP_API */
