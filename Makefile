EXTENSION = pg_ducklake_next
MODULE_big = pg_ducklake_next
OBJS = src/pg_ducklake_next.o
DATA = sql/pg_ducklake_next--0.1.0.sql

override PG_CPPFLAGS += -I$(CURDIR)/third_party/pg_duckdb/include
override PG_CXXFLAGS += -std=c++17

# Allow pg_duckdb symbols to be resolved at runtime
SHLIB_LINK += -Wl,-rpath,$(PG_LIB)/ -L$(PG_LIB) -lstdc++
ifeq ($(shell uname -s), Darwin)
	SHLIB_LINK += -Wl,-undefined,dynamic_lookup
endif
# SO_MAJOR_VERSION = 1

include Makefile.global

$(shlib):
