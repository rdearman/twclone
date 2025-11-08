#include <string.h>
#include <jansson.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <time.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <math.h> // For pow() function
/* local includes */
#include "server_ports.h"
#include "database.h"
#include "errors.h"
#include "config.h"
#include "server_envelope.h"
#include "server_cmds.h"
#include "server_players.h"
#include "server_cron.h"
#include "server_log.h"
#include "common.h"
#include "server_universe.h"


#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif


#define RULE_REFUSE(_code,_msg,_hint_json) \
    do { send_enveloped_refused(ctx->fd, root, (_code), (_msg), (_hint_json)); goto trade_buy_done; } while (0)

#define RULE_ERROR(_code,_msg) \
    do { send_enveloped_error(ctx->fd, root, (_code), (_msg)); goto trade_buy_done; } while (0)

void idemp_fingerprint_json (json_t * obj, char out[17]);
void iso8601_utc (char out[32]);

/* Forward declarations for static helper functions */
static int h_calculate_port_buy_price (sqlite3 *db, int port_id, const char *commodity);
static int internal_calculate_buy_price_formula (int base_price, int supply);

/* Helpers */
static int
begin (sqlite3 *db)
{
  return sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
}

static int
commit (sqlite3 *db)
{
  return sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
}

static int
rollback (sqlite3 *db)
{
  return sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
}

int
cmd_trade_port_info (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401,
                              "Not authenticated", NULL);
      return 0;
    }

  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, 500,
                            "No database handle");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  int port_id = 0;
  int sector_id = 0;

  if (json_is_object (data))
    {
      json_t *jport = json_object_get (data, "port_id");
      if (json_is_integer (jport))
        port_id = (int) json_integer_value (jport);

      json_t *jsec = json_object_get (data, "sector_id");
      if (json_is_integer (jsec))
        sector_id = (int) json_integer_value (jsec);
    }

  /* Resolve by port_id if supplied */
  const char *sql = NULL;
  if (port_id > 0)
    {
      sql =
        "SELECT id, number, name, sector, size, techlevel, "
        "max_ore, max_organics, max_equipment, "
        "product_ore, product_organics, product_equipment, "
        "price_index_ore, price_index_organics, price_index_equipment, "
        "credits, type "
        "FROM ports WHERE id = ?1 LIMIT 1;";
    }
  else if (sector_id > 0)
    {
      sql =
        "SELECT id, number, name, sector, size, techlevel, "
        "max_ore, max_organics, max_equipment, "
        "product_ore, product_organics, product_equipment, "
        "price_index_ore, price_index_organics, price_index_equipment, "
        "credits, type "
        "FROM ports WHERE sector = ?1 LIMIT 1;";
    }
  else
    {
      send_enveloped_error (ctx->fd, root, 400,
                            "Missing port_id or sector_id");
      return 0;
    }

  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500,
                            sqlite3_errmsg (db));
      return 0;
    }

  if (port_id > 0)
    sqlite3_bind_int (st, 1, port_id);
  else
    sqlite3_bind_int (st, 1, sector_id);

  int rc = sqlite3_step (st);
  if (rc != SQLITE_ROW)
    {
      sqlite3_finalize (st);
      send_enveloped_refused (ctx->fd, root, 1404,
                              "No port in this sector", NULL);
      return 0;
    }

  json_t *port = json_object ();
  json_object_set_new (port, "id",          json_integer (sqlite3_column_int (st, 0)));
  json_object_set_new (port, "number",      json_integer (sqlite3_column_int (st, 1)));
  json_object_set_new (port, "name",        json_string  ((const char *) sqlite3_column_text (st, 2)));
  json_object_set_new (port, "sector",      json_integer (sqlite3_column_int (st, 3)));
  json_object_set_new (port, "size",        json_integer (sqlite3_column_int (st, 4)));
  json_object_set_new (port, "techlevel",   json_integer (sqlite3_column_int (st, 5)));
  json_object_set_new (port, "max_ore",     json_integer (sqlite3_column_int (st, 6)));
  json_object_set_new (port, "max_organics",json_integer (sqlite3_column_int (st, 7)));
  json_object_set_new (port, "max_equipment",json_integer(sqlite3_column_int (st, 8)));
  json_object_set_new (port, "product_ore", json_integer (sqlite3_column_int (st, 9)));
  json_object_set_new (port, "product_organics", json_integer (sqlite3_column_int (st,10)));
  json_object_set_new (port, "product_equipment", json_integer (sqlite3_column_int (st,11)));
  json_object_set_new (port, "price_index_ore", json_real (sqlite3_column_double (st,12)));
  json_object_set_new (port, "price_index_organics", json_real (sqlite3_column_double (st,13)));
  json_object_set_new (port, "price_index_equipment", json_real (sqlite3_column_double (st,14)));
  json_object_set_new (port, "credits",    json_integer (sqlite3_column_int (st,15)));
  json_object_set_new (port, "type",       json_integer (sqlite3_column_int (st,16)));

  sqlite3_finalize (st);

  json_t *payload = json_object ();
  json_object_set_new (payload, "port", port);

  send_enveloped_ok (ctx->fd, root, "trade.port_info", payload);

  json_decref (payload);
  return 0;
}


static const char *
commodity_to_code (const char *commodity)
{
  if (!commodity || !*commodity)
    return NULL;

  /* Accept canonical codes directly (any case). */
  if (!strcasecmp (commodity, "ORE"))
    return "ORE";
  if (!strcasecmp (commodity, "ORG"))
    return "ORG";
  if (!strcasecmp (commodity, "EQU"))
    return "EQU";
  if (!strcasecmp (commodity, "SLV"))
    return "SLV";
  if (!strcasecmp (commodity, "WPN"))
    return "WPN";
  if (!strcasecmp (commodity, "DRG"))
    return "DRG";

  /* Backwards-compat: map known names to codes (case-insensitive). */
  if (!strcasecmp (commodity, "Ore"))
    return "ORE";
  if (!strcasecmp (commodity, "Organics"))
    return "ORG";
  if (!strcasecmp (commodity, "Equipment"))
    return "EQU";
  if (!strcasecmp (commodity, "Slaves"))
    return "SLV";
  if (!strcasecmp (commodity, "Weapons"))
    return "WPN";
  if (!strcasecmp (commodity, "Drugs"))
    return "DRG";

  /* Unknown string. */
  return NULL;
}








/////////// STUBS ///////////////////////

int
cmd_trade_offer (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "trade.offer");
}

int
cmd_trade_accept (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "trade.accept");
}

