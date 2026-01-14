#define TW_DB_INTERNAL 1
#include "db_int.h"
/* src/database_cmd.c */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <jansson.h>
#include <stdbool.h>

/* local includes */
#include "common.h"
#include "server_config.h"
#include "game_db.h"
#include "repo_cmd.h"
#include "server_log.h"
#include "server_cron.h"
#include "errors.h"
#include "db/db_api.h"
#include "db/sql_driver.h"


/* ==================================================================== */


/* STATIC HELPER DEFINITIONS                                            */


/* ==================================================================== */


int
h_get_port_commodity_quantity (db_t *db,
                               int port_id,
                               const char *commodity_code,
                               int *qty_out)
{
  if (!db || port_id <= 0 || !commodity_code || !*commodity_code || !qty_out)
    {
      return ERR_DB_MISUSE;
    }

  const char *sql =
    "SELECT quantity "
    "FROM entity_stock "
    "WHERE entity_type = 'port' AND entity_id = {1} AND commodity_code = {2} "
    "LIMIT 1;";

  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_DB_NOT_FOUND;

  *qty_out = 0;

  db_bind_t binds[] = {
    db_bind_i32 (port_id),
    db_bind_text (commodity_code)
  };

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, binds, 2, &res, &err))
    {
      /* keep behaviour: propagate as DB error */
      rc = err.code ? err.code : ERR_DB;
      goto cleanup;
    }

  if (db_res_step (res, &err))
    {
      *qty_out = db_res_col_int (res, 0, &err);
      rc = 0;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


static int
stmt_to_json_array (db_res_t *st, json_t **out_array, db_error_t *err);


/* Parse "2,3,4,5" -> [2,3,4,5] */
json_t *
parse_neighbors_csv (const char *txt)
{
  json_t *arr = json_array ();
  if (!txt)
    {
      return arr;
    }
  const char *p = txt;


  while (*p)
    {
      while (*p == ' ' || *p == '\t')
        {
          p++;                  /* trim left */
        }
      const char *start = p;


      while (*p && *p != ',')
        {
          p++;
        }
      int len = (int) (p - start);


      if (len > 0)
        {
          char buf[32];


          if (len >= (int) sizeof (buf))
            {
              len = (int) sizeof (buf) - 1;     /* defensive */
            }
          memcpy (buf, start, len);
          buf[len] = '\0';
          int id = atoi (buf);


          if (id > 0)
            {
              json_array_append_new (arr, json_integer (id));
            }
        }
      if (*p == ',')
        {
          p++;                  /* skip comma */
        }
    }
  return arr;
}


static int
db_is_npc_player (db_t *db, int player_id)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  const char *sql = "SELECT is_npc FROM players WHERE player_id = {1};";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  int is_npc = 0;
  int rc = 0;


  if (db_query (db, sql_converted, (db_bind_t[]){ db_bind_i32 (player_id) }, 1, &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          is_npc = (int) db_res_col_i32 (res, 0, &err);
        }
      rc = is_npc;
      goto cleanup;
    }
  rc = 0;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


static struct {
  const char *client_name;
  const char *view_name;
} player_field_map[] = {
  {"id", "player_id"},
  {"username", "player_name"},
  {"number", "player_number"},
  {"sector", "sector_id"},
  {"sector_name", "sector_name"},
  {"credits", "petty_cash"},   /* Client requested "credits" maps to "petty_cash" in view */
  {"alignment", "alignment"},
  {"experience", "experience"},
  {"ship_number", "ship_number"},
  {"ship_id", "ship_id"},
  {"ship_name", "ship_name"},
  {"ship_type_id", "ship_type_id"},
  {"ship_type_name", "ship_type_name"},
  {"ship_holds_capacity", "ship_holds_capacity"},
  {"ship_holds_current", "ship_holds_current"},
  {"ship_fighters", "ship_fighters"},
  {"ship_mines", "ship_mines"},
  {"ship_limpets", "ship_limpets"},
  {"ship_genesis", "ship_genesis"},
  {"ship_photons", "ship_photons"},
  {"ship_beacons", "ship_beacons"},
  {"ship_colonists", "ship_colonists"},
  {"ship_equipment", "ship_equipment"},
  {"ship_organics", "ship_organics"},
  {"ship_ore", "ship_ore"},
  {"ship_ported", "ship_ported"},
  {"ship_onplanet", "ship_onplanet"},
  {"approx_worth", "approx_worth"},
  {NULL, NULL}   /* Sentinel */
};


/* Function to get the corresponding view column name */
static const char *
get_player_view_column_name (const char *client_name)
{
  for (int i = 0; player_field_map[i].client_name != NULL; i++)
    {
      if (strcmp (client_name, player_field_map[i].client_name) == 0)
        {
          return player_field_map[i].view_name;
        }
    }
  return NULL;   /* Not found */
}


static int
column_exists_unlocked (db_t *db, const char *table, const char *col)
{
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = 0;
  /* Standard SQL information_schema query */
  const char *sql =
    "SELECT 1 FROM information_schema.columns WHERE table_name = {1} AND column_name = {2};";
  db_bind_t params[] = { db_bind_text (table), db_bind_text (col) };
  int found = 0;

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, params, 2, &res, &err))
    {
      rc = 0;  /* query failed, column doesn't exist */
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      found = 1;
    }
  rc = found;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_ensure_ship_perms_column (db_t *db)
{
  if (!db)
    {
      return -1;
    }
  if (!column_exists_unlocked (db, "ships", "perms"))
    {
      db_error_t err;


      if (!db_exec (db,
                    "ALTER TABLE ships ADD COLUMN perms INTEGER DEFAULT 0;",
                    NULL,
                    0,
                    &err))
        {
          LOGE ("db_ensure_ship_perms_column: ALTER TABLE failed: %s",
                err.message);
          return -1;
        }
      LOGI ("db_ensure_ship_perms_column: added perms column to ships table.");
    }
  return 0;
}


static int
stmt_to_json_array (db_res_t *st, json_t **out_array, db_error_t *err)
{
  json_t *arr = NULL;
  int rc = 0;

  if (!out_array)
    {
      while (db_res_step (st, err))
        {
        }
      return 0;
    }

  arr = json_array ();
  if (!arr)
    {
      return ERR_NOMEM;
    }

  while (db_res_step (st, err))
    {
      int cols = db_res_col_count (st);
      json_t *obj = json_object ();

      if (!obj)
        {
          rc = ERR_NOMEM;
          goto cleanup;
        }

      for (int i = 0; i < cols; i++)
        {
          const char *col_name = db_res_col_name (st, i);
          db_col_type_t col_type = db_res_col_type (st, i);
          json_t *val = NULL;

          switch (col_type)
            {
              case DB_TYPE_INTEGER:
                val = json_integer (db_res_col_i64 (st, i, err));
                break;
              case DB_TYPE_FLOAT:
                val = json_real (db_res_col_double (st, i, err));
                break;
              case DB_TYPE_TEXT:
                val = json_string (db_res_col_text (st, i, err) ?: "");
                break;
              case DB_TYPE_NULL:
                val = json_null ();
                break;
              default:
                val = json_null ();
                break;
            }

          if (!val)
            {
              json_decref (obj);
              rc = ERR_NOMEM;
              goto cleanup;
            }

          if (json_object_set_new (obj, col_name, val) < 0)
            {
              json_decref (obj);
              rc = ERR_NOMEM;
              goto cleanup;
            }
        }

      if (json_array_append_new (arr, obj) < 0)
        {
          json_decref (obj);
          rc = ERR_NOMEM;
          goto cleanup;
        }
    }

  /* Check if db_res_step failed with an error */
  if (err && err->code != 0)
    {
      rc = err->code;
      goto cleanup;
    }

  *out_array = arr;
  arr = NULL;  /* Transfer ownership to caller */
  rc = 0;

cleanup:
  if (arr)
    json_decref (arr);
  return rc;
}


/* ==================================================================== */


/* CORE PLAYER LOGIC                                                    */


/* ==================================================================== */


int
db_player_update_commission (db_t *db, int player_id)
{
  if (!db)
    {
      return ERR_DB_MISUSE;
    }
  db_error_t err;


  for (int retry = 0; retry < 3; retry++)
    {
      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
        {
          if (err.code == ERR_DB_BUSY)
            {
              usleep (100000); continue;
            }
          return err.code;
        }
      db_res_t *res = NULL;
      int align = 0; long long exp = 0;
      const char *sql_sel =
        "SELECT alignment, experience FROM players WHERE player_id = {1} FOR UPDATE;";
      db_bind_t p_sel[] = { db_bind_i32 (player_id) };

      char sql_sel_converted[256];
      sql_build(db, sql_sel, sql_sel_converted, sizeof(sql_sel_converted));

      if (!db_query (db, sql_sel_converted, p_sel, 1, &res, &err))
        {
          goto rollback;
        }
      if (db_res_step (res, &err))
        {
          align = db_res_col_i32 (res, 0, &err);
          exp = db_res_col_i64 (res, 1, &err);
          db_res_finalize (res);
        }
      else
        {
          db_res_finalize (res);
          if (err.code == 0)
            {
              err.code = ERR_NOT_FOUND;
            }
          goto rollback;
        }

      int band_id = 0, is_evil = 0;


      if (db_alignment_band_for_value (db, align, &band_id, NULL, NULL, NULL,
                                       &is_evil, NULL, NULL) != 0)
        {
          goto rollback;
        }

      int new_comm_id = 0;


      if (db_commission_for_player (db, is_evil, exp, &new_comm_id, NULL,
                                    NULL) != 0)
        {
          goto rollback;
        }

      const char *sql_upd =
        "UPDATE players SET commission_id = {1} WHERE player_id = {2};";
      db_bind_t p_upd[] = { db_bind_i32 (new_comm_id),
                            db_bind_i32 (player_id) };

      char sql_upd_converted[256];
      sql_build(db, sql_upd, sql_upd_converted, sizeof(sql_upd_converted));

      if (!db_exec (db, sql_upd_converted, p_upd, 2, &err))
        {
          goto rollback;
        }
      if (!db_tx_commit (db, &err))
        {
          goto rollback;
        }
      return 0;
rollback:
      db_tx_rollback (db, &err);
      if (err.code == ERR_DB_BUSY)
        {
          usleep (100000); continue;
        }
      return err.code;
    }
  return ERR_DB_BUSY;
}


