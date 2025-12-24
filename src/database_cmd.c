/* src/database_cmd.c */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <jansson.h>
#include <stdbool.h>
#include <pthread.h>
#include <stddef.h>

/* local includes */
#include "common.h"
#include "server_config.h"
#include "database.h"
#include "game_db.h"
#include "database_cmd.h"
#include "server_log.h"
#include "server_cron.h"
#include "errors.h"
#include "db/db_api.h"

/* ==================================================================== */
/* STATIC HELPER DEFINITIONS                                            */
/* ==================================================================== */

static int
stmt_to_json_array (db_res_t *st, json_t **out_array, db_error_t *err)
{
  if (!out_array)
    {
      while (db_res_step (st, err)) {}
      return 0;
    }
  json_t *arr = json_array ();
  if (!arr) return ERR_NOMEM;
  
  while (db_res_step (st, err))
    {
      int cols = db_res_col_count (st);
      json_t *obj = json_object ();
      if (!obj) { db_res_cancel(st); json_decref(arr); return ERR_NOMEM; }
      for (int i = 0; i < cols; i++)
        {
          const char *col_name = db_res_col_name (st, i);
          db_col_type_t col_type = db_res_col_type (st, i);
          json_t *val = NULL;
          switch (col_type)
            {
              case DB_TYPE_INTEGER: val = json_integer (db_res_col_i64 (st, i, err)); break;
              case DB_TYPE_FLOAT: val = json_real (db_res_col_double (st, i, err)); break;
              case DB_TYPE_TEXT: val = json_string (db_res_col_text (st, i, err) ?: ""); break;
              case DB_TYPE_NULL: val = json_null (); break;
              default: val = json_null (); break;
            }
          json_object_set_new (obj, col_name, val ?: json_null());
        }
      json_array_append_new (arr, obj);
    }
  *out_array = arr;
  return 0;
}

/* ==================================================================== */
/* CORE PLAYER LOGIC                                                    */
/* ==================================================================== */

int
db_player_update_commission (db_t *db, int player_id)
{
  if (!db) return ERR_DB_MISUSE;
  db_error_t err;
  for (int retry = 0; retry < 3; retry++) {
      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err)) {
          if (err.code == ERR_DB_BUSY) { usleep (100000); continue; }
          return err.code;
      }
      db_res_t *res = NULL;
      int align = 0; long long exp = 0;
      if (!db_query (db, "SELECT alignment, experience FROM players WHERE id = $1 FOR UPDATE;", (db_bind_t[]){db_bind_i32(player_id)}, 1, &res, &err)) goto rollback;
      if (db_res_step (res, &err)) {
          align = db_res_col_i32 (res, 0, &err);
          exp = db_res_col_i64 (res, 1, &err);
      } else { db_res_finalize(res); if (err.code == ERR_DB_NO_ROWS) { db_tx_rollback(db, NULL); return ERR_NOT_FOUND; } goto rollback; }
      db_res_finalize (res);

      int band_id, is_evil;
      if (db_alignment_band_for_value (db, align, &band_id, NULL, NULL, NULL, &is_evil, NULL, NULL) != 0) goto rollback;

      int new_comm_id;
      if (db_commission_for_player (db, is_evil, exp, &new_comm_id, NULL, NULL) != 0) goto rollback;

      if (!db_exec (db, "UPDATE players SET commission = $1 WHERE id = $2;", (db_bind_t[]){db_bind_i32(new_comm_id), db_bind_i32(player_id)}, 2, &err)) goto rollback;
      if (!db_tx_commit (db, &err)) goto rollback;
      return 0;
rollback:
      db_tx_rollback (db, &err);
      if (err.code == ERR_DB_BUSY) { usleep (100000); continue; }
      return err.code;
  }
  return ERR_DB_BUSY;
}

int
db_commission_for_player (db_t *db, int is_evil_track, long long xp, int *out_id, char **out_title, int *out_is_evil)
{
  if (!db) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  const char *sql = "SELECT id, description, is_evil FROM commision WHERE is_evil = $1 AND min_exp <= $2 ORDER BY min_exp DESC LIMIT 1;";
  if (!db_query (db, sql, (db_bind_t[]){db_bind_i32(is_evil_track), db_bind_i64(xp)}, 2, &res, &err)) return err.code;
  if (db_res_step (res, &err)) {
      if (out_id) *out_id = db_res_col_i32 (res, 0, &err);
      if (out_title) *out_title = strdup (db_res_col_text (res, 1, &err) ?: "");
      if (out_is_evil) *out_is_evil = db_res_col_bool (res, 2, &err);
      db_res_finalize (res); return 0;
  }
  db_res_finalize (res);
  sql = "SELECT id, description, is_evil FROM commision WHERE is_evil = $1 ORDER BY min_exp ASC LIMIT 1;";
  if (!db_query (db, sql, (db_bind_t[]){db_bind_i32(is_evil_track)}, 1, &res, &err)) return err.code;
  if (db_res_step (res, &err)) {
      if (out_id) *out_id = db_res_col_i32 (res, 0, &err);
      if (out_title) *out_title = strdup (db_res_col_text (res, 1, &err) ?: "");
      if (out_is_evil) *out_is_evil = db_res_col_bool (res, 2, &err);
      db_res_finalize (res); return 0;
  }
  db_res_finalize (res); return ERR_NOT_FOUND;
}

int
db_alignment_band_for_value (db_t *db, int align, int *out_id, char **out_code, char **out_name, int *out_is_good, int *out_is_evil, int *out_can_buy_iss, int *out_can_rob_ports)
{
  if (!db) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  const char *sql = "SELECT id, code, name, is_good, is_evil, can_buy_iss, can_rob_ports FROM alignment_band WHERE $1 BETWEEN min_align AND max_align LIMIT 1;";
  if (!db_query (db, sql, (db_bind_t[]){db_bind_i32(align)}, 1, &res, &err)) return -1;
  if (db_res_step (res, &err)) {
      if (out_id) *out_id = db_res_col_i32 (res, 0, &err);
      if (out_code) *out_code = strdup (db_res_col_text (res, 1, &err) ?: "NEUTRAL");
      if (out_name) *out_name = strdup (db_res_col_text (res, 2, &err) ?: "Neutral");
      if (out_is_good) *out_is_good = db_res_col_bool (res, 3, &err);
      if (out_is_evil) *out_is_evil = db_res_col_bool (res, 4, &err);
      if (out_can_buy_iss) *out_can_buy_iss = db_res_col_bool (res, 5, &err);
      if (out_can_rob_ports) *out_can_rob_ports = db_res_col_bool (res, 6, &err);
  } else {
      if (out_id) *out_id = -1; if (out_code) *out_code = strdup("NEUTRAL"); if (out_name) *out_name = strdup("Neutral");
      if (out_is_good) *out_is_good = 0; if (out_is_evil) *out_is_evil = 0;
      if (out_can_buy_iss) *out_can_buy_iss = 0; if (out_can_rob_ports) *out_can_rob_ports = 0;
  }
  db_res_finalize (res); return 0;
}

