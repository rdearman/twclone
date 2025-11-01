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

// Define a reasonable cap for commodity quantity at a port
#define PORT_MAX_QUANTITY 10000.0

#define RULE_REFUSE(_code,_msg,_hint_json) \
    do { send_enveloped_refused(ctx->fd, root, (_code), (_msg), (_hint_json)); goto trade_buy_done; } while (0)

#define RULE_ERROR(_code,_msg) \
    do { send_enveloped_error(ctx->fd, root, (_code), (_msg)); goto trade_buy_done; } while (0)

void idemp_fingerprint_json (json_t * obj, char out[17]);
void iso8601_utc (char out[32]);

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


/* Update a port's stock for one commodity by delta (can be +/-). */
int
h_update_port_stock (sqlite3 *db, int port_id,
		     const char *commodity, int delta, int *new_qty_out)
{
  if (!commodity || *commodity == '\0')
    return SQLITE_MISUSE;

  int rc;
  char *errmsg = NULL;
  sqlite3_stmt *sel = NULL, *upd = NULL, *ins = NULL;

  rc = sqlite3_exec (db, "BEGIN IMMEDIATE", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      if (errmsg)
	sqlite3_free (errmsg);
      return rc;
    }

  // Get current qty and max_capacity
  const char *SQL_SEL =
    "SELECT quantity, max_capacity "
    "FROM port_goods WHERE port_id=?1 AND commodity=?2";
  rc = sqlite3_prepare_v2 (db, SQL_SEL, -1, &sel, NULL);
  if (rc != SQLITE_OK)
    goto rollback;

  sqlite3_bind_int (sel, 1, port_id);
  sqlite3_bind_text (sel, 2, commodity, -1, SQLITE_STATIC);

  int have_row = 0;
  int cur_qty = 0, max_cap = 0;

  rc = sqlite3_step (sel);
  if (rc == SQLITE_ROW)
    {
      have_row = 1;
      cur_qty = sqlite3_column_int (sel, 0);
      max_cap = sqlite3_column_int (sel, 1);
    }
  else if (rc != SQLITE_DONE)
    {
      goto rollback;
    }
  sqlite3_finalize (sel);
  sel = NULL;

  // If no row exists yet, we still need max_capacity. For simplicity, treat
  // missing row as quantity=0 and require delta>=0 (cannot sell what doesn't exist).
  if (!have_row)
    {
      if (delta < 0)
	{
	  rc = SQLITE_CONSTRAINT;
	  goto rollback;
	}

      // Insert with quantity=delta, but we need a max_capacity.
      // If you have a separate port types table, fetch it; here we assume
      // max_capacity >= delta by setting it to delta (or a large default).
      max_cap = (delta > 0 ? delta : 0);
      const char *SQL_INS0 =
	"INSERT INTO port_goods(port_id, commodity, quantity, max_capacity, production_rate) "
	"VALUES (?1, ?2, ?3, ?4, 0)";
      rc = sqlite3_prepare_v2 (db, SQL_INS0, -1, &ins, NULL);
      if (rc != SQLITE_OK)
	goto rollback;
      sqlite3_bind_int (ins, 1, port_id);
      sqlite3_bind_text (ins, 2, commodity, -1, SQLITE_STATIC);
      sqlite3_bind_int (ins, 3, delta);
      sqlite3_bind_int (ins, 4, max_cap);
      rc = sqlite3_step (ins);
      if (rc != SQLITE_DONE)
	{
	  rc = SQLITE_ERROR;
	  goto rollback;
	}
      sqlite3_finalize (ins);
      ins = NULL;
      if (new_qty_out)
	*new_qty_out = delta;
      rc = sqlite3_exec (db, "COMMIT", NULL, NULL, &errmsg);
      if (errmsg)
	sqlite3_free (errmsg);
      return rc;
    }

  // Compute proposed quantity and enforce bounds: 0..max_capacity
  long long proposed = (long long) cur_qty + (long long) delta;
  if (proposed < 0 || (max_cap > 0 && proposed > max_cap))
    {
      rc = SQLITE_CONSTRAINT;
      goto rollback;
    }

  // Update row
  const char *SQL_UPD =
    "UPDATE port_goods SET quantity=?3 WHERE port_id=?1 AND commodity=?2";
  rc = sqlite3_prepare_v2 (db, SQL_UPD, -1, &upd, NULL);
  if (rc != SQLITE_OK)
    goto rollback;
  sqlite3_bind_int (upd, 1, port_id);
  sqlite3_bind_text (upd, 2, commodity, -1, SQLITE_STATIC);
  sqlite3_bind_int (upd, 3, (int) proposed);
  rc = sqlite3_step (upd);
  if (rc != SQLITE_DONE)
    {
      rc = SQLITE_ERROR;
      goto rollback;
    }
  sqlite3_finalize (upd);
  upd = NULL;

  if (new_qty_out)
    *new_qty_out = (int) proposed;

  rc = sqlite3_exec (db, "COMMIT", NULL, NULL, &errmsg);
  if (errmsg)
    sqlite3_free (errmsg);
  return rc;

rollback:
  if (sel)
    sqlite3_finalize (sel);
  if (upd)
    sqlite3_finalize (upd);
  if (ins)
    sqlite3_finalize (ins);
  sqlite3_exec (db, "ROLLBACK", NULL, NULL, NULL);
  return rc;
}





//////////////////////////////////////////////////////////////////

int
cmd_trade_port_info (client_ctx_t *ctx, json_t *root)
{
  // Stateless alias; returns the same payload/type as cmd_port_info
  return cmd_port_info (ctx, root);
}



