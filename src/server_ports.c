#include <string.h>
#include <jansson.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h> // For snprintf
#include <string.h> // For strcasecmp, strdup etc.
#include <math.h>		// For pow() function
#include <stddef.h> // For size_t

/* local includes */
#include "server_ports.h"
#include "database.h"
#include "database_cmd.h"
#include "errors.h"
#include "config.h"
#include "server_envelope.h"
#include "server_cmds.h"
#include "server_players.h"
#include "server_cron.h"
#include "server_log.h"
#include "common.h"
#include "server_universe.h"
#include "db_player_settings.h"
#include "server_clusters.h"
#include "server_clusters.h"


#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif
void idemp_fingerprint_json (json_t * obj, char out[17]);
void iso8601_utc (char out[32]);

/* Forward declarations for static helper functions */
int h_calculate_port_buy_price (sqlite3 * db, int port_id,
				const char *commodity);

#define RULE_REFUSE(_code,_msg,_hint_json) do { send_enveloped_refused(ctx->fd, root, (_code), (_msg), (_hint_json)); goto trade_buy_done; } while (0)


/* Helpers */
static const char *
commodity_to_code (const char *commodity)
{
  if (!commodity || !*commodity)
    return NULL;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return NULL;

  sqlite3_stmt *st = NULL;
  const char *sql =
    "SELECT code FROM commodities WHERE UPPER(code) = UPPER(?1) LIMIT 1;";

  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("commodity_to_code: prepare failed: %s", sqlite3_errmsg (db));
      return NULL;
    }

  sqlite3_bind_text (st, 1, commodity, -1, SQLITE_STATIC);

  const char *canonical_code = NULL;
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      canonical_code = (const char *) sqlite3_column_text (st, 0);
      // NOTE: We are returning a pointer to SQLite's internal string.
      // This is safe as long as the statement is not finalized and the DB is open.
      // Callers should use this immediately or copy it.
    }

  // Do NOT finalize the statement here if canonical_code is returned,
  // as it would invalidate the pointer.
  // The caller must ensure the statement is finalized when done with the code.
  // For now, we'll return a static string for simplicity, but this needs
  // careful consideration for memory management in a real application.
  // For this exercise, we'll assume the caller copies the string if needed.
  // Or, we can strdup it here, but then the caller must free it.
  // Let's strdup for safety.

  char *result = NULL;
  if (canonical_code)
    {
      result = strdup (canonical_code);
    }
  sqlite3_finalize (st);
  return result;
}








/////////// STUBS ///////////////////////

int
cmd_trade_offer (client_ctx_t *ctx, json_t *root)
{
  // NOTE: This command is not defined in PROTOCOL.v2.0.
  // It might have been intended for a player-to-player trade offer system.
  STUB_NIY (ctx, root, "trade.offer");
}

int
cmd_trade_accept (client_ctx_t *ctx, json_t *root)
{
  // NOTE: This command is not defined in PROTOCOL.v2.0.
  // It might have been intended for a player-to-player trade offer system.
  STUB_NIY (ctx, root, "trade.accept");
}

int
cmd_trade_cancel (client_ctx_t *ctx, json_t *root)
{
  // NOTE: This command is not defined in PROTOCOL.v2.0.
  // It might have been intended for a player-to-player trade offer system.
  STUB_NIY (ctx, root, "trade.cancel");
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
    return sqlite3_bind_text (st, idx, s, -1, SQLITE_TRANSIENT);
  return sqlite3_bind_null (st, idx);
}




static int
h_port_buys_commodity (sqlite3 *db, int port_id, const char *commodity)
{
  if (!db || port_id <= 0 || !commodity || !*commodity)
    return 0;

  char *canonical_commodity_code = (char *) commodity_to_code (commodity);
  if (!canonical_commodity_code)
    {
      return 0;			// Invalid or unsupported commodity
    }

  sqlite3_stmt *st = NULL;
  const char *sql = "SELECT "
    "CASE ?2 "
    "WHEN 'ORE' THEN p.ore_on_hand "
    "WHEN 'ORG' THEN p.organics_on_hand "
    "WHEN 'EQU' THEN p.equipment_on_hand "
    "WHEN 'SLV' THEN p.slaves_on_hand " // NEW
    "WHEN 'WPN' THEN p.weapons_on_hand " // NEW
    "WHEN 'DRG' THEN p.drugs_on_hand "   // NEW
    "ELSE 0 END AS quantity, "
    "p.size * 1000 AS max_capacity "	/* Using port size as a proxy for max_capacity for now */
    "FROM ports p WHERE p.id = ?1 LIMIT 1;";

  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("h_port_buys_commodity: prepare failed: %s", sqlite3_errmsg (db));
      free (canonical_commodity_code);
      return 0;
    }

  sqlite3_bind_int (st, 1, port_id);
  sqlite3_bind_text (st, 2, canonical_commodity_code, -1, SQLITE_STATIC);

  int buys = 0;
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      int current_quantity = sqlite3_column_int (st, 0);
      int max_capacity = sqlite3_column_int (st, 1);

      if (current_quantity < max_capacity)
	buys = 1;
    }
  else
    {
      // If no entry in port_stock, assume port doesn't trade this commodity or is full
      LOGD
	("h_port_buys_commodity: No port_stock entry for port %d, commodity %s",
	 port_id, canonical_commodity_code);
    }

  sqlite3_finalize (st);
  free (canonical_commodity_code);
  return buys;
}







/**
 * @brief Gets a ship's current cargo and total holds.
 */
int
h_get_ship_cargo_and_holds (sqlite3 *db, int ship_id, int *ore, int *organics,
			    int *equipment, int *holds, int *colonists,
			    int *slaves, int *weapons, int *drugs)
{

  sqlite3_stmt *st = NULL;
  const char *SQL_SEL =
    "SELECT ore, organics, equipment, holds, colonists, slaves, weapons, drugs FROM ships WHERE id = ?1";
  int rc = sqlite3_prepare_v2 (db, SQL_SEL, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st, 1, ship_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *ore = sqlite3_column_int (st, 0);
      *organics = sqlite3_column_int (st, 1);
      *equipment = sqlite3_column_int (st, 2);
      *holds = sqlite3_column_int (st, 3);
      *colonists = sqlite3_column_int (st, 4);
      *slaves = sqlite3_column_int (st, 5);
      *weapons = sqlite3_column_int (st, 6);
      *drugs = sqlite3_column_int (st, 7);
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_NOTFOUND;
    }

  sqlite3_finalize (st);
  return rc;
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
h_calculate_port_sell_price (sqlite3 *db, int port_id, const char *commodity)
{
  // LOGD("h_calculate_port_sell_price: Entering with port_id=%d, commodity=%s", port_id, commodity);
  if (!db || port_id <= 0 || !commodity || !*commodity)
    {
      LOGW
	("h_calculate_port_sell_price: Invalid input: db=%p, port_id=%d, commodity=%s",
	 db, port_id, commodity);
      return 0;
    }

  // Assume commodity is already canonical code
  const char *canonical_commodity = commodity;
  // LOGD("h_calculate_port_sell_price: Canonical commodity for %s is %s", commodity, canonical_commodity);

  sqlite3_stmt *st = NULL;
  const char *sql = "SELECT c.base_price, c.volatility, " "CASE ?2 " "WHEN 'ORE' THEN p.ore_on_hand " "WHEN 'ORG' THEN p.organics_on_hand " "WHEN 'EQU' THEN p.equipment_on_hand " "ELSE 0 END AS quantity, " "p.size * 1000 AS max_capacity, p.techlevel "	/* Using port size as a proxy for max_capacity for now */
    "FROM commodities c JOIN ports p ON p.id = ?1 "
    "WHERE UPPER(c.code) = UPPER(?2) LIMIT 1;";

  int rc_prepare = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc_prepare != SQLITE_OK)
    {
      LOGE ("h_calculate_port_sell_price: prepare failed: %s (rc=%d)",
	    sqlite3_errmsg (db), rc_prepare);
      return 0;
    }
  // LOGD("h_calculate_port_sell_price: SQL prepared successfully for port_id=%d, canonical_commodity=%s", port_id, canonical_commodity);

  int rc_bind_int = sqlite3_bind_int (st, 1, port_id);
  if (rc_bind_int != SQLITE_OK)
    {
      LOGE
	("h_calculate_port_sell_price: bind_int failed for port_id %d: %s (rc=%d)",
	 port_id, sqlite3_errmsg (db), rc_bind_int);
      sqlite3_finalize (st);
      return 0;
    }
  // Bind the canonical commodity code
  int rc_bind_text =
    sqlite3_bind_text (st, 2, canonical_commodity, -1, SQLITE_STATIC);
  if (rc_bind_text != SQLITE_OK)
    {
      LOGE
	("h_calculate_port_sell_price: bind_text failed for canonical_commodity %s: %s (rc=%d)",
	 canonical_commodity, sqlite3_errmsg (db), rc_bind_text);
      sqlite3_finalize (st);
      return 0;
    }
  //LOGD("h_calculate_port_sell_price: Parameters bound for port_id=%d, canonical_commodity=%s", port_id, canonical_commodity);

  int base_price = 0;
  int volatility = 0;
  int quantity = 0;
  int max_capacity = 0;
  int techlevel = 0;
  int rc_step = sqlite3_step (st);

  if (rc_step == SQLITE_ROW)
    {
      base_price = sqlite3_column_int (st, 0);
      volatility = sqlite3_column_int (st, 1);
      quantity = sqlite3_column_int (st, 2);
      max_capacity = sqlite3_column_int (st, 3);
      techlevel = sqlite3_column_int (st, 4);
      // LOGD("h_calculate_port_sell_price: Data found for port_id=%d, canonical_commodity=%s: base_price=%d, quantity=%d, max_capacity=%d", port_id, canonical_commodity, base_price, quantity, max_capacity);
    }
  else
    {
      LOGW
	("h_calculate_port_sell_price: No data found for canonical_commodity %s at port %d (rc_step=%d, errmsg=%s)",
	 canonical_commodity, port_id, rc_step, sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return 0;			// Port not selling this commodity or no stock info
    }

  sqlite3_finalize (st);

  if (base_price <= 0)
    {
      LOGW
	("h_calculate_port_sell_price: Base price is zero or less for canonical_commodity %s at port %d",
	 canonical_commodity, port_id);
      return 0;
    }

  double price_multiplier = 1.0;

  if (max_capacity > 0)
    {
      double fill_ratio = (double) quantity / max_capacity;
      // Port sells (player buys): price is higher when supply is low
      // If quantity is low (e.g., < 50% of max_capacity), price increases.
      // If quantity is high (e.g., > 50% of max_capacity), price decreases towards base.
      if (fill_ratio < 0.5)
	{
	  // Price increases as supply decreases
	  price_multiplier = 1.0 + (1.0 - fill_ratio) * (volatility / 100.0);
	}
      else
	{
	  // Price decreases towards base as supply increases
	  price_multiplier = 1.0 - (fill_ratio - 0.5) * (volatility / 100.0);
	}
    }

  // Adjust for techlevel (higher techlevel means better prices for the port, so higher sell price)
  price_multiplier *= (1.0 + (techlevel - 1) * 0.05);

  long long price = (long long) (base_price * price_multiplier + 0.999999);	/* ceil */

  if (price < 1)
    price = 1;
  if (price > 2000000000LL)
    price = 2000000000LL;

  return (int) price;
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
    return 0;

  const char *col = NULL;

  if (strcasecmp (commodity, "ORE") == 0)
    col = "ore_on_hand";
  else if (strcasecmp (commodity, "ORG") == 0)
    col = "organics_on_hand";
  else if (strcasecmp (commodity, "EQU") == 0)
    col = "equipment_on_hand";
  else if (strcasecmp (commodity, "SLV") == 0) // NEW
    col = "slaves_on_hand";
  else if (strcasecmp (commodity, "WPN") == 0) // NEW
    col = "weapons_on_hand";
  else if (strcasecmp (commodity, "DRG") == 0) // NEW
    col = "drugs_on_hand";
  else
    {
      return 0;			/* unsupported commodity */
    }

  char sql[256];
  sqlite3_snprintf (sizeof (sql), sql,
		    "SELECT %s FROM ports WHERE id = ?1 LIMIT 1;", col);

  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      return 0;
    }

  if (sqlite3_bind_int (st, 1, port_id) != SQLITE_OK)
    {
      sqlite3_finalize (st);
      return 0;
    }

  int sells = 0;

  if (sqlite3_step (st) == SQLITE_ROW)
    {
      int qty = sqlite3_column_int (st, 0);
      if (qty > 0)
	sells = 1;
    }

  sqlite3_finalize (st);
  return sells;
}

