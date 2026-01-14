#ifndef REPO_SHIPS_H
#define REPO_SHIPS_H

#include "db/db_api.h"
#include <stdint.h>

int repo_ships_handle_destruction(db_t *db,
                                  int64_t victim_pid,
                                  int64_t victim_sid,
                                  int64_t killer_pid,
                                  const char *cause,
                                  int64_t sector_id,
                                  int64_t xp_loss_flat,
                                  int64_t xp_loss_percent,
                                  int64_t max_per_day,
                                  int64_t big_sleep,
                                  int64_t now_ts,
                                  int32_t *result_code_out);

int repo_ships_get_active_id(db_t *db, int32_t player_id, int32_t *ship_id_out);

int repo_ships_get_towing_id(db_t *db, int32_t ship_id, int32_t *towing_id_out);

int repo_ships_clear_towing_id(db_t *db, int32_t ship_id);

int repo_ships_clear_is_being_towed_by(db_t *db, int32_t ship_id);

int repo_ships_get_is_being_towed_by(db_t *db, int32_t ship_id, int32_t *towed_by_id_out);

int repo_ships_set_towing_id(db_t *db, int32_t ship_id, int32_t target_ship_id);

int repo_ships_set_is_being_towed_by(db_t *db, int32_t target_ship_id, int32_t player_ship_id);

int repo_ships_get_cargo_and_holds(db_t *db,
                                   int32_t ship_id,
                                   int *ore,
                                   int *organics,
                                   int *equipment,
                                   int *colonists,
                                   int *slaves,
                                   int *weapons,
                                   int *drugs,
                                   int *holds);

int repo_ships_update_cargo_column(db_t *db, int32_t ship_id, const char *col_name, int32_t new_qty);

#endif