int
db_commission_for_player (db_t *db,
                          int is_evil_track,
                          long long xp,
                          int *out_id,
                          char **out_title,
                          int *out_is_evil)
{
  if (!db)
    {
      return ERR_DB_MISUSE;
    }
  
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_NOT_FOUND;
  
  const char *sql =
    "SELECT commission_id, description, is_evil FROM commission WHERE is_evil = {1} AND min_exp <= {2} ORDER BY min_exp DESC LIMIT 1;";
  db_bind_t params[] = { db_bind_bool (is_evil_track ? true : false),
                         db_bind_i64 (xp) };

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, params, 2, &res, &err))
    {
      rc = err.code;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      if (out_id)
        {
          *out_id = db_res_col_i32 (res, 0, &err);
        }
      if (out_title)
        {
          *out_title = strdup (db_res_col_text (res, 1, &err) ?: "");
        }
      if (out_is_evil)
        {
          *out_is_evil = db_res_col_bool (res, 2, &err);
        }
      rc = 0;
      goto cleanup;
    }
  db_res_finalize (res);
  res = NULL;

  /* Fallback to lowest rank */
  sql =
    "SELECT commission_id, description, is_evil FROM commission WHERE is_evil = {1} ORDER BY min_exp ASC LIMIT 1;";
  db_bind_t p_fall[] = { db_bind_bool (is_evil_track ? true : false) };

  char sql_fall_converted[256];
  sql_build(db, sql, sql_fall_converted, sizeof(sql_fall_converted));

  if (!db_query (db, sql_fall_converted, p_fall, 1, &res, &err))
    {
      rc = err.code;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      if (out_id)
        {
          *out_id = db_res_col_i32 (res, 0, &err);
        }
      if (out_title)
        {
          *out_title = strdup (db_res_col_text (res, 1, &err) ?: "");
        }
      if (out_is_evil)
        {
          *out_is_evil = db_res_col_bool (res, 2, &err);
        }
      rc = 0;
      goto cleanup;
    }
  rc = ERR_NOT_FOUND;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_alignment_band_for_value (db_t *db,
                             int align,
                             int *out_id,
                             char **out_code,
                             char **out_name,
                             int *out_is_good,
                             int *out_is_evil,
                             int *out_can_buy_iss,
                             int *out_can_rob_ports)
{
  if (!db)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = 0;
  const char *sql =
    "SELECT alignment_band_id, code, name, is_good, is_evil, can_buy_iss, can_rob_ports FROM alignment_band WHERE {1} BETWEEN min_align AND max_align LIMIT 1;";
  db_bind_t params[] = { db_bind_i32 (align) };

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, params, 1, &res, &err))
    {
      rc = -1;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      if (out_id)
        {
          *out_id = db_res_col_i32 (res, 0, &err);
        }
      if (out_code)
        {
          *out_code = strdup (db_res_col_text (res, 1, &err) ?: "NEUTRAL");
        }
      if (out_name)
        {
          *out_name = strdup (db_res_col_text (res, 2, &err) ?: "Neutral");
        }
      if (out_is_good)
        {
          *out_is_good = db_res_col_bool (res, 3, &err);
        }
      if (out_is_evil)
        {
          *out_is_evil = db_res_col_bool (res, 4, &err);
        }
      if (out_can_buy_iss)
        {
          *out_can_buy_iss = db_res_col_bool (res, 5, &err);
        }
      if (out_can_rob_ports)
        {
          *out_can_rob_ports = db_res_col_bool (res, 6, &err);
        }
    }
  else
    {
      if (out_id)
        {
          *out_id = -1;
        }
      if (out_code)
        {
          *out_code = strdup ("NEUTRAL");
        }
      if (out_name)
        {
          *out_name = strdup ("Neutral");
        }
      if (out_is_good)
        {
          *out_is_good = 0;
        }
      if (out_is_evil)
        {
          *out_is_evil = 0;
        }
      if (out_can_buy_iss)
        {
          *out_can_buy_iss = 0;
        }
      if (out_can_rob_ports)
        {
          *out_can_rob_ports = 0;
        }
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


/* ==================================================================== */


/* ==================================================================== */


/* COMMODITIES                                                          */


/* ==================================================================== */


int
db_commodity_get_price (db_t *db, const char *code, int *out_price)
{
  if (!db || !code || !out_price)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_DB;


  static const char *cols[] = { "last_price", "price", "base_price", NULL };


  for (int i = 0; cols[i]; i++)
    {
      char sql[256];


      snprintf (sql,
                sizeof(sql),
                "SELECT %s FROM commodities WHERE code = {1};",
                cols[i]);
      char sql_converted[256];
      sql_build(db, sql, sql_converted, sizeof(sql_converted));
      if (db_query (db, sql_converted, (db_bind_t[]){db_bind_text (code)}, 1, &res, &err))
        {
          if (db_res_step (res, &err))
            {
              *out_price = db_res_col_i32 (res, 0, &err);
              rc = 0;
              goto cleanup;
            }
          rc = ERR_NOT_FOUND;
          goto cleanup;
        }
      db_error_clear (&err);
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_commodity_update_price (db_t *db, const char *code, int price)
{
  if (!db || !code)
    {
      return ERR_DB_MISUSE;
    }
  db_error_t err;
  static const char *cols[] = { "last_price", "price", "base_price", NULL };


  for (int i = 0; cols[i]; i++)
    {
      char sql[256];


      snprintf (sql,
                sizeof(sql),
                "UPDATE commodities SET %s = {1} WHERE code = {2};",
                cols[i]);
      if (db_exec (db, sql,
                   (db_bind_t[]){db_bind_i32 (price), db_bind_text (code)}, 2,
                   &err))
        {
          return 0;
        }
      db_error_clear (&err);
    }
  return ERR_DB;
}


int
db_commodity_create_order (db_t *db,
                           const char *actor_type,
                           int actor_id,
                           const char *code,
                           const char *side,
                           int qty,
                           int price)
{
  if (!db || !code || !side)
    {
      return ERR_DB_MISUSE;
    }
  db_error_t err;
  const char *sql =
    "INSERT INTO commodity_orders (commodity_id, actor_type, actor_id, side, quantity, price) "
    "SELECT commodities_id, {2}, {3}, {4}, {5}, {6} FROM commodities WHERE code = {1};";
  db_bind_t params[] = {
    db_bind_text (code), db_bind_text (actor_type), db_bind_i32 (actor_id),
    db_bind_text (side), db_bind_i32 (qty), db_bind_i32 (price)
  };

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db, sql_converted, params, 6, &err))
    {
      return err.code;
    }
  return 0;
}


int
db_commodity_fill_order (db_t *db, int order_id, int qty)
{
  /* Use stored procedure for atomicity */
  if (!db || order_id <= 0 || qty <= 0)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = 0;

  // Call market_fill_order(p_order_id bigint, p_fill_qty bigint, p_actor_id bigint)
  // Note: Actor ID is not passed here? Legacy signature mismatch.
  // We'll pass 0 as system actor or similar if signature can't change.
  // Actually, let's just do the SQL update if we can't change signature,
  // or assume caller handles logic.
  // Postgres version:
  const char *sql = "SELECT * FROM market_fill_order({1}, {2}, 0);";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  db_bind_t params[] = { db_bind_i64 (order_id), db_bind_i64 (qty) };


  if (db_query (db, sql_converted, params, 2, &res, &err))
    {
      int code = 0;


      if (db_res_step (res, &err))
        {
          code = db_res_col_i32 (res,
                                 0,
                                 &err);        // (code, message, id) tuple
        }
      rc = (code == 0) ? 0 : ERR_DB_CONSTRAINT;
      goto cleanup;
    }
  rc = err.code;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_commodity_get_orders (db_t *db,
                         const char *code,
                         const char *status,
                         json_t **out)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err = {0};
  const char *sql =
    "SELECT co.commodity_orders_id, co.actor_type, co.actor_id, c.code as commodity, co.side, co.quantity, co.price, co.status "
    "FROM commodity_orders co JOIN commodities c ON co.commodity_id = c.commodities_id "
    "WHERE c.code = {1} AND co.status = {2} ORDER BY co.ts DESC;";
  db_res_t *res = NULL;
  int rc = 0;

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db,
                 sql_converted,
                 (db_bind_t[]){db_bind_text (code), db_bind_text (status)},
                 2,
                 &res,
                 &err))
    {
      rc = err.code;
      goto cleanup;
    }
  rc = stmt_to_json_array (res, out, &err);

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_commodity_get_trades (db_t *db, const char *code, int limit, json_t **out)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err = {0};
  const char *sql =
    "SELECT ct.id, c.code as commodity, ct.quantity, ct.price, ct.ts "
    "FROM commodity_trades ct JOIN commodities c ON ct.commodity_id = c.commodities_id "
    "WHERE c.code = {1} ORDER BY ct.ts DESC LIMIT {2};";
  db_res_t *res = NULL;
  int rc = 0;

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db,
                 sql_converted,
                 (db_bind_t[]){db_bind_text (code), db_bind_i32 (limit)},
                 2,
                 &res,
                 &err))
    {
      rc = err.code;
      goto cleanup;
    }
  rc = stmt_to_json_array (res, out, &err);

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


/* ==================================================================== */


/* PLANETS & GOODS                                                      */


/* ==================================================================== */


int
db_planet_get_goods_on_hand (db_t *db,
                             int planet_id,
                             const char *code,
                             int *out_qty)
{
  if (!db || !code || !out_qty)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_DB;
  const char *sql =
    "SELECT quantity FROM planet_goods WHERE planet_id = {1} AND commodity = {2};";
  db_bind_t params[] = { db_bind_i32 (planet_id), db_bind_text (code) };

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, params, 2, &res, &err))
    {
      rc = err.code;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      *out_qty = db_res_col_i32 (res, 0, &err);
      rc = 0;
    }
  else
    {
      *out_qty = 0;
      rc = 0;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_planet_update_goods_on_hand (db_t *db,
                                int planet_id,
                                const char *code,
                                int delta)
{
  if (!db || !code)
    {
      return ERR_DB_MISUSE;
    }
  db_error_t err;
  /* Postgres UPSERT */
  const char *sql =
    "INSERT INTO planet_goods (planet_id, commodity, quantity, max_capacity, production_rate) "
    "VALUES ({1}, {2}, GREATEST({3}, 0), 1000000, 0) "
    "ON CONFLICT(planet_id, commodity) DO UPDATE SET quantity = GREATEST(planet_goods.quantity + {3}, 0);";

  db_bind_t params[] = { db_bind_i32 (planet_id), db_bind_text (code),
                         db_bind_i32 (delta) };

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db, sql_converted, params, 3, &err))
    {
      return err.code;
    }
  return 0;
}


/* ==================================================================== */


/* SHIPS & STATUS                                                       */


/* ==================================================================== */


int
db_mark_ship_destroyed (db_t *db, int ship_id)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_DB;
  /* Use stored procedure */
  const char *sql = "SELECT * FROM ship_destroy({1});";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));


  if (!db_query (db, sql_converted, (db_bind_t[]){db_bind_i64 (ship_id)}, 1, &res, &err))
    {
      rc = err.code;
      goto cleanup;
    }
  rc = 0;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_clear_player_active_ship (db_t *db, int player_id)
{
  /* This is handled by ship_destroy typically, but manual clear: */
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err;

  const char *sql_template = "UPDATE players SET ship_id = NULL WHERE player_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  /* Players table doesn't have active_ship_id in sqlite schema, but postgres schema has it?
     Actually schema in 000_tables.sql has 'ship_id' column. */
  if (!db_exec (db, sql,
                (db_bind_t[]){db_bind_i32 (player_id)}, 1, &err))
    {
      return err.code;
    }
  return 0;
}


int
db_increment_player_stat (db_t *db, int pid, const char *stat)
{
  if (!db || !stat)
    {
      return ERR_DB_MISUSE;
    }
  char sql[256];


  /* Use double quotes for the identifier to be backend-neutral */
  snprintf (sql,
            sizeof (sql),
            "UPDATE players SET \"%s\" = \"%s\" + 1 WHERE player_id = {1};",
            stat,
            stat);
  db_error_t err;
  db_bind_t params[] = { db_bind_i32 (pid) };

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db, sql_converted, params, 1, &err))
    {
      LOGE ("db_increment_player_stat: Failed for %s: %s", stat, err.message);
      return err.code;
    }
  return 0;
}


int
db_get_player_xp (db_t *db, int pid)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int xp = 0;

  const char *sql_template = "SELECT experience FROM players WHERE player_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_query (db, sql,
                 (db_bind_t[]){db_bind_i32 (pid)}, 1, &res, &err))
    {
      xp = 0;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      xp = db_res_col_i32 (res,
                           0,
                           &err);
    }
  else
    {
      xp = 0;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return xp;
}


int
db_update_player_xp (db_t *db, int pid, int xp)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err;

  const char *sql_template = "UPDATE players SET experience = {1} WHERE player_id = {2};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_exec (db, sql,
                (db_bind_t[]){db_bind_i32 (xp), db_bind_i32 (pid)}, 2, &err))
    {
      return err.code;
    }
  return 0;
}


bool
db_shiptype_has_escape_pod (db_t *db, int ship_id)
{
  if (!db)
    {
      return false;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int tid = -1;
  /* Need to join shiptypes? Or assumes ship has type_id */
  const char *sql =
    "SELECT st.shiptypes_id FROM ships s JOIN shiptypes st ON s.type_id = st.shiptypes_id WHERE s.ship_id = {1} AND st.name LIKE '%Escape%';";

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, (db_bind_t[]){db_bind_i32 (ship_id)}, 1, &res, &err))
    {
      tid = -1;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      tid = db_res_col_i32 (res, 0, &err);
    }
  else
    {
      tid = -1;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return (tid > 0);
}


int
db_create_podded_status_entry (db_t *db, int pid)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err;
  
  const char *sql_template =
    "INSERT INTO podded_status (player_id, status, podded_count_today, podded_last_reset) VALUES ({1}, 'active', 0, %s) ON CONFLICT DO NOTHING;";

  char sql_dialect[512];
  sql_build(db, sql_template, sql_dialect, sizeof sql_dialect);

  char sql[512];
  snprintf(sql, sizeof sql, sql_dialect, sql_now_expr(db));

  if (!db_exec (db, sql, (db_bind_t[]){db_bind_i32 (pid)}, 1, &err))
    {
      return err.code;
    }
  return 0;
}


int
db_get_player_podded_count_today (db_t *db, int pid)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int cnt = 0;


  const char *sql_template = "SELECT podded_count_today FROM podded_status WHERE player_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_query (db,
                 sql,
                 (db_bind_t[]){db_bind_i32 (pid)},
                 1,
                 &res,
                 &err))
    {
      cnt = 0;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      cnt = db_res_col_i32 (res, 0, &err);
    }
  else
    {
      db_create_podded_status_entry (db, pid);
      cnt = 0;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return cnt;
}


long long
db_get_player_podded_last_reset (db_t *db, int pid)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  long long ts = 0;


  const char *sql_template = "SELECT podded_last_reset FROM podded_status WHERE player_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_query (db,
                 sql,
                 (db_bind_t[]){db_bind_i32 (pid)},
                 1,
                 &res,
                 &err))
    {
      ts = 0;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      ts = db_res_col_i64 (res, 0, &err);
    }
  else
    {
      db_create_podded_status_entry (db, pid);
      ts = time (NULL);
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return ts;
}