static decision_t
validate_trade_buy_rule (int player_id, int port_id, const char *commodity,
			 int qty)
{

  if (!commodity || port_id <= 0 || qty <= 0)
    return err (ERR_BAD_REQUEST, "Missing required field");

  if (!port_is_open (port_id, commodity))
    return refused (REF_PORT_CLOSED, "Port is closed");

  /* Example rule checks */
  int price_per = 10;		/* stub */
  long long cost = (long long) price_per * qty;
  if (player_credits (player_id) < cost)
    return refused (REF_NOT_ENOUGH_CREDITS, "Not enough credits");

  if (cargo_space_free (player_id) < qty)
    return refused (REF_NOT_ENOUGH_HOLDS, "Not enough cargo holds");

  return ok ();
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

// --- core -----------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////


int
cmd_trade_sell (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = NULL;
  json_t *receipt = NULL;
  json_t *lines = NULL;
  json_t *data = NULL;
  int sector_id = 0;
  const char *key = NULL;
  int port_id = 0;
  long long total_credits = 0;
  int rc = 0;
  char *req_s = NULL;
  char *resp_s = NULL;

  if (!ctx || !root)
    return -1;

  db = db_get_handle ();

  TurnConsumeResult tc = h_consume_player_turn (db, ctx, "trade.sell");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "trade.sell", root,
					    NULL);
    }

  // --- 0. Initial Validation & Setup (Pre-Transaction) ---

  if (!db)
    {
      send_enveloped_error (ctx->fd, root, 500, "No database handle.");
      return -1;
    }
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 1401, "not_authenticated");
      return -1;
    }

  // Decloak ship
  h_decloak_ship (db, h_get_active_ship_id (db, ctx->player_id));
  int player_sector = ctx->sector_id;

  // Input parsing and validation (Non-DB)
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

  json_t *jitems = json_object_get (data, "items");
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

  // Fast path: check idempotency table
  {
    static const char *SQL_GET =
      "SELECT request_json, response_json FROM trade_idempotency WHERE key = ?1 AND player_id = ?2 AND sector_id = ?3;";
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
	      req_s_stored ? json_loads ((const char *) req_s_stored, 0,
					 &jerr) : NULL;
	    json_t *incoming_req = json_incref (data);
	    int same = (stored_req
			&& json_equal_strict (stored_req, incoming_req));
	    json_decref (incoming_req);

	    if (stored_req)
	      json_decref (stored_req);
	    sqlite3_finalize (st);

	    if (same)
	      {
		json_t *stored_resp =
		  resp_s_stored ? json_loads ((const char *) resp_s_stored, 0,
					      &jerr) : NULL;
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
	    else
	      {
		send_enveloped_error (ctx->fd, root, 1105,
				      "Same idempotency_key used with different request.");
		return -1;
	      }
	  }
	sqlite3_finalize (st);
      }
  }

  // 1) Validate port and fetch id
  {
    static const char *SQL_PORT =
      "SELECT id FROM ports WHERE sector_id = ?1 LIMIT 1;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db, SQL_PORT, -1, &st, NULL) != SQLITE_OK)
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
	send_enveloped_error (ctx->fd, root, 1404, "No port in this sector.");
	return -1;
      }
  }

  // 2) Begin transaction
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
      return -1;
    }

  // Initialize objects that need cleanup
  receipt = json_object ();
  lines = json_array ();
  if (!receipt || !lines)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, 500, "Memory allocation error.");
      return -1;
    }

  json_object_set_new (receipt, "sector_id", json_integer (sector_id));
  json_object_set_new (receipt, "port_id", json_integer (port_id));
  json_object_set_new (receipt, "player_id", json_integer (ctx->player_id));
  json_object_set_new (receipt, "lines", lines);

  // 3) For each item: validate and apply
  size_t n = json_array_size (jitems);
  for (size_t i = 0; i < n; ++i)
    {
      json_t *it = json_array_get (jitems, i);
      const char *commodity = NULL;
      int amount = 0;
      int commodity_id = 0, player_qty = 0, port_stock = 0, port_max =
	0, buy_price = 0;
      sqlite3_stmt *st = NULL;

      json_t *jc = json_object_get (it, "commodity");
      json_t *ja = json_object_get (it, "amount");
      if (json_is_string (jc))
	commodity = json_string_value (jc);
      if (json_is_integer (ja))
	amount = (int) json_integer_value (ja);

      if (!commodity || amount <= 0)
	{
	  send_enveloped_error (ctx->fd, root, 400,
				"items[] must contain {commodity, amount>0}.");
	  goto trade_sell_done;
	}

      // Resolve commodity_id
      {
	static const char *SQL_COM =
	  "SELECT id FROM commodities WHERE name = ?1;";
	if (sqlite3_prepare_v2 (db, SQL_COM, -1, &st, NULL) != SQLITE_OK)
	  goto SQL_ERR;
	sqlite3_bind_text (st, 1, commodity, -1, SQLITE_TRANSIENT);
	if (sqlite3_step (st) == SQLITE_ROW)
	  commodity_id = sqlite3_column_int (st, 0);
	sqlite3_finalize (st);
	st = NULL;
	if (commodity_id <= 0)
	  {
	    send_enveloped_error (ctx->fd, root, 1404,
				  "Commodity not recognised at this port.");
	    goto trade_sell_done;
	  }
      }

      // Player cargo
      {
	static const char *SQL_PC =
	  "SELECT amount FROM player_cargo WHERE player_id = ?1 AND commodity_id = ?2;";
	if (sqlite3_prepare_v2 (db, SQL_PC, -1, &st, NULL) != SQLITE_OK)
	  goto SQL_ERR;
	sqlite3_bind_int (st, 1, ctx->player_id);
	sqlite3_bind_int (st, 2, commodity_id);
	if (sqlite3_step (st) == SQLITE_ROW)
	  player_qty = sqlite3_column_int (st, 0);
	sqlite3_finalize (st);
	st = NULL;
	if (player_qty < amount)
	  {
	    send_enveloped_error (ctx->fd, root, 1402,
				  "You do not carry enough of that commodity.");
	    goto trade_sell_done;
	  }
      }

      // Port inventory/prices
      {
	static const char *SQL_PI =
	  "SELECT stock, max_stock, buy_price, sell_price FROM port_inventory WHERE port_id = ?1 AND commodity_id = ?2;";
	if (sqlite3_prepare_v2 (db, SQL_PI, -1, &st, NULL) != SQLITE_OK)
	  goto SQL_ERR;
	sqlite3_bind_int (st, 1, port_id);
	sqlite3_bind_int (st, 2, commodity_id);
	if (sqlite3_step (st) == SQLITE_ROW)
	  {
	    port_stock = sqlite3_column_int (st, 0);
	    port_max = sqlite3_column_int (st, 1);
	    buy_price = sqlite3_column_int (st, 2);
	  }
	sqlite3_finalize (st);
	st = NULL;

	if (port_max > 0 && port_stock + amount > port_max)
	  {
	    send_enveloped_error (ctx->fd, root, 1403,
				  "Port cannot accept that much cargo.");
	    goto trade_sell_done;
	  }
	if (buy_price <= 0)
	  {
	    send_enveloped_error (ctx->fd, root, 1405,
				  "Port is not buying this commodity right now.");
	    goto trade_sell_done;
	  }
      }

      // --- Apply Effects (Updates) ---

      // Log trade
      {
	static const char *log_sql =
	  "INSERT INTO trade_log (player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) "
	  "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);";
	if (sqlite3_prepare_v2 (db, log_sql, -1, &st, NULL) != SQLITE_OK)
	  goto SQL_ERR;
	sqlite3_bind_int (st, 1, ctx->player_id);
	sqlite3_bind_int (st, 2, port_id);
	sqlite3_bind_int (st, 3, player_sector);
	sqlite3_bind_text (st, 4, commodity, -1, SQLITE_STATIC);
	sqlite3_bind_int (st, 5, amount);
	sqlite3_bind_double (st, 6, (double) buy_price);
	sqlite3_bind_text (st, 7, "sell", -1, SQLITE_STATIC);
	sqlite3_bind_int64 (st, 8, time (NULL));
	if (sqlite3_step (st) != SQLITE_DONE)
	  {
	    sqlite3_finalize (st);
	    goto SQL_ERR;
	  }
	sqlite3_finalize (st);
	st = NULL;
      }

      // Decrement player cargo
      {
	static const char *SQL_UPD_PC =
	  "UPDATE player_cargo SET amount = amount - ?3 WHERE player_id = ?1 AND commodity_id = ?2;";
	if (sqlite3_prepare_v2 (db, SQL_UPD_PC, -1, &st, NULL) != SQLITE_OK)
	  goto SQL_ERR;
	sqlite3_bind_int (st, 1, ctx->player_id);
	sqlite3_bind_int (st, 2, commodity_id);
	sqlite3_bind_int (st, 3, amount);
	if (sqlite3_step (st) != SQLITE_DONE)
	  {
	    sqlite3_finalize (st);
	    goto SQL_ERR;
	  }
	sqlite3_finalize (st);
	st = NULL;
      }

      // Increment port stock
      {
	static const char *SQL_UPD_PI =
	  "UPDATE port_inventory SET stock = stock + ?3 WHERE port_id = ?1 AND commodity_id = ?2;";
	if (sqlite3_prepare_v2 (db, SQL_UPD_PI, -1, &st, NULL) != SQLITE_OK)
	  goto SQL_ERR;
	sqlite3_bind_int (st, 1, port_id);
	sqlite3_bind_int (st, 2, commodity_id);
	sqlite3_bind_int (st, 3, amount);
	if (sqlite3_step (st) != SQLITE_DONE)
	  {
	    sqlite3_finalize (st);
	    goto SQL_ERR;
	  }
	sqlite3_finalize (st);
	st = NULL;
      }

      // Update totals and build receipt line
      long long line_value = (long long) amount * (long long) buy_price;
      total_credits += line_value;

      json_t *jline = json_object ();
      json_object_set_new (jline, "commodity", json_string (commodity));
      json_object_set_new (jline, "amount", json_integer (amount));
      json_object_set_new (jline, "unit_price", json_integer (buy_price));
      json_object_set_new (jline, "value", json_integer (line_value));
      json_array_append_new (lines, jline);
    }

  // Credit the player
  {
    static const char *SQL_CRED =
      "UPDATE players SET credits = credits + ?2 WHERE id = ?1;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db, SQL_CRED, -1, &st, NULL) != SQLITE_OK)
      goto SQL_ERR;
    sqlite3_bind_int64 (st, 1, ctx->player_id);
    sqlite3_bind_int64 (st, 2, total_credits);
    if (sqlite3_step (st) != SQLITE_DONE)
      {
	sqlite3_finalize (st);
	goto SQL_ERR;
      }
    sqlite3_finalize (st);
  }

  // Finalise receipt JSON
  json_object_set_new (receipt, "total_credits",
		       json_integer (total_credits));

  // 4) Persist idempotency record
  {
    req_s = json_dumps (data, JSON_COMPACT | JSON_SORT_KEYS);
    resp_s = json_dumps (receipt, JSON_COMPACT | JSON_SORT_KEYS);
    static const char *SQL_PUT =
      "INSERT INTO trade_idempotency(key, player_id, sector_id, request_json, response_json, created_at) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db, SQL_PUT, -1, &st, NULL) != SQLITE_OK)
      {
	send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
	goto trade_sell_cleanup_strings_only;	// Error before ROLLBACK/DECREF
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
	goto IDEMPOTENCY_RACE;	// Jump to race handler
      }
    sqlite3_finalize (st);
  }

  // 5) Commit and reply
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);	// Failsafe
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
      goto trade_sell_cleanup_strings_only;
    }
  send_enveloped_ok (ctx->fd, root, "trade.sell_receipt_v1", receipt);

  goto trade_sell_cleanup_strings_only;	// Successful exit

// --- Refactored Error and Cleanup Flow ------------------------------------

SQL_ERR:
  // Fallthrough for all generic SQL errors within the transaction
  send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
  goto trade_sell_done;