/* ==================================================================== */
/* BANKING                                                              */
/* ==================================================================== */

int
db_bank_get_transactions (db_t *db, const char *owner_type, int owner_id, int limit, const char *filter, long long start, long long end, long long min, long long max, json_t **out)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err; char sql[1024];
  snprintf(sql, sizeof(sql), "SELECT date, account_id, type, amount, current_balance, description, tx_group_id FROM bank_transactions "
                             "WHERE account_id = (SELECT id FROM bank_accounts WHERE owner_type = $1 AND owner_id = $2) ");
  db_bind_t params[10]; int idx = 0;
  params[idx++] = db_bind_text(owner_type); params[idx++] = db_bind_i32(owner_id);
  if (filter && *filter) { strncat(sql, "AND type = $3 ", sizeof(sql)-strlen(sql)-1); params[idx++] = db_bind_text(filter); }
  strncat(sql, "ORDER BY date DESC LIMIT $", sizeof(sql)-strlen(sql)-1);
  char buf[16]; snprintf(buf, sizeof(buf), "%d;", idx + 1); strncat(sql, buf, sizeof(sql)-strlen(sql)-1);
  params[idx++] = db_bind_i32(limit);
  (void)start; (void)end; (void)min; (void)max;
  db_res_t *res = NULL; int rc = 0;
  if (db_query(db, sql, params, idx, &res, &err)) { rc = stmt_to_json_array (res, out, &err); db_res_finalize(res); }
  else rc = err.code;
  return rc;
}

int db_bank_set_frozen_status (db_t *db, const char *owner_type, int owner_id, int is_frozen)
{
  if (!db || strcmp(owner_type, "player") != 0) return ERR_DB_MISUSE;
  const char *sql = "INSERT INTO bank_flags (player_id, is_frozen) VALUES ($1, $2) ON CONFLICT(player_id) DO UPDATE SET is_frozen = excluded.is_frozen;";
  db_error_t err;
  if (!db_exec(db, sql, (db_bind_t[]){db_bind_i32(owner_id), db_bind_i32(is_frozen)}, 2, &err)) return err.code;
  return 0;
}

int db_bank_get_frozen_status (db_t *db, const char *owner_type, int owner_id, int *out_frozen)
{
  if (!db || !out_frozen || strcmp(owner_type, "player") != 0) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT is_frozen FROM bank_flags WHERE player_id = $1;", (db_bind_t[]){db_bind_i32(owner_id)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) *out_frozen = db_res_col_i32(res, 0, &err); else *out_frozen = 0;
      db_res_finalize(res); return 0;
  }
  return err.code;
}

/* ==================================================================== */
/* COMMODITIES                                                          */
/* ==================================================================== */

int db_commodity_get_price (db_t *db, const char *code, int *out_price)
{
  if (!db || !code || !out_price) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT last_price FROM commodities WHERE code = $1;", (db_bind_t[]){db_bind_text(code)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) { *out_price = db_res_col_i32(res, 0, &err); db_res_finalize(res); return 0; }
      db_res_finalize(res); return ERR_NOT_FOUND;
  }
  return err.code;
}

int db_commodity_update_price (db_t *db, const char *code, int price)
{
  if (!db || !code) return ERR_DB_MISUSE;
  db_error_t err;
  if (!db_exec(db, "UPDATE commodities SET last_price = $1 WHERE code = $2;", (db_bind_t[]){db_bind_i32(price), db_bind_text(code)}, 2, &err)) return err.code;
  return 0;
}

int db_commodity_create_order (db_t *db, const char *actor_type, int actor_id, const char *code, const char *side, int qty, int price)
{
  if (!db || !code || !side) return ERR_DB_MISUSE;
  db_error_t err;
  const char *sql = "INSERT INTO commodity_orders (commodity_id, actor_type, actor_id, side, quantity, price) SELECT id, $2, $3, $4, $5, $6 FROM commodities WHERE code = $1;";
  if (!db_exec(db, sql, (db_bind_t[]){db_bind_text(code), db_bind_text(actor_type), db_bind_i32(actor_id), db_bind_text(side), db_bind_i32(qty), db_bind_i32(price)}, 6, &err)) return err.code;
  return 0;
}

int db_commodity_fill_order (db_t *db, int order_id, int qty)
{
  if (!db || order_id <= 0 || qty <= 0) return ERR_DB_MISUSE;
  db_error_t err; db_res_t *res = NULL; int cur_qty = 0;
  if (!db_query(db, "SELECT quantity FROM commodity_orders WHERE id = $1 FOR UPDATE;", (db_bind_t[]){db_bind_i32(order_id)}, 1, &res, &err)) return err.code;
  if (db_res_step(res, &err)) cur_qty = db_res_col_i32(res, 0, &err); else { db_res_finalize(res); return ERR_NOT_FOUND; }
  db_res_finalize(res);
  int rem = (cur_qty > qty) ? cur_qty - qty : 0;
  const char *status = (rem == 0) ? "filled" : "open";
  if (!db_exec(db, "UPDATE commodity_orders SET quantity = $1, status = $2 WHERE id = $3;", (db_bind_t[]){db_bind_i32(rem), db_bind_text(status), db_bind_i32(order_id)}, 3, &err)) return err.code;
  return 0;
}

int db_commodity_get_orders (db_t *db, const char *code, const char *status, json_t **out)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err;
  const char *sql = "SELECT co.id, co.actor_type, co.actor_id, c.code as commodity, co.side, co.quantity, co.price, co.status FROM commodity_orders co JOIN commodities c ON co.commodity_id = c.id WHERE c.code = $1 AND co.status = $2 ORDER BY co.ts DESC;";
  db_res_t *res = NULL; int rc = 0;
  if (db_query(db, sql, (db_bind_t[]){db_bind_text(code), db_bind_text(status)}, 2, &res, &err)) { rc = stmt_to_json_array (res, out, &err); db_res_finalize(res); }
  else rc = err.code;
  return rc;
}

int db_commodity_get_trades (db_t *db, const char *code, int limit, json_t **out)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err;
  const char *sql = "SELECT ct.id, c.code as commodity, ct.quantity, ct.price, ct.ts FROM commodity_trades ct JOIN commodities c ON ct.commodity_id = c.id WHERE c.code = $1 ORDER BY ct.ts DESC LIMIT $2;";
  db_res_t *res = NULL; int rc = 0;
  if (db_query(db, sql, (db_bind_t[]){db_bind_text(code), db_bind_i32(limit)}, 2, &res, &err)) { rc = stmt_to_json_array (res, out, &err); db_res_finalize(res); }
  else rc = err.code;
  return rc;
}

