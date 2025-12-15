#ifndef DATABASE_H
#define DATABASE_H
#include <jansson.h>            /* for json_t */
#include <stdbool.h>
#include <sqlite3.h>
#include <pthread.h>            /* for pthread_mutex_t */
#include "database_cmd.h"
#define UUID_STR_LEN 37
#define TX_TYPE_TRADE_BUY "TRADE_BUY"
#define TX_TYPE_TRADE_SELL "TRADE_SELL"
#define DEFAULT_DB_NAME "twconfig.db"
/* External declaration for the mutex */
extern pthread_mutex_t db_mutex;


/* Issue 142: Helper for robust rollback logging */
void db_safe_rollback (sqlite3 *db, const char *context_name);
/* Forward declare to avoid including jansson here */
typedef struct json_t json_t;
/* Initialise database (creates file if not exists) */
int db_init (void);
/* Create required tables */
int db_create_tables (bool schema_exists);
/* Insert default data (config rows, etc.) */
int db_insert_defaults (void);
/* Load ports from config table */
int db_load_ports (int *server_port, int *s2s_port);
int db_get_int_config (sqlite3 *db, const char *key, int *out);

/* DB handle access
 *
 * Each worker thread has its own SQLite connection.
 * - Always call db_get_handle() from the current thread.
 * - Do NOT cache sqlite3* in global/static variables.
 * - Do NOT use external pthread mutexes around db handles; SQLite is used
 *   in its own serialized/thread-safe mode per connection.
 */
sqlite3 *db_get_handle (void);
void db_handle_close_and_reset (void);
void db_mutex_lock (void);
void db_mutex_unlock (void);
void db_close_thread (void);
/* Session management functions */
int db_session_create (int player_id, int ttl_seconds, char token_out[65]);
int db_session_revoke (const char *token);
int db_session_refresh (const char *token, int ttl_seconds,
                        char new_token_out[65], int *out_player_id);
int db_session_lookup (const char *token,
                       int *out_player_id,
                       long long *out_expires);
/* Cleanup */
void db_close (void);
// Bank accounts
int db_bank_account_create_default_for_player (sqlite3 *db, int player_id);

int db_seed_cron_tasks (sqlite3 *db);

#endif /* DATABASE_H */
