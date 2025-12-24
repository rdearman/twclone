/* src/server_ports.c */
#include <string.h>
#include <jansson.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>

/* local includes */
#include "server_ports.h"
#include "database.h"
#include "game_db.h"
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

/* Forward declarations */
int h_calculate_port_buy_price (db_t *db, int port_id, const char *commodity);
int h_calculate_port_sell_price (db_t *db, int port_id, const char *commodity);

const char *
commodity_to_code (const char *commodity)
{
  if (!commodity || !*commodity) return NULL;
  db_t *db = game_db_get_handle (); if (!db) return NULL;
  db_res_t *res = NULL; db_error_t err; char *result = NULL;
  const char *sql = "SELECT code FROM commodities WHERE UPPER(code) = UPPER($1) LIMIT 1;";
  if (db_query(db, sql, (db_bind_t[]){db_bind_text(commodity)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) result = strdup(db_res_col_text(res, 0, &err) ?: "");
      db_res_finalize(res);
  }
  return result;
}

static int
h_get_entity_stock_quantity (db_t *db, const char *type, int id, const char *code, int *qty)
{
  if (!db || !qty) return -1;
  db_res_t *res = NULL; db_error_t err;
  const char *sql = "SELECT quantity FROM entity_stock WHERE entity_type=$1 AND entity_id=$2 AND commodity_code=$3;";
  if (db_query(db, sql, (db_bind_t[]){db_bind_text(type), db_bind_i32(id), db_bind_text(code)}, 3, &res, &err)) {
      if (db_res_step(res, &err)) *qty = db_res_col_i32(res, 0, &err); else *qty = 0;
      db_res_finalize(res); return 0;
  }
  return err.code;
}

int
h_update_entity_stock (db_t *db, const char *type, int id, const char *code, int delta, int *new_qty)
{
  int cur = 0; h_get_entity_stock_quantity(db, type, id, code, &cur);
  int next = (cur + delta > 0) ? cur + delta : 0;
  db_error_t err;
  const char *sql = "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, last_updated_ts) "
                    "VALUES ($1, $2, $3, $4, strftime('%s','now')) "
                    "ON CONFLICT(entity_type, entity_id, commodity_code) DO UPDATE SET quantity = $4, last_updated_ts = strftime('%s','now');";
  if (!db_exec(db, sql, (db_bind_t[]){db_bind_text(type), db_bind_i32(id), db_bind_text(code), db_bind_i32(next)}, 4, &err)) return err.code;
  if (new_qty) *new_qty = next; return 0;
}

int
h_entity_calculate_sell_price (db_t *db, const char *type, int id, const char *code)
{
  if (strcmp(type, "port") == 0) return h_calculate_port_sell_price(db, id, code);
  return 0;
}

int
h_entity_calculate_buy_price (db_t *db, const char *type, int id, const char *code)
{
  if (strcmp(type, "port") == 0) return h_calculate_port_buy_price(db, id, code);
  return 0;
}

int
h_calculate_port_sell_price (db_t *db, int port_id, const char *code)
{
  if (!db || port_id <= 0 || !code) return 0;
  db_res_t *res = NULL; db_error_t err;
  const char *sql = "SELECT c.base_price, es.quantity, p.size * 1000 as max_cap FROM commodities c "
                    "JOIN ports p ON p.id = $1 JOIN entity_stock es ON es.entity_id = p.id AND es.entity_type = 'port' AND es.commodity_code = c.code "
                    "WHERE c.code = $2 LIMIT 1;";
  int price = 0;
  if (db_query(db, sql, (db_bind_t[]){db_bind_i32(port_id), db_bind_text(code)}, 2, &res, &err)) {
      if (db_res_step(res, &err)) {
          int base = db_res_col_i32(res, 0, &err);
          int qty = db_res_col_i32(res, 1, &err);
          int cap = db_res_col_i32(res, 2, &err);
          double ratio = cap > 0 ? (double)qty / cap : 1.0;
          price = (int)(base * (1.5 - ratio));
      }
      db_res_finalize(res);
  }
  return price;
}

int
h_calculate_port_buy_price (db_t *db, int port_id, const char *code)
{
  if (!db || port_id <= 0 || !code) return 0;
  db_res_t *res = NULL; db_error_t err;
  const char *sql = "SELECT c.base_price, es.quantity, p.size * 1000 as max_cap FROM commodities c "
                    "JOIN ports p ON p.id = $1 JOIN entity_stock es ON es.entity_id = p.id AND es.entity_type = 'port' AND es.commodity_code = c.code "
                    "WHERE c.code = $2 LIMIT 1;";
  int price = 0;
  if (db_query(db, sql, (db_bind_t[]){db_bind_i32(port_id), db_bind_text(code)}, 2, &res, &err)) {
      if (db_res_step(res, &err)) {
          int base = db_res_col_i32(res, 0, &err);
          int qty = db_res_col_i32(res, 1, &err);
          int cap = db_res_col_i32(res, 2, &err);
          double ratio = cap > 0 ? (double)qty / cap : 1.0;
          price = (int)(base * (1.0 - ratio * 0.5));
      }
      db_res_finalize(res);
  }
  return price;
}

int
h_get_port_commodity_details (db_t *db, int port_id, const char *code, int *qty, int *price, bool *buy, bool *sell)
{
  if (!db || !code) return -1;
  if (qty) h_get_entity_stock_quantity(db, "port", port_id, code, qty);
  if (price) *price = h_calculate_port_sell_price(db, port_id, code);
  if (buy) *buy = true; if (sell) *sell = true;
  return 0;
}

int h_update_port_stock (db_t *db, int port_id, const char *code, int delta, int *new_qty)
{
  return h_update_entity_stock(db, "port", port_id, code, delta, new_qty);
}

int h_market_move_port_stock (db_t *db, int port_id, const char *code, int delta)
{
  return h_update_port_stock(db, port_id, code, delta, NULL);
}

int cmd_trade_port_info (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_trade_buy (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_trade_sell (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_trade_history (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_dock_status (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_port_rob (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_trade_jettison (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_trade_quote (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }