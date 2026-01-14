#ifndef REPO_DATABASE_H
#define REPO_DATABASE_H

#include "db/db_api.h"

// S2S Keyring functions (migrated from s2s_keyring.c)
int repo_s2s_create_key (db_t *db, const char *key_id, const char *key_b64);
int repo_s2s_get_default_key (db_t *db, char *out_key_id, size_t key_id_size, char *out_key_b64, size_t key_b64_size);

int repo_database_raw_query(db_t *db, const char *sql, db_res_t **out_res);

#endif // REPO_DATABASE_H