#include <string.h>
#include <jansson.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>              // For snprintf
#include <string.h>             // For strcasecmp, strdup etc.
#include <math.h>               // For pow() function
#include <stddef.h>             // For size_t
#include <limits.h>             // For INT_MAX
/* local includes */
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
#include "globals.h"
#include "server_universe.h"
#include "db_player_settings.h"
#include "server_clusters.h"
#include "db/db_api.h"
#include "game_db.h"


#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif
void idemp_fingerprint_json (json_t *obj, char out[17]);
void iso8601_utc (char out[32]);
/* Forward declarations for static helper functions */

int h_calculate_port_buy_price (db_t *db, int port_id,
                                const char *commodity);
#define RULE_REFUSE(_code,_msg, \
                    _hint_json) do { send_response_refused_steal (ctx, \
                                                                  root, \
                                                                  ( \
                                                                    _code), \
                                                                  ( \
                                                                    _msg), \
                                                                  ( \
                                                                    _hint_json)); \
                                     goto refuse_buy; } while (0)
#define RULE_REFUSE_SELL(_code,_msg, \
                         _hint_json) do { send_response_refused_steal (ctx, \
                                                                       root, \
                                                                       (_code), \
                                                                       (_msg), \
                                                                       ( \
                                                                         _hint_json)); \
                                          goto refuse_sell; } while (0)

const char * commodity_to_code (db_t *db, const char *commodity);


/* Helpers */

#include <jansson.h>
#include <stdlib.h>


int
parse_trade_lines (json_t *jitems, TradeLine **out_lines, size_t *out_n)
{
  if (!out_lines || !out_n)
    {
      return -1;
    }

  *out_lines = NULL;
  *out_n = 0;

  if (!jitems || !json_is_array (jitems))
    {
      return -1;
    }

  size_t n = json_array_size (jitems);


  if (n == 0)
    {
      return -1;
    }

  TradeLine *lines = calloc (n, sizeof (*lines));


  if (!lines)
    {
      return -1;
    }

  for (size_t i = 0; i < n; i++)
    {
      json_t *it = json_array_get (jitems, i);


      if (!it || !json_is_object (it))
        {
          free_trade_lines (lines, i);
          return -1;
        }

      json_t *jcom = json_object_get (it, "commodity");


      if (!jcom || !json_is_string (jcom))
        {
          free_trade_lines (lines, i);
          return -1;
        }

      const char *com = json_string_value (jcom);


      if (!com || !*com)
        {
          free_trade_lines (lines, i);
          return -1;
        }

      /* quantity (buy) or amount (sell) */
      int qty = 0;
      json_t *jq = json_object_get (it, "quantity");


      if (jq && json_is_integer (jq))
        {
          qty = (int) json_integer_value (jq);
          lines[i].quantity = qty;
          lines[i].amount = 0;
        }
      else
        {
          json_t *ja = json_object_get (it, "amount");


          if (ja && json_is_integer (ja))
            {
              qty = (int) json_integer_value (ja);
              lines[i].amount = qty;
              lines[i].quantity = 0;
            }
          else
            {
              free_trade_lines (lines, i);
              return -1;
            }
        }

      if (qty <= 0)
        {
          free_trade_lines (lines, i);
          return -1;
        }

      /* optional */
      lines[i].unit_price = 0;
      {
        json_t *jup = json_object_get (it, "unit_price");


        if (jup && json_is_integer (jup))
          {
            int up = (int) json_integer_value (jup);


            if (up < 0)
              {
                free_trade_lines (lines, i);
                return -1;
              }
            lines[i].unit_price = up;
          }
      }

      lines[i].line_cost = 0;

      lines[i].commodity = strdup (com);
      if (!lines[i].commodity)
        {
          free_trade_lines (lines, i);
          return -1;
        }
    }

  *out_lines = lines;
  *out_n = n;
  return 0;
}


int
h_update_entity_stock (db_t *db,
                       const char *entity_type,
                       int entity_id,
                       const char *commodity_code,
                       int quantity_delta, int *new_quantity_out)
{
  int current_quantity = 0;
  /* Get current quantity (ignore error if not found, assume 0) */
  h_get_entity_stock_quantity (db,
                               entity_type,
                               entity_id,
                               commodity_code,
                               &current_quantity);

  int new_quantity;
  if (__builtin_add_overflow(current_quantity, quantity_delta, &new_quantity))
    {
      /* Overflow: clamp to INT_MAX or 0 depending on sign */
      new_quantity = (quantity_delta > 0) ? INT_MAX : 0;
    }


  if (new_quantity < 0)
    {
      new_quantity = 0;
    }

  const char *sql_upsert =
    "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "
    "VALUES ($1, $2, $3, $4, 0, $5) "
    "ON CONFLICT(entity_type, entity_id, commodity_code) DO UPDATE SET quantity = $4, last_updated_ts = $5;";

  db_error_t err;


  memset (&err, 0, sizeof (err));
  int64_t now = (int64_t) time (NULL);
  db_bind_t params[] = { db_bind_text (entity_type), db_bind_i32 (entity_id),
                         db_bind_text (commodity_code),
                         db_bind_i32 (new_quantity), db_bind_i64 (now) };


  if (!db_exec (db, sql_upsert, params, 5, &err))
    {
      LOGE ("h_update_entity_stock: upsert failed: %s", err.message);
      return -1;
    }

  if (new_quantity_out)
    {
      *new_quantity_out = new_quantity;
    }
  return 0;
}


static int
h_port_buys_commodity (db_t *db, int port_id, const char *commodity)
{
  if (!db || port_id <= 0 || !commodity || !*commodity)
    {
      return 0;
    }

  char *canonical_commodity_code = (char *) commodity_to_code (db, commodity);


  if (!canonical_commodity_code)
    {
      return 0; /* Invalid or unsupported commodity */
    }

  const char *sql =
    "SELECT es.quantity, p.size * 1000 AS max_capacity "
    "FROM ports p JOIN entity_stock es ON p.port_id = es.entity_id "
    "WHERE es.entity_type = 'port' AND es.commodity_code = $2 AND p.port_id = $1 LIMIT 1;";

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[] = {
    db_bind_i32 (port_id),
    db_bind_text (canonical_commodity_code)
  };


  if (!db_query (db,
                 sql,
                 params,
                 sizeof (params) / sizeof (params[0]),
                 &res,
                 &err))
    {
      LOGE ("h_port_buys_commodity: query failed: %s",
            err.message[0] ? err.message : "database error");
      free (canonical_commodity_code);
      return 0;
    }

  int buys = 0;


  db_error_clear (&err);
  if (db_res_step (res, &err))
    {
      int current_quantity = db_res_col_int (res, 0, &err);
      int max_capacity = db_res_col_int (res, 1, &err);


      /* If a conversion error occurred, preserve conservative behaviour: buys remains 0 */
      if (!err.code && current_quantity < max_capacity)
        {
          buys = 1;
        }
    }
  else
    {
      if (err.code)
        {
          /* Step error: preserve conservative behaviour (buys=0), but log */
          LOGE ("h_port_buys_commodity: step failed: %s",
                err.message[0] ? err.message : "database error");
        }
      else
        {
          /* No entry => assume port doesn't trade this commodity or is full */
          LOGD (
            "h_port_buys_commodity: No entity_stock entry for port %d, commodity %s",
            port_id,
            canonical_commodity_code);
        }
    }

  db_res_finalize (res);
  free (canonical_commodity_code);
  return buys;
}


// Helper to get entity stock quantity (generic)
int
h_get_entity_stock_quantity (db_t *db,
                             const char *entity_type,
                             int entity_id,
                             const char *commodity_code,
                             int *qty_out)
{
  if (!db || !entity_type || !*entity_type || !commodity_code ||
      !*commodity_code || !qty_out)
    {
      return ERR_DB_MISUSE;
    }

  const char *sql =
    "SELECT quantity "
    "FROM entity_stock "
    "WHERE entity_type = $1 AND entity_id = $2 AND commodity_code = $3;";

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[] = {
    db_bind_text (entity_type),
    db_bind_i32 (entity_id),
    db_bind_text (commodity_code)
  };


  if (!db_query (db,
                 sql,
                 params,
                 sizeof (params) / sizeof (params[0]),
                 &res,
                 &err))
    {
      return err.code ? err.code : ERR_DB_QUERY_FAILED;
    }

  int rc;


  db_error_clear (&err);
  if (db_res_step (res, &err))
    {
      *qty_out = db_res_col_int (res, 0, &err);
      rc = err.code ? err.code : 0; /* keep “0 means OK” shape */
    }
  else
    {
      if (err.code)
        {
          rc = err.code;
        }
      else
        {
          *qty_out = 0;
          rc = ERR_DB_NOT_FOUND;
        }
    }

  db_res_finalize (res);
  return rc;
}


const char *
commodity_to_code (db_t *db, const char *commodity)
{
  if (!commodity || !*commodity)
    {
      return NULL;
    }

  const char *sql =
    "SELECT code FROM commodities WHERE UPPER(code) = UPPER($1) LIMIT 1;";

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[] = {
    db_bind_text (commodity)
  };


  if (!db_query (db,
                 sql,
                 params,
                 sizeof (params) / sizeof (params[0]),
                 &res,
                 &err))
    {
      LOGE ("commodity_to_code: query failed: %s",
            err.message[0] ? err.message : "database error");
      return NULL;
    }

  char *result = NULL;


  db_error_clear (&err);
  if (db_res_step (res, &err))
    {
      const char *canonical_code = db_res_col_text (res, 0, &err);


      if (!err.code && canonical_code && *canonical_code)
        {
          result = strdup (canonical_code);
        }
    }
  else
    {
      /* no row => result stays NULL; if err.code set, conservative behaviour is NULL */
      if (err.code)
        {
          LOGE ("commodity_to_code: step failed: %s",
                err.message[0] ? err.message : "database error");
        }
    }

  db_res_finalize (res);
  return result;
}


/////////// STUBS ///////////////////////
int
cmd_trade_offer (client_ctx_t *ctx, json_t *root)
{
  // Option A: Hide/Refuse for v1.0
  send_response_error (ctx,
                       root,
                       ERR_NOT_IMPLEMENTED,
                       "Trading handshake disabled in v1.0");
  return 0;
}


int
cmd_trade_accept (client_ctx_t *ctx, json_t *root)
{
  // Option A: Hide/Refuse for v1.0
  send_response_error (ctx,
                       root,
                       ERR_NOT_IMPLEMENTED,
                       "Trading handshake disabled in v1.0");
  return 0;
}


int
cmd_trade_cancel (client_ctx_t *ctx, json_t *root)
{
  // Option A: Hide/Refuse for v1.0
  send_response_error (ctx,
                       root,
                       ERR_NOT_IMPLEMENTED,
                       "Trading handshake disabled in v1.0");
  return 0;
}


//////////////////////////////////////////////////
// --- helpers ---------------------------------------------------------------
static int
json_equal_strict (json_t *a, json_t *b)
{
  // serialise to canonical strings to compare (simplest reliable check)
  char *sa = json_dumps (a, JSON_COMPACT | JSON_SORT_KEYS);
  char *sb = json_dumps (b, JSON_COMPACT | JSON_SORT_KEYS);
  int same = (sa && sb && strcmp (sa, sb) == 0);
  free (sa);
  free (sb);
  return same;
}


//// FIND ALL CALLING SITES and modify to:


/* const char *sql = */


/*   "SELECT ... WHERE a = $1 AND b = $2 AND c = $3;"; */


/* db_bind_t binds[] = { */


/*   DB_P_INT(a), */


/*   DB_P_INT(b), */


/*   cursor ? DB_P_TEXT(cursor) : DB_P_NULL() */


/* }; */


/* if (db_query(db, &res, sql, binds, DB_NBINDS(binds), &err) != 0) { ... } */


/* static int */


/* bind_text_or_null (sqlite3_stmt *st, int idx, const char *s) */


/* { */


/*   if (s) */


/*     { */


/*       return sqlite3_bind_text (st, idx, s, -1, SQLITE_TRANSIENT); */


/*     } */


/*   return sqlite3_bind_null (st, idx); */


/* } */


int
h_entity_calculate_sell_price (db_t *db,
                               const char *entity_type,
                               int entity_id, const char *commodity)
{
  if (strcmp (entity_type, ENTITY_TYPE_PORT) == 0)
    {
      return h_calculate_port_sell_price (db, entity_id, commodity);
    }
  return 0;                     // Unknown entity
}


int
h_entity_calculate_buy_price (db_t *db,
                              const char *entity_type,
                              int entity_id, const char *commodity)
{
  if (strcmp (entity_type, ENTITY_TYPE_PORT) == 0)
    {
      return h_calculate_port_buy_price (db, entity_id, commodity);
    }
  return 0;                     // Unknown entity
}


int
h_calculate_port_sell_price (db_t *db, int port_id, const char *commodity)
{
  // LOGD("h_calculate_port_sell_price: Entering with port_id=%d, commodity=%s", port_id, commodity);
  if (!db || port_id <= 0 || !commodity || !*commodity)
    {
      LOGW
      (
        "h_calculate_port_sell_price: Invalid input: db=%p, port_id=%d, commodity=%s",
        db,
        port_id,
        commodity);
      return 0;
    }

  /* Assume commodity is already canonical code */
  const char *canonical_commodity = commodity;

  const char *sql =
    "SELECT c.base_price, c.volatility, "
    "       es.quantity, "
    "       p.size * 1000 AS max_capacity, "
    "       p.techlevel, "
    "       ec.price_elasticity, "
    "       ec.volatility_factor "
    "FROM commodities c "
    "JOIN ports p ON p.port_id = $1 "
    "JOIN entity_stock es ON p.port_id = es.entity_id AND es.entity_type = 'port' AND es.commodity_code = c.code "
    "JOIN economy_curve ec ON p.economy_curve_id = ec.economy_curve_id "
    "WHERE c.code = $2 LIMIT 1;";

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[] = {
    db_bind_i32 (port_id),
    db_bind_text (canonical_commodity)
  };


  if (!db_query (db,
                 sql,
                 params,
                 sizeof (params) / sizeof (params[0]),
                 &res,
                 &err))
    {
      /* Old code logged "prepare failed" with rc; we no longer have prepare/bind rc here. */
      LOGE ("h_calculate_port_sell_price: prepare failed: %s (rc=%d)",
            err.message[0] ? err.message : "database error",
            err.code);
      return 0;
    }

  int base_price = 0;
  int quantity = 0;
  int max_capacity = 0;
  int techlevel = 0;
  double price_elasticity = 0.0;
  double volatility_factor = 0.0;


  db_error_clear (&err);
  if (db_res_step (res, &err))
    {
      base_price = db_res_col_int (res, 0, &err);
      /* volatility col 1 exists but intentionally unused */
      quantity = db_res_col_int (res, 2, &err);
      max_capacity = db_res_col_int (res, 3, &err);
      techlevel = db_res_col_int (res, 4, &err);
      price_elasticity = db_res_col_double (res, 5, &err);
      volatility_factor = db_res_col_double (res, 6, &err);

      if (err.code)
        {
          /* Conversion/type error; conservative behaviour: return 0 */
          LOGE (
            "h_calculate_port_sell_price: column conversion failed: %s (rc=%d)",
            err.message[0] ? err.message : "database error",
            err.code);
          db_res_finalize (res);
          return 0;
        }
    }
  else
    {
      /* No row OR step error. Old code treated non-ROW as "no data found". */
      LOGW
      (
        "h_calculate_port_sell_price: No data found for canonical_commodity %s at port %d (rc_step=%d, errmsg=%s)",
        canonical_commodity,
        port_id,
        err.code,
        /* best available analogue for rc_step */
        err.message[0] ? err.message : "database error");

      db_res_finalize (res);
      return 0; /* Port not selling this commodity or no stock info */
    }

  db_res_finalize (res);

  if (base_price <= 0)
    {
      LOGW
      (
        "h_calculate_port_sell_price: Base price is zero or less for canonical_commodity %s at port %d",
        canonical_commodity,
        port_id);
      return 0;
    }

  double price_multiplier = 1.0;


  if (max_capacity > 0)
    {
      double fill_ratio = (double) quantity / max_capacity;


      if (fill_ratio < 0.5)
        {
          price_multiplier = 1.0 + (1.0 - fill_ratio) * price_elasticity *
                             volatility_factor;
        }
      else
        {
          price_multiplier = 1.0 - (fill_ratio - 0.5) * price_elasticity *
                             volatility_factor;
        }
    }

  /* Adjust for techlevel (higher techlevel means better prices for the port, so higher sell price) */
  price_multiplier *= (1.0 + (techlevel - 1) * 0.05);

  return (int) ceil (base_price * price_multiplier);
}


/**
 * @brief Checks if a port is selling a specific commodity.
 *
 * Uses ports.* columns:
 *   - product_ore, product_organics, product_equipment
 * A port is "selling" a commodity if product_X > 0.
 *
 * Returns:
 *   1 if selling, 0 otherwise (including errors / unknown commodity).
 */
static int
h_port_sells_commodity (db_t *db, int port_id, const char *commodity)
{
  if (!db || port_id <= 0 || !commodity || !*commodity)
    {
      return 0;
    }
  int quantity = 0;
  int rc = h_get_port_commodity_quantity (db, port_id, commodity, &quantity);


  if (rc == 0 && quantity > 0)
    {
      return 1;
    }
  return 0;
}


/* New Helpers for Illegal Goods and Cluster Alignment */