int
db_reset_player_podded_count (db_t *db, int pid, long long ts)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err;


  if (!db_exec (db,
                "UPDATE podded_status SET podded_count_today = 0, podded_last_reset = {1} WHERE player_id = {2};",
                (db_bind_t[]){db_bind_i64 (ts), db_bind_i32 (pid)},
                2,
                &err))
    {
      return err.code;
    }
  return 0;
}


int
db_update_player_podded_status (db_t *db,
                                int pid,
                                const char *status,
                                long long until)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err;


  const char *sql_template = "UPDATE podded_status SET status = {1}, last_updated = CURRENT_TIMESTAMP WHERE player_id = {2};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_exec (db,
                sql,
                (db_bind_t[]){db_bind_text (status), db_bind_i32 (pid)},
                2,
                &err))
    {
      return err.code;
    }
  return 0;
}


/* ==================================================================== */


/* UTILITIES                                                            */


/* ==================================================================== */


int
h_get_cluster_id_for_sector (db_t *db, int sid, int *out_cid)
{
  if (!db || !out_cid)
    {
      return ERR_DB_MISUSE;
    }
  
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_DB_NOT_FOUND;


  if (!db_query (db,
                 "SELECT cluster_id FROM cluster_sectors WHERE sector_id = {1};",
                 (db_bind_t[]){db_bind_i32 (sid)},
                 1,
                 &res,
                 &err))
    {
      rc = err.code;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      *out_cid = db_res_col_i32 (res, 0, &err);
      rc = 0;
    }
  else
    {
      rc = ERR_DB_NOT_FOUND;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
h_get_cluster_alignment (db_t *db, int cid, int *out_align)
{
  if (!db || !out_align)
    {
      return ERR_DB_MISUSE;
    }
  
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_DB_NOT_FOUND;

  const char *sql_template = "SELECT alignment FROM clusters WHERE clusters_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i32 (cid)}, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          *out_align = db_res_col_i32 (res, 0, &err);
          rc = 0;
        }
      else
        {
          rc = ERR_DB_NOT_FOUND;
        }
    }
  else
    {
      rc = err.code;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
h_get_cluster_alignment_band (db_t *db, int cid, int *out_bid)
{
  int align = 0; if (h_get_cluster_alignment (db, cid, &align) != 0)
    {
      return -1;
    }
  return db_alignment_band_for_value (db,
                                      align,
                                      out_bid,
                                      NULL,
                                      NULL,
                                      NULL,
                                      NULL,
                                      NULL,
                                      NULL);
}


int
db_get_shiptype_info (db_t *db, int tid, int *h, int *f, int *s)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_NOT_FOUND;


  if (!db_query (db,
                 "SELECT initialholds, maxfighters, maxshields FROM shiptypes WHERE shiptypes_id = {1};",
                 (db_bind_t[]){db_bind_i32 (tid)},
                 1,
                 &res,
                 &err))
    {
      rc = err.code;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      *h = db_res_col_i32 (res, 0, &err);
      *f = db_res_col_i32 (res, 1, &err);
      *s = db_res_col_i32 (res, 2, &err);
      rc = 0;
    }
  else
    {
      rc = ERR_NOT_FOUND;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_player_land_on_planet (db_t *db, int pid, int planet_id)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_DB;
  /* Use stored procedure: SELECT * FROM player_land(pid, planet_id) */
  const char *sql = "SELECT * FROM player_land({1}, {2});";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));


  if (!db_query (db,
                 sql_converted,
                 (db_bind_t[]){db_bind_i64 (pid), db_bind_i64 (planet_id)},
                 2,
                 &res,
                 &err))
    {
      rc = err.code;
      goto cleanup;
    }
  int code = 0;


  if (db_res_step (res, &err))
    {
      code = db_res_col_i32 (res, 0, &err);
    }
  rc = (code == 0) ? 0 : code;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_player_launch_from_planet (db_t *db, int pid, int *out_sid)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_DB;
  const char *sql = "SELECT * FROM player_launch({1});";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));


  if (!db_query (db, sql_converted, (db_bind_t[]){db_bind_i64 (pid)}, 1, &res, &err))
    {
      rc = err.code;
      goto cleanup;
    }
  int code = 0;


  if (db_res_step (res, &err))
    {
      code = db_res_col_i32 (res,
                             0,
                             &err);
    }
  if (code == 0)
    {
      /* Fetch current sector */
      if (db_player_get_sector (db, pid, out_sid) == 0)
        {
          rc = 0;
          goto cleanup;
        }
    }
  rc = code;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_get_port_sector (db_t *db, int port_id)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int sid = 0;

  const char *sql_template = "SELECT sector_id FROM ports WHERE port_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_query (db, sql,
                 (db_bind_t[]){db_bind_i32 (port_id)}, 1, &res, &err))
    {
      sid = 0;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      sid = db_res_col_i32 (res,
                            0,
                            &err);
    }
  else
    {
      sid = 0;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return sid;
}


int
db_bounty_create (db_t *db,
                  const char *pbt,
                  int pbid,
                  const char *tt,
                  int tid,
                  long long r,
                  const char *desc)
{
  (void)desc; if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err;
  const char *sql =
    "INSERT INTO bounties (posted_by_type, posted_by_id, target_type, target_id, reward, status, posted_ts) VALUES ({1}, {2}, {3}, {4}, {5}, 'open', CURRENT_TIMESTAMP);";

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db, sql_converted,
                (db_bind_t[]){db_bind_text (pbt), db_bind_i32 (pbid),
                              db_bind_text (tt), db_bind_i32 (tid),
                              db_bind_i64 (r)}, 5, &err))
    {
      return err.code;
    }
  return 0;
}


int
db_player_get_alignment (db_t *db, int pid, int *align)
{
  if (!db || !align)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_NOT_FOUND;


  if (!db_query (db,
                 "SELECT alignment FROM players WHERE player_id = {1};",
                 (db_bind_t[]){db_bind_i32 (pid)},
                 1,
                 &res,
                 &err))
    {
      rc = err.code;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      *align = db_res_col_i32 (res, 0, &err);
      rc = 0;
    }
  else
    {
      rc = ERR_NOT_FOUND;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_get_law_config_int (const char *key, int def)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return def;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int value = def;
  int rc = def;
  const char *sql =
    "SELECT value FROM law_enforcement_config WHERE key = {1} AND value_type = 'INTEGER';";

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, (db_bind_t[]){ db_bind_text (key) }, 1, &res, &err))
    {
      rc = def;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      value = (int) db_res_col_i32 (res, 0, &err);
      rc = value;
    }
  else
    {
      rc = def;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


char *
db_get_law_config_text (const char *key, const char *def)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return strdup (def);
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  char *value = NULL;
  const char *sql =
    "SELECT value FROM law_enforcement_config WHERE key = {1} AND value_type = 'TEXT';";

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, (db_bind_t[]){ db_bind_text (key) }, 1, &res, &err))
    {
      value = NULL;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      const char *txt = db_res_col_text (res, 0, &err);


      if (txt)
        {
          value = strdup (txt);
        }
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return value ? value : strdup (def);
}


int
db_player_get_last_rob_attempt (int pid, int *lpid, long long *lts)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_NOT_FOUND;
  if (!db_query (db,
                 "SELECT port_id, last_attempt_at FROM player_last_rob WHERE player_id = {1};",
                 (db_bind_t[]){db_bind_i32 (pid)},
                 1,
                 &res,
                 &err))
    {
      rc = err.code;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      if (lpid)
        {
          *lpid = db_res_col_i32 (res, 0, &err);
        }
      if (lts)
        {
          *lts = db_res_col_i64 (res, 1, &err);
        }
      rc = 0;
    }
  else
    {
      rc = ERR_NOT_FOUND;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_player_set_last_rob_attempt (int pid, int lpid, long long lts)
{
  db_t *db = game_db_get_handle (); if (!db)
    {
      return -1;
    }
  db_error_t err;
  const char *sql =
    "INSERT INTO player_last_rob (player_id, port_id, last_attempt_at, was_success) VALUES ({1}, {2}, {3}, 0) ON CONFLICT(player_id) DO UPDATE SET port_id = excluded.port_id, last_attempt_at = excluded.last_attempt_at;";
  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (!db_exec (db, sql_converted,
                (db_bind_t[]){db_bind_i32 (pid), db_bind_i32 (lpid),
                              db_bind_i64 (lts)}, 3, &err))
    {
      return err.code;
    }
  return 0;
}


int
db_port_add_bust_record (int port_id, int pid, long long ts, const char *type)
{
  db_t *db = game_db_get_handle (); if (!db)
    {
      return -1;
    }
  db_error_t err;
  const char *sql =
    "INSERT INTO port_busts (port_id, player_id, last_bust_at, bust_type) VALUES ({1}, {2}, {3}, {4}) ON CONFLICT(port_id, player_id) DO UPDATE SET last_bust_at = excluded.last_bust_at;";
  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (!db_exec (db, sql_converted,
                (db_bind_t[]){db_bind_i32 (port_id), db_bind_i32 (pid),
                              db_bind_i64 (ts), db_bind_text (type)}, 4, &err))
    {
      return err.code;
    }
  return 0;
}


int
db_port_get_active_busts (int port_id, json_t **out)
{
  db_t *db = game_db_get_handle ();
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;
  const char *sql =
    "SELECT player_id, bust_type, last_bust_at FROM port_busts WHERE port_id = {1} AND active = 1;";

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, (db_bind_t[]){ db_bind_i32 (port_id) }, 1, &res, &err))
    {
      rc = -1;
      goto cleanup;
    }
  rc = stmt_to_json_array (res, out, &err);

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_port_is_busted (int port_id, int pid)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int busted = 0;
  const char *sql =
    "SELECT 1 FROM port_busts WHERE port_id = {1} AND player_id = {2} LIMIT 1;";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (!db_query (db, sql_converted,
                 (db_bind_t[]){db_bind_i32 (port_id), db_bind_i32 (pid)},
                 2,
                 &res,
                 &err))
    {
      busted = 0;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      busted = 1;
    }
  else
    {
      busted = 0;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return (busted) ? 0 : -1;
}


int
db_get_ship_name (db_t *db, int ship_id, char **out)
{
  if (!db || !out)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_NOT_FOUND;

  const char *sql_template = "SELECT name FROM ships WHERE ship_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_query (db, sql,
                 (db_bind_t[]){db_bind_i32 (ship_id)}, 1, &res, &err))
    {
      rc = err.code;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      *out = strdup (db_res_col_text (res, 0, &err) ?: "");
      rc = 0;
    }
  else
    {
      rc = ERR_NOT_FOUND;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_get_port_name (db_t *db, int port_id, char **out)
{
  if (!db || !out)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_NOT_FOUND;

  const char *sql_template = "SELECT name FROM ports WHERE port_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_query (db, sql,
                 (db_bind_t[]){db_bind_i32 (port_id)}, 1, &res, &err))
    {
      rc = err.code;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      *out = strdup (db_res_col_text (res, 0, &err) ?: "");
      rc = 0;
    }
  else
    {
      rc = ERR_NOT_FOUND;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_log_engine_event (long long ts,
                     const char *type,
                     const char *owner_type,
                     int pid,
                     int sid,
                     json_t *payload,
                     db_t *db_opt)
{
  (void) owner_type;
  db_t *db = db_opt ?: game_db_get_handle (); if (!db)
    {
      return -1;
    }
  char *pstr = json_dumps (payload, 0); if (!pstr)
    {
      return ERR_NOMEM;
    }
  db_error_t err;
  const char *sql =
    "INSERT INTO engine_events (ts, type, actor_player_id, sector_id, payload) VALUES (to_timestamp({1}), {2}, {3}, {4}, {5});";
  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  bool ok = db_exec (db, sql_converted,
                     (db_bind_t[]){db_bind_i64 (ts), db_bind_text (type),
                                   db_bind_i32 (pid), db_bind_i32 (sid),
                                   db_bind_text (pstr)}, 5, &err);


  free (pstr); return ok ? 0 : err.code;
}


int
db_news_post (long long ts, long long exp, const char *cat, const char *txt)
{
  db_t *db = game_db_get_handle (); if (!db)
    {
      return -1;
    }
  db_error_t err;
  /* news_feed schema: published_ts, news_category, article_text */
  const char *sql =
    "INSERT INTO news_feed (published_ts, news_category, article_text) VALUES ({1}, {2}, {3});";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (!db_exec (db, sql_converted,
                (db_bind_t[]){db_bind_i64 (ts), db_bind_text (cat),
                              db_bind_text (txt)}, 3, &err))
    {
      return err.code;
    }
  (void)exp;
  return 0;
}


int
db_get_port_id_by_sector (db_t *db, int sid)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int pid = 0;

  const char *sql_template = "SELECT port_id FROM ports WHERE sector_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_query (db, sql,
                 (db_bind_t[]){db_bind_i32 (sid)}, 1, &res, &err))
    {
      pid = 0;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      pid = db_res_col_i32 (res, 0, &err);
    }
  else
    {
      pid = 0;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return pid;
}