IDEMPOTENCY_RACE:;
  // Handle late-arriving duplicate idempotency insert
  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);	// Rollback our transaction

  {
    static const char *SQL_GET2 =
      "SELECT request_json, response_json FROM trade_idempotency WHERE key = ?1 AND player_id = ?2 AND sector_id = ?3;";
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
	    sqlite3_finalize (st);

	    if (same)
	      {
		json_t *stored_resp =
		  resp_s_stored ? json_loads ((const char *) resp_s_stored, 0,
					      &jerr) : NULL;
		if (!stored_resp)
		  {
		    send_enveloped_error (ctx->fd, root, 500,
					  "Stored response unreadable.");
		    goto trade_sell_cleanup_strings_only;
		  }
		send_enveloped_ok (ctx->fd, root, "trade.sell_receipt_v1",
				   stored_resp);
		json_decref (stored_resp);
		goto trade_sell_cleanup_strings_only;
	      }
	    else
	      {
		send_enveloped_error (ctx->fd, root, 1105,
				      "Same idempotency_key used with different request.");
		goto trade_sell_cleanup_strings_only;
	      }
	  }
	sqlite3_finalize (st);
      }
    send_enveloped_error (ctx->fd, root, 500, "Could not resolve race.");
  }
  // Fall through to main cleanup

trade_sell_done:
  // Cleanup path for all errors that occurred INSIDE the transaction loop.
  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
  if (receipt)
    json_decref (receipt);

trade_sell_cleanup_strings_only:
  // Final cleanup for dynamically allocated strings
  if (req_s)
    free (req_s);
  if (resp_s)
    free (resp_s);
  return 0;
}



////////////////////////////////////////////////////////////////////////////




/* int */
/* cmd_trade_sell (client_ctx_t *ctx, json_t *root) */
/* { */
/*   if (!ctx || !root) */
/*     return -1; */
/*   sqlite3 *db_handle = db_get_handle (); */
/*   h_decloak_ship (db_handle, */
/* 		  h_get_active_ship_id (db_handle, ctx->player_id)); */
/*   int player_sector; */
/*   int quantity; // Or 'units_to_sell', whatever your variable is named */
/*   double price_per_unit; */


/*   if (ctx->player_id <= 0) */
/*     { */
/*       send_enveloped_error (ctx->fd, root, 1401, "not_authenticated"); */
/*       //      send_enveloped_error (ctx, 1401, "not_authenticated", */
/*       //                            "Login required."); */
/*     } */

/*   json_t *data = json_object_get (root, "data"); */
/*   if (!json_is_object (data)) */
/*     { */
/*       send_enveloped_error (ctx->fd, root, 400, "Missing data object."); */
/*     } */

/*   // sector_id (optional; default to ctx->sector_id) */
/*   int sector_id = ctx->sector_id; */
/*   json_t *jsec = json_object_get (data, "sector_id"); */
/*   if (json_is_integer (jsec)) */
/*     { */
/*       sector_id = (int) json_integer_value (jsec); */
/*     } */
/*   if (sector_id <= 0) */
/*     { */
/*       send_enveloped_error (ctx->fd, root, 400, "Invalid sector_id."); */
/*     } */

/*   // items: array of { commodity: string, amount: int } */
/*   json_t *jitems = json_object_get (data, "items"); */
/*   if (!json_is_array (jitems) || json_array_size (jitems) == 0) */
/*     { */
/*       send_enveloped_error (ctx->fd, root, 400, "items[] required."); */
/*     } */

/*   // idempotency_key */
/*   json_t *jkey = json_object_get (data, "idempotency_key"); */
/*   const char *key = json_is_string (jkey) ? json_string_value (jkey) : NULL; */
/*   if (!key || !*key) */
/*     { */
/*       send_enveloped_error (ctx->fd, root, 400, "idempotency_key required."); */
/*     } */

/*   sqlite3 *db = db_get_handle (); */
/*   if (!db) */
/*     { */
/*       send_enveloped_error (ctx->fd, root, 500, "No database handle."); */
/*     } */

/*   // 0) Fast path: check idempotency table */
/*   { */
/*     static const char *SQL_GET = */
/*       "SELECT request_json, response_json FROM trade_idempotency WHERE key = ?1 AND player_id = ?2 AND sector_id = ?3;"; */
/*     sqlite3_stmt *st = NULL; */
/*     if (sqlite3_prepare_v2 (db, SQL_GET, -1, &st, NULL) == SQLITE_OK) */
/*       { */
/* 	sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT); */
/* 	sqlite3_bind_int (st, 2, ctx->player_id); */
/* 	sqlite3_bind_int (st, 3, sector_id); */
/* 	int rc = sqlite3_step (st); */
/* 	if (rc == SQLITE_ROW) */
/* 	  { */
/* 	    const unsigned char *req_s = sqlite3_column_text (st, 0); */
/* 	    const unsigned char *resp_s = sqlite3_column_text (st, 1); */

/* 	    // Compare requests: if identical, return stored response; else 1105 */
/* 	    json_error_t jerr; */
/* 	    json_t *stored_req = */
/* 	      req_s ? json_loads ((const char *) req_s, 0, &jerr) : NULL; */
/* 	    json_t *incoming_req = json_incref (data);	// compare only the "data" object */
/* 	    int same = (stored_req */
/* 			&& json_equal_strict (stored_req, incoming_req)); */
/* 	    json_decref (incoming_req); */
/* 	    if (stored_req) */
/* 	      json_decref (stored_req); */

/* 	    if (same) */
/* 	      { */
/* 		// Return stored response as-is */
/* 		json_t *stored_resp = */
/* 		  resp_s ? json_loads ((const char *) resp_s, 0, */
/* 				       &jerr) : NULL; */
/* 		sqlite3_finalize (st); */
/* 		if (!stored_resp) */
/* 		  { */
/* 		    send_enveloped_error (ctx->fd, root, 500, */
/* 					  "Stored response unreadable."); */
/* 		  } */
/* 		send_enveloped_ok (ctx->fd, root, "trade.sell_receipt_v1", */
/* 				   stored_resp); */
/* 		json_decref (stored_resp); */
/* 		return 0; */
/* 	      } */
/* 	    else */
/* 	      { */
/* 		sqlite3_finalize (st); */
/* 		send_enveloped_error (ctx->fd, root, 1105, */
/* 				      "Same idempotency_key used with different request."); */
/* 	      } */
/* 	  } */
/* 	sqlite3_finalize (st); */
/*       } */
/*   } */

/*   // 1) Validate there is a port in this sector + fetch its id */
/*   int port_id = 0; */
/*   { */
/*     // TODO: adjust to your schema if different */
/*     static const char *SQL_PORT = */
/*       "SELECT id FROM ports WHERE sector_id = ?1 LIMIT 1;"; */
/*     sqlite3_stmt *st = NULL; */
/*     if (sqlite3_prepare_v2 (db, SQL_PORT, -1, &st, NULL) != SQLITE_OK) */
/*       { */
/* 	send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db)); */
/*       } */
/*     sqlite3_bind_int (st, 1, sector_id); */
/*     int rc = sqlite3_step (st); */
/*     if (rc == SQLITE_ROW) */
/*       { */
/* 	port_id = sqlite3_column_int (st, 0); */
/*       } */
/*     sqlite3_finalize (st); */
/*     if (port_id <= 0) */
/*       { */
/* 	send_enveloped_error (ctx->fd, root, 1404, "No port in this sector."); */
/*       } */
/*   } */

/*   // 2) Begin transaction */
/*   if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) */
/*     { */
/*       send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db)); */
/*     } */

/*   int ok = 0;			// will flip to 1 on success */
/*   json_t *receipt = json_object (); */
/*   json_t *lines = json_array (); */
/*   json_object_set_new (receipt, "sector_id", json_integer (sector_id)); */
/*   json_object_set_new (receipt, "port_id", json_integer (port_id)); */
/*   json_object_set_new (receipt, "player_id", json_integer (ctx->player_id)); */

/*   long long total_credits = 0; */

/*   // 3) For each item: validate cargo >= amount, port capacity (max_stock), prices, then apply */
/*   size_t n = json_array_size (jitems); */
/*   for (size_t i = 0; i < n; ++i) */
/*     { */
/*       json_t *it = json_array_get (jitems, i); */
/*       if (!json_is_object (it)) */
/* 	{ */
/* 	  ok = 0; */
/* 	  goto REFUSE_BAD_ITEMS; */
/* 	} */

/*       const char *commodity = NULL; */
/*       int amount = 0; */

/*       json_t *jc = json_object_get (it, "commodity"); */
/*       json_t *ja = json_object_get (it, "amount"); */
/*       if (json_is_string (jc)) */
/* 	commodity = json_string_value (jc); */
/*       if (json_is_integer (ja)) */
/* 	amount = (int) json_integer_value (ja); */

