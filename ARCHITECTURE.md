# Architecture: pg_ducklake_next Extension

This document provides a visual overview of how the extension integrates with PostgreSQL, pg_duckdb, and DuckLake.

## Component Stack

```
┌─────────────────────────────────────────────────────────────┐
│                       PostgreSQL                             │
│  ┌───────────────────────────────────────────────────────┐  │
│  │            pg_ducklake_next Extension                  │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │   pg_ducklake_next_verify()                     │  │  │
│  │  │   - References RegisterDuckdbTableAm()          │  │  │
│  │  │   - Calls duckdb.raw_query() via SPI            │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  │                        ↓                              │  │
│  │                  Uses SPI                             │  │
│  └───────────────────────────────────────────────────────┘  │
│                         ↓                                    │
│  ┌───────────────────────────────────────────────────────┐  │
│  │               pg_duckdb Extension                      │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │  C Interface (exported symbols)                  │  │  │
│  │  │  - RegisterDuckdbTableAm()                       │  │  │
│  │  │  - _PG_init()                                    │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │  SQL Interface (via SPI)                         │  │  │
│  │  │  - duckdb.raw_query(query)                       │  │  │
│  │  │  - duckdb.install_extension(name)                │  │  │
│  │  │  - duckdb.load_extension(name)                   │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  │                        ↓                              │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │        Embedded DuckDB Engine                    │  │  │
│  │  │  - Query execution                               │  │  │
│  │  │  - Extension loading                             │  │  │
│  │  │  - Transaction management                        │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────┘  │
│                         ↓                                    │
│           LOAD ducklake (dynamic extension)                  │
│                         ↓                                    │
│  ┌───────────────────────────────────────────────────────┐  │
│  │            DuckLake Extension                          │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │  Storage Engine                                  │  │  │
│  │  │  - DuckLakeCatalog                               │  │  │
│  │  │  - DuckLakeTransaction                           │  │  │
│  │  │  - Table/Schema/View management                  │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  │  ┌─────────────────────────────────────────────────┐  │  │
│  │  │  Metadata Manager (pluggable)                    │  │  │
│  │  │  - DuckLakeMetadataManager::Register()           │  │  │
│  │  │  - PostgresMetadataManager                       │  │  │
│  │  │  - SQLiteMetadataManager                         │  │  │
│  │  │  - [Custom backends...]                          │  │  │
│  │  └─────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

## Data Flow: SQL Interface (Current Implementation)

```
┌──────────┐
│   User   │
└────┬─────┘
     │ SELECT pg_ducklake_next_verify();
     ↓
┌────────────────────────────┐
│  pg_ducklake_next          │
│  Extension Function        │
└────┬───────────────────────┘
     │ SPI_execute("SELECT duckdb.raw_query('INSTALL ducklake')")
     ↓
┌────────────────────────────┐
│  PostgreSQL SPI Layer      │
└────┬───────────────────────┘
     │ Function call: duckdb.raw_query()
     ↓
┌────────────────────────────┐
│  pg_duckdb Extension       │
│  SQL Function Handler      │
└────┬───────────────────────┘
     │ DuckDB C++ API call
     ↓
┌────────────────────────────┐
│  DuckDB Engine             │
│  Query Execution           │
└────┬───────────────────────┘
     │ INSTALL ducklake / LOAD ducklake
     ↓
┌────────────────────────────┐
│  DuckDB Extension Loader   │
│  Loads ducklake.duckdb_ext │
└────┬───────────────────────┘
     │ Extension registered
     ↓
┌────────────────────────────┐
│  DuckLake Extension        │
│  Now available for use     │
└────────────────────────────┘
```

## Symbol Resolution Flow

### Current Implementation (SQL Interface)

```
Compilation Time:
┌────────────────────────────┐
│  pg_ducklake_next.cpp      │
│  - Declares extern symbol  │
│  - No direct calls         │
└────────┬───────────────────┘
         │ -Wl,-undefined,dynamic_lookup
         ↓
┌────────────────────────────┐
│  pg_ducklake_next.dylib    │
│  (51KB, symbols unresolved)│
└────────────────────────────┘

Runtime (PostgreSQL startup):
┌────────────────────────────┐
│  PostgreSQL loads pg_duckdb│
│  via shared_preload_libs   │
└────────┬───────────────────┘
         │ Exports C symbols
         ↓
┌────────────────────────────┐
│  RegisterDuckdbTableAm()   │
│  symbol now available      │
└────────┬───────────────────┘
         │
         ↓
