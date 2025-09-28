#include <jansson.h>
#include <string.h>
#include <jansson.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
/* local includes */
#include "server_ports.h"
#include "database.h"		// db_* for ports/trade
#include "errors.h"
#include "config.h"
#include "server_envelope.h"
#include "server_cmds.h"
#include "common.h"

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif


#define RULE_REFUSE(_code,_msg,_hint_json) \
    do { send_enveloped_refused(ctx->fd, root, (_code), (_msg), (_hint_json)); goto trade_buy_done; } while (0)

#define RULE_ERROR(_code,_msg) \
    do { send_enveloped_error(ctx->fd, root, (_code), (_msg)); goto trade_buy_done; } while (0)

void idemp_fingerprint_json (json_t * obj, char out[17]);
void iso8601_utc (char out[32]);

//////////////////////////////////////////////////////////////////

int cmd_trade_port_info (client_ctx_t *ctx, json_t *root)
{
  // Stateless alias; returns the same payload/type as cmd_port_info
  return cmd_port_info(ctx, root);
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

int
cmd_trade_history (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "trade.history");
}

//////////////////////////////////////////////////


// --- helpers ---------------------------------------------------------------

static int json_equal_strict(json_t *a, json_t *b) {
  // serialise to canonical strings to compare (simplest reliable check)
  char *sa = json_dumps(a, JSON_COMPACT | JSON_SORT_KEYS);
  char *sb = json_dumps(b, JSON_COMPACT | JSON_SORT_KEYS);
  int same = (sa && sb && strcmp(sa, sb) == 0);
  free(sa); free(sb);
  return same;
}

static int bind_text_or_null(sqlite3_stmt *st, int idx, const char *s) {
  if (s) return sqlite3_bind_text(st, idx, s, -1, SQLITE_TRANSIENT);
  return sqlite3_bind_null(st, idx);
}

// --- core -----------------------------------------------------------------