int
db_get_sector_info (int sid,
                    char **nm,
                    int *sz,
                    int *pc,
                    int *sc,
                    int *plc,
                    char **bt)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;
  // Using sector_ops view for aggregation
  const char *sql = "SELECT s.name, 1 as safe_zone, "
                    "(SELECT COUNT(*) FROM ports WHERE sector_id={1}) as pc, "
                    "(SELECT COUNT(*) FROM ships WHERE sector_id={1}) as sc, "
                    "(SELECT COUNT(*) FROM planets WHERE sector_id={1}) as plc, "
                    "s.beacon "
                    "FROM sectors s WHERE s.sector_id = {1};";

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, (db_bind_t[]){db_bind_i32 (sid)}, 1, &res, &err))
    {
      rc = -1;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      if (nm)
        {
          *nm = strdup (db_res_col_text (res, 0, &err) ?: "");
        }
      if (sz)
        {
          *sz = db_res_col_i32 (res, 1, &err);
        }
      if (pc)
        {
          *pc = db_res_col_i32 (res, 2, &err);
        }
      if (sc)
        {
          *sc = db_res_col_i32 (res, 3, &err);
        }
      if (plc)
        {
          *plc = db_res_col_i32 (res, 4, &err);
        }
      if (bt)
        {
          *bt = strdup (db_res_col_text (res, 5, &err) ?: "");
        }
      rc = 0;
    }
  else
    {
      rc = -1;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_news_get_recent (int pid, json_t **out)
{
  (void)pid;
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;
  const char *sql =
    "SELECT news_id, published_ts, news_category, article_text FROM news_feed ORDER BY published_ts DESC LIMIT 50;";


  if (!db_query (db, sql, NULL, 0, &res, &err))
    {
      rc = -1;
      goto cleanup;
    }
  rc = stmt_to_json_array (res, out, &err);

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_port_get_goods_on_hand (db_t *db, int pid, const char *code, int *qty)
{
  if (!db || !qty)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;
  const char *sql =
    "SELECT quantity FROM entity_stock WHERE entity_type='port' AND entity_id={1} AND commodity_code={2};";

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db,
                 sql_converted,
                 (db_bind_t[]){db_bind_i32 (pid), db_bind_text (code)},
                 2,
                 &res,
                 &err))
    {
      rc = -1;
      goto cleanup;
    }
  if (db_res_step (res, &err))
    {
      *qty = db_res_col_i32 (res, 0, &err);
      rc = 0;
    }
  else
    {
      rc = -1;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_path_exists (db_t *db, int from, int to)
{
  if (!db)
    {
      return 0;
    }
  if (from == to)
    {
      return 1;
    }
  db_res_t *res = NULL;
  db_error_t err;
  int max_id = 0;


  if (db_query (db, "SELECT MAX(sector_id) FROM sectors;", NULL, 0, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          max_id = (int) db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }
  if (max_id <= 0)
    {
      return 0;
    }
  unsigned char *visited = calloc (max_id + 1, 1);


  if (!visited)
    {
      return 0;
    }
  int *queue = malloc ((max_id + 1) * sizeof (int));


  if (!queue)
    {
      free (visited);
      return 0;
    }
  int head = 0, tail = 0;


  queue[tail++] = from;
  visited[from] = 1;
  int found = 0;


  const char *sql_warps =
    "SELECT to_sector FROM sector_warps WHERE from_sector = {1};";


  while (head < tail)
    {
      int current = queue[head++];


      if (current == to)
        {
          found = 1;
          break;
        }
      db_res_t *wres = NULL;
      db_bind_t params[] = { db_bind_i32 (current) };

      char sql_warps_converted[256];
      sql_build(db, sql_warps, sql_warps_converted, sizeof(sql_warps_converted));

      if (db_query (db, sql_warps_converted, params, 1, &wres, &err))
        {
          while (db_res_step (wres, &err))
            {
              int next = (int) db_res_col_i32 (wres,
                                               0,
                                               &err);


              if (next > 0 && next <= max_id && !visited[next])
                {
                  visited[next] = 1;
                  queue[tail++] = next;
                }
            }
          db_res_finalize (wres);
        }
    }
  free (visited);
  free (queue);
  return found;
}


int
db_apply_lock_policy_for_pilot (db_t *db, int ship_id, int pilot_id)
{
  if (!db)
    {
      return -1;
    }
  db_error_t err;


  if (pilot_id > 0 && db_is_npc_player (db, pilot_id))
    {
      /* NPC piloted -> lock it */
      const char *sql =
        "UPDATE ships SET flags = COALESCE(flags,0) | {1} WHERE ship_id = {2};";

      char sql_converted[256];
      sql_build(db, sql, sql_converted, sizeof(sql_converted));

      if (!db_exec (db, sql_converted,
                    (db_bind_t[]){ db_bind_i32 (SHIPF_LOCKED),
                                   db_bind_i32 (ship_id) }, 2, &err))
        {
          return err.code;
        }
    }
  else
    {
      /* Not NPC piloted (or unpiloted) -> clear LOCK */
      const char *sql =
        "UPDATE ships SET flags = COALESCE(flags,0) & ~{1} WHERE ship_id = {2};";

      char sql_converted[256];
      sql_build(db, sql, sql_converted, sizeof(sql_converted));

      if (!db_exec (db, sql_converted,
                    (db_bind_t[]){ db_bind_i32 (SHIPF_LOCKED),
                                   db_bind_i32 (ship_id) }, 2, &err))
        {
          return err.code;
        }
    }
  return 0;
}


int
db_destroy_ship (db_t *db, int pid, int sid)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = 0;
  const char *sql = "SELECT * FROM ship_destroy({1});";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));


  if (db_query (db, sql_converted, (db_bind_t[]){db_bind_i64 (sid)}, 1, &res, &err))
    {
      (void)pid;
      rc = 0;
      goto cleanup;
    }
  rc = err.code;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_sector_info_json (db_t *db, int sector_id, json_t **out)
{
  if (!db || !out)
    {
      return ERR_DB_CLOSED;
    }

  json_t *root = NULL;
  db_res_t *res = NULL;
  db_error_t err;
  int rc = 0;

  root = json_object ();
  if (!root)
    {
      return ERR_MEMORY;
    }

  json_object_set_new (root, "sector_id", json_integer (sector_id));

  /* 0) Sector core: name, beacon, security */
  const char *sql_info =
    "SELECT name, beacon, 1 as safe_zone, 0 as security_level FROM sectors WHERE sector_id = {1};";

  char sql_info_converted[256];
  sql_build(db, sql_info, sql_info_converted, sizeof(sql_info_converted));

  if (db_query (db,
                sql_info_converted,
                (db_bind_t[]){ db_bind_i32 (sector_id) },
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          const char *nm = db_res_col_text (res, 0, &err);

          if (nm)
            {
              json_object_set_new (root, "name", json_string (nm));
            }

          const char *btxt = db_res_col_text (res, 1, &err);

          if (btxt && btxt[0])
            {
              json_t *b = json_object ();
              if (b)
                {
                  json_object_set_new (b, "text", json_string (btxt));
                  json_object_set_new (root, "beacon", b);
                }
            }

          json_t *sec = json_object ();
          if (sec)
            {
              json_object_set_new (sec,
                                   "level",
                                   json_integer (db_res_col_i32 (res,
                                                                 3,
                                                                 &err)));
              json_object_set_new (sec, "is_safe_zone",
                                   json_boolean (db_res_col_i32 (res, 2, &err)));
              json_object_set_new (root, "security", sec);
            }
        }
      db_res_finalize (res);
      res = NULL;
    }

  /* 1) Adjacency via sector_warps */
  const char *sql_adj =
    "SELECT to_sector FROM sector_warps FROM from_sector = {1};";

  char sql_adj_converted[256];
  sql_build(db, sql_adj, sql_adj_converted, sizeof(sql_adj_converted));

  if (db_query (db,
                sql_adj_converted,
                (db_bind_t[]){ db_bind_i32 (sector_id) },
                1,
                &res,
                &err))
    {
      json_t *adj = json_array ();
      if (adj)
        {
          while (db_res_step (res, &err))
            {
              json_array_append_new (adj,
                                     json_integer (db_res_col_i32 (res, 0, &err)));
            }
          if (json_array_size (adj) > 0)
            {
              json_object_set_new (root, "adjacent", adj);
            }
          else
            {
              json_decref (adj);
            }
        }
      db_res_finalize (res);
      res = NULL;
    }

  /* 2) Counts */
  const char *sql_counts =
    "SELECT "
    "  (SELECT COUNT(*) FROM ports WHERE sector_id={1}) as pc, "
    "  (SELECT COUNT(*) FROM ships WHERE sector_id={1}) as sc, "
    "  (SELECT COUNT(*) FROM planets WHERE sector_id={1}) as plc;";

  char sql_counts_converted[512];
  sql_build(db, sql_counts, sql_counts_converted, sizeof(sql_counts_converted));

  if (db_query (db,
                sql_counts_converted,
                (db_bind_t[]){ db_bind_i32 (sector_id) },
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          json_object_set_new (root, "port_count",
                               json_integer (db_res_col_i32 (res, 0, &err)));
          json_object_set_new (root, "ship_count",
                               json_integer (db_res_col_i32 (res, 1, &err)));
          json_object_set_new (root, "planet_count",
                               json_integer (db_res_col_i32 (res, 2, &err)));
        }
      db_res_finalize (res);
      res = NULL;
    }

  *out = root;
  return rc;
}


int
db_rand_npc_shipname (db_t *db, char *out, size_t sz)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;


  if (db_query (db,
                "SELECT name FROM npc_shipnames ORDER BY RANDOM() LIMIT 1;",
                NULL,
                0,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          strncpy (out, db_res_col_text (res, 0, &err) ?: "NPC Ship", sz - 1);
          out[sz - 1] = '\0';
          rc = 0;
          goto cleanup;
        }
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_sector_beacon_text (db_t *db, int sid, char **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;

  const char *sql_template = "SELECT beacon FROM sectors WHERE sector_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i32 (sid)}, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          *out = strdup (db_res_col_text (res, 0, &err) ?: "");
          rc = 0;
          goto cleanup;
        }
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_sector_set_beacon (db_t *db, int sid, const char *txt, int pid)
{
  if (!db)
    {
      return -1;
    }
  db_error_t err;

  const char *sql_template = "UPDATE sectors SET beacon = {1} WHERE sector_id = {2};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_exec (db, sql,
                (db_bind_t[]){db_bind_text (txt), db_bind_i32 (sid)}, 2, &err))
    {
      return -1;
    }
  (void)pid; return 0;
}


int
db_player_set_alignment (db_t *db, int pid, int align)
{
  if (!db)
    {
      return -1;
    }
  db_error_t err;

  const char *sql_template = "UPDATE players SET alignment = {1} WHERE player_id = {2};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_exec (db, sql,
                (db_bind_t[]){db_bind_i32 (align), db_bind_i32 (pid)}, 2, &err))
    {
      return -1;
    }
  return 0;
}


int
db_port_info_json (int port_id, json_t **out)
{
  db_t *db = game_db_get_handle ();
  if (!db || !out)
    {
      return -1;
    }

  json_t *root = NULL;
  json_t *goods = NULL;
  db_res_t *res = NULL;
  db_error_t err;
  int rc = 0;

  root = json_object ();
  if (!root)
    {
      return -1;
    }

  /* 1) Details */
  const char *sql_det =
    "SELECT port_id, name, type, sector_id FROM ports WHERE port_id = {1};";

  char sql_det_converted[256];
  sql_build(db, sql_det, sql_det_converted, sizeof(sql_det_converted));

  if (db_query (db,
                sql_det_converted,
                (db_bind_t[]){ db_bind_i32 (port_id) },
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          json_object_set_new (root, "id", json_integer (db_res_col_i32 (res,
                                                                         0,
                                                                         &err)));
          json_object_set_new (root, "name", json_string (db_res_col_text (res,
                                                                           1,
                                                                           &err)
      ?: ""));
          json_object_set_new (root,
                               "type",
                               json_integer (db_res_col_i32 (res,
                                                             2,
                                                             &err)));
          json_object_set_new (root, "sector_id",
                               json_integer (db_res_col_i32 (res,
                                                             3,
                                                             &err)));
        }
      db_res_finalize (res);
      res = NULL;
    }

  /* 2) Goods (using entity_stock) */
  goods = json_object ();
  if (!goods)
    {
      rc = -1;
      goto cleanup;
    }

  const char *sql_goods =
    "SELECT commodity_code, quantity FROM entity_stock WHERE entity_type = 'port' AND entity_id = {1};";

  char sql_goods_converted[256];
  sql_build(db, sql_goods, sql_goods_converted, sizeof(sql_goods_converted));

  if (db_query (db,
                sql_goods_converted,
                (db_bind_t[]){ db_bind_i32 (port_id) },
                1,
                &res,
                &err))
    {
      while (db_res_step (res, &err))
        {
          const char *comm = db_res_col_text (res, 0, &err);
          int qty = (int) db_res_col_i32 (res, 1, &err);

          if (comm)
            {
              json_object_set_new (goods, comm, json_integer (qty));
            }
        }
      db_res_finalize (res);
      res = NULL;
    }
  json_object_set_new (root, "goods", goods);
  goods = NULL;

  *out = root;
  root = NULL;

cleanup:
  if (res)
    db_res_finalize (res);
  if (root)
    json_decref (root);
  if (goods)
    json_decref (goods);
  return rc;
}