/*       if (!commodity || amount <= 0) */
/* 	{ */
/* 	  ok = 0; */
/* 	  goto REFUSE_BAD_ITEMS; */
/* 	} */

/*       // Resolve commodity_id, player cargo, port line (prices/stock/limits) */
/*       int commodity_id = 0, player_qty = 0, port_stock = 0, port_max = */
/* 	0, buy_price = 0, sell_price = 0; */

/*       // TODO: adjust for your schema */
/*       // Resolve commodity_id */
/*       { */
/* 	static const char *SQL_COM = */
/* 	  "SELECT id FROM commodities WHERE name = ?1;"; */
/* 	sqlite3_stmt *st = NULL; */
/* 	if (sqlite3_prepare_v2 (db, SQL_COM, -1, &st, NULL) != SQLITE_OK) */
/* 	  { */
/* 	    ok = 0; */
/* 	    goto SQL_ERR; */
/* 	  } */
/* 	sqlite3_bind_text (st, 1, commodity, -1, SQLITE_TRANSIENT); */
/* 	int rc = sqlite3_step (st); */
/* 	if (rc == SQLITE_ROW) */
/* 	  commodity_id = sqlite3_column_int (st, 0); */
/* 	sqlite3_finalize (st); */
/* 	if (commodity_id <= 0) */
/* 	  { */
/* 	    ok = 0; */
/* 	    goto REFUSE_UNKNOWN_COMMODITY; */
/* 	  } */
/*       } */

/*       // Player cargo */
/*       { */
/* 	static const char *SQL_PC = */
/* 	  "SELECT amount FROM player_cargo WHERE player_id = ?1 AND commodity_id = ?2;"; */
/* 	sqlite3_stmt *st = NULL; */
/* 	if (sqlite3_prepare_v2 (db, SQL_PC, -1, &st, NULL) != SQLITE_OK) */
/* 	  { */
/* 	    ok = 0; */
/* 	    goto SQL_ERR; */
/* 	  } */
/* 	sqlite3_bind_int (st, 1, ctx->player_id); */
/* 	sqlite3_bind_int (st, 2, commodity_id); */
/* 	int rc = sqlite3_step (st); */
/* 	if (rc == SQLITE_ROW) */
/* 	  player_qty = sqlite3_column_int (st, 0); */
/* 	sqlite3_finalize (st); */
/* 	if (player_qty < amount) */
/* 	  { */
/* 	    ok = 0; */
/* 	    goto REFUSE_INSUFFICIENT_CARGO; */
/* 	  } */
/*       } */

/*       // Port inventory/prices */
/*       { */
/* 	static const char *SQL_PI = */
/* 	  "SELECT stock, max_stock, buy_price, sell_price " */
/* 	  "FROM port_inventory WHERE port_id = ?1 AND commodity_id = ?2;"; */
/* 	sqlite3_stmt *st = NULL; */
/* 	if (sqlite3_prepare_v2 (db, SQL_PI, -1, &st, NULL) != SQLITE_OK) */
/* 	  { */
/* 	    ok = 0; */
/* 	    goto SQL_ERR; */
/* 	  } */
/* 	sqlite3_bind_int (st, 1, port_id); */
/* 	sqlite3_bind_int (st, 2, commodity_id); */
/* 	int rc = sqlite3_step (st); */
/* 	if (rc == SQLITE_ROW) */
/* 	  { */
/* 	    port_stock = sqlite3_column_int (st, 0); */
/* 	    port_max = sqlite3_column_int (st, 1); */
/* 	    buy_price = sqlite3_column_int (st, 2);	// price port pays to buy from player */
/* 	    sell_price = sqlite3_column_int (st, 3);	// price port charges; not used for sell */
/* 	  } */
/* 	sqlite3_finalize (st); */
/* 	if (port_max > 0 && port_stock + amount > port_max) */
/* 	  { */
/* 	    ok = 0; */
/* 	    goto REFUSE_OVER_CAPACITY; */
/* 	  } */
/* 	if (buy_price <= 0) */
/* 	  { */
/* 	    ok = 0; */
/* 	    goto REFUSE_NOT_BUYING; */
/* 	  } */
/*       } */

/*       // Apply effects: */
/*       // - decrement player cargo */
/*       // - increment port stock */
/*       // - increment player credits by amount * buy_price */
/*       // (Do not mutate prices here unless your economy model calls for it.) */

/*       // In cmd_trade_sell in server_ports.c, right after the UPDATE statements */
/*       // (and before the final commit/cleanup) */

/*       { */
/* 	// Local definition for logging SQL */
/* 	const char *log_sql = */
/* 	  "INSERT INTO trade_log (player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) " */
/* 	  "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);"; */

/* 	sqlite3_stmt *log_stmt = NULL; */

/* 	// NOTE: Replace 'price_per_unit' with 'buy_price' if that's the variable name used */
/* 	// in your cmd_trade_sell logic. */

/* 	int rc = sqlite3_prepare_v2(db, log_sql, -1, &log_stmt, NULL); */
/* 	if (rc != SQLITE_OK) { */
/* 	  LOGE("trade_log prepare error in SELL: %s", sqlite3_errmsg(db)); */
/* 	  rollback(db); */
/* 	  RULE_ERROR(1500, "Trade logging setup failed"); // Use your error macro */
/* 	  goto trade_sell_done; // Use your cleanup label */
/* 	} */

/* 	// Bind parameters for logging the sale */
/* 	sqlite3_bind_int(log_stmt, 1, ctx->player_id); */
/* 	sqlite3_bind_int(log_stmt, 2, port_id); */
/* 	sqlite3_bind_int(log_stmt, 3, player_sector); // Sector where the trade occurred */
/* 	sqlite3_bind_text(log_stmt, 4, commodity, -1, SQLITE_STATIC); */
/* 	sqlite3_bind_int(log_stmt, 5, amount);  */
/* 	sqlite3_bind_double(log_stmt, 6, (double)buy_price); // Cast to double for the column */
/* 	sqlite3_bind_text(log_stmt, 7, "sell", -1, SQLITE_STATIC); */
/* 	sqlite3_bind_int64(log_stmt, 8, time(NULL)); // Current UNIX timestamp */

/* 	if ((rc = sqlite3_step(log_stmt)) != SQLITE_DONE) { */
/* 	  LOGE("trade_log exec error in SELL: %s", sqlite3_errmsg(db)); */
/* 	  rollback(db); */
/* 	  sqlite3_finalize(log_stmt); */
/* 	  RULE_ERROR(1500, "Trade logging failed"); */
/* 	  goto trade_sell_done; */
/* 	} */
/* 	sqlite3_finalize(log_stmt); */
/*       } */


/*       { */
/* 	static const char *SQL_UPD_PC = */
/* 	  "UPDATE player_cargo SET amount = amount - ?3 " */
/* 	  "WHERE player_id = ?1 AND commodity_id = ?2;"; */
/* 	sqlite3_stmt *st = NULL; */
/* 	if (sqlite3_prepare_v2 (db, SQL_UPD_PC, -1, &st, NULL) != SQLITE_OK) */
/* 	  { */
/* 	    ok = 0; */
/* 	    goto SQL_ERR; */
/* 	  } */
/* 	sqlite3_bind_int (st, 1, ctx->player_id); */
/* 	sqlite3_bind_int (st, 2, commodity_id); */
/* 	sqlite3_bind_int (st, 3, amount); */
/* 	if (sqlite3_step (st) != SQLITE_DONE) */
/* 	  { */
/* 	    sqlite3_finalize (st); */
/* 	    ok = 0; */
/* 	    goto SQL_ERR; */
/* 	  } */
/* 	sqlite3_finalize (st); */
/*       } */

/*       { */
/* 	static const char *SQL_UPD_PI = */
/* 	  "UPDATE port_inventory SET stock = stock + ?3 " */
/* 	  "WHERE port_id = ?1 AND commodity_id = ?2;"; */
/* 	sqlite3_stmt *st = NULL; */
/* 	if (sqlite3_prepare_v2 (db, SQL_UPD_PI, -1, &st, NULL) != SQLITE_OK) */
/* 	  { */
/* 	    ok = 0; */
/* 	    goto SQL_ERR; */
/* 	  } */
/* 	sqlite3_bind_int (st, 1, port_id); */
/* 	sqlite3_bind_int (st, 2, commodity_id); */
/* 	sqlite3_bind_int (st, 3, amount); */
/* 	if (sqlite3_step (st) != SQLITE_DONE) */
/* 	  { */
/* 	    sqlite3_finalize (st); */
/* 	    ok = 0; */
/* 	    goto SQL_ERR; */
/* 	  } */
/* 	sqlite3_finalize (st); */
/*       } */

/*       long long line_value = (long long) amount * (long long) buy_price; */
/*       total_credits += line_value; */

