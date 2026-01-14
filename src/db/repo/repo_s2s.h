#pragma once
#include "db/db_api.h"

int repo_s2s_create_key (db_t *db, const char *key_id, const char *key_b64);
int repo_s2s_get_default_key (db_t *db, char *out_key_id, size_t key_id_size, char *out_key_b64, size_t key_b64_size);
