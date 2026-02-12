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

## Prior Art: How pg_mooncake Does It

pg_mooncake (`../pg_mooncake/`) is a production project built on top of pg_duckdb with its own DuckDB extension (`duckdb_mooncake`). Studying its architecture reveals pragmatic solutions to the exact problems we face.

### Architecture Overview

pg_mooncake is a **three-component system**:

```
pg_mooncake  (Rust/pgrx PostgreSQL extension)
     │
     │  calls RegisterDuckdbTableAm() via extern "C" + dynamic_lookup
     ▼
pg_duckdb    (C++ PostgreSQL extension, git submodule)
     │
     │  embeds DuckDB engine; duckdb_mooncake installed from community repo at SQL time
     ▼
duckdb_mooncake  (C++ DuckDB extension, built & published separately)
     │
     │  calls back into pg_mooncake via dlsym(RTLD_DEFAULT, ...)
     ▼
pg_mooncake  (exports #[no_mangle] extern "C" functions)
```

### Key Finding 1: duckdb_mooncake is NOT built into DuckDB

Despite having the source in-tree, `duckdb_mooncake` is **not statically compiled** into DuckDB. The pg_duckdb build's `pg_duckdb_extensions.cmake` only loads `json`, `icu`, and `httpfs` — no mooncake.

Instead, `duckdb_mooncake` is installed at **SQL time** from DuckDB's community extension repository:

```sql
-- src/sql/bootstrap.sql (runs during CREATE EXTENSION pg_mooncake)
SELECT duckdb.install_extension('mooncake', 'community');
```

The `make duckdb_mooncake` target in the top-level Makefile is a **developer-only** target for building and publishing the extension to the community repo. It is not a dependency of `make install`.

**Actual flow**:
```
Build time:   pg_duckdb (C++, embeds DuckDB) → pg_mooncake (Rust/pgrx)
SQL time:     CREATE EXTENSION pg_mooncake
              → bootstrap.sql runs
              → duckdb.install_extension('mooncake', 'community')
              → downloads pre-built duckdb_mooncake binary from DuckDB community repo
```

### Key Finding 2: Cross-component communication uses C FFI only

pg_mooncake **never calls pg_duckdb's C++ API** (e.g., `pgduckdb::DuckDBQueryOrThrow`). All cross-component communication uses plain C interfaces:

**pg_mooncake → pg_duckdb** (standard direction):
```rust
// src/table.rs — declares pg_duckdb's exported C symbol
extern "C" {
    fn RegisterDuckdbTableAm(name: *const c_char, am: *const pg_sys::TableAmRoutine) -> bool;
}
```
Resolved at load time via `-Wl,-undefined,dynamic_lookup` (in `.cargo/config.toml`), since pg_duckdb is already loaded through `shared_preload_libraries`.

**duckdb_mooncake → pg_mooncake** (reverse callback via dlsym):
```cpp
// duckdb_mooncake/src/include/pgmooncake.hpp
static get_init_query_fn get_init_query =
    reinterpret_cast<get_init_query_fn>(dlsym(RTLD_DEFAULT, "pgmooncake_get_init_query"));
```
This lets the DuckDB extension call back into the PG extension to get PostgreSQL state (database name, LSN). The pg_mooncake side exports these with `#[no_mangle] pub extern "C"`:

```rust
// src/duckdb_mooncake.rs
#[no_mangle]
extern "C" fn pgmooncake_get_init_query() -> *mut c_char { ... }

#[no_mangle]
extern "C" fn pgmooncake_get_lsn() -> u64 { ... }
```

### Key Finding 3: dlsym is used only for the reverse callback

`dlsym(RTLD_DEFAULT, ...)` is **not** the standard PostgreSQL pattern for inter-extension calls. pg_mooncake uses it only in one specific case: duckdb_mooncake (a DuckDB plugin running inside DuckDB) needs to call back into pg_mooncake (a PostgreSQL extension). Since duckdb_mooncake cannot link against PostgreSQL or pg_mooncake, `dlsym` with `RTLD_DEFAULT` searches all symbols loaded in the process — which works because pg_mooncake is already loaded.