/**
 * @brief Checks if a commodity is marked as illegal in the commodities table.
 * @param db The database handle.
 * @param commodity_code The canonical code of the commodity.
 * @return True if the commodity is illegal, false otherwise.
 */
static bool
h_is_illegal_commodity (db_t *db, const char *commodity_code)
{
  if (!db || !commodity_code || !*commodity_code)
    {
      return false;
    }

  const char *sql = "SELECT illegal FROM commodities WHERE code = $1 LIMIT 1";

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[] = {
    db_bind_text (commodity_code)
  };


  if (!db_query (db,
                 sql,
                 params,
                 sizeof (params) / sizeof (params[0]),
                 &res,
                 &err))
    {
      LOGE ("h_is_illegal_commodity: Failed to query: %s",
            err.message[0] ? err.message : "database error");
      return false;
    }

  bool illegal = false;


  db_error_clear (&err);
  if (db_res_step (res, &err))
    {
      int v = db_res_col_int (res, 0, &err);


      if (!err.code)
        {
          illegal = (v != 0);
        }
      /* On conversion error, preserve conservative behaviour: illegal remains false */
    }
  else
    {
      /* No row or step error => illegal remains false (matches old behaviour) */
      /* (Old code had no warning here; keep it silent.) */
    }

  db_res_finalize (res);
  return illegal;
}


static bool
h_can_trade_commodity (db_t *db,
                       int port_id, int player_id, const char *commodity_code)
{
  if (!db || port_id <= 0 || player_id <= 0 || !commodity_code)
    {
      return false;
    }

  // 1. Check if the commodity is even one a port is designed to store (e.g., no 'FOOD' in ports)
  // Use h_get_port_commodity_quantity to check if the commodity exists in entity_stock for this port
  int dummy_qty;                // No need to read the actual quantity here, just existence
  int rc_get_qty = h_get_port_commodity_quantity (db,
                                                  port_id,
                                                  commodity_code,
                                                  &dummy_qty);


  /* Refactor: replace SQLITE_OK / SQLITE_NOTFOUND with DB-layer equivalents */
  if (rc_get_qty != 0 && rc_get_qty != ERR_DB_NOT_FOUND)
    {
      LOGE
      (
        "h_can_trade_commodity: Error checking commodity existence for port %d, commodity %s (rc=%d)",
        port_id,
        commodity_code,
        rc_get_qty);
      return false;             // Error reading DB
    }

  // If not found in entity_stock, assume port doesn't trade this commodity (or it's not present)
  if (rc_get_qty == ERR_DB_NOT_FOUND)
    {
      return false;
    }

  bool illegal_commodity_status = h_is_illegal_commodity (db, commodity_code);


  LOGD
  (
    "h_can_trade_commodity: Port %d, Player %d, Cmd %s: illegal_commodity_status=%d",
    port_id,
    player_id,
    commodity_code,
    illegal_commodity_status);

  // 2. If commodity is not illegal, allow (subject to existing rules)
  if (!illegal_commodity_status)
    {
      return true;
    }

  // From here, we know it's an illegal commodity.
  // 3. Get port's sector and its cluster alignment
  int sector_id = 0;

  {
    db_res_t *res = NULL;
    db_error_t err;


    db_error_clear (&err);

    const char *sql = "SELECT sector_id FROM ports WHERE port_id = $1 LIMIT 1";

    db_bind_t params[] = {
      db_bind_i32 (port_id)
    };


    /* Preserve old behaviour: if query fails, we just fall through with sector_id == 0 */
    if (db_query (db,
                  sql,
                  params,
                  sizeof (params) / sizeof (params[0]),
                  &res,
                  &err))
      {
        db_error_clear (&err);
        if (db_res_step (res, &err))
          {
            sector_id = db_res_col_int (res, 0, &err);
            if (err.code)
              {
                sector_id = 0; /* conservative */
              }
          }
        db_res_finalize (res);
      }
  }


  if (sector_id == 0)
    {
      LOGD ("h_can_trade_commodity: Port %d not linked to a sector.",
            port_id);
      return false;             // Port not linked to a sector?
    }

  int cluster_align_band_id = 0;


  h_get_cluster_alignment_band (db, sector_id, &cluster_align_band_id);
  int cluster_is_evil = 0;
  int cluster_is_good = 0;


  db_alignment_band_for_value (db,
                               cluster_align_band_id,
                               NULL,
                               NULL,
                               NULL,
                               &cluster_is_good,
                               &cluster_is_evil, NULL, NULL);

  LOGD
  (
    "h_can_trade_commodity: Port %d, Sector %d (Cluster Band %d): cluster_is_good=%d, cluster_is_evil=%d",
    port_id,
    sector_id,
    cluster_align_band_id,
    cluster_is_good,
    cluster_is_evil);

  // 4. Check cluster alignment for illegal trade
  if (cluster_is_good)
    {
      // Good cluster – no illegal trade
      LOGD
      (
        "h_can_trade_commodity: Port %d, Cmd %s: Refused because cluster is good.",
        port_id,
        commodity_code);
      return false;
    }

  // 5. Check player alignment band properties
  int player_alignment = 0;     // Declare player_alignment


  db_player_get_alignment (db, player_id, &player_alignment);  // Retrieve player's raw alignment score
  int player_align_band_id = 0;
  int player_is_evil = 0;


  db_alignment_band_for_value (db,
                               player_alignment,
                               &player_align_band_id,
                               NULL, NULL, NULL, &player_is_evil, NULL, NULL);
  int neutral_band_value = db_get_config_int (db, "neutral_band", 75); // Get neutral_band from config


  LOGD
  (
    "h_can_trade_commodity: Port %d, Player %d (Alignment %d, Band %d): player_is_evil=%d, neutral_band_value=%d",
    port_id,
    player_id,
    player_alignment,
    player_align_band_id,
    player_is_evil,
    neutral_band_value);

  // If player is Good (alignment > neutral_band), refuse illegal trade.
  if (player_alignment > neutral_band_value)
    {
      LOGI
      (
        "h_can_trade_commodity: Port %d, Player %d, Cmd %s: Player alignment is too good (%d). Refused illegal trade.",
        port_id,
        player_id,
        commodity_code,
        player_alignment);
      return false;
    }

  if (!player_is_evil)
    {
      // If player is not evil (i.e., neutral), then check 'illegal_allowed_neutral' config
      if (!db_get_config_bool (db, "illegal_allowed_neutral", true))
        {
          LOGI
          (
            "h_can_trade_commodity: Port %d, Player %d, Cmd %s: Player alignment is neutral, and neutral illegal trade is disallowed. Refused.",
            port_id,
            player_id,
            commodity_code);
          return false;
        }
    }

  LOGI
  (
    "h_can_trade_commodity: Port %d, Player %d, Cmd %s: All conditions met. Allowed.",
    port_id,
    player_id,
    commodity_code);
  return true;                 // Evil player in an evil cluster, trading illegal goods is permitted
}


/**
 * @brief Updates the quantity of a commodity in entity_stock for a given port.
 * @param db The SQLite database handle.
 * @param port_id The ID of the port.
 * @param commodity_code The canonical code of the commodity.
 * @param quantity_delta The amount to add or subtract from the current quantity.
 * @param new_quantity_out Pointer to an integer to store the new quantity.
 * @return SQLITE_OK on success, or an SQLite error code.
 */
int
h_update_port_stock (db_t *db,
                     int port_id,
                     const char *commodity_code, int delta, int *new_qty_out)
{
  return h_update_entity_stock (db,
                                ENTITY_TYPE_PORT,
                                port_id, commodity_code, delta, new_qty_out);
}


/**
 * @brief Adjusts a port's stock for market settlement purposes.
 *
 * Explicitly enforces that the resulting quantity stays within [0, max_capacity].
 * Does NOT generate logs or messages (other than debug/error logs).
 *
 * @param db Database handle.
 * @param port_id The ID of the port.
 * @param commodity_code The canonical code of the commodity.
 * @param quantity_delta The amount to add (positive) or subtract (negative).
 * @return 0 on success, or an ERR_DB_* code (or backend-mapped err.code).
 */
int
h_market_move_port_stock (db_t *db,
                          int port_id,
                          const char *commodity_code,
                          int quantity_delta)
{
  if (!db || port_id <= 0 || !commodity_code)
    {
      return ERR_DB_MISUSE;
    }

  if (quantity_delta == 0)
    {
      return 0;
    }

  /* 1. Get current quantity and port size (for max capacity) */
  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  const char *sql_info =
    "SELECT es.quantity, p.size * 1000 AS max_capacity "
    "FROM ports p "
    "LEFT JOIN entity_stock es "
    "  ON p.port_id = es.entity_id "
    " AND es.entity_type = 'port' "
    " AND es.commodity_code = $2 "
    "WHERE p.port_id = $1;";

  db_bind_t params[] = {
    db_bind_i32 (port_id),
    db_bind_text (commodity_code)
  };


  if (!db_query (db, sql_info, params, sizeof (params) / sizeof (params[0]),
                 &res, &err))
    {
      LOGE ("h_market_move_port_stock: prepare info failed: %s",
            err.message[0] ? err.message : "database error");
      return err.code ? err.code : ERR_DB_QUERY_FAILED;
    }

  int current_quantity = 0;
  int max_capacity = 0;


  db_error_clear (&err);
  if (db_res_step (res, &err))
    {
      /* NULL becomes 0 (matches old behaviour) */
      if (!db_res_col_is_null (res, 0))
        {
          current_quantity = (int) db_res_col_i32 (res, 0, &err);
        }
      else
        {
          current_quantity = 0;
        }

      max_capacity = (int) db_res_col_i32 (res, 1, &err);

      if (err.code)
        {
          db_res_finalize (res);
          return err.code;
        }
    }
  else
    {
      db_res_finalize (res);

      if (err.code)
        {
          return err.code;
        }

      LOGE ("h_market_move_port_stock: Port %d not found.", port_id);
      return ERR_DB_NOT_FOUND;
    }

  db_res_finalize (res);

  /* 2. Calculate new quantity with overflow and bounds checking */
  int new_quantity;
  if (__builtin_add_overflow(current_quantity, quantity_delta, &new_quantity))
    {
      /* Overflow: clamp based on delta direction */
      new_quantity = (quantity_delta > 0) ? INT_MAX : 0;
    }


  new_quantity = (new_quantity < 0) ? 0 : new_quantity;
  new_quantity = (new_quantity > max_capacity) ? max_capacity : new_quantity;

  /* 3. Update DB */
  return h_update_entity_stock (db,
                                ENTITY_TYPE_PORT,
                                port_id,
                                commodity_code,
                                new_quantity - current_quantity,
                                NULL);
}


void
free_trade_lines (TradeLine *lines, size_t n)
{
  if (!lines)
    {
      return;
    }
  for (size_t i = 0; i < n; i++)
    {
      if (lines[i].commodity)
        {
          free (lines[i].commodity);
          lines[i].commodity = NULL;    // <--- CRITICAL FIX
        }
    }
  //  free (lines);
}


int
cmd_trade_quote (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");
  json_t *payload = NULL;
  const char *commodity = NULL;
  char *commodity_code = NULL;   /* NOTE: owned */
  int port_id = 0;
  int quantity = 0;

  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated", NULL);
      return 0;
    }

  if (!json_is_object (data))
    {
      send_response_error (ctx, root, 400, "Missing data object.");
      return 0;
    }

  json_t *jport_id = json_object_get (data, "port_id");


  if (json_is_integer (jport_id))
    {
      port_id = (int) json_integer_value (jport_id);
    }

  json_t *jcommodity = json_object_get (data, "commodity");


  if (json_is_string (jcommodity))
    {
      commodity = json_string_value (jcommodity);
    }

  json_t *jquantity = json_object_get (data, "quantity");


  if (json_is_integer (jquantity))
    {
      quantity = (int) json_integer_value (jquantity);
    }

  if (port_id <= 0 || !commodity || quantity <= 0)
    {
      send_response_error (ctx, root, 400,
                           "port_id, commodity, and quantity are required.");
      return 0;
    }

  /* Validate commodity using commodity_to_code (returns strdup()’d string) */
  commodity_code = (char *) commodity_to_code (db, commodity);
  if (!commodity_code)
    {
      send_response_error (ctx, root, 400, "Invalid commodity.");
      return 0;
    }

  /* Calculate prices */
  int player_buy_price_per_unit =
    h_entity_calculate_sell_price (db, ENTITY_TYPE_PORT, port_id,
                                   commodity_code);
  long long total_player_buy_price =
    (long long) player_buy_price_per_unit * quantity;

  int player_sell_price_per_unit =
    h_entity_calculate_buy_price (db, ENTITY_TYPE_PORT, port_id,
                                  commodity_code);
  long long total_player_sell_price =
    (long long) player_sell_price_per_unit * quantity;


  payload = json_object ();
  json_object_set_new (payload, "port_id", json_integer (port_id));
  json_object_set_new (payload, "commodity", json_string (commodity));
  json_object_set_new (payload, "quantity", json_integer (quantity));
  json_object_set_new (payload, "buy_price",
                       json_real ((double) player_buy_price_per_unit));
  json_object_set_new (payload, "sell_price",
                       json_real ((double) player_sell_price_per_unit));
  json_object_set_new (payload, "total_buy_price",
                       json_integer (total_player_buy_price));
  json_object_set_new (payload, "total_sell_price",
                       json_integer (total_player_sell_price));

  send_response_ok_take (ctx, root, "trade.quote", &payload);

  free (commodity_code);
  return 0;
}


int
cmd_trade_history (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");

  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated", NULL);
      return 0;
    }

  /* 1. Get parameters */
  const char *cursor = NULL;
  int limit = 0;


  if (json_is_object (data))
    {
      json_t *jcursor = json_object_get (data, "cursor");


      if (json_is_string (jcursor))
        {
          cursor = json_string_value (jcursor);
        }

      json_t *jlimit = json_object_get (data, "limit");


      if (json_is_integer (jlimit))
        {
          limit = (int) json_integer_value (jlimit);
        }
    }

  if (limit <= 0 || limit > 50)
    {
      limit = 20;              /* Default or maximum */
    }

  /* 2. Prepare the query and cursor parameters */
  long long cursor_ts = 0;
  long long cursor_id = 0;


  /* Parse cursor safely: expected format "<ts>_<id>" */
  if (cursor && cursor[0])
    {
      char buf[128];
      size_t n = strlen (cursor);


      if (n < sizeof (buf))
        {
          memcpy (buf, cursor, n + 1);
          char *sep = strchr (buf, '_');


          if (sep)
            {
              *sep = '\0';
              cursor_ts = atoll (buf);
              cursor_id = atoll (sep + 1);
            }
        }
    }

  const char *sql_no_cursor =
    "SELECT timestamp, trade_log_id as id, port_id, commodity, units, price_per_unit, action "
    "FROM trade_log WHERE player_id = $1 "
    "ORDER BY timestamp DESC, trade_log_id DESC LIMIT $2;";

  const char *sql_with_cursor =
    "SELECT timestamp, trade_log_id as id, port_id, commodity, units, price_per_unit, action "
    "FROM trade_log WHERE player_id = $1 "
    "AND (timestamp < $3 OR (timestamp = $3 AND trade_log_id < $4)) "
    "ORDER BY timestamp DESC, trade_log_id DESC LIMIT $2;";

  const bool use_cursor = (cursor_ts > 0 && cursor_id > 0);
  const char *sql = use_cursor ? sql_with_cursor : sql_no_cursor;

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  /* LIMIT + 1 to detect next page (preserve behaviour) */
  int fetch_n = limit + 1;


  if (!use_cursor)
    {
      db_bind_t params[] = {
        db_bind_i32 (ctx->player_id),
        db_bind_i32 (fetch_n)
      };


      if (!db_query (db,
                     sql,
                     params,
                     sizeof (params) / sizeof (params[0]),
                     &res,
                     &err))
        {
          LOGE ("trade.history prepare error: %s",
                err.message[0] ? err.message : "database error");
          send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                               "Database error");
          return 0;
        }
    }
  else
    {
      db_bind_t params[] = {
        db_bind_i32 (ctx->player_id),
        db_bind_i32 (fetch_n),
        db_bind_i64 (cursor_ts),
        db_bind_i64 (cursor_id)
      };


      if (!db_query (db,
                     sql,
                     params,
                     sizeof (params) / sizeof (params[0]),
                     &res,
                     &err))
        {
          LOGE ("trade.history prepare error: %s",
                err.message[0] ? err.message : "database error");
          send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                               "Database error");
          return 0;
        }
    }

  /* 4. Fetch results */
  json_t *history_array = json_array ();
  int count = 0;
  long long last_ts = 0;
  long long last_id = 0;


  for (;;)
    {
      db_error_clear (&err);
      if (!db_res_step (res, &err))
        {
          /* done or error */
          break;
        }

      if (count < limit)
        {
          json_t *hist_row = json_object ();

          long long ts = (long long) db_res_col_i64 (res, 0, &err);
          long long id = (long long) db_res_col_i64 (res, 1, &err);
          int port_id = (int) db_res_col_i32 (res, 2, &err);

          const char *cmdty = db_res_col_text (res, 3, &err);
          int units = (int) db_res_col_i32 (res, 4, &err);
          double ppu = db_res_col_double (res, 5, &err);
          const char *action = db_res_col_text (res, 6, &err);


          /* If any conversion fails, treat as DB error (conservative) */
          if (err.code)
            {
              break;
            }

          json_object_set_new (hist_row, "timestamp", json_integer (ts));
          json_object_set_new (hist_row, "id", json_integer (id));
          json_object_set_new (hist_row, "port_id", json_integer (port_id));
          json_object_set_new (hist_row, "commodity",
                               json_string (cmdty ? cmdty : ""));
          json_object_set_new (hist_row, "units", json_integer (units));
          json_object_set_new (hist_row, "price_per_unit", json_real (ppu));
          json_object_set_new (hist_row, "action",
                               json_string (action ? action : ""));

          json_array_append_new (history_array, hist_row);

          last_ts = ts;
          last_id = id;
          count++;
        }
      else
        {
          /* fetched the (limit + 1) row => next page exists */
          break;
        }
    }

  db_res_finalize (res);

  /* If we broke due to DB error, preserve existing behaviour: generic DB error response */
  if (err.code)
    {
      LOGE ("trade.history step error: %s",
            err.message[0] ? err.message : "database error");
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Database error");
      json_decref (history_array);
      return 0;
    }

  /* 5. Build and send response */
  json_t *payload = json_object ();


  json_object_set_new (payload,
                       "history",
                       history_array);

  if (count == limit && last_id > 0)
    {
      char next_cursor[64];


      snprintf (next_cursor, sizeof (next_cursor), "%lld_%lld", last_ts,
                last_id);
      json_object_set_new (payload, "next_cursor", json_string (next_cursor));
    }

  send_response_ok_take (ctx, root, "trade.history", &payload);
  return 0;
}