int
cmd_trade_cancel (client_ctx_t *ctx, json_t *root)
{
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




/**
 * @brief Checks if a port is buying a specific commodity.
 *
 * Returns:
 *  - 1 if there is a matching port_trade row with mode='buy'
 *  - 0 otherwise (including errors or bad input)
 */
static int
h_port_buys_commodity (sqlite3 *db, int port_id, const char *commodity)
{
  if (!db || port_id <= 0 || !commodity || !*commodity)
    return 0;

  static const char *SQL_SEL =
    "SELECT 1 FROM port_trade "
    "WHERE port_id = ?1 AND commodity = ?2 AND mode = 'buy' "
    "LIMIT 1;";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL_SEL, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      /* Optional: fprintf(stderr, "h_port_buys_commodity: prepare failed: %s\n", sqlite3_errmsg(db)); */
      return 0;
    }

  rc = sqlite3_bind_int (st, 1, port_id);
  if (rc == SQLITE_OK)
    rc = sqlite3_bind_text (st, 2, commodity, -1, SQLITE_TRANSIENT);

  if (rc != SQLITE_OK)
    {
      /* Optional: fprintf(stderr, "h_port_buys_commodity: bind failed: %s\n", sqlite3_errmsg(db)); */
      sqlite3_finalize (st);
      return 0;
    }

  int buys = (sqlite3_step (st) == SQLITE_ROW) ? 1 : 0;

  sqlite3_finalize (st);
  return buys;
}



int
h_update_port_stock (sqlite3 *db, int port_id,
                     const char *commodity, int delta, int *new_qty_out)
{
  if (!commodity || *commodity == '\0')
    return SQLITE_MISUSE;

  char sql_buf[512];
  const char *col_product = NULL;
  const char *col_max = NULL;

  /* 1. Select the correct column names based on the commodity string */
  if (strcmp(commodity, "ore") == 0) {
    col_product = "product_ore";
    col_max = "max_ore";
  } else if (strcmp(commodity, "organics") == 0) {
    col_product = "product_organics";
    col_max = "max_organics";
  } else if (strcmp(commodity, "equipment") == 0) {
    col_product = "product_equipment";
    col_max = "max_equipment";
  } else {
    /* Unknown or unsupported commodity */
    return SQLITE_MISUSE;
  }

  // --- Fetch current stock and max capacity ---
  int current_qty = 0;
  int max_capacity = 0;
  sqlite3_stmt *select_stmt = NULL;
  snprintf(sql_buf, sizeof(sql_buf),
           "SELECT %s, %s FROM ports WHERE id = ?1;",
           col_product, col_max);
  
  int rc = sqlite3_prepare_v2(db, sql_buf, -1, &select_stmt, NULL);
  if (rc != SQLITE_OK) {
    LOGE("h_update_port_stock: SELECT prepare error: %s", sqlite3_errmsg(db));
    return rc;
  }
  sqlite3_bind_int(select_stmt, 1, port_id);
  
  if (sqlite3_step(select_stmt) == SQLITE_ROW) {
    current_qty = sqlite3_column_int(select_stmt, 0);
    max_capacity = sqlite3_column_int(select_stmt, 1);
  } else {
    sqlite3_finalize(select_stmt);
    return SQLITE_NOTFOUND; // Port not found
  }
  sqlite3_finalize(select_stmt);

  // --- Perform C-side checks for underflow/overflow ---
  int potential_new_qty = current_qty + delta;
  
  if (potential_new_qty < 0) {
    LOGD("h_update_port_stock: Underflow detected for port_id=%d, commodity=%s, current_qty=%d, delta=%d", port_id, commodity, current_qty, delta);
    return SQLITE_CONSTRAINT; // Underflow
  }
  
  if (max_capacity > 0 && potential_new_qty > max_capacity) {
    LOGD("h_update_port_stock: Overflow detected for port_id=%d, commodity=%s, current_qty=%d, delta=%d, max_capacity=%d", port_id, commodity, current_qty, delta, max_capacity);
    return SQLITE_CONSTRAINT; // Overflow
  }

  // --- Build and execute the simplified UPDATE query ---
  snprintf(sql_buf, sizeof(sql_buf),
    "UPDATE ports "
    "SET %s = ?2 "
    "WHERE id = ?1 "
    "RETURNING %s;",
    col_product,
    col_product
  );
  LOGD("h_update_port_stock: UPDATE SQL: %s", sql_buf);

  sqlite3_stmt *update_stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql_buf, -1, &update_stmt, NULL);
  if (rc != SQLITE_OK) {
    LOGE("h_update_port_stock: UPDATE prepare error: %s", sqlite3_errmsg(db));
    return rc;
  }

  sqlite3_bind_int(update_stmt, 1, port_id);
  sqlite3_bind_int(update_stmt, 2, potential_new_qty);

  rc = sqlite3_step(update_stmt);

  if (rc == SQLITE_ROW) {
    if (new_qty_out) {
      *new_qty_out = sqlite3_column_int(update_stmt, 0);
    }
    rc = SQLITE_OK;
  } else if (rc == SQLITE_DONE) {
    rc = SQLITE_NOTFOUND; // Should not happen if SELECT found the port
  }
  
  sqlite3_finalize(update_stmt);
  return rc;
}



/**
 * @brief Gets a ship's current cargo and total holds.
 */
int
h_get_ship_cargo_and_holds (sqlite3 *db, int ship_id, int *ore, int *organics,
                            int *equipment, int *holds)
{

  sqlite3_stmt *st = NULL;
  const char *SQL_SEL = "SELECT ore, organics, equipment, holds FROM ships WHERE id = ?1";
  int rc = sqlite3_prepare_v2(db, SQL_SEL, -1, &st, NULL);
  if (rc != SQLITE_OK) return rc;

  sqlite3_bind_int(st, 1, ship_id);
  rc = sqlite3_step(st);
  if (rc == SQLITE_ROW) {
    *ore = sqlite3_column_int(st, 0);
    *organics = sqlite3_column_int(st, 1);
    *equipment = sqlite3_column_int(st, 2);
    *holds = sqlite3_column_int(st, 3);
    rc = SQLITE_OK;
  } else {
    rc = SQLITE_NOTFOUND;
  }

  sqlite3_finalize(st);
  return rc;
}

/**
 * @brief Calculates the price a port will CHARGE the player for a commodity.
 *
 * Uses:
 *   - commodities(code, base_price)
 *   - ports(price_index_ore/organics/equipment)
 *
 * Formula:
 *   price = ceil(base_price * price_index_X)
 *
 * Returns:
 *   >0 : valid price
 *   0  : not sellable / misconfigured
 */