static int
h_update_credits (sqlite3 *db, const char *owner_type, int owner_id,
		  long long delta, long long *new_balance_out)
{
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;

  // Ensure a bank account exists for the owner (INSERT OR IGNORE)
  const char *SQL_ENSURE_ACCOUNT =
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) VALUES (?1, ?2, 'CRD', 0);";
  rc = sqlite3_prepare_v2 (db, SQL_ENSURE_ACCOUNT, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_update_credits: ENSURE_ACCOUNT prepare error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (st, 1, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, owner_id);
  sqlite3_step (st);		// Execute the insert or ignore
  sqlite3_finalize (st);
  st = NULL;			// Reset statement pointer

  const char *SQL_UPD = "UPDATE bank_accounts "
    "SET balance = balance + ?3 "
    "WHERE owner_type = ?1 AND owner_id = ?2 AND balance + ?3 >= 0 "
    "RETURNING balance;";
  rc = sqlite3_prepare_v2 (db, SQL_UPD, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_update_credits: UPDATE prepare error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }

  sqlite3_bind_text (st, 1, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, owner_id);
  sqlite3_bind_int64 (st, 3, delta);

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      if (new_balance_out)
	*new_balance_out = sqlite3_column_int64 (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      /* This means the WHERE clause failed (insufficient funds) */
      rc = SQLITE_CONSTRAINT;
    }
  else
    {
      LOGE ("h_update_credits: UPDATE step error: %s", sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return rc;
}



/* static int
json_get_int_field (json_t *obj, const char *key, int *out)
{
  json_t *v = json_object_get (obj, key);
  if (!json_is_integer (v))
    return 0;
  *out = (int) json_integer_value (v);
  return 1;
} */



/* New Helpers for Illegal Goods and Cluster Alignment */

/**
 * @brief Retrieves the alignment of the cluster associated with a given sector.
 * @param db The SQLite database handle.
 * @param sector_id The ID of the sector to query.
 * @return The cluster alignment (e.g., +100 for Fed, -100 for Orion, -25 for Ferrengi), 0 if no cluster found.
 */
static int
h_get_cluster_alignment(sqlite3 *db, int sector_id)
{
    sqlite3_stmt *stmt;
    int alignment = 0;
    const char *sql = 
        "SELECT c.alignment FROM clusters c "
        "JOIN cluster_sectors cs ON cs.cluster_id = c.id "
        "WHERE cs.sector_id = ? LIMIT 1";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, sector_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            alignment = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    } else {
        LOGE("h_get_cluster_alignment: Failed to prepare statement: %s", sqlite3_errmsg(db));
    }
    return alignment;
}

/**
 * @brief Checks if a commodity is marked as illegal in the commodities table.
 * @param db The SQLite database handle.
 * @param commodity_code The canonical code of the commodity.
 * @return True if the commodity is illegal, false otherwise.
 */
static bool
h_is_illegal_commodity(sqlite3 *db, const char *commodity_code)
{
    if (!commodity_code) return false;
    sqlite3_stmt *stmt;
    bool illegal = false;
    const char *sql = "SELECT illegal FROM commodities WHERE code = ? LIMIT 1";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, commodity_code, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            illegal = (sqlite3_column_int(stmt, 0) != 0);
        }
        sqlite3_finalize(stmt);
    } else {
        LOGE("h_is_illegal_commodity: Failed to prepare statement: %s", sqlite3_errmsg(db));
    }
    return illegal;
}

/**
 * @brief Returns a pointer to the relevant on_hand quantity field for a commodity in a port.
 * @param db The SQLite database handle.
 * @param port_id The ID of the port.
 * @param commodity_code The canonical code of the commodity.
 * @return A pointer to an integer representing the on_hand quantity, or NULL if not found/supported.
 */
static int*
h_get_port_commodity_on_hand_ptr(sqlite3 *db, int port_id, const char *commodity_code)
{
    (void)db;
    (void)port_id;
    // This is tricky without passing a port_t struct. We'll simulate by returning the current quantity.
    // For update, we'd need to dynamically build SQL.
    // The spec implies a 'port_t' struct with direct access. Since we're working with DB rows,
    // this helper will need to perform DB queries to get and set quantities for different types.
    // For now, let's make it fetch the quantity, and updates will be done via specific SQL.

    // This helper's primary use in can_trade_commodity is to know if the port *can* store it,
    // and for direct stock updates in trade commands.
    // Since we're dealing with individual queries, we'll return a dynamic string for the column name.
    
    // This function can be simplified if port data is loaded into a struct first.
    // For direct DB access, we just need to know the column name.
    if (!commodity_code) return NULL;

    const char *column_name = NULL;
    if (strcasecmp(commodity_code, "ORE") == 0) column_name = "ore_on_hand";
    else if (strcasecmp(commodity_code, "ORG") == 0) column_name = "organics_on_hand";
    else if (strcasecmp(commodity_code, "EQU") == 0) column_name = "equipment_on_hand";
    else if (strcasecmp(commodity_code, "SLV") == 0) column_name = "slaves_on_hand";
    else if (strcasecmp(commodity_code, "WPN") == 0) column_name = "weapons_on_hand";
    else if (strcasecmp(commodity_code, "DRG") == 0) column_name = "drugs_on_hand";
    
    // For now, this function needs to return a way to reference the column.
    // Returning a string of the column name is the most flexible for SQL updates.
    // The previous plan had it returning int*. This is not practical for SQL-centric updates without a struct.
    // So, this helper will provide the column name.

    // For simplicity of direct stock management, this helper will not return a pointer.
    // Instead, the trade logic will construct SQL dynamically using the commodity code.
    // We confirm here if the commodity is one of the types a port can hold.
    if (column_name) return (int*)1; // Not NULL, just indicates it's a known commodity type for ports.
    return NULL;
}


/**
 * @brief Determines if a player can trade a specific commodity at a given port based on alignment rules.
 * @param db The SQLite database handle.
 * @param port_id The ID of the port.
 * @param player_id The ID of the player.
 * @param commodity_code The canonical code of the commodity.
 * @return True if trade is allowed, false otherwise.
 */
static bool
h_can_trade_commodity(sqlite3 *db, int port_id, int player_id, const char *commodity_code)
{
    if (!db || port_id <= 0 || player_id <= 0 || !commodity_code) return false;

    // 1. Check if the commodity is even one a port is designed to store (e.g., no 'FOOD' in ports)
    if (!h_get_port_commodity_on_hand_ptr(db, port_id, commodity_code)) {
        return false;
    }

    // 2. If commodity is not illegal, allow (subject to existing rules)
    if (!h_is_illegal_commodity(db, commodity_code)) {
        return true;
    }

    // From here, we know it's an illegal commodity.
    // 3. Get port's sector and its cluster alignment
    int sector_id = 0;
    sqlite3_stmt *port_sector_stmt;
    if (sqlite3_prepare_v2(db, "SELECT sector FROM ports WHERE id = ? LIMIT 1", -1, &port_sector_stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(port_sector_stmt, 1, port_id);
        if (sqlite3_step(port_sector_stmt) == SQLITE_ROW) {
            sector_id = sqlite3_column_int(port_sector_stmt, 0);
        }
        sqlite3_finalize(port_sector_stmt);
    }
    if (sector_id == 0) return false; // Port not linked to a sector?

    int cluster_alignment = h_get_cluster_alignment(db, sector_id);

    // 4. Check cluster alignment for illegal trade
    if (cluster_alignment >= CLUSTER_GOOD_MIN_ALIGN) {
        // Good cluster – no illegal trade
        return false;
    }
    // Neutral clusters are also assumed to prohibit illegal trade by default
    if (cluster_alignment > CLUSTER_EVIL_MAX_ALIGN) {
        return false;
    }

    // Now we are in an 'evil' cluster (alignment <= CLUSTER_EVIL_MAX_ALIGN)
    // 5. Check player alignment
    int player_alignment = 0;
    db_player_get_alignment(db, player_id, &player_alignment); // Assuming this helper exists

    if (player_alignment > PLAYER_EVIL_ALIGNMENT_THRESHOLD) {
        LOGI("h_can_trade_commodity: Port %d, Player %d, Cmd %s: Player alignment %d not evil enough (threshold %d). Refused.",
             port_id, player_id, commodity_code, player_alignment, PLAYER_EVIL_ALIGNMENT_THRESHOLD);
        return false;
    }

    LOGI("h_can_trade_commodity: Port %d, Player %d, Cmd %s: All conditions met. Allowed.",
         port_id, player_id, commodity_code);
    return true; // Evil player in an evil cluster, trading illegal goods is permitted
}


void free_trade_lines (TradeLine *lines, size_t n)
{
  if (!lines) return;

  for (size_t i = 0; i < n; i++)
    {
      if (lines[i].commodity)
        {
          free (lines[i].commodity);
          lines[i].commodity = NULL; // <--- CRITICAL FIX
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
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      return 0;
    }

  json_t *jport_id = json_object_get (data, "port_id");
  if (json_is_integer (jport_id))
    port_id = (int) json_integer_value (jport_id);

  json_t *jcommodity = json_object_get (data, "commodity");
  if (json_is_string (jcommodity))
    commodity = json_string_value (jcommodity);

  json_t *jquantity = json_object_get (data, "quantity");
  if (json_is_integer (jquantity))
    quantity = (int) json_integer_value (jquantity);

  if (port_id <= 0 || !commodity || quantity <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400,
			    "port_id, commodity, and quantity are required.");
      return 0;
    }

  // Validate commodity using commodity_to_code
  const char *commodity_code = commodity_to_code (commodity);
  if (!commodity_code)
    {
      send_enveloped_error (ctx->fd, root, 400, "Invalid commodity.");
      return 0;
    }

  // Calculate the price the port will CHARGE the player (player's buy price)
  int player_buy_price_per_unit =
    h_calculate_port_sell_price (db, port_id, commodity_code);
  long long total_player_buy_price =
    (long long) player_buy_price_per_unit * quantity;

  // Calculate the price the port will PAY the player (player's sell price)
  int player_sell_price_per_unit =
    h_calculate_port_buy_price (db, port_id, commodity_code);
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

  send_enveloped_ok (ctx->fd, root, "trade.quote", payload);
  json_decref (payload);

  // Free the commodity_code
  free ((char *) commodity_code);	// Cast to char* because strdup returns char*

  return 0;
}


int
cmd_trade_history (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  int rc = SQLITE_OK;
  json_t *data = json_object_get (root, "data");

  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  // 1. Get parameters
  const char *cursor = json_string_value (json_object_get (data, "cursor"));
  int limit = json_integer_value (json_object_get (data, "limit"));
  if (limit <= 0 || limit > 50)
    {
      limit = 20;		// Default or maximum
    }

  // 2. Prepare the query and cursor parameters
  const char *sql_base =
    "SELECT timestamp, id, port_id, commodity, units, price_per_unit, action "
    "FROM trade_log WHERE player_id = ?1 ";

  const char *sql_cursor_cond =
    "AND (timestamp < ?3 OR (timestamp = ?3 AND id < ?4)) ";

  const char *sql_suffix = "ORDER BY timestamp DESC, id DESC LIMIT ?2;";

  long long cursor_ts = 0;
  long long cursor_id = 0;
  char sql[512] = { 0 };
  sqlite3_stmt *stmt = NULL;

  // Build the SQL query based on the cursor state
  if (cursor && (strlen (cursor) > 0))
    {
      char *sep = strchr ((char *) cursor, '_');
      if (sep)
	{
	  *sep = '\0';		// Temporarily null-terminate the timestamp part
	  cursor_ts = atoll (cursor);
	  cursor_id = atoll (sep + 1);
	  *sep = '_';		// Restore original cursor string

	  if (cursor_ts > 0 && cursor_id > 0)
	    {
	      // Query with cursor condition
	      snprintf (sql, sizeof (sql), "%s%s%s", sql_base,
			sql_cursor_cond, sql_suffix);
	    }
	}
    }

  if (sql[0] == 0)
    {
      // Query without cursor condition (first page)
      snprintf (sql, sizeof (sql), "%s%s", sql_base, sql_suffix);
    }

  // 3. Bind parameters
  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("trade.history prepare error: %s", sqlite3_errmsg (db));
      send_enveloped_error (ctx->fd, root, 1500, "Database error");
      return 0;
    }

  sqlite3_bind_int (stmt, 1, ctx->player_id);
  sqlite3_bind_int (stmt, 2, limit + 1);	// Fetch LIMIT + 1 to check for next page

  if (cursor_ts > 0 && cursor_id > 0)
    {
      sqlite3_bind_int64 (stmt, 3, cursor_ts);
      sqlite3_bind_int64 (stmt, 4, cursor_id);
    }

  // 4. Fetch results
  json_t *history_array = json_array ();
  int count = 0;
  long long last_ts = 0;
  long long last_id = 0;

  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      if (count < limit)
	{
	  json_array_append_new (history_array,
				 json_pack
				 ("{s:I, s:I, s:i, s:s, s:i, s:f, s:s}",
				  "timestamp", sqlite3_column_int64 (stmt, 0),
				  "id", sqlite3_column_int64 (stmt, 1),
				  "port_id", sqlite3_column_int (stmt, 2),
				  "commodity", sqlite3_column_text (stmt, 3),
				  "units", sqlite3_column_int (stmt, 4),
				  "price_per_unit",
				  sqlite3_column_double (stmt, 5), "action",
				  sqlite3_column_text (stmt, 6)));

	  last_ts = sqlite3_column_int64 (stmt, 0);
	  last_id = sqlite3_column_int64 (stmt, 1);
	  count++;
	}
      else
	{
	  // We fetched the (limit + 1) row, which is the start of the next page
	  // This row is not added to the array.
	  break;
	}
    }

  sqlite3_finalize (stmt);

  // 5. Build and send response
  json_t *payload = json_object ();
  json_object_set_new (payload, "history", history_array);

  // Check if a next page exists (i.e., we fetched limit + 1 rows)
  if (count == limit && last_id > 0)
    {
      char next_cursor[64];
      snprintf (next_cursor, sizeof (next_cursor), "%lld_%lld", last_ts,
		last_id);
      json_object_set_new (payload, "next_cursor", json_string (next_cursor));
    }


  send_enveloped_ok (ctx->fd, root, "trade.history", payload);

  return 0;
}