The standard PostgreSQL approach for resolving symbols from other extensions is `load_external_function()`:
```c
void *fn = load_external_function("$libdir/pg_duckdb", "RegisterDuckdbTableAm", false, NULL);
```

pg_mooncake skips this because Rust/pgrx + `-undefined,dynamic_lookup` makes `extern "C"` declarations simpler, and the symbol is guaranteed to exist since pg_duckdb is preloaded.

### Key Finding 4: Rust/pgrx sidesteps the header conflict problem

By writing the PostgreSQL extension in Rust (via pgrx), pg_mooncake completely avoids the C/C++ header conflict between PostgreSQL and DuckDB. PostgreSQL types come from `pgrx::pg_sys`, and all DuckDB interaction happens through C FFI or SQL — never through C++ headers.

### Key Finding 5: pg_duckdb's exported C interface is minimal

pg_duckdb deliberately exports very few symbols. The one that matters for extension developers is:

```c
// Marked with visibility("default") in pg_duckdb's source
extern "C" __attribute__((visibility("default"))) bool
RegisterDuckdbTableAm(const char *name, const TableAmRoutine *am);
```

Everything else (the entire `pgduckdb::` C++ namespace) is hidden behind `-fvisibility=hidden`. This is intentional — pg_duckdb's public API is:
- **C interface**: `RegisterDuckdbTableAm()` for table access method registration
- **SQL interface**: `duckdb.raw_query()`, `duckdb.install_extension()`, etc.

---

## Approaches for pg_ducklake_next

### Approach 1: SQL Interface (Current — Recommended)

**Status**: ✅ Implemented in `src/pg_ducklake_next.cpp`

Following the pg_mooncake pattern: install ducklake from a repository at SQL time, interact via SQL.

```cpp
// Use duckdb.raw_query() via SPI
ExecuteDuckDBQuery("INSTALL ducklake");
ExecuteDuckDBQuery("LOAD ducklake");
ExecuteDuckDBQuery("ATTACH 'ducklake:metadata.db' AS my_catalog ...");
```

**Pros**:
- Clean and simple, matching pg_mooncake's proven pattern
- No header conflicts
- Lightweight (51KB extension)
- Stable SQL API

**Cons**:
- No access to C++ symbols like `DuckLakeMetadataManager::Register`
- Limited to SQL operations

**Use Case**: Best for standard ducklake operations (attach, create, insert, query). This is what pg_mooncake does for all DuckDB interaction.

---

### Approach 2: Static Compilation into DuckDB

**Status**: ⚠️ Requires rebuilding DuckDB and pg_duckdb

Compile ducklake directly into DuckDB via pg_duckdb's `pg_duckdb_extensions.cmake`:

```cmake
# third_party/pg_duckdb/third_party/pg_duckdb_extensions.cmake
duckdb_extension_load(json)
duckdb_extension_load(icu)
duckdb_extension_load(ducklake
    SOURCE_DIR ../../ducklake
    LOAD_TESTS
)
```

Then include ducklake headers (before PostgreSQL headers) and reference symbols directly.

**Pros**:
- Full access to ducklake C++ API
- Can call `DuckLakeMetadataManager::Register` directly
- No runtime download needed

**Cons**:
- Requires custom DuckDB + pg_duckdb rebuild
- Must manage DuckDB ↔ PostgreSQL header conflicts (include order matters)
- Large binary size
- Maintenance burden: must rebuild when any component updates

**Note**: pg_mooncake explicitly chose NOT to do this for duckdb_mooncake.

---

### Approach 3: Separate DuckDB Extension + C FFI Callbacks

**Status**: ⚠️ Requires building a separate DuckDB extension

This follows pg_mooncake's actual architecture most closely:

1. Build ducklake as a standalone loadable DuckDB extension (it already is one)
2. If ducklake needs to call back into our PG extension, export `#[no_mangle] extern "C"` / `extern "C"` functions from the PG extension
3. ducklake (or a custom DuckDB extension wrapping ducklake) uses `dlsym(RTLD_DEFAULT, ...)` to find those functions at runtime
4. Install via `duckdb.install_extension()` at SQL time

