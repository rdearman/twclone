#ifndef REPO_UNIVERSE_H
#define REPO_UNIVERSE_H

#include "db/db_api.h"
#include <stdbool.h>

int repo_universe_log_engine_event(db_t *db, const char *type, int sector_id, const char *payload);
db_res_t* repo_universe_get_adjacent_sectors(db_t *db, int sector_id, db_error_t *err);
int repo_universe_get_random_neighbor(db_t *db, int sector_id, int *neighbor_out);
int repo_universe_update_ship_sector(db_t *db, int ship_id, int sector_id);
int repo_universe_mass_randomize_zero_sector_ships(db_t *db);
db_res_t* repo_universe_get_orion_ships(db_t *db, int owner_id, db_error_t *err);
int repo_universe_update_ship_target(db_t *db, int ship_id, int target_sector);
int repo_universe_get_corp_owner_by_tag(db_t *db, const char *tag, int *owner_id_out);
int repo_universe_get_port_sector_by_id_name(db_t *db, int port_id, const char *name, int *sector_out);
db_res_t* repo_universe_search_index(db_t *db, const char *q, int limit, int offset, int search_type, db_error_t *err);
db_res_t* repo_universe_get_density_sector_list(db_t *db, int target_sector, db_error_t *err);
int repo_universe_get_sector_density(db_t *db, int sector_id, int *density_out);
int repo_universe_warp_exists(db_t *db, int from, int to, int *exists_out);
db_res_t* repo_universe_get_interdictors(db_t *db, int sector_id, db_error_t *err);
int repo_universe_sector_has_port(db_t *db, int sector_id, int *has_port_out);
int repo_universe_get_max_sector_id(db_t *db, int *max_id_out);
int repo_universe_get_warp_count(db_t *db, int *count_out);
db_res_t* repo_universe_get_all_warps(db_t *db, db_error_t *err);
db_res_t* repo_universe_get_asset_counts(db_t *db, int sector_id, db_error_t *err);
int repo_universe_set_beacon(db_t *db, int sector_id, const char *text);
int repo_universe_check_transwarp(db_t *db, int ship_id, int *enabled_out);
int repo_universe_update_player_sector(db_t *db, int player_id, int sector_id);
int repo_universe_get_ferengi_corp_info(db_t *db, int *corp_id_out, int *player_id_out);
int repo_universe_get_ferengi_homeworld_sector(db_t *db, int *sector_out);
int repo_universe_get_ferengi_warship_type_id(db_t *db, int *type_id_out);
int repo_universe_get_random_wormhole_neighbor(db_t *db, int sector_id, int *neighbor_out);

#endif // REPO_UNIVERSE_H
