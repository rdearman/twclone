#ifndef REPO_AUTOPILOT_H
#define REPO_AUTOPILOT_H

#include "db/db_api.h"
#include <stdint.h>

int repo_autopilot_get_max_sector_id(db_t *db, int32_t *out_max_id);

int repo_autopilot_get_warp_count(db_t *db, int32_t *out_count);

int repo_autopilot_get_all_warps(db_t *db, db_res_t **out_res);

#endif