┌────────────────────────────┐
│  Load pg_ducklake_next     │
│  Symbols resolve to pg_duckdb
└────────────────────────────┘
```

### Alternative: C++ API (Static Compilation)

```
Compilation Time:
┌────────────────────────────┐
│  Build DuckDB with         │
│  ducklake static           │
└────────┬───────────────────┘
         │
         ↓
┌────────────────────────────┐
│  Build pg_duckdb against   │
│  custom DuckDB             │
└────────┬───────────────────┘
         │
         ↓
┌────────────────────────────┐
│  custom_extension.cpp      │
│  #include "ducklake_*.hpp" │
│  - Direct C++ calls        │
└────────┬───────────────────┘
         │ Link against pg_duckdb
         ↓
┌────────────────────────────┐
│  custom_extension.dylib    │
│  (large, all symbols)      │
└────────────────────────────┘

Result: Can call DuckLakeMetadataManager::Register()
```

## Interface Comparison

### Approach 1: SQL Interface (Implemented)

**Pros**:
- ✅ Simple implementation
- ✅ No header conflicts
- ✅ Lightweight (51KB)
- ✅ Stable API

**Cons**:
- ❌ No C++ symbol access
- ❌ SQL overhead

**Code Pattern**:
```cpp
extern bool RegisterDuckdbTableAm(...);  // For linkage

void ExecuteDuckDBQuery(const char *query) {
    char *sql = psprintf("SELECT duckdb.raw_query(%s)",
                         quote_literal_cstr(query));
    SPI_execute(sql, false, 0);
}

// Usage
ExecuteDuckDBQuery("INSTALL ducklake");
ExecuteDuckDBQuery("ATTACH 'ducklake:...'");
```

### Approach 2: C++ API (Conceptual)

**Pros**:
- ✅ Direct C++ access
- ✅ Can extend internals
- ✅ No SQL overhead

**Cons**:
- ❌ Complex build
- ❌ Header conflicts
- ❌ Large binary
- ❌ Maintenance burden

**Code Pattern**:
```cpp
#include "storage/ducklake_metadata_manager.hpp"

class CustomManager : public duckdb::DuckLakeMetadataManager {
    // Implement 50+ virtual methods
};

void Register() {
    duckdb::DuckLakeMetadataManager::Register(
        "custom", &CustomManager::Create);
}
```

## Dependencies

```
pg_ducklake_next
    ├── Depends on (requires): pg_duckdb
    │   └── Contains: DuckDB engine
    │       └── Can load: ducklake extension
    │
    └── References:
        ├── C Symbol: RegisterDuckdbTableAm (pg_duckdb)
        └── SQL Function: duckdb.raw_query (pg_duckdb)
```

## Build Artifacts

```
src/pg_ducklake_next.cpp  (109 lines)
    ↓ Compile with g++ -std=c++17
    ↓ Include: third_party/pg_duckdb/include
    ↓ Link: -Wl,-undefined,dynamic_lookup
    ↓
pg_ducklake_next.dylib (51KB)
    ↓ Install to $PG_LIB/postgresql/
    ↓
sql/pg_ducklake_next--0.1.0.sql
    ↓ Install to $PG_SHAREDIR/extension/
    ↓
CREATE EXTENSION pg_ducklake_next;
    ↓ Loads extension
    ↓
SELECT pg_ducklake_next_verify();
    ↓ Executes verification
    ↓ Returns: "ok: ..."
```

## Memory Layout (Runtime)

```
PostgreSQL Process
├── Shared Libraries
│   ├── pg_duckdb.dylib (944KB)
│   │   ├── DuckDB engine
│   │   ├── C interface exports
│   │   └── SQL function handlers
│   │
│   └── pg_ducklake_next.dylib (51KB)
│       ├── References pg_duckdb symbols
│       └── Extension functions
│
├── DuckDB Process State
│   ├── Connections
│   ├── Catalogs
│   └── Loaded Extensions
│       └── ducklake (loaded via LOAD command)
│           ├── Storage engine
│           └── Metadata managers
│
└── PostgreSQL State
    ├── Extension metadata
    └── Function registrations
```

## Conclusion

The current architecture demonstrates the **clean separation of concerns**:

1. **PostgreSQL**: Extension framework and SQL layer
2. **pg_duckdb**: Bridge between PostgreSQL and DuckDB
3. **DuckDB**: Query engine and extension system
4. **DuckLake**: Storage extension loaded dynamically
5. **pg_ducklake_next**: Minimal wrapper using stable interfaces

This layered approach provides **flexibility** (can swap components) and **simplicity** (clear boundaries between layers).