int
cmd_dock_status (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = NULL;
  int rc = 0;
  int player_ship_id = 0;
  int resolved_port_id = 0;
  char *player_name = NULL;
  char *ship_name = NULL;
  char *port_name = NULL;
  json_t *payload = NULL;

  if (!ctx || !root)
    return -1;

  db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, 500, "No database handle.");
      return -1;
    }

  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			      "Not authenticated", NULL);
      return 0;
    }

  player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (player_ship_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_NO_ACTIVE_SHIP,
			      "No active ship found.", NULL);
      return 0;
    }

  // Resolve port ID in current sector
  resolved_port_id = db_get_port_id_by_sector (ctx->sector_id);

  if (resolved_port_id > 0 && !cluster_can_trade(db, ctx->sector_id, ctx->player_id))
    {
      send_enveloped_refused (ctx->fd, root, 1403, "Port refuses docking: You are banned in this cluster.", NULL);
      return 0;
    }

  // Update ships.ported status
  {
    sqlite3_stmt *st = NULL;
    const char *sql_update_ported =
      "UPDATE ships SET ported = ?1, onplanet = 0 WHERE id = ?2;"; // Assume docking means not on a planet
    rc = sqlite3_prepare_v2 (db, sql_update_ported, -1, &st, NULL);
    if (rc != SQLITE_OK)
      {
	LOGE ("cmd_dock_status: Failed to prepare update ported statement: %s",
	      sqlite3_errmsg (db));
	send_enveloped_error (ctx->fd, root, ERR_DB, "Database error.");
	return -1;
      }
    sqlite3_bind_int (st, 1, resolved_port_id);
    sqlite3_bind_int (st, 2, player_ship_id);
    if (sqlite3_step (st) != SQLITE_DONE)
      {
	LOGE ("cmd_dock_status: Failed to update ships.ported: %s",
	      sqlite3_errmsg (db));
	sqlite3_finalize (st);
	send_enveloped_error (ctx->fd, root, ERR_DB, "Database error.");
	return -1;
      }
    sqlite3_finalize (st);
  }

  // Generate System Notice if successfully docked at a port
  if (resolved_port_id > 0)
    {
      // Fetch ship name
      db_get_ship_name (db, player_ship_id, &ship_name);
      // Fetch port name
      db_get_port_name (db, resolved_port_id, &port_name);
      // Fetch player name
      db_player_name (ctx->player_id, &player_name);

      char notice_body[512];
      snprintf (notice_body, sizeof (notice_body),
		"Player %s (ID: %d)'s ship '%s' (ID: %d) docked at port '%s' (ID: %d) in Sector %d.",
		player_name ? player_name : "Unknown", ctx->player_id,
		ship_name ? ship_name : "Unknown", player_ship_id,
		port_name ? port_name : "Unknown", resolved_port_id,
		ctx->sector_id);

      db_notice_create ("Docking Log", notice_body, "info",
			time (NULL) + (86400 * 7));	// Expires in 1 week
    }

  payload = json_object ();
  if (!payload)
    {
      send_enveloped_error (ctx->fd, root, ERR_SERVER_ERROR, "Out of memory.");
      goto cleanup;
    }

  json_object_set_new (payload, "port_id", json_integer (resolved_port_id));
  json_object_set_new (payload, "sector_id", json_integer (ctx->sector_id));
  json_object_set_new (payload, "docked", json_boolean (resolved_port_id > 0));

  send_enveloped_ok (ctx->fd, root, "dock.status_v1", payload);
  payload = NULL; /* Ownership transferred to send_enveloped_ok */

cleanup:
  if (player_name)
    free (player_name);
  if (ship_name)
    free (ship_name);
  if (port_name)
    free (port_name);
  if (payload)
    json_decref(payload); // In case of error before ownership transfer
  return 0;
}

int
h_calculate_port_buy_price (sqlite3 *db, int port_id, const char *commodity)
{
  // LOGD("h_calculate_port_buy_price: Entering with port_id=%d, commodity=%s", port_id, commodity);
  if (!db || port_id <= 0 || !commodity || !*commodity)
    {
      LOGW
	("h_calculate_port_buy_price: Invalid input: db=%p, port_id=%d, commodity=%s",
	 db, port_id, commodity);
      return 0;
    }

  // Assume commodity is already canonical code
  const char *canonical_commodity = commodity;
  // LOGI("h_calculate_port_buy_price: Canonical commodity for %s is %s", commodity, canonical_commodity);

  sqlite3_stmt *st = NULL;
  const char *sql = "SELECT c.base_price, c.volatility, " "CASE ?2 " "WHEN 'ORE' THEN p.ore_on_hand " "WHEN 'ORG' THEN p.organics_on_hand " "WHEN 'EQU' THEN p.equipment_on_hand " "ELSE 0 END AS quantity, " "p.size * 1000 AS max_capacity, p.techlevel "	/* Using port size as a proxy for max_capacity for now */
    "FROM commodities c JOIN ports p ON p.id = ?1 "
    "WHERE UPPER(c.code) = UPPER(?2) LIMIT 1;";

  int rc_prepare = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc_prepare != SQLITE_OK)
    {
      LOGE ("h_calculate_port_buy_price: prepare failed: %s (rc=%d)",
	    sqlite3_errmsg (db), rc_prepare);
      return 0;
    }
  // LOGD("h_calculate_port_buy_price: SQL prepared successfully for port_id=%d, canonical_commodity=%s", port_id, canonical_commodity);

  int rc_bind_int = sqlite3_bind_int (st, 1, port_id);
  if (rc_bind_int != SQLITE_OK)
    {
      LOGE
	("h_calculate_port_buy_price: bind_int failed for port_id %d: %s (rc=%d)",
	 port_id, sqlite3_errmsg (db), rc_bind_int);
      sqlite3_finalize (st);
      return 0;
    }
  // Bind the canonical commodity code
  int rc_bind_text =
    sqlite3_bind_text (st, 2, canonical_commodity, -1, SQLITE_STATIC);
  if (rc_bind_text != SQLITE_OK)
    {
      LOGE
	("h_calculate_port_buy_price: bind_text failed for canonical_commodity %s: %s (rc=%d)",
	 canonical_commodity, sqlite3_errmsg (db), rc_bind_text);
      sqlite3_finalize (st);
      return 0;
    }
  // LOGD("h_calculate_port_buy_price: Parameters bound for port_id=%d, canonical_commodity=%s", port_id, canonical_commodity);

  int base_price = 0;
  int volatility = 0;
  int quantity = 0;
  int max_capacity = 0;
  int techlevel = 0;
  int rc_step = sqlite3_step (st);

  if (rc_step == SQLITE_ROW)
    {
      base_price = sqlite3_column_int (st, 0);
      volatility = sqlite3_column_int (st, 1);
      quantity = sqlite3_column_int (st, 2);
      max_capacity = sqlite3_column_int (st, 3);
      techlevel = sqlite3_column_int (st, 4);
      // LOGD("h_calculate_port_buy_price: Data found for port_id=%d, canonical_commodity=%s: base_price=%d, quantity=%d, max_capacity=%d", port_id, canonical_commodity, base_price, quantity, max_capacity);
    }
  else
    {
      LOGW
	("h_calculate_port_buy_price: No data found for canonical_commodity %s at port %d (rc_step=%d, errmsg=%s)",
	 canonical_commodity, port_id, rc_step, sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return 0;			// Port not buying this commodity or no stock info
    }

  sqlite3_finalize (st);

  if (quantity >= max_capacity)
    {
      return 0;			// Port is full, it won't buy.
    }

  if (base_price <= 0)
    {
      LOGW
	("h_calculate_port_buy_price: Base price is zero or less for canonical_commodity %s at port %d",
	 canonical_commodity, port_id);
      return 0;
    }

  double price_multiplier = 1.0;

  if (max_capacity > 0)
    {
      double fill_ratio = (double) quantity / max_capacity;
      // Port buys (player sells): price is lower when supply is high
      // If quantity is high (e.g., > 50% of max_capacity), price decreases.
      // If quantity is low (e.g., < 50% of max_capacity), price increases towards base.
      if (fill_ratio > 0.5)
	{
	  // Price decreases as supply increases
	  price_multiplier = 1.0 - (fill_ratio - 0.5) * (volatility / 100.0);
	}
      else
	{
	  // Price increases towards base as supply decreases
	  price_multiplier = 1.0 + (0.5 - fill_ratio) * (volatility / 100.0);
	}
    }

  // Adjust for techlevel (higher techlevel means better prices for the port, so lower buy price)
  price_multiplier *= (1.0 - (techlevel - 1) * 0.02);	// Port wants to buy low

  long long price = (long long) (base_price * price_multiplier + 0.999999);	/* ceil */

  if (price < 1)
    price = 1;
  if (price > 2000000000LL)
    price = 2000000000LL;

  return (int) price;
}