static int
h_calculate_port_sell_price (sqlite3 *db, int port_id, const char *commodity)
{
  if (!db || port_id <= 0 || !commodity || !*commodity)
    return 0;

  const char *code = NULL;
  const char *idx_col = NULL;

  if (strcmp (commodity, "ore") == 0)
    {
      code = "ORE";
      idx_col = "price_index_ore";
    }
  else if (strcmp (commodity, "organics") == 0)
    {
      code = "ORG";
      idx_col = "price_index_organics";
    }
  else if (strcmp (commodity, "equipment") == 0)
    {
      code = "EQU";
      idx_col = "price_index_equipment";
    }
  else
    {
      return 0; /* unsupported commodity */
    }

  char sql[256];
  sqlite3_snprintf (sizeof (sql), sql,
                    "SELECT c.base_price, p.%s "
                    "FROM commodities c, ports p "
                    "WHERE c.code = ?1 AND p.id = ?2 "
                    "LIMIT 1;",
                    idx_col);

  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    return 0;

  if (sqlite3_bind_text (st, 1, code, -1, SQLITE_TRANSIENT) != SQLITE_OK ||
      sqlite3_bind_int (st, 2, port_id) != SQLITE_OK)
    {
      sqlite3_finalize (st);
      return 0;
    }

  int base_price = 0;
  double idx = 0.0;
  int rc = sqlite3_step (st);

  if (rc == SQLITE_ROW)
    {
      base_price = sqlite3_column_int (st, 0);
      idx = sqlite3_column_double (st, 1);
    }

  sqlite3_finalize (st);

  if (base_price <= 0 || idx <= 0.0)
    return 0;

  double raw = (double) base_price * idx;
  long long price = (long long) (raw + 0.999999); /* ceil */

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

  if (strcmp (commodity, "ore") == 0)
    col = "product_ore";
  else if (strcmp (commodity, "organics") == 0)
    col = "product_organics";
  else if (strcmp (commodity, "equipment") == 0)
    col = "product_equipment";
  else
    return 0; /* unsupported commodity */

  char sql[256];
  sqlite3_snprintf (sizeof (sql), sql,
                    "SELECT %s FROM ports WHERE id = ?1 LIMIT 1;", col);

  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    return 0;

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
h_update_player_credits(sqlite3 *db, int player_id, long long delta, long long *new_balance_out)
{
    sqlite3_stmt *st = NULL;
    const char *SQL_UPD = "UPDATE players "
                          "SET credits = credits + ?2 "
                          "WHERE id = ?1 AND credits + ?2 >= 0 "
                          "RETURNING credits;";
    int rc = sqlite3_prepare_v2(db, SQL_UPD, -1, &st, NULL);
    if (rc != SQLITE_OK) return rc;

    sqlite3_bind_int(st, 1, player_id);
    sqlite3_bind_int64(st, 2, delta);

    rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        if (new_balance_out) *new_balance_out = sqlite3_column_int64(st, 0);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        /* This means the WHERE clause failed (insufficient funds) */
        rc = SQLITE_CONSTRAINT;
    }
    sqlite3_finalize(st);
    return rc;
}



static int
json_get_int_field (json_t *obj, const char *key, int *out)
{
  json_t *v = json_object_get (obj, key);
  if (!json_is_integer (v))
    return 0;
  *out = (int) json_integer_value (v);
  return 1;
}



int
cmd_trade_quote (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented: trade.quote");
  return 0;
}

int
cmd_trade_jettison (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db_handle = db_get_handle ();
  h_decloak_ship (db_handle,
		  h_get_active_ship_id (db_handle, ctx->player_id));

  TurnConsumeResult tc =
    h_consume_player_turn (db_handle, ctx, "trade.jettison");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "trade.jettison", root,
					    NULL);
    }

  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }
  send_enveloped_error (ctx->fd, root, 1101,
			"Not implemented: trade.jettison");
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



double
h_calculate_trade_price(int port_id, const char *commodity, int quantity)
{
    sqlite3 *db_handle = db_get_handle();
    sqlite3_stmt *stmt = NULL;
    double price_index = 1.0;
    double base_price = 0.0;
    char port_type[2] = {'\0', '\0'};
    
    const char *index_column = NULL;
    const char *max_stock_column = NULL;
    int max_stock = 10000; // Default max stock

    // =========================================================
    // 1. Acquire Mutex Lock 
    // =========================================================
    pthread_mutex_lock(&db_mutex); 

    // Safety check for impossible quantity (keep as is)
    if (quantity < 0) {
        pthread_mutex_unlock(&db_mutex);
        return 1000.0;
    }

    // --- 1a. Map commodity to dynamic columns ---
    if (strcmp(commodity, "ore") == 0) {
        index_column = "price_index_ore";
        max_stock_column = "max_ore";
    } else if (strcmp(commodity, "organics") == 0) {
        index_column = "price_index_organics";
        max_stock_column = "max_organics";
    } else if (strcmp(commodity, "equipment") == 0) {
        index_column = "price_index_equipment";
        max_stock_column = "max_equipment";
    } else if (strcmp(commodity, "fuel") == 0) {
        index_column = "price_index_fuel";
        max_stock_column = "max_ore"; // NOTE: Adjust if 'max_fuel' exists later
    } else {
        LOGE("h_calculate_trade_price: Unknown commodity %s. Cannot calculate price.", commodity);
        pthread_mutex_unlock(&db_mutex);
        return 10.0; 
    }

    // --- 1b. Fetch Base Price from 'commodities' table (kept as is) ---
    const char *sql_base_price = "SELECT base_price FROM commodities WHERE name = ?;";
    
    if (sqlite3_prepare_v2(db_handle, sql_base_price, -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, commodity, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            base_price = sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
        //LOGI("BASE PRICE: %f", base_price); // Changed %d to %f for double
    } else {
        LOGE("DB Error in h_calculate_trade_price (base price lookup): %s", sqlite3_errmsg(db_handle));
        pthread_mutex_unlock(&db_mutex);
        return 10.0; 
    }
    
    if (base_price <= 0.0) {
        LOGE("h_calculate_trade_price: Commodity %s not defined or base price is zero.", commodity);
        pthread_mutex_unlock(&db_mutex);
        return 10.0; 
    }

    // --- 2. Fetch Port Data (FIXED: Uses dynamic columns from 'ports') ---
    char sql_port_query[256];
    // Select the generic 'type' (which you called port_type) and the specific price index/max stock.
    snprintf(sql_port_query, sizeof(sql_port_query),
             "SELECT type, %s, %s FROM ports WHERE id = ?;", 
             index_column, max_stock_column);

    if (sqlite3_prepare_v2(db_handle, sql_port_query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, port_id);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            // Read port_type (INTEGER, column 0) - assuming 'type' is INTEGER in schema
            int type_int = sqlite3_column_int(stmt, 0);
            port_type[0] = (char)('0' + type_int); // Convert int type (e.g., 1) to char ('1')
            
            // Read price_index (REAL, column 1)
            price_index = sqlite3_column_double(stmt, 1);
            
            // Read max_stock (INTEGER, column 2)
            max_stock = sqlite3_column_int(stmt, 2);
            
        }
        sqlite3_finalize(stmt);
    } else {
        LOGE("DB Error in h_calculate_trade_price (port lookup): %s", sqlite3_errmsg(db_handle));
        pthread_mutex_unlock(&db_mutex);
        return base_price * 1.0; 
    }
    
    // --- 3. Calculation ---
    
    // Safety check against division by zero
    if (max_stock <= 0) {
        max_stock = 1; 
    }

    // Normalized quantity: 0.0 (empty) to 1.0 (full)
    double normalized_quantity = fmin(1.0, ((double)quantity) / max_stock);
    
    // Volatility Factor (Demand-based pricing: high price when stock is low)
    double fluctuation_factor = pow((1.0 - normalized_quantity), 2.0);

    // Apply the price index
    double indexed_base_price = base_price * price_index;
    
    // The price modifier dictates the maximum deviation from the base price.
    double price_modifier = 0.50; // Max 50% increase from the indexed base price.

    // final_price = Indexed Base Price + (Indexed Base Price * Fluctuation based on Demand)
    double final_price = indexed_base_price + (indexed_base_price * price_modifier * fluctuation_factor);

    // Ensure price is never less than 1.0 credit
    double final_result = fmax(1.0, final_price);

    // =========================================================
    // 4. Release Mutex Lock
    // =========================================================
    pthread_mutex_unlock(&db_mutex); 

    return final_result;
}