int
db_get_online_player_count (void)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int cnt = 0;

  if (db_query (db, "SELECT COUNT(*) FROM sessions;", NULL, 0, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          cnt = db_res_col_i32 (res, 0, &err);
        }
    }

  if (res)
    db_res_finalize (res);
  return cnt;
}


int
db_get_player_id_by_name (const char *nm)
{
  db_t *db = game_db_get_handle ();
  if (!db || !nm)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int pid = 0;

  const char *sql_template = "SELECT player_id FROM players WHERE name = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_text (nm)}, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          pid = db_res_col_i32 (res, 0, &err);
        }
    }

  if (res)
    db_res_finalize (res);
  return pid;
}


int
db_player_name (db_t *db, int64_t pid, char **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;

  const char *sql_template = "SELECT name FROM players WHERE player_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i64 (pid)}, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          *out = strdup (db_res_col_text (res, 0, &err) ?: "");
          rc = 0;
          goto cleanup;
        }
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_is_black_market_port (db_t *db, int pid)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int black = 0;


  if (db_query (db,
                "SELECT 1 FROM ports WHERE port_id = {1} AND type = 10 LIMIT 1;",
                (db_bind_t[]){db_bind_i32 (pid)},
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          black = 1;
        }
    }

  if (res)
    db_res_finalize (res);
  return black;
}


int
db_get_port_commodity_quantity (db_t *db, int pid, const char *code, int *qty)
{
  if (!db || !qty)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;
  const char *sql =
    "SELECT quantity FROM entity_stock WHERE entity_type='port' AND entity_id={1} AND commodity_code={2};";

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db,
                sql_converted,
                (db_bind_t[]){db_bind_i32 (pid), db_bind_text (code)},
                2,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          *qty = db_res_col_i32 (res, 0, &err);
          rc = 0;
          goto cleanup;
        }
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_sector_basic_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;


  const char *sql_template = "SELECT sector_id, name, beacon FROM sectors WHERE sector_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db,
                sql,
                (db_bind_t[]){db_bind_i32 (sid)},
                1,
                &res,
                &err))
    {
      json_t *arr = NULL;
      rc = stmt_to_json_array (res, &arr, &err);
      if (rc == 0 && arr) {
        if (json_array_size(arr) > 0) {
          *out = json_incref(json_array_get(arr, 0));
        } else {
          rc = -1;
        }
        json_decref(arr);
      }
      goto cleanup;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_adjacent_sectors_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;


  const char *sql_template = "SELECT to_sector FROM sector_warps WHERE from_sector = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db,
                sql,
                (db_bind_t[]){db_bind_i32 (sid)},
                1,
                &res,
                &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      goto cleanup;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_ports_at_sector_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;


  const char *sql_template = "SELECT port_id, name, type FROM ports WHERE sector_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db,
                sql,
                (db_bind_t[]){db_bind_i32 (sid)},
                1,
                &res,
                &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      goto cleanup;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_ships_at_sector_json (db_t *db, int pid, int sid, json_t **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;

  const char *sql_template = "SELECT ship_id, name FROM ships WHERE sector_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i32 (sid)}, 1, &res, &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      (void)pid;
      goto cleanup;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_planets_at_sector_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;

  const char *sql_template = "SELECT planet_id, name, type FROM planets WHERE sector_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db,
                sql,
                (db_bind_t[]){db_bind_i32 (sid)},
                1,
                &res,
                &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      goto cleanup;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_players_at_sector_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;

  const char *sql_template = "SELECT player_id, name FROM players WHERE sector_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i32 (sid)}, 1, &res, &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      goto cleanup;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_fighters_at_sector_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = 0;
  const char *sql =
    "SELECT sector_assets_id, owner_id as player_id, quantity FROM sector_assets WHERE sector_id = {1} AND asset_type = 2;";

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db, sql_converted, (db_bind_t[]){ db_bind_i32 (sid) }, 1, &res, &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      goto cleanup;
    }
  rc = err.code;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_mines_at_sector_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = 0;
  const char *sql =
    "SELECT sector_assets_id, owner_id as player_id, quantity FROM sector_assets WHERE sector_id = {1} AND asset_type = 3;";

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db, sql_converted, (db_bind_t[]){ db_bind_i32 (sid) }, 1, &res, &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      goto cleanup;
    }
  rc = err.code;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_beacons_at_sector_json (db_t *db, int sid, json_t **out)
{
  if (!db || !out)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = 0;
  const char *sql =
    "SELECT sector_assets_id, owner_id as player_id, quantity FROM sector_assets WHERE sector_id = {1} AND asset_type = 1;";

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db, sql_converted, (db_bind_t[]){ db_bind_i32 (sid) }, 1, &res, &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      goto cleanup;
    }
  rc = err.code;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_update_player_sector (db_t *db, int pid, int sid)
{
  if (!db)
    {
      return -1;
    }
  db_error_t err;

  const char *sql_template = "UPDATE players SET sector_id = {1} WHERE player_id = {2};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (!db_exec (db, sql,
                (db_bind_t[]){db_bind_i32 (sid), db_bind_i32 (pid)}, 2, &err))
    {
      return -1;
    }
  return 0;
}


int
db_ship_flags_set (db_t *db, int ship_id, int flags)
{
  if (!db)
    {
      return ERR_DB_MISUSE;
    }
  db_error_t err;
  const char *sql = "UPDATE ships SET flags = flags | {1} WHERE ship_id = {2};";
  db_bind_t params[] = { db_bind_i32 (flags), db_bind_i32 (ship_id) };

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db, sql_converted, params, 2, &err))
    {
      return err.code;
    }
  return 0;
}


int
db_ship_flags_clear (db_t *db, int ship_id, int flags)
{
  if (!db)
    {
      return ERR_DB_MISUSE;
    }
  db_error_t err;
  const char *sql = "UPDATE ships SET flags = flags & ~{1} WHERE ship_id = {2};";
  db_bind_t params[] = { db_bind_i32 (flags), db_bind_i32 (ship_id) };

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db, sql_converted, params, 2, &err))
    {
      return err.code;
    }
  return 0;
}


int
db_get_sector_min (db_t *db, int sid, json_t **out)
{
  return db_sector_basic_json (db, sid, out);
}


int
db_get_sector_rich (db_t *db, int sid, json_t **out)
{
  return db_sector_basic_json (db, sid, out);
}


int
db_get_port_details_json (db_t *db, int pid, json_t **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;

  const char *sql_template = "SELECT * FROM ports WHERE port_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i32 (pid)}, 1, &res, &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      goto cleanup;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_port_get_goods_json (db_t *db, int pid, json_t **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;


  /* Use entity_stock */
  if (db_query (db,
                "SELECT * FROM entity_stock WHERE entity_type='port' AND entity_id = {1};",
                (db_bind_t[]){db_bind_i32 (pid)},
                1,
                &res,
                &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      goto cleanup;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_planet_get_details_json (db_t *db, int pid, json_t **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;

  const char *sql_template = "SELECT * FROM planets WHERE planet_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i32 (pid)}, 1, &res, &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      goto cleanup;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_planet_get_goods_json (db_t *db, int pid, json_t **out)
{
  if (!db || !out)
    {
      return -1;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = -1;

  const char *sql_template = "SELECT * FROM planet_goods WHERE planet_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i32 (pid)}, 1, &res, &err))
    {
      rc = stmt_to_json_array (res, out, &err);
      goto cleanup;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_idemp_store_response (db_t *db, const char *key, const char *response_json)
{
  if (!db || !key)
    {
      return ERR_DB_MISUSE;
    }
  db_error_t err;
  const char *sql =
    "INSERT INTO idempotency (key, response) VALUES ({1}, {2}) ON CONFLICT(key) DO UPDATE SET response = excluded.response;";

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db,
                sql_converted,
                (db_bind_t[]){db_bind_text (key), db_bind_text (response_json)},
                2,
                &err))
    {
      return err.code;
    }
  return 0;
}


int
db_player_get_sector (db_t *db, int pid, int *out_sector)
{
  if (!db || !out_sector)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL; db_error_t err;

  const char *sql_template = "SELECT sector_id FROM players WHERE player_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i32 (pid)}, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          *out_sector = db_res_col_i32 (res, 0, &err); db_res_finalize (res);
          return 0;
        }
      db_res_finalize (res); return ERR_NOT_FOUND;
    }
  return err.code;
}


int
db_ships_inspectable_at_sector_json (db_t *db,
                                     int player_id,
                                     int sector_id,
                                     json_t **out_array)
{
  if (!db || !out_array)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = 0;
  /* const char *sql = */
  /*   "SELECT ship_id, name FROM ships WHERE sector_id = {1} AND (cloaked IS NULL OR {2} = (SELECT player_id FROM ship_ownership WHERE ship_id = ships.ship_id AND is_primary = TRUE));"; */
  const char *sql =
    "SELECT ship_id, name FROM ships "
    "WHERE sector_id = {1} AND ( cloaked IS NULL "
    "        OR EXISTS ( SELECT 1 "
    "            FROM ship_ownership "
    "            WHERE ship_id = ships.ship_id "
    "              AND is_primary = TRUE "
    "              AND player_id = {2} )); ";

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db,
                sql_converted,
                (db_bind_t[]){db_bind_i32 (sector_id), db_bind_i32 (player_id)},
                2,
                &res,
                &err))
    {
      rc = stmt_to_json_array (res, out_array, &err);
      goto cleanup;
    }
  rc = err.code;

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_notice_create (db_t *db,
                  const char *title,
                  const char *body,
                  const char *severity,
                  time_t expires_at)
{
  if (!db)
    {
      return -1;
    }
  db_error_t err;
  const char *sql =
    "INSERT INTO system_notice (title, body, severity, expires_at) VALUES ({1}, {2}, {3}, {4});";
  int64_t id = 0;


  if (db_exec_insert_id (db, sql,
                         (db_bind_t[]){db_bind_text (title),
                                       db_bind_text (body),
                                       db_bind_text (severity),
                                       db_bind_i64 (expires_at)}, 4, &id, &err))
    {
      return (int)id;
    }
  return -1;
}


json_t *
db_notice_list_unseen_for_player (db_t *db, int player_id)
{
  if (!db)
    {
      return json_array ();
    }
  db_res_t *res = NULL; db_error_t err;
  
  const char *sql_template =
    "SELECT n.system_notice_id as id, n.title, n.body, n.severity FROM system_notice n LEFT JOIN notice_seen r ON n.system_notice_id = r.notice_id AND r.player_id = {1} WHERE r.player_id IS NULL AND (n.expires_at IS NULL OR n.expires_at > %s);";

  char sql_dialect[1024];
  sql_build(db, sql_template, sql_dialect, sizeof sql_dialect);

  char sql[1024];
  snprintf(sql, sizeof sql, sql_dialect, sql_now_expr(db));

  if (db_query (db, sql, (db_bind_t[]){db_bind_i32 (player_id)}, 1, &res, &err))
    {
      json_t *arr = NULL; stmt_to_json_array (res, &arr, &err);


      db_res_finalize (res); return arr ?: json_array ();
    }
  return json_array ();
}


int
db_notice_mark_seen (db_t *db, int notice_id, int player_id)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err;
  
  const char *sql_template =
    "INSERT INTO notice_seen (notice_id, player_id, seen_at) VALUES ({1}, {2}, %s) ON CONFLICT DO NOTHING;";

  char sql_dialect[512];
  sql_build(db, sql_template, sql_dialect, sizeof sql_dialect);

  char sql[512];
  snprintf(sql, sizeof sql, sql_dialect, sql_now_expr(db));

  if (!db_exec (db,
                sql,
                (db_bind_t[]){db_bind_i32 (notice_id), db_bind_i32 (player_id)},
                2,
                &err))
    {
      return err.code;
    }
  return 0;
}


int
db_commands_accept (db_t *db,
                    const char *cmd_type,
                    const char *idem_key,
                    json_t *payload,
                    int *out_cmd_id,
                    int *out_duplicate,
                    int *out_due_at)
{
  (void)db; (void)cmd_type; (void)idem_key; (void)payload; (void)out_cmd_id;
  (void)out_duplicate; (void)out_due_at;
  return -1;
}


int
db_sector_scan_core (db_t *db, int sector_id, json_t **out_obj)
{
  if (!db || !out_obj)
    {
      return ERR_DB_CLOSED;
    }
  db_res_t *res = NULL;
  db_error_t err;
  json_t *obj = NULL;
  int rc = -1;
  /* Rich scan query using sector_ops view logic or subqueries */
  const char *sql =
    "SELECT s.sector_id, s.name, 1 as safe_zone, "
    "(SELECT COUNT(*) FROM ports WHERE sector_id={1}) as port_count, "
    "(SELECT COUNT(*) FROM ships WHERE sector_id={1}) as ship_count, "
    "(SELECT COUNT(*) FROM planets WHERE sector_id={1}) as planet_count, "
    "s.beacon as beacon_text "
    "FROM sectors s WHERE s.sector_id = {1};";

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, (db_bind_t[]){ db_bind_i32 (sector_id) }, 1, &res,
                 &err))
    {
      rc = -1;
      goto cleanup;
    }

  if (!db_res_step (res, &err))
    {
      rc = -1;
      goto cleanup;
    }

  obj = json_object ();
  if (!obj)
    {
      rc = ERR_NOMEM;
      goto cleanup;
    }

  json_object_set_new (obj, "id", json_integer (db_res_col_i32 (res, 0, &err)));
  json_object_set_new (obj, "name", json_string (db_res_col_text (res, 1, &err) ?: ""));
  json_object_set_new (obj, "safe_zone", json_integer (db_res_col_i32 (res, 2, &err)));
  json_object_set_new (obj, "port_count", json_integer (db_res_col_i32 (res, 3, &err)));
  json_object_set_new (obj, "ship_count", json_integer (db_res_col_i32 (res, 4, &err)));
  json_object_set_new (obj, "planet_count", json_integer (db_res_col_i32 (res, 5, &err)));
  json_object_set_new (obj, "beacon_text", json_string (db_res_col_text (res, 6, &err) ?: ""));

  *out_obj = obj;
  obj = NULL;  /* Transfer ownership to caller */
  rc = 0;