int
cmd_trade_port_info (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, 500, "No database handle");
      return 0;
    }

  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 0;
  int port_id = 0;

  json_t *data = json_object_get (root, "data");
  LOGD
    ("cmd_trade_port_info: Extracted data object (simplified): is_object: %d",
     json_is_object (data));

  LOGD ("cmd_trade_port_info: About to get jport from data object.");
  json_t *jport = json_object_get (data, "port_id");
  LOGD ("cmd_trade_port_info: After jport extraction: jport (raw): %p",
	(void *) jport);
  if (json_is_integer (jport))
    port_id = (int) json_integer_value (jport);

  LOGD ("cmd_trade_port_info: About to get jsec from data object.");
  json_t *jsec = json_object_get (data, "sector_id");
  LOGD ("cmd_trade_port_info: After jsec extraction: jsec (raw): %p",
	(void *) jsec);
  if (json_is_integer (jsec))
    sector_id = (int) json_integer_value (jsec);

  LOGD ("cmd_trade_port_info: Received port_id=%d, sector_id=%d", port_id,
	sector_id);

  /* Resolve by port_id if supplied */
  const char *sql = NULL;
  if (port_id > 0)
    {
      sql =
	"SELECT id, number, name, sector, size, techlevel, "
	"ore_on_hand, organics_on_hand, equipment_on_hand, "
	"slaves_on_hand, weapons_on_hand, drugs_on_hand, " // NEW
	"petty_cash, type " "FROM ports WHERE id = ?1 LIMIT 1;";
    }
  else if (sector_id > 0)
    {
      sql =
	"SELECT id, number, name, sector, size, techlevel, "
	"ore_on_hand, organics_on_hand, equipment_on_hand, "
	"slaves_on_hand, weapons_on_hand, drugs_on_hand, " // NEW
	"petty_cash, type " "FROM ports WHERE sector = ?1 LIMIT 1;";
    }
  else
    {
      send_enveloped_error (ctx->fd, root, 400,
			    "Missing port_id or sector_id");
      return 0;
    }

  LOGD ("cmd_trade_port_info: Preparing SQL: %s", sql);

  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
      return 0;
    }

  if (port_id > 0)
    {
      sqlite3_bind_int (st, 1, port_id);
      LOGD ("cmd_trade_port_info: Bound port_id: %d", port_id);
    }
  else
    {
      sqlite3_bind_int (st, 1, sector_id);
      LOGD ("cmd_trade_port_info: Bound sector_id: %d", sector_id);
    }

  int rc = sqlite3_step (st);
  LOGD ("cmd_trade_port_info: sqlite3_step returned: %d", rc);
  if (rc != SQLITE_ROW)
    {
      sqlite3_finalize (st);
      send_enveloped_refused (ctx->fd, root, 1404,
			      "No port in this sector", NULL);
      return 0;
    }

  json_t *port = json_object ();
  int col_idx = 0; // Use a counter for column index for clarity

  int port_id_val = sqlite3_column_int(st, col_idx++);
  json_object_set_new (port, "id", json_integer (port_id_val));
  json_object_set_new (port, "number", json_integer (sqlite3_column_int (st, col_idx++)));
  json_object_set_new (port, "name", json_string ((const char *)sqlite3_column_text (st, col_idx++)));
  int sector_id_val = sqlite3_column_int(st, col_idx++);
  json_object_set_new (port, "sector", json_integer (sector_id_val));
  json_object_set_new (port, "size", json_integer (sqlite3_column_int (st, col_idx++)));
  json_object_set_new (port, "techlevel", json_integer (sqlite3_column_int (st, col_idx++)));

  json_object_set_new (port, "ore_on_hand", json_integer (sqlite3_column_int (st, col_idx++)));
  json_object_set_new (port, "organics_on_hand", json_integer (sqlite3_column_int (st, col_idx++)));
  json_object_set_new (port, "equipment_on_hand", json_integer (sqlite3_column_int (st, col_idx++)));

  // Illegal commodities (conditionally added)
  int slaves_on_hand_val = sqlite3_column_int (st, col_idx++);
  if (h_can_trade_commodity(db, port_id_val, ctx->player_id, "SLV")) {
      json_object_set_new (port, "slaves_on_hand", json_integer (slaves_on_hand_val));
  }
  int weapons_on_hand_val = sqlite3_column_int (st, col_idx++);
  if (h_can_trade_commodity(db, port_id_val, ctx->player_id, "WPN")) {
      json_object_set_new (port, "weapons_on_hand", json_integer (weapons_on_hand_val));
  }
  int drugs_on_hand_val = sqlite3_column_int (st, col_idx++);
  if (h_can_trade_commodity(db, port_id_val, ctx->player_id, "DRG")) {
      json_object_set_new (port, "drugs_on_hand", json_integer (drugs_on_hand_val));
  }
  
  // Remaining original columns
  json_object_set_new (port, "petty_cash", json_integer (sqlite3_column_int (st, col_idx++)));
  json_object_set_new (port, "credits", json_integer (sqlite3_column_int (st, col_idx-1))); // credits is alias for petty_cash, so use same index
  json_object_set_new (port, "type", json_integer (sqlite3_column_int (st, col_idx++)));

  sqlite3_finalize (st);

  json_t *payload = json_object ();
  json_object_set_new (payload, "port", port);

  send_enveloped_ok (ctx->fd, root, "trade.port_info", payload);

  json_decref (payload);
  return 0;
}



