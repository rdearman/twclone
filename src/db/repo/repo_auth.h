#ifndef REPO_AUTH_H
#define REPO_AUTH_H

#include "db/db_api.h"
#include <stdint.h>
#include <stdbool.h>

int repo_auth_get_player_type_flags(db_t *db, int player_id, int *type_out, int *flags_out);
int repo_auth_upsert_global_sub(db_t *db, int player_id);
int repo_auth_upsert_player_sub(db_t *db, int player_id, const char *channel);
int repo_auth_upsert_sysop_sub(db_t *db, int player_id);
int repo_auth_upsert_locked_sub(db_t *db, int player_id, const char *topic);
int repo_auth_insert_pref_if_missing(db_t *db, int player_id, const char *key, const char *type, const char *value);
int repo_auth_get_podded_status(db_t *db, int player_id, char *status_out, size_t status_sz, int64_t *big_sleep_until_out);
int repo_auth_get_unread_news_count(db_t *db, int player_id, int *count_out);
int repo_auth_upsert_system_sub(db_t *db, int player_id);
int repo_auth_register_player(db_t *db, const char *name, const char *pass, const char *ship_name, int spawn_sid, int64_t *player_id_out, db_error_t *err);
int repo_auth_insert_initial_turns(db_t *db, const char *now_ts, int player_id, int initial_turns);
int repo_auth_update_player_credits(db_t *db, int credits, int player_id);
int repo_auth_upsert_news_sub(db_t *db, int player_id);
int repo_auth_check_username_exists(db_t *db, const char *username, int *exists_out);
int repo_auth_verify_password(db_t *db, int player_id, const char *password, int *valid_out);
int repo_auth_update_password(db_t *db, int player_id, const char *new_password);

#endif // REPO_AUTH_H