/*       // Build receipt line */
/*       json_t *jline = json_object (); */
/*       json_object_set_new (jline, "commodity", json_string (commodity)); */
/*       json_object_set_new (jline, "amount", json_integer (amount)); */
/*       json_object_set_new (jline, "unit_price", json_integer (buy_price)); */
/*       json_object_set_new (jline, "value", json_integer (line_value)); */
/*       json_array_append_new (lines, jline); */
/*     } */

/*   // Credit the player */
/*   { */
/*     static const char *SQL_CRED = */
/*       "UPDATE players SET credits = credits + ?2 WHERE id = ?1;"; */
/*     sqlite3_stmt *st = NULL; */
/*     if (sqlite3_prepare_v2 (db, SQL_CRED, -1, &st, NULL) != SQLITE_OK) */
/*       { */
/* 	ok = 0; */
/* 	goto SQL_ERR; */
/*       } */
/*     sqlite3_bind_int64 (st, 1, ctx->player_id); */
/*     sqlite3_bind_int64 (st, 2, total_credits); */
/*     if (sqlite3_step (st) != SQLITE_DONE) */
/*       { */
/* 	sqlite3_finalize (st); */
/* 	ok = 0; */
/* 	goto SQL_ERR; */
/*       } */
/*     sqlite3_finalize (st); */
/*   } */

/*   // Finalise receipt JSON */
/*   json_object_set_new (receipt, "lines", lines); */
/*   json_object_set_new (receipt, "total_credits", */
/* 		       json_integer (total_credits)); */

/*   // 4) Persist idempotency record with the exact request/response */
/*   { */
/*     char *req_s = json_dumps (data, JSON_COMPACT | JSON_SORT_KEYS); */
/*     char *resp_s = json_dumps (receipt, JSON_COMPACT | JSON_SORT_KEYS); */
/*     static const char *SQL_PUT = */
/*       "INSERT INTO trade_idempotency(key, player_id, sector_id, request_json, response_json, created_at) " */
/*       "VALUES (?1, ?2, ?3, ?4, ?5, ?6);"; */
/*     sqlite3_stmt *st = NULL; */
/*     if (sqlite3_prepare_v2 (db, SQL_PUT, -1, &st, NULL) != SQLITE_OK) */
/*       { */
/* 	free (req_s); */
/* 	free (resp_s); */
/* 	ok = 0; */
/* 	goto SQL_ERR; */
/*       } */
/*     sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT); */
/*     sqlite3_bind_int (st, 2, ctx->player_id); */
/*     sqlite3_bind_int (st, 3, sector_id); */
/*     bind_text_or_null (st, 4, req_s); */
/*     bind_text_or_null (st, 5, resp_s); */
/*     sqlite3_bind_int64 (st, 6, (sqlite3_int64) time (NULL)); */

/*     if (sqlite3_step (st) != SQLITE_DONE) */
/*       { */
/* 	// If UNIQUE violation occurs here (race), fall back to the same logic as replay */
/* 	// Fetch the row and respond accordingly. */
/* 	sqlite3_finalize (st); */
/* 	free (req_s); */
/* 	free (resp_s); */
/* 	goto IDEMPOTENCY_RACE; */
/*       } */
/*     sqlite3_finalize (st); */
/*     free (req_s); */
/*     free (resp_s); */
/*   } */

/*   // 5) Commit and reply */
/*   if (sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) */
/*     { */
/*       sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL); */
/*       json_decref (receipt); */
/*       send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db)); */
/*     } */
/*   send_enveloped_ok (ctx->fd, root, "trade.sell_receipt_v1", receipt); */

/* // --- refusals & errors ---------------------------------------------------- */

/* REFUSE_BAD_ITEMS: */
/*   sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL); */
/*   json_decref (receipt); */
/*   send_enveloped_error (ctx->fd, root, 400, */
/* 			"items[] must contain {commodity, amount>0}."); */

/* REFUSE_UNKNOWN_COMMODITY: */
/*   sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL); */
/*   json_decref (receipt); */
/*   send_enveloped_error (ctx->fd, root, 1404, */
/* 			"Commodity not recognised at this port."); */

/* REFUSE_INSUFFICIENT_CARGO: */
/*   sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL); */
/*   json_decref (receipt); */
/*   send_enveloped_error (ctx->fd, root, 1402, */
/* 			"You do not carry enough of that commodity."); */

/* REFUSE_OVER_CAPACITY: */
/*   sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL); */
/*   json_decref (receipt); */
/*   send_enveloped_error (ctx->fd, root, 1403, */
/* 			"Port cannot accept that much cargo."); */

/* REFUSE_NOT_BUYING: */
/*   sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL); */
/*   json_decref (receipt); */
/*   send_enveloped_error (ctx->fd, root, 1405, */
/* 			"Port is not buying this commodity right now."); */

/* SQL_ERR: */
/*   sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL); */
/*   json_decref (receipt); */
/*   send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db)); */

/* // Handle late-arriving duplicate idempotency insert */
/* IDEMPOTENCY_RACE:; */
/*   { */
/*     static const char *SQL_GET2 = */
/*       "SELECT request_json, response_json FROM trade_idempotency WHERE key = ?1 AND player_id = ?2 AND sector_id = ?3;"; */
/*     sqlite3_stmt *st = NULL; */
/*     if (sqlite3_prepare_v2 (db, SQL_GET2, -1, &st, NULL) == SQLITE_OK) */
/*       { */
/* 	sqlite3_bind_text (st, 1, key, -1, SQLITE_TRANSIENT); */
/* 	sqlite3_bind_int (st, 2, ctx->player_id); */
/* 	sqlite3_bind_int (st, 3, sector_id); */
/* 	int rc = sqlite3_step (st); */
/* 	if (rc == SQLITE_ROW) */
/* 	  { */
/* 	    const unsigned char *req_s = sqlite3_column_text (st, 0); */
/* 	    const unsigned char *resp_s = sqlite3_column_text (st, 1); */
/* 	    json_error_t jerr; */
/* 	    json_t *stored_req = */
/* 	      req_s ? json_loads ((const char *) req_s, 0, &jerr) : NULL; */
/* 	    int same = (stored_req && json_equal_strict (stored_req, data)); */
/* 	    if (stored_req) */
/* 	      json_decref (stored_req); */
/* 	    if (same) */
/* 	      { */
/* 		json_t *stored_resp = */
/* 		  resp_s ? json_loads ((const char *) resp_s, 0, */
/* 				       &jerr) : NULL; */
/* 		sqlite3_finalize (st); */
/* 		if (!stored_resp) */
/* 		  { */
/* 		    send_enveloped_error (ctx->fd, root, 500, */
/* 					  "Stored response unreadable."); */
/* 		  } */
/* 		// Safe to commit what we did; but we rolled back already. Just return stored response. */
/* 		send_enveloped_ok (ctx->fd, root, "trade.sell_receipt_v1", */
/* 				   stored_resp); */
/* 	      } */
/* 	    else */
/* 	      { */
/* 		sqlite3_finalize (st); */
/* 		send_enveloped_error (ctx->fd, root, 1105, */
/* 				      "Same idempotency_key used with different request."); */
/* 	      } */
/* 	  } */
/* 	sqlite3_finalize (st); */
/*       } */
/*     send_enveloped_error (ctx->fd, root, 500, "Could not resolve race."); */
/*   } */

/*  trade_buy_done: */
/*  trade_sell_done: */
/*   return 0; */

/* } */



/* Build a minimal, stable JSON for fingerprinting (cmd + data subset) */
static json_t *
build_trade_buy_fp_obj (const char *cmd, json_t *jdata)
{
  /* Expect: port_id (int), commodity (str), quantity (int).
     Ignore meta and unrelated keys. */
  json_t *fp = json_object ();
  json_object_set_new (fp, "command", json_string (cmd));

  int port_id = 0, qty = 0;
  const char *commodity = NULL;
  json_t *jport = json_object_get (jdata, "port_id");
  json_t *jcomm = json_object_get (jdata, "commodity");
  json_t *jqty = json_object_get (jdata, "quantity");
  if (json_is_integer (jport))
    port_id = (int) json_integer_value (jport);
  if (json_is_integer (jqty))
    qty = (int) json_integer_value (jqty);
  if (json_is_string (jcomm))
    commodity = json_string_value (jcomm);

  json_object_set_new (fp, "port_id", json_integer (port_id));
  json_object_set_new (fp, "quantity", json_integer (qty));
  json_object_set_new (fp, "commodity",
		       json_string (commodity ? commodity : ""));
  return fp;			/* caller must json_decref */
}



/* ========= Trading BUY ========= */

/* ========= Trading ========= */