/* ==================================================================== */
/* PLANETS & GOODS                                                      */
/* ==================================================================== */

int db_planet_get_goods_on_hand (db_t *db, int planet_id, const char *code, int *out_qty)
{
  if (!db || !code || !out_qty) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT quantity FROM planet_goods WHERE planet_id = $1 AND commodity = $2;", (db_bind_t[]){db_bind_i32(planet_id), db_bind_text(code)}, 2, &res, &err)) {
      if (db_res_step(res, &err)) *out_qty = db_res_col_i32(res, 0, &err); else *out_qty = 0;
      db_res_finalize(res); return 0;
  }
  return err.code;
}

int db_planet_update_goods_on_hand (db_t *db, int planet_id, const char *code, int delta)
{
  if (!db || !code) return ERR_DB_MISUSE;
  db_error_t err; int cur = 0;
  db_planet_get_goods_on_hand(db, planet_id, code, &cur);
  int next = (cur + delta > 0) ? cur + delta : 0;
  if (!db_exec(db, "INSERT INTO planet_goods (planet_id, commodity, quantity) VALUES ($1, $2, $3) ON CONFLICT(planet_id, commodity) DO UPDATE SET quantity = excluded.quantity;", (db_bind_t[]){db_bind_i32(planet_id), db_bind_text(code), db_bind_i32(next)}, 3, &err)) return err.code;
  return 0;
}

/* ==================================================================== */
/* SHIPS & STATUS                                                       */
/* ==================================================================== */

int db_mark_ship_destroyed (db_t *db, int ship_id)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err;
  if (!db_exec(db, "UPDATE ships SET destroyed = 1 WHERE id = $1;", (db_bind_t[]){db_bind_i32(ship_id)}, 1, &err)) return err.code;
  return 0;
}

int db_clear_player_active_ship (db_t *db, int player_id)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err;
  if (!db_exec(db, "UPDATE players SET ship = 0 WHERE id = $1;", (db_bind_t[]){db_bind_i32(player_id)}, 1, &err)) return err.code;
  return 0;
}

int db_increment_player_stat (db_t *db, int pid, const char *stat)
{
  if (!db || (strcmp(stat, "times_blown_up") != 0 && strcmp(stat, "times_podded") != 0)) return ERR_DB_MISUSE;
  db_error_t err; char sql[128]; snprintf(sql, sizeof(sql), "UPDATE players SET %s = %s + 1 WHERE id = $1;", stat, stat);
  if (!db_exec(db, sql, (db_bind_t[]){db_bind_i32(pid)}, 1, &err)) return err.code;
  return 0;
}

int db_get_player_xp (db_t *db, int pid)
{
  if (!db) return 0;
  db_res_t *res = NULL; db_error_t err; int xp = 0;
  if (db_query(db, "SELECT experience FROM players WHERE id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) xp = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return xp;
}

int db_update_player_xp (db_t *db, int pid, int xp)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err;
  if (!db_exec(db, "UPDATE players SET experience = $1 WHERE id = $2;", (db_bind_t[]){db_bind_i32(xp), db_bind_i32(pid)}, 2, &err)) return err.code;
  return 0;
}

bool db_shiptype_has_escape_pod (db_t *db, int ship_id)
{
  if (!db) return false;
  db_res_t *res = NULL; db_error_t err; int tid = -1;
  if (db_query(db, "SELECT type_id FROM ships WHERE id = $1;", (db_bind_t[]){db_bind_i32(ship_id)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) tid = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return (tid > 0);
}

int db_create_podded_status_entry (db_t *db, int pid)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err;
  if (!db_exec(db, "INSERT INTO podded_status (player_id, status, podded_count_today, podded_last_reset) VALUES ($1, 'active', 0, $2) ON CONFLICT DO NOTHING;", (db_bind_t[]){db_bind_i32(pid), db_bind_i64(time(NULL))}, 2, &err)) return err.code;
  return 0;
}

int db_get_player_podded_count_today (db_t *db, int pid)
{
  if (!db) return 0;
  db_res_t *res = NULL; db_error_t err; int cnt = 0;
  if (db_query(db, "SELECT podded_count_today FROM podded_status WHERE player_id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) cnt = db_res_col_i32(res, 0, &err); else db_create_podded_status_entry(db, pid);
      db_res_finalize(res);
  }
  return cnt;
}

long long db_get_player_podded_last_reset (db_t *db, int pid)
{
  if (!db) return 0;
  db_res_t *res = NULL; db_error_t err; long long ts = 0;
  if (db_query(db, "SELECT podded_last_reset FROM podded_status WHERE player_id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) ts = db_res_col_i64(res, 0, &err); else { db_create_podded_status_entry(db, pid); ts = time(NULL); }
      db_res_finalize(res);
  }
  return ts;
}

int db_reset_player_podded_count (db_t *db, int pid, long long ts)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err;
  if (!db_exec(db, "UPDATE podded_status SET podded_count_today = 0, podded_last_reset = $1 WHERE player_id = $2;", (db_bind_t[]){db_bind_i64(ts), db_bind_i32(pid)}, 2, &err)) return err.code;
  return 0;
}

int db_update_player_podded_status (db_t *db, int pid, const char *status, long long until)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err;
  if (!db_exec(db, "UPDATE podded_status SET status = $1, big_sleep_until = $2 WHERE player_id = $3;", (db_bind_t[]){db_bind_text(status), db_bind_i64(until), db_bind_i32(pid)}, 3, &err)) return err.code;
  return 0;
}

/* ==================================================================== */
/* UTILITIES                                                            */
/* ==================================================================== */

int h_get_cluster_id_for_sector (db_t *db, int sid, int *out_cid)
{
  if (!db || !out_cid) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT cluster_id FROM cluster_sectors WHERE sector_id = $1;", (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) { *out_cid = db_res_col_i32(res, 0, &err); db_res_finalize(res); return 0; }
      db_res_finalize(res); return ERR_DB_NOT_FOUND;
  }
  return err.code;
}

int h_get_cluster_alignment (db_t *db, int cid, int *out_align)
{
  if (!db || !out_align) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT alignment FROM clusters WHERE id = $1;", (db_bind_t[]){db_bind_i32(cid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) { *out_align = db_res_col_i32(res, 0, &err); db_res_finalize(res); return 0; }
      db_res_finalize(res); return ERR_DB_NOT_FOUND;
  }
  return err.code;
}