int
cmd_trade_buy (client_ctx_t *ctx, json_t *root)
{
  LOGD("cmd_trade_buy: entered for player_id=%d", ctx->player_id); // ADDED
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
  long long total_cost = 0;

  struct TradeLine {
    const char *commodity;
    int amount;
    int unit_price;
    long long line_cost;
  };

  struct TradeLine *trade_lines = NULL;
  int we_started_tx = 0;

  if (!ctx || !root) {
    LOGE("cmd_trade_buy: Invalid context or root JSON."); // ADDED
    return -1;
  }

  db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, 500, "No database handle.");
      LOGE("cmd_trade_buy: No database handle for player_id=%d", ctx->player_id); // ADDED
      return -1;
    }

  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      LOGD("cmd_trade_buy: Not authenticated for player_id=%d", ctx->player_id); // ADDED
      return 0;
    }

  /* consume turn (may open a transaction) */
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, "trade.buy");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      LOGD("cmd_trade_buy: Turn consumption failed for player_id=%d, result=%d", ctx->player_id, tc); // ADDED
      return handle_turn_consumption_error (ctx, tc, "trade.buy", root, NULL);
    }
  LOGD("cmd_trade_buy: Turn consumed successfully for player_id=%d", ctx->player_id); // ADDED

  /* input */
  data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      LOGD("cmd_trade_buy: Missing data object for player_id=%d", ctx->player_id); // ADDED
      return -1;
    }
  LOGD("cmd_trade_buy: Data object present for player_id=%d", ctx->player_id); // ADDED

  sector_id = ctx->sector_id;
  json_t *jsec = json_object_get (data, "sector_id");
  if (json_is_integer (jsec))
    sector_id = (int) json_integer_value (jsec);
  if (sector_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "Invalid sector_id.");
      LOGD("cmd_trade_buy: Invalid sector_id=%d for player_id=%d", sector_id, ctx->player_id); // ADDED
      return -1;
    }
  LOGD("cmd_trade_buy: Resolved sector_id=%d for player_id=%d", sector_id, ctx->player_id); // ADDED


  json_t *jport = json_object_get (data, "port_id");
  if (json_is_integer (jport))
    requested_port_id = (int) json_integer_value (jport);
  LOGD("cmd_trade_buy: Requested port_id=%d for player_id=%d", requested_port_id, ctx->player_id); // ADDED


  jitems = json_object_get (data, "items");
  if (!json_is_array (jitems) || json_array_size (jitems) == 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "items[] required.");
      LOGD("cmd_trade_buy: Missing or empty items array for player_id=%d", ctx->player_id); // ADDED
      return -1;
    }
  LOGD("cmd_trade_buy: Items array present, size=%zu for player_id=%d", json_array_size(jitems), ctx->player_id); // ADDED

  json_t *jkey = json_object_get (data, "idempotency_key");
  key = json_is_string (jkey) ? json_string_value (jkey) : NULL;
  if (!key || !*key)
    {
      send_enveloped_error (ctx->fd, root, 400, "idempotency_key required.");
      LOGD("cmd_trade_buy: Missing idempotency_key for player_id=%d", ctx->player_id); // ADDED
      return -1;
    }
  LOGD("cmd_trade_buy: Idempotency key='%s' for player_id=%d", key, ctx->player_id); // ADDED

  /* idempotency: fast-path */
  LOGD("cmd_trade_buy: Checking idempotency fast-path for key='%s'", key); // ADDED
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
            LOGD("cmd_trade_buy: Idempotency fast-path: found existing record for key='%s'", key); // ADDED
            const unsigned char *req_s_stored = sqlite3_column_text (st, 0);
            const unsigned char *resp_s_stored = sqlite3_column_text (st, 1);
            json_error_t jerr;
            json_t *stored_req =
              req_s_stored ? json_loads ((const char *) req_s_stored, 0, &jerr) : NULL;

            json_t *incoming_req = json_incref (data);
            int same = (stored_req && json_equal_strict (stored_req, incoming_req));
            json_decref (incoming_req);
            if (stored_req)
              json_decref (stored_req);

            if (same)
              {
                LOGD("cmd_trade_buy: Idempotency fast-path: request matches, replaying response for key='%s'", key); // ADDED
                json_t *stored_resp =
                  resp_s_stored ? json_loads ((const char *) resp_s_stored, 0, &jerr) : NULL;
                sqlite3_finalize (st);
                if (!stored_resp)
                  {
                    send_enveloped_error (ctx->fd, root, 500,
                                          "Stored response unreadable.");
                    LOGE("cmd_trade_buy: Idempotency fast-path: stored response unreadable for key='%s'", key); // ADDED
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
            LOGD("cmd_trade_buy: Idempotency fast-path: key='%s' used with different request", key); // ADDED
            return -1;
          }
        sqlite3_finalize (st);
      }
  }
  LOGD("cmd_trade_buy: Idempotency fast-path: no existing record or no match for key='%s'", key); // ADDED


  /* 1) Resolve port_id: */
  LOGD("cmd_trade_buy: Resolving port_id for player_id=%d, requested_port_id=%d, sector_id=%d", ctx->player_id, requested_port_id, sector_id); // ADDED
  if (requested_port_id > 0)
    {
      static const char *SQL_BY_ID =
        "SELECT id FROM ports WHERE id = ?1 LIMIT 1;";
      sqlite3_stmt *st = NULL;

      if (sqlite3_prepare_v2 (db, SQL_BY_ID, -1, &st, NULL) != SQLITE_OK)
        {
          send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
          LOGE("cmd_trade_buy: Port resolve by ID prepare error: %s", sqlite3_errmsg(db)); // ADDED
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
          LOGD("cmd_trade_buy: No such port_id=%d", requested_port_id); // ADDED
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
          LOGE("cmd_trade_buy: Port resolve by sector prepare error: %s", sqlite3_errmsg(db)); // ADDED
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
          LOGD("cmd_trade_buy: No port in sector_id=%d", sector_id); // ADDED
          return -1;
        }
    }
  LOGD("cmd_trade_buy: Resolved port_id=%d for player_id=%d", port_id, ctx->player_id); // ADDED

  
  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  LOGD("cmd_trade_buy: Player ship_id=%d for player_id=%d", player_ship_id, ctx->player_id); // ADDED
  h_decloak_ship (db, player_ship_id);
  LOGD("cmd_trade_buy: Ship decloaked for ship_id=%d", player_ship_id); // ADDED

  /* pre-load credits & cargo (outside tx is fine; final checks are atomic) */
  int credits_i = 0;
  if (h_get_player_credits (db, ctx->player_id, &credits_i) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, "Could not read player credits.");
      LOGE("cmd_trade_buy: Could not read player credits for player_id=%d", ctx->player_id); // ADDED
      return -1;
    }
  long long current_credits = (long long) credits_i;
  LOGD("cmd_trade_buy: Player credits=%lld for player_id=%d", current_credits, ctx->player_id); // ADDED

  int cur_ore, cur_org, cur_eq, cur_holds;
  if (h_get_ship_cargo_and_holds (db, player_ship_id,
                                  &cur_ore, &cur_org, &cur_eq, &cur_holds) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, "Could not read ship cargo.");
      LOGE("cmd_trade_buy: Could not read ship cargo for ship_id=%d", player_ship_id); // ADDED
      return -1;
    }
  int current_load = cur_ore + cur_org + cur_eq;
  LOGD("cmd_trade_buy: Ship cargo: ore=%d, organics=%d, equipment=%d, holds=%d, current_load=%d for ship_id=%d", cur_ore, cur_org, cur_eq, cur_holds, current_load, player_ship_id); // ADDED

  size_t n = json_array_size (jitems);
  trade_lines = calloc (n, sizeof (*trade_lines));
  if (!trade_lines)
    {
      send_enveloped_error (ctx->fd, root, 500, "Memory allocation error.");
      LOGE("cmd_trade_buy: Memory allocation error for trade_lines"); // ADDED
      return -1;
    }
  LOGD("cmd_trade_buy: Allocated trade_lines for %zu items", n); // ADDED

  /* validate each line & compute totals */
  LOGD("cmd_trade_buy: Starting trade line validation loop"); // ADDED
  for (size_t i = 0; i < n; i++)
    {
      json_t *it = json_array_get (jitems, i);
      const char *commodity = json_string_value (json_object_get (it, "commodity"));
      int amount = (int) json_integer_value (json_object_get (it, "quantity"));
      LOGD("cmd_trade_buy: Validating item %zu: commodity='%s', amount=%d", i, commodity, amount); // ADDED

      if (!commodity || amount <= 0
          || (strcmp (commodity, "ore") != 0
              && strcmp (commodity, "organics") != 0
              && strcmp (commodity, "equipment") != 0))
        {
          free (trade_lines);
          send_enveloped_error (ctx->fd, root, 400,
                                "items[] must contain {commodity, quantity>0}.");
          LOGD("cmd_trade_buy: Invalid item %zu: commodity='%s', amount=%d", i, commodity, amount); // ADDED
          return -1;
        }

      if (!h_port_sells_commodity (db, port_id, commodity))
        {
          free (trade_lines);
          send_enveloped_refused (ctx->fd, root, 1405,
                                  "Port is not selling this commodity right now.",
                                  NULL);
          LOGD("cmd_trade_buy: Port %d not selling commodity '%s'", port_id, commodity); // ADDED
          return 0;
        }
      LOGD("cmd_trade_buy: Port %d sells commodity '%s'", port_id, commodity); // ADDED

      int unit_price = h_calculate_port_sell_price (db, port_id, commodity);
      if (unit_price <= 0)
        {
          free (trade_lines);
          send_enveloped_refused (ctx->fd, root, 1405,
                                  "Port is not selling this commodity right now.",
                                  NULL);
          LOGD("cmd_trade_buy: Port %d sell price <= 0 for commodity '%s'", port_id, commodity); // ADDED
          return 0;
        }
      LOGD("cmd_trade_buy: Unit price for '%s' at port %d is %d", commodity, port_id, unit_price); // ADDED

      long long line_cost = (long long) amount * (long long) unit_price;
      trade_lines[i].commodity = commodity;
      trade_lines[i].amount = amount;
      trade_lines[i].unit_price = unit_price;
      trade_lines[i].line_cost = line_cost;

      total_cost += line_cost;
      total_cargo_space_needed += amount;
    }
  LOGD("cmd_trade_buy: Finished trade line validation. Total cost=%lld, total cargo needed=%d", total_cost, total_cargo_space_needed); // ADDED

  if (total_cost > current_credits)
    {
      free (trade_lines);
      send_enveloped_refused (ctx->fd, root, 1402,
                              "Insufficient credits for this purchase.", NULL);
      LOGD("cmd_trade_buy: Insufficient credits: %lld vs %lld", current_credits, total_cost); // ADDED
      return 0;
    }
  LOGD("cmd_trade_buy: Sufficient credits."); // ADDED

  if (current_load + total_cargo_space_needed > cur_holds)
    {
      free (trade_lines);
      send_enveloped_refused (ctx->fd, root, 1403,
                              "Insufficient cargo space for this purchase.", NULL);
      LOGD("cmd_trade_buy: Insufficient cargo space: current_load=%d, needed=%d, holds=%d", current_load, total_cargo_space_needed, cur_holds); // ADDED
      return 0;
    }
  LOGD("cmd_trade_buy: Sufficient cargo space."); // ADDED

  /* transactional section: only start/rollback/commit if we're in autocommit */
  LOGD("cmd_trade_buy: Checking autocommit status."); // ADDED
  if (sqlite3_get_autocommit (db))
    {
      LOGD("cmd_trade_buy: Autocommit is ON, starting transaction."); // ADDED
      if (begin (db) != SQLITE_OK)
        {
          free (trade_lines);
          send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
          LOGE("cmd_trade_buy: Failed to begin transaction: %s", sqlite3_errmsg(db)); // ADDED
          return -1;
        }
      we_started_tx = 1;
      LOGD("cmd_trade_buy: Transaction started."); // ADDED
    }
  else {
      LOGD("cmd_trade_buy: Autocommit is OFF, already in a transaction."); // ADDED
  }

  receipt = json_object ();
  lines = json_array ();
  if (!receipt || !lines)
    {
      rc = 500;
      LOGE("cmd_trade_buy: Failed to create receipt or lines JSON objects."); // ADDED
      goto fail_tx;
    }
  LOGD("cmd_trade_buy: Receipt and lines JSON objects created."); // ADDED

  json_object_set_new (receipt, "sector_id", json_integer (sector_id));
  json_object_set_new (receipt, "port_id", json_integer (port_id));
  json_object_set_new (receipt, "player_id", json_integer (ctx->player_id));
  json_object_set_new (receipt, "lines", lines);

  /* apply trades */
  LOGD("cmd_trade_buy: Starting trade application loop"); // ADDED
  for (size_t i = 0; i < n; i++)
    {
      const char *commodity = trade_lines[i].commodity;
      int amount = trade_lines[i].amount;
      int unit_price = trade_lines[i].unit_price;
      long long line_cost = trade_lines[i].line_cost;
      sqlite3_stmt *st = NULL;
      LOGD("cmd_trade_buy: Applying trade for item %zu: commodity='%s', amount=%d", i, commodity, amount); // ADDED

      /* log */
      LOGD("cmd_trade_buy: Logging trade for item %zu", i); // ADDED
      static const char *LOG_SQL =
        "INSERT INTO trade_log "
        "(player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, 'buy', ?7);";
      if (sqlite3_prepare_v2 (db, LOG_SQL, -1, &st, NULL) != SQLITE_OK) {
        LOGE("cmd_trade_buy: Trade log prepare error: %s", sqlite3_errmsg(db)); // ADDED
        goto sql_err;
      }
      sqlite3_bind_int (st, 1, ctx->player_id);
      sqlite3_bind_int (st, 2, port_id);
      sqlite3_bind_int (st, 3, sector_id);
      sqlite3_bind_text (st, 4, commodity, -1, SQLITE_STATIC);
      sqlite3_bind_int (st, 5, amount);
      sqlite3_bind_int (st, 6, unit_price);
      sqlite3_bind_int64 (st, 7, (sqlite3_int64) time (NULL));
      if (sqlite3_step (st) != SQLITE_DONE)
        {
          sqlite3_finalize (st);
          LOGE("cmd_trade_buy: Trade log execute error: %s", sqlite3_errmsg(db)); // ADDED
          goto sql_err;
        }
      sqlite3_finalize (st);
      LOGD("cmd_trade_buy: Trade logged for item %zu", i); // ADDED

      /* ship cargo + */
      LOGD("cmd_trade_buy: Updating ship cargo for item %zu", i); // ADDED
      int dummy_qty = 0;
      rc = h_update_ship_cargo (db, player_ship_id, commodity, amount, &dummy_qty);
      if (rc != SQLITE_OK)
        {
          if (rc == SQLITE_CONSTRAINT)
            {
              send_enveloped_refused (ctx->fd, root, 1403,
                                      "Insufficient cargo space (atomic check).",
                                      NULL);
              LOGD("cmd_trade_buy: Refused: Insufficient cargo space for item %zu", i); // ADDED
            } else {
              LOGE("cmd_trade_buy: h_update_ship_cargo failed with rc=%d for item %zu", rc, i); // ADDED
            }
          goto fail_tx;
        }
      LOGD("cmd_trade_buy: Ship cargo updated for item %zu, new_qty=%d", i, dummy_qty); // ADDED

      /* port stock - */
      LOGD("cmd_trade_buy: Updating port stock for item %zu", i); // ADDED
      int dummy_port = 0;
      rc = h_update_port_stock (db, port_id, commodity, -amount, &dummy_port);
      if (rc != SQLITE_OK)
        {
          if (rc == SQLITE_CONSTRAINT)
            {
              send_enveloped_refused (ctx->fd, root, 1403,
                                      "Port is out of stock (atomic check).",
                                      NULL);
              LOGD("cmd_trade_buy: Refused: Port out of stock for item %zu", i); // ADDED
            } else {
              LOGE("cmd_trade_buy: h_update_port_stock failed with rc=%d for item %zu", rc, i); // ADDED
            }
          goto fail_tx;
        }
      LOGD("cmd_trade_buy: Port stock updated for item %zu, new_qty=%d", i, dummy_port); // ADDED

      json_t *jline = json_object ();
      json_object_set_new (jline, "commodity", json_string (commodity));
      json_object_set_new (jline, "quantity", json_integer (amount));
      json_object_set_new (jline, "unit_price", json_integer (unit_price));
      json_object_set_new (jline, "value", json_integer (line_cost));
      json_array_append_new (lines, jline);
    }
  LOGD("cmd_trade_buy: Finished trade application loop"); // ADDED

  /* debit credits (atomic helper) */
  LOGD("cmd_trade_buy: Debiting player credits. Total cost=%lld", total_cost); // ADDED
  {
    long long new_balance = 0;
    rc = h_update_player_credits (db, ctx->player_id, -total_cost,
                                  &new_balance);
    if (rc != SQLITE_OK)
      {
        if (rc == SQLITE_CONSTRAINT)
          {
            send_enveloped_refused (ctx->fd, root, 1402,
                                    "Insufficient credits (atomic check).",
                                    NULL);
            LOGD("cmd_trade_buy: Refused: Insufficient credits during debit."); // ADDED
          } else {
            LOGE("cmd_trade_buy: h_update_player_credits failed with rc=%d", rc); // ADDED
          }
        goto fail_tx;
      }
    json_object_set_new (receipt, "credits_remaining",
                         json_integer (new_balance));
  }
  LOGD("cmd_trade_buy: Player credits debited. New balance=%lld", json_integer_value(json_object_get(receipt, "credits_remaining"))); // ADDED

  json_object_set_new (receipt, "total_cost",
                       json_integer (total_cost));

  /* idempotency insert */
  LOGD("cmd_trade_buy: Attempting idempotency insert for key='%s'", key); // ADDED
  {
    req_s = json_dumps (data, JSON_COMPACT | JSON_SORT_KEYS);
    resp_s = json_dumps (receipt, JSON_COMPACT | JSON_SORT_KEYS);
    static const char *SQL_PUT =
      "INSERT INTO trade_idempotency "
      "(key, player_id, sector_id, request_json, response_json, created_at) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db, SQL_PUT, -1, &st, NULL) != SQLITE_OK) {
      LOGE("cmd_trade_buy: Idempotency insert prepare error: %s", sqlite3_errmsg(db)); // ADDED
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
        LOGD("cmd_trade_buy: Idempotency insert failed, going to idempotency_race. rc=%d", sqlite3_step(st)); // ADDED
        goto idempotency_race;
      }
    sqlite3_finalize (st);
  }
  LOGD("cmd_trade_buy: Idempotency insert successful for key='%s'", key); // ADDED

  if (we_started_tx && commit (db) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
      LOGE("cmd_trade_buy: Failed to commit transaction: %s", sqlite3_errmsg(db)); // ADDED
      goto cleanup;
    }
  LOGD("cmd_trade_buy: Transaction committed."); // ADDED

  send_enveloped_ok (ctx->fd, root, "trade.buy_receipt_v1", receipt);
  LOGD("cmd_trade_buy: Sent enveloped OK response."); // ADDED
  goto cleanup;

sql_err:
  send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
  LOGE("cmd_trade_buy: SQL error path: %s", sqlite3_errmsg(db)); // ADDED
fail_tx:
  if (we_started_tx) {
    LOGD("cmd_trade_buy: Rolling back transaction."); // ADDED
    rollback (db);
  }
  LOGD("cmd_trade_buy: Going to cleanup from fail_tx."); // ADDED
  goto cleanup;

 idempotency_race:
  LOGD("cmd_trade_buy: Entered idempotency_race block for key='%s'", key); // ADDED
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
	int rc_select = sqlite3_step (st); // Capture the return code
    LOGD("cmd_trade_buy: Idempotency_race SELECT rc_select=%d", rc_select); // ADDED
	if (rc_select == SQLITE_ROW) // Only proceed if a row is found
	  {
        LOGD("cmd_trade_buy: Idempotency_race: Found existing record."); // ADDED
	    const unsigned char *req_s_stored = sqlite3_column_text (st, 0);
	    const unsigned char *resp_s_stored = sqlite3_column_text (st, 1);
	    json_error_t jerr;
	    json_t *stored_req =
	      req_s_stored ? json_loads ((const char *) req_s_stored, 0, &jerr) : NULL;
	    int same = (stored_req && json_equal_strict (stored_req, data));
	    if (stored_req)
	      json_decref (stored_req);
	    if (same)
	      {
            LOGD("cmd_trade_buy: Idempotency_race: Request matches, replaying response."); // ADDED
		json_t *stored_resp =
		  resp_s_stored ? json_loads ((const char *) resp_s_stored, 0, &jerr) : NULL;
		sqlite3_finalize (st);
		if (!stored_resp)
		  {
		    send_enveloped_error (ctx->fd, root, 500,
					  "Stored response unreadable.");
            LOGE("cmd_trade_buy: Idempotency_race: Stored response unreadable."); // ADDED
		    goto cleanup;
		  }
		send_enveloped_ok (ctx->fd, root, "trade.buy_receipt_v1",
				   stored_resp);
		json_decref (stored_resp);
		goto cleanup;
	      }
        LOGD("cmd_trade_buy: Idempotency_race: Request mismatch."); // ADDED
	  } else if (rc_select != SQLITE_DONE) { // Handle other errors from sqlite3_step
        LOGE("cmd_trade_buy: Idempotency_race SELECT failed with rc=%d: %s", rc_select, sqlite3_errmsg(db)); // ADDED
      }
	// If rc_select was not SQLITE_ROW (e.g., SQLITE_DONE, SQLITE_BUSY, or other error),
	// or if 'same' was false, we must finalize the statement here.
	sqlite3_finalize (st);
      } else {
        LOGE("cmd_trade_buy: Idempotency_race prepare error: %s", sqlite3_errmsg(db)); // ADDED
      }
  }
  // This line is reached if the idempotency race could not be resolved (no matching row, or error)
  send_enveloped_error (ctx->fd, root, 500, "Could not resolve idempotency race.");
  LOGD("cmd_trade_buy: Idempotency_race: Could not resolve, sending error."); // ADDED
  goto cleanup; // Ensure it always goes to cleanup after sending error


cleanup:
  if (trade_lines)
    free (trade_lines);
  if (receipt)
    json_decref (receipt);
  if (req_s)
    free (req_s);
  if (resp_s)
    free (resp_s);
  LOGD("cmd_trade_buy: Exiting cleanup."); // ADDED
  return 0;
}


int
cmd_trade_sell (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = NULL;
  json_t *receipt = NULL;
  json_t *lines = NULL;
  json_t *data = NULL;
  json_t *jitems = NULL;
  const char *key = NULL;
  int sector_id = 0;
  int port_id = 0;
  long long total_credits = 0;
  int rc = 0;
  char *req_s = NULL;
  char *resp_s = NULL;
  int we_started_tx = 0;

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
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  /* consume turn (may open a transaction) */
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, "trade.sell");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "trade.sell", root, NULL);
    }

  /* decloak */
  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  h_decloak_ship (db, player_ship_id);

  data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      return -1;
    }

  sector_id = ctx->sector_id;
  json_t *jsec = json_object_get (data, "sector_id");
  if (json_is_integer (jsec))
    sector_id = (int) json_integer_value (jsec);
  if (sector_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "Invalid sector_id.");
      return -1;
    }

  jitems = json_object_get (data, "items");
  if (!json_is_array (jitems) || json_array_size (jitems) == 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "items[] required.");
      return -1;
    }

  json_t *jkey = json_object_get (data, "idempotency_key");
  key = json_is_string (jkey) ? json_string_value (jkey) : NULL;
  if (!key || !*key)
    {
      send_enveloped_error (ctx->fd, root, 400, "idempotency_key required.");
      return -1;
    }

  /* idempotency fast-path */
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
            const unsigned char *req_s_stored = sqlite3_column_text (st, 0);
            const unsigned char *resp_s_stored = sqlite3_column_text (st, 1);
            json_error_t jerr;
            json_t *stored_req =
              req_s_stored ? json_loads ((const char *) req_s_stored, 0, &jerr) : NULL;
            json_t *incoming_req = json_incref (data);
            int same = (stored_req && json_equal_strict (stored_req, incoming_req));
            json_decref (incoming_req);
            if (stored_req)
              json_decref (stored_req);

            if (same)
              {
                json_t *stored_resp =
                  resp_s_stored ? json_loads ((const char *) resp_s_stored, 0, &jerr) : NULL;
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
            return -1;
          }
        sqlite3_finalize (st);
      }
  }

  /* resolve port from sector */
  {
    static const char *SQL_PORT =
      "SELECT id FROM ports WHERE sector = ?1 LIMIT 1;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db, SQL_PORT, -1, &st, NULL) != SQLITE_OK)
      {
        send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
        return -1;
      }
    sqlite3_bind_int (st, 1, sector_id);
    if (sqlite3_step (st) == SQLITE_ROW)
      port_id = sqlite3_column_int (st, 0);
    sqlite3_finalize (st);
    if (port_id <= 0)
      {
        send_enveloped_refused (ctx->fd, root, 1404,
                                "No port in this sector.", NULL);
        return 0;
      }
  }

  /* start local tx only if not already in one */
  if (sqlite3_get_autocommit (db))
    {
      if (begin (db) != SQLITE_OK)
        {
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
  size_t n = json_array_size (jitems);
  for (size_t i = 0; i < n; i++)
    {
      json_t *it = json_array_get (jitems, i);
      const char *commodity =
        json_string_value (json_object_get (it, "commodity"));
      int amount =
        (int) json_integer_value (json_object_get (it, "quantity"));

      if (!commodity || amount <= 0
          || (strcmp (commodity, "ore") != 0
              && strcmp (commodity, "organics") != 0
              && strcmp (commodity, "equipment") != 0))
        {
          /* BUGFIX: invalid commodity → refused 1405, not 400/error */
          send_enveloped_refused (ctx->fd, root, 1405,
                                  "Invalid or unsupported commodity.", NULL);
          goto fail_tx;
        }

      if (!h_port_buys_commodity (db, port_id, commodity))
        {
          send_enveloped_refused (ctx->fd, root, 1405,
                                  "Port is not buying this commodity right now.",
                                  NULL);
          goto fail_tx;
        }

      int buy_price = h_calculate_port_buy_price (db, port_id, commodity);
      if (buy_price <= 0)
        {
          send_enveloped_refused (ctx->fd, root, 1405,
                                  "Port is not buying this commodity right now.",
                                  NULL);
          goto fail_tx;
        }

      /* check cargo */
      int ore, org, eq, holds;
      if (h_get_ship_cargo_and_holds (db, player_ship_id,
                                      &ore, &org, &eq, &holds) != SQLITE_OK)
        {
          send_enveloped_error (ctx->fd, root, 500,
                                "Could not read ship cargo.");
          goto fail_tx;
        }

      int have = 0;
      if (strcmp (commodity, "ore") == 0)
        have = ore;
      else if (strcmp (commodity, "organics") == 0)
        have = org;
      else if (strcmp (commodity, "equipment") == 0)
        have = eq;

      if (have < amount)
        {
          send_enveloped_refused (ctx->fd, root, 1402,
                                  "You do not carry enough of that commodity.",
                                  NULL);
          goto fail_tx;
        }

      /* log row */
      {
        static const char *LOG_SQL =
          "INSERT INTO trade_log "
          "(player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) "
          "VALUES (?1, ?2, ?3, ?4, ?5, ?6, 'sell', ?7);";
        sqlite3_stmt *st = NULL;
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
        rc = h_update_ship_cargo (db, player_ship_id, commodity, -amount,
                                  &new_ship_qty);
        if (rc != SQLITE_OK)
          {
            if (rc == SQLITE_CONSTRAINT)
              {
                send_enveloped_refused (ctx->fd, root, 1402,
                                        "Insufficient cargo to sell (atomic check).",
                                        NULL);
              }
            goto fail_tx;
          }
      }

      /* update port stock (+amount) */
      {
        int new_port_qty = 0;
        rc = h_update_port_stock (db, port_id, commodity, amount,
                                  &new_port_qty);
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

      long long line_value = (long long) amount * (long long) buy_price;
      total_credits += line_value;

      json_t *jline = json_object ();
      json_object_set_new (jline, "commodity", json_string (commodity));
      json_object_set_new (jline, "quantity", json_integer (amount));
      json_object_set_new (jline, "unit_price", json_integer (buy_price));
      json_object_set_new (jline, "value", json_integer (line_value));
      json_array_append_new (lines, jline);
    }

  /* credit player */
  {
    sqlite3_stmt *st = NULL;
    static const char *SQL_CRED =
      "UPDATE players SET credits = credits + ?2 WHERE id = ?1;";
    if (sqlite3_prepare_v2 (db, SQL_CRED, -1, &st, NULL) != SQLITE_OK)
      goto sql_err;
    sqlite3_bind_int (st, 1, ctx->player_id);
    sqlite3_bind_int64 (st, 2, total_credits);
    if (sqlite3_step (st) != SQLITE_DONE)
      {
        sqlite3_finalize (st);
        goto sql_err;
      }
    sqlite3_finalize (st);
  }

  json_object_set_new (receipt, "total_credits",
                       json_integer (total_credits));

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
              req_s_stored ? json_loads ((const char *) req_s_stored, 0, &jerr) : NULL;
            int same = (stored_req && json_equal_strict (stored_req, data));
            if (stored_req)
              json_decref (stored_req);
            if (same)
              {
                json_t *stored_resp =
                  resp_s_stored ? json_loads ((const char *) resp_s_stored, 0, &jerr) : NULL;
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
  send_enveloped_error (ctx->fd, root, 500, "Could not resolve idempotency race.");

cleanup:
  if (we_started_tx)
    {
      /* if still in a tx here, something went wrong above;
         make sure we don't leave it open */
      if (!sqlite3_get_autocommit (db))
        rollback (db);
    }
  if (receipt)
    json_decref (receipt);
  if (req_s)
    free (req_s);
  if (resp_s)
    free (resp_s);
  return 0;
}


int
cmd_port_info (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401,
                              "Not authenticated", NULL);
      return 0;
    }

  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, 500,
                            "No database handle");
      return 0;
    }

  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 0;
  int port_id = 0;

  json_t *data = json_object_get (root, "data");
  if (json_is_object (data))
    {
      int s = 0, p = 0;

      /* Prefer explicit port_id if provided */
      if (json_get_int_field (data, "port_id", &p) && p > 0)
        {
          /* Resolve sector from ports for this port_id */
          static const char *SQL =
            "SELECT sector FROM ports WHERE id = ?1 LIMIT 1;";
          sqlite3_stmt *st = NULL;

          if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
            {
              send_enveloped_error (ctx->fd, root, 500,
                                    sqlite3_errmsg (db));
              return 0;
            }

          sqlite3_bind_int (st, 1, p);

          if (sqlite3_step (st) == SQLITE_ROW)
            {
              sector_id = sqlite3_column_int (st, 0);
              port_id = p;
            }

          sqlite3_finalize (st);

          if (port_id <= 0 || sector_id <= 0)
            {
              send_enveloped_refused (ctx->fd, root, 1404,
                                      "Port not found", NULL);
              return 0;
            }
        }
      else if (json_get_int_field (data, "sector_id", &s) && s > 0)
        {
          sector_id = s;
        }
    }

  if (sector_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400,
                            "Missing sector_id or port_id");
      return 0;
    }

  json_t *sector = NULL;
  if (db_sector_info_json (sector_id, &sector) != SQLITE_OK || !sector)
    {
      send_enveloped_error (ctx->fd, root, 1500,
                            "Database error");
      return 0;
    }

  json_t *port = json_object_get (sector, "port");
  if (!json_is_object (port))
    {
      json_decref (sector);
      send_enveloped_refused (ctx->fd, root, 1404,
                              "No port in this sector", NULL);
      return 0;
    }

  if (!port_id)
    {
      json_t *jid = json_object_get (port, "id");
      if (json_is_integer (jid))
        port_id = (int) json_integer_value (jid);
    }

  json_t *payload =
    json_pack ("{s:i,s:i,s:O}",
               "sector_id", sector_id,
               "port_id", port_id,
               "port", port);

  send_enveloped_ok (ctx->fd, root, "trade.port_info", payload);

  json_decref (payload);
  json_decref (sector);
  return 0;
}




