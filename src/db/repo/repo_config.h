#ifndef REPO_CONFIG_H
#define REPO_CONFIG_H

#include "db/db_api.h"

int repo_config_get_all(db_t *db, db_res_t **out_res);
int repo_config_get_value(db_t *db, const char *key, char *out_val, size_t out_sz);
int repo_config_get_default_s2s_key(db_t *db, db_res_t **out_res);

#endif
