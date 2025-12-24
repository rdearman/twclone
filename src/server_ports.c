#include <string.h>
#include <jansson.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>              // For snprintf
#include <string.h>             // For strcasecmp, strdup etc.
#include <math.h>               // For pow() function
#include <stddef.h>             // For size_t
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
#include "server_universe.h"
#include "db_player_settings.h"
#include "server_clusters.h"


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


/* Helpers */
const char *
commodity_to_code (const char *commodity)
{
  if (!commodity || !*commodity)
    {
      return NULL;
    }
  db_t *db = game_db_get_handle ();
  if (!db) return NULL;

  db_error_t err;
  db_error_clear(&err);

  const char *sql = "SELECT code FROM commodities WHERE UPPER(code) = UPPER($1) LIMIT 1;";
  db_bind_t params[] = { db_bind_text(commodity) };
  db_res_t *res = NULL;
  
  char *result = NULL;

  if (db_query(db, sql, params, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          const char *code = db_res_col_text(res, 0, &err);
          if (code) result = strdup(code);
      }
      db_res_finalize(res);
  } else {
      LOGE ("commodity_to_code: query failed: %s", err.message);
  }

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


static int
bind_text_or_null (sqlite3_stmt *st, int idx, const char *s)
{
  if (s)
    {
      return sqlite3_bind_text (st, idx, s, -1, SQLITE_TRANSIENT);
    }
  return sqlite3_bind_null (st, idx);
}


static int
h_port_buys_commodity (db_t *db, int port_id, const char *commodity)
{
  if (!db || port_id <= 0 || !commodity || !*commodity)
    {
      return 0;
    }
  char *canonical_commodity_code = (char *) commodity_to_code (commodity);


  if (!canonical_commodity_code)
    {
      return 0;                 // Invalid or unsupported commodity
    }
  
  db_error_t err;
  db_error_clear(&err);

  const char *sql =
    "SELECT es.quantity, p.size * 1000 AS max_capacity "
    "FROM ports p JOIN entity_stock es ON p.id = es.entity_id "
    "WHERE es.entity_type = 'port' AND es.commodity_code = $2 AND p.id = $1 LIMIT 1;";

  db_bind_t params[] = { db_bind_i32(port_id), db_bind_text(canonical_commodity_code) };
  db_res_t *res = NULL;
  
  int buys = 0;

  if (db_query(db, sql, params, 2, &res, &err)) {
      if (db_res_step(res, &err)) {
          int current_quantity = db_res_col_i32(res, 0, &err);
          int max_capacity = db_res_col_i32(res, 1, &err);
          if (current_quantity < max_capacity) {
              buys = 1;
          }
      }
      db_res_finalize(res);
  } else {
      LOGE ("h_port_buys_commodity: query failed: %s", err.message);
  }

  free (canonical_commodity_code);
  return buys;
}


// Helper to get entity stock quantity (generic)
static int
h_get_entity_stock_quantity (db_t *db,
                             const char *entity_type,
                             int entity_id,
                             const char *commodity_code, int *qty_out)
{
  db_error_t err;
  db_error_clear(&err);

  const char *sql =
    "SELECT quantity FROM entity_stock WHERE entity_type=$1 AND entity_id=$2 AND commodity_code=$3;";
  db_bind_t params[] = {
      db_bind_text(entity_type),
      db_bind_i32(entity_id),
      db_bind_text(commodity_code)
  };
  db_res_t *res = NULL;

  if (db_query(db, sql, params, 3, &res, &err)) {
      if (db_res_step(res, &err)) {
          *qty_out = db_res_col_i32(res, 0, &err);
          db_res_finalize(res);
          return 0; // SQLITE_OK equivalent
      }
      db_res_finalize(res);
      *qty_out = 0;
      return ERR_NOT_FOUND;
  }
  return err.code;
}


int
h_update_entity_stock (db_t *db,
                       const char *entity_type,
                       int entity_id,
                       const char *commodity_code,
                       int quantity_delta, int *new_quantity_out)
{
  int current_quantity = 0;
  // Get current quantity (ignore error if not found, assume 0)
  h_get_entity_stock_quantity (db,
                               entity_type,
                               entity_id, commodity_code, &current_quantity);

  int new_quantity = current_quantity + quantity_delta;


  if (new_quantity < 0)
    {
      new_quantity = 0;         // Prevent negative stock
    }
  
  db_error_t err;
  db_error_clear(&err);

  const char *sql_upsert = (db_backend(db) == DB_BACKEND_POSTGRES) ?
    "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "
    "VALUES ($1, $2, $3, $4, 0, EXTRACT(EPOCH FROM now())) "
    "ON CONFLICT(entity_type, entity_id, commodity_code) DO UPDATE SET quantity = $4, last_updated_ts = EXTRACT(EPOCH FROM now());" :
    "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "
    "VALUES ($1, $2, $3, $4, 0, strftime('%s','now')) "
    "ON CONFLICT(entity_type, entity_id, commodity_code) DO UPDATE SET quantity = $4, last_updated_ts = strftime('%s','now');";

  db_bind_t params[] = {
      db_bind_text(entity_type),
      db_bind_i32(entity_id),
      db_bind_text(commodity_code),
      db_bind_i32(new_quantity)
  };

  if (!db_exec(db, sql_upsert, params, 4, &err)) {
      LOGE ("h_update_entity_stock: upsert failed: %s", err.message);
      return err.code;
  }

  if (new_quantity_out)
    {
      *new_quantity_out = new_quantity;
    }
  return 0;
}


int
h_entity_calculate_sell_price (db_t *db,
                               const char *entity_type,
                               int entity_id, const char *commodity)
{
  if (strcmp (entity_type, "port") == 0)
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
  if (strcmp (entity_type, "port") == 0)
    {
      return h_calculate_port_buy_price (db, entity_id, commodity);
    }
  return 0;                     // Unknown entity
}


/**
 * @brief Calculates the price a port will CHARGE the player for a commodity.
 *
 * Uses:
 *   - commodities(code, base_price)
 *   - ports(ore_on_hand, organics_on_hand, equipment_on_hand, techlevel)
 *
 * Formula:
 *   price = ceil(base_price * price_multiplier)
 *
 * Returns:
 *   >0 : valid price
 *   0  : not sellable / misconfigured
 */
int
h_calculate_port_sell_price (db_t *db, int port_id, const char *commodity)
{
  if (!db || port_id <= 0 || !commodity || !*commodity)
    {
      LOGW ("h_calculate_port_sell_price: Invalid input: db=%p, port_id=%d, commodity=%s", (void*)db, port_id, commodity ? commodity : "NULL");
      return 0;
    }

  const char *canonical_commodity = commodity;
  db_error_t err;
  db_error_clear(&err);

  const char *sql =
    "SELECT c.base_price, c.volatility, "
    "       es.quantity, "
    "       p.size * 1000 AS max_capacity, "
    "       p.techlevel, "
    "       ec.price_elasticity, "
    "       ec.volatility_factor "
    "FROM commodities c "
    "JOIN ports p ON p.id = $1 "
    "JOIN entity_stock es ON p.id = es.entity_id AND es.entity_type = 'port' AND es.commodity_code = c.code "
    "JOIN economy_curve ec ON p.economy_curve_id = ec.id "
    "WHERE c.code = $2 LIMIT 1;";

  db_bind_t params[] = {
      db_bind_i32(port_id),
      db_bind_text(canonical_commodity)
  };
  db_res_t *res = NULL;

  int base_price = 0;
  int quantity = 0;
  int max_capacity = 0;
  int techlevel = 0;
  double price_elasticity = 0.0;
  double volatility_factor = 0.0;

  if (db_query(db, sql, params, 2, &res, &err)) {
      if (db_res_step(res, &err)) {
          base_price = db_res_col_i32(res, 0, &err);
          quantity = db_res_col_i32(res, 2, &err);
          max_capacity = db_res_col_i32(res, 3, &err);
          techlevel = db_res_col_i32(res, 4, &err);
          price_elasticity = db_res_col_double(res, 5, &err);
          volatility_factor = db_res_col_double(res, 6, &err);
      } else {
          LOGW ("h_calculate_port_sell_price: No data found for commodity %s at port %d", canonical_commodity, port_id);
          db_res_finalize(res);
          return 0;
      }
      db_res_finalize(res);
  } else {
      LOGE ("h_calculate_port_sell_price: query failed: %s", err.message);
      return 0;
  }

  if (base_price <= 0) return 0;

  double price_multiplier = 1.0;
  if (max_capacity > 0)
    {
      double fill_ratio = (double) quantity / max_capacity;
      if (fill_ratio < 0.5)
        {
          price_multiplier = 1.0 + (1.0 - fill_ratio) * price_elasticity * volatility_factor;
        }
      else
        {
          price_multiplier = 1.0 - (fill_ratio - 0.5) * price_elasticity * volatility_factor;
        }
    }
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
h_port_sells_commodity (sqlite3 *db, int port_id, const char *commodity)
{
  if (!db || port_id <= 0 || !commodity || !*commodity)
    {
      return 0;
    }
  int quantity = 0;
  int rc = h_get_port_commodity_quantity (db, port_id, commodity, &quantity);


  if (rc == SQLITE_OK && quantity > 0)
    {
      return 1;
    }
  return 0;
}


/* static int
   json_get_int_field (json_t *obj, const char *key, int *out)
   {
   json_t *v = json_object_get (obj, key);
   if (!json_is_integer (v))
    return 0;
 * out = (int) json_integer_value (v);
   return 1;
   } */


/* New Helpers for Illegal Goods and Cluster Alignment */


/**
 * @brief Retrieves the alignment of the cluster associated with a given sector.
 * @param db The SQLite database handle.
 * @param sector_id The ID of the sector to query.
 * @return The cluster alignment (e.g., +100 for Fed, -100 for Orion, -25 for Ferrengi), 0 if no cluster found.
 */


/**
 * @brief Checks if a commodity is marked as illegal in the commodities table.
 * @param db The SQLite database handle.
 * @param commodity_code The canonical code of the commodity.
 * @return True if the commodity is illegal, false otherwise.
 */
static bool
h_is_illegal_commodity (sqlite3 *db, const char *commodity_code)
{
  if (!commodity_code)
    {
      return false;
    }
  sqlite3_stmt *stmt;
  bool illegal = false;
  const char *sql = "SELECT illegal FROM commodities WHERE code = ? LIMIT 1";


  if (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_text (stmt, 1, commodity_code, -1, SQLITE_STATIC);
      if (sqlite3_step (stmt) == SQLITE_ROW)
        {
          illegal = (sqlite3_column_int (stmt, 0) != 0);
        }
      sqlite3_finalize (stmt);
    }
  else
    {
      LOGE ("h_is_illegal_commodity: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
    }
  return illegal;
}


/**
 * @brief Retrieves the quantity of a specific commodity at a given port from entity_stock.
 * @param db The SQLite database handle.
 * @param port_id The ID of the port.
 * @param commodity_code The canonical code of the commodity.
 * @param quantity_out Pointer to an integer where the quantity will be stored.
 * @return SQLITE_OK on success, SQLITE_NOTFOUND if commodity not in stock, or other SQLite error codes.
 */


/**
 * @brief Determines if a player can trade a specific commodity at a given port based on alignment rules.
 * @param db The SQLite database handle.
 * @param port_id The ID of the port.
 * @param player_id The ID of the player.
 * @param commodity_code The canonical code of the commodity.
 * @return True if trade is allowed, false otherwise.
 */
static bool
h_can_trade_commodity (sqlite3 *db,
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


  if (rc_get_qty != SQLITE_OK && rc_get_qty != SQLITE_NOTFOUND)
    {
      LOGE
      (
        "h_can_trade_commodity: Error checking commodity existence for port %d, commodity %s (rc=%d)",
        port_id,
        commodity_code,
        rc_get_qty);
      return false;             // Error reading DB
    }
  // If not found in entity_stock (SQLITE_NOTFOUND), assume port doesn't trade this commodity (or it's not present)
  // This means it is not a tradable commodity for this port through the new system.
  if (rc_get_qty == SQLITE_NOTFOUND)
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
  sqlite3_stmt *port_sector_stmt;


  if (sqlite3_prepare_v2 (db,
                          "SELECT sector FROM ports WHERE id = ? LIMIT 1",
                          -1, &port_sector_stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (port_sector_stmt, 1, port_id);
      if (sqlite3_step (port_sector_stmt) == SQLITE_ROW)
        {
          sector_id = sqlite3_column_int (port_sector_stmt, 0);
        }
      sqlite3_finalize (port_sector_stmt);
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
  // Neutral clusters are also assumed to prohibit illegal trade by default
  // If cluster is evil, we can proceed to check player alignment
  // Now we are in an 'evil' cluster (or neutral if configured)
  // 5. Check player alignment band properties
  int player_alignment = 0;     // Declare player_alignment


  db_player_get_alignment (db, player_id, &player_alignment);   // Retrieve player's raw alignment score
  int player_align_band_id = 0;
  int player_is_evil = 0;


  db_alignment_band_for_value (db,
                               player_alignment,
                               &player_align_band_id,
                               NULL, NULL, NULL, &player_is_evil, NULL, NULL);
  int neutral_band_value = db_get_config_int (db, "neutral_band", 75);  // Get neutral_band from config


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
  return true;                  // Evil player in an evil cluster, trading illegal goods is permitted
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
                     const char *commodity_code,
                     int quantity_delta, int *new_quantity_out)
{
  return h_update_entity_stock (db, "port", port_id, commodity_code, quantity_delta, new_quantity_out);
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
 * @return SQLITE_OK on success, or an SQLite error code.
 */
int
h_market_move_port_stock (sqlite3 *db,
                          int port_id,
                          const char *commodity_code, int quantity_delta)
{
  if (!db || port_id <= 0 || !commodity_code)
    {
      return SQLITE_MISUSE;
    }

  if (quantity_delta == 0)
    {
      return SQLITE_OK;
    }

  // 1. Get current quantity and port size (for max capacity)
  sqlite3_stmt *stmt = NULL;
  const char *sql_info =
    "SELECT es.quantity, p.size * 1000 AS max_capacity "
    "FROM ports p "
    "LEFT JOIN entity_stock es ON p.id = es.entity_id AND es.entity_type = 'port' AND es.commodity_code = ?2 "
    "WHERE p.id = ?1;";

  int rc = sqlite3_prepare_v2 (db, sql_info, -1, &stmt, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("h_market_move_port_stock: prepare info failed: %s",
            sqlite3_errmsg (db));
      return rc;
    }

  sqlite3_bind_int (stmt, 1, port_id);
  sqlite3_bind_text (stmt, 2, commodity_code, -1, SQLITE_STATIC);

  int current_quantity = 0;
  int max_capacity = 0;


  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      current_quantity = sqlite3_column_int (stmt, 0);  // NULL becomes 0
      max_capacity = sqlite3_column_int (stmt, 1);
    }
  else
    {
      LOGE ("h_market_move_port_stock: Port %d not found.", port_id);
      sqlite3_finalize (stmt);
      return SQLITE_NOTFOUND;
    }
  sqlite3_finalize (stmt);

  // 2. Calculate new quantity with bounds checking
  int new_quantity = current_quantity + quantity_delta;


  new_quantity = (new_quantity < 0) ? 0 : new_quantity;
  new_quantity = (new_quantity > max_capacity) ? max_capacity : new_quantity;

  // 3. Update DB
  return h_update_entity_stock (db,
                                ENTITY_TYPE_PORT,
                                port_id,
                                commodity_code,
                                new_quantity - current_quantity, NULL);
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
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");
  json_t *payload = NULL;
  const char *commodity = NULL;
  int port_id = 0;
  int quantity = 0;
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
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
      send_response_error (ctx,
                           root,
                           400,
                           "port_id, commodity, and quantity are required.");
      return 0;
    }
  // Validate commodity using commodity_to_code
  const char *commodity_code = commodity_to_code (commodity);


  if (!commodity_code)
    {
      send_response_error (ctx, root, 400, "Invalid commodity.");
      return 0;
    }
  // Calculate the price the port will CHARGE the player (player's buy price)
  int player_buy_price_per_unit =
    h_entity_calculate_sell_price (db, ENTITY_TYPE_PORT, port_id,
                                   commodity_code);
  long long total_player_buy_price =
    (long long) player_buy_price_per_unit * quantity;
  // Calculate the price the port will PAY the player (player's sell price)
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
  // Free the commodity_code
  free ((char *) commodity_code);       // Cast to char* because strdup returns char*
  return 0;
}


int
cmd_trade_history (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db) {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
  }
  json_t *data = json_object_get (root, "data");
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated", NULL);
      return 0;
    }
  // 1. Get parameters
  const char *cursor = json_string_value (json_object_get (data, "cursor"));
  int limit = json_integer_value (json_object_get (data, "limit"));

  if (limit <= 0 || limit > 50)
    {
      limit = 20;               // Default or maximum
    }
  // 2. Prepare the query and cursor parameters
  const char *sql_base =
    "SELECT timestamp, id, port_id, commodity, units, price_per_unit, action "
    "FROM trade_log WHERE player_id = $1 ";
  const char *sql_cursor_cond =
    "AND (timestamp < $3 OR (timestamp = $3 AND id < $4)) ";
  const char *sql_suffix = "ORDER BY timestamp DESC, id DESC LIMIT $2;";
  long long cursor_ts = 0;
  long long cursor_id = 0;
  char sql[512] = { 0 };

  // Build the SQL query based on the cursor state
  if (cursor && (strlen (cursor) > 0))
    {
      char *sep = strchr ((char *) cursor, '_');
      if (sep)
        {
          *sep = '\0';
          cursor_ts = atoll (cursor);
          cursor_id = atoll (sep + 1);
          *sep = '_';
          if (cursor_ts > 0 && cursor_id > 0)
            {
              snprintf (sql, sizeof (sql), "%s%s%s", sql_base,
                        sql_cursor_cond, sql_suffix);
            }
        }
    }
  if (sql[0] == 0)
    {
      snprintf (sql, sizeof (sql), "%s%s", sql_base, sql_suffix);
    }

  db_bind_t params[4];
  int n_params = 2;
  params[0] = db_bind_i32(ctx->player_id);
  params[1] = db_bind_i32(limit + 1);
  if (cursor_ts > 0 && cursor_id > 0) {
      params[2] = db_bind_i64(cursor_ts);
      params[3] = db_bind_i64(cursor_id);
      n_params = 4;
  }

  db_res_t *res = NULL;
  db_error_t err;
  if (!db_query(db, sql, params, n_params, &res, &err)) {
      LOGE ("trade.history query error: %s", err.message);
      send_response_error (ctx, root, ERR_DB, "Database error");
      return 0;
  }

  // 4. Fetch results
  json_t *history_array = json_array ();
  int count = 0;
  long long last_ts = 0;
  long long last_id = 0;

  while (db_res_step(res, &err)) {
      if (count < limit)
        {
          json_t *hist_row = json_object ();
          json_object_set_new (hist_row, "timestamp", json_integer (db_res_col_i64(res, 0, &err)));
          json_object_set_new (hist_row, "id", json_integer (db_res_col_i64(res, 1, &err)));
          json_object_set_new (hist_row, "port_id", json_integer (db_res_col_i32(res, 2, &err)));
          json_object_set_new (hist_row, "commodity", json_string (db_res_col_text(res, 3, &err)));
          json_object_set_new (hist_row, "units", json_integer (db_res_col_i32(res, 4, &err)));
          json_object_set_new (hist_row, "price_per_unit", json_real (db_res_col_double(res, 5, &err)));
          json_object_set_new (hist_row, "action", json_string (db_res_col_text(res, 6, &err)));
          json_array_append_new (history_array, hist_row);
          last_ts = db_res_col_i64(res, 0, &err);
          last_id = db_res_col_i64(res, 1, &err);
          count++;
        }
      else
        {
          break;
        }
  }
  db_res_finalize(res);

  // 5. Build and send response
  json_t *payload = json_object ();
  json_object_set_new (payload, "history", history_array);
  if (count == limit && last_id > 0)
    {
      char next_cursor[64];
      snprintf (next_cursor, sizeof (next_cursor), "%lld_%lld", last_ts, last_id);
      json_object_set_new (payload, "next_cursor", json_string (next_cursor));
    }
  send_response_ok_take (ctx, root, "trade.history", &payload);
  return 0;
}


int
cmd_dock_status (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db) {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
  }
  int rc = 0;
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
  // Resolve port ID in current sector
  resolved_port_id = db_get_port_id_by_sector (ctx->sector_id);
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
      // Update ships.ported status
      {
        const char *sql_update_ported =
          "UPDATE ships SET ported = $1, onplanet = 0 WHERE id = $2;";
        db_bind_t params[] = { db_bind_i32(new_ported_status), db_bind_i32(player_ship_id) };
        db_error_t err;
        if (!db_exec(db, sql_update_ported, params, 2, &err))
          {
            LOGE ("cmd_dock_status: Failed to update ships.ported: %s", err.message);
            send_response_error (ctx, root, ERR_DB, "Database error.");
            return -1;
          }
      }
      // Generate System Notice if successfully docked at a port
      if (new_ported_status > 0)
        {
          db_get_ship_name (db, player_ship_id, &ship_name);
          db_get_port_name (db, new_ported_status, &port_name);
          db_player_name (ctx->player_id, &player_name);
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
          db_notice_create ("Docking Log", notice_body, "info",
                            time (NULL) + (86400 * 7));
        }
      resolved_port_id = new_ported_status;
    }
  else
    {
      // Status check only
      const char *sql_check = "SELECT ported FROM ships WHERE id = $1";
      db_bind_t params[] = { db_bind_i32(player_ship_id) };
      db_res_t *res = NULL;
      db_error_t err;
      if (db_query(db, sql_check, params, 1, &res, &err)) {
          if (db_res_step(res, &err)) {
              resolved_port_id = db_res_col_i32(res, 0, &err);
          }
          db_res_finalize(res);
      }
    }
  payload = json_object ();
  if (!payload)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Out of memory.");
      goto cleanup;
    }
  json_object_set_new (payload, "port_id", json_integer (resolved_port_id));
  json_object_set_new (payload, "sector_id", json_integer (ctx->sector_id));
  json_object_set_new (payload, "docked",
                       json_boolean (resolved_port_id > 0));
  send_response_ok_take (ctx, root, "dock.status_v1", &payload);
  payload = NULL;