int
cmd_trade_buy (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  int rc = SQLITE_OK;

  TurnConsumeResult tc = h_consume_player_turn (db, ctx, "trade.buy");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "trade.buy", root, NULL);
    }

  // 1. Consolidated Declarations
  int port_id = 0;
  const char *commodity = NULL;
  int qty = 0;
  int player_sector = 0;	// Needs to be set correctly, assumed via h_get_player_sector() or similar

  // Variables populated during lookup/check:
  double port_sell_price = 0.0;
  int port_stock = 0;
  long long total_cost = 0;
  long long current_player_credits = 0;
  int current_cargo_free = 0;
  sqlite3_stmt *stmt = NULL;

  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  json_t *jdata = json_object_get (root, "data");
  if (!json_is_object (jdata))
    {
      RULE_ERROR (ERR_BAD_REQUEST, "Missing required field");
      goto trade_buy_done;	// Use correct label here
    }

  /* Extract fields */
  json_t *jport = json_object_get (jdata, "port_id");
  json_t *jcomm = json_object_get (jdata, "commodity");
  json_t *jqty = json_object_get (jdata, "quantity");

  commodity = json_is_string (jcomm) ? json_string_value (jcomm) : NULL;
  port_id = json_is_integer (jport) ? (int) json_integer_value (jport) : 0;
  qty = json_is_integer (jqty) ? (int) json_integer_value (jqty) : 0;

  if (!commodity || port_id <= 0 || qty <= 0)
    {
      RULE_ERROR (ERR_BAD_REQUEST,
		  "Missing required field or invalid quantity");
      goto trade_buy_done;	// Use correct label here
    }

  // =================================================================
  // 2. Insert Price & Validation Logic (Replaces old stub/error-prone code)
  // =================================================================

  // Assume player_sector is retrieved here, e.g., player_sector = h_get_player_sector(ctx->player_id);
  player_sector = h_get_player_sector (ctx->player_id);	// Assuming this is defined and returns the player's current sector

  // Look up Price and Stock
  const char *sql_lookup =
    "SELECT stock, sell_price FROM port_inventory WHERE port_id = ?1 AND commodity = ?2;";

  rc = sqlite3_prepare_v2 (db, sql_lookup, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("trade_buy price prepare error: %s", sqlite3_errmsg (db));
      RULE_ERROR (1500, "Database error");
      goto trade_buy_done;
    }

  sqlite3_bind_int (stmt, 1, port_id);
  sqlite3_bind_text (stmt, 2, commodity, -1, SQLITE_STATIC);

  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      port_stock = sqlite3_column_int (stmt, 0);
      port_sell_price = sqlite3_column_double (stmt, 1);
    }
  else
    {
      sqlite3_finalize (stmt);
      RULE_REFUSE (1405, "Port does not trade that commodity", NULL);
      return 0;			// Return 0 outside of idempotency block
    }

  sqlite3_finalize (stmt);

  // Validation Checks
  if (port_sell_price <= 0)
    {
      RULE_REFUSE (1406, "Price is invalid or zero", NULL);
      return 0;
    }

  if (port_stock < qty)
    {
      json_t *hint = json_pack ("{s:i}", "current_stock", port_stock);
      RULE_REFUSE (1407, "Port ran out of stock", hint);
      json_decref (hint);
      return 0;
    }

  total_cost = (long long) (port_sell_price * qty);
  current_player_credits = player_credits (ctx->player_id);	// Assuming h_get_player_credits() or similar
  current_cargo_free = cargo_space_free (ctx->player_id);	// Assuming h_get_cargo_space_free() or similar

  if (current_player_credits < total_cost)
    {
      json_t *hint = json_pack ("{s:i}", "cost", (int) total_cost);
      RULE_REFUSE (1408, "Insufficient funds", hint);
      json_decref (hint);
      return 0;
    }

  if (current_cargo_free < qty)
    {
      json_t *hint = json_pack ("{s:i}", "free_space", current_cargo_free);
      RULE_REFUSE (1409, "Insufficient cargo space", hint);
      json_decref (hint);
      return 0;
    }

  // =================================================================
  // 3. Preserve User's Idempotency Block (Wrapped for context)
  // =================================================================

  /* Pull idempotency key if present */
  const char *idem_key = NULL;
  json_t *jmeta = json_object_get (root, "meta");
  if (json_is_object (jmeta))
    {
      json_t *jk = json_object_get (jmeta, "idempotency_key");
      if (json_is_string (jk))
	idem_key = json_string_value (jk);
    }

  /* Build fingerprint */
  char fp[17];
  fp[0] = 0;
  int c;
  json_t *fpobj = build_trade_buy_fp_obj ("trade.buy", jdata);
  idemp_fingerprint_json (fpobj, fp);
  json_decref (fpobj);

  if (idem_key && *idem_key)
    {
      /* Try to begin idempotent op */
      int rc = db_idemp_try_begin (idem_key, "trade.buy", fp);
      if (rc == SQLITE_CONSTRAINT)
	{
	  /* Existing key: fetch */
	  char *ecmd = NULL, *efp = NULL, *erst = NULL;
	  if (db_idemp_fetch (idem_key, &ecmd, &efp, &erst) == SQLITE_OK)
	    {
	      int fp_match = (efp && strcmp (efp, fp) == 0);
	      if (!fp_match)
		{
		  send_enveloped_error (ctx->fd, root, 1105,
					"Duplicate request (idempotency key reused)");
		}
	      else if (erst)
		{
		  /* Replay stored response exactly */
		  json_error_t jerr;
		  json_t *env = json_loads (erst, 0, &jerr);
		  if (env)
		    {
		      send_all_json (ctx->fd, env);
		      json_decref (env);
		    }
		  else
		    {
		      /* Corrupt stored response; treat as server error */
		      send_enveloped_error (ctx->fd, root, 1500,
					    "Idempotency replay error");
		    }
		}
	      else
		{
		  /* Record exists but no stored response (in-flight/crash before store). */
		  send_enveloped_error (ctx->fd, root, 1105,
					"Duplicate request (pending)");
		}
	      free (ecmd);
	      free (efp);
	      free (erst);
	      goto done_trade_buy;
	    }
	  else
	    {
	      send_enveloped_error (ctx->fd, root, 1500, "Database error");
	      goto done_trade_buy;
	    }
	}
      else if (rc != SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 1500, "Database error");
	  goto done_trade_buy;
	}
      /* If SQLITE_OK, we “own” this key now and should execute then store. */
    }


  // =================================================================
  // 4. Transaction and Updates (THE ACTUAL TRADE)
  // =================================================================

  rc = begin (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("trade_buy begin error: %s", sqlite3_errmsg (db));
      RULE_ERROR (1500, "Database error");
      goto trade_buy_done;
    }

  int new_qty = 0;

  // Apply effects (Transactional Updates)
  rc = h_deduct_ship_credits (db, ctx->player_id, total_cost, NULL);
  rc |= h_update_ship_cargo (db, ctx->player_id, commodity, qty, &new_qty);
  rc |= h_update_port_stock (db, port_id, commodity, -qty, NULL);

  if (rc != SQLITE_OK)
    {
      LOGE ("trade_buy update error: %s", sqlite3_errmsg (db));
      rollback (db);
      RULE_ERROR (1500, "Trade transaction failed");
      goto trade_buy_done;
    }

  // Logging (Correctly uses looked-up price and quantity)
  {
    const char *log_sql =
      "INSERT INTO trade_log (player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);";

    sqlite3_stmt *log_stmt = NULL;

    rc = sqlite3_prepare_v2 (db, log_sql, -1, &log_stmt, NULL);
    if (rc != SQLITE_OK)
      {
	LOGE ("trade_log prepare error in BUY: %s", sqlite3_errmsg (db));
	rollback (db);
	RULE_ERROR (1500, "Trade logging setup failed");
	goto trade_buy_done;
      }

    sqlite3_bind_int (log_stmt, 1, ctx->player_id);
    sqlite3_bind_int (log_stmt, 2, port_id);
    sqlite3_bind_int (log_stmt, 3, player_sector);
    sqlite3_bind_text (log_stmt, 4, commodity, -1, SQLITE_STATIC);
    sqlite3_bind_int (log_stmt, 5, qty);
    sqlite3_bind_double (log_stmt, 6, port_sell_price);	// CORRECT BINDING
    sqlite3_bind_text (log_stmt, 7, "buy", -1, SQLITE_STATIC);
    sqlite3_bind_int64 (log_stmt, 8, time (NULL));

    if ((rc = sqlite3_step (log_stmt)) != SQLITE_DONE)
      {
	LOGE ("trade_log exec error in BUY: %s", sqlite3_errmsg (db));
	rollback (db);
	sqlite3_finalize (log_stmt);
	RULE_ERROR (1500, "Trade logging failed");
	goto trade_buy_done;
      }
    sqlite3_finalize (log_stmt);
  }

  // Commit Transaction
  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("trade_buy commit error: %s", sqlite3_errmsg (db));
      RULE_ERROR (1500, "Database commit failed");
      goto trade_buy_done;
    }


  // =================================================================
  // 5. Build Success Response (Uses final values)
  // =================================================================

  json_t *data_rsp = json_pack ("{s:i, s:s, s:i, s:f, s:i}",
				"port_id", port_id,
				"commodity", commodity,
				"quantity", qty,
				"unit_price", port_sell_price,	// Sends the correct unit price
				"credits_remaining", (int) (current_player_credits - total_cost));	// Sends the new balance

  /* Build the final envelope so we can persist exactly what we send */
  json_t *env = json_object ();
  json_object_set_new (env, "id", json_string ("srv-trade"));
  json_object_set (env, "reply_to", json_object_get (root, "id"));
  char ts[32];
  iso8601_utc (ts);
  json_object_set_new (env, "ts", json_string (ts));
  json_object_set_new (env, "status", json_string ("ok"));

  json_object_set_new (env, "type", json_string ("trade.accepted"));
  json_object_set_new (env, "data", data_rsp);
  json_object_set_new (env, "error", json_null ());

  /* Optional meta: signal replay=false on first-run */
  json_t *meta = json_object ();
  if (idem_key && *idem_key)
    {
      json_object_set_new (meta, "idempotent_replay", json_false ());
      json_object_set_new (meta, "idempotency_key", json_string (idem_key));
    }
  if (json_object_size (meta) > 0)
    json_object_set_new (env, "meta", meta);
  else
    json_decref (meta);

  /* If we’re idempotent, store the envelope JSON BEFORE sending */
  if (idem_key && *idem_key)
    {
      char *env_json = json_dumps (env, JSON_COMPACT | JSON_SORT_KEYS);
      if (!env_json
	  || db_idemp_store_response (idem_key, env_json) != SQLITE_OK)
	{
	  if (env_json)
	    free (env_json);
	  json_decref (env);
	  send_enveloped_error (ctx->fd, root, 1500, "Database error");
	  goto done_trade_buy;
	}
      free (env_json);
    }

  /* Send */
  send_all_json (ctx->fd, env);
  json_decref (env);

