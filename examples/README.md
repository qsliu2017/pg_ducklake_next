# DuckLake C++ API Integration Examples

This directory contains examples demonstrating how to use DuckLake's C++ API, specifically `DuckLakeMetadataManager::Register`, from a PostgreSQL extension.

## Important Note

⚠️ **These examples are CONCEPTUAL and will not compile without a custom build setup.** They demonstrate the pattern and API usage, not a working out-of-the-box solution.

## Files

- `custom_metadata_manager.cpp` - Example custom metadata manager implementation
- `custom_metadata_manager--1.0.sql` - SQL definitions for the extension
- `Makefile.example` - Example Makefile showing required build flags
- This README

## Prerequisites

To actually build and use these examples, you need:

### 1. Build DuckDB with DuckLake Static Extension

```bash
cd third_party/pg_duckdb/third_party/duckdb

# Configure DuckDB to include ducklake statically
cmake -DCMAKE_BUILD_TYPE=Release \
      -DEXTERNAL_EXTENSION_DIRECTORIES=../../../../ducklake \
      -DBUILD_EXTENSIONS="ducklake" \
      -DEXTENSION_STATIC_BUILD=1 \
      -DCMAKE_INSTALL_PREFIX=/path/to/install \
      .

make -j4
make install
```

### 2. Rebuild pg_duckdb Against Custom DuckDB

```bash
cd third_party/pg_duckdb

# Clean previous build
make clean

# Rebuild with custom DuckDB
PKG_CONFIG_PATH=/path/to/install/lib/pkgconfig \
make PG_CONFIG=/path/to/pg_config install
```

### 3. Build This Example Extension

```bash
cd examples

# Copy example Makefile
cp Makefile.example Makefile

# Check dependencies
make check-deps

# Build (will fail without proper setup)
make
make install
```

## API Usage: DuckLakeMetadataManager::Register

### Function Signature

```cpp
namespace duckdb {

class DuckLakeMetadataManager {
public:
    // Factory function type for creating metadata managers
    typedef unique_ptr<DuckLakeMetadataManager> (*create_t)(
        DuckLakeTransaction &transaction);

    // Register a custom metadata manager
    static void Register(const string &name, create_t create);

    // Create a metadata manager for a transaction
    static unique_ptr<DuckLakeMetadataManager> Create(
        DuckLakeTransaction &transaction);
};

} // namespace duckdb
```

### Example: Registering a Custom Backend

```cpp
// 1. Implement DuckLakeMetadataManager
class MyMetadataManager : public duckdb::DuckLakeMetadataManager {
public:
    explicit MyMetadataManager(duckdb::DuckLakeTransaction &txn)
        : DuckLakeMetadataManager(txn) {}

    // Override virtual methods (50+ total)
    void Initialize() override;
    void LoadCatalogInfo(duckdb::DuckLakeCatalogInfo &info) override;
    // ... etc

    static duckdb::unique_ptr<duckdb::DuckLakeMetadataManager>
    Create(duckdb::DuckLakeTransaction &txn) {
        return duckdb::make_uniq<MyMetadataManager>(txn);
    }
};

// 2. Register during extension initialization
extern "C" void _PG_init(void) {
    duckdb::DuckLakeMetadataManager::Register("my_backend",
        &MyMetadataManager::Create);
}

// 3. Use in SQL
// ATTACH 'ducklake:/path/to/catalog' AS my_catalog (METADATA_TYPE 'my_backend');
```

## Pre-Registered Metadata Managers

DuckLake comes with these metadata managers pre-registered:

| Name | Implementation | Purpose |
|------|----------------|---------|
| `postgres` | `PostgresMetadataManager` | PostgreSQL database backend |
| `postgres_scanner` | `PostgresMetadataManager` | PostgreSQL scanner integration |
| `sqlite` | `SQLiteMetadataManager` | SQLite database backend |
| `sqlite_scanner` | `SQLiteMetadataManager` | SQLite scanner integration |

## Virtual Methods to Implement

A custom metadata manager must implement approximately 50 virtual methods:

### Core Initialization
- `Initialize()` - Set up the backend
- `LoadCatalogInfo(DuckLakeCatalogInfo &)` - Load catalog metadata

### Catalog Operations
- `GetCatalogInfo()` - Retrieve catalog information
- `LoadSchemas()` - Load schema definitions
- `LoadTables()` - Load table definitions

### Schema Management
- `CreateSchema()`, `DropSchema()`
- `CreateTable()`, `DropTable()`
- `CreateView()`, `DropView()`

### Transaction Support
- `BeginTransaction()`, `CommitTransaction()`, `RollbackTransaction()`
- `GetSnapshot()` - Get transaction snapshot

### Data File Management
- `RegisterDataFile()`, `DeleteDataFile()`
- `GetDataFiles()` - List files for a table

### Statistics & Metadata
- `UpdateTableStats()` - Update table statistics
- `GetPartitionData()` - Retrieve partition information

### And many more...

See `third_party/ducklake/src/include/storage/ducklake_metadata_manager.hpp` for the full interface.

## Why Is This Complex?

1. **Header Dependencies**: DuckLake headers require DuckDB internal headers
2. **Build Integration**: Need to build DuckDB, ducklake, and pg_duckdb in correct order
3. **Symbol Visibility**: Must ensure symbols are exported correctly
4. **Version Compatibility**: All components must use compatible versions

## Alternative: Use SQL Interface

For most use cases, using the SQL interface is much simpler:

```cpp
// Instead of C++ API:
duckdb::DuckLakeMetadataManager::Register("json", &JSONMetadataManager::Create);

// Use SQL via duckdb.raw_query():
ExecuteDuckDBQuery("INSTALL ducklake");
ExecuteDuckDBQuery("LOAD ducklake");
ExecuteDuckDBQuery("ATTACH 'ducklake:metadata.db' AS catalog ...");
```

See `../src/pg_ducklake_next.cpp` for a working example using the SQL interface.

## When to Use C++ API

Only use the C++ API if you need to:
- ✅ Implement custom metadata storage backends (Redis, Cassandra, etc.)
- ✅ Extend ducklake's internal behavior
- ✅ Performance-critical direct access to metadata structures

Otherwise, stick with the SQL interface for simplicity.

## Support

These examples are for demonstration purposes. For production use, consider:
1. Working with the DuckDB/DuckLake maintainers to understand supported extension patterns
2. Using the SQL interface for standard operations
3. Contributing your custom metadata manager back to the ducklake project

## References

- DuckLake GitHub: https://github.com/duckdb/ducklake
- DuckDB Extensions: https://duckdb.org/docs/extensions/overview
- pg_duckdb: https://github.com/duckdb/pg_duckdb