cleanup:
  if (player_name) free (player_name);
  if (ship_name) free (ship_name);
  if (port_name) free (port_name);
  if (payload) json_decref (payload);
  return 0;
}


int
h_calculate_port_buy_price (db_t *db, int port_id, const char *commodity)
{
  if (!db || port_id <= 0 || !commodity || !*commodity)
    {
      LOGW ("h_calculate_port_buy_price: Invalid input: db=%p, port_id=%d, commodity=%s", (void*)db, port_id, commodity ? commodity : "NULL");
      return 0;
    }

  const char *canonical_commodity = commodity;
  db_error_t err;
  db_error_clear(&err);

  const char *sql =
    "SELECT c.base_price, c.volatility, "
    "       es.quantity, "
    "       p.size * 1000 AS max_capacity, "
    "       p.techlevel, "
    "       ec.price_elasticity, "
    "       ec.volatility_factor "
    "FROM commodities c "
    "JOIN ports p ON p.id = $1 "
    "JOIN entity_stock es ON p.id = es.entity_id AND es.entity_type = 'port' AND es.commodity_code = c.code "
    "JOIN economy_curve ec ON p.economy_curve_id = ec.id "
    "WHERE c.code = $2 LIMIT 1;";

  db_bind_t params[] = {
      db_bind_i32(port_id),
      db_bind_text(canonical_commodity)
  };
  db_res_t *res = NULL;

  int base_price = 0;
  int quantity = 0;
  int max_capacity = 0;
  int techlevel = 0;
  double price_elasticity = 0.0;
  double volatility_factor = 0.0;

  if (db_query(db, sql, params, 2, &res, &err)) {
      if (db_res_step(res, &err)) {
          base_price = db_res_col_i32(res, 0, &err);
          quantity = db_res_col_i32(res, 2, &err);
          max_capacity = db_res_col_i32(res, 3, &err);
          techlevel = db_res_col_i32(res, 4, &err);
          price_elasticity = db_res_col_double(res, 5, &err);
          volatility_factor = db_res_col_double(res, 6, &err);
      } else {
          LOGW ("h_calculate_port_buy_price: No data found for commodity %s at port %d", canonical_commodity, port_id);
          db_res_finalize(res);
          return 0;
      }
      db_res_finalize(res);
  } else {
      LOGE ("h_calculate_port_buy_price: query failed: %s", err.message);
      return 0;
  }

  if (quantity >= max_capacity) return 0;
  if (base_price <= 0) return 0;

  double price_multiplier = 1.0;
  if (max_capacity > 0)
    {
      double fill_ratio = (double) quantity / max_capacity;
      if (fill_ratio > 0.5)
        {
          price_multiplier = 1.0 - (fill_ratio - 0.5) * price_elasticity * volatility_factor;
        }
      else
        {
          price_multiplier = 1.0 + (0.5 - fill_ratio) * price_elasticity * volatility_factor;
        }
    }
  price_multiplier *= (1.0 - (techlevel - 1) * 0.02);
  long long price = (long long) (base_price * price_multiplier + 0.999999);
  if (price < 1) price = 1;
  if (price > 2000000000LL) price = 2000000000LL;
  return (int) price;
}