trade_buy_done:
  return 1;
done_trade_buy:
  /* Your final cleanup label */
  return 0;
}


/* ========= Trading BUY ========= */


/* int */
/* cmd_trade_buy (client_ctx_t *ctx, json_t *root) */
/* { */
/*   double unit_price = 0.0; */
/*   int player_sector; */
/*   int quantity; // Or 'units_to_buy' */
/*   double price_per_unit; // Or 'total_cost' / 'buy_price' */
/*   sqlite3 *db = db_get_handle(); */

/*   json_t *jdata = json_object_get (root, "data"); */
/*   if (!json_is_object (jdata)) */
/*     { */
/*       send_enveloped_error (ctx->fd, root, 1301, "Missing required field"); */
/*     } */
/*   else */
/*     {       */
/*       /\* Extract fields *\/ */
/*       json_t *jport = json_object_get (jdata, "port_id"); */
/*       json_t *jcomm = json_object_get (jdata, "commodity"); */
/*       json_t *jqty = json_object_get (jdata, "quantity"); */
/*       const char *commodity = */
/* 	json_is_string (jcomm) ? json_string_value (jcomm) : NULL; */
/*       int port_id = */
/* 	json_is_integer (jport) ? (int) json_integer_value (jport) : 0; */
/*       int qty = json_is_integer (jqty) ? (int) json_integer_value (jqty) : 0; */

/*       if (!commodity || port_id <= 0 || qty <= 0) */
/* 	{ */
/* 	  RULE_ERROR (ERR_BAD_REQUEST, "Missing required field"); */
/* 	  //              send_enveloped_error (ctx->fd, root, 1301, */
/* 	  //                            "Missing required field"); */
/* 	  /\* no idempotency processing if invalid *\/ */
/* 	} */
/*       else */
/* 	{ */
/* 	  /\* Pull idempotency key if present *\/ */
/* 	  const char *idem_key = NULL; */
/* 	  json_t *jmeta = json_object_get (root, "meta"); */
/* 	  if (json_is_object (jmeta)) */
/* 	    { */
/* 	      json_t *jk = json_object_get (jmeta, "idempotency_key"); */
/* 	      if (json_is_string (jk)) */
/* 		idem_key = json_string_value (jk); */
/* 	    } */

/* 	  /\* Build fingerprint *\/ */
/* 	  char fp[17]; */
/* 	  fp[0] = 0; */
/* 	  int c; */
/* 	  //json_t *fpobj = build_trade_buy_fp_obj (c, jdata); */
/* 	  json_t *fpobj = build_trade_buy_fp_obj ("trade.buy", jdata); */
/* 	  idemp_fingerprint_json (fpobj, fp); */
/* 	  json_decref (fpobj); */

/* 	  if (idem_key && *idem_key) */
/* 	    { */
/* 	      /\* Try to begin idempotent op *\/ */
/* 	      //              int rc = db_idemp_try_begin (idem_key, c, fp); */
/* 	      int rc = db_idemp_try_begin (idem_key, "trade.buy", fp); */
/* 	      if (rc == SQLITE_CONSTRAINT) */
/* 		{ */
/* 		  /\* Existing key: fetch *\/ */
/* 		  char *ecmd = NULL, *efp = NULL, *erst = NULL; */
/* 		  if (db_idemp_fetch (idem_key, &ecmd, &efp, &erst) == */
/* 		      SQLITE_OK) */
/* 		    { */
/* 		      int fp_match = (efp && strcmp (efp, fp) == 0); */
/* 		      if (!fp_match) */
/* 			{ */
/* 			  /\* Key reused with different payload *\/ */
/* 			  send_enveloped_error (ctx->fd, root, 1105, */
/* 						"Duplicate request (idempotency key reused)"); */
/* 			} */
/* 		      else if (erst) */
/* 			{ */
/* 			  /\* Replay stored response exactly *\/ */
/* 			  json_error_t jerr; */
/* 			  json_t *env = json_loads (erst, 0, &jerr); */
/* 			  if (env) */
/* 			    { */
/* 			      send_all_json (ctx->fd, env); */
/* 			      json_decref (env); */
/* 			    } */
/* 			  else */
/* 			    { */
/* 			      /\* Corrupt stored response; treat as server error *\/ */
/* 			      send_enveloped_error (ctx->fd, root, 1500, */
/* 						    "Idempotency replay error"); */
/* 			    } */
/* 			} */
/* 		      else */
/* 			{ */
/* 			  /\* Record exists but no stored response (in-flight/crash before store). */
/* 			     For now, treat as duplicate; later you could block/wait or retry op safely. *\/ */
/* 			  send_enveloped_error (ctx->fd, root, 1105, */
/* 						"Duplicate request (pending)"); */
/* 			} */
/* 		      free (ecmd); */
/* 		      free (efp); */
/* 		      free (erst); */
/* 		      /\* Done *\/ */
/* 		      goto done_trade_buy; */
/* 		    } */
/* 		  else */
/* 		    { */
/* 		      /\* Couldn’t fetch; treat as server error *\/ */
/* 		      send_enveloped_error (ctx->fd, root, 1500, */
/* 					    "Database error"); */
/* 		      goto done_trade_buy; */
/* 		    } */
/* 		} */
/* 	      else if (rc != SQLITE_OK) */
/* 		{ */
/* 		  send_enveloped_error (ctx->fd, root, 1500, */
/* 					"Database error"); */
/* 		  goto done_trade_buy; */
/* 		} */
/* 	      /\* If SQLITE_OK, we “own” this key now and should execute then store. *\/ */
/* 	    } */

/* 	  // Update the history */

/* 	  { */
/* 	    // Local definition for logging SQL (ensure this is available in your file) */
/* 	    const char *log_sql = */
/* 	      "INSERT INTO trade_log (player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) " */
/* 	      "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);"; */

/* 	    sqlite3_stmt *log_stmt = NULL; */

/* 	    // NOTE: Replace 'price_per_unit' with 'buy_price' or whatever variable holds the COST */
/* 	    // to the player in your buy function. */

/* 	    int rc = sqlite3_prepare_v2(db, log_sql, -1, &log_stmt, NULL); */
/* 	    if (rc != SQLITE_OK) { */
/* 	      LOGE("trade_log prepare error in BUY: %s", sqlite3_errmsg(db)); */
/* 	      rollback(db); */
/* 	      RULE_ERROR(1500, "Trade logging setup failed"); // Use your error macro */
/* 	      goto trade_sell_done; // Use your cleanup label */
/* 	    } */

/* 	    //unit_price = (double)item_sell_price; */