int h_get_cluster_alignment_band (db_t *db, int cid, int *out_bid)
{
  int align = 0; if (h_get_cluster_alignment(db, cid, &align) != 0) return -1;
  return db_alignment_band_for_value(db, align, out_bid, NULL, NULL, NULL, NULL, NULL, NULL);
}

int db_get_shiptype_info (db_t *db, int tid, int *h, int *f, int *s)
{
  if (!db) return ERR_DB_CLOSED;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT initialholds, maxfighters, maxshields FROM shiptypes WHERE id = $1;", (db_bind_t[]){db_bind_i32(tid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) { *h = db_res_col_i32(res, 0, &err); *f = db_res_col_i32(res, 1, &err); *s = db_res_col_i32(res, 2, &err); db_res_finalize(res); return 0; }
      db_res_finalize(res); return ERR_NOT_FOUND;
  }
  return err.code;
}

int db_player_land_on_planet (db_t *db, int pid, int planet_id)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err; db_res_t *res = NULL; int ship_id = -1;
  if (!db_query(db, "SELECT ship FROM players WHERE id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) return err.code;
  if (db_res_step(res, &err)) ship_id = db_res_col_i32(res, 0, &err);
  db_res_finalize(res);
  if (ship_id <= 0) return ERR_NOT_FOUND;
  if (!db_exec(db, "UPDATE ships SET onplanet = $1, sector = NULL, ported = 0 WHERE id = $2;", (db_bind_t[]){db_bind_i32(planet_id), db_bind_i32(ship_id)}, 2, &err)) return err.code;
  if (!db_exec(db, "UPDATE players SET lastplanet = $1, sector = NULL WHERE id = $2;", (db_bind_t[]){db_bind_i32(planet_id), db_bind_i32(pid)}, 2, &err)) return err.code;
  return 0;
}

int db_player_launch_from_planet (db_t *db, int pid, int *out_sid)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err; db_res_t *res = NULL; int ship_id = -1, lpid = -1, sid = -1;
  if (!db_query(db, "SELECT ship, lastplanet FROM players WHERE id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) return err.code;
  if (db_res_step(res, &err)) { ship_id = db_res_col_i32(res, 0, &err); lpid = db_res_col_i32(res, 1, &err); }
  db_res_finalize(res);
  if (ship_id <= 0 || lpid <= 0) return ERR_DB_MISUSE;
  if (!db_query(db, "SELECT sector FROM planets WHERE id = $1;", (db_bind_t[]){db_bind_i32(lpid)}, 1, &res, &err)) return err.code;
  if (db_res_step(res, &err)) sid = db_res_col_i32(res, 0, &err);
  db_res_finalize(res);
  if (sid <= 0) return ERR_NOT_FOUND;
  if (!db_exec(db, "UPDATE ships SET onplanet = NULL, sector = $1 WHERE id = $2;", (db_bind_t[]){db_bind_i32(sid), db_bind_i32(ship_id)}, 2, &err)) return err.code;
  if (!db_exec(db, "UPDATE players SET sector = $1 WHERE id = $2;", (db_bind_t[]){db_bind_i32(sid), db_bind_i32(pid)}, 2, &err)) return err.code;
  if (out_sid) *out_sid = sid; return 0;
}

int db_get_port_sector (db_t *db, int port_id)
{
  if (!db) return 0;
  db_res_t *res = NULL; db_error_t err; int sid = 0;
  if (db_query(db, "SELECT sector FROM ports WHERE id = $1;", (db_bind_t[]){db_bind_i32(port_id)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) sid = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return sid;
}

int db_bounty_create (db_t *db, const char *pbt, int pbid, const char *tt, int tid, long long r, const char *desc)
{
  (void)desc; if (!db) return ERR_DB_CLOSED;
  db_error_t err;
  const char *sql = "INSERT INTO bounties (posted_by_type, posted_by_id, target_type, target_id, reward, status, posted_ts) VALUES ($1, $2, $3, $4, $5, 'open', CURRENT_TIMESTAMP);";
  if (!db_exec(db, sql, (db_bind_t[]){db_bind_text(pbt), db_bind_i32(pbid), db_bind_text(tt), db_bind_i32(tid), db_bind_i64(r)}, 5, &err)) return err.code;
  return 0;
}

int db_player_get_alignment (db_t *db, int pid, int *align)
{
  if (!db || !align) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT alignment FROM players WHERE id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) { *align = db_res_col_i32(res, 0, &err); db_res_finalize(res); return 0; }
      db_res_finalize(res); return ERR_NOT_FOUND;
  }
  return err.code;
}

int db_get_law_config_int (const char *key, int def)
{
  db_t *db = game_db_get_handle(); if (!db) return def;
  db_res_t *res = NULL; db_error_t err; int val = def;
  if (db_query(db, "SELECT value FROM law_enforcement_config WHERE key = $1 AND value_type = 'INTEGER';", (db_bind_t[]){db_bind_text(key)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) val = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return val;
}

char *db_get_law_config_text (const char *key, const char *def)
{
  db_t *db = game_db_get_handle(); if (!db) return strdup(def ?: "");
  db_res_t *res = NULL; db_error_t err; char *val = NULL;
  if (db_query(db, "SELECT value FROM law_enforcement_config WHERE key = $1 AND value_type = 'TEXT';", (db_bind_t[]){db_bind_text(key)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) val = strdup(db_res_col_text(res, 0, &err) ?: "");
      db_res_finalize(res);
  }
  return val ?: strdup(def ?: "");
}

int db_player_get_last_rob_attempt (int pid, int *lpid, long long *lts)
{
  db_t *db = game_db_get_handle(); if (!db) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT last_port_id, last_attempt_at FROM player_last_rob WHERE player_id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) { if (lpid) *lpid = db_res_col_i32(res, 0, &err); if (lts) *lts = db_res_col_i64(res, 1, &err); db_res_finalize(res); return 0; }
      db_res_finalize(res); return ERR_NOT_FOUND;
  }
  return err.code;
}

int db_player_set_last_rob_attempt (int pid, int lpid, long long lts)
{
  db_t *db = game_db_get_handle(); if (!db) return -1;
  db_error_t err;
  const char *sql = "INSERT INTO player_last_rob (player_id, last_port_id, last_attempt_at) VALUES ($1, $2, $3) ON CONFLICT(player_id) DO UPDATE SET last_port_id = excluded.last_port_id, last_attempt_at = excluded.last_attempt_at;";
  if (!db_exec(db, sql, (db_bind_t[]){db_bind_i32(pid), db_bind_i32(lpid), db_bind_i64(lts)}, 3, &err)) return err.code;
  return 0;
}

int db_port_add_bust_record (int port_id, int pid, long long ts, const char *type)
{
  db_t *db = game_db_get_handle(); if (!db) return -1;
  db_error_t err;
  if (!db_exec(db, "INSERT INTO port_busts (port_id, player_id, timestamp, bust_type) VALUES ($1, $2, $3, $4);", (db_bind_t[]){db_bind_i32(port_id), db_bind_i32(pid), db_bind_i64(ts), db_bind_text(type)}, 4, &err)) return err.code;
  return 0;
}

int db_port_is_busted (int port_id, int pid)
{
  db_t *db = game_db_get_handle(); if (!db) return -1;
  db_res_t *res = NULL; db_error_t err; int busted = 0;
  if (db_query(db, "SELECT 1 FROM port_busts WHERE port_id = $1 AND player_id = $2 LIMIT 1;", (db_bind_t[]){db_bind_i32(port_id), db_bind_i32(pid)}, 2, &res, &err)) {
      if (db_res_step(res, &err)) busted = 1;
      db_res_finalize(res);
  }
  return (busted) ? 0 : -1; // 0 means busted in this weird API
}

int db_get_ship_name (db_t *db, int ship_id, char **out)
{
  if (!db || !out) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT name FROM ships WHERE id = $1;", (db_bind_t[]){db_bind_i32(ship_id)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) { *out = strdup(db_res_col_text(res, 0, &err) ?: ""); db_res_finalize(res); return 0; }
      db_res_finalize(res); return ERR_NOT_FOUND;
  }
  return err.code;
}

int db_get_port_name (db_t *db, int port_id, char **out)
{
  if (!db || !out) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT name FROM ports WHERE id = $1;", (db_bind_t[]){db_bind_i32(port_id)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) { *out = strdup(db_res_col_text(res, 0, &err) ?: ""); db_res_finalize(res); return 0; }
      db_res_finalize(res); return ERR_NOT_FOUND;
  }
  return err.code;
}

int db_log_engine_event (long long ts, const char *type, const char *owner_type, int pid, int sid, json_t *payload, db_t *db_opt)
{
  db_t *db = db_opt ?: game_db_get_handle(); if (!db) return -1;
  char *pstr = json_dumps(payload, 0); if (!pstr) return ERR_NOMEM;
  db_error_t err;
  const char *sql = "INSERT INTO engine_events (ts, type, actor_owner_type, actor_player_id, sector_id, payload) VALUES ($1, $2, $3, $4, $5, $6);";
  bool ok = db_exec(db, sql, (db_bind_t[]){db_bind_i64(ts), db_bind_text(type), db_bind_text(owner_type), db_bind_i32(pid), db_bind_i32(sid), db_bind_text(pstr)}, 6, &err);
  free(pstr); return ok ? 0 : err.code;
}

int db_news_post (long long ts, long long exp, const char *cat, const char *txt)
{
  db_t *db = game_db_get_handle(); if (!db) return -1;
  db_error_t err;
  if (!db_exec(db, "INSERT INTO news (ts, expiration_ts, category, article_text) VALUES ($1, $2, $3, $4);", (db_bind_t[]){db_bind_i64(ts), db_bind_i64(exp), db_bind_text(cat), db_bind_text(txt)}, 4, &err)) return err.code;
  return 0;
}

int
h_get_account_id_unlocked (db_t *db, const char *owner_type, int owner_id, int *account_id_out)
{
  if (!db || !owner_type) return ERR_DB_MISUSE;
  const char *sql = "SELECT id FROM bank_accounts WHERE owner_type = $1 AND owner_id = $2;";
  db_bind_t params[] = { db_bind_text(owner_type), db_bind_i32(owner_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int rc = ERR_NOT_FOUND;

  if (db_query(db, sql, params, 2, &res, &err)) {
      if (db_res_step(res, &err)) {
          if (account_id_out) *account_id_out = db_res_col_i32(res, 0, &err);
          rc = 0;
      }
      db_res_finalize(res);
  } else {
      rc = err.code;
  }
  return rc;
}

int
h_create_bank_account_unlocked (db_t *db, const char *owner_type, int owner_id, long long initial_balance, int *account_id_out)
{
  if (!db || !owner_type) return ERR_DB_MISUSE;
  const char *sql = "INSERT INTO bank_accounts (owner_type, owner_id, balance) VALUES ($1, $2, $3);";
  db_bind_t params[] = { db_bind_text(owner_type), db_bind_i32(owner_id), db_bind_i64(initial_balance) };
  db_error_t err;
  int64_t new_id = 0;

  if (db_exec_insert_id(db, sql, params, 3, &new_id, &err)) {
      if (account_id_out) *account_id_out = (int)new_id;
      return 0;
  }
  return err.code;
}

int
h_add_credits_unlocked (db_t *db, int account_id, long long amount, const char *tx_type, const char *tx_group_id, long long *new_balance_out)
{
  if (!db || account_id <= 0 || amount < 0) return ERR_DB_MISUSE;
  db_error_t err;
  
  // Update balance
  const char *sql_upd = "UPDATE bank_accounts SET balance = balance + $1 WHERE id = $2;";
  db_bind_t params_upd[] = { db_bind_i64(amount), db_bind_i32(account_id) };
  if (!db_exec(db, sql_upd, params_upd, 2, &err)) return err.code;

  // Insert transaction
  const char *sql_tx = "INSERT INTO bank_transactions (account_id, amount, direction, type, tx_group_id, ts) VALUES ($1, $2, 'CREDIT', $3, $4, $5);";
  db_bind_t params_tx[] = { 
      db_bind_i32(account_id), 
      db_bind_i64(amount), 
      db_bind_text(tx_type ?: "TRANSFER"), 
      tx_group_id ? db_bind_text(tx_group_id) : db_bind_null(),
      db_bind_i64(time(NULL))
  };
  db_exec(db, sql_tx, params_tx, 5, &err);

  if (new_balance_out) {
      const char *sql_bal = "SELECT balance FROM bank_accounts WHERE id = $1;";
      db_res_t *res = NULL;
      if (db_query(db, sql_bal, (db_bind_t[]){db_bind_i32(account_id)}, 1, &res, &err)) {
          if (db_res_step(res, &err)) *new_balance_out = db_res_col_i64(res, 0, &err);
          db_res_finalize(res);
      }
  }
  return 0;
}

int
h_deduct_credits_unlocked (db_t *db, int account_id, long long amount, const char *tx_type, const char *tx_group_id, long long *new_balance_out)
{
  if (!db || account_id <= 0 || amount < 0) return ERR_DB_MISUSE;
  db_error_t err;

  // Check balance first (row lock if possible, but here we just do it)
  long long current_balance = 0;
  const char *sql_check = "SELECT balance FROM bank_accounts WHERE id = $1;";
  db_res_t *res_c = NULL;
  if (db_query(db, sql_check, (db_bind_t[]){db_bind_i32(account_id)}, 1, &res_c, &err)) {
      if (db_res_step(res_c, &err)) current_balance = db_res_col_i64(res_c, 0, &err);
      db_res_finalize(res_c);
  }
  
  if (current_balance < amount) return ERR_INSUFFICIENT_FUNDS;

  // Update balance
  const char *sql_upd = "UPDATE bank_accounts SET balance = balance - $1 WHERE id = $2;";
  db_bind_t params_upd[] = { db_bind_i64(amount), db_bind_i32(account_id) };
  if (!db_exec(db, sql_upd, params_upd, 2, &err)) return err.code;

  // Insert transaction
  const char *sql_tx = "INSERT INTO bank_transactions (account_id, amount, direction, type, tx_group_id, ts) VALUES ($1, $2, 'DEBIT', $3, $4, $5);";
  db_bind_t params_tx[] = { 
      db_bind_i32(account_id), 
      db_bind_i64(amount), 
      db_bind_text(tx_type ?: "TRANSFER"), 
      tx_group_id ? db_bind_text(tx_group_id) : db_bind_null(),
      db_bind_i64(time(NULL))
  };
  db_exec(db, sql_tx, params_tx, 5, &err);

  if (new_balance_out) *new_balance_out = current_balance - amount;
  return 0;
}

long long
h_get_config_int_unlocked (db_t *db, const char *key, long long default_value)
{
  if (!db || !key) return default_value;
  const char *sql = "SELECT value FROM config WHERE key = $1;";
  db_bind_t params[] = { db_bind_text(key) };
  db_res_t *res = NULL;
  db_error_t err;
  long long val = default_value;

  if (db_query(db, sql, params, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          const char *text = db_res_col_text(res, 0, &err);
          if (text) val = atoll(text);
      }
      db_res_finalize(res);
  }
  return val;
}

int db_get_port_id_by_sector (db_t *db, int sid)
{
  if (!db) return 0;
  db_res_t *res = NULL; db_error_t err; int pid = 0;
  if (db_query(db, "SELECT id FROM ports WHERE sector = $1;", (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) pid = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return pid;
}

int db_get_sector_info (int sid, char **nm, int *sz, int *pc, int *sc, int *plc, char **bt)
{
  db_t *db = game_db_get_handle(); if (!db) return -1;
  db_res_t *res = NULL; db_error_t err;
  const char *sql = "SELECT name, safe_zone, port_count, ship_count, planet_count, beacon_text FROM sector_info_v1 WHERE id = $1;";
  if (db_query(db, sql, (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          if (nm) *nm = strdup(db_res_col_text(res, 0, &err) ?: "");
          if (sz) *sz = db_res_col_i32(res, 1, &err);
          if (pc) *pc = db_res_col_i32(res, 2, &err);
          if (sc) *sc = db_res_col_i32(res, 3, &err);
          if (plc) *plc = db_res_col_i32(res, 4, &err);
          if (bt) *bt = strdup(db_res_col_text(res, 5, &err) ?: "");
          db_res_finalize(res); return 0;
      }
      db_res_finalize(res);
  }
  return -1;
}

int db_news_get_recent (int pid, json_t **out)
{
  (void)pid;
  db_t *db = game_db_get_handle(); if (!db) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT id, ts, category, article_text FROM news ORDER BY ts DESC LIMIT 50;", NULL, 0, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); return rc;
  }
  return -1;
}

int db_port_get_goods_on_hand (db_t *db, int pid, const char *code, int *qty)
{
  if (!db || !qty) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT quantity FROM port_goods WHERE port_id = $1 AND commodity = $2;", (db_bind_t[]){db_bind_i32(pid), db_bind_text(code)}, 2, &res, &err)) {
      if (db_res_step(res, &err)) *qty = db_res_col_i32(res, 0, &err); else *qty = 0;
      db_res_finalize(res); return 0;
  }
  return -1;
}

int db_path_exists (db_t *db, int from, int to)
{
  if (!db) return 0;
  (void)from; (void)to;
  return 0;
}

int db_destroy_ship (db_t *db, int pid, int sid)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err;
  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) return err.code;
  bool ok = db_exec(db, "DELETE FROM ships WHERE id = $1;", (db_bind_t[]){db_bind_i32(sid)}, 1, &err);
  if (ok) ok = db_exec(db, "UPDATE players SET ship = 0 WHERE id = $1 AND ship = $2;", (db_bind_t[]){db_bind_i32(pid), db_bind_i32(sid)}, 2, &err);
  if (ok) return db_tx_commit(db, &err) ? 0 : err.code;
  db_tx_rollback(db, NULL); return err.code;
}

int db_rand_npc_shipname (db_t *db, char *out, size_t sz)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT name FROM npc_shipnames ORDER BY RANDOM() LIMIT 1;", NULL, 0, &res, &err)) {
      if (db_res_step(res, &err)) { strncpy(out, db_res_col_text(res, 0, &err) ?: "NPC Ship", sz-1); out[sz-1] = '\0'; db_res_finalize(res); return 0; }
      db_res_finalize(res);
  }
  return -1;
}