int
cmd_trade_port_info (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }

  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated", NULL);
      return 0;
    }

  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 0;
  int requested_port_id = 0;
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

  const char *sql_port_info = NULL;
  db_bind_t params_port[1];
  int port_id_val = 0;

  if (requested_port_id > 0)
    {
      sql_port_info =
        "SELECT id, number, name, sector, size, techlevel, petty_cash, type "
        "FROM ports WHERE id = $1 LIMIT 1;";
      params_port[0] = db_bind_i32(requested_port_id);
    }
  else if (sector_id > 0)
    {
      sql_port_info =
        "SELECT id, number, name, sector, size, techlevel, petty_cash, type "
        "FROM ports WHERE sector = $1 LIMIT 1;";
      params_port[0] = db_bind_i32(sector_id);
    }
  else
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_MISSING_FIELD,
                                   "Missing port_id or sector_id", NULL);
      return 0;
    }

  db_error_t err;
  db_error_clear(&err);
  db_res_t *res_port = NULL;
  json_t *port = NULL;

  if (db_query(db, sql_port_info, params_port, 1, &res_port, &err)) {
      if (db_res_step(res_port, &err)) {
          port = json_object ();
          int col_idx = 0;
          port_id_val = db_res_col_i32(res_port, col_idx++, &err);
          json_object_set_new (port, "id", json_integer (port_id_val));
          json_object_set_new (port, "number", json_integer (db_res_col_i32(res_port, col_idx++, &err)));
          json_object_set_new (port, "name", json_string (db_res_col_text(res_port, col_idx++, &err)));
          json_object_set_new (port, "sector", json_integer (db_res_col_i32(res_port, col_idx++, &err)));
          json_object_set_new (port, "size", json_integer (db_res_col_i32(res_port, col_idx++, &err)));
          json_object_set_new (port, "techlevel", json_integer (db_res_col_i32(res_port, col_idx++, &err)));
          json_object_set_new (port, "petty_cash", json_integer (db_res_col_i32(res_port, col_idx++, &err)));
          json_object_set_new (port, "type", json_integer (db_res_col_i32(res_port, col_idx++, &err)));
      } else {
          LOGW ("cmd_trade_port_info: No port found for id=%d or sector=%d", requested_port_id, sector_id);
          send_response_refused_steal (ctx, root, ERR_PORT_NOT_FOUND, "No port found.", NULL);
          db_res_finalize(res_port);
          return 0;
      }
      db_res_finalize(res_port);
  } else {
      send_response_error (ctx, root, err.code, "Database error.");
      return -1;
  }

  const char *sql_commodities =
    "SELECT es.commodity_code, es.quantity, es.price, c.illegal "
    "FROM entity_stock es JOIN commodities c ON es.commodity_code = c.code "
    "WHERE es.entity_type = 'port' AND es.entity_id = $1;";

  db_bind_t params_comm[] = { db_bind_i32(port_id_val) };
  db_res_t *res_comm = NULL;
  json_t *commodities_array = json_array ();

  if (db_query(db, sql_commodities, params_comm, 1, &res_comm, &err)) {
      while (db_res_step(res_comm, &err)) {
          const char *commodity_code = db_res_col_text(res_comm, 0, &err);
          int quantity = db_res_col_i32(res_comm, 1, &err);
          int price = db_res_col_i32(res_comm, 2, &err);
          int illegal = db_res_col_i32(res_comm, 3, &err);

          if (illegal && !h_can_trade_commodity (db, port_id_val, ctx->player_id, commodity_code)) {
              continue;
          }

          json_t *commodity_obj = json_object ();
          json_object_set_new (commodity_obj, "code", json_string (commodity_code ? commodity_code : ""));
          json_object_set_new (commodity_obj, "quantity", json_integer (quantity));
          json_object_set_new (commodity_obj, "price", json_integer (price));
          json_object_set_new (commodity_obj, "illegal", json_boolean (illegal));
          json_array_append_new (commodities_array, commodity_obj);
      }
      db_res_finalize(res_comm);
  }

  json_object_set_new (port, "commodities", commodities_array);
  json_t *payload = json_object ();
  json_object_set_new (payload, "port", port);
  send_response_ok_take (ctx, root, "trade.port_info", &payload);
  return 0;
}


