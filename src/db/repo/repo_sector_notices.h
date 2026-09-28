#ifndef REPO_SECTOR_NOTICES_H
#define REPO_SECTOR_NOTICES_H

#include "db/db_api.h"
#include <jansson.h>
#include <stdint.h>

int repo_sector_notice_create (db_t *db, int sector_id, int player_id,
                               const char *subtype, json_t *details,
                               int64_t created_at, int64_t expires_at,
                               const char *notice_key, int64_t *notice_id_out,
                               int *created_out);

#endif
