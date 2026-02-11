# DuckLake Integration Approaches

This document explores different approaches for integrating ducklake C++ symbols (specifically `DuckLakeMetadataManager::Register`) into a PostgreSQL extension built on top of pg_duckdb.

## Challenge

DuckLake is a DuckDB extension with the following characteristics:
- **Location**: `third_party/ducklake/`
- **Build System**: CMake-based, uses DuckDB's extension build functions
- **Target Symbol**: `DuckLakeMetadataManager::Register` (in `src/storage/ducklake_metadata_manager.cpp`)
- **Headers**: 48 public headers in `src/include/` with heavy DuckDB dependencies

## The Problem

1. **Symbol Visibility**: When ducklake is loaded via `INSTALL ducklake; LOAD ducklake`, it's a dynamically loaded plugin inside DuckDB. Its C++ symbols are not exposed outside DuckDB's process space.

2. **Header Dependencies**: Ducklake headers depend on DuckDB internal headers, which conflict with PostgreSQL headers (FATAL, Min, Max, etc.).

3. **Linking Complexity**: Linking directly against ducklake requires linking against the full DuckDB library, creating a heavyweight dependency.

## Approach 1: SQL Interface (Current - Elegant & Practical)

**Status**: ✅ Implemented in `src/pg_ducklake_next.cpp`

**How it Works**:
```cpp
// Use duckdb.raw_query() via SPI
ExecuteDuckDBQuery("INSTALL ducklake");
ExecuteDuckDBQuery("LOAD ducklake");
ExecuteDuckDBQuery("ATTACH 'ducklake:metadata.db' AS my_catalog ...");
```

**Pros**:
- ✅ Clean and simple
- ✅ No header conflicts
- ✅ Lightweight (51KB extension)
- ✅ Stable SQL API

**Cons**:
- ❌ No access to C++ symbols like `DuckLakeMetadataManager::Register`
- ❌ Limited to SQL operations

**Use Case**: Best for standard ducklake operations (attach, create, insert, query).

---

## Approach 2: Static Compilation into DuckDB

**Status**: ⚠️ Requires rebuilding DuckDB and pg_duckdb

**How it Works**:
1. Modify DuckDB build to include ducklake statically
2. Rebuild pg_duckdb against this custom DuckDB
3. Include ducklake headers in our extension
4. Reference ducklake symbols directly

**Build Steps**:
```bash
# 1. Configure DuckDB with ducklake as static extension
cd third_party/pg_duckdb/third_party/duckdb
cmake -DCMAKE_BUILD_TYPE=Release \
      -DEXTERNAL_EXTENSION_DIRECTORIES=../../../../ducklake \
      -DBUILD_EXTENSIONS="ducklake" \
      .

# 2. Rebuild pg_duckdb
cd ../../
make clean && make

# 3. Update our Makefile to include ducklake headers
override PG_CPPFLAGS += -I$(CURDIR)/third_party/ducklake/src/include
```

**Code Example**:
```cpp
// Include ducklake headers (would need DuckDB headers first)
#include "duckdb.hpp"
#include "storage/ducklake_metadata_manager.hpp"
#include "storage/ducklake_transaction.hpp"

extern "C" Datum my_custom_function(PG_FUNCTION_ARGS) {
    // Access DuckDB connection from pg_duckdb
    auto conn = pgduckdb::DuckDBManager::GetConnection();

    // Register custom metadata manager
    duckdb::DuckLakeMetadataManager::Register("my_backend",
        [](duckdb::DuckLakeTransaction &txn) {
            return std::make_unique<MyMetadataManager>(txn);
        });

    PG_RETURN_VOID();
}
```

**Pros**:
- ✅ Full access to ducklake C++ API
- ✅ Can register custom metadata managers
- ✅ Direct function calls (no SQL overhead)

**Cons**:
- ❌ Requires custom DuckDB build
- ❌ Complex build process
- ❌ Header conflict management needed
- ❌ Large binary size
- ❌ Maintenance burden

**Use Case**: When you need to extend ducklake with custom metadata backends or modify its internals.

---

## Approach 3: Shared Library with Symbol Export

**Status**: ⚠️ Requires custom ducklake build

**How it Works**:
1. Build ducklake as a shared library (not a DuckDB extension)
2. Export specific symbols with visibility attributes
3. Link our extension against ducklake.so

**Ducklake Modifications**:
```cmake
# Modified CMakeLists.txt
add_library(ducklake_shared SHARED ${EXTENSION_SOURCES})
set_target_properties(ducklake_shared PROPERTIES
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF)

# Export specific symbols
target_compile_definitions(ducklake_shared PRIVATE
    DUCKLAKE_EXPORT=__attribute__((visibility("default"))))
```