int db_sector_beacon_text (db_t *db, int sid, char **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT beacon_text FROM sectors WHERE id = $1;", (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) { *out = strdup(db_res_col_text(res, 0, &err) ?: ""); db_res_finalize(res); return 0; }
      db_res_finalize(res);
  }
  return -1;
}

int db_sector_set_beacon (db_t *db, int sid, const char *txt, int pid)
{
  if (!db) return -1;
  db_error_t err;
  if (!db_exec(db, "UPDATE sectors SET beacon_text = $1 WHERE id = $2;", (db_bind_t[]){db_bind_text(txt), db_bind_i32(sid)}, 2, &err)) return -1;
  (void)pid; return 0;
}

int db_player_set_alignment (db_t *db, int pid, int align)
{
  if (!db) return -1;
  db_error_t err;
  if (!db_exec(db, "UPDATE players SET alignment = $1 WHERE id = $2;", (db_bind_t[]){db_bind_i32(align), db_bind_i32(pid)}, 2, &err)) return -1;
  return 0;
}

int db_get_online_player_count (void)
{
  db_t *db = game_db_get_handle(); if (!db) return 0;
  db_res_t *res = NULL; db_error_t err; int cnt = 0;
  if (db_query(db, "SELECT COUNT(*) FROM sessions;", NULL, 0, &res, &err)) {
      if (db_res_step(res, &err)) cnt = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return cnt;
}

int db_get_player_id_by_name (const char *nm)
{
  db_t *db = game_db_get_handle(); if (!db || !nm) return 0;
  db_res_t *res = NULL; db_error_t err; int pid = 0;
  if (db_query(db, "SELECT id FROM players WHERE name = $1;", (db_bind_t[]){db_bind_text(nm)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) pid = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return pid;
}

int db_player_name (db_t *db, int64_t pid, char **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT name FROM players WHERE id = $1;", (db_bind_t[]){db_bind_i64(pid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) { *out = strdup(db_res_col_text(res, 0, &err) ?: ""); db_res_finalize(res); return 0; }
      db_res_finalize(res);
  }
  return -1;
}

int db_is_black_market_port (db_t *db, int pid)
{
  if (!db) return 0;
  db_res_t *res = NULL; db_error_t err; int black = 0;
  if (db_query(db, "SELECT 1 FROM ports WHERE id = $1 AND type = 'black_market' LIMIT 1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) black = 1;
      db_res_finalize(res);
  }
  return black;
}

int db_get_port_commodity_quantity (db_t *db, int pid, const char *code, int *qty)
{
  if (!db || !qty) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT quantity FROM port_goods WHERE port_id = $1 AND commodity = $2;", (db_bind_t[]){db_bind_i32(pid), db_bind_text(code)}, 2, &res, &err)) {
      if (db_res_step(res, &err)) { *qty = db_res_col_i32(res, 0, &err); db_res_finalize(res); return 0; }
      db_res_finalize(res);
  }
  return -1;
}

int db_sector_basic_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT id, name FROM sectors WHERE id = $1;", (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); return rc;
  }
  return -1;
}

