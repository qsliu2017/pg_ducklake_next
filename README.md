# pg_ducklake_next

Minimal PostgreSQL extension that depends on `pg_duckdb` and verifies DuckLake usage through `pg_duckdb`.

## What this demonstrates

This extension shows the **elegant approach** for building PostgreSQL extensions on top of pg_duckdb:

1. **Dependency declaration**: `requires = 'pg_duckdb'` in `pg_ducklake_next.control`
2. **C interface linkage**: References pg_duckdb's exported C symbol `RegisterDuckdbTableAm`
3. **DuckDB interaction**: Uses `duckdb.raw_query()` SQL function via SPI (the official interface)
4. **DuckLake operations**: Installs DuckLake and executes attach/create/insert/select operations

### Why this approach?

pg_duckdb intentionally **does not export C++ symbols** (compiled with `-fvisibility=hidden`). This is by design:
- **C interface**: For extension registration (`RegisterDuckdbTableAm`)
- **SQL interface**: For DuckDB operations (`duckdb.raw_query()`)

This keeps the API surface clean and stable.

## Repository layout

- `third_party/pg_duckdb` (git submodule) - PostgreSQL + DuckDB integration
- `third_party/ducklake` (git submodule) - DuckLake storage extension for DuckDB
- `src/pg_ducklake_next.cpp` - Main extension implementation (SQL interface approach)
- `examples/` - Advanced examples showing C++ API usage (see `DUCKLAKE_INTEGRATION.md`)
- `DUCKLAKE_INTEGRATION.md` - Comprehensive guide to different integration approaches

## Build requirements

- PostgreSQL 14+ installed
- C++17 compatible compiler
- pg_duckdb submodule initialized and built

## Build with local pg-17

```bash
# Initialize submodules
git submodule update --init --recursive

# Build and install pg_duckdb first
make -C third_party/pg_duckdb PG_CONFIG=$PWD/pg-17/bin/pg_config install -j4

# Build pg_ducklake_next
make PG_CONFIG=$PWD/pg-17/bin/pg_config

# Install pg_ducklake_next
make install PG_CONFIG=$PWD/pg-17/bin/pg_config
```

## Technical notes

### pg_duckdb's exported interface

pg_duckdb provides two interfaces for extensions:
- **C interface**: `RegisterDuckdbTableAm()` - for table access method registration
- **SQL interface**: `duckdb.raw_query()` - for executing DuckDB queries

C++ symbols from the `pgduckdb::` namespace are intentionally not exported (visibility is hidden).

### Dynamic symbol resolution

On macOS, the extension uses `-Wl,-undefined,dynamic_lookup` to allow `RegisterDuckdbTableAm` to be resolved at runtime when pg_duckdb is loaded. This avoids static linking and keeps the extension lightweight.

### No header conflicts

Since we only use pg_duckdb's C interface and don't include DuckDB headers, there are no macro conflicts with PostgreSQL headers (FATAL, Min, Max, etc.).

## End-to-end verification

`pg_duckdb` must be installed and loaded via `shared_preload_libraries`.

```sql
CREATE EXTENSION pg_duckdb;
CREATE EXTENSION pg_ducklake_next;
SELECT pg_ducklake_next_verify();
```

Expected result:

```text
ok: referenced pg_duckdb C interface and executed DuckLake operations via duckdb.raw_query()
```

## Advanced: Using DuckLake C++ API

Want to access DuckLake's C++ symbols like `DuckLakeMetadataManager::Register`?

See **[DUCKLAKE_INTEGRATION.md](DUCKLAKE_INTEGRATION.md)** for:
- ✅ 4 different integration approaches
- ✅ Trade-offs and recommendations
- ✅ Example custom metadata manager implementation
- ✅ Build instructions for C++ API access

**Quick Summary:**
- **Standard use case** (this repo): Use SQL interface via `duckdb.raw_query()` ✅
- **Advanced use case** (custom metadata backends): Requires rebuilding DuckDB with ducklake statically compiled
- **See `examples/` directory** for custom metadata manager code examples