int
cmd_dock_status (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  int player_ship_id = 0;
  int resolved_port_id = 0;
  char *player_name = NULL;
  char *ship_name = NULL;
  char *port_name = NULL;
  json_t *payload = NULL;

  if (!ctx || !root)
    {
      return -1;
    }


  if (!db)
    {
      send_response_error (ctx, root, 500, "No database handle.");
      return -1;
    }

  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated", NULL);
      return 0;
    }

  player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (player_ship_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NO_ACTIVE_SHIP,
                                   "No active ship found.", NULL);
      return 0;
    }

  /* Resolve port ID in current sector */
  resolved_port_id = db_get_port_id_by_sector (db, ctx->sector_id);

  const char *action = NULL;
  json_t *j_action =
    json_object_get (json_object_get (root, "data"), "action");


  if (json_is_string (j_action))
    {
      action = json_string_value (j_action);
    }

  int new_ported_status = resolved_port_id;


  if (action && strcasecmp (action, "undock") == 0)
    {
      new_ported_status = 0;
    }
  else if (action && strcasecmp (action, "dock") == 0)
    {
      new_ported_status = resolved_port_id;
    }
  else if (action)
    {
      /* Leave behaviour unchanged (ignore unknown action). */
    }

  if (action)
    {
      if (new_ported_status > 0 && !cluster_can_trade (db,
                                                       ctx->sector_id,
                                                       ctx->player_id))
        {
          send_response_refused_steal (ctx,
                                       root,
                                       REF_TURN_COST_EXCEEDS,
                                       "Port refuses docking: You are banned in this cluster.",
                                       NULL);
          return 0;
        }

      /* Update ships.ported status (and clear onplanet) */
      {
        db_error_t err;


        db_error_clear (&err);

        const char *sql_update_ported =
          "UPDATE ships SET ported = $1, onplanet = 0 WHERE ship_id = $2;";

        db_bind_t params[] = {
          db_bind_i32 (new_ported_status),
          db_bind_i32 (player_ship_id)
        };


        if (!db_exec (db, sql_update_ported,
                      params, sizeof (params) / sizeof (params[0]),
                      &err))
          {
            LOGE ("cmd_dock_status: Failed to update ships.ported: %s",
                  err.message[0] ? err.message : "database error");
            send_response_error (ctx, root, ERR_DB, "Database error.");
            return -1;
          }
      }

      /* Generate System Notice if successfully docked at a port */
      if (new_ported_status > 0)
        {
          db_get_ship_name (db, player_ship_id, &ship_name);
          db_get_port_name (db, new_ported_status, &port_name);
          db_player_name (db, ctx->player_id, &player_name);

          char notice_body[512];


          snprintf (notice_body,
                    sizeof (notice_body),
                    "Player %s (ID: %d)'s ship '%s' (ID: %d) docked at port '%s' (ID: %d) in Sector %d.",
                    player_name ? player_name : "Unknown",
                    ctx->player_id,
                    ship_name ? ship_name : "Unknown",
                    player_ship_id,
                    port_name ? port_name : "Unknown",
                    new_ported_status,
                    ctx->sector_id);

          db_notice_create (db, "Docking Log", notice_body, "info",
                            time (NULL) + (86400 * 7));
        }

      /* Use new status for response */
      resolved_port_id = new_ported_status;
    }
  else
    {
      /* Status check only - fetch actual status from DB */
      db_res_t *res = NULL;
      db_error_t err;


      db_error_clear (&err);

      const char *sql_check = "SELECT ported FROM ships WHERE ship_id = $1";

      db_bind_t params[] = {
        db_bind_i32 (player_ship_id)
      };


      if (db_query (db, sql_check,
                    params, sizeof (params) / sizeof (params[0]),
                    &res, &err))
        {
          db_error_clear (&err);
          if (db_res_step (res, &err))
            {
              int v = (int) db_res_col_i32 (res, 0, &err);


              if (!err.code)
                {
                  resolved_port_id = v;
                }
            }
          db_res_finalize (res);
        }
      /* Old code silently ignored prepare/step failures here; keep it that way. */
    }

  payload = json_object ();
  if (!payload)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Out of memory.");
      goto cleanup;
    }

  json_object_set_new (payload, "port_id", json_integer (resolved_port_id));
  json_object_set_new (payload, "sector_id", json_integer (ctx->sector_id));
  json_object_set_new (payload, "docked", json_boolean (resolved_port_id > 0));

  send_response_ok_take (ctx, root, "dock.status_v1", &payload);
  payload = NULL;               /* Ownership transferred */

cleanup:
  if (player_name)
    {
      free (player_name);
    }
  if (ship_name)
    {
      free (ship_name);
    }
  if (port_name)
    {
      free (port_name);
    }
  if (payload)
    {
      json_decref (payload);
    }
  return 0;
}


int
h_calculate_port_buy_price (db_t *db, int port_id, const char *commodity)
{
  // LOGD("h_calculate_port_buy_price: Entering with port_id=%d, commodity=%s", port_id, commodity);
  if (!db || port_id <= 0 || !commodity || !*commodity)
    {
      LOGW
      (
        "h_calculate_port_buy_price: Invalid input: db=%p, port_id=%d, commodity=%s",
        db,
        port_id,
        commodity);
      return 0;
    }

  // Assume commodity is already canonical code
  const char *canonical_commodity = commodity;

  const char *sql =
    "SELECT c.base_price, c.volatility, "
    "       es.quantity, "
    "       p.size * 1000 AS max_capacity, "
    "       p.techlevel, "
    "       ec.price_elasticity, "
    "       ec.volatility_factor "
    "FROM commodities c "
    "JOIN ports p ON p.port_id = $1 "
    "JOIN entity_stock es ON p.port_id = es.entity_id AND es.entity_type = 'port' AND es.commodity_code = c.code "
    "JOIN economy_curve ec ON p.economy_curve_id = ec.economy_curve_id "
    "WHERE c.code = $2 LIMIT 1;";

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[] = {
    db_bind_i32 (port_id),
    db_bind_text (canonical_commodity)
  };


  if (!db_query (db,
                 sql,
                 params,
                 sizeof (params) / sizeof (params[0]),
                 &res,
                 &err))
    {
      LOGE ("h_calculate_port_buy_price: prepare failed: %s (rc=%d)",
            err.message[0] ? err.message : "database error",
            err.code);
      return 0;
    }

  int base_price = 0;
  int quantity = 0;
  int max_capacity = 0;
  int techlevel = 0;
  double price_elasticity = 0.0;
  double volatility_factor = 0.0;


  db_error_clear (&err);
  if (db_res_step (res, &err))
    {
      base_price = db_res_col_int (res, 0, &err);
      /* volatility col 1 exists but intentionally unused */
      quantity = db_res_col_int (res, 2, &err);
      max_capacity = db_res_col_int (res, 3, &err);
      techlevel = db_res_col_int (res, 4, &err);
      price_elasticity = db_res_col_double (res, 5, &err);
      volatility_factor = db_res_col_double (res, 6, &err);

      if (err.code)
        {
          LOGE (
            "h_calculate_port_buy_price: column conversion failed: %s (rc=%d)",
            err.message[0] ? err.message : "database error",
            err.code);
          db_res_finalize (res);
          return 0;
        }
    }
  else
    {
      LOGW
      (
        "h_calculate_port_buy_price: No data found for canonical_commodity %s at port %d (rc_step=%d, errmsg=%s)",
        canonical_commodity,
        port_id,
        err.code,
        err.message[0] ? err.message : "database error");
      db_res_finalize (res);
      return 0;                 // Port not buying this commodity or no stock info
    }

  db_res_finalize (res);

  if (quantity >= max_capacity)
    {
      return 0;                 // Port is full, it won't buy.
    }

  if (base_price <= 0)
    {
      LOGW
      (
        "h_calculate_port_buy_price: Base price is zero or less for canonical_commodity %s at port %d",
        canonical_commodity,
        port_id);
      return 0;
    }

  double price_multiplier = 1.0;


  if (max_capacity > 0)
    {
      double fill_ratio = (double) quantity / max_capacity;


      // Port buys (player sells): price is lower when supply is high
      if (fill_ratio > 0.5)
        {
          price_multiplier = 1.0 - (fill_ratio - 0.5) * price_elasticity *
                             volatility_factor;
        }
      else
        {
          price_multiplier = 1.0 + (0.5 - fill_ratio) * price_elasticity *
                             volatility_factor;
        }
    }

  // Adjust for techlevel (higher techlevel means better prices for the port, so lower buy price)
  price_multiplier *= (1.0 - (techlevel - 1) * 0.02);  // Port wants to buy low

  long long price = (long long) (base_price * price_multiplier + 0.999999); /* ceil */


  if (price < 1)
    {
      price = 1;
    }
  if (price > 2000000000LL)
    {
      price = 2000000000LL;
    }
  return (int) price;
}


int
cmd_trade_port_info (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  json_t *port = NULL;
  json_t *payload = NULL;
  json_t *commodities_array = NULL;

  int port_id_val = 0;
  int requested_port_id = 0;
  int sector_id = 0;

  int ok = 0; /* 0 = success path; set to -1 on failure */

  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated", NULL);
      return 0;
    }


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }

  sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 0;

  json_t *data = json_object_get (root, "data");


  if (json_is_object (data))
    {
      json_t *jport_id = json_object_get (data, "port_id");


      if (json_is_integer (jport_id))
        {
          requested_port_id = (int) json_integer_value (jport_id);
        }

      json_t *jsec = json_object_get (data, "sector_id");


      if (json_is_integer (jsec))
        {
          sector_id = (int) json_integer_value (jsec);
        }
    }

  /* Resolve by port_id if supplied */
  const char *sql_port_info = NULL;
  bool by_port_id = false;


  if (requested_port_id > 0)
    {
      sql_port_info =
        "SELECT port_id, number, name, sector_id, size, techlevel, petty_cash, type "
        "FROM ports WHERE port_id = $1 LIMIT 1;";
      port_id_val = requested_port_id;
      by_port_id = true;
    }
  else if (sector_id > 0)
    {
      sql_port_info =
        "SELECT port_id, number, name, sector_id, size, techlevel, petty_cash, type "
        "FROM ports WHERE sector_id = $1 LIMIT 1;";
      by_port_id = false;
    }
  else
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_MISSING_FIELD,
                                   "Missing port_id or sector_id", NULL);
      return 0;
    }

  /* --- Query port header --- */
  {
    db_res_t *res = NULL;
    db_error_t err;


    db_error_clear (&err);

    db_bind_t params[] = {
      db_bind_i32 (by_port_id ? requested_port_id : sector_id)
    };


    if (!db_query (db, sql_port_info,
                   params, sizeof (params) / sizeof (params[0]),
                   &res, &err))
      {
        LOGE ("cmd_trade_port_info: port prepare failed: %s",
              err.message[0] ? err.message : "database error");
        send_response_error (ctx, root, ERR_DB, "Database error.");
        ok = -1;
        goto cleanup;
      }

    db_error_clear (&err);
    if (!db_res_step (res, &err))
      {
        /* No row or step error: old code treated as "No port found" */
        if (err.code)
          {
            LOGE ("cmd_trade_port_info: port step failed: %s",
                  err.message[0] ? err.message : "database error");
            send_response_error (ctx, root, ERR_DB, "Database error.");
            db_res_finalize (res);
            ok = -1;
            goto cleanup;
          }

        LOGW ("cmd_trade_port_info: No port found for id=%d or sector=%d",
              requested_port_id, sector_id);
        send_response_refused_steal (ctx,
                                     root,
                                     ERR_PORT_NOT_FOUND,
                                     "No port found.", NULL);
        db_res_finalize (res);
        ok = -1;
        goto cleanup;
      }

    port = json_object ();
    if (!port)
      {
        LOGE ("cmd_trade_port_info: Out of memory for port object.");
        db_res_finalize (res);
        ok = -1;
        goto cleanup;
      }

    /* Column mapping matches original col_idx++ logic */
    db_error_clear (&err);

    int col_idx = 0;


    port_id_val = (int) db_res_col_i32 (res, col_idx++, &err);
    int number = (int) db_res_col_i32 (res, col_idx++, &err);
    const char *name = db_res_col_text (res, col_idx++, &err);
    int p_sector = (int) db_res_col_i32 (res, col_idx++, &err);
    int size = (int) db_res_col_i32 (res, col_idx++, &err);
    int tech = (int) db_res_col_i32 (res, col_idx++, &err);
    int petty = (int) db_res_col_i32 (res, col_idx++, &err);
    int type = (int) db_res_col_i32 (res, col_idx++, &err);


    if (err.code)
      {
        LOGE ("cmd_trade_port_info: port column conversion failed: %s",
              err.message[0] ? err.message : "database error");
        db_res_finalize (res);
        ok = -1;
        goto cleanup;
      }

    json_object_set_new (port, "id", json_integer (port_id_val));
    json_object_set_new (port, "number", json_integer (number));
    json_object_set_new (port, "name", json_string (name ? name : ""));
    json_object_set_new (port, "sector", json_integer (p_sector));
    json_object_set_new (port, "size", json_integer (size));
    json_object_set_new (port, "techlevel", json_integer (tech));
    json_object_set_new (port, "petty_cash", json_integer (petty));
    json_object_set_new (port, "type", json_integer (type));

    db_res_finalize (res);
  }

  /* --- Retrieve commodities from entity_stock --- */
  {
    const char *sql_commodities =
      "SELECT es.commodity_code, es.quantity, es.price, c.illegal "
      "FROM entity_stock es JOIN commodities c ON es.commodity_code = c.code "
      "WHERE es.entity_type = 'port' AND es.entity_id = $1;";


    LOGD
      ("cmd_trade_port_info: Executing SQL_COMMODITIES for port_id_val=%d: %s",
      port_id_val, sql_commodities);

    db_res_t *res = NULL;
    db_error_t err;


    db_error_clear (&err);

    db_bind_t params[] = {
      db_bind_i32 (port_id_val)
    };


    if (!db_query (db, sql_commodities,
                   params, sizeof (params) / sizeof (params[0]),
                   &res, &err))
      {
        LOGE
        (
          "cmd_trade_port_info: commodities prepare failed: %s (rc=%d, errmsg=%s)",
          err.message[0] ? err.message : "database error",
          err.code,
          err.message[0] ? err.message : "database error");
        send_response_error (ctx, root, ERR_DB, "Database error.");
        ok = -1;
        goto cleanup;
      }

    commodities_array = json_array ();
    if (!commodities_array)
      {
        LOGE ("cmd_trade_port_info: Out of memory for commodities array.");
        db_res_finalize (res);
        ok = -1;
        goto cleanup;
      }

    int row_count = 0;


    for (;;)
      {
        db_error_clear (&err);
        if (!db_res_step (res, &err))
          {
            /* done or error */
            break;
          }

        row_count++;

        db_error_clear (&err);
        const char *commodity_code = db_res_col_text (res, 0, &err);
        int quantity = (int) db_res_col_i32 (res, 1, &err);
        int price = (int) db_res_col_i32 (res, 2, &err);
        int illegal = (int) db_res_col_i32 (res, 3, &err);


        if (err.code)
          {
            /* Matches old behaviour: treat as DB error */
            LOGE ("cmd_trade_port_info: commodity step failed: %s",
                  err.message[0] ? err.message : "database error");
            db_res_finalize (res);
            ok = -1;
            goto cleanup;
          }

        json_t *commodity_obj = json_object ();


        if (!commodity_obj)
          {
            LOGE ("cmd_trade_port_info: Out of memory for commodity object.");
            db_res_finalize (res);
            ok = -1;
            goto cleanup;
          }

        json_object_set_new (commodity_obj, "code",
                             json_string (commodity_code ? commodity_code :
                                          ""));
        json_object_set_new (commodity_obj, "quantity",
                             json_integer (quantity));
        json_object_set_new (commodity_obj, "price", json_integer (price));
        json_object_set_new (commodity_obj, "illegal", json_boolean (illegal));

        LOGD
        (
          "cmd_trade_port_info: Processing commodity: code=%s, quantity=%d, illegal=%d",
          commodity_code ? commodity_code : "NULL",
          quantity,
          illegal);

        /* Apply h_can_trade_commodity rules for visibility of illegal goods */
        if (illegal && !h_can_trade_commodity (db,
                                               port_id_val,
                                               ctx->player_id,
                                               commodity_code))
          {
            LOGD
            (
              "cmd_trade_port_info: Commodity %s filtered for player %d (illegal=%d)",
              commodity_code ? commodity_code : "NULL",
              ctx->player_id,
              illegal);
            json_decref (commodity_obj);
            continue;
          }

        json_array_append_new (commodities_array, commodity_obj);
      }

    if (err.code)
      {
        /* Step error (not done) */
        LOGE ("cmd_trade_port_info: commodity step failed: %s",
              err.message[0] ? err.message : "database error");
        db_res_finalize (res);
        ok = -1;
        goto cleanup;
      }

    db_res_finalize (res);
  }

  json_object_set_new (port, "commodities", commodities_array);
  commodities_array = NULL; /* Ownership transferred to port object */

  payload = json_object ();
  if (!payload)
    {
      LOGE ("cmd_trade_port_info: Out of memory for payload object.");
      ok = -1;
      goto cleanup;
    }

  json_object_set_new (payload, "port", port);
  port = NULL; /* Ownership transferred to payload */

  send_response_ok_take (ctx, root, "trade.port_info", &payload);
  payload = NULL; /* Ownership transferred */

  ok = 0;