int
cmd_trade_buy (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
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
  char tx_group_id[64];

  h_generate_hex_uuid (tx_group_id, sizeof (tx_group_id));
  TradeLine *trade_lines = NULL;
  size_t n = 0;

  if (!ctx || !root || !db) return -1;
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_SECTOR_NOT_FOUND, "Not authenticated", NULL);
      return 0;
    }

  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "trade.buy", root, NULL);
    }

  data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_response_error (ctx, root, 400, "Missing data object.");
      return -1;
    }

  key = json_string_value (json_object_get (data, "idempotency_key"));
  if (key) {
      if (h_idemp_check_fast_path(db, key, ctx->player_id, ctx->sector_id, "trade.buy", root, "trade.buy_receipt_v1")) {
          return 0;
      }
  }

  int account_type = db_get_player_pref_int (ctx->player_id, "trade.default_account", 0);
  json_t *jaccount = json_object_get (data, "account");
  if (json_is_integer (jaccount)) {
      int requested_account_type = (int) json_integer_value (jaccount);
      if (requested_account_type == 0 || requested_account_type == 1) account_type = requested_account_type;
  }

  sector_id = ctx->sector_id;
  json_t *jsec = json_object_get (data, "sector_id");
  if (json_is_integer (jsec)) sector_id = (int) json_integer_value (jsec);
  if (sector_id <= 0) {
      send_response_error (ctx, root, 400, "Invalid sector_id.");
      return -1;
  }

  if (!cluster_can_trade (db, sector_id, ctx->player_id)) {
      send_response_refused_steal (ctx, root, REF_TURN_COST_EXCEEDS, "Port refuses to trade: Banned in cluster.", NULL);
      return 0;
  }

  json_t *jport = json_object_get (data, "port_id");
  if (json_is_integer (jport)) requested_port_id = (int) json_integer_value (jport);

  if (requested_port_id > 0) {
      port_id = requested_port_id;
  } else {
      port_id = db_get_port_id_by_sector(sector_id);
  }
  if (port_id <= 0) {
      send_response_error (ctx, root, REF_AUTOPILOT_RUNNING, "No port found.");
      return -1;
  }

  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  h_decloak_ship (db, player_ship_id);

  long long current_credits = 0;
  if (account_type == 0) h_get_player_petty_cash (db, ctx->player_id, &current_credits);
  else h_get_credits (db, "player", ctx->player_id, &current_credits);

  int cur_ore=0, cur_org=0, cur_eq=0, cur_holds=0;
  h_get_ship_cargo_and_holds (db, player_ship_id, &cur_ore, &cur_org, &cur_eq, NULL, NULL, NULL, NULL, &cur_holds);
  int current_load = cur_ore + cur_org + cur_eq;

  jitems = json_object_get (data, "items");
  if (!json_is_array(jitems)) {
      send_response_error(ctx, root, 400, "Missing items array");
      return -1;
  }
  n = json_array_size (jitems);
  trade_lines = calloc (n, sizeof (*trade_lines));

  for (size_t i = 0; i < n; i++) {
      json_t *it = json_array_get (jitems, i);
      const char *raw_commodity = json_string_value (json_object_get (it, "commodity"));
      int amount = (int) json_integer_value (json_object_get (it, "quantity"));

      if (!raw_commodity || amount <= 0) {
          free_trade_lines (trade_lines, n);
          send_response_error (ctx, root, 400, "Invalid item data.");
          return -1;
      }
      char *canonical = (char *) commodity_to_code (raw_commodity);
      if (!canonical) {
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx, root, ERR_AUTOPILOT_PATH_INVALID, "Invalid commodity.", NULL);
          return 0;
      }
      trade_lines[i].commodity = canonical;
      if (!h_can_trade_commodity (db, port_id, ctx->player_id, canonical)) {
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx, root, 1406, "Illegal trade forbidden.", NULL);
          return 0;
      }
      if (!h_port_sells_commodity (db, port_id, canonical)) {
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx, root, ERR_AUTOPILOT_PATH_INVALID, "Port not selling.", NULL);
          return 0;
      }
      int uprice = h_calculate_port_sell_price (db, port_id, canonical);
      if (uprice <= 0) {
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx, root, ERR_AUTOPILOT_PATH_INVALID, "Price error.", NULL);
          return 0;
      }
      trade_lines[i].amount = amount;
      trade_lines[i].unit_price = uprice;
      trade_lines[i].line_cost = (long long)amount * uprice;
      total_item_cost += trade_lines[i].line_cost;
      total_cargo_space_needed += amount;
  }

  calculate_fees (db, "TRADE_BUY", total_item_cost, "player", &charges);
  total_cost_with_fees = total_item_cost + charges.fee_total;

  if (current_credits < total_cost_with_fees) {
      free_trade_lines(trade_lines, n);
      send_response_refused_steal(ctx, root, 1402, "Insufficient credits.", NULL);
      return 0;
  }
  if (current_load + total_cargo_space_needed > cur_holds) {
      free_trade_lines(trade_lines, n);
      send_response_refused_steal(ctx, root, 1403, "Insufficient cargo space.", NULL);
      return 0;
  }

  receipt = json_object ();
  lines = json_array ();
  json_object_set_new (receipt, "sector_id", json_integer (sector_id));
  json_object_set_new (receipt, "port_id", json_integer (port_id));
  json_object_set_new (receipt, "player_id", json_integer (ctx->player_id));
  json_object_set_new (receipt, "lines", lines);

  db_error_t err;
  int retry_count;
  for (retry_count = 0; retry_count < 3; retry_count++) {
      if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) {
          if (err.code == ERR_DB_BUSY) { usleep(100000); continue; }
          goto cleanup;
      }

      for (size_t i = 0; i < n; i++) {
          const char *sql_log = "INSERT INTO trade_log (player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) "
                                "VALUES ($1, $2, $3, $4, $5, $6, 'buy', $7);";
          db_bind_t p_log[] = { db_bind_i32(ctx->player_id), db_bind_i32(port_id), db_bind_i32(sector_id), db_bind_text(trade_lines[i].commodity), db_bind_i32(trade_lines[i].amount), db_bind_i32(trade_lines[i].unit_price), db_bind_i64(time(NULL)) };
          if (!db_exec(db, sql_log, p_log, 7, &err)) goto rollback;

          if (h_update_ship_cargo(db, ctx->ship_id, trade_lines[i].commodity, trade_lines[i].amount, NULL) != 0) goto rollback;
          if (h_update_port_stock(db, port_id, trade_lines[i].commodity, -trade_lines[i].amount, NULL) != 0) goto rollback;

          json_t *jline = json_object ();
          json_object_set_new (jline, "commodity", json_string (trade_lines[i].commodity));
          json_object_set_new (jline, "quantity", json_integer (trade_lines[i].amount));
          json_object_set_new (jline, "unit_price", json_integer (trade_lines[i].unit_price));
          json_object_set_new (jline, "value", json_integer (trade_lines[i].line_cost));
          json_array_append_new (lines, jline);
      }

      if (account_type == 0) {
          if (h_deduct_credits_unlocked(db, ctx->player_id, total_cost_with_fees, "TRADE_BUY", tx_group_id, &new_balance) != 0) goto rollback;
      } else {
          int aid = 0;
          h_get_account_id_unlocked(db, "player", ctx->player_id, &aid);
          if (h_deduct_credits_unlocked(db, aid, total_cost_with_fees, "TRADE_BUY", tx_group_id, &new_balance) != 0) goto rollback;
      }

      int paid = 0;
      h_get_account_id_unlocked(db, "port", port_id, &paid);
      if (h_add_credits_unlocked(db, paid, total_item_cost, "TRADE_BUY", tx_group_id, NULL) != 0) goto rollback;

      if (charges.fee_to_bank > 0) {
          int sysid = 0;
          h_get_system_account_id_unlocked(db, "SYSTEM", 0, &sysid);
          h_add_credits_unlocked(db, sysid, charges.fee_to_bank, "TRADE_BUY_FEE", tx_group_id, NULL);
      }

      if (!db_tx_commit(db, &err)) goto rollback;

      json_object_set_new (receipt, "credits_remaining", json_integer (new_balance));
      json_object_set_new (receipt, "total_item_cost", json_integer (total_item_cost));
      json_object_set_new (receipt, "total_cost_with_fees", json_integer (total_cost_with_fees));
      json_object_set_new (receipt, "fees", json_integer (charges.fee_total));

      if (key) db_idemp_store_response(key, json_dumps(receipt, 0));

      send_response_ok_take (ctx, root, "trade.buy_receipt_v1", &receipt);
      free_trade_lines(trade_lines, n);
      return 0;

