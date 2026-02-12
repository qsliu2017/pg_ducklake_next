# pg_ducklake_next — Approach 5: Statically embed ducklake into the PG extension.
#
# This Makefile compiles two separate translation unit groups:
#   1. PostgreSQL-facing:  src/pg_ducklake_pg.cpp (includes postgres.h, never duckdb.hpp)
#   2. DuckDB-facing:      src/pg_ducklake_duckdb.cpp + all ducklake sources
#                          (includes duckdb.hpp, never postgres.h)
#
# Prerequisites:
#   - pg_duckdb must be built first (we need libduckdb and pg_duckdb headers)
#   - git submodules initialized: git submodule update --init
#   - ducklake version must target the same DuckDB version as pg_duckdb
#
# The pg_duckdb DuckDB build directory is expected at:
#   third_party/pg_duckdb/third_party/duckdb/build/release/

EXTENSION = pg_ducklake_next
MODULE_big = pg_ducklake_next
DATA = sql/pg_ducklake_next--0.1.0.sql

# ---------------------------------------------------------------------------
# Path configuration
# ---------------------------------------------------------------------------
PG_DUCKDB_DIR = $(CURDIR)/third_party/pg_duckdb
DUCKDB_SRC_DIR = $(PG_DUCKDB_DIR)/third_party/duckdb
DUCKLAKE_DIR = $(CURDIR)/third_party/ducklake

# DuckDB build output (must be built first via pg_duckdb's Makefile)
DUCKDB_BUILD_TYPE ?= release
DUCKDB_BUILD_DIR = $(DUCKDB_SRC_DIR)/build/$(DUCKDB_BUILD_TYPE)

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------

# DuckDB + pg_duckdb + ducklake headers (for DuckDB-facing TUs)
DUCKDB_INCLUDES = \
	-isystem $(DUCKDB_SRC_DIR)/src/include \
	-isystem $(DUCKDB_SRC_DIR)/third_party/re2 \
	-isystem $(DUCKDB_SRC_DIR)/third_party/fmt/include \
	-isystem $(DUCKDB_SRC_DIR)/third_party/yyjson/include \
	-I$(PG_DUCKDB_DIR)/include \
	-I$(DUCKLAKE_DIR)/src/include

# Project-local headers (bridge header)
LOCAL_INCLUDES = -I$(CURDIR)/include

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------

# All ducklake extension sources
DUCKLAKE_SRCS = $(wildcard \
	$(DUCKLAKE_DIR)/src/*.cpp \
	$(DUCKLAKE_DIR)/src/common/*.cpp \
	$(DUCKLAKE_DIR)/src/functions/*.cpp \
	$(DUCKLAKE_DIR)/src/metadata_manager/*.cpp \
	$(DUCKLAKE_DIR)/src/storage/*.cpp \
	$(DUCKLAKE_DIR)/src/storage/statistics/*.cpp \
)

DUCKLAKE_OBJS = $(DUCKLAKE_SRCS:.cpp=.o)

# All object files for the shared library
OBJS = src/pg_ducklake_pg.o src/pg_ducklake_duckdb.o $(DUCKLAKE_OBJS)

# ---------------------------------------------------------------------------
# Compiler flags
# ---------------------------------------------------------------------------

# PG-facing TU uses standard PGXS flags (PG_CPPFLAGS, PG_CXXFLAGS)
override PG_CPPFLAGS += $(LOCAL_INCLUDES)
override PG_CXXFLAGS += -std=c++17

# Common DuckDB C++ flags
DUCKDB_CXXFLAGS = -std=c++17 -fPIC -Wno-sign-compare -Wno-unused-parameter

# ---------------------------------------------------------------------------
# Linker flags
# ---------------------------------------------------------------------------

# Link against libduckdb from pg_duckdb's build
SHLIB_LINK += -L$(DUCKDB_BUILD_DIR)/src -lduckdb
SHLIB_LINK += -Wl,-rpath,$(PG_LIB)/ -L$(PG_LIB) -lstdc++

# Platform-specific: allow pg_duckdb symbols to resolve at load time
ifeq ($(shell uname -s), Darwin)
	SHLIB_LINK += -Wl,-undefined,dynamic_lookup
endif

# ---------------------------------------------------------------------------
# PGXS integration
# ---------------------------------------------------------------------------
include Makefile.global

# ---------------------------------------------------------------------------
# Explicit compilation rules for DuckDB-facing TUs
#
# We CANNOT use PGXS pattern rules for these because PGXS injects PostgreSQL
# server include paths into CPPFLAGS, and DuckDB headers conflict with PG
# headers (FATAL, ERROR, Min, Max macros). Instead, we compile DuckDB-facing
# files with our own rules that use only DuckDB include paths.
# ---------------------------------------------------------------------------

# DuckDB bridge TU: needs DuckDB + pg_duckdb + ducklake + bridge headers
src/pg_ducklake_duckdb.o: src/pg_ducklake_duckdb.cpp
	@if test ! -d $(DEPDIR); then mkdir -p $(DEPDIR); fi
	$(CXX) $(DUCKDB_CXXFLAGS) $(DUCKDB_INCLUDES) $(LOCAL_INCLUDES) \
		-c -o $@ $< -MMD -MP -MF $(DEPDIR)/$(*F).Po

# Ducklake sources: need DuckDB + ducklake headers only
$(DUCKLAKE_OBJS): %.o: %.cpp
	@mkdir -p $(DEPDIR)/ducklake
	$(CXX) $(DUCKDB_CXXFLAGS) $(DUCKDB_INCLUDES) \
		-c -o $@ $< -MMD -MP -MF $(DEPDIR)/ducklake/$(notdir $*).Po

# The PG-facing TU (src/pg_ducklake_pg.o) uses the default PGXS pattern rule,
# which includes PG server headers via CPPFLAGS. Our PG_CPPFLAGS += adds the
# bridge header path.

# Ensure the shared library depends on all objects
$(shlib):

# ---------------------------------------------------------------------------
# Convenience targets
# ---------------------------------------------------------------------------
.PHONY: check-submodules print-ducklake-srcs

check-submodules:
	@test -f $(DUCKDB_SRC_DIR)/src/include/duckdb.hpp || \
		(echo "ERROR: DuckDB headers not found. Run:"; \
		 echo "  cd third_party/pg_duckdb && git submodule update --init third_party/duckdb"; \
		 exit 1)
	@test -f $(DUCKLAKE_DIR)/src/ducklake_extension.cpp || \
		(echo "ERROR: Ducklake sources not found. Run:"; \
		 echo "  git submodule update --init"; \
		 exit 1)

print-ducklake-srcs:
	@echo "Ducklake sources ($(words $(DUCKLAKE_SRCS)) files):"
	@echo $(DUCKLAKE_SRCS) | tr ' ' '\n'