int
cmd_trade_buy (client_ctx_t *ctx, json_t *root)
{
  LOGD ("cmd_trade_buy: entered for player_id=%d", ctx->player_id);	// ADDED
  sqlite3 *db = NULL;
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

  TradeLine *trade_lines = NULL; // Use the globally defined TradeLine struct
  size_t n = 0; // Initialize n here
  int we_started_tx = 0;

  if (!ctx || !root)
    {
      return -1;
    }

  db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, 500, "No database handle.");
      return -1;
    }

  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      LOGD ("cmd_trade_buy: Not authenticated for player_id=%d", ctx->player_id);	// ADDED
      return 0;
    }

  /* consume turn (may open a transaction) */
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, "trade.buy");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      LOGD ("cmd_trade_buy: Turn consumption failed for player_id=%d, result=%d", ctx->player_id, tc);	// ADDED
      return handle_turn_consumption_error (ctx, tc, "trade.buy", root, NULL);
    }
  LOGD ("cmd_trade_buy: Turn consumed successfully for player_id=%d", ctx->player_id);	// ADDED

  /* input */
  data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      LOGD ("cmd_trade_buy: Missing data object for player_id=%d", ctx->player_id);	// ADDED
      return -1;
    }
  int account_type = db_get_player_pref_int (ctx->player_id, "trade.default_account", 0);	// Default to petty cash (0)
  json_t *jaccount = json_object_get (data, "account");
  if (json_is_integer (jaccount))
    {
      int requested_account_type = (int) json_integer_value (jaccount);
      if (requested_account_type != 0 && requested_account_type != 1)
	{
	  send_enveloped_error (ctx->fd, root, 400,
				"Invalid account type. Must be 0 (petty cash) or 1 (bank).");
	  return -1;
	}
      account_type = requested_account_type;	// Override with explicit request
    }
  LOGD ("cmd_trade_buy: Account type for player_id=%d is %d", ctx->player_id,
	account_type);

  sector_id = ctx->sector_id;
  json_t *jsec = json_object_get (data, "sector_id");
  if (json_is_integer (jsec))
    sector_id = (int) json_integer_value (jsec);
  if (sector_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "Invalid sector_id.");
      LOGD ("cmd_trade_buy: Invalid sector_id=%d for player_id=%d", sector_id, ctx->player_id);	// ADDED
      return -1;
    }
  LOGD ("cmd_trade_buy: Resolved sector_id=%d for player_id=%d", sector_id, ctx->player_id);	// ADDED

  if (!cluster_can_trade(db, sector_id, ctx->player_id))
    {
      send_enveloped_refused (ctx->fd, root, 1403, "Port refuses to trade: You are banned in this cluster.", NULL);
      return 0;
    }


  json_t *jport = json_object_get (data, "port_id");
  if (json_is_integer (jport))
    requested_port_id = (int) json_integer_value (jport);
  LOGD ("cmd_trade_buy: Requested port_id=%d for player_id=%d", requested_port_id, ctx->player_id);	// ADDED


  jitems = json_object_get (data, "items");
  if (!json_is_array (jitems) || json_array_size (jitems) == 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "items[] required.");
      LOGD ("cmd_trade_buy: Missing or empty items array for player_id=%d", ctx->player_id);	// ADDED
      return -1;
    }
  n = json_array_size (jitems); // Set n here
  LOGD ("cmd_trade_buy: Items array present, size=%zu for player_id=%d", n, ctx->player_id);	// ADDED

  json_t *jkey = json_object_get (data, "idempotency_key");
  key = json_is_string (jkey) ? json_string_value (jkey) : NULL;
  if (!key || !*key)
    {
      send_enveloped_error (ctx->fd, root, 400, "idempotency_key required.");
      LOGD ("cmd_trade_buy: Missing idempotency_key for player_id=%d", ctx->player_id);	// ADDED
      return -1;
    }
  LOGD ("cmd_trade_buy: Idempotency key='%s' for player_id=%d", key, ctx->player_id);	// ADDED

  /* idempotency: fast-path */
  LOGD ("cmd_trade_buy: Checking idempotency fast-path for key='%s'", key);	// ADDED
  {
    static const char *SQL_GET =
      "SELECT request_json, response_json "
      "FROM trade_idempotency "
      "WHERE key = ?1 AND player_id = ?2 AND sector_id = ?3;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db, SQL_GET, -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int (st, 2, ctx->player_id);
	sqlite3_bind_int (st, 3, sector_id);

	if (sqlite3_step (st) == SQLITE_ROW)
	  {
	    LOGD ("cmd_trade_buy: Idempotency fast-path: found existing record for key='%s'", key);	// ADDED
	    const unsigned char *req_s_stored = sqlite3_column_text (st, 0);
	    const unsigned char *resp_s_stored = sqlite3_column_text (st, 1);
	    json_error_t jerr;
	    json_t *stored_req =
	      req_s_stored ? json_loads ((const char *) req_s_stored, 0,
					 &jerr) : NULL;

	    json_t *incoming_req = json_incref (data);
	    int same = (stored_req
			&& json_equal_strict (stored_req, incoming_req));
	    json_decref (incoming_req);
	    if (stored_req)
	      json_decref (stored_req);

	    if (same)
	      {
		LOGD ("cmd_trade_buy: Idempotency fast-path: request matches, replaying response for key='%s'", key);	// ADDED
		json_t *stored_resp =
		  resp_s_stored ? json_loads ((const char *) resp_s_stored, 0,
					      &jerr) : NULL;
		sqlite3_finalize (st);
		if (!stored_resp)
		  {
		    send_enveloped_error (ctx->fd, root, 500,
					  "Stored response unreadable.");
		    return -1;
		  }
		send_enveloped_ok (ctx->fd, root, "trade.buy_receipt_v1",
				   stored_resp);
		json_decref (stored_resp);
		return 0;
	      }
	    sqlite3_finalize (st);
	    send_enveloped_error (ctx->fd, root, 1105,
				  "Same idempotency_key used with different request.");
	    LOGD ("cmd_trade_buy: Idempotency fast-path: key='%s' used with different request", key);	// ADDED
	    return -1;
	  }
	sqlite3_finalize (st);
      }
  }
  LOGD ("cmd_trade_buy: Idempotency fast-path: no existing record or no match for key='%s'", key);	// ADDED


  /* 1) Resolve port_id: */
  LOGD ("cmd_trade_buy: Resolving port_id for player_id=%d, requested_port_id=%d, sector_id=%d", ctx->player_id, requested_port_id, sector_id);	// ADDED
  if (requested_port_id > 0)
    {
      static const char *SQL_BY_ID =
	"SELECT id FROM ports WHERE id = ?1 LIMIT 1;";
      sqlite3_stmt *st = NULL;

      if (sqlite3_prepare_v2 (db, SQL_BY_ID, -1, &st, NULL) != SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
	  return -1;
	}

      sqlite3_bind_int (st, 1, requested_port_id);

      rc = sqlite3_step (st);
      if (rc == SQLITE_ROW)
	port_id = sqlite3_column_int (st, 0);

      sqlite3_finalize (st);

      if (port_id <= 0)
	{
	  send_enveloped_error (ctx->fd, root, 1404, "No such port_id.");
	  LOGD ("cmd_trade_buy: No such port_id=%d", requested_port_id);	// ADDED
	  return -1;
	}
    }
  else
    {
      static const char *SQL_BY_SECTOR =
	"SELECT id FROM ports WHERE sector = ?1 LIMIT 1;";
      sqlite3_stmt *st = NULL;

      if (sqlite3_prepare_v2 (db, SQL_BY_SECTOR, -1, &st, NULL) != SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
	  return -1;
	}

      sqlite3_bind_int (st, 1, sector_id);

      rc = sqlite3_step (st);
      if (rc == SQLITE_ROW)
	port_id = sqlite3_column_int (st, 0);

      sqlite3_finalize (st);

      if (port_id <= 0)
	{
	  send_enveloped_error (ctx->fd, root, 1404,
				"No port in this sector.");
	  LOGD ("cmd_trade_buy: No port in sector_id=%d", sector_id);	// ADDED
	  return -1;
	}
    }
  LOGD ("cmd_trade_buy: Resolved port_id=%d for player_id=%d", port_id, ctx->player_id);	// ADDED


  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  LOGD ("cmd_trade_buy: Player ship_id=%d for player_id=%d", player_ship_id, ctx->player_id);	// ADDED
  h_decloak_ship (db, player_ship_id);
  LOGD ("cmd_trade_buy: Ship decloaked for ship_id=%d", player_ship_id);	// ADDED

  /* pre-load credits & cargo (outside tx is fine; final checks are atomic) */
  long long current_credits = 0;
  if (account_type == 0)
    {				// Petty cash
      if (h_get_player_petty_cash (db, ctx->player_id, &current_credits) !=
	  SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 500,
				"Could not read player petty cash.");
	  return -1;
	}
    }
  else
    {				// Bank account
      long long credits_i = 0;
      if (h_get_credits (db, "player", ctx->player_id, &credits_i) !=
	  SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 500,
				"Could not read player bank credits.");
	  return -1;
	}
      current_credits = (long long) credits_i;
    }
  LOGD ("cmd_trade_buy: Player credits (account_type=%d)=%lld for player_id=%d", account_type, current_credits, ctx->player_id);	// ADDED

  int cur_ore, cur_org, cur_eq, cur_holds, cur_colonists, cur_slaves, cur_weapons, cur_drugs; // Declare for new cargo types
  if (h_get_ship_cargo_and_holds (db, player_ship_id,
				  &cur_ore, &cur_org, &cur_eq, &cur_holds,
				  &cur_colonists, &cur_slaves, &cur_weapons, &cur_drugs) != SQLITE_OK) // Pass new cargo types
    {
      send_enveloped_error (ctx->fd, root, 500, "Could not read ship cargo.");
      return -1;
    }
  int current_load = cur_ore + cur_org + cur_eq + cur_colonists + cur_slaves + cur_weapons + cur_drugs; // Update current_load calculation
  LOGD ("cmd_trade_buy: Ship cargo: ore=%d, organics=%d, equipment=%d, holds=%d, current_load=%d for ship_id=%d", cur_ore, cur_org, cur_eq, cur_holds, current_load, player_ship_id);	// ADDED

  n = json_array_size (jitems); // Assign to global n
  trade_lines = calloc (n, sizeof (*trade_lines));
  if (!trade_lines)
    {
      send_enveloped_error (ctx->fd, root, 500, "Memory allocation error.");
      return -1;
    }
  LOGD ("cmd_trade_buy: Allocated trade_lines for %zu items", n);	// ADDED

  /* validate each line & compute totals */
  LOGD ("cmd_trade_buy: Starting trade line validation loop");	// ADDED
  for (size_t i = 0; i < n; i++)
    {
      json_t *it = json_array_get (jitems, i);
      const char *raw_commodity =
	json_string_value (json_object_get (it, "commodity"));
      int amount =
	(int) json_integer_value (json_object_get (it, "quantity"));
      LOGD
	("cmd_trade_buy: Validating item %zu: raw_commodity='%s', amount=%d",
	 i, raw_commodity, amount);

      if (!raw_commodity || amount <= 0)
	{
	  free_trade_lines (trade_lines, n); // Free trade_lines on error
	  send_enveloped_error (ctx->fd, root, 400,
				"items[] must contain {commodity, quantity>0}.");
	  LOGD
	    ("cmd_trade_buy: Invalid item %zu: raw_commodity='%s', amount=%d",
	     i, raw_commodity, amount);
	  return -1;
	}

      char *canonical_commodity = (char *) commodity_to_code (raw_commodity);
      if (!canonical_commodity)
	{
	  free_trade_lines (trade_lines, n); // Free trade_lines on error
	  send_enveloped_refused (ctx->fd, root, 1405,
				  "Invalid or unsupported commodity.", NULL);
	  LOGD ("cmd_trade_buy: Invalid or unsupported commodity '%s'",
		raw_commodity);
	  goto cleanup;
	}
      trade_lines[i].commodity = canonical_commodity;	// Store the strdup'ed canonical code

            if (!h_can_trade_commodity (db, port_id, ctx->player_id, trade_lines[i].commodity))
      	{
      	  free_trade_lines (trade_lines, n); // Free trade_lines on error
      	  RULE_REFUSE (1406, "Forbidden: Illegal trade not permitted for this player or port.", NULL);
      	}
      if (!h_port_sells_commodity (db, port_id, trade_lines[i].commodity))
	{
	  free_trade_lines (trade_lines, n); // Free trade_lines on error
	  send_enveloped_refused (ctx->fd, root, 1405,
				  "Port is not selling this commodity right now.",
				  NULL);
	  LOGD ("cmd_trade_buy: Port %d not selling commodity '%s'", port_id,
		trade_lines[i].commodity);
	  goto cleanup;
	}
      LOGD ("cmd_trade_buy: Port %d sells commodity '%s'", port_id,
	    trade_lines[i].commodity);

      int unit_price =
	h_calculate_port_sell_price (db, port_id, trade_lines[i].commodity);
      if (unit_price <= 0)
	{
	  free_trade_lines (trade_lines, n); // Free trade_lines on error
	  send_enveloped_refused (ctx->fd, root, 1405,
				  "Port is not selling this commodity right now.",
				  NULL);
	  // LOGD("cmd_trade_buy: Port %d sell price <= 0 for commodity '%s'", port_id, commodity); // ADDED
	  return 0;
	}
      // LOGD("cmd_trade_buy: Unit price for '%s' at port %d is %d", commodity, port_id, unit_price); // ADDED

      long long line_cost = (long long) amount * (long long) unit_price;
      trade_lines[i].amount = amount;
      trade_lines[i].unit_price = unit_price;
      trade_lines[i].line_cost = line_cost;

      total_item_cost += line_cost;
      total_cargo_space_needed += amount;
    }
  LOGD ("cmd_trade_buy: Finished trade line validation. Total item cost=%lld, total cargo needed=%d", total_item_cost, total_cargo_space_needed);	// MODIFIED
  rc =
    calculate_fees (db, TX_TYPE_TRADE_BUY, total_item_cost, "player",
		    &charges);
  total_cost_with_fees = total_item_cost + charges.fee_total;

  LOGD ("cmd_trade_buy: Total cost with fees: %lld", total_cost_with_fees);

    if (current_credits < total_cost_with_fees)

      {

        free_trade_lines (trade_lines, n); // Free trade_lines on error

        LOGD ("cmd_trade_buy: Insufficient credits: %lld vs %lld",

  	    current_credits, total_cost_with_fees);

        RULE_REFUSE (1402, "Insufficient credits for this purchase.",

  		   json_pack ("{s:I, s:I}", "needed", total_cost_with_fees,

  			      "have", current_credits));

      }
  LOGD ("cmd_trade_buy: Sufficient credits. Player %d has %lld",
	ctx->player_id, current_credits);

  if (current_load + total_cargo_space_needed > cur_holds)
    {
      free_trade_lines (trade_lines, n); // Free trade_lines on error
      LOGD ("cmd_trade_buy: Insufficient cargo space: current_load=%d, needed=%d, holds=%d",
	    current_load, total_cargo_space_needed, cur_holds);
      RULE_REFUSE (1403, "Insufficient cargo space.",
		   json_pack ("{s:I, s:I}", "needed", total_cargo_space_needed,
			      "have", cargo_space_free (ctx))); // Corrected cargo_space_free usage
    }
  LOGD ("cmd_trade_buy: Sufficient cargo space. Player %d has %d free space",
	ctx->player_id, cargo_space_free (ctx)); // Corrected cargo_space_free usage




  receipt = json_object ();
  lines = json_array ();
  if (!receipt || !lines)
    {
      rc = 500;
      goto fail_tx;
    }
  LOGD ("cmd_trade_buy: Receipt and lines JSON objects created.");	// ADDED

  json_object_set_new (receipt, "sector_id", json_integer (sector_id));
  json_object_set_new (receipt, "port_id", json_integer (port_id));
  json_object_set_new (receipt, "player_id", json_integer (ctx->player_id));
  json_object_set_new (receipt, "lines", lines);

  /* apply trades */
  LOGD ("cmd_trade_buy: Starting trade application loop");	// ADDED
  for (size_t i = 0; i < n; i++)
    {
      const char *commodity = trade_lines[i].commodity;
      int amount = trade_lines[i].amount;
      int unit_price = trade_lines[i].unit_price;
      long long line_cost = trade_lines[i].line_cost;
      sqlite3_stmt *st = NULL;
      LOGD ("cmd_trade_buy: Applying trade for item %zu: commodity='%s', amount=%d", i, commodity, amount);	// ADDED

      /* log */
      LOGD ("cmd_trade_buy: Logging trade for item %zu", i);	// ADDED
      static const char *LOG_SQL =
	"INSERT INTO trade_log "
	"(player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) "
	"VALUES (?1, ?2, ?3, ?4, ?5, ?6, 'buy', ?7);";
      if (sqlite3_prepare_v2 (db, LOG_SQL, -1, &st, NULL) != SQLITE_OK)
	{
	  goto sql_err;
	}
      sqlite3_bind_int (st, 1, ctx->player_id);
      sqlite3_bind_int (st, 2, port_id);
      sqlite3_bind_int (st, 3, sector_id);
      sqlite3_bind_text (st, 4, trade_lines[i].commodity, -1, SQLITE_STATIC);
      sqlite3_bind_int (st, 5, amount);
      sqlite3_bind_int (st, 6, unit_price);
      sqlite3_bind_int64 (st, 7, (sqlite3_int64) time (NULL));
      if (sqlite3_step (st) != SQLITE_DONE)
	{
	  sqlite3_finalize (st);
	  goto sql_err;
	}
      sqlite3_finalize (st);
      LOGD ("cmd_trade_buy: Trade logged for item %zu", i);	// ADDED

      /* ship cargo + */
      LOGD ("cmd_trade_buy: Updating ship cargo for item %zu", i);	// ADDED
      int dummy_qty = 0;
      rc =
	h_update_ship_cargo (db, ctx->player_id, trade_lines[i].commodity,
			     amount, &dummy_qty);
      if (rc != SQLITE_OK)
	{
	  if (rc == SQLITE_CONSTRAINT)
	    {
	      send_enveloped_refused (ctx->fd, root, 1403,
				      "Insufficient cargo space (atomic check).",
				      NULL);
	      LOGD ("cmd_trade_buy: Refused: Insufficient cargo space for item %zu", i);	// ADDED
	    }
	  else
	    {
	    }
	  goto fail_tx;
	}
      LOGD ("cmd_trade_buy: Ship cargo updated for item %zu, new_qty=%d", i, dummy_qty);	// ADDED

      /* port stock - */
      LOGD ("cmd_trade_buy: Updating port stock for item %zu", i);	// ADDED
      int dummy_port = 0;
      rc =
	h_update_port_stock (db, port_id, trade_lines[i].commodity, -amount,
			     &dummy_port);
      if (rc != SQLITE_OK)
	{
	  if (rc == SQLITE_CONSTRAINT)
	    {
	      send_enveloped_refused (ctx->fd, root, 1403,
				      "Port is out of stock (atomic check).",
				      NULL);
	      LOGD ("cmd_trade_buy: Refused: Port out of stock for item %zu", i);	// ADDED
	    }
	  else
	    {
	    }
	  goto fail_tx;
	}
      LOGD ("cmd_trade_buy: Port stock updated for item %zu, new_qty=%d", i, dummy_port);	// ADDED

      json_t *jline = json_object ();
      json_object_set_new (jline, "commodity",
			   json_string (trade_lines[i].commodity));
      json_object_set_new (jline, "quantity", json_integer (amount));
      json_object_set_new (jline, "unit_price", json_integer (unit_price));
      json_object_set_new (jline, "value", json_integer (line_cost));
      json_array_append_new (lines, jline);
    }
  LOGD ("cmd_trade_buy: Finished trade application loop");	// ADDED

    /* debit player (atomic helper) */
    {
      if (account_type == 0)
        {                // Petty cash
  	if (we_started_tx)
  	  {
  	    rc = h_deduct_player_petty_cash_unlocked (db, ctx->player_id,
  						      total_cost_with_fees,
  						      &new_balance);
  	  }
  	else
  	  {
  	    rc = h_deduct_player_petty_cash (db, ctx->player_id,
  					     total_cost_with_fees,
  					     &new_balance);
  	  }
        }
      else
        {                // Bank account
	int player_bank_account_id = -1;
	int get_account_rc =
	  h_get_account_id_unlocked (db, "player", ctx->player_id,
				     &player_bank_account_id);
	if (get_account_rc != SQLITE_OK)
	  {
	    LOGE
	      ("cmd_trade_buy: Failed to get player bank account ID for player %d (rc=%d)",
	       ctx->player_id, get_account_rc);
	    rc = get_account_rc;	// Propagate the error
	  }
	else
	  {
	    rc =
	      h_deduct_credits_unlocked (db, player_bank_account_id,
					 total_cost_with_fees, "TRADE_BUY",
					 tx_group_id, &new_balance);
	  }
      }

    if (rc != SQLITE_OK)
      {
	if (rc == SQLITE_CONSTRAINT)
	  {
	    send_enveloped_refused (ctx->fd, root, 1402,
				    "Insufficient credits (atomic check).",
				    NULL);
	    LOGD ("cmd_trade_buy: Refused: Insufficient credits during debit.");	// ADDED
	  }
	else
	  {
	  }
	goto fail_tx;
      }
    json_object_set_new (receipt, "credits_remaining",
			 json_integer (new_balance));
  }
  LOGD ("cmd_trade_buy: Player credits debited. New balance=%lld", json_integer_value (json_object_get (receipt, "credits_remaining")));	// ADDED

  // Add total_item_cost to port's bank account
  {
    long long new_port_balance = 0;
    int port_bank_account_id = -1;
    int get_account_rc =
      h_get_account_id_unlocked (db, "port", port_id, &port_bank_account_id);
    if (get_account_rc != SQLITE_OK)
      {
	LOGE
	  ("cmd_trade_buy: Failed to get port bank account ID for port %d (rc=%d)",
	   port_id, get_account_rc);
	rc = get_account_rc;
	goto fail_tx;
      }
    rc =
      h_add_credits_unlocked (db, port_bank_account_id, total_item_cost,
			      "TRADE_BUY", tx_group_id, &new_port_balance);
    if (rc != SQLITE_OK)
      {
	LOGE ("cmd_trade_buy: Failed to add credits to port %d: %s", port_id,
	      sqlite3_errmsg (db));
	goto fail_tx;
      }
    LOGD ("cmd_trade_buy: Added %lld credits to port %d. New balance=%lld",
	  total_item_cost, port_id, new_port_balance);
  }

  // Add fees to system bank account
  if (charges.fee_to_bank > 0)
    {
      long long new_system_balance = 0;
      int system_bank_account_id = -1;
      int get_account_rc =
	h_get_system_account_id_unlocked (db, "SYSTEM", 0,
					  &system_bank_account_id);
      if (get_account_rc != SQLITE_OK)
	{
	  LOGE ("cmd_trade_buy: Failed to get system bank account ID (rc=%d)",
		get_account_rc);
	  rc = get_account_rc;
	  goto fail_tx;
	}
      rc =
	h_add_credits_unlocked (db, system_bank_account_id,
				charges.fee_to_bank, "TRADE_BUY_FEE",
				tx_group_id, &new_system_balance);
      if (rc != SQLITE_OK)
	{
	  LOGE ("cmd_trade_buy: Failed to add fees to system bank: %s",
		sqlite3_errmsg (db));
	  goto fail_tx;
	}
      LOGD ("cmd_trade_buy: Added %lld fees to system bank. New balance=%lld",
	    charges.fee_to_bank, new_system_balance);
    }

  json_object_set_new (receipt, "total_item_cost",
		       json_integer (total_item_cost));
  json_object_set_new (receipt, "total_cost_with_fees",
		       json_integer (total_cost_with_fees));
  json_object_set_new (receipt, "fees", json_integer (charges.fee_total));

  /* idempotency insert */
  LOGD ("cmd_trade_buy: Attempting idempotency insert for key='%s'", key);	// ADDED
  {
    req_s = json_dumps (data, JSON_COMPACT | JSON_SORT_KEYS);
    resp_s = json_dumps (receipt, JSON_COMPACT | JSON_SORT_KEYS);
    static const char *SQL_PUT =
      "INSERT INTO trade_idempotency "
      "(key, player_id, sector_id, request_json, response_json, created_at) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db, SQL_PUT, -1, &st, NULL) != SQLITE_OK)
      {
	goto sql_err;
      }
    sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, ctx->player_id);
    sqlite3_bind_int (st, 3, sector_id);
    bind_text_or_null (st, 4, req_s);
    bind_text_or_null (st, 5, resp_s);
    sqlite3_bind_int64 (st, 6, (sqlite3_int64) time (NULL));
    if (sqlite3_step (st) != SQLITE_DONE)
      {
	sqlite3_finalize (st);
	LOGD ("cmd_trade_buy: Idempotency insert failed, going to idempotency_race. rc=%d", sqlite3_step (st));	// ADDED
	goto idempotency_race;
      }
    sqlite3_finalize (st);
  }
  LOGD ("cmd_trade_buy: Idempotency insert successful for key='%s'", key);	// ADDED