int cmd_trade_sell (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || !root) return -1;

  if (ctx->player_id <= 0) {
    send_enveloped_error(ctx, 1401, "not_authenticated", "Login required.");
  }

  json_t *data = json_object_get(root, "data");
  if (!json_is_object(data)) {
     send_enveloped_error(ctx, 400, "bad_request", "Missing data object.");
  }

  // sector_id (optional; default to ctx->sector_id)
  int sector_id = ctx->sector_id;
  json_t *jsec = json_object_get(data, "sector_id");
  if (json_is_integer(jsec)) {
    sector_id = (int)json_integer_value(jsec);
  }
  if (sector_id <= 0) {
     send_enveloped_error(ctx, 400, "bad_request", "Invalid sector_id.");
  }

  // items: array of { commodity: string, amount: int }
  json_t *jitems = json_object_get(data, "items");
  if (!json_is_array(jitems) || json_array_size(jitems) == 0) {
     send_enveloped_error(ctx, 400, "bad_request", "items[] required.");
  }

  // idempotency_key
  json_t *jkey = json_object_get(data, "idempotency_key");
  const char *key = json_is_string(jkey) ? json_string_value(jkey) : NULL;
  if (!key || !*key) {
     send_enveloped_error(ctx, 400, "bad_request", "idempotency_key required.");
  }

  sqlite3 *db = db_get_handle();
  if (!db) {
     send_enveloped_error(ctx, 500, "db_unavailable", "No database handle.");
  }

  // 0) Fast path: check idempotency table
  {
    static const char *SQL_GET =
      "SELECT request_json, response_json FROM trade_idempotency WHERE key = ?1 AND player_id = ?2 AND sector_id = ?3;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL_GET, -1, &st, NULL) == SQLITE_OK) {
      sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
      sqlite3_bind_int (st, 2, ctx->player_id);
      sqlite3_bind_int (st, 3, sector_id);
      int rc = sqlite3_step(st);
      if (rc == SQLITE_ROW) {
        const unsigned char *req_s = sqlite3_column_text(st, 0);
        const unsigned char *resp_s = sqlite3_column_text(st, 1);

        // Compare requests: if identical, return stored response; else 1105
        json_error_t jerr;
        json_t *stored_req  = req_s  ? json_loads((const char*)req_s, 0, &jerr) : NULL;
        json_t *incoming_req = json_incref(data); // compare only the "data" object
        int same = (stored_req && json_equal_strict(stored_req, incoming_req));
        json_decref(incoming_req);
        if (stored_req) json_decref(stored_req);

        if (same) {
          // Return stored response as-is
          json_t *stored_resp = resp_s ? json_loads((const char*)resp_s, 0, &jerr) : NULL;
          sqlite3_finalize(st);
          if (!stored_resp) {
             send_enveloped_error(ctx, 500, "idempotency_corrupt", "Stored response unreadable.");
          }
          send_enveloped_ok(ctx->fd,root, "trade.sell_receipt_v1", stored_resp);
          json_decref(stored_resp);
          return 0;
        } else {
          sqlite3_finalize(st);
           send_enveloped_error(ctx, 1105, "idempotency_key_conflict",
                                      "Same idempotency_key used with different request.");
        }
      }
      sqlite3_finalize(st);
    }
  }

  // 1) Validate there is a port in this sector + fetch its id
  int port_id = 0;
  {
    // TODO: adjust to your schema if different
    static const char *SQL_PORT =
      "SELECT id FROM ports WHERE sector_id = ?1 LIMIT 1;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL_PORT, -1, &st, NULL) != SQLITE_OK) {
       send_enveloped_error(ctx, 500, "sql_error", sqlite3_errmsg(db));
    }
    sqlite3_bind_int(st, 1, sector_id);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
      port_id = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    if (port_id <= 0) {
       send_enveloped_error(ctx, 1404, "no_port", "No port in this sector.");
    }
  }

  // 2) Begin transaction
  if (sqlite3_exec(db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK) {
     send_enveloped_error(ctx, 500, "tx_begin_failed", sqlite3_errmsg(db));
  }

  int ok = 0; // will flip to 1 on success
  json_t *receipt = json_object();
  json_t *lines   = json_array();
  json_object_set_new(receipt, "sector_id", json_integer(sector_id));
  json_object_set_new(receipt, "port_id",   json_integer(port_id));
  json_object_set_new(receipt, "player_id", json_integer(ctx->player_id));

  long long total_credits = 0;

  // 3) For each item: validate cargo >= amount, port capacity (max_stock), prices, then apply
  size_t n = json_array_size(jitems);
  for (size_t i = 0; i < n; ++i) {
    json_t *it = json_array_get(jitems, i);
    if (!json_is_object(it)) { ok = 0; goto REFUSE_BAD_ITEMS; }

    const char *commodity = NULL;
    int amount = 0;

    json_t *jc = json_object_get(it, "commodity");
    json_t *ja = json_object_get(it, "amount");
    if (json_is_string(jc)) commodity = json_string_value(jc);
    if (json_is_integer(ja)) amount = (int)json_integer_value(ja);

    if (!commodity || amount <= 0) { ok = 0; goto REFUSE_BAD_ITEMS; }

    // Resolve commodity_id, player cargo, port line (prices/stock/limits)
    int commodity_id = 0, player_qty = 0, port_stock = 0, port_max = 0, buy_price = 0, sell_price = 0;

    // TODO: adjust for your schema
    // Resolve commodity_id
    {
      static const char *SQL_COM =
        "SELECT id FROM commodities WHERE name = ?1;";
      sqlite3_stmt *st = NULL;
      if (sqlite3_prepare_v2(db, SQL_COM, -1, &st, NULL) != SQLITE_OK) {
        ok = 0; goto SQL_ERR;
      }
      sqlite3_bind_text(st, 1, commodity, -1, SQLITE_TRANSIENT);
      int rc = sqlite3_step(st);
      if (rc == SQLITE_ROW) commodity_id = sqlite3_column_int(st, 0);
      sqlite3_finalize(st);
      if (commodity_id <= 0) {
        ok = 0; goto REFUSE_UNKNOWN_COMMODITY;
      }
    }

    // Player cargo
    {
      static const char *SQL_PC =
        "SELECT amount FROM player_cargo WHERE player_id = ?1 AND commodity_id = ?2;";
      sqlite3_stmt *st = NULL;
      if (sqlite3_prepare_v2(db, SQL_PC, -1, &st, NULL) != SQLITE_OK) {
        ok = 0; goto SQL_ERR;
      }
      sqlite3_bind_int(st, 1, ctx->player_id);
      sqlite3_bind_int(st, 2, commodity_id);
      int rc = sqlite3_step(st);
      if (rc == SQLITE_ROW) player_qty = sqlite3_column_int(st, 0);
      sqlite3_finalize(st);
      if (player_qty < amount) {
        ok = 0; goto REFUSE_INSUFFICIENT_CARGO;
      }
    }

    // Port inventory/prices
    {
      static const char *SQL_PI =
        "SELECT stock, max_stock, buy_price, sell_price "
        "FROM port_inventory WHERE port_id = ?1 AND commodity_id = ?2;";
      sqlite3_stmt *st = NULL;
      if (sqlite3_prepare_v2(db, SQL_PI, -1, &st, NULL) != SQLITE_OK) {
        ok = 0; goto SQL_ERR;
      }
      sqlite3_bind_int(st, 1, port_id);
      sqlite3_bind_int(st, 2, commodity_id);
      int rc = sqlite3_step(st);
      if (rc == SQLITE_ROW) {
        port_stock = sqlite3_column_int(st, 0);
        port_max   = sqlite3_column_int(st, 1);
        buy_price  = sqlite3_column_int(st, 2); // price port pays to buy from player
        sell_price = sqlite3_column_int(st, 3); // price port charges; not used for sell
      }
      sqlite3_finalize(st);
      if (port_max > 0 && port_stock + amount > port_max) {
        ok = 0; goto REFUSE_OVER_CAPACITY;
      }
      if (buy_price <= 0) {
        ok = 0; goto REFUSE_NOT_BUYING;
      }
    }

    // Apply effects:
    // - decrement player cargo
    // - increment port stock
    // - increment player credits by amount * buy_price
    // (Do not mutate prices here unless your economy model calls for it.)

    {
      static const char *SQL_UPD_PC =
        "UPDATE player_cargo SET amount = amount - ?3 "
        "WHERE player_id = ?1 AND commodity_id = ?2;";
      sqlite3_stmt *st = NULL;
      if (sqlite3_prepare_v2(db, SQL_UPD_PC, -1, &st, NULL) != SQLITE_OK) { ok = 0; goto SQL_ERR; }
      sqlite3_bind_int(st, 1, ctx->player_id);
      sqlite3_bind_int(st, 2, commodity_id);
      sqlite3_bind_int(st, 3, amount);
      if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); ok = 0; goto SQL_ERR; }
      sqlite3_finalize(st);
    }

    {
      static const char *SQL_UPD_PI =
        "UPDATE port_inventory SET stock = stock + ?3 "
        "WHERE port_id = ?1 AND commodity_id = ?2;";
      sqlite3_stmt *st = NULL;
      if (sqlite3_prepare_v2(db, SQL_UPD_PI, -1, &st, NULL) != SQLITE_OK) { ok = 0; goto SQL_ERR; }
      sqlite3_bind_int(st, 1, port_id);
      sqlite3_bind_int(st, 2, commodity_id);
      sqlite3_bind_int(st, 3, amount);
      if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); ok = 0; goto SQL_ERR; }
      sqlite3_finalize(st);
    }

    long long line_value = (long long)amount * (long long)buy_price;
    total_credits += line_value;

    // Build receipt line
    json_t *jline = json_object();
    json_object_set_new(jline, "commodity",   json_string(commodity));
    json_object_set_new(jline, "amount",      json_integer(amount));
    json_object_set_new(jline, "unit_price",  json_integer(buy_price));
    json_object_set_new(jline, "value",       json_integer(line_value));
    json_array_append_new(lines, jline);
  }

  // Credit the player
  {
    static const char *SQL_CRED =
      "UPDATE players SET credits = credits + ?2 WHERE id = ?1;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL_CRED, -1, &st, NULL) != SQLITE_OK) { ok = 0; goto SQL_ERR; }
    sqlite3_bind_int64(st, 1, ctx->player_id);
    sqlite3_bind_int64(st, 2, total_credits);
    if (sqlite3_step(st) != SQLITE_DONE) { sqlite3_finalize(st); ok = 0; goto SQL_ERR; }
    sqlite3_finalize(st);
  }

  // Finalise receipt JSON
  json_object_set_new(receipt, "lines",         lines);
  json_object_set_new(receipt, "total_credits", json_integer(total_credits));

  // 4) Persist idempotency record with the exact request/response
  {
    char *req_s  = json_dumps(data, JSON_COMPACT | JSON_SORT_KEYS);
    char *resp_s = json_dumps(receipt, JSON_COMPACT | JSON_SORT_KEYS);
    static const char *SQL_PUT =
      "INSERT INTO trade_idempotency(key, player_id, sector_id, request_json, response_json, created_at) "
      "VALUES (?1, ?2, ?3, ?4, ?5, ?6);";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL_PUT, -1, &st, NULL) != SQLITE_OK) {
      free(req_s); free(resp_s); ok = 0; goto SQL_ERR;
    }
    sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (st, 2, ctx->player_id);
    sqlite3_bind_int (st, 3, sector_id);
    bind_text_or_null(st, 4, req_s);
    bind_text_or_null(st, 5, resp_s);
    sqlite3_bind_int64(st, 6, (sqlite3_int64)time(NULL));

    if (sqlite3_step(st) != SQLITE_DONE) {
      // If UNIQUE violation occurs here (race), fall back to the same logic as replay
      // Fetch the row and respond accordingly.
      sqlite3_finalize(st);
      free(req_s); free(resp_s);
      goto IDEMPOTENCY_RACE;
    }
    sqlite3_finalize(st);
    free(req_s); free(resp_s);
  }

  // 5) Commit and reply
  if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    json_decref(receipt);
     send_enveloped_error(ctx, 500, "tx_commit_failed", sqlite3_errmsg(db));
  }
  send_enveloped_ok(ctx->fd, root, "trade.sell_receipt_v1", receipt);