**Header Wrapper** (`ducklake_c_api.h`):
```cpp
// C API wrapper to avoid header conflicts
extern "C" {
    DUCKLAKE_EXPORT void ducklake_register_metadata_manager(
        const char *name,
        void *create_fn);

    DUCKLAKE_EXPORT void* ducklake_get_metadata_manager(
        void *transaction);
}
```

**Our Extension**:
```cpp
extern "C" {
#include "postgres.h"
// ... postgres headers
}

// Include only the C API wrapper
#include "ducklake_c_api.h"

extern "C" Datum my_function(PG_FUNCTION_ARGS) {
    ducklake_register_metadata_manager("my_backend", my_create_fn);
    PG_RETURN_VOID();
}
```

**Makefile**:
```makefile
override PG_CPPFLAGS += -I$(CURDIR)/third_party/ducklake/c_api
SHLIB_LINK += -L$(CURDIR)/third_party/ducklake -lducklake_shared
```

**Pros**:
- ✅ Access to ducklake symbols
- ✅ Cleaner than static compilation
- ✅ No PostgreSQL header conflicts (with C wrapper)

**Cons**:
- ❌ Requires ducklake modifications
- ❌ Need to maintain C API wrapper
- ❌ Symbol versioning management

**Use Case**: When you need some C++ API access but want to keep clean boundaries.

---

## Approach 4: IPC/Socket Communication

**Status**: 💡 Conceptual

**How it Works**:
1. Create a separate DuckDB process with ducklake loaded
2. Communicate via Unix socket or shared memory
3. Send commands and receive responses

**Not Recommended**: Too complex for this use case.

---

## Recommendation

For most use cases: **Use Approach 1 (SQL Interface)**

Only consider Approach 2 or 3 if you absolutely need:
- Custom metadata manager implementations
- Performance-critical direct API access
- Extension of ducklake's internal behaviors

## Example: Custom Metadata Manager (Approach 2)

If you do need to register a custom metadata manager, here's the full pattern:

```cpp
// custom_metadata_manager.hpp
#pragma once
#include "storage/ducklake_metadata_manager.hpp"

namespace duckdb {

class RedisMetadataManager : public DuckLakeMetadataManager {
public:
    explicit RedisMetadataManager(DuckLakeTransaction &transaction);
    ~RedisMetadataManager() override;

    // Implement virtual methods
    void Initialize() override;
    void LoadCatalogInfo(DuckLakeCatalogInfo &info) override;
    // ... 50+ more methods

    static unique_ptr<DuckLakeMetadataManager> Create(
        DuckLakeTransaction &transaction);
};

} // namespace duckdb
```

```cpp
// custom_metadata_manager.cpp
#include "custom_metadata_manager.hpp"
#include <hiredis/hiredis.h>

namespace duckdb {

RedisMetadataManager::RedisMetadataManager(DuckLakeTransaction &transaction)
    : DuckLakeMetadataManager(transaction) {
    // Connect to Redis
}

void RedisMetadataManager::Initialize() {
    // Initialize Redis schema
}

void RedisMetadataManager::LoadCatalogInfo(DuckLakeCatalogInfo &info) {
    // Load from Redis
}

unique_ptr<DuckLakeMetadataManager>
RedisMetadataManager::Create(DuckLakeTransaction &transaction) {
    return make_uniq<RedisMetadataManager>(transaction);
}

// Registration helper
void RegisterRedisMetadataManager() {
    DuckLakeMetadataManager::Register("redis",
        &RedisMetadataManager::Create);
}

} // namespace duckdb
```

```cpp
// pg_extension.cpp
extern "C" void _PG_init(void) {
    duckdb::RegisterRedisMetadataManager();
}

extern "C" Datum test_redis_backend(PG_FUNCTION_ARGS) {
    ExecuteDuckDBQuery("INSTALL ducklake");
    ExecuteDuckDBQuery("LOAD ducklake");
    ExecuteDuckDBQuery(
        "ATTACH 'ducklake:redis://localhost' AS my_catalog");
    PG_RETURN_VOID();
}
```

## Conclusion

The current implementation (Approach 1) is the **elegant solution** for standard ducklake usage. Only move to more complex approaches if you have specific requirements that cannot be met through the SQL interface.

For accessing `DuckLakeMetadataManager::Register`, you would need Approach 2 (static compilation) or Approach 3 (shared library with exports).
