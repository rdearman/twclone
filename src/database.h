#ifndef DATABASE_H
#define DATABASE_H

#include <jansson.h>		/* for json_t */
#include <sqlite3.h>		/* for sqlite3 */

/* Forward declare to avoid including jansson here */
typedef struct json_t json_t;
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
int db_player_info_json (int player_id, json_t ** out);
////////////////////

extern const char *create_table_sql[];
extern const char *insert_default_sql[];


/* Forward declare jansson type (avoid including jansson in header) */
typedef struct json_t json_t;

/* Sessions (opaque tokens) */
int db_ensure_auth_schema (void);

/* Create a new session for player_id, TTL seconds. token_out must be >= 65 bytes (hex-64 + NUL). */
int db_session_create (int player_id, int ttl_seconds, char token_out[65]);

/* Look up a session token. Returns SQLITE_OK and sets *out_player_id and *out_expires_epoch. */
int db_session_lookup (const char *token, int *out_player_id,
		       long long *out_expires_epoch);

/* Revoke (delete) a session token. Returns SQLITE_OK if deleted or not found. */
int db_session_revoke (const char *token);

/* Rotate: verify old token, create new one (revoking old). */
int db_session_refresh (const char *old_token, int ttl_seconds,
			char token_out[65], int *out_player_id);

#endif /* DATABASE_H */