cleanup:
  if (obj)
    json_decref (obj);
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_sector_scan_snapshot (db_t *db, int sid, json_t **out)
{
  return db_sector_basic_json (db, sid, out);
}


int
db_create_initial_ship (db_t *db, int pid, const char *name, int sid)
{
  db_error_t err;
  db_res_t *res = NULL;
  const char *sql = "SELECT * FROM ship_create_initial({1}, {2});";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  if (db_query (db,
                sql_converted,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_i64 (sid)},
                2,
                &res,
                &err))
    {
      int ship_id = 0;


      if (db_res_step (res, &err))
        {
          // (code, msg, id)
          ship_id = (int)db_res_col_i64 (res,
                                         2,
                                         &err);
        }
      db_res_finalize (res);
      return ship_id > 0 ? ship_id : -1;
    }
  return -1;
}


int
h_ship_claim_unlocked (db_t *db, int pid, int sid, int ship_id, json_t **out)
{
  if (!db)
    {
      return -1;
    }
  db_error_t err;
  db_res_t *res = NULL;
  json_t *root = NULL;
  json_t *type_obj = NULL;
  json_t *owner_obj = NULL;
  json_t *flags_obj = NULL;
  json_t *defence_obj = NULL;
  json_t *holds_obj = NULL;
  json_t *cargo = NULL;
  int rc = -1;


  /* 1. Check if ship is claimable (correct sector, no pilot) */
  const char *sql_check =
    "SELECT s.ship_id FROM ships s "
    "LEFT JOIN players pil ON pil.ship_id = s.ship_id "
    "WHERE s.ship_id={1} AND s.sector_id={2} "
    "AND pil.player_id IS NULL;";

  char sql_check_converted[512];
  sql_build(db, sql_check, sql_check_converted, sizeof(sql_check_converted));

  if (!db_query (db,
                 sql_check_converted,
                 (db_bind_t[]){ db_bind_i32 (ship_id), db_bind_i32 (sid) },
                 2,
                 &res,
                 &err))
    {
      rc = -1;
      goto cleanup;
    }
  if (!db_res_step (res, &err))
    {
      rc = -1;
      goto cleanup;
    }
  db_res_finalize (res);
  res = NULL;

  /* 2. Switch current pilot */
  if (!db_exec (db,
                "UPDATE players SET ship_id = {1} WHERE player_id = {2};",
                (db_bind_t[]){ db_bind_i32 (ship_id), db_bind_i32 (pid) },
                2,
                &err))
    {
      rc = -1;
      goto cleanup;
    }


  /* 3. Grant ownership and mark as primary */
  const char *sql_template1 = "UPDATE ship_ownership SET is_primary=FALSE WHERE player_id={1};";
  char sql1[256];
  sql_build(db, sql_template1, sql1, sizeof sql1);
  db_exec (db, sql1,
           (db_bind_t[]){ db_bind_i32 (pid) }, 1, &err);

  const char *sql_template2 = "DELETE FROM ship_ownership WHERE ship_id={1} AND role_id=1;";
  char sql2[256];
  sql_build(db, sql_template2, sql2, sizeof sql2);
  db_exec (db, sql2,
           (db_bind_t[]){ db_bind_i32 (ship_id) }, 1, &err);


  const char *sql_template =
    "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary, acquired_at) VALUES ({1}, {2}, 1, TRUE, %s);";

  char sql_dialect[512];
  sql_build(db, sql_template, sql_dialect, sizeof sql_dialect);

  char sql[512];
  snprintf(sql, sizeof sql, sql_dialect, sql_now_expr(db));

  if (!db_exec (db,
                sql,
                (db_bind_t[]){ db_bind_i32 (ship_id), db_bind_i32 (pid) },
                2,
                &err))
    {
      rc = -1;
      goto cleanup;
    }


  /* 4. Fetch snapshot for reply */
  const char *sql_fetch =
    "SELECT s.ship_id AS ship_id, "
    "       COALESCE(NULLIF(s.name,''), st.name || ' #' || s.ship_id) AS ship_name, "
    "       st.shiptypes_id AS type_id, st.name AS type_name, "
    "       s.sector_id AS sector_id, "
    "       own.player_id AS owner_id, "
    "       COALESCE( (SELECT name FROM players WHERE player_id=own.player_id), 'derelict') AS owner_name, "
    "       0 AS is_derelict, "
    "       s.fighters, s.shields, "
    "       s.holds AS holds_total, (s.holds - s.holds) AS holds_free, "
    "       s.ore, s.organics, s.equipment, s.colonists, "
    "       COALESCE(s.flags, 0) AS perms "
    "FROM ships s "
    "LEFT JOIN shiptypes st       ON st.shiptypes_id = s.type_id "
    "LEFT JOIN ship_ownership own ON own.ship_id = s.ship_id "
    "WHERE s.ship_id={1};";

  char sql_fetch_converted[1024];
  sql_build(db, sql_fetch, sql_fetch_converted, sizeof(sql_fetch_converted));

  if (!db_query (db,
                 sql_fetch_converted,
                 (db_bind_t[]){ db_bind_i32 (ship_id) },
                 1,
                 &res,
                 &err))
    {
      rc = -1;
      goto cleanup;
    }

  if (!db_res_step (res, &err))
    {
      rc = -1;
      goto cleanup;
    }

  if (out)
    {
      root = json_object ();
      if (!root)
        {
          rc = ERR_NOMEM;
          goto cleanup;
        }

      json_object_set_new (root, "id", json_integer (db_res_col_i32 (res, 0, &err)));
      json_object_set_new (root, "name", json_string (db_res_col_text (res, 1, &err) ?: ""));

      type_obj = json_object ();
      if (!type_obj)
        {
          rc = ERR_NOMEM;
          goto cleanup;
        }
      json_object_set_new (type_obj, "id", json_integer (db_res_col_i32 (res, 2, &err)));
      json_object_set_new (type_obj, "name", json_string (db_res_col_text (res, 3, &err) ?: ""));
      json_object_set_new (root, "type", type_obj);
      type_obj = NULL;  /* Stolen by root */

      json_object_set_new (root, "sector_id", json_integer (db_res_col_i32 (res, 4, &err)));

      owner_obj = json_object ();
      if (!owner_obj)
        {
          rc = ERR_NOMEM;
          goto cleanup;
        }
      json_object_set_new (owner_obj, "id", json_integer (db_res_col_i32 (res, 5, &err)));
      json_object_set_new (owner_obj, "name", json_string (db_res_col_text (res, 6, &err) ?: "derelict"));
      json_object_set_new (root, "owner", owner_obj);
      owner_obj = NULL;  /* Stolen by root */

      flags_obj = json_object ();
      if (!flags_obj)
        {
          rc = ERR_NOMEM;
          goto cleanup;
        }
      json_object_set_new (flags_obj, "derelict", json_boolean (db_res_col_i32 (res, 7, &err) != 0));
      json_object_set_new (flags_obj, "raw", json_integer (db_res_col_i32 (res, 16, &err)));
      json_object_set_new (root, "flags", flags_obj);
      flags_obj = NULL;  /* Stolen by root */

      defence_obj = json_object ();
      if (!defence_obj)
        {
          rc = ERR_NOMEM;
          goto cleanup;
        }
      json_object_set_new (defence_obj, "fighters", json_integer (db_res_col_i32 (res, 8, &err)));
      json_object_set_new (defence_obj, "shields", json_integer (db_res_col_i32 (res, 9, &err)));
      json_object_set_new (root, "defence", defence_obj);
      defence_obj = NULL;  /* Stolen by root */

      holds_obj = json_object ();
      if (!holds_obj)
        {
          rc = ERR_NOMEM;
          goto cleanup;
        }
      json_object_set_new (holds_obj, "total", json_integer (db_res_col_i32 (res, 10, &err)));
      json_object_set_new (holds_obj, "free", json_integer (db_res_col_i32 (res, 11, &err)));
      json_object_set_new (root, "holds", holds_obj);
      holds_obj = NULL;  /* Stolen by root */

      cargo = json_object ();
      if (!cargo)
        {
          rc = ERR_NOMEM;
          goto cleanup;
        }
      json_object_set_new (cargo, "ore", json_integer (db_res_col_i32 (res, 12, &err)));
      json_object_set_new (cargo, "organics", json_integer (db_res_col_i32 (res, 13, &err)));
      json_object_set_new (cargo, "equipment", json_integer (db_res_col_i32 (res, 14, &err)));
      json_object_set_new (cargo, "colonists", json_integer (db_res_col_i32 (res, 15, &err)));
      json_object_set_new (root, "cargo", cargo);
      cargo = NULL;  /* Stolen by root */

      *out = root;
      root = NULL;  /* Transfer ownership to caller */
    }

  rc = 0;

cleanup:
  if (cargo)
    json_decref (cargo);
  if (holds_obj)
    json_decref (holds_obj);
  if (defence_obj)
    json_decref (defence_obj);
  if (flags_obj)
    json_decref (flags_obj);
  if (owner_obj)
    json_decref (owner_obj);
  if (type_obj)
    json_decref (type_obj);
  if (root)
    json_decref (root);
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_ship_claim (db_t *db, int pid, int sid, int ship_id, json_t **out)
{
  db_error_t err;
  db_res_t *res = NULL;
  const char *sql = "SELECT * FROM ship_claim({1}, {2});";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  int rc = -1;
  if (db_query (db,
                sql_converted,
                (db_bind_t[]){db_bind_i64 (pid), db_bind_i64 (ship_id)},
                2,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          rc = db_res_col_i32 (res,
                               0,
                               &err);          // code
        }
      db_res_finalize (res);
    }

  if (rc == 0 && out)
    {
      json_t *player_info = NULL;
      if (db_player_info_json (db, pid, &player_info) == 0 && player_info)
        {
          json_t *ship = json_object_get (player_info, "ship");
          if (ship)
            {
              *out = json_incref (ship);
            }
          json_decref (player_info);
        }
    }

  (void) sid;
  return (rc == 0) ? 0 : rc;
}


int
db_news_insert_feed_item (long long ts,
                          const char *category,
                          const char *scope,
                          const char *headline,
                          const char *body,
                          json_t *context_data)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  char *context_str = context_data ? json_dumps (context_data,
                                                 JSON_COMPACT) : NULL;
  char *article_text = NULL;


  if (context_str)
    {
      if (asprintf (&article_text,
                    "HEADLINE: %s\nBODY: %s\nCONTEXT: %s",
                    headline,
                    body,
                    context_str) == -1)
        {
          free (context_str);
          return -1;
        }
    }
  else
    {
      if (asprintf (&article_text, "HEADLINE: %s\nBODY: %s", headline,
                    body) == -1)
        {
          return -1;
        }
    }

  db_error_t err;
  const char *sql =
    "INSERT INTO news_feed (published_ts, news_category, article_text) VALUES ({1}, {2}, {3});";
  db_bind_t params[] = {
    db_bind_i64 (ts),
    db_bind_text (category),
    db_bind_text (article_text)
  };

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  bool ok = db_exec (db, sql_converted, params, 3, &err);


  free (article_text);
  if (context_str)
    {
      free (context_str);
    }
  (void) scope;
  return ok ? 0 : -1;
}