int db_adjacent_sectors_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT to_sector FROM warps WHERE from_sector = $1;", (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); return rc;
  }
  return -1;
}

int db_ports_at_sector_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT id, name, type FROM ports WHERE sector = $1;", (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); return rc;
  }
  return -1;
}

int db_ships_at_sector_json (db_t *db, int pid, int sid, json_t **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT id, name FROM ships WHERE sector = $1;", (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); (void)pid; return rc;
  }
  return -1;
}

int db_planets_at_sector_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT id, name FROM planets WHERE sector = $1;", (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); return rc;
  }
  return -1;
}

int db_players_at_sector_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT id, name FROM players WHERE sector = $1;", (db_bind_t[]){db_bind_i32(sid)}, 1, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); return rc;
  }
  return -1;
}

int db_fighters_at_sector_json (db_t *db, int sid, json_t **out) { (void)db; (void)sid; *out = json_array(); return 0; }
int db_mines_at_sector_json (db_t *db, int sid, json_t **out) { (void)db; (void)sid; *out = json_array(); return 0; }
int db_beacons_at_sector_json (db_t *db, int sid, json_t **out) { (void)db; (void)sid; *out = json_array(); return 0; }
int db_update_player_sector (db_t *db, int pid, int sid)
{
  if (!db) return -1; db_error_t err;
  if (!db_exec(db, "UPDATE players SET sector = $1 WHERE id = $2;", (db_bind_t[]){db_bind_i32(sid), db_bind_i32(pid)}, 2, &err)) return -1;
  return 0;
}
int db_ship_flags_set (db_t *db, int sid, int mask) { (void)db; (void)sid; (void)mask; return 0; }
int db_ship_flags_clear (db_t *db, int sid, int mask) { (void)db; (void)sid; (void)mask; return 0; }
int db_get_sector_min (db_t *db, int sid, json_t **out) { return db_sector_basic_json(db, sid, out); }
int db_get_sector_rich (db_t *db, int sid, json_t **out) { return db_sector_basic_json(db, sid, out); }
int db_get_port_details_json (db_t *db, int pid, json_t **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT * FROM ports WHERE id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); return rc;
  }
  return -1;
}
int db_port_get_goods_json (db_t *db, int pid, json_t **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT * FROM port_goods WHERE port_id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); return rc;
  }
  return -1;
}
int db_planet_get_details_json (db_t *db, int pid, json_t **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT * FROM planets WHERE id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); return rc;
  }
  return -1;
}
int db_planet_get_goods_json (db_t *db, int pid, json_t **out)
{
  if (!db || !out) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT * FROM planet_goods WHERE planet_id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); return rc;
  }
  return -1;
}

