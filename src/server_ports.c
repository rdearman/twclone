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
  int player_sector = 0;

  double port_sell_price_index = 0.0; // We get the index from 'ports'
  double base_price = 0.0;            // We get this from 'commodities'
  double port_sell_price = 0.0;       // This will be calculated (base * index)
  int port_stock = 0;
  long long total_cost = 0;
  long long current_player_credits = 0;
  int current_cargo_free = 0;
  sqlite3_stmt *stmt = NULL;

  // --- Dynamic Column Names ---
  const char *stock_col = NULL;
  const char *price_index_col = NULL;
  char *sql_lookup = NULL;
  // ---

  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  json_t *jdata = json_object_get (root, "data");
  if (!json_is_object (jdata))
    {
      RULE_ERROR (ERR_BAD_REQUEST, "Missing required field");
      goto trade_buy_done;
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
      goto trade_buy_done;
    }

  // =================================================================
  // 2. Price & Validation Logic (FIXED)
  // =================================================================

  // --- Dynamically set column names ---
  if (strcmp(commodity, "ore") == 0) {
      stock_col = "product_ore";
      price_index_col = "price_index_ore";
  } else if (strcmp(commodity, "organics") == 0) {
      stock_col = "product_organics";
      price_index_col = "price_index_organics";
  } else if (strcmp(commodity, "equipment") == 0) {
      stock_col = "product_equipment";
      price_index_col = "price_index_equipment";
  } else {
      RULE_REFUSE (ERR_COMMODITY_NOT_SOLD, "Port does not trade that commodity", NULL);
      goto trade_buy_done;
  }
  // ---

  player_sector = h_get_player_sector (ctx->player_id);

  // --- Build dynamic SQL to get stock and price index from 'ports' ---
  // We use sqlite3_mprintf to safely build the query.
  // This is safe because stock_col/price_index_col are from our hard-coded strings.
  sql_lookup = sqlite3_mprintf("SELECT %q, %q FROM ports WHERE id = ?1",
                               stock_col, price_index_col);
  if (!sql_lookup) {
      RULE_ERROR(ERR_MEMORY, "Out of memory");
      goto trade_buy_done;
  }
  // ---

  rc = sqlite3_prepare_v2 (db, sql_lookup, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("trade_buy price prepare error: %s (SQL: %s)", sqlite3_errmsg (db), sql_lookup);
      sqlite3_free(sql_lookup);
      RULE_ERROR (1500, "Database error");
      goto trade_buy_done;
    }

  sqlite3_free(sql_lookup); // Free the string *after* prepare

  sqlite3_bind_int (stmt, 1, port_id);

  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      port_stock = sqlite3_column_int (stmt, 0);
      port_sell_price_index = sqlite3_column_double (stmt, 1);
    }
  else
    {
      sqlite3_finalize (stmt);
      RULE_REFUSE (ERR_PORT_NOT_FOUND, "Port not found", NULL); // Port ID is invalid
      return 0;
    }
  sqlite3_finalize (stmt);
  stmt = NULL; // Clear stmt

  // --- Get base price from 'commodities' table ---
  rc = sqlite3_prepare_v2(db, "SELECT base_price FROM commodities WHERE name = ?1", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
      RULE_ERROR(1500, "Database error finding commodity price");
      goto trade_buy_done;
  }
  sqlite3_bind_text(stmt, 1, commodity, -1, SQLITE_STATIC);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
      base_price = sqlite3_column_double(stmt, 0);
  } else {
      sqlite3_finalize(stmt);
      RULE_REFUSE(ERR_COMMODITY_UNKNOWN, "Commodity base price not found", NULL);
      return 0;
  }
  sqlite3_finalize (stmt);
  
  // --- Calculate final price and cost ---
  port_sell_price = base_price * port_sell_price_index;
  total_cost = (long long) (port_sell_price * qty);

  // Validation Checks
  if (port_sell_price <= 0)
    {
      RULE_REFUSE (ERR_PRICE_INVALID, "Price is invalid or zero", NULL);
      return 0;
    }

  if (port_stock < qty)
    {
      json_t *hint = json_pack ("{s:i}", "current_stock", port_stock);
      RULE_REFUSE (REF_PORT_OUT_OF_STOCK, "Port ran out of stock", hint);
      json_decref (hint);
      return 0;
    }

  current_player_credits = player_credits (ctx);
  current_cargo_free = cargo_space_free (ctx);

  if (current_player_credits < total_cost)
    {
      json_t *hint = json_pack ("{s:i}", "cost", (int) total_cost);
      RULE_REFUSE (REF_NOT_ENOUGH_CREDITS, "Insufficient funds", hint);
      json_decref (hint);
      return 0;
    }

  LOGI("cargo_free: %d Quantity: %d)", current_cargo_free, qty);
  if (current_cargo_free < qty)
    {
      json_t *hint = json_pack ("{s:i}", "free_space", current_cargo_free);
      RULE_REFUSE (REF_NOT_ENOUGH_HOLDS, "Insufficient cargo space", hint);
      json_decref (hint);
      return 0;
    }

  // =================================================================
  // 3. Idempotency Block
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

  //rc = begin (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("trade_buy begin error: %s", sqlite3_errmsg (db));
      RULE_ERROR (1500, "Database error");
      goto trade_buy_done;
    }

  int new_qty = 0;

  // Apply effects (Transactional Updates)
  rc = h_deduct_ship_credits (db, ctx->player_id, total_cost, NULL);
  if (rc == SQLITE_OK)
    rc |= h_update_ship_cargo (db, ctx->player_id, commodity, qty, &new_qty);
  if (rc == SQLITE_OK)
    rc |= h_update_port_stock (db, port_id, commodity, -qty, NULL); // NOTE: This helper function must also be fixed!

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
    sqlite3_bind_double (log_stmt, 6, port_sell_price);    // CORRECT BINDING
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
  //rc = commit (db);
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
                                "unit_price", port_sell_price,
                                "credits_remaining", (int) (current_player_credits - total_cost));

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
  // This label is used by the macros
  return 1;
done_trade_buy:
  /* Your final cleanup label */
  return 0;
}

/* ========= Trading BUY ========= */


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

#include <math.h> // Ensure you have this header for pow() and fmax()
#include <string.h> // Ensure you have this header for strcmp()
// ... other headers and globals ...

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
