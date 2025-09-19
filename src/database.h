#ifndef DATABASE_H
#define DATABASE_H

#include <jansson.h>		/* for json_t */
#include <sqlite3.h>		/* for sqlite3 */


/* Initialise database (creates file if not exists) */
int db_init ();

/* Create required tables */
int db_create_tables (void);

/* Insert default data (config rows, etc.) */
int db_insert_defaults (void);

/* CRUD operations */
int db_create (const char *table, json_t * row);
json_t *db_read (const char *table, int id);
int db_update (const char *table, int id, json_t * row);
int db_delete (const char *table, int id);

/* Cleanup */
void db_close (void);

extern sqlite3 *db_get_handle (void);

////////////////////

extern const char *create_table_sql[];
extern const char *insert_default_sql[];

#endif /* DATABASE_H */