int
db_is_sector_fedspace (db_t *db, int sector_id)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err;
  const char *sql =
    "SELECT 1 FROM sectors WHERE sector_id = {1} AND is_fedspace = 1;";
  int is_fed = 0;

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db, sql_converted, (db_bind_t[]){ db_bind_i32 (sector_id) }, 1, &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          is_fed = 1;
        }
      db_res_finalize (res);
    }
  return is_fed;
}


int
db_sector_has_beacon (db_t *db, int sector_id)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err;
  const char *sql =
    "SELECT 1 FROM sector_assets WHERE sector_id = {1} AND asset_type = 1 LIMIT 1;";
  int has_beacon = 0;

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db, sql_converted, (db_bind_t[]){ db_bind_i32 (sector_id) }, 1, &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          has_beacon = 1;
        }
      db_res_finalize (res);
    }
  return has_beacon;
}


int
db_player_has_beacon_on_ship (db_t *db, int player_id)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL;
  db_error_t err;
  const char *sql =
    "SELECT 1 FROM entity_stock es JOIN players p ON es.entity_id = p.ship_id WHERE p.player_id = {1} AND es.entity_type = 'ship' AND es.commodity_code = 'BEACON' AND es.quantity > 0 LIMIT 1;";
  int has_beacon = 0;

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db, sql_converted, (db_bind_t[]){ db_bind_i32 (player_id) }, 1, &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          has_beacon = 1;
        }
      db_res_finalize (res);
    }
  return has_beacon;
}


int
db_player_decrement_beacon_count (db_t *db, int player_id)
{
  if (!db)
    {
      return -1;
    }
  db_error_t err;
  const char *sql =
    "UPDATE entity_stock SET quantity = quantity - 1 WHERE entity_type = 'ship' AND entity_id = (SELECT ship_id FROM players WHERE player_id = {1}) AND commodity_code = 'BEACON' AND quantity > 0;";

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db, sql_converted, (db_bind_t[]){ db_bind_i32 (player_id) }, 1, &err))
    {
      return -1;
    }
  return 0;
}


int
db_get_ship_sector_id (db_t *db, int ship_id)
{
  if (!db)
    {
      return 0;
    }
  db_res_t *res = NULL; db_error_t err; int sid = 0;

  const char *sql_template = "SELECT sector_id FROM ships WHERE ship_id = {1};";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i32 (ship_id)}, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          sid = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }
  return sid;
}


int
db_get_ship_owner_id (db_t *db, int ship_id, int *out_pid, int *out_cid)
{
  if (!db)
    {
      return -1;
    }
  db_res_t *res = NULL; db_error_t err;


  if (db_query (db,
                "SELECT player_id, corporation_id FROM ship_ownership WHERE ship_id = {1} AND is_primary = 1;",
                (db_bind_t[]){db_bind_i32 (ship_id)},
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          if (out_pid)
            {
              *out_pid = db_res_col_i32 (res, 0, &err);
            }
          if (out_cid)
            {
              *out_cid = db_res_col_i32 (res, 1, &err);
            }
          db_res_finalize (res); return 0;
        }
      db_res_finalize (res);
    }
  return -1;
}


bool
db_is_ship_piloted (db_t *db, int ship_id)
{
  if (!db)
    {
      return false;
    }
  db_res_t *res = NULL; db_error_t err; bool piloted = false;

  const char *sql_template = "SELECT 1 FROM players WHERE ship_id = {1} LIMIT 1;";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof sql);

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i32 (ship_id)}, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          piloted = true;
        }
      db_res_finalize (res);
    }
  return piloted;
}


int
db_recall_fighter_asset (db_t *db, int asset_id, int player_id)
{
  (void)db; (void)asset_id; (void)player_id; return -1;
}


int
db_get_config_int (db_t *db, const char *key, int def)
{
  if (!db)
    {
      return def;
    }
  db_res_t *res = NULL; db_error_t err; int val = def;


  if (db_query (db,
                "SELECT value FROM config WHERE key = {1} AND type = 'int';",
                (db_bind_t[]){db_bind_text (key)},
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          const char *v = db_res_col_text (res, 0, &err);


          if (v)
            {
              val = atoi (v);
            }
        }
      db_res_finalize (res);
    }
  return val;
}


bool
db_get_config_bool (db_t *db, const char *key, bool def)
{
  if (!db)
    {
      return def;
    }
  db_res_t *res = NULL; db_error_t err; bool val = def;


  if (db_query (db,
                "SELECT value FROM config WHERE key = {1} AND type = 'bool';",
                (db_bind_t[]){db_bind_text (key)},
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          const char *v = db_res_col_text (res,
                                           0,
                                           &err);


          if (v)
            {
              val = (strcmp (v, "true") == 0 || strcmp (v, "1") == 0);
            }
        }
      db_res_finalize (res);
    }
  return val;
}


void
h_generate_hex_uuid (char *buffer, size_t buffer_size)
{
  if (buffer_size < 33)
    {
      return;
    }
  static const char *hex_chars = "0123456789abcdef";


  for (int i = 0; i < 32; i++)
    {
      buffer[i] = hex_chars[rand () % 16];
    }
  buffer[32] = '\0';
}


int
db_chain_traps_and_bridge (db_t *db, int fedspace_max)
{
  if (!db)
    {
      return -1;
    }
  db_error_t err;


  db_error_clear (&err);


  const char *sql_traps =
    "WITH ow AS (SELECT from_sector AS id, COUNT(*) AS c FROM sector_warps GROUP BY from_sector), "
    "     iw AS (SELECT to_sector   AS id, COUNT(*) AS c FROM sector_warps GROUP BY to_sector) "
    "SELECT s.sector_id "
    "FROM sectors s "
    "LEFT JOIN ow ON ow.id = s.sector_id "
    "LEFT JOIN iw ON iw.id = s.sector_id "
    "WHERE s.sector_id > {1} AND COALESCE(ow.c,0)=0 AND COALESCE(iw.c,0)=0 "
    "ORDER BY s.sector_id;";

  db_res_t *res_traps = NULL;
  db_bind_t params[] = { db_bind_i32 (fedspace_max) };

  char sql_traps_converted[512];
  sql_build(db, sql_traps, sql_traps_converted, sizeof(sql_traps_converted));

  if (!db_query (db, sql_traps_converted, params, 1, &res_traps, &err))
    {
      return -1;
    }


  int cap = 64, n = 0;
  int *traps = malloc (sizeof (int) * cap);


  if (!traps)
    {
      db_res_finalize (res_traps); return -1;
    }


  while (db_res_step (res_traps, &err))
    {
      if (n == cap)
        {
          cap *= 2;
          int *tmp = realloc (traps,
                              sizeof (int) * cap);


          if (!tmp)
            {
              free (traps); db_res_finalize (res_traps); return -1;
            }
          traps = tmp;
        }
      traps[n++] = (int) db_res_col_i32 (res_traps, 0, &err);
    }
  db_res_finalize (res_traps);


  if (n == 0)
    {
      free (traps); return 0;
    }


  const char *sql_ins =
    "INSERT INTO sector_warps(from_sector, to_sector) VALUES ({1}, {2}) ON CONFLICT DO NOTHING;";

  char sql_ins_converted[256];
  sql_build(db, sql_ins, sql_ins_converted, sizeof(sql_ins_converted));

  for (int i = 0; i + 1 < n; ++i)
    {
      db_exec (db,
               sql_ins_converted,
               (db_bind_t[]){ db_bind_i32 (traps[i]),
                              db_bind_i32 (traps[i + 1]) },
               2,
               &err);
      db_exec (db,
               sql_ins_converted,
               (db_bind_t[]){ db_bind_i32 (traps[i + 1]),
                              db_bind_i32 (traps[i]) },
               2,
               &err);
    }


  const char *sql_anchor =
    "WITH x AS ("
    "  SELECT s.sector_id "
    "  FROM sectors s "
    "  WHERE s.sector_id > {1} AND EXISTS ("
    "    SELECT 1 FROM sector_warps w "
    "    WHERE w.from_sector = s.sector_id OR w.to_sector = s.sector_id"
    "  )" ") SELECT sector_id FROM x ORDER BY RANDOM() LIMIT 1;";

  int anchor = 0;
  db_res_t *res_anchor = NULL;

  char sql_anchor_converted[512];
  sql_build(db, sql_anchor, sql_anchor_converted, sizeof(sql_anchor_converted));

  if (db_query (db, sql_anchor_converted, params, 1, &res_anchor, &err))
    {
      if (db_res_step (res_anchor, &err))
        {
          anchor = (int) db_res_col_i32 (res_anchor, 0, &err);
        }
      db_res_finalize (res_anchor);
    }


  if (anchor == 0)
    {
      anchor = fedspace_max + 1;
    }

  char sql_ins_converted2[256];
  sql_build(db, sql_ins, sql_ins_converted2, sizeof(sql_ins_converted2));

  db_exec (db,
           sql_ins_converted2,
           (db_bind_t[]){ db_bind_i32 (anchor), db_bind_i32 (traps[0]) },
           2,
           &err);
  db_exec (db,
           sql_ins_converted2,
           (db_bind_t[]){ db_bind_i32 (traps[0]), db_bind_i32 (anchor) },
           2,
           &err);


  free (traps);
  return 0;
}


int
db_ship_rename_if_owner (db_t *db,
                         int player_id,
                         int ship_id,
                         const char *new_name)
{
  if (!db || !new_name)
    {
      return ERR_DB_MISUSE;
    }
  db_error_t err;


  if (!db_exec (db,
                "UPDATE ships SET name = {1} WHERE ship_id = {2} AND EXISTS (SELECT 1 FROM ship_ownership WHERE ship_id = {2} AND player_id = {3} AND is_primary = TRUE);",
                (db_bind_t[]){db_bind_text (new_name), db_bind_i32 (ship_id),
                              db_bind_i32 (player_id)},
                3,
                &err))
    {
      return err.code;
    }
  return 0;
}


int
db_player_info_json (db_t *db, int player_id, json_t **out_json)
{
  if (!db || !out_json)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err;
  json_t *root = NULL;
  json_t *loc = NULL;
  json_t *ship = NULL;
  int rc = ERR_DB_QUERY_FAILED;
  const char *sql =
    "SELECT p.player_id, p.name, p.experience, p.alignment, p.credits, p.sector_id, "
    "       s.ship_id, s.name as ship_name, s.type_id as ship_type, "
    "       s.holds, s.fighters, s.shields, s.onplanet, s.ported, "
    "       sec.name as sector_name, "
    "       s.ore, s.equipment, s.organics, s.slaves, s.drugs, s.weapons, s.colonists "
    "FROM players p "
    "LEFT JOIN ships s ON p.ship_id = s.ship_id "
    "LEFT JOIN sectors sec ON p.sector_id = sec.sector_id "
    "WHERE p.player_id = {1};";

  char sql_converted[1024];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, (db_bind_t[]){ db_bind_i32 (player_id) }, 1, &res, &err))
    {
      rc = err.code;
      goto cleanup;
    }

  if (!db_res_step (res, &err))
    {
      rc = ERR_NOT_FOUND;
      goto cleanup;
    }

  root = json_object ();
  if (!root)
    {
      rc = ERR_NOMEM;
      goto cleanup;
    }

  json_object_set_new (root, "id", json_integer (db_res_col_i32 (res, 0, &err)));
  json_object_set_new (root, "name", json_string (db_res_col_text (res, 1, &err) ?: ""));
  json_object_set_new (root, "experience", json_integer (db_res_col_i32 (res, 2, &err)));
  json_object_set_new (root, "alignment", json_integer (db_res_col_i32 (res, 3, &err)));
  json_object_set_new (root, "credits", json_integer (db_res_col_i64 (res, 4, &err)));

  loc = json_object ();
  if (!loc)
    {
      rc = ERR_NOMEM;
      goto cleanup;
    }
  json_object_set_new (loc, "sector_id", json_integer (db_res_col_i32 (res, 5, &err)));
  json_object_set_new (loc, "sector_name", json_string (db_res_col_text (res, 14, &err) ?: ""));
  json_object_set_new (root, "location", loc);
  loc = NULL;  /* Stolen by root */

  if (db_res_col_i32 (res, 6, &err) > 0)
    {
      ship = json_object ();
      if (!ship)
        {
          rc = ERR_NOMEM;
          goto cleanup;
        }
      json_object_set_new (ship, "id", json_integer (db_res_col_i32 (res, 6, &err)));
      json_object_set_new (ship, "name", json_string (db_res_col_text (res, 7, &err) ?: ""));
      json_object_set_new (ship, "type_id", json_integer (db_res_col_i32 (res, 8, &err)));
      json_object_set_new (ship, "holds", json_integer (db_res_col_i32 (res, 9, &err)));
      json_object_set_new (ship, "fighters", json_integer (db_res_col_i32 (res, 10, &err)));
      json_object_set_new (ship, "shields", json_integer (db_res_col_i32 (res, 11, &err)));
      json_object_set_new (ship, "onplanet", json_integer (db_res_col_i32 (res, 12, &err)));
      json_object_set_new (ship, "ported", json_integer (db_res_col_i32 (res, 13, &err)));
      
      /* Build cargo array from commodity columns (columns 15-20) */
      json_t *cargo = json_array ();
      if (!cargo)
        {
          rc = ERR_NOMEM;
          goto cleanup;
        }
      
      /* Add commodities with non-zero quantities */
      const char *commodities[] = {"ore", "equipment", "organics", "slaves", "drugs", "weapons", "colonists"};
      for (int i = 0; i < 7; i++)
        {
          int qty = db_res_col_i32 (res, 15 + i, &err);
          if (qty > 0)
            {
              json_t *item = json_object ();
              if (!item)
                {
                  json_decref (cargo);
                  rc = ERR_NOMEM;
                  goto cleanup;
                }
              json_object_set_new (item, "commodity", json_string (commodities[i]));
              json_object_set_new (item, "quantity", json_integer (qty));
              json_array_append_new (cargo, item);
            }
        }
      
      json_object_set_new (ship, "cargo", cargo);
      json_object_set_new (root, "ship", ship);
      ship = NULL;  /* Stolen by root */
    }
  else
    {
      json_object_set_new (root, "ship", json_null ());
    }

  *out_json = root;
  root = NULL;  /* Transfer ownership to caller */
  rc = 0;

