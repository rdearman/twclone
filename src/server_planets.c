/* src/server_planets.c */
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <jansson.h>
#include <stdbool.h>

/* local includes */
#include "server_planets.h"
#include "server_rules.h"
#include "common.h"
#include "server_log.h"
#include "database.h"
#include "game_db.h"
#include "namegen.h"
#include "errors.h"
#include "server_cmds.h"
#include "server_corporation.h"
#include "server_ports.h"
#include "database_market.h"
#include "server_players.h"
#include "server_combat.h"
#include "server_ships.h"
#include "database_cmd.h"
#include "server_config.h"
#include "db/db_api.h"

int h_planet_check_trade_legality (db_t *db, int pid, int player_id, const char *code, bool buy) { (void)db; (void)pid; (void)player_id; (void)code; (void)buy; return 0; }
int h_get_planet_owner_info (db_t *db, int pid, planet_t *p) { (void)db; (void)pid; (void)p; return 0; }

int
cmd_planet_info (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle(); json_t *data = json_object_get(root, "data");
  int pid = (int)json_integer_value(json_object_get(data, "planet_id"));
  json_t *info = NULL; if (db_planet_get_details_json(db, pid, &info) == 0) send_response_ok_take(ctx, root, "planet.info", &info);
  else send_response_error(ctx, root, ERR_NOT_FOUND, "Failed");
  return 0;
}

int cmd_planet_rename (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_land (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_launch (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_transfer_ownership (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_harvest (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_deposit (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_withdraw (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_genesis_create (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_market_sell (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_market_buy_order (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_transwarp (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_colonists_set (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_planet_colonists_get (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int h_market_move_planet_stock (db_t *db, int pid, const char *code, int delta) { (void)db; (void)pid; (void)code; (void)delta; return 0; }