// --- refusals & errors ----------------------------------------------------

REFUSE_BAD_ITEMS:
  sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
  json_decref(receipt);
   send_enveloped_error(ctx, 400, "bad_items", "items[] must contain {commodity, amount>0}.");

REFUSE_UNKNOWN_COMMODITY:
  sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
  json_decref(receipt);
   send_enveloped_error(ctx, 1404, "unknown_commodity", "Commodity not recognised at this port.");

REFUSE_INSUFFICIENT_CARGO:
  sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
  json_decref(receipt);
   send_enveloped_error(ctx, 1402, "insufficient_cargo", "You do not carry enough of that commodity.");

REFUSE_OVER_CAPACITY:
  sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
  json_decref(receipt);
   send_enveloped_error(ctx, 1403, "port_capacity_exceeded", "Port cannot accept that much cargo.");

REFUSE_NOT_BUYING:
  sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
  json_decref(receipt);
   send_enveloped_error(ctx, 1405, "not_buying", "Port is not buying this commodity right now.");

SQL_ERR:
  sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
  json_decref(receipt);
   send_enveloped_error(ctx, 500, "sql_error", sqlite3_errmsg(db));

// Handle late-arriving duplicate idempotency insert
IDEMPOTENCY_RACE: ;
  {
    static const char *SQL_GET2 =
      "SELECT request_json, response_json FROM trade_idempotency WHERE key = ?1 AND player_id = ?2 AND sector_id = ?3;";
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL_GET2, -1, &st, NULL) == SQLITE_OK) {
      sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
      sqlite3_bind_int (st, 2, ctx->player_id);
      sqlite3_bind_int (st, 3, sector_id);
      int rc = sqlite3_step(st);
      if (rc == SQLITE_ROW) {
        const unsigned char *req_s = sqlite3_column_text(st, 0);
        const unsigned char *resp_s = sqlite3_column_text(st, 1);
        json_error_t jerr;
        json_t *stored_req  = req_s  ? json_loads((const char*)req_s, 0, &jerr) : NULL;
        int same = (stored_req && json_equal_strict(stored_req, data));
        if (stored_req) json_decref(stored_req);
        if (same) {
          json_t *stored_resp = resp_s ? json_loads((const char*)resp_s, 0, &jerr) : NULL;
          sqlite3_finalize(st);
          if (!stored_resp) {
             send_enveloped_error(ctx, 500, "idempotency_corrupt", "Stored response unreadable.");
          }
          // Safe to commit what we did; but we rolled back already. Just return stored response.
	  send_enveloped_ok(ctx->fd, root, "trade.sell_receipt_v1", stored_resp);
        } else {
          sqlite3_finalize(st);
           send_enveloped_error(ctx, 1105, "idempotency_key_conflict",
                                      "Same idempotency_key used with different request.");
        }
      }
      sqlite3_finalize(st);
    }
     send_enveloped_error(ctx, 500, "idempotency_lookup_failed", "Could not resolve race.");
  }
}



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