cleanup:
  if (ship)
    json_decref (ship);
  if (loc)
    json_decref (loc);
  if (root)
    json_decref (root);
  if (res)
    db_res_finalize (res);
  return rc;
}


int
db_player_info_selected_fields (db_t *db,
                                int player_id,
                                const json_t *fields_array,
                                json_t **out)
{
  if (!db || !fields_array || !json_is_array (fields_array) || !out)
    {
      return ERR_DB_MISUSE;
    }


  char select_clause[4096] = "";
  size_t index;
  json_t *value = NULL;
  int valid_fields = 0;


  json_array_foreach (fields_array, index, value)
  {
    const char *client_name = json_string_value (value);
    if (client_name)
      {
        const char *view_name = get_player_view_column_name (client_name);


        if (view_name)
          {
            if (valid_fields > 0)
              {
                strncat (select_clause, ", ",
                         sizeof (select_clause) - strlen (select_clause) - 1);
              }
            strncat (select_clause, view_name,
                     sizeof (select_clause) - strlen (select_clause) - 1);
            valid_fields++;
          }
      }
  }


  if (valid_fields == 0)
    {
      *out = json_object ();
      return 0;
    }


  char sql[8192];


  snprintf (sql,
            sizeof (sql),
            "SELECT %s FROM player_info_v1 WHERE player_id = {1};",
            select_clause);


  db_res_t *res = NULL;
  db_error_t err;


  if (db_query (db, sql, (db_bind_t[]){ db_bind_i32 (player_id) }, 1, &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          json_t *obj = json_object ();
          int col_idx = 0;


          json_array_foreach (fields_array, index, value)
          {
            const char *client_name = json_string_value (value);
            if (client_name && get_player_view_column_name (client_name))
              {
                db_col_type_t type = db_res_col_type (res, col_idx);
                json_t *val = NULL;


                switch (type)
                  {
                    case DB_TYPE_INTEGER: val =
                      json_integer (db_res_col_i64 (res,
                                                    col_idx,
                                                    &err)); break;
                    case DB_TYPE_FLOAT: val = json_real (db_res_col_double (res,
                                                                            col_idx,
                                                                            &err));
                      break;
                    case DB_TYPE_TEXT: val = json_string (db_res_col_text (res,
                                                                           col_idx,
                                                                           &err)
            ?: ""); break;
                    default: val = json_null (); break;
                  }
                json_object_set_new (obj, client_name, val);
                col_idx++;
              }
          }
          *out = obj;
          db_res_finalize (res);
          return 0;
        }
      db_res_finalize (res);
      return ERR_NOT_FOUND;
    }
  return err.code;
}


int
db_ensure_planet_bank_accounts_deprecated (void *db)
{
  (void)db; return 0;
}


/* ========================================================= */


/* LINKER FIXES: Restored C Bridges for Postgres             */


/* ========================================================= */


/* Stub for SQLite internal open (needed because headers reference it) */
int
db_sqlite_open_internal (void)
{
  return -1;
}


/* Thread cleanup is handled by libpq internally, but the symbol must exist */
void
db_close_thread (void)
{
}


/* Bridge for: 000_tables.sql (sessions table) */
int
db_session_create (int player_id, const char *token, long long expires_at)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  const char *sql =
    "INSERT INTO sessions (token, player_id, expires) VALUES ({1}, {2}, to_timestamp({3}))";
  db_bind_t p[] = { db_bind_text (token), db_bind_i32 (player_id),
                    db_bind_i64 (expires_at) };
  db_error_t err;

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db, sql_converted, p, 3, &err))
    {
      return -1;
    }
  return 0;
}


int
db_session_lookup (const char *token, int *player_id, long long *expires_at)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  const char *sql = "SELECT player_id, expires FROM sessions WHERE token = {1}";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));
  db_bind_t p[] = { db_bind_text (token) };
  db_res_t *res = NULL;
  db_error_t err;


  if (db_query (db, sql_converted, p, 1, &res, &err))
    {
      int rc = -1;


      if (db_res_step (res, &err))
        {
          if (player_id)
            {
              *player_id = db_res_col_i32 (res, 0, &err);
            }
          if (expires_at)
            {
              *expires_at = db_res_col_i64 (res, 1, &err);
            }
          rc = 0;
        }
      db_res_finalize (res);
      return rc;
    }
  return -1;
}


/* Bridge for: Updating Player Sector */
int
db_player_set_sector (int pid, int sid)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err;


  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      return err.code;
    }


  const char *sql_player =
    "UPDATE players SET sector_id = {1} WHERE player_id = {2};";

  char sql_player_converted[256];
  sql_build(db, sql_player, sql_player_converted, sizeof(sql_player_converted));

  if (!db_exec (db, sql_player_converted,
                (db_bind_t[]){ db_bind_i32 (sid), db_bind_i32 (pid) }, 2, &err))
    {
      db_tx_rollback (db, NULL);
      return err.code;
    }


  /* Sync the ship sector if player has an active ship */
  const char *sql_ship_info =
    "SELECT ship_id FROM players WHERE player_id = {1};";
  db_res_t *res = NULL;
  int ship_id = 0;

  char sql_ship_info_converted[256];
  sql_build(db, sql_ship_info, sql_ship_info_converted, sizeof(sql_ship_info_converted));

  if (db_query (db,
                sql_ship_info_converted,
                (db_bind_t[]){ db_bind_i32 (pid) },
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          ship_id = (int) db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }


  if (ship_id > 0)
    {
      const char *sql_ship =
        "UPDATE ships SET sector_id = {1} WHERE ship_id = {2};";

      char sql_ship_converted[256];
      sql_build(db, sql_ship, sql_ship_converted, sizeof(sql_ship_converted));

      if (!db_exec (db,
                    sql_ship_converted,
                    (db_bind_t[]){ db_bind_i32 (sid), db_bind_i32 (ship_id) },
                    2,
                    &err))
        {
          LOGW ("db_player_set_sector: Failed to sync ship %d for player %d",
                ship_id,
                pid);
        }
    }


  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      return err.code;
    }
  return 0;
}


int
db_session_revoke (const char *token)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    return -1;
  db_error_t err;
  return db_exec (db, "DELETE FROM sessions WHERE token = {1}",
                  (db_bind_t[]){ db_bind_text (token) }, 1, &err) ? 0 : -1;
}


int
db_session_refresh (const char *token, int ttl, char new_tok[65], int *out_exp)
{
  return 0;
}


long long
h_get_config_int_unlocked (db_t *db, const char *key, long long def)
{
  return def;
}


int
db_port_is_shipyard (db_t *db, int sector_id, bool *out)
{
  return 0;
}


int
db_ship_get_hull (db_t *db, int ship_id, int *out)
{
  return 0;
}

int
h_update_planet_stock (db_t *db, int planet_id, const char *commodity_code,
                       int quantity_change, int *new_quantity)
{
  if (!db || !commodity_code || planet_id <= 0)
    {
      return ERR_DB_MISUSE;
    }

  db_error_t err;
  db_error_clear(&err);
  
  const char *sql = "UPDATE planet_goods SET quantity = quantity + {1} "
                    "WHERE planet_id = {2} AND commodity = {3}";
  db_bind_t params[] = {
    db_bind_i32 (quantity_change),
    db_bind_i32 (planet_id),
    db_bind_text (commodity_code)
  };
  
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec(db, sql_converted, params, 3, &err))
    {
      LOGE ("h_update_planet_stock: Failed to update: %s", err.message);
      return ERR_DB;
    }
  
  if (new_quantity)
    {
      const char *qty_sql = "SELECT quantity FROM planet_goods "
                            "WHERE planet_id = {1} AND commodity = {2}";
      db_bind_t qty_params[] = {
        db_bind_i32 (planet_id),
        db_bind_text (commodity_code)
      };
      
      db_res_t *res = NULL;
      char qty_sql_converted[256];
      sql_build(db, qty_sql, qty_sql_converted, sizeof(qty_sql_converted));
      if (db_query(db, qty_sql_converted, qty_params, 2, &res, &err))
        {
          if (db_res_step(res, &err))
            {
              *new_quantity = (int)db_res_col_int(res, 0, &err);
            }
          db_res_finalize(res);
        }
      else
        {
          LOGE ("h_update_planet_stock: Failed to query quantity: %s", err.message);
        }
    }
  
  return 0;
}



int
h_bank_transfer_unlocked (db_t *db,
                          const char *from_owner_type, int from_owner_id,
                          const char *to_owner_type, int to_owner_id,
                          long long amount,
                          const char *tx_type, const char *tx_group_id)
{
  if (!db || !from_owner_type || !to_owner_type || !tx_type || amount < 0)
    {
      return ERR_DB_MISUSE;
    }

  int from_account_id, to_account_id;
  int rc;
  
  /* Get source account ID */
  rc = h_get_account_id_unlocked (db, from_owner_type, from_owner_id,
                                  &from_account_id);
  if (rc != 0)
    {
      if (strcmp(from_owner_type, "system") != 0 && strcmp(from_owner_type, "gov") != 0)
        {
          LOGW ("h_bank_transfer_unlocked: Source account %s:%d not found.",
                from_owner_type, from_owner_id);
          return ERR_NOT_FOUND;
        }
      LOGW ("h_bank_transfer_unlocked: Implicit system/gov source account %s:%d. Insufficient funds.",
            from_owner_type, from_owner_id);
      return ERR_INSUFFICIENT_FUNDS;
    }
  
  /* Get or create destination account ID */
  rc = h_get_account_id_unlocked (db, to_owner_type, to_owner_id, &to_account_id);
  if (rc != 0)
    {
      rc = h_create_bank_account_unlocked (db, to_owner_type, to_owner_id, 0,
                                           &to_account_id);
      if (rc != 0)
        {
          LOGE ("h_bank_transfer_unlocked: Failed to create destination account %s:%d",
                to_owner_type, to_owner_id);
          return rc;
        }
    }
  
  /* Deduct from source */
  rc = h_deduct_credits_unlocked (db, from_account_id, amount, tx_type,
                                  tx_group_id, NULL);
  if (rc != 0)
    {
      LOGW ("h_bank_transfer_unlocked: Failed to deduct %lld from %s:%d (account %d). Error: %d",
            amount, from_owner_type, from_owner_id, from_account_id, rc);
      return rc;
    }
  
  /* Add to destination */
  rc = h_add_credits_unlocked (db, to_account_id, amount, tx_type, tx_group_id, NULL);
  if (rc != 0)
    {
      LOGE ("h_bank_transfer_unlocked: Failed to add %lld to %s:%d (account %d). Error: %d",
            amount, to_owner_type, to_owner_id, to_account_id, rc);
      /* Attempt refund */
      h_add_credits_unlocked (db, from_account_id, amount, "REFUND",
                              "TRANSFER_FAILED", NULL);
      return rc;
    }
  
  return 0;
}

/* Check if a port is a black market based on port name */
bool
h_is_black_market_port (db_t *db, int port_id)
{
  if (!db || port_id <= 0)
    {
      return false;
    }
  
  db_res_t *res = NULL;
  db_error_t err;
  bool is_bm = false;
  
  const char *sql_template = "SELECT name FROM ports WHERE port_id = {1} LIMIT 1;";
  char sql[256];
  sql_build(db, sql_template, sql, sizeof(sql));

  if (db_query (db, sql,
                (db_bind_t[]){db_bind_i32 (port_id)},
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          const char *name = db_res_col_text (res, 0, &err);
          if (name && strstr (name, "Black Market"))
            {
              is_bm = true;
            }
        }
      db_res_finalize (res);
    }
  
  return is_bm;
}
