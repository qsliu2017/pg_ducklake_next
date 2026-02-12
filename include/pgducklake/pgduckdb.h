/*
 * Exported functions from pg_duckdb
 */

#include "access/tableam.h"

extern bool RegisterDuckdbTableAm(const char *name, const TableAmRoutine *am);
extern void *GetDuckDBDatabase(void);