int db_idemp_store_response (db_t *db, const char *key, const char *response_json)
{
  if (!db || !key) return ERR_DB_MISUSE;
  db_error_t err;
  const char *sql = "INSERT INTO idempotency (idem_key, response_json) VALUES ($1, $2) ON CONFLICT(idem_key) DO UPDATE SET response_json = excluded.response_json;";
  if (!db_exec(db, sql, (db_bind_t[]){db_bind_text(key), db_bind_text(response_json)}, 2, &err)) return err.code;
  return 0;
}

int db_player_get_sector (db_t *db, int pid, int *out_sector)
{
  if (!db || !out_sector) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT sector FROM players WHERE id = $1;", (db_bind_t[]){db_bind_i32(pid)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) { *out_sector = db_res_col_i32(res, 0, &err); db_res_finalize(res); return 0; }
      db_res_finalize(res); return ERR_NOT_FOUND;
  }
  return err.code;
}

int db_ships_inspectable_at_sector_json (db_t *db, int player_id, int sector_id, json_t **out_array)
{
  if (!db || !out_array) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  const char *sql = "SELECT id, name FROM ships WHERE sector = $1 AND (cloaked IS NULL OR $2 = (SELECT player_id FROM ship_ownership WHERE ship_id = ships.id AND is_primary = 1));";
  if (db_query(db, sql, (db_bind_t[]){db_bind_i32(sector_id), db_bind_i32(player_id)}, 2, &res, &err)) {
      int rc = stmt_to_json_array(res, out_array, &err); db_res_finalize(res); return rc;
  }
  return err.code;
}

int db_notice_create (db_t *db, const char *title, const char *body, const char *severity, time_t expires_at)
{
  if (!db) return -1;
  db_error_t err;
  const char *sql = "INSERT INTO notices (title, body, severity, expires_at) VALUES ($1, $2, $3, $4);";
  int64_t id = 0;
  if (db_exec_insert_id(db, sql, (db_bind_t[]){db_bind_text(title), db_bind_text(body), db_bind_text(severity), db_bind_i64(expires_at)}, 4, &id, &err)) return (int)id;
  return -1;
}

json_t *db_notice_list_unseen_for_player (db_t *db, int player_id)
{
  if (!db) return json_array();
  db_res_t *res = NULL; db_error_t err;
  const char *sql = "SELECT n.id, n.title, n.body, n.severity FROM notices n LEFT JOIN notice_reads r ON n.id = r.notice_id AND r.player_id = $1 WHERE r.player_id IS NULL AND (n.expires_at IS NULL OR n.expires_at > CURRENT_TIMESTAMP);";
  if (db_query(db, sql, (db_bind_t[]){db_bind_i32(player_id)}, 1, &res, &err)) {
      json_t *arr = NULL; stmt_to_json_array(res, &arr, &err); db_res_finalize(res); return arr ?: json_array();
  }
  return json_array();
}