rollback:
      db_tx_rollback(db, &err);
      if (err.code == ERR_DB_BUSY) { usleep(100000); continue; }
      goto cleanup;
  }

cleanup:
  if (trade_lines) free_trade_lines (trade_lines, n);
  if (receipt) json_decref (receipt);
  if (req_s) free(req_s);
  if (resp_s) free(resp_s);
  return 0;
}

int
cmd_trade_sell (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  json_t *receipt = NULL;
  json_t *lines = NULL;
  json_t *data = NULL;
  json_t *jitems = NULL;
  int rc = 0;
  char *req_s = NULL;
  char *resp_s = NULL;
  int sector_id = 0;
  const char *key = NULL;
  int port_id = 0;
  int requested_port_id = 0;
  long long total_item_value = 0;
  long long total_credits_after_fees = 0;
  fee_result_t charges = { 0 };
  long long new_balance = 0;
  char tx_group_id[64];

  h_generate_hex_uuid (tx_group_id, sizeof (tx_group_id));
  TradeLine *trade_lines = NULL;
  size_t n = 0;

  if (!ctx || !root || !db) return -1;
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_SECTOR_NOT_FOUND, "Not authenticated", NULL);
      return 0;
    }

  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "trade.sell", root, NULL);
    }

  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  h_decloak_ship (db, player_ship_id);

  data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_response_error (ctx, root, 400, "Missing data object.");
      return -1;
    }

  key = json_string_value (json_object_get (data, "idempotency_key"));
  if (key) {
      if (h_idemp_check_fast_path(db, key, ctx->player_id, ctx->sector_id, "trade.sell", root, "trade.sell_receipt_v1")) {
          return 0;
      }
  }

  int account_type = db_get_player_pref_int (ctx->player_id, "trade.default_account", 0);
  json_t *jaccount = json_object_get (data, "account");
  if (json_is_integer (jaccount)) {
      int requested_account_type = (int) json_integer_value (jaccount);
      if (requested_account_type == 0 || requested_account_type == 1) account_type = requested_account_type;
  }

  sector_id = ctx->sector_id;
  json_t *jsec = json_object_get (data, "sector_id");
  if (json_is_integer (jsec)) sector_id = (int) json_integer_value (jsec);
  if (sector_id <= 0) {
      send_response_error (ctx, root, 400, "Invalid sector_id.");
      return -1;
  }

  if (!cluster_can_trade (db, sector_id, ctx->player_id)) {
      send_response_refused_steal (ctx, root, REF_TURN_COST_EXCEEDS, "Port refuses to trade: Banned in cluster.", NULL);
      return 0;
  }

  json_t *jport = json_object_get (data, "port_id");
  if (json_is_integer (jport)) requested_port_id = (int) json_integer_value (jport);

  if (requested_port_id > 0) {
      port_id = requested_port_id;
  } else {
      port_id = db_get_port_id_by_sector(sector_id);
  }
  if (port_id <= 0) {
      send_response_error (ctx, root, REF_AUTOPILOT_RUNNING, "No port found.");
      return -1;
  }

  jitems = json_object_get (data, "items");
  if (!json_is_array(jitems)) {
      send_response_error(ctx, root, 400, "Missing items array");
      return -1;
  }
  n = json_array_size (jitems);
  trade_lines = calloc (n, sizeof (*trade_lines));

  for (size_t i = 0; i < n; i++) {
      json_t *it = json_array_get (jitems, i);
      const char *raw_commodity = json_string_value (json_object_get (it, "commodity"));
      int amount = (int) json_integer_value (json_object_get (it, "quantity"));

      if (!raw_commodity || amount <= 0) {
          free_trade_lines (trade_lines, n);
          send_response_error (ctx, root, 400, "Invalid item data.");
          return -1;
      }
      char *canonical = (char *) commodity_to_code (raw_commodity);
      if (!canonical) {
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx, root, ERR_AUTOPILOT_PATH_INVALID, "Invalid commodity.", NULL);
          return 0;
      }
      trade_lines[i].commodity = canonical;
      
      // Illegal check: if player sells illegal goods, they might get caught? 
      // For now, allow selling illegal goods to port if port buys them.
      // But verify if port BUYS it.
      
      if (!h_port_buys_commodity (db, port_id, canonical)) {
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx, root, ERR_AUTOPILOT_PATH_INVALID, "Port not buying.", NULL);
          return 0;
      }
      int uprice = h_calculate_port_buy_price (db, port_id, canonical);
      if (uprice <= 0) {
          // Port full or price 0
          free_trade_lines (trade_lines, n);
          send_response_refused_steal (ctx, root, ERR_AUTOPILOT_PATH_INVALID, "Port not buying (full/no price).", NULL);
          return 0;
      }
      
      // Check player has cargo
      int ship_qty = 0;
      h_get_ship_stock_quantity(db, player_ship_id, canonical, &ship_qty);
      if (ship_qty < amount) {
          free_trade_lines (trade_lines, n);
          json_t *d = json_object();
          json_object_set_new(d, "have", json_integer(ship_qty));
          json_object_set_new(d, "need", json_integer(amount));
          send_response_refused_steal (ctx, root, 1404, "Insufficient cargo.", d);
          return 0;
      }

      trade_lines[i].amount = amount;
      trade_lines[i].unit_price = uprice;
      trade_lines[i].line_cost = (long long)amount * uprice;
      total_item_value += trade_lines[i].line_cost;
  }

  calculate_fees (db, "TRADE_SELL", total_item_value, "player", &charges);
  total_credits_after_fees = total_item_value - charges.fee_total;
  if (total_credits_after_fees < 0) total_credits_after_fees = 0; // Should not happen usually

  receipt = json_object ();
  lines = json_array ();
  json_object_set_new (receipt, "sector_id", json_integer (sector_id));
  json_object_set_new (receipt, "port_id", json_integer (port_id));
  json_object_set_new (receipt, "player_id", json_integer (ctx->player_id));
  json_object_set_new (receipt, "lines", lines);

  db_error_t err;
  int retry_count;
  for (retry_count = 0; retry_count < 3; retry_count++) {
      if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) {
          if (err.code == ERR_DB_BUSY) { usleep(100000); continue; }
          goto cleanup;
      }

      for (size_t i = 0; i < n; i++) {
          const char *sql_log = "INSERT INTO trade_log (player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) "
                                "VALUES ($1, $2, $3, $4, $5, $6, 'sell', $7);";
          db_bind_t p_log[] = { db_bind_i32(ctx->player_id), db_bind_i32(port_id), db_bind_i32(sector_id), db_bind_text(trade_lines[i].commodity), db_bind_i32(trade_lines[i].amount), db_bind_i32(trade_lines[i].unit_price), db_bind_i64(time(NULL)) };
          if (!db_exec(db, sql_log, p_log, 7, &err)) goto rollback;

          if (h_update_ship_cargo(db, ctx->ship_id, trade_lines[i].commodity, -trade_lines[i].amount, NULL) != 0) goto rollback;
          if (h_update_port_stock(db, port_id, trade_lines[i].commodity, trade_lines[i].amount, NULL) != 0) goto rollback;

          json_t *jline = json_object ();
          json_object_set_new (jline, "commodity", json_string (trade_lines[i].commodity));
          json_object_set_new (jline, "quantity", json_integer (trade_lines[i].amount));
          json_object_set_new (jline, "unit_price", json_integer (trade_lines[i].unit_price));
          json_object_set_new (jline, "value", json_integer (trade_lines[i].line_cost));
          json_array_append_new (lines, jline);
      }

      if (account_type == 0) {
          if (h_add_player_petty_cash_unlocked(db, ctx->player_id, total_credits_after_fees, &new_balance) != 0) goto rollback;
      } else {
          int aid = 0;
          h_get_account_id_unlocked(db, "player", ctx->player_id, &aid);
          if (h_add_credits_unlocked(db, aid, total_credits_after_fees, "TRADE_SELL", tx_group_id, &new_balance) != 0) goto rollback;
      }

      int paid = 0;
      h_get_account_id_unlocked(db, "port", port_id, &paid);
      // Port pays total_item_value. Fees are deducted from player.
      if (h_deduct_credits_unlocked(db, paid, total_item_value, "TRADE_SELL", tx_group_id, NULL) != 0) {
          // If port cannot pay, we should rollback and fail? Or partial?
          // Standard: refuse if port bank is empty?
          // For now, if port has infinite credits or overdraft, it's fine.
          // If strict:
          // goto rollback;
          // But h_deduct... returns error on constraint?
          // We'll assume failure means rollback.
          goto rollback;
      }

      if (charges.fee_to_bank > 0) {
          int sysid = 0;
          h_get_system_account_id_unlocked(db, "SYSTEM", 0, &sysid);
          h_add_credits_unlocked(db, sysid, charges.fee_to_bank, "TRADE_SELL_FEE", tx_group_id, NULL);
      }

      if (!db_tx_commit(db, &err)) goto rollback;

      json_object_set_new (receipt, "credits_remaining", json_integer (new_balance));
      json_object_set_new (receipt, "total_item_value", json_integer (total_item_value));
      json_object_set_new (receipt, "total_credits_received", json_integer (total_credits_after_fees));
      json_object_set_new (receipt, "fees", json_integer (charges.fee_total));

      if (key) db_idemp_store_response(key, json_dumps(receipt, 0));

      send_response_ok_take (ctx, root, "trade.sell_receipt_v1", &receipt);
      free_trade_lines(trade_lines, n);
      return 0;