/**
 * @brief Calculates the price a port will PAY the player for a commodity.
 *
 * Data:
 *   - commodities(code, base_price, ...)
 *   - port_commodities(port_id, commodity_id, supply)
 *
 * Returns:
 *   - >= 1 : valid unit price
 *   -  -1  : no row / config error / not traded here
 */
 int
h_calculate_port_buy_price (sqlite3 *db, int port_id, const char *commodity)
{
  if (!db || port_id <= 0 || !commodity)
    return -1;

  const char *code = commodity_to_code (commodity);
  if (!code)
    return -1;

  static const char *sql =
    "SELECT c.base_price, pc.supply "
    "FROM port_commodities pc "
    "JOIN commodities c ON pc.commodity_id = c.id "
    "WHERE pc.port_id = ?1 AND c.code = ?2 "
    "LIMIT 1;";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "h_calculate_port_buy_price: prepare failed: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }

  rc = sqlite3_bind_int (stmt, 1, port_id);
  if (rc == SQLITE_OK)
    rc = sqlite3_bind_text (stmt, 2, code, -1, SQLITE_TRANSIENT);

  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "h_calculate_port_buy_price: bind failed: %s\n",
               sqlite3_errmsg (db));
      sqlite3_finalize (stmt);
      return -1;
    }

  int result = -1;

  rc = sqlite3_step (stmt);
  if (rc == SQLITE_ROW)
    {
      int base_price = sqlite3_column_int (stmt, 0);
      int supply     = sqlite3_column_int (stmt, 1);
      result = internal_calculate_buy_price_formula (base_price, supply);
    }
  else if (rc == SQLITE_DONE)
    {
      /* No row: port does not trade this commodity. */
      result = -1;
    }
  else
    {
      fprintf (stderr, "h_calculate_port_buy_price: step failed: %s\n",
               sqlite3_errmsg (db));
      result = -1;
    }

  sqlite3_finalize (stmt);
  return result;
}


/**
 * @brief Internal pricing curve for port buy-price (what port pays player).
 *
 * supply is treated as 0..100 (clamped):
 *   supply=0   -> 150% of base_price
 *   supply=100 ->  50% of base_price
 */
 int
internal_calculate_buy_price_formula (int base_price, int supply)
{
  if (base_price <= 0)
    return -1;

  if (supply < 0)
    supply = 0;
  else if (supply > 100)
    supply = 100;

  const int max_factor_pct = 150;
  const int min_factor_pct = 50;
  int delta = max_factor_pct - min_factor_pct;             /* 100 */
  int factor_pct = max_factor_pct - (delta * supply / 100);

  long long scaled = (long long) base_price * factor_pct;
  long long price = (scaled + 50) / 100; /* rounded */

  if (price < 1)
    price = 1;
  if (price > 2000000000LL)
    price = 2000000000LL;

  return (int) price;
}