trade_buy_done:
  if (we_started_tx && commit (db) != SQLITE_OK)
    {
      free_trade_lines (trade_lines, n);
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
      goto cleanup;
    }
  LOGD ("cmd_trade_buy: Transaction committed.");	// ADDED

  send_enveloped_ok (ctx->fd, root, "trade.buy_receipt_v1", receipt);
  LOGD ("cmd_trade_buy: Sent enveloped OK response.");	// ADDED
  goto cleanup;

sql_err:
  send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
fail_tx:
  if (we_started_tx)
    {
      LOGD ("cmd_trade_buy: Rolling back transaction.");	// ADDED
      rollback (db);
    }
  LOGD ("cmd_trade_buy: Going to cleanup from fail_tx.");	// ADDED
  goto cleanup;

idempotency_race:
  LOGD ("cmd_trade_buy: Entered idempotency_race block for key='%s'", key);	// ADDED
  /* transaction already rolled back if we started it; resolve via stored row */
  {
    static const char *SQL_GET2 =
      "SELECT request_json, response_json "
      "FROM trade_idempotency "
      "WHERE key = ?1 AND player_id = ?2 AND sector_id = ?3;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db, SQL_GET2, -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int (st, 2, ctx->player_id);
	sqlite3_bind_int (st, 3, sector_id);
	int rc_select = sqlite3_step (st);	// Capture the return code
	LOGD ("cmd_trade_buy: Idempotency_race SELECT rc_select=%d", rc_select);	// ADDED
	if (rc_select == SQLITE_ROW)	// Only proceed if a row is found
	  {
	    LOGD ("cmd_trade_buy: Idempotency_race: Found existing record.");	// ADDED
	    const unsigned char *req_s_stored = sqlite3_column_text (st, 0);
	    const unsigned char *resp_s_stored = sqlite3_column_text (st, 1);
	    json_error_t jerr;
	    json_t *stored_req =
	      req_s_stored ? json_loads ((const char *) req_s_stored, 0,
					 &jerr) : NULL;
	    int same = (stored_req && json_equal_strict (stored_req, data));
	    if (stored_req)
	      json_decref (stored_req);
	    if (same)
	      {
		LOGD ("cmd_trade_buy: Idempotency_race: Request matches, replaying response.");	// ADDED
		json_t *stored_resp =
		  resp_s_stored ? json_loads ((const char *) resp_s_stored, 0,
					      &jerr) : NULL;
		sqlite3_finalize (st);
		if (!stored_resp)
		  {
		    send_enveloped_error (ctx->fd, root, 500,
					  "Stored response unreadable.");
		    goto cleanup;
                  }
		send_enveloped_ok (ctx->fd, root, "trade.buy_receipt_v1",
				   stored_resp);
		json_decref (stored_resp);
		goto cleanup;
              }
	    LOGD ("cmd_trade_buy: Idempotency_race: Request mismatch.");	// ADDED
	  }
	else if (rc_select != SQLITE_DONE)
	  {			// Handle other errors from sqlite3_step
	  }
	// If rc_select was not SQLITE_ROW (e.g., SQLITE_DONE, SQLITE_BUSY, or other error),
	// or if 'same' was false, we must finalize the statement here.
	sqlite3_finalize (st);
      }
    else
      {
      }
  }
  // This line is reached if the idempotency race could not be resolved (no matching row, or error)
  send_enveloped_error (ctx->fd, root, 500,
			"Could not resolve idempotency race.");
  LOGD ("cmd_trade_buy: Idempotency_race: Could not resolve, sending error.");	// ADDED
  goto cleanup;

cleanup:
  if (trade_lines) free_trade_lines (trade_lines, n); // Ensure freeing here
  if (receipt)
    json_decref (receipt);
  if (req_s)
    free (req_s);
  if (resp_s)
    free (resp_s);
  LOGD ("cmd_trade_buy: Exiting cleanup.");
  return 0;
}




