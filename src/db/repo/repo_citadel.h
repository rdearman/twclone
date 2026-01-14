#ifndef REPO_CITADEL_H
#define REPO_CITADEL_H

#include "db/db_api.h"
#include <stdint.h>

int repo_citadel_get_onplanet(db_t *db, int32_t ship_id, int32_t *planet_id_out);

int repo_citadel_get_planet_info(db_t *db, int32_t planet_id, db_res_t **out_res);

int repo_citadel_get_status(db_t *db, int32_t planet_id, db_res_t **out_res);

int repo_citadel_get_upgrade_reqs(db_t *db, int target_level, int32_t planet_type_id, db_res_t **out_res);

int repo_citadel_deduct_resources(db_t *db, int64_t ore, int64_t org, int64_t equip, int32_t planet_id);

int repo_citadel_start_construction(db_t *db, int32_t planet_id, int32_t current_level, int32_t player_id, int32_t target_level, int64_t start_time, int64_t end_time);

#endif
