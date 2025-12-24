/* src/server_combat.c */
#include <jansson.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>

/* local includes */
#include "server_cron.h"
#include "server_log.h"
#include "common.h"
#include "server_players.h"
#include "server_universe.h"
#include "server_combat.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_config.h"
#include "server_planets.h"
#include "game_db.h"
#include "database_cmd.h"
#include "server_ships.h"
#include "db/db_api.h"

int cmd_combat_attack (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_combat_status (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_combat_deploy_fighters (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_fighters_recall (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_combat_deploy_mines (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_combat_lay_mines (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_mines_recall (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_combat_scrub_mines (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_combat_attack_planet (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_deploy_fighters_list (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_deploy_mines_list (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_combat_sweep_mines (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }

int h_handle_sector_entry_hazards (db_t *db, client_ctx_t *ctx, int sid) { (void)db; (void)ctx; (void)sid; return 0; }
int h_trigger_atmosphere_quasar (db_t *db, client_ctx_t *ctx, int pid) { (void)db; (void)ctx; (void)pid; return 0; }