/* src/server_ports.c */
#include <string.h>
#include <jansson.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include "server_ports.h"
#include "game_db.h"
#include "server_config.h"
#include "database_market.h"
#include "database.h"
#include "database_cmd.h"
#include "errors.h"
#include "config.h"
#include "server_envelope.h"
#include "server_cmds.h"
#include "server_ships.h"
#include "server_players.h"
#include "server_cron.h"
#include "server_log.h"
#include "common.h"
#include "server_universe.h"
#include "db_player_settings.h"
#include "server_clusters.h"
#include "db/db_api.h"


/* Helpers */
static int
h_robbery_get_config (db_t *db, double *chance_base)
{
  /* Simplified fetch from law_enforcement table */
  const char *sql =
    "SELECT robbery_bust_chance_base FROM law_enforcement WHERE id=1";
  db_res_t *res; db_error_t err;
  if (db_query (db, sql, NULL, 0, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          *chance_base = db_res_col_double (res, 0, &err);
        }
      else
        {
          *chance_base = 0.05;
        }
      db_res_finalize (res);
      return 0;
    }
  return -1;
}


int
cmd_port_rob (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      return 0;
    }
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");
  int port_id = json_integer_value (json_object_get (data, "port_id"));
  const char *mode = json_string_value (json_object_get (data, "mode"));

  /* 1. Real Bust Calc */
  double chance = 0.05;


  h_robbery_get_config (db, &chance);

  double roll = (double)rand () / (double)RAND_MAX;
  int is_bust = (roll < chance);


  if (is_bust)
    {
      /* Real Bust: Record + Penalty */
      const char *sql =
        "INSERT INTO port_busts (port_id, player_id, bust_type, active) VALUES ($1, $2, 'real', 1)";
      db_bind_t p[] = { db_bind_i32 (port_id), db_bind_i32 (ctx->player_id) };
      db_error_t err;


      db_exec (db, sql, p, 2, &err);

      json_t *resp = json_object ();


      json_object_set_new (resp, "rob_result", json_string ("real_bust"));
      json_object_set_new (resp, "message", json_string ("You were caught!"));
      send_response_ok_take (ctx, root, "port.rob", &resp);
    }
  else
    {
      /* Success: Steal Credits */
      long long stolen = 1000;   /* Fixed amt for now */


      h_add_player_petty_cash_unlocked (db, ctx->player_id, stolen, NULL);

      json_t *resp = json_object ();


      json_object_set_new (resp, "rob_result", json_string ("success"));
      json_object_set_new (resp, "credits_stolen", json_integer (stolen));
      send_response_ok_take (ctx, root, "port.rob", &resp);
    }
  return 0;
}


/* Original trade stubs from your file */
int
cmd_trade_offer (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx, root, 501, "Trading handshake disabled in v1.0");
  return 0;
}


int
cmd_trade_accept (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx, root, 501, "Trading handshake disabled in v1.0");
  return 0;
}


int
cmd_trade_cancel (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx, root, 501, "Trading handshake disabled in v1.0");
  return 0;
}


int
cmd_trade_jettison (client_ctx_t *ctx, json_t *root)
{
  /* Full logic implementation of jettison */
  if (ctx->player_id <= 0)
    {
      return 0;
    }
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");
  const char *comm = json_string_value (json_object_get (data, "commodity"));
  int qty = json_integer_value (json_object_get (data, "quantity"));

  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  int new_qty = 0;


  if (h_update_ship_cargo (db, ship_id, comm, -qty, &new_qty) == 0)
    {
      send_response_ok_borrow (ctx, root, "ship.jettisoned", NULL);
    }
  else
    {
      send_response_error (ctx, root, 500, "Jettison failed");
    }
  return 0;
}







int h_calculate_port_buy_price(db_t *db, int port_id, const char *commodity) { (void)db; (void)port_id; (void)commodity; return 100; }

int h_calculate_port_sell_price(db_t *db, int port_id, const char *commodity) { (void)db; (void)port_id; (void)commodity; return 90; }

int h_get_port_commodity_quantity(db_t *db, int port_id, const char *code, int *qty) {     if (qty) *qty = 1000;     const char *sql = "SELECT quantity FROM port_commodities WHERE port_id =  AND commodity_code = ;";     db_res_t *res = NULL; db_error_t err;     if (db_query(db, sql, (db_bind_t[]){db_bind_i32(port_id), db_bind_text(code)}, 2, &res, &err)) {         if (db_res_step(res, &err)) {             if (qty) *qty = db_res_col_i32(res, 0, &err);         }         db_res_finalize(res);     }     return 0; }

int h_market_move_port_stock(db_t *db, int port_id, const char *code, int delta) {     const char *sql = "UPDATE port_commodities SET quantity = quantity +  WHERE port_id =  AND commodity_code = ;";     db_error_t err;     db_exec(db, sql, (db_bind_t[]){db_bind_i32(delta), db_bind_i32(port_id), db_bind_text(code)}, 3, &err);     return 0; }

int h_update_entity_stock(db_t *db, const char *type, int id, const char *code, int delta, int *new_qty) {     if (strcmp(type, "port") == 0) {         return h_market_move_port_stock(db, id, code, delta);     }     return -1; }

int
db_load_ports (int *server_port, int *s2s_port)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  *server_port = db_get_config_int (db, "server_port", 1234);
  *s2s_port = db_get_config_int (db, "s2s_port", 4321);
  return 0;
}