```
pg_ducklake_next (PG extension)
    │ exports: extern "C" ducklake_next_get_config() via no_mangle
    │ calls:   RegisterDuckdbTableAm() via dynamic_lookup
    │ calls:   duckdb.install_extension('ducklake') via SPI
    ▼
pg_duckdb
    │ loads DuckDB engine
    ▼
ducklake (DuckDB extension, installed from repo)
    │ optionally calls back via dlsym(RTLD_DEFAULT, "ducklake_next_get_config")
    ▼
pg_ducklake_next (symbols resolved in process)
```

**Pros**:
- Follows the pg_mooncake production pattern exactly
- Clean C FFI boundaries, no header conflicts
- Each component built independently

**Cons**:
- Reverse callbacks via `dlsym` are fragile (not type-safe, no compile-time checks)
- Only works for functions the DuckDB extension needs to call back into PG
- Does not give arbitrary access to ducklake C++ internals

**Use Case**: When the DuckDB extension needs PostgreSQL state (e.g., current database name, LSN, configuration) to function properly.

---

### Approach 4: Shared Library with Exported C API

**Status**: ⚠️ Requires custom ducklake build

Build ducklake as a shared library with an explicit C API layer:

```c
// ducklake_c_api.h — thin C wrapper around C++ internals
extern "C" {
    void ducklake_register_metadata_manager(const char *name, void *create_fn);
    void *ducklake_create_metadata_manager(void *transaction);
}
```

**Pros**: Access to ducklake symbols without C++ header conflicts.
**Cons**: Requires ducklake modifications and ongoing maintenance of the C wrapper.

---

### Approach 5: Statically Embed Ducklake in the PG Extension

**Status**: 🔧 Prototype — Build setup ready, requires pg_duckdb + DuckDB to be built first

This approach compiles ducklake's C++ source directly into `pg_ducklake_next.so`, then registers it with DuckDB at runtime using DuckDB's `LoadStaticExtension<T>()` template. The result mirrors the ducklake-on-DuckDB relationship: **pg_ducklake is to pg_duckdb as ducklake is to DuckDB**.

#### Architecture

```
pg_ducklake_next.so
├── src/pg_ducklake_pg.cpp          PostgreSQL side (includes postgres.h)
│   └── _PG_init(), SQL functions, SPI calls
│   └── calls ducklake_ensure_loaded() via C linkage
│
├── src/pg_ducklake_duckdb.cpp      DuckDB side (includes duckdb.hpp + ducklake headers)
│   └── ducklake_ensure_loaded():
│          db.LoadStaticExtension<DucklakeExtension>()
│
├── ducklake sources (compiled in)  All 49 .cpp files from third_party/ducklake/src/
│   └── ducklake_extension.cpp, ducklake_catalog.cpp, ...
│
└── include/pg_ducklake_bridge.h    Pure C bridge header (no PG or DuckDB includes)
    └── extern "C" void ducklake_ensure_loaded(void);
```

#### The Separate Translation Unit Pattern

PostgreSQL and DuckDB headers cannot coexist in the same translation unit due to macro conflicts (`FATAL`, `ERROR`, `Min`, `Max`, etc.). The solution: never mix them.

| File | Includes postgres.h | Includes duckdb.hpp | Purpose |
|------|:---:|:---:|---------|
| `src/pg_ducklake_pg.cpp` | Yes | No | PostgreSQL extension interface |
| `src/pg_ducklake_duckdb.cpp` | No | Yes | DuckDB/ducklake registration |
| `include/pg_ducklake_bridge.h` | No | No | `extern "C"` bridge declarations |
| `third_party/ducklake/src/*.cpp` | No | Yes | Ducklake implementation |

#### Key API: `DuckDB::LoadStaticExtension<T>()`

DuckDB provides a template method designed for exactly this use case (`duckdb/main/database.hpp:115`):

