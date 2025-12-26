// db_player_settings.h
#ifndef DB_PLAYER_SETTINGS_H
#define DB_PLAYER_SETTINGS_H
#include <stddef.h>
#include <stdint.h>
#include "db/db_api.h"

/* ---- Prefs (typed KV) ---- */
#ifndef PREF_TYPE_DEFINED
#define PREF_TYPE_DEFINED
typedef enum
{
  PT_BOOL = 1,
  PT_INT = 2,
  PT_STRING = 3,
  PT_JSON = 4
} pref_type;
#endif

/* Call once after DB open */
int db_player_settings_init (db_t *db);

/* ---- Subscriptions ---- */
int db_subscribe_upsert (db_t *db, int64_t player_id, const char *topic,
                         const char *filter_json, int locked /*0/1 */ );
int db_subscribe_disable (db_t *db, int64_t player_id, const char *topic, /*out */
                          int *was_locked);

/* ---- Bookmarks ---- */
int db_bookmark_upsert (db_t *db, int64_t player_id, const char *name,
                        int64_t sector_id);
int db_bookmark_list (db_t *db, int64_t player_id, /*out */ db_res_t **it);
int db_bookmark_remove (db_t *db, int64_t player_id, const char *name);

/* ---- Avoid ---- */
int db_avoid_add (db_t *db, int64_t player_id, int64_t sector_id);
int db_avoid_list (db_t *db, int64_t player_id, /*out */ db_res_t **it);
int db_avoid_remove (db_t *db, int64_t player_id, int64_t sector_id);

/* ---- Notes ---- */
int db_note_set (db_t *db,
                 int64_t player_id,
                 const char *scope,
                 const char *key,
                 const char *note);
int db_note_delete (db_t *db,
                    int64_t player_id,
                    const char *scope,
                    const char *key);
int db_note_list (db_t *db, int64_t player_id, const char *scope_or_null,
                  /*out */ db_res_t **it);

/* cols: scope,key,note */
// Iterate all players who should receive an event of the given type.
typedef int (*player_id_cb) (int player_id, void *arg);
int db_for_each_subscriber (db_t *db,  const char *event_type,
                            player_id_cb cb, void *arg);

int db_prefs_get_all (db_t *db, int64_t player_id, /*out */ db_res_t **it);
int db_prefs_get_one (db_t *db,
                      int64_t player_id,
                      const char *key,
                      char **out_value);
int db_prefs_set_one (db_t *db, int64_t player_id, const char *key, pref_type t,
                      const char *value);

int db_get_player_pref_int (db_t *db, int player_id, const char *key,
                            int default_value);
int db_get_player_pref_string (db_t *db, int player_id, const char *key,
                               const char *default_value, char *out_buffer,
                               size_t buffer_size);

#endif /* DB_PLAYER_SETTINGS_H */