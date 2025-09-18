#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "database.h"

static sqlite3 *db_handle = NULL;

/* Initialise database (creates file if missing) */
int db_init(const char *filename) {
    if (sqlite3_open(filename, &db_handle) != SQLITE_OK) {
        fprintf(stderr, "DB init error: %s\n", sqlite3_errmsg(db_handle));
        return -1;
    }
    return 0;
}

/* Close database */
void db_close(void) {
    if (db_handle) {
        sqlite3_close(db_handle);
        db_handle = NULL;
    }
}

/* Create tables */
int db_create_tables(void) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS players ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT,"
        " score INTEGER"
        ");";

    char *errmsg = NULL;
    if (sqlite3_exec(db_handle, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "DB create_tables error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

/* Insert default data */
int db_insert_defaults(void) {
    const char *sql = "INSERT INTO players (name, score) VALUES ('Default', 0);";
    char *errmsg = NULL;
    if (sqlite3_exec(db_handle, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
        fprintf(stderr, "DB insert_defaults error: %s\n", errmsg);
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

/* Create row in table from JSON */
int db_create(const char *table, json_t *row) {
    /* TODO: Build INSERT SQL dynamically based on JSON keys/values */
    fprintf(stderr, "db_create(%s, row) called (not implemented)\n", table);
    return 0;
}

/* Read row by id into JSON */
json_t *db_read(const char *table, int id) {
    /* TODO: Prepare SELECT ... WHERE id=? and return json_t * */
    fprintf(stderr, "db_read(%s, %d) called (not implemented)\n", table, id);
    return NULL;
}

/* Update row by id with new JSON */
int db_update(const char *table, int id, json_t *row) {
    /* TODO: Build UPDATE SQL dynamically */
    fprintf(stderr, "db_update(%s, %d, row) called (not implemented)\n", table, id);
    return 0;
}

/* Delete row by id */
int db_delete(const char *table, int id) {
    /* TODO: Prepare DELETE ... WHERE id=? */
    fprintf(stderr, "db_delete(%s, %d) called (not implemented)\n", table, id);
    return 0;
}