```cpp
template <class T>
void LoadStaticExtension() {
    T extension;
    auto &manager = ExtensionManager::Get(*instance);
    auto info = manager.BeginLoad(extension.Name());
    if (!info) return; // already loaded — idempotent
    ExtensionLoader loader(*instance, extension.Name());
    extension.Load(loader);
    loader.FinalizeLoad();
    ExtensionInstallInfo install_info;
    install_info.mode = ExtensionInstallMode::STATICALLY_LINKED;
    install_info.version = extension.Version();
    info->FinishLoad(install_info);
}
```

Since `DuckDB` is a friend of `ExtensionLoader`, it can call the private `FinalizeLoad()`. The call is idempotent — safe to call multiple times.

#### Registration Code

In `src/pg_ducklake_duckdb.cpp` (DuckDB-only translation unit):

```cpp
#include "duckdb.hpp"
#include "pgduckdb/pgduckdb_duckdb.hpp"
#include "ducklake_extension.hpp"

extern "C" void ducklake_ensure_loaded(void) {
    auto &db = pgduckdb::DuckDBManager::Get().GetDatabase();
    db.LoadStaticExtension<duckdb::DucklakeExtension>();
}
```

In `src/pg_ducklake_pg.cpp` (PostgreSQL-only translation unit):

```cpp
extern "C" {
#include "postgres.h"
// ... PostgreSQL headers ...
}
#include "pg_ducklake_bridge.h"  // extern "C" void ducklake_ensure_loaded(void);

// Called before any ducklake operation
static void EnsureDucklake(void) {
    ducklake_ensure_loaded();  // idempotent
}
```

#### Why This Works

1. **Symbol visibility**: pg_duckdb does NOT compile with `-fvisibility=hidden` on its own code. `DuckDBManager::Get()` and `GetDatabase()` are inline methods in the public header — they instantiate in our translation unit and reference pg_duckdb's `manager_instance` static member via dynamic linking.

2. **No header conflicts**: Strict TU separation ensures PostgreSQL macros never contaminate DuckDB translation units and vice versa.

3. **Same DuckDB instance**: `DuckDBManager::Get().GetDatabase()` returns pg_duckdb's singleton DuckDB instance. Ducklake registers its storage extension, functions, and types directly into that instance.

4. **Template instantiation**: `LoadStaticExtension<DucklakeExtension>()` is instantiated in our TU. Since `DuckDB` is a friend of `ExtensionLoader`, `FinalizeLoad()` is accessible. The template uses only DuckDB public headers.

5. **Idempotent**: `BeginLoad` returns `nullptr` if the extension is already loaded, making `ducklake_ensure_loaded()` safe to call from multiple code paths.

#### Build Requirements

The Makefile must handle two distinct compilation contexts:

```makefile
# PostgreSQL-facing files: include PG headers, no DuckDB headers
src/pg_ducklake_pg.o: PG_CPPFLAGS = -Iinclude
src/pg_ducklake_pg.o: PG_CXXFLAGS = -std=c++17

# DuckDB-facing files: include DuckDB + ducklake headers, no PG headers
DUCKDB_INCLUDES = -isystem third_party/pg_duckdb/third_party/duckdb/src/include \
                  -I third_party/pg_duckdb/include \
                  -I third_party/ducklake/src/include
src/pg_ducklake_duckdb.o: override CXXFLAGS = $(DUCKDB_INCLUDES) -std=c++17
```

Ducklake's 49 source files compile with DuckDB include paths only, then all object files link into a single `pg_ducklake_next.so`.

#### Hard Requirements

| Requirement | Why |
|-------------|-----|
| ABI compatibility | Ducklake must compile against the **exact same DuckDB headers** as pg_duckdb. Different versions = undefined behavior. |
| pg_duckdb built first | Need libduckdb and pg_duckdb's include headers available |
| Submodule versions pinned | `third_party/pg_duckdb` and `third_party/ducklake` must target the same DuckDB version |
| macOS: `-undefined,dynamic_lookup` | pg_duckdb symbols resolved at load time |
| Linux: may need `-Wl,--unresolved-symbols=ignore-in-shared-libs` | Same purpose on Linux |

#### Risks

