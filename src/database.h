#ifndef DATABASE_H
#define DATABASE_H

#include <jansson.h>		/* for json_t */
#include <stdbool.h>
#include <sqlite3.h>
#include <pthread.h>		/* for pthread_mutex_t */
#include <sqlite3.h>
#include "database_cmd.h"

#define UUID_STR_LEN 37
#define TX_TYPE_TRADE_BUY "TRADE_BUY"
#define TX_TYPE_TRADE_SELL "TRADE_SELL"


/* External declaration for the mutex */
extern pthread_mutex_t db_mutex;

/* Forward declare to avoid including jansson here */
typedef struct json_t json_t;
/* Initialise database (creates file if not exists) */
int db_init ();

/* Create required tables */
int db_create_tables (bool schema_exists);

/* Insert default data (config rows, etc.) */
int db_insert_defaults (void);

/* Load ports from config table */
int db_load_ports (int *server_port, int *s2s_port);

/* Get the shared database handle */
sqlite3 *db_get_handle (void);

/* Cleanup */
void db_close (void);

//const char *create_table_sql[];
//const char *insert_default_sql[];


#endif /* DATABASE_H */
