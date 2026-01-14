#ifndef REPO_CMDS_H
#define REPO_CMDS_H

#include "db/db_api.h"
#include <stdint.h>
#include <stdbool.h>

int repo_cmds_get_port_name(db_t *db, int32_t port_id, char *name_out, size_t name_sz);

int repo_cmds_get_login_info(db_t *db, const char *username, int32_t *player_id_out, char *pass_out, size_t pass_sz, bool *is_npc_out);

int repo_cmds_register_player(db_t *db, const char *user, const char *pass, const char *ship_name, int64_t *player_id_out);

int repo_cmds_upsert_turns(db_t *db, int64_t player_id);

int repo_cmds_get_bounties(db_t *db, int alignment, db_res_t **out_res);

int repo_cmds_get_planet_info(db_t *db, int32_t planet_id, db_res_t **out_res);

int repo_cmds_get_planet_stock(db_t *db, int32_t planet_id, db_res_t **out_res);

int repo_cmds_get_port_info(db_t *db, int32_t port_id, db_res_t **out_res);

int repo_cmds_get_port_stock(db_t *db, int32_t port_id, db_res_t **out_res);

#endif