| Risk | Severity | Mitigation |
|------|----------|------------|
| DuckDB version mismatch between pg_duckdb and ducklake | High | Pin both submodules to the same DuckDB version. Verify at build time. |
| pg_duckdb changes `DuckDBManager` API | Medium | Only `Get()` and `GetDatabase()` are used — small surface. |
| Build complexity (49 extra source files) | Medium | Wildcard `$(wildcard third_party/ducklake/src/**/*.cpp)` in Makefile |
| Binary size increase | Low | Ducklake adds ~2-3 MB of object code |
| Initialization timing | Low | `DuckDBManager::Get()` uses lazy init; safe from SQL function context |

**Pros**:
- Full C++ API access to ducklake — can call `DuckLakeMetadataManager::Register` and any other symbol
- No internet dependency (no downloading from community repo)
- No pg_duckdb rebuild required
- Mirrors the ducklake-on-DuckDB architecture naturally
- Header conflicts solved cleanly via separate TUs
- Uses DuckDB's official `LoadStaticExtension` API

**Cons**:
- Tight version coupling to DuckDB's ABI (same as Approach 2, but without rebuilding DuckDB)
- Build complexity: must compile 49 extra C++ files with correct include paths
- Larger binary than Approach 1

**Use Case**: When you want pg_ducklake to be a "thick" extension that owns the ducklake lifecycle, has full C++ API access, and doesn't depend on downloading extensions at runtime.

---

## Recommendation

**For a thick extension with full ducklake control**: Use Approach 5 (Embedded). This gives full C++ API access, no internet dependency, and mirrors the ducklake-on-DuckDB architecture. The cost is build complexity and ABI coupling.

**For a thin extension with minimal coupling**: Use Approach 1 (SQL Interface). This is what pg_mooncake does in production — install the DuckDB extension from a repository at SQL time, interact entirely through SQL.

**If you need reverse callbacks** (DuckDB extension calling into PG extension): Use Approach 3, following pg_mooncake's `dlsym(RTLD_DEFAULT, ...)` pattern with `#[no_mangle] extern "C"` exports.

**If you need full C++ API access but want to stay in DuckDB's build**: Use Approach 2, but understand the pg_duckdb rebuild cost. pg_mooncake explicitly avoided this path.

## Summary Table

| Approach | Complexity | C++ API Access | Header Conflicts | Internet Required | pg_duckdb Rebuild |
|----------|-----------|---------------|-----------------|-------------------|-------------------|
| 1. SQL Interface | Low | No | None | Yes (download) | No |
| 2. Static into DuckDB | High | Full | Must manage | No | Yes |
| 3. C FFI + dlsym | Medium | Callbacks only | None | Yes (download) | No |
| 4. Shared lib + C API | Medium-High | Via wrapper | None | No | No |
| 5. Embedded in PG ext | Medium | Full | None (separate TUs) | No | No |

## References

- pg_mooncake source: `../pg_mooncake/`
- pg_mooncake → pg_duckdb linkage: `../pg_mooncake/src/table.rs:5-7`
- duckdb_mooncake → pg_mooncake callbacks: `../pg_mooncake/duckdb_mooncake/src/include/pgmooncake.hpp`
- pg_mooncake bootstrap SQL: `../pg_mooncake/src/sql/bootstrap.sql`
- pg_duckdb symbol export: `../pg_mooncake/pg_duckdb/src/pgduckdb_table_am.cpp`
- macOS dynamic lookup: `../pg_mooncake/.cargo/config.toml`
- DuckDB `LoadStaticExtension<T>()`: `third_party/pg_duckdb/third_party/duckdb/src/include/duckdb/main/database.hpp:115`
- DuckDB `ExtensionLoader` API: `third_party/pg_duckdb/third_party/duckdb/src/include/duckdb/main/extension/extension_loader.hpp`
- Ducklake extension entry point: `third_party/ducklake/src/ducklake_extension.cpp`
- pg_duckdb `DuckDBManager` singleton: `third_party/pg_duckdb/src/pgduckdb_duckdb.cpp:185-197`