cleanup:
  if (port)
    {
      json_decref (port);
    }
  if (commodities_array)
    {
      json_decref (commodities_array);
    }
  if (payload)
    {
      json_decref (payload);
    }

  return (ok == 0) ? 0 : -1;
}


int
cmd_trade_jettison (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");
  json_t *payload = NULL;
  const char *commodity = NULL;
  int quantity = 0;
  int player_ship_id = 0;
  int rc = 0;

  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated", NULL);
      return 0;
    }

  player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (player_ship_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_AUTOPILOT_RUNNING,
                                   "No active ship found.", NULL);
      return 0;
    }

  h_decloak_ship (db, player_ship_id);
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);


  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "ship.jettison", root,
                                            NULL);
    }

  if (!json_is_object (data))
    {
      send_response_error (ctx, root, 400, "Missing data object.");
      return 0;
    }

  json_t *jcommodity = json_object_get (data, "commodity");


  if (json_is_string (jcommodity))
    {
      commodity = json_string_value (jcommodity);
    }

  json_t *jquantity = json_object_get (data, "quantity");


  if (json_is_integer (jquantity))
    {
      quantity = (int) json_integer_value (jquantity);
    }

  if (!commodity || quantity <= 0)
    {
      send_response_error (ctx,
                           root,
                           400,
                           "commodity and quantity are required, and quantity must be positive.");
      return 0;
    }

  /* Check current cargo */
  int cur_ore, cur_org, cur_eq, cur_holds, cur_colonists;
  int cur_slaves, cur_weapons, cur_drugs;


  if (h_get_ship_cargo_and_holds (db, player_ship_id,
                                  &cur_ore, &cur_org, &cur_eq, &cur_holds,
                                  &cur_colonists,
                                  &cur_slaves, &cur_weapons,
                                  &cur_drugs) != 0)
    {
      send_response_error (ctx, root, 500, "Could not read ship cargo.");
      return 0;
    }

  int have = 0;


  if (strcasecmp (commodity, "ore") == 0)
    {
      have = cur_ore;
    }
  else if (strcasecmp (commodity, "organics") == 0)
    {
      have = cur_org;
    }
  else if (strcasecmp (commodity, "equipment") == 0)
    {
      have = cur_eq;
    }
  else if (strcasecmp (commodity, "colonists") == 0)
    {
      have = cur_colonists;
    }
  else if (strcasecmp (commodity, "slaves") == 0)
    {
      have = cur_slaves;
    }
  else if (strcasecmp (commodity, "weapons") == 0)
    {
      have = cur_weapons;
    }
  else if (strcasecmp (commodity, "drugs") == 0)
    {
      have = cur_drugs;
    }
  else
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_AUTOPILOT_PATH_INVALID,
                                   "Unknown commodity.", NULL);
      return 0;
    }

  if (have < quantity)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_NO_WARP_LINK,
                                   "You do not carry enough of that commodity to jettison.",
                                   NULL);
      return 0;
    }

  /* Update ship cargo (jettisoning means negative delta) */
  int new_qty = 0;


  rc =
    h_update_ship_cargo (db, ctx->ship_id, commodity, -quantity, &new_qty);

  LOGD ("cmd_trade_jettison: Ship cargo updated. new_qty=%d", new_qty);

  if (rc != 0)
    {
      send_response_error (ctx, root, 500, "Failed to update ship cargo.");
      return 0;
    }

  /* Construct response with remaining cargo */
  payload = json_object ();
  json_t *remaining_cargo_array = json_array ();


  /* Re-fetch current cargo to ensure accurate remaining_cargo */
  if (h_get_ship_cargo_and_holds (db, player_ship_id,
                                  &cur_ore, &cur_org, &cur_eq, &cur_holds,
                                  &cur_colonists,
                                  &cur_slaves, &cur_weapons,
                                  &cur_drugs) == 0)
    {
      if (cur_ore > 0)
        {
          json_t *it = json_object ();


          json_object_set_new (it, "commodity", json_string ("ore"));
          json_object_set_new (it, "quantity", json_integer (cur_ore));
          json_array_append_new (remaining_cargo_array, it);
        }
      if (cur_org > 0)
        {
          json_t *it = json_object ();


          json_object_set_new (it, "commodity", json_string ("organics"));
          json_object_set_new (it, "quantity", json_integer (cur_org));
          json_array_append_new (remaining_cargo_array, it);
        }
      if (cur_eq > 0)
        {
          json_t *it = json_object ();


          json_object_set_new (it, "commodity", json_string ("equipment"));
          json_object_set_new (it, "quantity", json_integer (cur_eq));
          json_array_append_new (remaining_cargo_array, it);
        }
      if (cur_colonists > 0)
        {
          json_t *it = json_object ();


          json_object_set_new (it, "commodity", json_string ("colonists"));
          json_object_set_new (it, "quantity", json_integer (cur_colonists));
          json_array_append_new (remaining_cargo_array, it);
        }
      if (cur_slaves > 0)
        {
          json_t *it = json_object ();


          json_object_set_new (it, "commodity", json_string ("slaves"));
          json_object_set_new (it, "quantity", json_integer (cur_slaves));
          json_array_append_new (remaining_cargo_array, it);
        }
      if (cur_weapons > 0)
        {
          json_t *it = json_object ();


          json_object_set_new (it, "commodity", json_string ("weapons"));
          json_object_set_new (it, "quantity", json_integer (cur_weapons));
          json_array_append_new (remaining_cargo_array, it);
        }
      if (cur_drugs > 0)
        {
          json_t *it = json_object ();


          json_object_set_new (it, "commodity", json_string ("drugs"));
          json_object_set_new (it, "quantity", json_integer (cur_drugs));
          json_array_append_new (remaining_cargo_array, it);
        }
    }

  json_object_set_new (payload, "remaining_cargo", remaining_cargo_array);
  send_response_ok_take (ctx, root, "ship.jettisoned", &payload);
  return 0;
}


/* --- Port Robbery --- */
static int
h_robbery_get_config (db_t *db,
                      int *threshold,
                      int *xp_per_hold,
                      int *cred_per_xp,
                      double *chance_base,
                      int *turn_cost,
                      double *good_bonus,
                      double *pro_delta,
                      double *evil_cluster_bonus,
                      double *good_penalty_mult, int *ttl_days)
{
  static const char *SQL =
    "SELECT robbery_evil_threshold, robbery_xp_per_hold, robbery_credits_per_xp, "
    "robbery_bust_chance_base, robbery_turn_cost, good_guy_bust_bonus, "
    "pro_criminal_bust_delta, evil_cluster_bust_bonus, good_align_penalty_mult, "
    "robbery_real_bust_ttl_days FROM law_enforcement WHERE law_enforcement_id=1;";

  db_res_t *res = NULL;
  db_error_t dberr;
  memset (&dberr, 0, sizeof (dberr));

  if (db_query (db, SQL, NULL, 0, &res, &dberr))
    {
      if (db_res_step (res, &dberr))
        {
          if (threshold)
            {
              *threshold = (int) db_res_col_i64 (res, 0, &dberr);
            }
          if (xp_per_hold)
            {
              *xp_per_hold = (int) db_res_col_i64 (res, 1, &dberr);
            }
          if (cred_per_xp)
            {
              *cred_per_xp = (int) db_res_col_i64 (res, 2, &dberr);
            }
          if (chance_base)
            {
              *chance_base = db_res_col_double (res, 3, &dberr);
            }
          if (turn_cost)
            {
              *turn_cost = (int) db_res_col_i64 (res, 4, &dberr);
            }
          if (good_bonus)
            {
              *good_bonus = db_res_col_double (res, 5, &dberr);
            }
          if (pro_delta)
            {
              *pro_delta = db_res_col_double (res, 6, &dberr);
            }
          if (evil_cluster_bonus)
            {
              *evil_cluster_bonus = db_res_col_double (res, 7, &dberr);
            }
          if (good_penalty_mult)
            {
              *good_penalty_mult = db_res_col_double (res, 8, &dberr);
            }
          if (ttl_days)
            {
              *ttl_days = (int) db_res_col_i64 (res, 9, &dberr);
            }

          db_res_finalize (res);
          return 0;             /* SQLITE_OK */
        }
      db_res_finalize (res);
    }

  /* Fallback defaults or error */
  if (threshold)
    {
      *threshold = -10;
    }
  if (xp_per_hold)
    {
      *xp_per_hold = 20;
    }
  if (cred_per_xp)
    {
      *cred_per_xp = 10;
    }
  if (chance_base)
    {
      *chance_base = 0.05;
    }
  if (turn_cost)
    {
      *turn_cost = 1;
    }
  if (good_bonus)
    {
      *good_bonus = 0.10;
    }
  if (pro_delta)
    {
      *pro_delta = -0.02;
    }
  if (evil_cluster_bonus)
    {
      *evil_cluster_bonus = 0.05;
    }
  if (good_penalty_mult)
    {
      *good_penalty_mult = 3.0;
    }
  if (ttl_days)
    {
      *ttl_days = 7;
    }

  return 0;                     /* SQLITE_OK */
}


