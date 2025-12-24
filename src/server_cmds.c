/* src/server_cmds.c */
#include <string.h>
#include <strings.h>
#include <jansson.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

/* local includes */
#include "database.h"
#include "game_db.h"
#include "server_cmds.h"
#include "server_auth.h"
#include "server_envelope.h"
#include "db_player_settings.h"
#include "errors.h"
#include "server_players.h"
#include "server_cron.h"
#include "server_log.h"
#include "server_clusters.h"
#include "database_market.h"
#include "server_ports.h"
#include "server_universe.h"
#include "database_cmd.h"
#include "db/db_api.h"

int play_login (const char *user, const char *pass, int *pid)
{
  db_t *db = game_db_get_handle(); if (!db) return AUTH_ERR_DB;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT id, passwd FROM players WHERE name = $1;", (db_bind_t[]){db_bind_text(user)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          if (strcmp(pass, db_res_col_text(res, 1, &err)) == 0) {
              *pid = db_res_col_i32(res, 0, &err); db_res_finalize(res); return AUTH_OK;
          }
      }
      db_res_finalize(res);
  }
  return AUTH_ERR_INVALID_CRED;
}

int user_create (db_t *db, const char *user, const char *pass, int *pid)
{
  db_error_t err; int64_t id = 0;
  if (!db_exec_insert_id(db, "INSERT INTO players (name, passwd) VALUES ($1, $2);", (db_bind_t[]){db_bind_text(user), db_bind_text(pass)}, 2, &id, &err)) return AUTH_ERR_DB;
  db_exec(db, "INSERT INTO turns (player, turns_remaining) VALUES ($1, 750);", (db_bind_t[]){db_bind_i64(id)}, 1, &err);
  if (pid) *pid = (int)id; return AUTH_OK;
}

int cmd_sys_cluster_init (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_sys_cluster_seed_illegal_goods (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_sys_test_news_cron (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_sys_raw_sql_exec (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_bounty_post_federation (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_bounty_post_hitlist (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_bounty_list (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_sys_econ_planet_status (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_sys_econ_port_status (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_sys_econ_orders_summary (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_sys_npc_ferengi_tick_once (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_debug_run_fedspace_cleanup (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int send_error_response (client_ctx_t *ctx, json_t *root, int code, const char *msg) { send_response_error(ctx, root, code, msg); return 0; }
int send_json_response (client_ctx_t *ctx, json_t *root, json_t *json) { send_response_ok_take(ctx, root, "ok", &json); return 0; }
