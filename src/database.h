#ifndef DATABASE_H
#define DATABASE_H

#include <jansson.h>		/* for json_t */
#include <sqlite3.h>		/* for sqlite3 */
#include <pthread.h>		/* for pthread_mutex_t */

/* External declaration for the mutex */
extern pthread_mutex_t db_mutex;

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
/* Idempotency storage */
int db_ensure_idempotency_schema (void);

/* Return codes:
   - SQLITE_OK: created a new placeholder row for key (begin ok)
   - SQLITE_CONSTRAINT: key already exists (caller should fetch and compare)
   - other sqlite codes: error
*/
int db_idemp_try_begin (const char *key, const char *cmd, const char *req_fp);

/* Fetch existing record; any out param may be NULL.
   Returns SQLITE_OK if found; SQLITE_NOTFOUND if missing. */
int db_idemp_fetch (const char *key, char **out_cmd, char **out_req_fp,
		    char **out_response_json);

/* Store final response JSON for a key (after a successful op).
   Returns SQLITE_OK on success. */
int db_idemp_store_response (const char *key, const char *response_json);

int db_sector_info_json (int sector_id, json_t ** out);
int db_sector_basic_json (int sector_id, json_t ** out_obj);
int db_adjacent_sectors_json (int sector_id, json_t ** out_array);
int db_ports_at_sector_json (int sector_id, json_t ** out_array);
int db_players_at_sector_json (int sector_id, json_t ** out_array);
int db_beacons_at_sector_json (int sector_id, json_t ** out_array);
int db_planets_at_sector_json (int sector_id, json_t ** out_array);
int db_player_set_sector (int player_id, int sector_id);
int db_player_get_sector (int player_id, int *out_sector);
int db_player_info_json (int player_id, json_t ** out);
int db_sector_beacon_text (int sector_id, char **out_text);	// caller frees *out_text
int db_planets_at_sector_json (int sector_id, json_t ** out_array);
int db_players_at_sector_json (int sector_id, json_t ** out_array);
int db_ports_at_sector_json (int sector_id, json_t ** out_array);
int db_ships_at_sector_json (int player_id, int sector_id, json_t ** out);
int db_sector_has_beacon (int sector_id);
int db_sector_set_beacon (int sector_id, const char *beacon_text);
int db_player_has_beacon_on_ship (int player_id);
int db_player_decrement_beacon_count (int player_id);
int db_player_has_beacon_on_ship (int player_id);
int db_player_decrement_beacon_count (int player_id);
int db_ships_inspectable_at_sector_json (int player_id, int sector_id,
					 json_t ** out_array);
int db_ship_claim (int player_id, int sector_id, int ship_id,
		   json_t ** out_ship);
int db_ship_flags_set (int ship_id, int mask);
int db_ship_flags_clear (int ship_id, int mask);
/* List ships in sector (exclude caller’s piloted ship), include ownership & pilot status */
int db_ships_inspectable_at_sector_json (int player_id, int sector_id,
					 json_t ** out_array);
/* Rename if caller owns the ship (via ship_ownership) */
int db_ship_rename_if_owner (int player_id, int ship_id,
			     const char *new_name);
/* Claim an unpiloted ship (ownership unchanged); returns JSON of claimed ship */
int db_ship_claim (int player_id, int sector_id, int ship_id,
		   json_t ** out_ship);

int db_ensure_ship_perms_column(void);

#endif /* DATABASE_H */
