#pragma once
#include <sqlite3.h>
#include <stdint.h>

typedef int (*cron_handler_fn)(sqlite3 *db, int64_t now_s);

/* Lookup by task name (e.g., "fedspace_cleanup"). */
cron_handler_fn cron_find(const char *name);

/* Register all built-in cron handlers. Call once at startup. */
void cron_register_builtins(void);