int
cmd_port_rob (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, 400, "Missing data.");
      return 0;
    }
  /* 1. Parse Inputs */
  int sector_id = (int) json_integer_value (json_object_get (data,
                                                             "sector_id"));
  int port_id = (int) json_integer_value (json_object_get (data, "port_id"));
  const char *mode = json_string_value (json_object_get (data, "mode"));
  const char *commodity = json_string_value (json_object_get (data,
                                                              "commodity"));


  if (sector_id <= 0 || port_id <= 0 || !mode)
    {
      send_response_error (ctx, root, 400, "Invalid parameters.");
      return 0;
    }
  /* 2. Pre-Checks (No Turn Cost) */
  /* Location Check */
  if (h_get_player_sector (db, ctx->player_id) != sector_id)
    {
      send_response_error (ctx,
                           root,
                           REF_TURN_COST_EXCEEDS,
                           "You are not in that sector.");
      return 0;
    }
  int port_real_sector = db_get_port_sector (db, port_id);


  if (port_real_sector != sector_id)
    {
      send_response_error (ctx,
                           root,
                           REF_AUTOPILOT_RUNNING,
                           "Port not found in sector.");
      return 0;
    }
  /* Active Bust Check */
  {
    static const char *SQL =
      "SELECT 1 FROM port_busts WHERE port_id=$1 AND player_id=$2 AND active=1";
    db_res_t *res = NULL;
    db_error_t dberr;


    memset (&dberr, 0, sizeof (dberr));
    db_bind_t params[2];


    params[0] = db_bind_i64 (port_id);
    params[1] = db_bind_i64 (ctx->player_id);

    if (db_query (db, SQL, params, 2, &res, &dberr))
      {
        if (db_res_step (res, &dberr))
          {
            db_res_finalize (res);
            send_response_refused_steal (ctx,
                                         root,
                                         REF_TURN_COST_EXCEEDS,
                                         "You are already wanted at this port.",
                                         NULL);
            return 0;
          }
        db_res_finalize (res);
      }
  }
  /* Cluster Ban Check */
  int cluster_id = 0;
  {
    static const char *SQL =
      "SELECT cluster_id FROM cluster_sectors WHERE sector_id=$1";
    db_res_t *res = NULL;
    db_error_t dberr;


    memset (&dberr, 0, sizeof (dberr));
    db_bind_t params[1];


    params[0] = db_bind_i64 (sector_id);

    if (db_query (db, SQL, params, 1, &res, &dberr))
      {
        if (db_res_step (res, &dberr))
          {
            cluster_id = (int) db_res_col_i64 (res, 0, &dberr);
          }
        db_res_finalize (res);
      }
  }


  if (cluster_id > 0)
    {
      static const char *SQL =
        "SELECT banned FROM cluster_player_status WHERE cluster_id=$1 AND player_id=$2";
      db_res_t *res = NULL;
      db_error_t dberr;


      memset (&dberr, 0, sizeof (dberr));
      db_bind_t params[2];


      params[0] = db_bind_i64 (cluster_id);
      params[1] = db_bind_i64 (ctx->player_id);

      if (db_query (db, SQL, params, 2, &res, &dberr))
        {
          if (db_res_step (res, &dberr))
            {
              if (db_res_col_i64 (res, 0, &dberr) == 1)
                {
                  db_res_finalize (res);
                  send_response_refused_steal (ctx,
                                               root,
                                               REF_TURN_COST_EXCEEDS,
                                               "Cluster authorities have banned you.",
                                               NULL);
                  return 0;
                }
            }
          db_res_finalize (res);
        }
    }
  /* Illegal Goods Check */
  if (strcmp (mode, "goods") == 0)
    {
      if (!commodity)
        {
          send_response_error (ctx,
                               root,
                               400, "Commodity required for goods mode.");
          return 0;
        }
      if (h_is_illegal_commodity (db, commodity))
        {
          if (!h_can_trade_commodity (db, port_id, ctx->player_id, commodity))
            {
              send_response_refused_steal (ctx,
                                           root,
                                           REF_SAFE_ZONE_ONLY,
                                           "Illegal trade restrictions apply to robbery targets too.",
                                           NULL);
              return 0;
            }
        }
    }
  /* 3. Turn Consumption */
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);


  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "port.rob", root, NULL);
    }
  /* 4. Config & Variables */
  int cfg_thresh = 0, cfg_xp_hold = 0, cfg_cred_xp = 0, cfg_turn = 0;
  double cfg_base = 0.0, cfg_good_bonus = 0.0, cfg_pro_delta = 0.0,
         cfg_evil_bonus = 0.0, cfg_good_mult = 0.0;


  h_robbery_get_config (db,
                        &cfg_thresh,
                        &cfg_xp_hold,
                        &cfg_cred_xp,
                        &cfg_base,
                        &cfg_turn,
                        &cfg_good_bonus,
                        &cfg_pro_delta,
                        &cfg_evil_bonus, &cfg_good_mult, NULL);
  int p_align = 0, p_xp = 0;


  db_player_get_alignment (db, ctx->player_id, &p_align);
  {
    static const char *SQL = "SELECT experience FROM players WHERE player_id=$1";
    db_res_t *res = NULL;
    db_error_t dberr;


    memset (&dberr, 0, sizeof (dberr));
    db_bind_t params[1];


    params[0] = db_bind_i64 (ctx->player_id);

    if (db_query (db, SQL, params, 1, &res, &dberr))
      {
        if (db_res_step (res, &dberr))
          {
            p_xp = (int) db_res_col_i64 (res, 0, &dberr);
          }
        db_res_finalize (res);
      }
  }
  int player_align_band_id = 0;
  int can_rob_ports_flag = 0;   // Flag to check if player's alignment allows robbing


  // Get player's alignment band properties
  db_alignment_band_for_value (db,
                               p_align,
                               &player_align_band_id,
                               NULL,
                               NULL, NULL, NULL, NULL, &can_rob_ports_flag);
  if (can_rob_ports_flag == 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_TURN_COST_EXCEEDS,
                                   "You are not allowed to rob ports with your current alignment.",
                                   NULL);
      return 0;
    }
  int cluster_align_band_id = 0;


  h_get_cluster_alignment_band (db, sector_id, &cluster_align_band_id);
  int cluster_is_evil = 0;
  int cluster_is_good = 0;


  db_alignment_band_for_value (db,
                               cluster_align_band_id,
                               NULL,
                               NULL,
                               NULL,
                               &cluster_is_good,
                               &cluster_is_evil, NULL, NULL);
  int is_good_cluster = cluster_is_good;
  int is_evil_cluster = cluster_is_evil;
  int is_good_player = (p_align > cfg_thresh);
  /* 5. Fake Bust Check */
  int fake_bust = 0;
  {
    static const char *SQL =
      "SELECT port_id, last_attempt_at, was_success FROM player_last_rob WHERE player_id=$1";
    db_res_t *res = NULL;
    db_error_t dberr;


    memset (&dberr, 0, sizeof (dberr));
    db_bind_t params[1];


    params[0] = db_bind_i64 (ctx->player_id);

    if (db_query (db, SQL, params, 1, &res, &dberr))
      {
        if (db_res_step (res, &dberr))
          {
            int last_port = (int) db_res_col_i64 (res, 0, &dberr);
            int last_ts = (int) db_res_col_i64 (res, 1, &dberr);
            int success = (int) db_res_col_i64 (res, 2, &dberr);


            if (last_port == port_id && success &&
                (time (NULL) - last_ts < 900))
              {
                fake_bust = 1;
              }
          }
        db_res_finalize (res);
      }
  }


  if (fake_bust)
    {
      /* Execute Fake Bust Writes */
      db_error_t dberr;


      memset (&dberr, 0, sizeof (dberr));
      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &dberr))
        {
          send_response_error (ctx, root, ERR_DB_BUSY, "Database busy.");
          return 0;
        }

      static const char *SQL_BUST =
        "INSERT INTO port_busts (port_id, player_id, last_bust_at, bust_type, active) "
        "VALUES ($1, $2, strftime('%s','now'), 'fake', 1) "
        "ON CONFLICT(port_id, player_id) DO UPDATE SET "
        "last_bust_at=strftime('%s','now'), bust_type='fake', active=1";

      static const char *SQL_LAST =
        "INSERT INTO player_last_rob (player_id, port_id, last_attempt_at, was_success) "
        "VALUES ($1, $2, strftime('%s','now'), 0) "
        "ON CONFLICT(player_id) DO UPDATE SET "
        "port_id=EXCLUDED.port_id, last_attempt_at=strftime('%s','now'), was_success=0";

      db_bind_t params[2];


      params[0] = db_bind_i64 (port_id);
      params[1] = db_bind_i64 (ctx->player_id);

      if (!db_exec (db, SQL_BUST, params, 2, &dberr))
        {
          db_tx_rollback (db, NULL);
          send_response_error (ctx, root, ERR_DB, "Database error.");
          return 0;
        }

      db_bind_t params2[2];


      params2[0] = db_bind_i64 (ctx->player_id);
      params2[1] = db_bind_i64 (port_id);

      if (!db_exec (db, SQL_LAST, params2, 2, &dberr))
        {
          db_tx_rollback (db, NULL);
          send_response_error (ctx, root, ERR_DB, "Database error.");
          return 0;
        }

      if (!db_tx_commit (db, &dberr))
        {
          send_response_error (ctx, root, ERR_DB, "Database error.");
          return 0;
        }

      json_t *fresp = json_object ();


      json_object_set_new (fresp, "rob_result", json_string ("fake_bust"));
      json_object_set_new (fresp, "message",
                           json_string
                           (
                             "The port authorities were waiting for you. You got away, but empty-handed."));

      send_response_ok_take (ctx, root, "port.rob", &fresp);
      return 0;
    }
  /* 6. Real Bust Calculation */
  double p_base = cfg_base;
  double p_player = is_good_player ? cfg_good_bonus : cfg_pro_delta;
  double p_cluster = is_evil_cluster ? cfg_evil_bonus : 0.0;
  double chance = p_base + p_player + p_cluster;


  if (chance < 0.01)
    {
      chance = 0.01;
    }
  if (chance > 0.30)
    {
      chance = 0.30;
    }
  double roll = (double) rand () / (double) RAND_MAX;
  int is_real_bust = (roll < chance);

  /* 7. Execution */
  int we_started_tx = 0;
  db_error_t dberr;


  memset (&dberr, 0, sizeof (dberr));

  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &dberr))
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_BUSY,
                           "Database busy.");
      return 0;
    }
  we_started_tx = 1;

  long long loot_credits = 0;
  json_t *res_data = json_object ();


  if (!is_real_bust)
    {
      /* SUCCESS */
      if (strcmp (mode, "credits") == 0)
        {
          long long max_xp = (long long) p_xp * cfg_cred_xp;
          long long port_cash = 0;

          static const char *SQL_GET_CASH =
            "SELECT petty_cash FROM ports WHERE port_id=$1";
          db_res_t *res = NULL;
          db_bind_t params[1];


          params[0] = db_bind_i64 (port_id);

          if (db_query (db, SQL_GET_CASH, params, 1, &res, &dberr))
            {
              if (db_res_step (res, &dberr))
                {
                  port_cash = db_res_col_i64 (res, 0, &dberr);
                }
              db_res_finalize (res);
            }

          loot_credits = (max_xp < port_cash) ? max_xp : port_cash;
          if (loot_credits > 0)
            {
              static const char *SQL_UPD_PORT =
                "UPDATE ports SET petty_cash = petty_cash - $1 WHERE port_id=$2";
              db_bind_t uparams[2];


              uparams[0] = db_bind_i64 (loot_credits);
              uparams[1] = db_bind_i64 (port_id);

              if (!db_exec (db, SQL_UPD_PORT, uparams, 2, &dberr))
                {
                  goto fail_tx;
                }

              if (h_add_player_petty_cash_unlocked (db,
                                                    ctx->player_id,
                                                    loot_credits,
                                                    NULL) != SQLITE_OK)
                {
                  goto fail_tx;
                }
            }
        }
      else                      // mode == "goods"
        {
          if (!commodity)
            {
              send_response_error (ctx,
                                   root,
                                   400,
                                   "Commodity required for goods mode.");
              goto fail_tx;
            }
          char *canonical_commodity_code = (char *) commodity_to_code (db,
                                                                       commodity);


          if (!canonical_commodity_code)
            {
              send_response_refused_steal (ctx,
                                           root,
                                           ERR_AUTOPILOT_PATH_INVALID,
                                           "Invalid or unsupported commodity.",
                                           NULL);
              goto fail_tx;
            }

          int amount_to_steal = 10;
          int port_stock = 0;


          if (db_port_get_goods_on_hand (db,
                                         port_id,
                                         canonical_commodity_code,
                                         &port_stock) != SQLITE_OK)
            {
              send_response_error (ctx,
                                   root,
                                   500,
                                   "Database error getting port stock.");
              free (canonical_commodity_code);
              goto fail_tx;
            }
          if (port_stock < amount_to_steal)
            {
              send_response_refused_steal (ctx,
                                           root,
                                           ERR_AUTOPILOT_PATH_INVALID,
                                           "Port does not have enough of that commodity.",
                                           NULL);
              free (canonical_commodity_code);
              goto fail_tx;
            }

          int ship_id = h_get_active_ship_id (db, ctx->player_id);


          if (ship_id <= 0)
            {
              send_response_error (ctx, root, 500, "No active ship found.");
              free (canonical_commodity_code);
              goto fail_tx;
            }
          int free_space = 0;


          h_get_cargo_space_free (db, ctx->player_id, &free_space);
          if (free_space < amount_to_steal)
            {
              send_response_refused_steal (ctx,
                                           root,
                                           REF_TURN_COST_EXCEEDS,
                                           "Insufficient cargo space.",
                                           NULL);
              free (canonical_commodity_code);
              goto fail_tx;
            }

          if (h_update_port_stock (db,
                                   port_id,
                                   canonical_commodity_code,
                                   -amount_to_steal,
                                   NULL) != SQLITE_OK)
            {
              send_response_error (ctx, root, 500, "Database error.");
              free (canonical_commodity_code);
              goto fail_tx;
            }
          if (h_update_ship_cargo (db,
                                   ctx->ship_id,
                                   canonical_commodity_code,
                                   amount_to_steal,
                                   NULL) != SQLITE_OK)
            {
              send_response_error (ctx, root, 500, "Database error.");
              free (canonical_commodity_code);
              goto fail_tx;
            }

          json_t *goods_stolen = json_object ();


          json_object_set_new (goods_stolen, "commodity",
                               json_string (canonical_commodity_code));
          json_object_set_new (goods_stolen, "quantity",
                               json_integer (amount_to_steal));
          json_object_set_new (res_data, "goods_stolen", goods_stolen);

          bool stolen_item_is_illegal = h_is_illegal_commodity (db,
                                                                canonical_commodity_code);


          json_object_set_new (res_data,
                               "stolen_item_is_illegal_tmp",
                               json_boolean (stolen_item_is_illegal));
          free (canonical_commodity_code);
        }

      long long xp_gain = 0;
      int align_change_success = 0;


      if (strcmp (mode, "credits") == 0)
        {
          xp_gain = (long long) floor ((double) loot_credits /
                                       g_xp_align.trade_xp_ratio);
          align_change_success = -10;
        }
      else
        {
          xp_gain = (long long) floor (10.0 * 0.5);     // Fixed amount 10 used for goods
          bool stolen_item_is_illegal = false;
          json_t *j_tmp = json_object_get (res_data,
                                           "stolen_item_is_illegal_tmp");


          if (j_tmp && json_is_true (j_tmp))
            {
              stolen_item_is_illegal = true;
            }
          json_object_del (res_data, "stolen_item_is_illegal_tmp");
          align_change_success = stolen_item_is_illegal ? -20 : -10;
        }

      h_player_apply_progress (db,
                               ctx->player_id,
                               xp_gain,
                               align_change_success,
                               "port.rob.success");

      int susp_inc = is_evil_cluster ? 1 : 2;


      if (cluster_id > 0)
        {
          static const char *SQL_SUSP =
            "INSERT INTO cluster_player_status (cluster_id, player_id, suspicion) VALUES ($1, $2, $3) "
            "ON CONFLICT(cluster_id, player_id) DO UPDATE SET suspicion = suspicion + $4";
          db_bind_t params[4];


          params[0] = db_bind_i64 (cluster_id);
          params[1] = db_bind_i64 (ctx->player_id);
          params[2] = db_bind_i64 (susp_inc);
          params[3] = db_bind_i64 (susp_inc);
          if (!db_exec (db, SQL_SUSP, params, 4, &dberr))
            {
              goto fail_tx;
            }
        }

      static const char *SQL_LAST_SUCCESS =
        "INSERT INTO player_last_rob (player_id, port_id, last_attempt_at, was_success) "
        "VALUES ($1, $2, strftime('%s','now'), 1) "
        "ON CONFLICT(player_id) DO UPDATE SET port_id=EXCLUDED.port_id, "
        "last_attempt_at=strftime('%s','now'), was_success=1";
      db_bind_t lparams[2];


      lparams[0] = db_bind_i64 (ctx->player_id);
      lparams[1] = db_bind_i64 (port_id);
      if (!db_exec (db, SQL_LAST_SUCCESS, lparams, 2, &dberr))
        {
          goto fail_tx;
        }

      if (!db_tx_commit (db, &dberr))
        {
          goto fail_tx;
        }
      we_started_tx = 0;

      json_object_set_new (res_data, "rob_result", json_string ("success"));
      json_object_set_new (res_data, "credits_stolen",
                           json_integer (loot_credits));
      send_response_ok_take (ctx, root, "port.rob", &res_data);
    }
  else
    {
      /* REAL BUST */
      long long xp_loss = (long long) (p_xp * 0.05);
      int align_change_bust = 15;


      h_player_apply_progress (db,
                               ctx->player_id,
                               -xp_loss,
                               align_change_bust,
                               "port.rob.bust");

      static const char *SQL_BUST_REAL =
        "INSERT INTO port_busts (port_id, player_id, last_bust_at, bust_type, active) "
        "VALUES ($1, $2, strftime('%s','now'), 'real', 1) "
        "ON CONFLICT(port_id, player_id) DO UPDATE SET "
        "last_bust_at=strftime('%s','now'), bust_type='real', active=1";
      db_bind_t bparams[2];


      bparams[0] = db_bind_i64 (port_id);
      bparams[1] = db_bind_i64 (ctx->player_id);
      if (!db_exec (db, SQL_BUST_REAL, bparams, 2, &dberr))
        {
          goto fail_tx;
        }

      int susp_inc = is_good_cluster ? 10 : 5;


      if (cluster_id > 0)
        {
          static const char *SQL_CLUST_UPD =
            "INSERT INTO cluster_player_status (cluster_id, player_id, suspicion, bust_count, last_bust_at) "
            "VALUES ($1, $2, $3, 1, strftime('%s','now')) "
            "ON CONFLICT(cluster_id, player_id) DO UPDATE SET "
            "suspicion = suspicion + $4, bust_count = bust_count + 1, last_bust_at = strftime('%s','now')";
          db_bind_t cparams[4];


          cparams[0] = db_bind_i64 (cluster_id);
          cparams[1] = db_bind_i64 (ctx->player_id);
          cparams[2] = db_bind_i64 (susp_inc);
          cparams[3] = db_bind_i64 (susp_inc);
          if (!db_exec (db, SQL_CLUST_UPD, cparams, 4, &dberr))
            {
              goto fail_tx;
            }

          static const char *SQL_BAN =
            "UPDATE cluster_player_status SET banned=1 WHERE cluster_id=$1 AND player_id=$2 AND wanted_level >= 3";
          db_bind_t banparams[2];


          banparams[0] = db_bind_i64 (cluster_id);
          banparams[1] = db_bind_i64 (ctx->player_id);
          if (!db_exec (db, SQL_BAN, banparams, 2, &dberr))
            {
              goto fail_tx;
            }
        }

      static const char *SQL_LAST_FAIL =
        "INSERT INTO player_last_rob (player_id, port_id, last_attempt_at, was_success) "
        "VALUES ($1, $2, strftime('%s','now'), 0) "
        "ON CONFLICT(player_id) DO UPDATE SET port_id=EXCLUDED.port_id, "
        "last_attempt_at=strftime('%s','now'), was_success=0";
      db_bind_t lparams[2];


      lparams[0] = db_bind_i64 (ctx->player_id);
      lparams[1] = db_bind_i64 (port_id);
      if (!db_exec (db, SQL_LAST_FAIL, lparams, 2, &dberr))
        {
          goto fail_tx;
        }

      json_t *news_pl = json_object ();


      json_object_set_new (news_pl, "port_id", json_integer (port_id));
      json_object_set_new (news_pl, "sector_id", json_integer (sector_id));

      db_log_engine_event (time (NULL),
                           "port.bust",
                           "player",
                           ctx->player_id,
                           sector_id,
                           news_pl,
                           NULL);
      json_decref (news_pl);

      if (!db_tx_commit (db, &dberr))
        {
          goto fail_tx;
        }
      we_started_tx = 0;

      json_t *bresp = json_object ();


      json_object_set_new (bresp, "rob_result", json_string ("real_bust"));
      json_object_set_new (bresp, "message",
                           json_string (
                             "You were caught! The authorities have flagged you."));
      send_response_ok_take (ctx, root, "port.rob", &bresp);
    }

  if (res_data)
    {
      json_decref (res_data);
    }
  return 0;

fail_tx:
  if (we_started_tx)
    {
      db_tx_rollback (db, NULL);
    }
  if (res_data)
    {
      json_decref (res_data);
    }
  return 0;
}