/* 	    // Bind parameters for logging the purchase */
/* 	    sqlite3_bind_int(log_stmt, 1, ctx->player_id); */
/* 	    sqlite3_bind_int(log_stmt, 2, port_id); */
/* 	    sqlite3_bind_int(log_stmt, 3, player_sector); // Sector where the trade occurred */
/* 	    sqlite3_bind_text(log_stmt, 4, commodity, -1, SQLITE_STATIC); */
/* 	    sqlite3_bind_int(log_stmt, 5, qty); */
/* 	    sqlite3_bind_double(log_stmt, 6, unit_price); */
/* 	    // sqlite3_bind_double(log_stmt, 6, (double)sell_price);  */
/* 	    sqlite3_bind_text(log_stmt, 7, "buy", -1, SQLITE_STATIC); // Action should be 'buy' */
/* 	    sqlite3_bind_int64(log_stmt, 8, time(NULL)); // Current UNIX timestamp */

/* 	    if ((rc = sqlite3_step(log_stmt)) != SQLITE_DONE) { */
/* 	      LOGE("trade_log exec error in BUY: %s", sqlite3_errmsg(db)); */
/* 	      rollback(db); */
/* 	      sqlite3_finalize(log_stmt); */
/* 	      RULE_ERROR(1500, "Trade logging failed"); */
/* 	      goto trade_sell_done; */
/* 	    } */
/* 	    sqlite3_finalize(log_stmt); */
/* 	  } */

/* 	  /\* === Perform the actual operation (your existing stub) === *\/ */
/* 	  json_t *data = json_pack ("{s:i, s:s, s:i}", */
/* 				    "port_id", port_id, */
/* 				    "commodity", commodity, */
/* 				    "quantity", qty); */

/* 	  /\* Build the final envelope so we can persist exactly what we send *\/ */
/* 	  json_t *env = json_object (); */
/* 	  json_object_set_new (env, "id", json_string ("srv-trade")); */
/* 	  json_object_set (env, "reply_to", json_object_get (root, "id")); */
/* 	  char ts[32]; */
/* 	  iso8601_utc (ts); */
/* 	  json_object_set_new (env, "ts", json_string (ts)); */
/* 	  json_object_set_new (env, "status", json_string ("ok")); */
/* 	trade_buy_done: */
/* 	  ; */
/* 	  json_object_set_new (env, "type", json_string ("trade.accepted")); */
/* 	  json_object_set_new (env, "data", data); */
/* 	  json_object_set_new (env, "error", json_null ()); */

/* 	  /\* Optional meta: signal replay=false on first-run *\/ */
/* 	  json_t *meta = json_object (); */
/* 	  if (idem_key && *idem_key) */
/* 	    { */
/* 	      json_object_set_new (meta, "idempotent_replay", json_false ()); */
/* 	      json_object_set_new (meta, "idempotency_key", */
/* 				   json_string (idem_key)); */
/* 	    } */
/* 	  if (json_object_size (meta) > 0) */
/* 	    json_object_set_new (env, "meta", meta); */
/* 	  else */
/* 	    json_decref (meta); */

/* 	  /\* If we’re idempotent, store the envelope JSON BEFORE sending *\/ */
/* 	  if (idem_key && *idem_key) */
/* 	    { */
/* 	      char *env_json = */
/* 		json_dumps (env, JSON_COMPACT | JSON_SORT_KEYS); */
/* 	      if (!env_json */
/* 		  || db_idemp_store_response (idem_key, */
/* 					      env_json) != SQLITE_OK) */
/* 		{ */
/* 		  if (env_json) */
/* 		    free (env_json); */
/* 		  json_decref (env); */
/* 		  send_enveloped_error (ctx->fd, root, 1500, */
/* 					"Database error"); */
/* 		  goto done_trade_buy; */
/* 		} */
/* 	      free (env_json); */
/* 	    } */

/* 	  /\* Send *\/ */
/* 	  send_all_json (ctx->fd, env); */
/* 	  json_decref (env); */

/* 	done_trade_buy: */
/* 	  (void) 0; */
/* 	} */
/*     } */
/*   trade_sell_done: */
/*   return 0; */
/* } */



/////////////////////////////////
static int
json_get_int_field (json_t *obj, const char *key, int *out)
{
  json_t *v = json_object_get (obj, key);
  if (!json_is_integer (v))
    return 0;
  *out = (int) json_integer_value (v);
  return 1;
}

/* ---------- port.info ---------- */
int
cmd_port_info (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 1;
  json_t *data = json_object_get (root, "data");
  if (json_is_object (data))
    {
      int s;
      if (json_get_int_field (data, "sector_id", &s) && s > 0)
	{
	  sector_id = s;
	}
    }

  /* Fall back to sector-level API and extract {port} */
  json_t *sector = NULL;
  if (db_sector_info_json (sector_id, &sector) != SQLITE_OK || !sector)
    {
      send_enveloped_error (ctx->fd, root, 1500, "Database error");
      return 0;
    }

  json_t *port = json_object_get (sector, "port");
  if (!json_is_object (port))
    {
      json_decref (sector);
      send_enveloped_refused (ctx->fd, root, 1404, "No port in this sector",
			      NULL);
      return 0;
    }

  /* Build payload and send */
  json_t *payload = json_pack ("{s:i s:O}", "sector", sector_id, "port", port);	// port is borrowed; pack copies ref
  send_enveloped_ok (ctx->fd, root, "port.info", payload);
  json_decref (payload);
  json_decref (sector);
  return 0;
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


/////////////////////////////////

/* --- Public Helper Function Implementation --- */
double
h_calculate_trade_price(int port_id, const char *commodity, int quantity)
{
    sqlite3 *db_handle = db_get_handle();
    sqlite3_stmt *stmt = NULL;
    double price_index = 1.0;
    double base_price = 0.0;
    char port_type[2] = {'\0', '\0'}; 

    // =========================================================
    // 1. Acquire Mutex Lock (Must be RECURSIVE for nested calls)
    // =========================================================
    pthread_mutex_lock(&db_mutex); 
    
    // Safety check for impossible quantity
    if (quantity < 0) {
        // Unlock on error before return
        pthread_mutex_unlock(&db_mutex);
        return 1000.0;    
    }

    // --- 1. Fetch Base Price from 'commodities' table ---
    const char *sql_base_price = "SELECT base_price FROM commodities WHERE name = ?;";
    
    if (sqlite3_prepare_v2(db_handle, sql_base_price, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, commodity, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            base_price = sqlite3_column_double(stmt, 0);
        }
        sqlite3_finalize(stmt);
        stmt = NULL;
    } else {
        // log_error("DB Error in h_calculate_trade_price (base price lookup): %s\n", sqlite3_errmsg(db_handle));
        // Unlock on error before return
        pthread_mutex_unlock(&db_mutex);
        return 10.0; 
    }
    
    if (base_price <= 0.0) {
        // log_error("h_calculate_trade_price: Commodity %s not defined in the 'commodities' table.", commodity);
        // Unlock on error before return
        pthread_mutex_unlock(&db_mutex);
        return 10.0; // Fallback to a nominal price
    }
    
    // --- 2. Fetch Port Type and Price Index from DB ---
    const char *sql_query = 
        "SELECT T1.port_type, T2.price_index "
        "FROM ports T1 "
        "INNER JOIN port_commodities T2 ON T1.id = T2.port_id "
        "WHERE T1.id = ? AND T2.commodity = ?;";

    if (sqlite3_prepare_v2(db_handle, sql_query, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, port_id);
        sqlite3_bind_text(stmt, 2, commodity, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            // Read port_type (TEXT, column 0)
            const char *type = (const char *)sqlite3_column_text(stmt, 0);
            if (type && *type) {
                port_type[0] = type[0];
            }
            // Read price_index (REAL, column 1)
            price_index = sqlite3_column_double(stmt, 1);
        }
        sqlite3_finalize(stmt);
    } else {
        // log_error("DB Error in h_calculate_trade_price (port lookup): %s\n", sqlite3_errmsg(db_handle));
        // Unlock on error before return
        pthread_mutex_unlock(&db_mutex);
        return base_price * 1.0; // Fallback
    }
    
    // =========================================================
    // 3. Calculation (No DB access required below this line)
    // =========================================================
    
    // Clamp quantity
    double clamped_quantity = (double)quantity;
    // NOTE: You need to ensure PORT_MAX_QUANTITY is defined or replace it.
    if (clamped_quantity > PORT_MAX_QUANTITY) {
        clamped_quantity = PORT_MAX_QUANTITY;
    }

    // Normalized quantity: 0.0 (empty) to 1.0 (full)
    double normalized_quantity = clamped_quantity / PORT_MAX_QUANTITY;

    // Volatility Factor
    double fluctuation_factor = pow((1.0 - normalized_quantity), 2.0);

    // Apply the price index
    double indexed_base_price = base_price * price_index;
    
    double price_modifier = 0.50; // The maximum percentage the price can fluctuate up

    double final_price = indexed_base_price + (indexed_base_price * price_modifier * fluctuation_factor);

    // Ensure price is never less than 1.0 credit
    double final_result = fmax(1.0, final_price);

    // =========================================================
    // 4. Release Mutex Lock
    // =========================================================
    pthread_mutex_unlock(&db_mutex); 
    
    return final_result;
}