rollback:
      db_tx_rollback(db, &err);
      if (err.code == ERR_DB_BUSY) { usleep(100000); continue; }
      goto cleanup;
  }

cleanup:
  if (trade_lines) free_trade_lines (trade_lines, n);
  if (receipt) json_decref (receipt);
  if (req_s) free(req_s);
  if (resp_s) free(resp_s);
  return 0;
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

  if (!db) {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
  }

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
      return handle_turn_consumption_error (ctx, tc, "ship.jettison", root, NULL);
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

  const char *canonical = commodity_to_code(commodity);
  if (!canonical) {
      send_response_refused_steal (ctx, root, ERR_AUTOPILOT_PATH_INVALID, "Unknown commodity.", NULL);
      return 0;
  }

  // Check current cargo
  int have = 0;
  h_get_ship_stock_quantity(db, player_ship_id, canonical, &have);

  if (have < quantity)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_NO_WARP_LINK,
                                   "You do not carry enough of that commodity to jettison.",
                                   NULL);
      return 0;
    }

  // Update ship cargo (jettisoning means negative delta)
  int new_qty = 0;
  rc = h_update_ship_cargo (db, player_ship_id, canonical, -quantity, &new_qty);
  if (rc != 0)
    {
      send_response_error (ctx, root, 500, "Failed to update ship cargo.");
      return 0;
    }

  // Construct response with remaining cargo
  payload = json_object ();
  json_t *remaining_cargo_array = json_array ();

  // Restoring basic logic for remaining cargo report
  int cur_ore=0, cur_org=0, cur_eq=0, cur_holds=0, cur_colonists=0;
  int cur_slaves=0, cur_weapons=0, cur_drugs=0;

  if (h_get_ship_cargo_and_holds (db, player_ship_id,
                                  &cur_ore, &cur_org, &cur_eq, &cur_holds,
                                  &cur_colonists,
                                  &cur_slaves, &cur_weapons,
                                  &cur_drugs) == 0)
    {
      struct { const char* name; int qty; } cargos[] = {
          {"ore", cur_ore}, {"organics", cur_org}, {"equipment", cur_eq},
          {"colonists", cur_colonists}, {"slaves", cur_slaves},
          {"weapons", cur_weapons}, {"drugs", cur_drugs}
      };
      for (int i=0; i<7; i++) {
          if (cargos[i].qty > 0) {
              json_t *it = json_object();
              json_object_set_new(it, "commodity", json_string(cargos[i].name));
              json_object_set_new(it, "quantity", json_integer(cargos[i].qty));
              json_array_append_new(remaining_cargo_array, it);
          }
      }
    }

  json_object_set_new (payload, "remaining_cargo", remaining_cargo_array);
  send_response_ok_take (ctx, root, "ship.jettison_receipt", &payload);
  return 0;
}


