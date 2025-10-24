// db_player_settings.h
#ifndef DB_PLAYER_SETTINGS_H
#define DB_PLAYER_SETTINGS_H
#endif

#include <stdint.h>
#include <sqlite3.h>


/* ---- Prefs (typed KV) ---- */
#ifndef PREF_TYPE_DEFINED
#define PREF_TYPE_DEFINED
typedef enum {
  PT_BOOL   = 1,
  PT_INT    = 2,
  PT_STRING = 3,
  PT_JSON   = 4
} pref_type;
#endif

/* Call once after DB open */
int db_player_settings_init (sqlite3 * db);

/* ---- Subscriptions ---- */
int db_subscribe_upsert (int64_t player_id, const char *topic,
			 const char *filter_json, int locked /*0/1 */ );
int db_subscribe_disable (int64_t player_id, const char *topic,	/*out */
			  int *was_locked);
int db_subscribe_list (int64_t player_id, /*out */ sqlite3_stmt ** it);	/* cols: topic,locked,enabled,delivery,filter_json */

/* ---- Bookmarks ---- */
int db_bookmark_upsert (int64_t player_id, const char *name,
			int64_t sector_id);
int db_bookmark_list (int64_t player_id, /*out */ sqlite3_stmt ** it);	// cols: name,sector_id
int db_bookmark_remove (int64_t player_id, const char *name);

/* ---- Avoid ---- */

int db_avoid_add (int64_t player_id, int64_t sector_id);
int db_avoid_list (int64_t player_id, /*out */ sqlite3_stmt ** it);	// cols: sector_id
int db_avoid_remove (int64_t player_id, int64_t sector_id);

/* ---- Notes ---- */
int db_note_set (int64_t player_id, const char *scope, const char *key,
		 const char *note);
int db_note_delete (int64_t player_id, const char *scope, const char *key);
int db_note_list (int64_t player_id, const char *scope_or_null, /*out */ sqlite3_stmt ** it);	/* cols: scope,key,note */
// Iterate all players who should receive an event of the given type.
// Matches exact topic and one-segment wildcard ("domain.*").
// cb(player_id, arg) is called for each distinct player_id; return 0 to continue.
typedef int (*player_id_cb) (int player_id, void *arg);

int db_for_each_subscriber (sqlite3 * db, const char *event_type,	// e.g., "sector.42"
			    player_id_cb cb, void *arg);


int db_prefs_get_all (int64_t player_id, /*out*/ sqlite3_stmt **it);
int db_prefs_set_one (int64_t player_id, const char *key, pref_type t, const char *value);
int db_prefs_get_one (int64_t player_id, const char *key, char **out_value);