int
cmd_trade_sell (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");
  int rc = 0;
  char *req_s = NULL;
  char *resp_s = NULL;
  int sector_id = 0;
  const char *key = NULL;
  int port_id = 0;
  int requested_port_id = 0;
  long long total_item_value = 0;       // Value of items sold
  long long total_credits_after_fees = 0;       // Credits player actually receives
  fee_result_t charges = { 0 };
  long long new_balance = 0;
  char tx_group_id[UUID_STR_LEN];
  json_t *jitems = NULL;
  json_t *receipt = NULL;
  json_t *lines = NULL;

  h_generate_hex_uuid (tx_group_id, sizeof (tx_group_id));
  TradeLine *trade_lines = NULL;
  size_t n = 0;
  int we_started_tx = 0;


  if (!ctx || !root)
    {
      return -1;
    }
  if (!db)
    {
      send_response_error (ctx, root, 500, "No database handle.");
      return -1;
    }
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated", NULL);
      LOGD ("cmd_trade_sell: Not authenticated for player_id=%d",
            ctx->player_id);                                                            // ADDED
      return 0;
    }
  /* consume turn (may open a transaction) */
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);


  if (tc != TURN_CONSUME_SUCCESS)
    {
      LOGD (
        "cmd_trade_sell: Turn consumption failed for player_id=%d, result=%d",
        ctx->player_id,
        tc);                                                                                            // ADDED
      return handle_turn_consumption_error (ctx, tc, "trade.sell", root,
                                            NULL);
    }
  LOGD ("cmd_trade_sell: Turn consumed successfully for player_id=%d",
        ctx->player_id);                                                                // ADDED
  /* decloak */
  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);


  h_decloak_ship (db, player_ship_id);
  LOGD ("cmd_trade_sell: Ship decloaked for ship_id=%d", player_ship_id);       // ADDED
  data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_response_error (ctx, root, 400, "Missing data object.");
      LOGD ("cmd_trade_sell: Missing data object for player_id=%d",
            ctx->player_id);                                                            // ADDED
      return -1;
    }
  int account_type = db_get_player_pref_int (db, ctx->player_id,
                                             "trade.default_account",
                                             0);        // Default to petty cash (0)
  json_t *jaccount = json_object_get (data, "account");


  if (json_is_integer (jaccount))
    {
      int requested_account_type = (int) json_integer_value (jaccount);


      if (requested_account_type != 0 && requested_account_type != 1)
        {
          send_response_error (ctx,
                               root,
                               400,
                               "Invalid account type. Must be 0 (petty cash) or 1 (bank).");
          return -1;
        }
      account_type = requested_account_type;    // Override with explicit request
    }
  LOGD ("cmd_trade_sell: Account type for player_id=%d is %d", ctx->player_id,
        account_type);
  sector_id = ctx->sector_id;
  json_t *jsec = json_object_get (data, "sector_id");


  if (json_is_integer (jsec))
    {
      sector_id = (int) json_integer_value (jsec);
    }
  if (sector_id <= 0)
    {
      send_response_error (ctx, root, 400, "Invalid sector_id.");
      LOGD ("cmd_trade_sell: Invalid sector_id=%d for player_id=%d",
            sector_id,
            ctx->player_id);                                                                            // ADDED
      return -1;
    }
  LOGD ("cmd_trade_sell: Resolved sector_id=%d for player_id=%d",
        sector_id,
        ctx->player_id);                                                                        // ADDED
  if (!cluster_can_trade (db, sector_id, ctx->player_id))
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_TURN_COST_EXCEEDS,
                                   "Port refuses to trade: You are banned in this cluster.",
                                   NULL);
      return -1;
    }
  jitems = json_object_get (data, "items");
  if (!json_is_array (jitems) || json_array_size (jitems) == 0)
    {
      send_response_error (ctx, root, 400, "items[] required.");
      LOGD ("cmd_trade_sell: Missing or empty items array for player_id=%d",
            ctx->player_id);                                                                    // ADDED
      return -1;
    }
  n = json_array_size (jitems);
  LOGD ("cmd_trade_sell: Items array present, size=%zu for player_id=%d",
        n,
        ctx->player_id);                                                                        // ADDED
  json_t *jkey = json_object_get (data, "idempotency_key");


  key = json_is_string (jkey) ? json_string_value (jkey) : NULL;
  if (!key || !*key)
    {
      send_response_error (ctx, root, 400, "idempotency_key required.");
      LOGD ("cmd_trade_sell: Missing idempotency_key for player_id=%d",
            ctx->player_id);                                                                    // ADDED
      return -1;
    }
  char actual_key[UUID_STR_LEN];


  if (strcmp (key, "*generate*") == 0)
    {
      h_generate_hex_uuid (actual_key, sizeof (actual_key));
      key = actual_key;
      LOGD ("cmd_trade_sell: Generated idempotency key '%s' for player_id=%d",
            key, ctx->player_id);
    }
  LOGD ("cmd_trade_sell: Idempotency key='%s' for player_id=%d",
        key,
        ctx->player_id);                                                                // ADDED
  /* idempotency fast-path */
  LOGD ("cmd_trade_sell: Checking idempotency fast-path for key='%s'", key);    // ADDED
  {
    static const char *SQL_GET =
      "SELECT request_json, response_json "
      "FROM trade_idempotency "
      "WHERE key = $1 AND player_id = $2 AND sector_id = $3;";

    db_res_t *res = NULL;
    db_error_t dberr;


    memset (&dberr, 0, sizeof (dberr));

    db_bind_t params[3];


    params[0] = db_bind_text (key);
    params[1] = db_bind_i64 ((int64_t) ctx->player_id);
    params[2] = db_bind_i64 ((int64_t) sector_id);

    if (db_query (db, SQL_GET, params, 3, &res, &dberr))
      {
        if (db_res_step (res, &dberr))
          {
            const char *req_s_stored = db_res_col_text (res, 0, &dberr);
            const char *resp_s_stored = db_res_col_text (res, 1, &dberr);

            json_error_t jerr;
            json_t *stored_req =
              req_s_stored ? json_loads (req_s_stored, 0, &jerr) : NULL;

            json_t *incoming_req = json_incref (data);
            int same = (stored_req && json_equal_strict (stored_req,
                                                         incoming_req));


            json_decref (incoming_req);
            if (stored_req)
              {
                json_decref (stored_req);
              }

            if (same)
              {
                LOGD (
                  "cmd_trade_sell: Idempotency fast-path: request matches, replaying response for key='%s'",
                  key);

                json_t *stored_resp =
                  resp_s_stored ? json_loads (resp_s_stored, 0, &jerr) : NULL;


                db_res_finalize (res);

                if (!stored_resp)
                  {
                    send_response_error (ctx,
                                         root,
                                         500,
                                         "Stored response unreadable.");
                    return -1;
                  }

                send_response_ok_take (ctx,
                                       root,
                                       "trade.sell_receipt_v1",
                                       &stored_resp);
                /* send_response_ok_take steals payload */
                LOGD ("cmd_trade_sell: Replayed stored response, returning.");
                goto cleanup;
              }
          }

        db_res_finalize (res);
      }
    else
      {
        /* Non-fatal: idempotency miss or transient error should not break trading */
        LOGD (
          "cmd_trade_sell: Idempotency fast-path lookup failed: %s (code=%d)",
          dberr.message[0] ? dberr.message : "database error",
          dberr.code);
      }
  }
  LOGD (
    "cmd_trade_sell: Idempotency fast-path: no existing record or no match for key='%s'",
    key);                                                                                               // ADDED
  /* resolve port from sector */
  LOGD ("cmd_trade_sell: Resolving port_id for player_id=%d, sector_id=%d",
        ctx->player_id,
        sector_id);                                                                                     // ADDED
  if (requested_port_id > 0)
    {
      static const char *SQL_BY_ID =
        "SELECT port_id FROM ports WHERE port_id = $1 LIMIT 1;";

      db_res_t *res = NULL;
      db_error_t dberr;


      memset (&dberr, 0, sizeof (dberr));

      db_bind_t params[1];


      params[0] = db_bind_i64 ((int64_t) requested_port_id);

      if (!db_query (db, SQL_BY_ID, params, 1, &res, &dberr))
        {
          send_response_error (ctx, root, ERR_DB, "Database error.");
          return -1;
        }

      if (!db_res_step (res, &dberr))
        {
          db_res_finalize (res);
          send_response_refused_steal (ctx, root,
                                       REF_AUTOPILOT_RUNNING,
                                       "No such port_id.", NULL);
          LOGD ("cmd_trade_sell: No such port_id=%d", requested_port_id);
          return -1;
        }

      /* row exists */
      port_id = requested_port_id;
      db_res_finalize (res);
    }
  else
    {
      static const char *SQL_BY_SECTOR =
        "SELECT port_id FROM ports WHERE sector_id = $1 LIMIT 1;";

      db_res_t *res = NULL;
      db_error_t dberr;


      memset (&dberr, 0, sizeof (dberr));

      db_bind_t params[1];


      params[0] = db_bind_i64 ((int64_t) sector_id);

      if (!db_query (db, SQL_BY_SECTOR, params, 1, &res, &dberr))
        {
          send_response_error (ctx, root, ERR_DB, "Database error.");
          return -1;
        }

      if (!db_res_step (res, &dberr))
        {
          db_res_finalize (res);
          send_response_refused_steal (ctx, root,
                                       REF_AUTOPILOT_RUNNING,
                                       "No port in this sector.", NULL);
          LOGD ("cmd_trade_sell: No port in sector_id=%d", sector_id);
          return -1;
        }

      port_id = (int) db_res_col_i64 (res, 0, &dberr);
      db_res_finalize (res);
    }
  LOGD ("cmd_trade_sell: Resolved port_id=%d for player_id=%d",
        port_id,
        ctx->player_id);                                                                        // ADDED
  player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  LOGD ("cmd_trade_sell: Player ship_id=%d for player_id=%d",
        player_ship_id,
        ctx->player_id);                                                                        // ADDED
  h_decloak_ship (db, player_ship_id);
  LOGD ("cmd_trade_sell: Ship decloaked for ship_id=%d", player_ship_id);       // ADDED
  /* pre-load credits & cargo (outside tx is fine; final checks are atomic) */
  long long current_credits = 0;


  if (account_type == 0)
    {                           // Petty cash
      if (h_get_player_petty_cash (db, ctx->player_id, &current_credits) !=
          SQLITE_OK)
        {
          send_response_error (ctx,
                               root,
                               500, "Could not read player petty cash.");
          return -1;
        }
    }
  else
    {                           // Bank account
      long long credits_i = 0;


      if (h_get_credits (db, "player", ctx->player_id, &credits_i) !=
          SQLITE_OK)
        {
          send_response_error (ctx,
                               root,
                               500, "Could not read player bank credits.");
          return -1;
        }
      current_credits = (long long) credits_i;
    }
  LOGD (
    "cmd_trade_sell: Player credits (account_type=%d)=%lld for player_id=%d",
    account_type,
    current_credits,
    ctx->player_id);                                                                                                                    // ADDED
  int cur_ore = 0, cur_org = 0, cur_eq = 0, cur_colonists = 0, cur_slaves = 0,
      cur_weapons = 0, cur_drugs = 0, cur_holds = 0;                                                                            // Initialize to 0


  if (h_get_ship_cargo_and_holds (db,
                                  player_ship_id,
                                  &cur_ore,
                                  &cur_org,
                                  &cur_eq,
                                  &cur_colonists,
                                  &cur_slaves,
                                  &cur_weapons,
                                  &cur_drugs,
                                  &cur_holds) != SQLITE_OK)                                                                                                             // Pass new cargo types
    {
      send_response_error (ctx, root, 500, "Could not read ship cargo.");
      return -1;
    }
  int current_load = cur_ore + cur_org + cur_eq + cur_colonists + cur_slaves +
                     cur_weapons + cur_drugs;                                                           // Update current_load calculation


  LOGD (
    "cmd_trade_sell: Ship cargo: ore=%d, organics=%d, equipment=%d, holds=%d, current_load=%d for ship_id=%d",
    cur_ore,
    cur_org,
    cur_eq,
    cur_holds,
    current_load,
    player_ship_id);                                                                                                                                                                    // ADDED
  n = json_array_size (jitems);
  trade_lines = calloc (n, sizeof (*trade_lines));
  if (!trade_lines)
    {
      send_response_error (ctx, root, 500, "Memory allocation error.");
      return -1;
    }
  LOGD ("cmd_trade_sell: Allocated trade_lines for %zu items", n);      // ADDED
  /* validate each line & compute totals */
  LOGD ("cmd_trade_sell: Starting trade line validation loop"); // ADDED
  for (size_t i = 0; i < n; i++)
    {
      json_t *it = json_array_get (jitems, i);
      const char *raw_commodity =
        json_string_value (json_object_get (it, "commodity"));
      int amount =
        (int) json_integer_value (json_object_get (it, "quantity"));


      LOGD
        ("cmd_trade_sell: Validating item %zu: raw_commodity='%s', amount=%d",
        i, raw_commodity, amount);
      if (!raw_commodity || amount <= 0)
        {
          free_trade_lines (trade_lines, n);
          send_response_error (ctx,
                               root,
                               400,
                               "items[] must contain {commodity, quantity>0}.");
          LOGD
            ("cmd_trade_sell: Invalid item %zu: raw_commodity='%s', amount=%d",
            i, raw_commodity, amount);
          return -1;
        }
      char *canonical_commodity_code =
        (char *) commodity_to_code (db, raw_commodity);


      if (!canonical_commodity_code)
        {
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_AUTOPILOT_PATH_INVALID,
                                       "Invalid or unsupported commodity.",
                                       NULL);
          LOGD ("cmd_trade_sell: Invalid or unsupported commodity '%s'",
                raw_commodity);
          goto cleanup;
        }
      if (!h_port_buys_commodity (db, port_id, canonical_commodity_code))
        {
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_AUTOPILOT_PATH_INVALID,
                                       "Port is not buying this commodity right now.",
                                       NULL);
          LOGD ("cmd_trade_sell: Port %d not buying commodity '%s'", port_id,
                canonical_commodity_code);
          goto cleanup;
        }
      int buy_price = h_entity_calculate_buy_price (db,
                                                    ENTITY_TYPE_PORT,
                                                    port_id,
                                                    canonical_commodity_code);


      if (buy_price <= 0)
        {
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_AUTOPILOT_PATH_INVALID,
                                       "Port is not buying this commodity right now.",
                                       NULL);
          // LOGD("cmd_trade_sell: Port %d buy price <= 0 for commodity '%s'", port_id, canonical_commodity_code); // ADDED
          return 0;
        }
      // LOGD("cmd_trade_sell: Unit price for '%s' at port %d is %d", raw_commodity, port_id, buy_price); // ADDED
      /* check cargo */
      int ore, org, eq, holds, colonists, slaves, weapons, drugs;       // Declare for new cargo types


      if (h_get_ship_cargo_and_holds (db, player_ship_id, &ore, &org, &eq,
                                      &holds, &colonists, &slaves, &weapons,
                                      &drugs) != SQLITE_OK)                                                                             // Pass new cargo types
        {
          free_trade_lines (trade_lines, n);
          send_response_error (ctx, root, 500, "Could not read ship cargo.");
          return -1;
        }

      int have = 0;


      /* Map canonical commodity code to what the player is actually carrying */
      if (strcasecmp (canonical_commodity_code, "ore") == 0)
        {
          have = ore;
        }
      else if (strcasecmp (canonical_commodity_code, "organics") == 0)
        {
          have = org;
        }
      else if (strcasecmp (canonical_commodity_code, "equipment") == 0)
        {
          have = eq;
        }
      else if (strcasecmp (canonical_commodity_code, "colonists") == 0)
        {
          have = colonists;
        }
      /* Illegal / special commodities: check permission first, then map */
      else if (strcasecmp (canonical_commodity_code, "SLV") == 0 ||
               strcasecmp (canonical_commodity_code, "WPN") == 0 ||
               strcasecmp (canonical_commodity_code, "DRG") == 0)
        {
          /* Check illegal trade for sell */
          if (!h_can_trade_commodity (db, port_id, ctx->player_id,
                                      canonical_commodity_code))
            {
              free_trade_lines (trade_lines, n);
              send_response_refused_steal (ctx,
                                           root,
                                           REF_SAFE_ZONE_ONLY,
                                           "Forbidden: Illegal trade not permitted for this player or port.",
                                           NULL);
              return 0;         /* IMPORTANT: stop here */
            }
          if (strcasecmp (canonical_commodity_code, "SLV") == 0)
            {
              have = slaves;
            }
          else if (strcasecmp (canonical_commodity_code, "WPN") == 0)
            {
              have = weapons;
            }
          else                  /* DRG */
            {
              have = drugs;
            }
        }
      else
        {
          /* Unknown / unsupported commodity code */
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_SECTOR_NOT_FOUND,
                                       "Unknown commodity code.", NULL);
          return 0;
        }
      if (have < amount)
        {
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx,
                                       root,
                                       REF_NO_WARP_LINK,
                                       "You do not carry enough of that commodity.",
                                       NULL);
          return 0;
        }
      long long line_credits = (long long) amount * buy_price;


      trade_lines[i].commodity = canonical_commodity_code;
      trade_lines[i].amount = amount;
      trade_lines[i].unit_price = buy_price;
      trade_lines[i].line_cost = line_credits;
      total_item_value += line_credits;
    }
  LOGD ("cmd_trade_sell: Finished trade line validation. Total item value=%lld",
        total_item_value);                                                                              // ADDED
  rc =
    calculate_fees (db, TX_TYPE_TRADE_SELL, total_item_value, "player",
                    &charges);
  total_credits_after_fees = total_item_value - charges.fee_total;
  if (total_credits_after_fees < 0)
    {
      free_trade_lines (trade_lines, n);
      send_response_refused_steal (ctx,
                                   root,
                                   REF_NO_WARP_LINK,
                                   "Selling this would result in negative credits after fees.",
                                   NULL);
      return 0;
    }
  /* START TRANSACTION */
  {
    db_error_t dberr;


    memset (&dberr, 0, sizeof (dberr));

    if (!db_tx_begin (db, DB_TX_IMMEDIATE, &dberr))
      {
        /* Preserve existing user-facing behaviour */
        send_response_error (ctx, root,
                             (dberr.code ? dberr.code : ERR_DB_BUSY),
                             "Database busy (tx).");
        goto cleanup;
      }
    we_started_tx = 1;
  }
  receipt = json_object ();
  lines = json_array ();
  if (!receipt || !lines)
    {
      rc = 500;
      goto fail_tx;
    }
  json_object_set_new (receipt, "sector_id", json_integer (sector_id));
  json_object_set_new (receipt, "port_id", json_integer (port_id));
  json_object_set_new (receipt, "player_id", json_integer (ctx->player_id));
  json_object_set_new (receipt, "lines", lines);
  /* iterate items */
  for (size_t i = 0; i < n; i++)
    {
      const char *commodity = trade_lines[i].commodity;
      int amount = trade_lines[i].amount;
      long long line_credits = trade_lines[i].line_cost;
      int buy_price = trade_lines[i].unit_price;
      sqlite3_stmt *st = NULL;
      /* log row */
      {
        static const char *LOG_SQL =
          "INSERT INTO trade_log "
          "(player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) "
          "VALUES ($1, $2, $3, $4, $5, $6, 'sell', $7);";

        db_bind_t log_params[] = {
          db_bind_i32 (ctx->player_id),
          db_bind_i32 (port_id),
          db_bind_i32 (sector_id),
          db_bind_text (commodity),
          db_bind_i32 (amount),
          db_bind_i32 (buy_price),
          db_bind_i64 ((long long)time (NULL))
        };

        db_res_t *log_res = NULL;
        db_error_t log_err;


        db_error_clear (&log_err);

        if (!db_exec (db, LOG_SQL, log_params,
                      sizeof(log_params) / sizeof(log_params[0]), &log_err))
          {
            goto sql_err;
          }
      }
      /* update ship cargo (−amount) */
      {
        int new_ship_qty = 0;


        rc =
          h_update_ship_cargo (db, ctx->ship_id, commodity,
                               -amount, &new_ship_qty);
        if (rc != SQLITE_OK)
          {
            if (rc == SQLITE_CONSTRAINT)
              {
                send_response_refused_steal (ctx,
                                             root,
                                             REF_NO_WARP_LINK,
                                             "You do not carry enough of that commodity (atomic check).",
                                             NULL);
              }
            goto fail_tx;
          }
        LOGD ("cmd_trade_sell: Ship cargo updated. new_qty=%d", new_ship_qty);
      }
      /* update port stock (+amount) */
      {
        int new_port_qty = 0;


        rc =
          h_update_port_stock (db, port_id, commodity, amount, &new_port_qty);
        if (rc != SQLITE_OK)
          {
            if (rc == SQLITE_CONSTRAINT)
              {
                send_response_refused_steal (ctx,
                                             root,
                                             REF_TURN_COST_EXCEEDS,
                                             "Port cannot accept that much cargo (atomic check).",
                                             NULL);
              }
            goto fail_tx;
          }
      }
      json_t *jline = json_object ();


      json_object_set_new (jline, "commodity", json_string (commodity));
      json_object_set_new (jline, "quantity", json_integer (amount));
      json_object_set_new (jline, "unit_price", json_integer (buy_price));
      json_object_set_new (jline, "value", json_integer (line_credits));
      json_array_append_new (lines, jline);
    }
  /* credit player (atomic helper) */
  {
    if (account_type == 0)
      {                         // Petty cash
        if (we_started_tx)
          {
            rc =
              h_add_player_petty_cash_unlocked (db, ctx->player_id,
                                                total_credits_after_fees,
                                                &new_balance);
          }
        else
          {
            rc =
              h_add_player_petty_cash (db, ctx->player_id,
                                       total_credits_after_fees,
                                       &new_balance);
          }
      }
    else
      {                         // Bank account
        int player_bank_account_id = -1;
        int get_account_rc =
          h_get_account_id_unlocked (db, "player", ctx->player_id,
                                     &player_bank_account_id);


        if (get_account_rc != SQLITE_OK)
          {
            LOGE
            (
              "cmd_trade_sell: Failed to get player bank account ID for player %d (rc=%d)",
              ctx->player_id,
              get_account_rc);
            rc = get_account_rc;
          }
        else
          {
            rc =
              h_add_credits_unlocked (db, player_bank_account_id,
                                      total_credits_after_fees, "TRADE_SELL",
                                      tx_group_id, &new_balance);
          }
      }
    if (rc != SQLITE_OK)
      {
        send_response_error (ctx, root, 500, "Failed to credit player.");
        goto fail_tx;
      }
    json_object_set_new (receipt, "credits_remaining",
                         json_integer (new_balance));
  }
  LOGD ("cmd_trade_sell: Player credits credited. New balance=%lld",
        json_integer_value (json_object_get (receipt, "credits_remaining")));
  // Deduct total_item_value from port's bank account
  {
    long long new_port_balance = 0;
    int port_bank_account_id = -1;
    int get_account_rc =
      h_get_account_id_unlocked (db, "port", port_id, &port_bank_account_id);


    if (get_account_rc != SQLITE_OK)
      {
        LOGE
        (
          "cmd_trade_sell: Failed to get port bank account ID for port %d (rc=%d)",
          port_id,
          get_account_rc);
        rc = get_account_rc;
        goto fail_tx;
      }
    else
      {
        rc =
          h_deduct_credits_unlocked (db, port_bank_account_id,
                                     total_item_value, "TRADE_SELL",
                                     tx_group_id, &new_port_balance);
      }
    if (rc != SQLITE_OK)
      {
        LOGE ("cmd_trade_sell: Failed to deduct credits from port %d: %s",
              port_id, "database error");
        goto fail_tx;
      }
    LOGD
      ("cmd_trade_sell: Deducted %lld credits from port %d. New balance=%lld",
      total_item_value, port_id, new_port_balance);
  }
  // Add fees to system bank account
  if (charges.fee_to_bank > 0)
    {
      long long new_system_balance = 0;
      int system_bank_account_id = -1;
      int get_account_rc = h_get_system_account_id_unlocked (db,
                                                             "SYSTEM",
                                                             0,
                                                             &
                                                             system_bank_account_id);


      if (get_account_rc != SQLITE_OK)
        {
          LOGE
            ("cmd_trade_sell: Failed to get system bank account ID (rc=%d)",
            get_account_rc);
          rc = get_account_rc;
          goto fail_tx;
        }
      else
        {
          rc =
            h_add_credits_unlocked (db, system_bank_account_id,
                                    charges.fee_to_bank, "TRADE_SELL_FEE",
                                    tx_group_id, &new_system_balance);
        }
      if (rc != SQLITE_OK)
        {
          LOGE ("cmd_trade_sell: Failed to add fees to system bank: %s",
                "database error");
          goto fail_tx;
        }
      LOGD
        ("cmd_trade_sell: Added %lld fees to system bank. New balance=%lld",
        charges.fee_to_bank, new_system_balance);
    }
  json_object_set_new (receipt, "total_item_value",
                       json_integer (total_item_value));
  json_object_set_new (receipt, "total_credits_after_fees",
                       json_integer (total_credits_after_fees));
  json_object_set_new (receipt, "fees", json_integer (charges.fee_to_bank));
  /* idempotency insert */
  LOGD ("cmd_trade_sell: Attempting idempotency insert for key='%s'", key);
  {
    req_s = json_dumps (data, JSON_COMPACT | JSON_SORT_KEYS);
    resp_s = json_dumps (receipt, JSON_COMPACT | JSON_SORT_KEYS);

    static const char *SQL_PUT =
      "INSERT INTO trade_idempotency "
      "(key, player_id, sector_id, request_json, response_json, created_at) "
      "VALUES ($1, $2, $3, $4, $5, $6);";

    db_error_t dberr;


    memset (&dberr, 0, sizeof (dberr));

    db_bind_t params[6];


    params[0] = db_bind_text (key);
    params[1] = db_bind_i64 ((int64_t) ctx->player_id);
    params[2] = db_bind_i64 ((int64_t) sector_id);
    params[3] = req_s  ? db_bind_text (req_s)  : db_bind_null ();
    params[4] = resp_s ? db_bind_text (resp_s) : db_bind_null ();
    params[5] = db_bind_i64 ((int64_t) time (NULL));

    if (!db_exec (db, SQL_PUT, params, 6, &dberr))
      {
        LOGD (
          "cmd_trade_sell: Idempotency insert failed, going to idempotency_race. code=%d msg=%s",
          dberr.code,
          dberr.message[0] ? dberr.message : "database error");

        /* On insert failure, rollback the gameplay transaction we started */
        if (we_started_tx)
          {
            db_error_t rberr;


            memset (&rberr, 0, sizeof (rberr));
            (void) db_tx_rollback (db, &rberr);
            we_started_tx = 0;
          }

        goto idempotency_race;
      }
  }
  LOGD ("cmd_trade_sell: Idempotency insert successful for key='%s'", key);
  if (we_started_tx)
    {
      db_error_t cberr;


      memset (&cberr, 0, sizeof (cberr));

      if (!db_tx_commit (db, &cberr))
        {
          send_response_error (ctx, root, ERR_DB, "Database error.");
          goto cleanup;
        }
      we_started_tx = 0;
    }
  send_response_ok_take (ctx, root, "trade.sell_receipt_v1", &receipt);
  receipt = NULL;
  goto cleanup;