/* ========= Trading ========= */

int
cmd_trade_buy (client_ctx_t *ctx, json_t *root)
{
  json_t *jdata = json_object_get (root, "data");
  if (!json_is_object (jdata))
    {
      send_enveloped_error (ctx->fd, root, 1301, "Missing required field");
    }
  else
    {
      /* Extract fields */
      json_t *jport = json_object_get (jdata, "port_id");
      json_t *jcomm = json_object_get (jdata, "commodity");
      json_t *jqty = json_object_get (jdata, "quantity");
      const char *commodity =
	json_is_string (jcomm) ? json_string_value (jcomm) : NULL;
      int port_id =
	json_is_integer (jport) ? (int) json_integer_value (jport) : 0;
      int qty = json_is_integer (jqty) ? (int) json_integer_value (jqty) : 0;

      if (!commodity || port_id <= 0 || qty <= 0)
	{
	  RULE_ERROR (ERR_BAD_REQUEST, "Missing required field");
	  //              send_enveloped_error (ctx->fd, root, 1301,
	  //                            "Missing required field");
	  /* no idempotency processing if invalid */
	}
      else
	{
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
	  json_t *fpobj = build_trade_buy_fp_obj (c, jdata);
	  idemp_fingerprint_json (fpobj, fp);
	  json_decref (fpobj);

	  if (idem_key && *idem_key)
	    {
	      /* Try to begin idempotent op */
	      int rc = db_idemp_try_begin (idem_key, c, fp);
	      if (rc == SQLITE_CONSTRAINT)
		{
		  /* Existing key: fetch */
		  char *ecmd = NULL, *efp = NULL, *erst = NULL;
		  if (db_idemp_fetch (idem_key, &ecmd, &efp, &erst) ==
		      SQLITE_OK)
		    {
		      int fp_match = (efp && strcmp (efp, fp) == 0);
		      if (!fp_match)
			{
			  /* Key reused with different payload */
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
			  /* Record exists but no stored response (in-flight/crash before store).
			     For now, treat as duplicate; later you could block/wait or retry op safely. */
			  send_enveloped_error (ctx->fd, root, 1105,
						"Duplicate request (pending)");
			}
		      free (ecmd);
		      free (efp);
		      free (erst);
		      /* Done */
		      goto done_trade_buy;
		    }
		  else
		    {
		      /* Couldn’t fetch; treat as server error */
		      send_enveloped_error (ctx->fd, root, 1500,
					    "Database error");
		      goto done_trade_buy;
		    }
		}
	      else if (rc != SQLITE_OK)
		{
		  send_enveloped_error (ctx->fd, root, 1500,
					"Database error");
		  goto done_trade_buy;
		}
	      /* If SQLITE_OK, we “own” this key now and should execute then store. */
	    }

	  /* === Perform the actual operation (your existing stub) === */
	  json_t *data = json_pack ("{s:i, s:s, s:i}",
				    "port_id", port_id,
				    "commodity", commodity,
				    "quantity", qty);

	  /* Build the final envelope so we can persist exactly what we send */
	  json_t *env = json_object ();
	  json_object_set_new (env, "id", json_string ("srv-trade"));
	  json_object_set (env, "reply_to", json_object_get (root, "id"));
	  char ts[32];
	  iso8601_utc (ts);
	  json_object_set_new (env, "ts", json_string (ts));
	  json_object_set_new (env, "status", json_string ("ok"));
	trade_buy_done:
	  ;
	  json_object_set_new (env, "type", json_string ("trade.accepted"));
	  json_object_set_new (env, "data", data);
	  json_object_set_new (env, "error", json_null ());

	  /* Optional meta: signal replay=false on first-run */
	  json_t *meta = json_object ();
	  if (idem_key && *idem_key)
	    {
	      json_object_set_new (meta, "idempotent_replay", json_false ());
	      json_object_set_new (meta, "idempotency_key",
				   json_string (idem_key));
	    }
	  if (json_object_size (meta) > 0)
	    json_object_set_new (env, "meta", meta);
	  else
	    json_decref (meta);

	  /* If we’re idempotent, store the envelope JSON BEFORE sending */
	  if (idem_key && *idem_key)
	    {
	      char *env_json =
		json_dumps (env, JSON_COMPACT | JSON_SORT_KEYS);
	      if (!env_json
		  || db_idemp_store_response (idem_key,
					      env_json) != SQLITE_OK)
		{
		  if (env_json)
		    free (env_json);
		  json_decref (env);
		  send_enveloped_error (ctx->fd, root, 1500,
					"Database error");
		  goto done_trade_buy;
		}
	      free (env_json);
	    }

	  /* Send */
	  send_all_json (ctx->fd, env);
	  json_decref (env);

	done_trade_buy:
	  (void) 0;
	}
    }
  return 0;
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
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }
  send_enveloped_error (ctx->fd, root, 1101,
			"Not implemented: trade.jettison");
  return 0;
}
