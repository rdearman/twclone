/* src/server_ports.c */
#include <string.h>
#include <jansson.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include "server_ports.h"
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
      send_response_ok (ctx, root, "ship.jettisoned");
    }
  else
    {
      send_response_error (ctx, root, 500, "Jettison failed");
    }
  return 0;
}


int
db_cancel_commodity_orders_for_actor_and_commodity (db_t *db,
                                                    const char *actor_type,
                                                    int actor_id,
                                                    int commodity_id,
                                                    const char *side)
{
  db_error_t err;
  db_error_clear (&err);

  db_bind_t params[4];


  params[0] = db_bind_text (actor_type);   /* $1 */
  params[1] = db_bind_i32 (actor_id);      /* $2 */
  params[2] = db_bind_i32 (commodity_id);  /* $3 */
  params[3] = db_bind_text (side);         /* $4 */

  const char *sql =
    "UPDATE commodity_orders SET status='cancelled' "
    "WHERE actor_type=$1 AND actor_id=$2 AND commodity_id=$3 AND side=$4 AND status='open';";

  int64_t rows = 0;


  if (!db_exec_rows_affected (db, sql, params, 4, &rows, &err))
    {
      LOGE (
        "db_cancel_commodity_orders_for_actor_and_commodity: update failed: %s (code=%d backend=%d)",
        err.message,
        err.code,
        err.backend_code);
      return -1;
    }

  return 0;
}


int
db_update_commodity_order (db_t *db,
                           int order_id,
                           int new_quantity,
                           int new_filled_quantity,
                           const char *new_status)
{
  db_error_t err;
  db_error_clear (&err);

  db_bind_t params[4];


  params[0] = db_bind_i32 (order_id);          /* $1 */
  params[1] = db_bind_i32 (new_quantity);      /* $2 */
  params[2] = db_bind_i32 (new_filled_quantity); /* $3 */
  params[3] = db_bind_text (new_status);       /* $4 */

  const char *sql =
    "UPDATE commodity_orders SET quantity=$2, filled_quantity=$3, status=$4 "
    "WHERE id=$1;";


  if (!db_exec (db, sql, params, 4, &err))
    {
      LOGE ("db_update_commodity_order: update failed: %s (code=%d backend=%d)",
            err.message, err.code, err.backend_code);
      return -1;
    }

  return 0;
}


int
db_cancel_commodity_orders_for_port_and_commodity (db_t *db,
                                                   int port_id,
                                                   int commodity_id,
                                                   const char *side)
{
  return db_cancel_commodity_orders_for_actor_and_commodity (db,
                                                             "port",
                                                             port_id,
                                                             commodity_id,
                                                             side);
}


int
db_get_open_order (db_t *db,
                   const char *actor_type,
                   int actor_id,
                   int commodity_id,
                   const char *side,
                   commodity_order_t *out_order)
{
  if (!out_order || !actor_type || !side)
    {
      return ERR_DB_NO_ROWS;
    }

  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[4];


  params[0] = db_bind_text (actor_type);
  params[1] = db_bind_i32 (actor_id);
  params[2] = db_bind_i32 (commodity_id);
  params[3] = db_bind_text (side);

  const char *sql =
    "SELECT id, actor_type, actor_id, commodity_id, side, quantity, price, status, ts, expires_at, filled_quantity "
    "FROM commodity_orders "
    "WHERE actor_type=$1 AND actor_id=$2 AND commodity_id=$3 AND side=$4 AND status='open' "
    "LIMIT 1;";

  db_res_t *res = NULL;


  if (!db_query (db, sql, params, 4, &res, &err))
    {
      LOGE ("db_get_open_order: query failed: %s (code=%d backend=%d)",
            err.message, err.code, err.backend_code);
      return -1;
    }

  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      return ERR_DB_NO_ROWS; /* or your “not found” code */
    }

  memset (out_order, 0, sizeof(*out_order));
  out_order->id = (int)db_res_col_i64 (res, 0, &err);

  const char *a_type = db_res_col_text (res, 1, &err);


  if (a_type)
    {
      strncpy (out_order->actor_type, a_type,
               sizeof(out_order->actor_type) - 1);
    }

  out_order->actor_id = (int)db_res_col_i64 (res, 2, &err);
  out_order->commodity_id = (int)db_res_col_i64 (res, 3, &err);

  const char *s_side = db_res_col_text (res, 4, &err);


  if (s_side)
    {
      strncpy (out_order->side, s_side, sizeof(out_order->side) - 1);
    }

  out_order->quantity = (int)db_res_col_i64 (res, 5, &err);
  out_order->price = (int)db_res_col_i64 (res, 6, &err);

  const char *st = db_res_col_text (res, 7, &err);


  if (st)
    {
      strncpy (out_order->status, st, sizeof(out_order->status) - 1);
    }

  out_order->ts = db_res_col_i64 (res, 8, &err);

  if (db_res_col_is_null (res, 9))
    {
      out_order->expires_at = 0;
    }
  else
    {
      out_order->expires_at = db_res_col_i64 (res, 9, &err);
    }

  out_order->filled_quantity = (int)db_res_col_i64 (res, 10, &err);
  out_order->remaining_quantity = out_order->quantity -
                                  out_order->filled_quantity;

  db_res_finalize (res);
  return 0;
}

