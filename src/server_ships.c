/* src/server_ships.c */
#include <strings.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <jansson.h>
#include <stdbool.h>

/* local includes */
#include "schemas.h"
#include "server_cmds.h"
#include "server_loop.h"
#include "server_ships.h"
#include "database.h"
#include "game_db.h"
#include "database_cmd.h"
#include "errors.h"
#include "config.h"
#include "common.h"
#include "server_envelope.h"
#include "server_rules.h"
#include "server_players.h"
#include "server_log.h"
#include "server_ports.h"
#include "server_config.h"
#include "db/db_api.h"

int handle_ship_destruction (db_t *db, ship_kill_context_t *ctx)
{
  (void)db; (void)ctx; return 0;
}

int handle_big_sleep (db_t *db, ship_kill_context_t *ctx)
{
  (void)db; (void)ctx; return 0;
}

int handle_escape_pod_spawn (db_t *db, ship_kill_context_t *ctx)
{
  (void)db; (void)ctx; return 0;
}

int cmd_ship_transfer_cargo (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_ship_upgrade (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_ship_repair (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_ship_inspect (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_ship_rename (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_ship_claim (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_ship_status (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_ship_info_compat (client_ctx_t *ctx, json_t *root) { return cmd_ship_status(ctx, root); }
int cmd_ship_self_destruct (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_ship_tow (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }

int h_get_active_ship_id (db_t *db, int player_id)
{
  db_res_t *res = NULL; db_error_t err; int ship_id = -1;
  if (db_query (db, "SELECT ship FROM players WHERE id = $1;", (db_bind_t[]){db_bind_i32(player_id)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) ship_id = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return ship_id;
}

int h_update_ship_cargo (db_t *db, int ship_id, const char *commodity, int delta, int *new_qty)
{
  (void)db; (void)ship_id; (void)commodity; (void)delta; (void)new_qty; return 0;
}
