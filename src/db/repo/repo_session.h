#ifndef REPO_SESSION_H
#define REPO_SESSION_H

#include "db/db_api.h"
#include <stdint.h>

int repo_session_lookup(db_t *db, const char *token, int32_t *out_player_id, int64_t *out_expires);

int repo_session_get_unseen_notices(db_t *db, int32_t max_rows, db_res_t **out_res);

#endif