int
cmd_trade_sell (client_ctx_t *ctx, json_t *root)
{
  LOGD ("cmd_trade_sell: entered for player_id=%d", ctx->player_id);	// ADDED
  sqlite3 *db = NULL;
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
  long long total_item_value = 0; // Value of items sold
  long long total_credits_after_fees = 0; // Credits player actually receives
  fee_result_t charges = { 0 };
  long long new_balance = 0;
  char tx_group_id[UUID_STR_LEN];
  h_generate_hex_uuid (tx_group_id, sizeof (tx_group_id));

  TradeLine *trade_lines = NULL;
  size_t n = 0;
  int we_started_tx = 0;

  if (!ctx || !root)
    {
      return -1;
    }

  db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, 500, "No database handle.");
      return -1;
    }

  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      LOGD ("cmd_trade_sell: Not authenticated for player_id=%d", ctx->player_id);	// ADDED
      return 0;
    }

  /* consume turn (may open a transaction) */
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, "trade.sell");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      LOGD ("cmd_trade_sell: Turn consumption failed for player_id=%d, result=%d", ctx->player_id, tc);	// ADDED
      return handle_turn_consumption_error (ctx, tc, "trade.sell", root,
					    NULL);
    }
  LOGD ("cmd_trade_sell: Turn consumed successfully for player_id=%d", ctx->player_id);	// ADDED

  /* decloak */
  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  h_decloak_ship (db, player_ship_id);
  LOGD ("cmd_trade_sell: Ship decloaked for ship_id=%d", player_ship_id);	// ADDED

  data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      LOGD ("cmd_trade_sell: Missing data object for player_id=%d", ctx->player_id);	// ADDED
      return -1;
    }

  int account_type = db_get_player_pref_int (ctx->player_id, "trade.default_account", 0);	// Default to petty cash (0)
  json_t *jaccount = json_object_get (data, "account");
  if (json_is_integer (jaccount))
    {
      int requested_account_type = (int) json_integer_value (jaccount);
      if (requested_account_type != 0 && requested_account_type != 1)
	{
	  send_enveloped_error (ctx->fd, root, 400,
				"Invalid account type. Must be 0 (petty cash) or 1 (bank).");
	  return -1;
	}
      account_type = requested_account_type;	// Override with explicit request
    }
  LOGD ("cmd_trade_sell: Account type for player_id=%d is %d", ctx->player_id,
	account_type);

  sector_id = ctx->sector_id;
  json_t *jsec = json_object_get (data, "sector_id");
  if (json_is_integer (jsec))
    sector_id = (int) json_integer_value (jsec);
  if (sector_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "Invalid sector_id.");
      LOGD ("cmd_trade_sell: Invalid sector_id=%d for player_id=%d", sector_id, ctx->player_id);	// ADDED
      return -1;
    }
  LOGD ("cmd_trade_sell: Resolved sector_id=%d for player_id=%d", sector_id, ctx->player_id);	// ADDED

  if (!cluster_can_trade(db, sector_id, ctx->player_id))
    {
      send_enveloped_refused (ctx->fd, root, 1403, "Port refuses to trade: You are banned in this cluster.", NULL);
      return -1;
    }

  jitems = json_object_get (data, "items");
  if (!json_is_array (jitems) || json_array_size (jitems) == 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "items[] required.");
      LOGD ("cmd_trade_sell: Missing or empty items array for player_id=%d", ctx->player_id);	// ADDED
      return -1;
    }
  n = json_array_size (jitems);
  LOGD ("cmd_trade_sell: Items array present, size=%zu for player_id=%d", n, ctx->player_id);	// ADDED


  json_t *jkey = json_object_get (data, "idempotency_key");
  key = json_is_string (jkey) ? json_string_value (jkey) : NULL;
  if (!key || !*key)
    {
      send_enveloped_error (ctx->fd, root, 400, "idempotency_key required.");
      LOGD ("cmd_trade_sell: Missing idempotency_key for player_id=%d", ctx->player_id);	// ADDED
      return -1;
    }
  LOGD ("cmd_trade_sell: Idempotency key='%s' for player_id=%d", key, ctx->player_id);	// ADDED

  /* idempotency fast-path */
  LOGD ("cmd_trade_sell: Checking idempotency fast-path for key='%s'", key);	// ADDED
  {
    static const char *SQL_GET =
      "SELECT request_json, response_json "
      "FROM trade_idempotency WHERE key = ?1 AND player_id = ?2 AND sector_id = ?3;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db, SQL_GET, -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int (st, 2, ctx->player_id);
	sqlite3_bind_int (st, 3, sector_id);
	if (sqlite3_step (st) == SQLITE_ROW)
	  {
	    LOGD ("cmd_trade_sell: Idempotency fast-path: found existing record for key='%s'", key);	// ADDED
	    const unsigned char *req_s_stored = sqlite3_column_text (st, 0);
	    const unsigned char *resp_s_stored = sqlite3_column_text (st, 1);
	    json_error_t jerr;
	    json_t *stored_req =
	      req_s_stored ? json_loads ((const char *) req_s_stored, 0,
					 &jerr) : NULL;
	    json_t *incoming_req = json_incref (data);
	    int same = (stored_req
			&& json_equal_strict (stored_req, incoming_req));
	    json_decref (incoming_req);
	    if (stored_req)
	      json_decref (stored_req);

	    if (same)
	      {
		LOGD ("cmd_trade_sell: Idempotency fast-path: request matches, replaying response for key='%s'", key);	// ADDED
		json_t *stored_resp =
		  resp_s_stored ? json_loads ((const char *) resp_s_stored, 0,
					      &jerr) : NULL;
		sqlite3_finalize (st);
		if (!stored_resp)
		  {
		    send_enveloped_error (ctx->fd, root, 500,
					  "Stored response unreadable.");
		    return -1;
		  }
		send_enveloped_ok (ctx->fd, root, "trade.sell_receipt_v1",
				   stored_resp);
		json_decref (stored_resp);
		return 0;
	      }
	    sqlite3_finalize (st);
	    send_enveloped_error (ctx->fd, root, 1105,
				  "Same idempotency_key used with different request.");
	    LOGD ("cmd_trade_sell: Idempotency fast-path: key='%s' used with different request", key);	// ADDED
	    return -1;
	  }
	sqlite3_finalize (st);
      }
  }
  LOGD ("cmd_trade_sell: Idempotency fast-path: no existing record or no match for key='%s'", key);	// ADDED


  /* resolve port from sector */
  LOGD ("cmd_trade_sell: Resolving port_id for player_id=%d, sector_id=%d", ctx->player_id, sector_id);	// ADDED
  if (requested_port_id > 0)
    {
      static const char *SQL_BY_ID =
	"SELECT id FROM ports WHERE id = ?1 LIMIT 1;";
      sqlite3_stmt *st = NULL;

      if (sqlite3_prepare_v2 (db, SQL_BY_ID, -1, &st, NULL) != SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
	  return -1;
	}

      sqlite3_bind_int (st, 1, requested_port_id);

      rc = sqlite3_step (st);
      if (rc == SQLITE_ROW)
	port_id = sqlite3_column_int (st, 0);

      sqlite3_finalize (st);

      if (port_id <= 0)
	{
	  send_enveloped_error (ctx->fd, root, 1404, "No such port_id.");
	  LOGD ("cmd_trade_sell: No such port_id=%d", requested_port_id);	// ADDED
	  return -1;
	}
    }
  else
    {
      static const char *SQL_BY_SECTOR =
	"SELECT id FROM ports WHERE sector = ?1 LIMIT 1;";
      sqlite3_stmt *st = NULL;

      if (sqlite3_prepare_v2 (db, SQL_BY_SECTOR, -1, &st, NULL) != SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
	  return -1;
	}

      sqlite3_bind_int (st, 1, sector_id);

      rc = sqlite3_step (st);
      if (rc == SQLITE_ROW)
	port_id = sqlite3_column_int (st, 0);

      sqlite3_finalize (st);

      if (port_id <= 0)
	{
	  send_enveloped_refused (ctx->fd, root, 1404,
				"No port in this sector.", NULL);
	  LOGD ("cmd_trade_sell: No port in sector_id=%d", sector_id);	// ADDED
	  return -1;
	}
    }
  LOGD ("cmd_trade_sell: Resolved port_id=%d for player_id=%d", port_id, ctx->player_id);	// ADDED


  player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  LOGD ("cmd_trade_sell: Player ship_id=%d for player_id=%d", player_ship_id, ctx->player_id);	// ADDED
  h_decloak_ship (db, player_ship_id);
  LOGD ("cmd_trade_sell: Ship decloaked for ship_id=%d", player_ship_id);	// ADDED

  /* pre-load credits & cargo (outside tx is fine; final checks are atomic) */
  long long current_credits = 0;
  if (account_type == 0)
    {				// Petty cash
      if (h_get_player_petty_cash (db, ctx->player_id, &current_credits) !=
	  SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 500,
				"Could not read player petty cash.");
	  return -1;
	}
    }
  else
    {				// Bank account
      long long credits_i = 0;
      if (h_get_credits (db, "player", ctx->player_id, &credits_i) !=
	  SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 500,
				"Could not read player bank credits.");
	  return -1;
	}
      current_credits = (long long) credits_i;
    }
  LOGD ("cmd_trade_sell: Player credits (account_type=%d)=%lld for player_id=%d", account_type, current_credits, ctx->player_id);	// ADDED

  int cur_ore, cur_org, cur_eq, cur_holds, cur_colonists, cur_slaves, cur_weapons, cur_drugs; // Declare for new cargo types
  if (h_get_ship_cargo_and_holds (db, player_ship_id,
				  &cur_ore, &cur_org, &cur_eq, &cur_holds,
				  &cur_colonists, &cur_slaves, &cur_weapons, &cur_drugs) != SQLITE_OK) // Pass new cargo types
    {
      send_enveloped_error (ctx->fd, root, 500, "Could not read ship cargo.");
      return -1;
    }
  int current_load = cur_ore + cur_org + cur_eq + cur_colonists + cur_slaves + cur_weapons + cur_drugs; // Update current_load calculation
  LOGD ("cmd_trade_sell: Ship cargo: ore=%d, organics=%d, equipment=%d, holds=%d, current_load=%d for ship_id=%d", cur_ore, cur_org, cur_eq, cur_holds, current_load, player_ship_id);	// ADDED

  n = json_array_size (jitems);
  trade_lines = calloc (n, sizeof (*trade_lines));
  if (!trade_lines)
    {
      send_enveloped_error (ctx->fd, root, 500, "Memory allocation error.");
      return -1;
    }
  LOGD ("cmd_trade_sell: Allocated trade_lines for %zu items", n);	// ADDED

  /* validate each line & compute totals */
  LOGD ("cmd_trade_sell: Starting trade line validation loop");	// ADDED
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
	  send_enveloped_error (ctx->fd, root, 400,
				"items[] must contain {commodity, quantity>0}.");
	  LOGD
	    ("cmd_trade_sell: Invalid item %zu: raw_commodity='%s', amount=%d",
	     i, raw_commodity, amount);
	  return -1;
	}

      char *canonical_commodity_code =
	(char *) commodity_to_code (raw_commodity);
      if (!canonical_commodity_code)
	{
	  free_trade_lines (trade_lines, n);
	  send_enveloped_refused (ctx->fd, root, 1405,
				  "Invalid or unsupported commodity.", NULL);
	  LOGD ("cmd_trade_sell: Invalid or unsupported commodity '%s'",
		raw_commodity);
	  goto cleanup;
	}

      if (!h_port_buys_commodity (db, port_id, canonical_commodity_code))
	{
	  free_trade_lines (trade_lines, n);
	  send_enveloped_refused (ctx->fd, root, 1405,
				  "Port is not buying this commodity right now.",
				  NULL);
	  LOGD ("cmd_trade_sell: Port %d not buying commodity '%s'", port_id,
		canonical_commodity_code);
	  goto cleanup;
	}
      int buy_price =
	h_calculate_port_buy_price (db, port_id, canonical_commodity_code);
      if (buy_price <= 0)
	{
	  free_trade_lines (trade_lines, n);
	  send_enveloped_refused (ctx->fd, root, 1405,
				  "Port is not buying this commodity right now.",
				  NULL);
	  // LOGD("cmd_trade_sell: Port %d buy price <= 0 for commodity '%s'", port_id, canonical_commodity_code); // ADDED
	  return 0;
	}
      // LOGD("cmd_trade_sell: Unit price for '%s' at port %d is %d", raw_commodity, port_id, buy_price); // ADDED

      /* check cargo */
      int ore, org, eq, holds, colonists, slaves, weapons, drugs; // Declare for new cargo types
      if (h_get_ship_cargo_and_holds (db, player_ship_id,
				      &ore, &org, &eq, &holds, &colonists,
				      &slaves, &weapons, &drugs) != SQLITE_OK) // Pass new cargo types
	{
	  free_trade_lines (trade_lines, n);
	  send_enveloped_error (ctx->fd, root, 500,
				"Could not read ship cargo.");
	  return -1;
	}

      /* int have = 0; */
      /* if (strcasecmp (canonical_commodity_code, "ore") == 0) */
      /* 	have = ore; */
      /* else if (strcasecmp (canonical_commodity_code, "organics") == 0) */
      /* 	have = org; */
      /* else if (strcasecmp (canonical_commodity_code, "equipment") == 0) */
      /* 	have = eq; */
      /* else if (strcasecmp (canonical_commodity_code, "colonists") == 0) */
      /* 	have = colonists; */
      /* else if (!h_can_trade_commodity (db, port_id, ctx->player_id, canonical_commodity_code)) // Check illegal trade for sell */
      /* 	{ */
      /* 	  free_trade_lines (trade_lines, n); */
      /* 	  send_enveloped_refused (ctx->fd, root, 1406, */
      /* 				  "Forbidden: Illegal trade not permitted for this player or port.", */
      /* 				  NULL); */
      /* 	} */
      /* else if (strcasecmp (canonical_commodity_code, "SLV") == 0) // ADDED */
      /* 	have = slaves; */
      /* else if (strcasecmp (canonical_commodity_code, "WPN") == 0) // ADDED */
      /* 	have = weapons; */
      /* else if (strcasecmp (canonical_commodity_code, "DRG") == 0) // ADDED */
      /* 	have = drugs; */

      /* if (have < amount) */
      /* 	{ */
      /* 	  free_trade_lines (trade_lines, n); */
      /* 	  send_enveloped_refused (ctx->fd, root, 1402, */
      /* 				  "You do not carry enough of that commodity.", */
      /* 				  NULL); */
      /* 	  return 0; */
      /* 	} */

      int have = 0;

      /* Map canonical commodity code to what the player is actually carrying */
      if (strcasecmp (canonical_commodity_code, "ore") == 0)
        have = ore;
      else if (strcasecmp (canonical_commodity_code, "organics") == 0)
        have = org;
      else if (strcasecmp (canonical_commodity_code, "equipment") == 0)
        have = eq;
      else if (strcasecmp (canonical_commodity_code, "colonists") == 0)
        have = colonists;
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
              send_enveloped_refused (ctx->fd, root, 1406,
                                      "Forbidden: Illegal trade not permitted for this player or port.",
                                      NULL);
              return 0;  /* IMPORTANT: stop here */
            }

          if (strcasecmp (canonical_commodity_code, "SLV") == 0)
            have = slaves;
          else if (strcasecmp (canonical_commodity_code, "WPN") == 0)
            have = weapons;
          else /* DRG */
            have = drugs;
        }
      else
        {
          /* Unknown / unsupported commodity code */
          free_trade_lines (trade_lines, n);
          send_enveloped_refused (ctx->fd, root, 1401,
                                  "Unknown commodity code.",
                                  NULL);
          return 0;
        }

      if (have < amount)
        {
          free_trade_lines (trade_lines, n);
          send_enveloped_refused (ctx->fd, root, 1402,
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
  LOGD ("cmd_trade_sell: Finished trade line validation. Total item value=%lld", total_item_value);	// ADDED

  rc =
    calculate_fees (db, TX_TYPE_TRADE_SELL, total_item_value, "player",
		    &charges);
  total_credits_after_fees = total_item_value - charges.fee_total;

  if (total_credits_after_fees < 0)
    {
      free_trade_lines (trade_lines, n);
      send_enveloped_refused (ctx->fd, root, 1402,
			      "Selling this would result in negative credits after fees.",
			      NULL);
      return 0;
    }

  /* transactional section: only start/rollback/commit if we're in autocommit */
  if (sqlite3_get_autocommit (db))
    {
      if (begin (db) != SQLITE_OK)
	{
	  free_trade_lines (trade_lines, n);
	  send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
	  return -1;
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
	  "VALUES (?1, ?2, ?3, ?4, ?5, ?6, 'sell', ?7);";
	if (sqlite3_prepare_v2 (db, LOG_SQL, -1, &st, NULL) != SQLITE_OK)
	  goto sql_err;
	sqlite3_bind_int (st, 1, ctx->player_id);
	sqlite3_bind_int (st, 2, port_id);
	sqlite3_bind_int (st, 3, sector_id);
	sqlite3_bind_text (st, 4, commodity, -1, SQLITE_STATIC);
	sqlite3_bind_int (st, 5, amount);
	sqlite3_bind_int (st, 6, buy_price);
	sqlite3_bind_int64 (st, 7, (sqlite3_int64) time (NULL));
	if (sqlite3_step (st) != SQLITE_DONE)
	  {
	    sqlite3_finalize (st);
	    goto sql_err;
	  }
	sqlite3_finalize (st);
      }

      /* update ship cargo (−amount) */
      {
	int new_ship_qty = 0;
	rc =
	  h_update_ship_cargo (db, ctx->player_id, commodity,
			       -amount, &new_ship_qty);
	if (rc != SQLITE_OK)
	  {
	    if (rc == SQLITE_CONSTRAINT)
	      {
		send_enveloped_refused (ctx->fd, root, 1402,
					"You do not carry enough of that commodity (atomic check).",
					NULL);
	      }
	    goto fail_tx;
	  }
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
		send_enveloped_refused (ctx->fd, root, 1403,
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
      {				// Petty cash
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
      {				// Bank account
	int player_bank_account_id = -1;
	int get_account_rc =
	  h_get_account_id_unlocked (db, "player", ctx->player_id,
				     &player_bank_account_id);
	if (get_account_rc != SQLITE_OK)
	  {
	    LOGE
	      ("cmd_trade_sell: Failed to get player bank account ID for player %d (rc=%d)",
	       ctx->player_id, get_account_rc);
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
	send_enveloped_error (ctx->fd, root, 500, "Failed to credit player.");
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
	  ("cmd_trade_sell: Failed to get port bank account ID for port %d (rc=%d)",
	   port_id, get_account_rc);
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
	      port_id, sqlite3_errmsg (db));
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
      int get_account_rc =
	h_get_system_account_id_unlocked (db, "SYSTEM", 0,
					  &system_bank_account_id);
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
		sqlite3_errmsg (db));
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
  {
    req_s = json_dumps (data, JSON_COMPACT | JSON_SORT_KEYS);
    resp_s = json_dumps (receipt, JSON_COMPACT | JSON_SORT_KEYS);
    static const char *SQL_PUT =
      "INSERT INTO trade_idempotency "
      "(key, player_id, sector_id, request_json, response_json, created_at) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db, SQL_PUT, -1, &st, NULL) != SQLITE_OK)
      goto sql_err;
    sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, ctx->player_id);
    sqlite3_bind_int (st, 3, sector_id);
    bind_text_or_null (st, 4, req_s);
    bind_text_or_null (st, 5, resp_s);
    sqlite3_bind_int64 (st, 6, (sqlite3_int64) time (NULL));
    if (sqlite3_step (st) != SQLITE_DONE)
      {
	sqlite3_finalize (st);
	goto idempotency_race;
      }
    sqlite3_finalize (st);
  }

  if (we_started_tx && commit (db) != SQLITE_OK)
    {
      free_trade_lines (trade_lines, n);
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
      goto cleanup;
    }

  send_enveloped_ok (ctx->fd, root, "trade.sell_receipt_v1", receipt);
  goto cleanup;

sql_err:
  send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
fail_tx:
  if (we_started_tx)
    rollback (db);
  goto cleanup;

idempotency_race:
  /* same pattern as buy(): read existing entry and return if match */
  {
    static const char *SQL_GET2 =
      "SELECT request_json, response_json "
      "FROM trade_idempotency "
      "WHERE key = ?1 AND player_id = ?2 AND sector_id = ?3;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db, SQL_GET2, -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int (st, 2, ctx->player_id);
	sqlite3_bind_int (st, 3, sector_id);
	if (sqlite3_step (st) == SQLITE_ROW)
	  {
	    const unsigned char *req_s_stored = sqlite3_column_text (st, 0);
	    const unsigned char *resp_s_stored = sqlite3_column_text (st, 1);
	    json_error_t jerr;
	    json_t *stored_req =
	      req_s_stored ? json_loads ((const char *) req_s_stored, 0,
					 &jerr) : NULL;
	    int same = (stored_req && json_equal_strict (stored_req, data));
	    if (stored_req)
	      json_decref (stored_req);
	    if (same)
	      {
		json_t *stored_resp =
		  resp_s_stored ? json_loads ((const char *) resp_s_stored, 0,
					      &jerr) : NULL;
		sqlite3_finalize (st);
		if (!stored_resp)
		  {
		    send_enveloped_error (ctx->fd, root, 500,
					  "Stored response unreadable.");
		    goto cleanup;
                  }
		send_enveloped_ok (ctx->fd, root, "trade.sell_receipt_v1",
				   stored_resp);
		json_decref (stored_resp);
		goto cleanup;
              }
	  }
	sqlite3_finalize (st);
      }
  }
  send_enveloped_error (ctx->fd, root, 500,
			"Could not resolve idempotency race.");

cleanup:
  if (trade_lines) free_trade_lines (trade_lines, n); // Ensure freeing here
  if (receipt)
    json_decref (receipt);
  if (req_s)
    free (req_s);
  if (resp_s)
    free (resp_s);
  return 0;
}



int
cmd_trade_jettison (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");
  json_t *payload = NULL;
  const char *commodity = NULL;
  int quantity = 0;
  int player_ship_id = 0;
  int rc = 0;

  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (player_ship_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1404, "No active ship found.",
			      NULL);
      return 0;
    }

  h_decloak_ship (db, player_ship_id);

  TurnConsumeResult tc = h_consume_player_turn (db, ctx, "ship.jettison");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "ship.jettison", root,
					    NULL);
    }

  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      return 0;
    }

  json_t *jcommodity = json_object_get (data, "commodity");
  if (json_is_string (jcommodity))
    commodity = json_string_value (jcommodity);

  json_t *jquantity = json_object_get (data, "quantity");
  if (json_is_integer (jquantity))
    quantity = (int) json_integer_value (jquantity);

  if (!commodity || quantity <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400,
			    "commodity and quantity are required, and quantity must be positive.");
      return 0;
    }

  // Check current cargo
  int cur_ore, cur_org, cur_eq, cur_holds, cur_colonists;
  int cur_slaves, cur_weapons, cur_drugs; // New variables for illegal cargo
  if (h_get_ship_cargo_and_holds (db, player_ship_id,
				  &cur_ore, &cur_org, &cur_eq, &cur_holds,
				  &cur_colonists,
				  &cur_slaves, &cur_weapons, &cur_drugs) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, "Could not read ship cargo.");
      return 0;
    }

  int have = 0;
  if (strcasecmp (commodity, "ore") == 0)
    have = cur_ore;
  else if (strcasecmp (commodity, "organics") == 0)
    have = cur_org;
  else if (strcasecmp (commodity, "equipment") == 0)
    have = cur_eq;
  else if (strcasecmp (commodity, "colonists") == 0)
    have = cur_colonists;
  else if (strcasecmp (commodity, "slaves") == 0)
    have = cur_slaves;
  else if (strcasecmp (commodity, "weapons") == 0)
    have = cur_weapons;
  else if (strcasecmp (commodity, "drugs") == 0)
    have = cur_drugs;
  else
    {
      send_enveloped_refused (ctx->fd, root, 1405, "Unknown commodity.",
			      NULL);
      return 0;
    }

  if (have < quantity)
    {
      send_enveloped_refused (ctx->fd, root, 1402,
			      "You do not carry enough of that commodity to jettison.",
			      NULL);
      return 0;
    }

  // Update ship cargo (jettisoning means negative delta)
  int new_qty = 0;
  rc =
    h_update_ship_cargo (db, ctx->player_id, commodity, -quantity, &new_qty);
  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500,
			    "Failed to update ship cargo.");
      return 0;
    }

  // Construct response with remaining cargo
  payload = json_object ();
  json_t *remaining_cargo_array = json_array ();

  // Re-fetch current cargo to ensure accurate remaining_cargo
  if (h_get_ship_cargo_and_holds (db, player_ship_id,
				  &cur_ore, &cur_org, &cur_eq, &cur_holds,
				  &cur_colonists,
				  &cur_slaves, &cur_weapons, &cur_drugs) == SQLITE_OK)
    {
      if (cur_ore > 0)
	json_array_append_new (remaining_cargo_array,
			       json_pack ("{s:s, s:i}", "commodity", "ore",
					  "quantity", cur_ore));
      if (cur_org > 0)
	json_array_append_new (remaining_cargo_array,
			       json_pack ("{s:s, s:i}", "commodity",
					  "organics", "quantity", cur_org));
      if (cur_eq > 0)
	json_array_append_new (remaining_cargo_array,
			       json_pack ("{s:s, s:i}", "commodity",
					  "equipment", "quantity", cur_eq));
      if (cur_colonists > 0)
	json_array_append_new (remaining_cargo_array,
			       json_pack ("{s:s, s:i}", "commodity",
					  "colonists", "quantity",
					  cur_colonists));
      // Add illegal cargo to remaining_cargo_array
      if (cur_slaves > 0)
	json_array_append_new (remaining_cargo_array,
			       json_pack ("{s:s, s:i}", "commodity",
					  "slaves", "quantity", cur_slaves));
      if (cur_weapons > 0)
	json_array_append_new (remaining_cargo_array,
			       json_pack ("{s:s, s:i}", "commodity",
					  "weapons", "quantity", cur_weapons));
      if (cur_drugs > 0)
	json_array_append_new (remaining_cargo_array,
			       json_pack ("{s:s, s:i}", "commodity",
					  "drugs", "quantity", cur_drugs));
    }
  json_object_set_new (payload, "remaining_cargo", remaining_cargo_array);

  send_enveloped_ok (ctx->fd, root, "ship.jettisoned", payload);
  json_decref (payload);

  return 0;
}