sql_err:
  send_response_error (ctx, root, ERR_SERVER_ERROR, "database error");
fail_tx:
  if (we_started_tx)
    {
      db_error_t rberr;


      memset (&rberr, 0, sizeof (rberr));
      (void) db_tx_rollback (db, &rberr);
      we_started_tx = 0;
      LOGD ("cmd_trade_sell: Rolled back transaction.");
    }
  LOGD ("cmd_trade_sell: Going to cleanup from fail_tx.");
  goto cleanup;
idempotency_race:
  LOGD ("cmd_trade_sell: Entered idempotency_race block for key='%s'", key);
  /* transaction already rolled back if we started it; resolve via stored row */
  {
    static const char *SQL_GET2 =
      "SELECT request_json, response_json "
      "FROM trade_idempotency "
      "WHERE key = $1 AND player_id = $2 AND sector_id = $3;";

    db_res_t *res = NULL;
    db_error_t dberr;


    memset (&dberr, 0, sizeof (dberr));

    db_bind_t params[3];


    params[0] = db_bind_text (key);
    params[1] = db_bind_i64 ((int64_t) ctx->player_id);
    params[2] = db_bind_i64 ((int64_t) sector_id);

    if (db_query (db, SQL_GET2, params, 3, &res, &dberr))
      {
        if (db_res_step (res, &dberr))
          {
            LOGD ("cmd_trade_sell: Idempotency_race: Found existing record.");

            const char *req_s_stored = db_res_col_text (res, 0, &dberr);
            const char *resp_s_stored = db_res_col_text (res, 1, &dberr);

            json_error_t jerr;
            json_t *stored_req =
              req_s_stored ? json_loads (req_s_stored, 0, &jerr) : NULL;

            int same = (stored_req && json_equal_strict (stored_req, data));


            if (stored_req)
              {
                json_decref (stored_req);
              }

            if (same)
              {
                LOGD (
                  "cmd_trade_sell: Idempotency_race: Request matches, replaying response.");

                json_t *stored_resp =
                  resp_s_stored ? json_loads (resp_s_stored, 0, &jerr) : NULL;


                db_res_finalize (res);

                if (!stored_resp)
                  {
                    send_response_error (ctx,
                                         root,
                                         500,
                                         "Stored response unreadable.");
                    goto cleanup;
                  }

                send_response_ok_take (ctx,
                                       root,
                                       "trade.sell_receipt_v1",
                                       &stored_resp);
                /* send_response_ok_take steals payload */
                goto cleanup;
              }
          }

        db_res_finalize (res);
      }
  }
  /* If we reach here, race not resolved */
  send_response_error (ctx, root, 500, "Could not resolve idempotency race.");
  goto cleanup;
cleanup:
  if (trade_lines)
    {
      free_trade_lines (trade_lines, n);        // Ensure freeing here
    }
  if (receipt)
    {
      json_decref (receipt);
    }
  if (req_s)
    {
      free (req_s);
    }
  if (resp_s)
    {
      free (resp_s);
    }
  return 0;
}