/* --- Port Robbery --- */
static int
h_robbery_get_config (sqlite3 *db,
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
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db,
                               "SELECT robbery_evil_threshold, robbery_xp_per_hold, robbery_credits_per_xp, "
                               "robbery_bust_chance_base, robbery_turn_cost, good_guy_bust_bonus, "
                               "pro_criminal_bust_delta, evil_cluster_bust_bonus, good_align_penalty_mult, "
                               "robbery_real_bust_ttl_days FROM law_enforcement WHERE id=1;",
                               -1,
                               &st,
                               NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      if (threshold)
        {
          *threshold = sqlite3_column_int (st, 0);
        }
      if (xp_per_hold)
        {
          *xp_per_hold = sqlite3_column_int (st, 1);
        }
      if (cred_per_xp)
        {
          *cred_per_xp = sqlite3_column_int (st, 2);
        }
      if (chance_base)
        {
          *chance_base = sqlite3_column_double (st, 3);
        }
      if (turn_cost)
        {
          *turn_cost = sqlite3_column_int (st, 4);
        }
      if (good_bonus)
        {
          *good_bonus = sqlite3_column_double (st, 5);
        }
      if (pro_delta)
        {
          *pro_delta = sqlite3_column_double (st, 6);
        }
      if (evil_cluster_bonus)
        {
          *evil_cluster_bonus = sqlite3_column_double (st, 7);
        }
      if (good_penalty_mult)
        {
          *good_penalty_mult = sqlite3_column_double (st, 8);
        }
      if (ttl_days)
        {
          *ttl_days = sqlite3_column_int (st, 9);
        }
    }
  else
    {
      // Fallback defaults
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
    }
  sqlite3_finalize (st);
  return SQLITE_OK;
}


