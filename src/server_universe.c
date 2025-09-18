#include <stdio.h>
#include <sqlite3.h>
#include "server_universe.h"
#include "database.h"
#include "universe.h"

int universe_init(void) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT COUNT(*) FROM sectors;";
    if (sqlite3_prepare_v2(db_handle, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "DB universe_init error: %s\n", sqlite3_errmsg(db_handle));
        return -1;
    }

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        fprintf(stderr, "Universe empty, running bigbang...\n");
        return bigbang();
    }

    return 0;
}

/* Shutdown universe (hook for cleanup) */
void universe_shutdown(void) {
    /* At present nothing to do, placeholder for later */
}