int
cmd_trade_buy (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  LOGD ("cmd_trade_buy: entered for player_id=%d", ctx->player_id);
  json_t *receipt = NULL;
  json_t *lines = NULL;
  json_t *data = NULL;
  json_t *jitems = NULL;
  int total_cargo_space_needed = 0;
  int rc = 0;
  char *req_s = NULL;
  char *resp_s = NULL;
  int sector_id = 0;
  const char *key = NULL;
  int port_id = 0;
  int requested_port_id = 0;
  long long total_item_cost = 0;
  long long total_cost_with_fees = 0;
  fee_result_t charges = { 0 };
  long long new_balance = 0;
  char tx_group_id[UUID_STR_LEN];


  h_generate_hex_uuid (tx_group_id, sizeof (tx_group_id));

  TradeLine *trade_lines = NULL;
  size_t n = 0;
  int we_started_tx = 0;

  db_error_t dberr;


  db_error_clear (&dberr);

  if (!ctx || !root)
    {
      return -1;
    }

  if (!db)
    {
      send_response_error (ctx, root, 500, "No database handle.");
      return -1;
    }

  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated", NULL);
      LOGD ("cmd_trade_buy: Not authenticated for player_id=%d",
            ctx->player_id);
      return 0;
    }

  /* consume turn (may open a transaction) */
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);


  if (tc != TURN_CONSUME_SUCCESS)
    {
      LOGD (
        "cmd_trade_buy: Turn consumption failed for player_id=%d, result=%d",
        ctx->player_id,
        tc);
      return handle_turn_consumption_error (ctx, tc, "trade.buy", root, NULL);
    }
  LOGD ("cmd_trade_buy: Turn consumed successfully for player_id=%d",
        ctx->player_id);

  /* input */
  data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_response_error (ctx, root, 400, "Missing data object.");
      LOGD ("cmd_trade_buy: Missing data object for player_id=%d",
            ctx->player_id);
      return -1;
    }

  int account_type = db_get_player_pref_int (db, ctx->player_id,
                                             "trade.default_account",
                                             0);

  json_t *jaccount = json_object_get (data, "account");


  if (json_is_integer (jaccount))
    {
      int requested_account_type = (int) json_integer_value (jaccount);


      if (requested_account_type != 0 && requested_account_type != 1)
        {
          send_response_error (ctx,
                               root,
                               400,
                               "Invalid account type. Must be 0 (petty_cash) or 1 (bank).");
          return 0;
        }
      account_type = requested_account_type;
    }

  /* idempotency key */
  {
    json_t *jid = json_object_get (root, "idempotency_key");


    if (json_is_string (jid))
      {
        key = json_string_value (jid);
      }
  }

  /* sector + port resolution */
  sector_id = ctx->sector_id;

  json_t *jport = json_object_get (data, "port_id");


  if (json_is_integer (jport))
    {
      requested_port_id = (int) json_integer_value (jport);
    }

  /* items */
  jitems = json_object_get (data, "items");
  if (!json_is_array (jitems))
    {
      send_response_error (ctx, root, 400, "Missing items array.");
      return 0;
    }

  /* --- idempotency replay path (read-through) --- */
  if (key && *key)
    {
      static const char *SQL_GET =
        "SELECT request_json, response_json "
        "FROM trade_idempotency "
        "WHERE key = $1 AND player_id = $2 AND sector_id = $3;";

      db_res_t *res = NULL;


      db_error_clear (&dberr);

      db_bind_t binds[] = {
        db_bind_text (key),
        db_bind_i32 (ctx->player_id),
        db_bind_i32 (sector_id)
      };


      if (!db_query (db,
                     SQL_GET,
                     binds,
                     sizeof (binds) / sizeof (binds[0]),
                     &res,
                     &dberr))
        {
          /* Non-fatal: treat as cache miss; continue with normal execution */
          LOGE ("cmd_trade_buy: Idempotency lookup failed: %s",
                dberr.message[0] ? dberr.message : "database error");
        }
      else
        {
          db_error_t step_err;


          db_error_clear (&step_err);

          if (db_res_step (res, &step_err))
            {
              const char *req_s_stored = db_res_col_text (res, 0, &step_err);
              const char *resp_s_stored = db_res_col_text (res, 1, &step_err);

              json_error_t jerr;
              json_t *stored_req =
                req_s_stored ? json_loads (req_s_stored, 0, &jerr) : NULL;

              int same = (stored_req && json_equal_strict (stored_req, data));


              if (stored_req)
                {
                  json_decref (stored_req);
                }

              if (same)
                {
                  json_t *stored_resp =
                    resp_s_stored ? json_loads (resp_s_stored, 0, &jerr) : NULL;


                  db_res_finalize (res);

                  if (!stored_resp)
                    {
                      send_response_error (ctx,
                                           root,
                                           500,
                                           "Stored response unreadable.");
                      goto cleanup;
                    }

                  send_response_ok_take (ctx,
                                         root,
                                         "trade.buy_receipt_v1", &stored_resp);
                  goto cleanup;
                }
            }
          db_res_finalize (res);
        }
    }

  /* resolve port_id (if requested_port_id provided) */
  if (requested_port_id > 0)
    {
      static const char *SQL_BY_ID =
        "SELECT port_id FROM ports WHERE port_id = $1 LIMIT 1;";
      db_res_t *res = NULL;


      db_error_clear (&dberr);

      db_bind_t binds[] = {
        db_bind_i32 (requested_port_id)
      };


      if (!db_query (db, SQL_BY_ID, binds, 1, &res, &dberr))
        {
          send_response_error (ctx,
                               root,
                               ERR_SERVER_ERROR,
                               dberr.message[0] ? dberr.message :
                               "Database error");
          goto cleanup;
        }

      db_error_t step_err;


      db_error_clear (&step_err);

      if (db_res_step (res, &step_err))
        {
          port_id = (int) db_res_col_i64 (res, 0, &step_err);
        }

      db_res_finalize (res);

      if (port_id <= 0)
        {
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_PORT_NOT_FOUND,
                                       "Port not found.",
                                       NULL);
          goto cleanup;
        }
    }
  else
    {
      static const char *SQL_BY_SECTOR =
        "SELECT port_id FROM ports WHERE sector_id = $1 LIMIT 1;";
      db_res_t *res = NULL;


      db_error_clear (&dberr);

      db_bind_t binds[] = {
        db_bind_i32 (sector_id)
      };


      if (!db_query (db, SQL_BY_SECTOR, binds, 1, &res, &dberr))
        {
          send_response_error (ctx,
                               root,
                               ERR_SERVER_ERROR,
                               dberr.message[0] ? dberr.message :
                               "Database error");
          goto cleanup;
        }

      db_error_t step_err;


      db_error_clear (&step_err);

      if (db_res_step (res, &step_err))
        {
          port_id = (int) db_res_col_i64 (res, 0, &step_err);
        }

      db_res_finalize (res);

      if (port_id <= 0)
        {
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_PORT_NOT_FOUND,
                                       "No port in this sector.",
                                       NULL);
          goto cleanup;
        }
    }

  /* Parse trade lines */
  rc = parse_trade_lines (jitems, &trade_lines, &n);
  if (rc != 0)
    {
      send_response_error (ctx, root, 400, "Invalid items.");
      goto cleanup;
    }

  /* Validate cargo capacity before we do anything expensive */
  total_cargo_space_needed = 0;
  for (size_t i = 0; i < n; i++)
    {
      total_cargo_space_needed += trade_lines[i].quantity;
    }

  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (player_ship_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NO_ACTIVE_SHIP,
                                   "No active ship found.",
                                   NULL);
      goto cleanup;
    }

  int cur_ore = 0, cur_org = 0, cur_eq = 0, cur_colonists = 0, cur_slaves = 0,
      cur_weapons = 0, cur_drugs = 0;
  int cur_holds = 0;


  if (h_get_ship_cargo_and_holds (db,
                                  player_ship_id,
                                  &cur_ore,
                                  &cur_org,
                                  &cur_eq,
                                  &cur_colonists,
                                  &cur_slaves,
                                  &cur_weapons,
                                  &cur_drugs,
                                  &cur_holds) != 0)
    {
      send_response_error (ctx, root, ERR_DB, "Failed to read ship cargo.");
      goto cleanup;
    }

  int used_holds = cur_ore + cur_org + cur_eq + cur_colonists + cur_slaves +
                   cur_weapons + cur_drugs;
  int free_holds = cur_holds - used_holds;


  if (total_cargo_space_needed > free_holds)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_NOT_ENOUGH_HOLDS,
                                   "Not enough free cargo holds.",
                                   NULL);
      goto cleanup;
    }

  /* Validate each line for tradability, price, and stock */
  total_item_cost = 0;
  for (size_t i = 0; i < n; i++)
    {
      const char *commodity = trade_lines[i].commodity;
      int qty = trade_lines[i].quantity;


      if (!commodity || !*commodity || qty <= 0)
        {
          send_response_error (ctx, root, 400, "Invalid trade line.");
          goto cleanup;
        }

      /* Port must sell this commodity */
      if (!h_port_sells_commodity (db, port_id, commodity))
        {
          send_response_refused_steal (ctx,
                                       root,
                                       REF_PORT_OUT_OF_STOCK,
                                       "Port does not sell this commodity.",
                                       NULL);
          goto cleanup;
        }

      /* Enforce illegal goods visibility/rules */
      if (!h_can_trade_commodity (db, port_id, ctx->player_id, commodity))
        {
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_ALIGNMENT_RESTRICTED,
                                       "You cannot trade this commodity here.",
                                       NULL);
          goto cleanup;
        }

      int unit_price = h_entity_calculate_sell_price (db,
                                                      ENTITY_TYPE_PORT,
                                                      port_id,
                                                      commodity);


      if (unit_price <= 0)
        {
          send_response_refused_steal (ctx,
                                       root,
                                       REF_PORT_OUT_OF_STOCK,
                                       "Commodity not for sale.",
                                       NULL);
          goto cleanup;
        }

      long long line_cost = (long long) unit_price * (long long) qty;


      total_item_cost += line_cost;
    }

  /* fees */
  /* charges = (fee_result_t) h_calculate_fees (ctx->player_id, "trade.buy", total_item_cost); */
  /* total_cost_with_fees = total_item_cost + charges.total_fees; */

  rc = calculate_fees (db,
                       TX_TYPE_TRADE_BUY,
                       total_item_cost,
                       "player",
                       &charges);
  total_cost_with_fees = total_item_cost + charges.fee_total;


  /* Ensure balance is sufficient */
  int get_account_rc = 0;


  if (account_type == 0)
    {
      get_account_rc = h_get_player_petty_cash (db, ctx->player_id,
                                                &new_balance);
    }
  else
    {
      get_account_rc = db_get_player_bank_balance (db,
                                                   ctx->player_id,
                                                   &new_balance);
    }

  if (get_account_rc != 0)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Could not read account balance.");
      goto cleanup;
    }

  if (new_balance < total_cost_with_fees)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient funds.",
                                   NULL);
      goto cleanup;
    }

  /* Build receipt JSON now (used both for response + idempotency storage) */
  receipt = json_object ();
  lines = json_array ();
  if (!receipt || !lines)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Out of memory.");
      goto cleanup;
    }

  json_object_set_new (receipt, "port_id", json_integer (port_id));
  json_object_set_new (receipt, "sector_id", json_integer (sector_id));
  json_object_set_new (receipt,
                       "total_item_cost",
                       json_integer (total_item_cost));
  json_object_set_new (receipt, "total_fees",
                       json_integer (charges.total_fees));
  json_object_set_new (receipt,
                       "total_cost",
                       json_integer (total_cost_with_fees));
  json_object_set_new (receipt, "tx_group_id", json_string (tx_group_id));
  json_object_set_new (receipt, "lines", lines);

  /* ----------------- TRANSACTION ----------------- */
  db_error_clear (&dberr);
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &dberr))
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           dberr.message[0] ? dberr.message :
                           "Could not begin transaction.");
      goto cleanup;
    }
  we_started_tx = 1;

  /* 1) Post each line: update port stock + ship cargo + trade log */
  for (size_t i = 0; i < n; i++)
    {
      const char *commodity = trade_lines[i].commodity;
      int qty = trade_lines[i].quantity;

      int unit_price = h_entity_calculate_sell_price (db,
                                                      ENTITY_TYPE_PORT,
                                                      port_id,
                                                      commodity);
      int amount = qty;


      /* stock move: port sells -> subtract stock */
      rc = h_market_move_port_stock (db,
                                     port_id,
                                     commodity,
                                     -amount);
      if (rc != 0)
        {
          send_response_error (ctx, root, ERR_DB,
                               "Failed to update port stock.");
          goto fail_tx;
        }

      int new_qty = 0;


      /* ship cargo update */
      rc = h_update_ship_cargo (db,
                                player_ship_id,
                                commodity,
                                amount,
                                &new_qty);
      if (rc != 0)
        {
          LOGE (
            "cmd_trade_buy: h_update_ship_cargo failed for item %zu with rc=%d",
            i,
            rc);
          send_response_error (ctx, root, ERR_DB,
                               "Failed to update ship cargo.");
          goto fail_tx;
        }

      /* trade log */
      {
        static const char *LOG_SQL =
          "INSERT INTO trade_log (player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) "
          "VALUES ($1,$2,$3,$4,$5,$6,'buy',$7);";

        db_error_t e;


        db_error_clear (&e);

        db_bind_t binds[] = {
          db_bind_i32 (ctx->player_id),
          db_bind_i32 (port_id),
          db_bind_i32 (sector_id),
          db_bind_text (commodity),
          db_bind_i32 (amount),
          db_bind_i32 (unit_price),
          db_bind_i64 ((int64_t) time (NULL))
        };


        if (!db_exec (db, LOG_SQL, binds, sizeof (binds) / sizeof (binds[0]),
                      &e))
          {
            /* keep existing behaviour: treat constraint as special (idempotency/race), else error */
            if (e.code == ERR_DB_CONSTRAINT)
              {
                LOGW (
                  "cmd_trade_buy: trade_log constraint hit; proceeding (likely duplicate).");
              }
            else
              {
                LOGE ("cmd_trade_buy: trade_log insert failed: %s", e.message);
                send_response_error (ctx,
                                     root,
                                     ERR_DB,
                                     "Failed to write trade log.");
                goto fail_tx;
              }
          }
      }

      /* receipt line */
      json_t *line = json_object ();


      json_object_set_new (line, "commodity", json_string (commodity));
      json_object_set_new (line, "units", json_integer (amount));
      json_object_set_new (line, "price_per_unit", json_integer (unit_price));
      json_array_append_new (lines, line);
    }

  /* 2) Deduct funds + fees */
  {
    int debit_rc = 0;


    if (account_type == 0)
      {
        debit_rc = h_player_petty_cash_add (db,
                                            ctx->player_id,
                                            -(long long) total_cost_with_fees,
                                            &new_balance);
      }
    else
      {
        debit_rc = h_player_bank_balance_add (db,
                                              ctx->player_id,
                                              -(long long) total_cost_with_fees,
                                              &new_balance);
      }

    if (debit_rc != 0)
      {
        send_response_error (ctx, root, ERR_DB, "Failed to apply payment.");
        goto fail_tx;
      }
  }

  /* 3) Optional alignment hit if buying illegal */
  {
    int any_illegal = 0;


    for (size_t i = 0; i < n; i++)
      {
        if (h_is_illegal_commodity (db, trade_lines[i].commodity))
          {
            any_illegal = 1;
            break;
          }
      }

    if (any_illegal)
      {
        static const char *SQL_ALIGN =
          "UPDATE players SET alignment = alignment - 10 WHERE player_id = $1;";

        db_error_t e;


        db_error_clear (&e);

        db_bind_t binds[] = {
          db_bind_i32 (ctx->player_id)
        };


        if (!db_exec (db, SQL_ALIGN, binds, 1, &e))
          {
            LOGE ("cmd_trade_buy: alignment update failed: %s", e.message);
            send_response_error (ctx,
                                 root,
                                 ERR_DB,
                                 "Failed to update alignment.");
            goto fail_tx;
          }
      }
  }

  /* 4) Store idempotency record (request/response JSON) */
  if (key && *key)
    {
      static const char *SQL_PUT =
        "INSERT INTO trade_idempotency (key, player_id, sector_id, request_json, response_json, created_at) "
        "VALUES ($1,$2,$3,$4,$5,$6);";


      req_s = json_dumps (data, JSON_SORT_KEYS);
      resp_s = json_dumps (receipt, JSON_SORT_KEYS);

      if (!req_s || !resp_s)
        {
          send_response_error (ctx, root, ERR_SERVER_ERROR, "Out of memory.");
          goto fail_tx;
        }

      db_error_t e;


      db_error_clear (&e);

      db_bind_t binds[] = {
        db_bind_text (key),
        db_bind_i32 (ctx->player_id),
        db_bind_i32 (sector_id),
        db_bind_text (req_s),
        db_bind_text (resp_s),
        db_bind_i64 ((int64_t) time (NULL))
      };


      if (!db_exec (db, SQL_PUT, binds, sizeof (binds) / sizeof (binds[0]), &e))
        {
          if (e.code == ERR_DB_CONSTRAINT)
            {
              /* Idempotency race: someone else inserted. Resolve by read/compare. */
              LOGD (
                "cmd_trade_buy: Idempotency constraint hit; attempting race resolution.");

              static const char *SQL_GET2 =
                "SELECT request_json, response_json "
                "FROM trade_idempotency "
                "WHERE key = $1 AND player_id = $2 AND sector_id = $3;";

              db_res_t *res = NULL;
              db_error_t qerr;


              db_error_clear (&qerr);

              db_bind_t qbinds[] = {
                db_bind_text (key),
                db_bind_i32 (ctx->player_id),
                db_bind_i32 (sector_id)
              };


              if (db_query (db, SQL_GET2, qbinds,
                            sizeof (qbinds) / sizeof (qbinds[0]), &res, &qerr))
                {
                  db_error_t step_err;


                  db_error_clear (&step_err);

                  if (db_res_step (res, &step_err))
                    {
                      const char *req_s_stored = db_res_col_text (res,
                                                                  0,
                                                                  &step_err);
                      const char *resp_s_stored = db_res_col_text (res,
                                                                   1,
                                                                   &step_err);

                      json_error_t jerr;
                      json_t *stored_req =
                        req_s_stored ? json_loads (req_s_stored, 0,
                                                   &jerr) : NULL;

                      int same = (stored_req && json_equal_strict (stored_req,
                                                                   data));


                      if (stored_req)
                        {
                          json_decref (stored_req);
                        }

                      if (same)
                        {
                          json_t *stored_resp =
                            resp_s_stored ? json_loads (resp_s_stored, 0,
                                                        &jerr) : NULL;


                          db_res_finalize (res);

                          if (!stored_resp)
                            {
                              send_response_error (ctx,
                                                   root,
                                                   500,
                                                   "Stored response unreadable.");
                              goto fail_tx;
                            }

                          /* Roll back our tx and replay stored response */
                          goto replay_after_race;
                        }
                    }

                  db_res_finalize (res);
                }

              send_response_error (ctx,
                                   root,
                                   500,
                                   "Could not resolve idempotency race.");
              goto fail_tx;
            }
          else
            {
              LOGE ("cmd_trade_buy: idempotency insert failed: %s", e.message);
              send_response_error (ctx,
                                   root,
                                   ERR_SERVER_ERROR,
                                   e.message[0] ? e.message :
                                   "Database error.");
              goto fail_tx;
            }
        }
    }

  /* Commit */
  db_error_clear (&dberr);
  if (we_started_tx && !db_tx_commit (db, &dberr))
    {
      LOGE ("cmd_trade_buy: commit failed: %s",
            dberr.message[0] ? dberr.message : "database error");
      db_error_t rb_err;


      db_error_clear (&rb_err);
      db_tx_rollback (db, &rb_err);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Could not commit transaction.");
      goto cleanup;
    }
  we_started_tx = 0;

  /* success response */
  send_response_ok_take (ctx, root, "trade.buy_receipt_v1", &receipt);
  receipt = NULL;
  goto cleanup;

replay_after_race:
  /* We inserted/updated state, but idempotency says someone else won; rollback and replay stored response. */
  if (we_started_tx)
    {
      db_error_t rb_err;


      db_error_clear (&rb_err);
      db_tx_rollback (db, &rb_err);
      we_started_tx = 0;
    }

  /* Re-fetch stored response and return it (same request). */
  {
    static const char *SQL_GET3 =
      "SELECT response_json FROM trade_idempotency "
      "WHERE key = $1 AND player_id = $2 AND sector_id = $3;";

    db_res_t *res = NULL;
    db_error_t qerr;


    db_error_clear (&qerr);

    db_bind_t qbinds[] = {
      db_bind_text (key),
      db_bind_i32 (ctx->player_id),
      db_bind_i32 (sector_id)
    };


    if (!db_query (db,
                   SQL_GET3,
                   qbinds,
                   sizeof (qbinds) / sizeof (qbinds[0]),
                   &res,
                   &qerr))
      {
        send_response_error (ctx,
                             root,
                             500,
                             "Could not resolve idempotency race.");
        goto cleanup;
      }

    db_error_t step_err;


    db_error_clear (&step_err);

    if (!db_res_step (res, &step_err))
      {
        db_res_finalize (res);
        send_response_error (ctx,
                             root,
                             500,
                             "Could not resolve idempotency race.");
        goto cleanup;
      }

    const char *resp_s_stored = db_res_col_text (res, 0, &step_err);
    json_error_t jerr;
    json_t *stored_resp =
      resp_s_stored ? json_loads (resp_s_stored, 0, &jerr) : NULL;


    db_res_finalize (res);

    if (!stored_resp)
      {
        send_response_error (ctx, root, 500, "Stored response unreadable.");
        goto cleanup;
      }

    send_response_ok_take (ctx, root, "trade.buy_receipt_v1", &stored_resp);
    goto cleanup;
  }

fail_tx:
  if (we_started_tx)
    {
      db_error_t rb_err;


      db_error_clear (&rb_err);
      db_tx_rollback (db, &rb_err);
      we_started_tx = 0;
    }
  goto cleanup;

cleanup:
  if (trade_lines)
    {
      free_trade_lines (trade_lines, n);
    }
  if (receipt)
    {
      json_decref (receipt);
    }
  if (req_s)
    {
      free (req_s);
    }
  if (resp_s)
    {
      free (resp_s);
    }
  LOGD ("cmd_trade_buy: Exiting cleanup.");
  return 0;
}


int
h_get_port_commodity_details (db_t *db,
                              int port_id,
                              const char *commodity_code,
                              int *quantity_out,
                              int *max_capacity_out,
                              bool *buys_out,
                              bool *sells_out)
{
  /* Preserve “always initialise outputs on failure” behaviour */
  if (quantity_out)
    {
      *quantity_out = 0;
    }
  if (max_capacity_out)
    {
      *max_capacity_out = 0;
    }
  if (buys_out)
    {
      *buys_out = false;
    }
  if (sells_out)
    {
      *sells_out = false;
    }

  if (!db || port_id <= 0 || !commodity_code)
    {
      return ERR_DB_MISUSE;
    }

  const char *sql =
    "SELECT es.quantity, p.size * 1000 AS max_capacity, "
    "       (CASE WHEN c.code IN ('ORE', 'ORG', 'EQU') THEN 1 ELSE 0 END) AS buys_commodity, "
    "       (CASE WHEN c.code IN ('ORE', 'ORG', 'EQU') THEN 1 ELSE 0 END) AS sells_commodity "
    "FROM ports p "
    "LEFT JOIN entity_stock es "
    "  ON p.port_id = es.entity_id "
    " AND es.entity_type = 'port' "
    " AND es.commodity_code = $2 "
    "JOIN commodities c ON c.code = $2 "
    "WHERE p.port_id = $1;";

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[] = {
    db_bind_i32 (port_id),
    db_bind_text (commodity_code)
  };


  if (!db_query (db,
                 sql,
                 params,
                 (size_t)(sizeof (params) / sizeof (params[0])),
                 &res,
                 &err))
    {
      LOGE ("h_get_port_commodity_details: query failed: %s",
            err.message[0] ? err.message : "database error");
      return err.code ? err.code : ERR_DB_QUERY_FAILED;
    }

  /* Port exists => 1 row (LEFT JOIN); missing port => no rows */
  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      return ERR_DB_NOT_FOUND;
    }

  /* quantity can be NULL (no entity_stock entry) => treat as 0 */
  if (quantity_out)
    {
      if (db_res_col_is_null (res, 0))
        {
          *quantity_out = 0;
        }
      else
        {
          *quantity_out = db_res_col_int (res, 0, &err);
        }
    }

  if (max_capacity_out)
    {
      *max_capacity_out = db_res_col_int (res, 1, &err);
    }

  if (buys_out)
    {
      *buys_out = (db_res_col_int (res, 2, &err) != 0);
    }

  if (sells_out)
    {
      *sells_out = (db_res_col_int (res, 3, &err) != 0);
    }

  db_res_finalize (res);

  /* If any column read raised an error, return a DB error code (conservative). */
  if (err.code)
    {
      return err.code;
    }

  return 0;
}