int
cmd_port_rob (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db) {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
  }
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "Not authenticated.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_response_error (ctx, root, 400, "Missing data.");
      return 0;
    }

  int sector_id = json_integer_value (json_object_get (data, "sector_id"));
  int port_id = json_integer_value (json_object_get (data, "port_id"));
  const char *mode = json_string_value (json_object_get (data, "mode"));
  const char *commodity = json_string_value (json_object_get (data, "commodity"));

  if (sector_id <= 0 || port_id <= 0 || !mode)
    {
      send_response_error (ctx, root, 400, "Invalid parameters.");
      return 0;
    }

  if (h_get_player_sector (ctx->player_id) != sector_id)
    {
      send_response_error (ctx, root, REF_TURN_COST_EXCEEDS, "You are not in that sector.");
      return 0;
    }

  if (db_get_port_sector (db, port_id) != sector_id)
    {
      send_response_error (ctx, root, REF_AUTOPILOT_RUNNING, "Port not found in sector.");
      return 0;
    }

  // wanted check
  {
      const char *sql = "SELECT 1 FROM port_busts WHERE port_id=$1 AND player_id=$2 AND active=1";
      db_bind_t p[] = { db_bind_i32(port_id), db_bind_i32(ctx->player_id) };
      db_res_t *res = NULL;
      db_error_t err;
      if (db_query(db, sql, p, 2, &res, &err)) {
          if (db_res_step(res, &err)) {
              db_res_finalize(res);
              send_response_refused_steal (ctx, root, REF_TURN_COST_EXCEEDS, "Already wanted at this port.", NULL);
              return 0;
          }
          db_res_finalize(res);
      }
  }

  int cluster_id = db_get_sector_cluster_id(sector_id);
  if (cluster_id > 0) {
      if (db_is_player_banned_in_cluster(cluster_id, ctx->player_id)) {
          send_response_refused_steal (ctx, root, REF_TURN_COST_EXCEEDS, "Banned in cluster.", NULL);
          return 0;
      }
  }

  if (strcmp (mode, "goods") == 0) {
      if (!commodity) {
          send_response_error (ctx, root, 400, "Commodity required.");
          return 0;
      }
      if (h_is_illegal_commodity (db, commodity)) {
          if (!h_can_trade_commodity (db, port_id, ctx->player_id, commodity)) {
              send_response_refused_steal (ctx, root, REF_SAFE_ZONE_ONLY, "Illegal trade restrictions apply.", NULL);
              return 0;
          }
      }
  }

  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);
  if (tc != TURN_CONSUME_SUCCESS) {
      return handle_turn_consumption_error (ctx, tc, "port.rob", root, NULL);
  }

  int cfg_thresh = 0, cfg_xp_hold = 0, cfg_cred_xp = 0, cfg_turn = 0;
  double cfg_base = 0.0, cfg_good_bonus = 0.0, cfg_pro_delta = 0.0, cfg_evil_bonus = 0.0, cfg_good_mult = 0.0;
  h_robbery_get_config (db, &cfg_thresh, &cfg_xp_hold, &cfg_cred_xp, &cfg_base, &cfg_turn, &cfg_good_bonus, &cfg_pro_delta, &cfg_evil_bonus, &cfg_good_mult, NULL);

  int p_align = 0, p_xp = 0;
  db_player_get_alignment (db, ctx->player_id, &p_align);
  p_xp = db_get_player_xp(ctx->player_id);

  int can_rob = 0;
  db_alignment_band_for_value (db, p_align, NULL, NULL, NULL, NULL, NULL, NULL, &can_rob);
  if (!can_rob) {
      send_response_refused_steal (ctx, root, REF_TURN_COST_EXCEEDS, "Alignment prevents robbery.", NULL);
      return 0;
  }

  bool is_good_player = (p_align > cfg_thresh);
  int fake_bust = 0;
  // fake bust logic ... (simplified for now or port logic)
  
  double chance = cfg_base + (is_good_player ? cfg_good_bonus : cfg_pro_delta);
  if (db_is_cluster_evil(db, cluster_id)) chance += cfg_evil_bonus;
  if (chance < 0.01) chance = 0.01;
  if (chance > 0.30) chance = 0.30;

  bool is_real_bust = ((double)rand() / RAND_MAX < chance);

  db_error_t err;
  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) return 0;

  if (!is_real_bust) {
      long long loot_credits = 0;
      json_t *resp_pl = json_object();
      if (strcmp (mode, "credits") == 0) {
          long long max_loot = (long long) p_xp * cfg_cred_xp;
          long long port_cash = db_port_get_petty_cash(db, port_id);
          loot_credits = (max_loot < port_cash) ? max_loot : port_cash;
          if (loot_credits > 0) {
              db_port_add_petty_cash(db, port_id, -loot_credits);
              h_add_player_petty_cash_unlocked(db, ctx->player_id, loot_credits, NULL);
          }
          json_object_set_new(resp_pl, "credits_stolen", json_integer(loot_credits));
      } else {
          int amount = 10;
          int stock = 0;
          db_port_get_goods_on_hand(port_id, commodity, &stock);
          if (stock < amount) {
              db_tx_rollback(db, &err);
              send_response_refused_steal(ctx, root, ERR_AUTOPILOT_PATH_INVALID, "Port out of stock.", NULL);
              json_decref(resp_pl);
              return 0;
          }
          int ship_id = h_get_active_ship_id(db, ctx->player_id);
          int free_space = 0;
          h_get_cargo_space_free(db, ctx->player_id, &free_space);
          if (free_space < amount) {
              db_tx_rollback(db, &err);
              send_response_refused_steal(ctx, root, REF_TURN_COST_EXCEEDS, "No cargo space.", NULL);
              json_decref(resp_pl);
              return 0;
          }
          h_update_port_stock(db, port_id, commodity, -amount, NULL);
          h_update_ship_cargo(db, ship_id, commodity, amount, NULL);
          json_t *gs = json_object();
          json_object_set_new(gs, "commodity", json_string(commodity));
          json_object_set_new(gs, "quantity", json_integer(amount));
          json_object_set_new(resp_pl, "goods_stolen", gs);
      }
      
      long long xp_gain = (strcmp(mode, "credits") == 0) ? (loot_credits / 10) : 5;
      h_player_apply_progress(db, ctx->player_id, xp_gain, -10, "port.rob.success");
      db_tx_commit(db, &err);
      json_object_set_new(resp_pl, "rob_result", json_string("success"));
      send_response_ok_take(ctx, root, "port.rob", &resp_pl);
  } else {
      long long xp_loss = p_xp * 0.05;
      h_player_apply_progress(db, ctx->player_id, -xp_loss, 15, "port.rob.bust");
      // Record bust
      const char *sql_bust = "INSERT INTO port_busts (port_id, player_id, last_bust_at, bust_type, active) VALUES ($1, $2, $3, 'real', 1)";
      db_bind_t p_bust[] = { db_bind_i32(port_id), db_bind_i32(ctx->player_id), db_bind_i64(time(NULL)) };
      db_exec(db, sql_bust, p_bust, 3, &err);
      db_tx_commit(db, &err);
      json_t *resp_pl = json_object();
      json_object_set_new(resp_pl, "rob_result", json_string("real_bust"));
      send_response_ok_take(ctx, root, "port.rob", &resp_pl);
  }
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
  if (!db || port_id <= 0 || !commodity_code)
    {
      if (quantity_out) *quantity_out = 0;
      if (max_capacity_out) *max_capacity_out = 0;
      if (buys_out) *buys_out = false;
      if (sells_out) *sells_out = false;
      return ERR_DB_MISUSE;
    }

  const char *sql =
    "SELECT es.quantity, p.size * 1000 AS max_capacity, "
    "       (CASE WHEN c.code IN ('ORE', 'ORG', 'EQU') THEN 1 ELSE 0 END) AS buys_commodity, "
    "       (CASE WHEN c.code IN ('ORE', 'ORG', 'EQU') THEN 1 ELSE 0 END) AS sells_commodity "
    "FROM ports p "
    "LEFT JOIN entity_stock es ON p.id = es.entity_id AND es.entity_type = 'port' AND es.commodity_code = $2 "
    "LEFT JOIN commodities c ON es.commodity_code = c.code "
    "WHERE p.id = $1 LIMIT 1;";

  db_bind_t params[] = { db_bind_i32(port_id), db_bind_text(commodity_code) };
  db_res_t *res = NULL;
  db_error_t err;

  if (db_query(db, sql, params, 2, &res, &err)) {
      if (db_res_step(res, &err)) {
          if (quantity_out) *quantity_out = db_res_col_i32(res, 0, &err);
          if (max_capacity_out) *max_capacity_out = db_res_col_i32(res, 1, &err);
          if (buys_out) *buys_out = (db_res_col_i32(res, 2, &err) != 0);
          if (sells_out) *sells_out = (db_res_col_i32(res, 3, &err) != 0);
          db_res_finalize(res);
          return 0;
      }
      db_res_finalize(res);
      return ERR_DB_NOT_FOUND;
  }
  return err.code;
}

