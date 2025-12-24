/* src/server_combat.h */
#ifndef SERVER_COMBAT_H
#define SERVER_COMBAT_H
#include <jansson.h>
#include <stdbool.h>
#include "common.h"
#include "db/db_api.h"

int cmd_combat_attack (client_ctx_t *ctx, json_t *root);
int cmd_combat_status (client_ctx_t *ctx, json_t *root);
int cmd_combat_deploy_fighters (client_ctx_t *ctx, json_t *root);
int cmd_fighters_recall (client_ctx_t *ctx, json_t *root);
int cmd_combat_deploy_mines (client_ctx_t *ctx, json_t *root);
int cmd_combat_lay_mines (client_ctx_t *ctx, json_t *root);
int cmd_mines_recall (client_ctx_t *ctx, json_t *root);
int cmd_combat_scrub_mines (client_ctx_t *ctx, json_t *root);
int cmd_combat_attack_planet (client_ctx_t *ctx, json_t *root);
int cmd_deploy_fighters_list (client_ctx_t *ctx, json_t *root);
int cmd_deploy_mines_list (client_ctx_t *ctx, json_t *root);
int cmd_combat_sweep_mines (client_ctx_t *ctx, json_t *root);

int h_handle_sector_entry_hazards (db_t *db, client_ctx_t *ctx, int sector_id);
int h_trigger_atmosphere_quasar (db_t *db, client_ctx_t *ctx, int planet_id);

#endif