int db_notice_mark_seen (db_t *db, int notice_id, int player_id)
{
  if (!db) return ERR_DB_CLOSED;
  db_error_t err;
  if (!db_exec(db, "INSERT INTO notice_reads (notice_id, player_id) VALUES ($1, $2) ON CONFLICT DO NOTHING;", (db_bind_t[]){db_bind_i32(notice_id), db_bind_i32(player_id)}, 2, &err)) return err.code;
  return 0;
}

int db_commands_accept (db_t *db, const char *cmd_type, const char *idem_key, json_t *payload, int *out_cmd_id, int *out_duplicate, int *out_due_at)
{
  (void)db; (void)cmd_type; (void)idem_key; (void)payload; (void)out_cmd_id; (void)out_duplicate; (void)out_due_at;
  return -1;
}

int db_ensure_ship_perms_column (db_t *db) { (void)db; return 0; }

int db_sector_scan_core (db_t *db, int sid, json_t **out) { return db_sector_basic_json(db, sid, out); }
int db_sector_scan_snapshot (db_t *db, int sid, json_t **out) { return db_sector_basic_json(db, sid, out); }
int db_create_initial_ship (db_t *db, int pid, const char *name, int sid) { (void)db; (void)pid; (void)name; (void)sid; return -1; }
int h_ship_claim_unlocked (db_t *db, int pid, int sid, int ship_id, json_t **out) { (void)db; (void)pid; (void)sid; (void)ship_id; (void)out; return -1; }
int db_ship_claim (db_t *db, int pid, int sid, int ship_id, json_t **out) { (void)db; (void)pid; (void)sid; (void)ship_id; (void)out; return -1; }
int db_news_insert_feed_item (db_t *db, int ts, const char *cat, const char *scope, const char *headline, const char *body, json_t *ctx) { (void)db; (void)ts; (void)cat; (void)scope; (void)headline; (void)body; (void)ctx; return -1; }
int db_is_sector_fedspace (db_t *db, int sid) { (void)db; (void)sid; return 0; }

int db_get_ship_sector_id (db_t *db, int ship_id)
{
  if (!db) return 0;
  db_res_t *res = NULL; db_error_t err; int sid = 0;
  if (db_query(db, "SELECT sector FROM ships WHERE id = $1;", (db_bind_t[]){db_bind_i32(ship_id)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) sid = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return sid;
}

int db_get_ship_owner_id (db_t *db, int ship_id, int *out_pid, int *out_cid)
{
  if (!db) return -1;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT player_id, corporation_id FROM ship_ownership WHERE ship_id = $1 AND is_primary = 1;", (db_bind_t[]){db_bind_i32(ship_id)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          if (out_pid) *out_pid = db_res_col_i32(res, 0, &err);
          if (out_cid) *out_cid = db_res_col_i32(res, 1, &err);
          db_res_finalize(res); return 0;
      }
      db_res_finalize(res);
  }
  return -1;
}

bool db_is_ship_piloted (db_t *db, int ship_id)
{
  if (!db) return false;
  db_res_t *res = NULL; db_error_t err; bool piloted = false;
  if (db_query(db, "SELECT 1 FROM players WHERE ship = $1 LIMIT 1;", (db_bind_t[]){db_bind_i32(ship_id)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) piloted = true;
      db_res_finalize(res);
  }
  return piloted;
}

int db_recall_fighter_asset (db_t *db, int asset_id, int player_id) { (void)db; (void)asset_id; (void)player_id; return -1; }

int db_get_config_int (db_t *db, const char *key, int def)
{
  if (!db) return def;
  db_res_t *res = NULL; db_error_t err; int val = def;
  if (db_query(db, "SELECT value FROM server_config WHERE key = $1 AND value_type = 'INTEGER';", (db_bind_t[]){db_bind_text(key)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) val = db_res_col_i32(res, 0, &err);
      db_res_finalize(res);
  }
  return val;
}

bool db_get_config_bool (db_t *db, const char *key, bool def)
{
  if (!db) return def;
  db_res_t *res = NULL; db_error_t err; bool val = def;
  if (db_query(db, "SELECT value FROM server_config WHERE key = $1 AND value_type = 'BOOLEAN';", (db_bind_t[]){db_bind_text(key)}, 1, &res, &err)) {
      if (db_res_step(res, &err)) val = db_res_col_bool(res, 0, &err);
      db_res_finalize(res);
  }
  return val;
}

void h_generate_hex_uuid (char *buffer, size_t buffer_size)
{
  if (buffer_size < 37) return;
  static const char *hex = "0123456789abcdef";
  for (int i = 0; i < 36; i++) {
      if (i == 8 || i == 13 || i == 18 || i == 23) buffer[i] = '-';
      else buffer[i] = hex[rand() % 16];
  }
  buffer[36] = '\0';
}

int db_ensure_planet_bank_accounts (db_t *db) { (void)db; return 0; }
int db_chain_traps_and_bridge (db_t *db, int fedspace_max) { (void)db; (void)fedspace_max; return 0; }
int db_ship_rename_if_owner (db_t *db, int player_id, int ship_id, const char *new_name)
{
  if (!db || !new_name) return ERR_DB_MISUSE;
  db_error_t err;
  if (!db_exec(db, "UPDATE ships SET name = $1 WHERE id = $2 AND EXISTS (SELECT 1 FROM ship_ownership WHERE ship_id = $2 AND player_id = $3 AND is_primary = 1);", (db_bind_t[]){db_bind_text(new_name), db_bind_i32(ship_id), db_bind_i32(player_id)}, 3, &err)) return err.code;
  return 0;
}
int db_player_info_json (db_t *db, int player_id, json_t **out)
{
  if (!db || !out) return ERR_DB_MISUSE;
  db_res_t *res = NULL; db_error_t err;
  if (db_query(db, "SELECT * FROM player_info_v1 WHERE id = $1;", (db_bind_t[]){db_bind_i32(player_id)}, 1, &res, &err)) {
      int rc = stmt_to_json_array(res, out, &err); db_res_finalize(res); return rc;
  }
  return -1;
}
int db_player_info_selected_fields (db_t *db, int player_id, const json_t *fields, json_t **out)
{
  (void)fields; return db_player_info_json(db, player_id, out);
}
int db_ensure_planet_bank_accounts_deprecated (sqlite3 *db) { (void)db; return 0; }
