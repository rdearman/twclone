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

// --------------------------  Buy/Sell  ------------------------------------------

////////////////////////////////////////////////////////////////////////////


/**
 * @brief Calculates the price a port will pay for a commodity.
 * (IMPLEMENT THIS FUNCTION)
 */
static int
h_calculate_port_buy_price (sqlite3 *db, int port_id, const char *commodity)
{
  /*
   * This function needs to fetch base price (from config?) and
   * the port's price_index_X column.
   * e.g., "SELECT price_index_ore FROM ports WHERE id = ?1"
   *
   * For now, returns a flat price.
   */
  fprintf (stderr, "STUB: h_calculate_port_buy_price(%d, %s) called\n",
           port_id, commodity);

  /* Placeholder implementation (replace this) */
  if (strcmp (commodity, "ore") == 0)
    return 100;
  if (strcmp (commodity, "organics") == 0)
    return 150;
  if (strcmp (commodity, "equipment") == 0)
    return 200;
  return 0;
}

/**
 * @brief Checks if a port is buying a specific commodity.
 * (IMPLEMENT THIS FUNCTION)
 */
static int
h_port_buys_commodity (sqlite3 *db, int port_id, const char *commodity)
{
  /*
   * This function queries the new 'port_trade' table.
   * "SELECT 1 FROM port_trade WHERE port_id = ?1 AND commodity = ?2 AND mode = 'buy' LIMIT 1"
   */
  fprintf (stderr, "STUB: h_port_buys_commodity(%d, %s) called\n", port_id,
           commodity);

  /* Placeholder implementation (replace this) */
  sqlite3_stmt *st = NULL;
  const char *SQL_SEL = "SELECT 1 FROM port_trade WHERE port_id = ?1 AND commodity = ?2 AND mode = 'buy' LIMIT 1";
  int rc = sqlite3_prepare_v2(db, SQL_SEL, -1, &st, NULL);
  if (rc != SQLITE_OK) return 0; // Fails safe (not buying)

  sqlite3_bind_int(st, 1, port_id);
  sqlite3_bind_text(st, 2, commodity, -1, SQLITE_STATIC);

  int buys = 0;
  if (sqlite3_step(st) == SQLITE_ROW) {
      buys = 1;
  }

  sqlite3_finalize(st);
  return buys; /* 1 = true (buys), 0 = false */
}


/*
 * ============================================================================
 * COMPLETE REFATORED FUNCTION: cmd_trade_sell
 * ============================================================================
 */

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

  /* --- 0. Initial Validation & Setup (Pre-Transaction) --- */

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

  /* Decloak ship */
  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  h_decloak_ship (db, player_ship_id);
  int player_sector = ctx->sector_id;

  /* Input parsing and validation (Non-DB) */
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

  /* Fast path: check idempotency table */
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

  /* 1) Validate port and fetch id */
  {
    /* CHANGED: 'sector_id' column in 'ports' is now 'location' */
    static const char *SQL_PORT =
      "SELECT id FROM ports WHERE sector = ?1 LIMIT 1;";
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

  /* 2) Begin transaction */
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
      return -1;
    }

  /* Initialize objects that need cleanup */
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

  /*
   * ========================================================================
   * --- REFACTORED TRANSACTION LOGIC ---
   * This block is replaced to use the new schema
   * ========================================================================
   */
  size_t n = json_array_size (jitems);
  for (size_t i = 0; i < n; ++i)
    {
      json_t *it = json_array_get (jitems, i);
      const char *commodity = NULL;
      int amount = 0;
      int buy_price = 0;
      sqlite3_stmt *st = NULL;

      json_t *jc = json_object_get (it, "commodity");
      json_t *ja = json_object_get (it, "quantity");
      if (json_is_string (jc))
        commodity = json_string_value (jc);
      if (json_is_integer (ja))
        amount = (int) json_integer_value (ja);

      /* Check for valid commodity string and amount */
      if (!commodity || amount <= 0
          || (strcmp (commodity, "ore") != 0
              && strcmp (commodity, "organics") != 0
              && strcmp (commodity, "equipment") != 0))
        {
          send_enveloped_error (ctx->fd, root, 400,
                                "items[] must contain {commodity, quantity>0}.");
          goto trade_sell_done;
        }

      /* Check if port is buying this commodity (from port_trade table) */
      if (!h_port_buys_commodity (db, port_id, commodity))
        {
          send_enveloped_error (ctx->fd, root, 1405,
                                "Port is not buying this commodity right now.");
          goto trade_sell_done;
        }

      /* Get port's buy price for this commodity (calculated) */
      buy_price = h_calculate_port_buy_price (db, port_id, commodity);
      if (buy_price <= 0)
        {
          send_enveloped_error (ctx->fd, root, 1405,
                                "Port is not buying this commodity right now.");
          goto trade_sell_done;
        }

      /*
       * Check player cargo (from ships table)
       * This is a simple pre-check. The atomic h_update_ship_cargo
       * will do the final, safe check.
       */
      {
        int ore, organics, equipment, holds;
        if (h_get_ship_cargo_and_holds (db, player_ship_id, &ore, &organics,
                                        &equipment, &holds) != SQLITE_OK)
        {
            send_enveloped_error(ctx->fd, root, 500, "Could not read ship cargo.");
            goto trade_sell_done;
        }

        int player_qty = 0;
        if (strcmp (commodity, "ore") == 0)
          player_qty = ore;
        else if (strcmp (commodity, "organics") == 0)
          player_qty = organics;
        else if (strcmp (commodity, "equipment") == 0)
          player_qty = equipment;

        if (player_qty < amount)
          {
            send_enveloped_error (ctx->fd, root, 1402,
                                  "You do not carry enough of that commodity.");
            goto trade_sell_done;
          }
      }

      /* --- Apply Effects (Updates) --- */

      /* Log trade */
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
        sqlite3_bind_int64 (st, 8, (sqlite3_int64) time (NULL));
        if (sqlite3_step (st) != SQLITE_DONE)
          {
            sqlite3_finalize (st);
            goto SQL_ERR;
          }
        sqlite3_finalize (st);
        st = NULL;
      }

      /* Decrement player cargo (Atomic) */
      {
        int new_ship_qty = 0;
        rc = h_update_ship_cargo (db, player_ship_id, commodity, -amount,
                                  &new_ship_qty);
        if (rc != SQLITE_OK)
          {
            if (rc == SQLITE_CONSTRAINT)
              {
                send_enveloped_error (ctx->fd, root, 1402,
                                      "Insufficient cargo to sell (atomic check).");
                goto trade_sell_done;
              }
            goto SQL_ERR;
          }
      }

      /* Increment port stock (Atomic) */
      {
        int new_port_qty = 0;
        rc = h_update_port_stock (db, port_id, commodity, amount,
                                  &new_port_qty);
        if (rc != SQLITE_OK)
          {
            if (rc == SQLITE_CONSTRAINT)
              {
                send_enveloped_error (ctx->fd, root, 1403,
                                      "Port cannot accept that much cargo (atomic check).");
                goto trade_sell_done;
              }
            goto SQL_ERR;
          }
      }

      /* Update totals and build receipt line */
      long long line_value = (long long) amount * (long long) buy_price;
      total_credits += line_value;

      json_t *jline = json_object ();
      json_object_set_new (jline, "commodity", json_string (commodity));
      json_object_set_new (jline, "quantity", json_integer (amount));
      json_object_set_new (jline, "unit_price", json_integer (buy_price));
      json_object_set_new (jline, "value", json_integer (line_value));
      json_array_append_new (lines, jline);
    }
  /* ========================================================================
   * --- END OF REFACTORED LOGIC ---
   * ========================================================================
   */

  /* Credit the player */
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

  /* Finalise receipt JSON */
  json_object_set_new (receipt, "total_credits",
                       json_integer (total_credits));

  /* 4) Persist idempotency record */
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
    goto trade_sell_cleanup_strings_only;   /* Error before ROLLBACK/DECREF */
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
    goto IDEMPOTENCY_RACE;   /* Jump to race handler */
      }
    sqlite3_finalize (st);
  }

  /* 5) Commit and reply */
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);   /* Failsafe */
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
      goto trade_sell_cleanup_strings_only;
    }
  send_enveloped_ok (ctx->fd, root, "trade.sell_receipt_v1", receipt);

  goto trade_sell_cleanup_strings_only;   /* Successful exit */

/* --- Refactored Error and Cleanup Flow ------------------------------------ */

SQL_ERR:
  /* Fallthrough for all generic SQL errors within the transaction */
  send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
  goto trade_sell_done;

IDEMPOTENCY_RACE:;
  /* Handle late-arriving duplicate idempotency insert */
  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);   /* Rollback our transaction */

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
  /* Fall through to main cleanup */

trade_sell_done:
  /* Cleanup path for all errors that occurred INSIDE the transaction loop. */
  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
  if (receipt)
    json_decref (receipt);

trade_sell_cleanup_strings_only:
  /* Final cleanup for dynamically allocated strings */
  if (req_s)
    free (req_s);
  if (resp_s)
    free (resp_s);
  return 0;
}


////////////////////////////////////////////////////////////////////////////
/// 		CUT HERE!!!
////////////////////////////////////////////////////////////////////////////

/* ========= Trading BUY ========= */

/*
 * ============================================================================
 * REQUIRED HELPER: h_update_port_stock
 * (Pasted from previous file)
 * ============================================================================
 */
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

  /* 2. Build the single, atomic SQL query */
  snprintf(sql_buf, sizeof(sql_buf),
    "UPDATE ports "
    "SET %s = CASE "
      "WHEN %s + ?2 < 0 THEN RAISE(ABORT, 'SQLITE_CONSTRAINT: Underflow') "
      "WHEN %s > 0 AND %s + ?2 > %s THEN RAISE(ABORT, 'SQLITE_CONSTRAINT: Overflow') "
      "ELSE %s + ?2 "
    "END "
    "WHERE id = ?1 "
    "RETURNING %s;",
    col_product,             /* SET product_ore = ... */
    col_product,             /* WHEN product_ore + ?2 < 0 */
    col_max, col_product, col_max, /* WHEN max_ore > 0 AND product_ore + ?2 > max_ore */
    col_product,             /* ELSE product_ore + ?2 */
    col_product              /* RETURNING product_ore */
  );

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql_buf, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return rc;
  }

  sqlite3_bind_int(stmt, 1, port_id);
  sqlite3_bind_int(stmt, 2, delta);

  /* 3. Execute the atomic update */
  rc = sqlite3_step(stmt);

  if (rc == SQLITE_ROW) {
    if (new_qty_out) {
      *new_qty_out = sqlite3_column_int(stmt, 0);
    }
    rc = SQLITE_OK;
  } else if (rc == SQLITE_DONE) {
    rc = SQLITE_NOTFOUND;
  }
  /* else: SQLITE_CONSTRAINT or other error is returned as-is */

  sqlite3_finalize(stmt);
  return rc;
}



/**
 * @brief Gets a ship's current cargo and total holds.
 * (IMPLEMENT THIS FUNCTION)
 */
int
h_get_ship_cargo_and_holds (sqlite3 *db, int ship_id, int *ore, int *organics,
                            int *equipment, int *holds)
{

  /* Placeholder implementation (replace this) */
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
 * @brief Calculates the price a port will CHARGE for a commodity.
 * (IMPLEMENT THIS FUNCTION)
 */
static int
h_calculate_port_sell_price (sqlite3 *db, int port_id, const char *commodity)
{
  /*
   * This function needs to fetch base price (from config?) and
   * the port's price_index_X column.
   * e.g., "SELECT price_index_ore FROM ports WHERE id = ?1"
   *
   * For now, returns a flat price.
   */
  fprintf (stderr, "STUB: h_calculate_port_sell_price(%d, %s) called\n",
           port_id, commodity);

  /* Placeholder implementation (replace this) */
  if (strcmp (commodity, "ore") == 0)
    return 120;
  if (strcmp (commodity, "organics") == 0)
    return 170;
  if (strcmp (commodity, "equipment") == 0)
    return 220;
  return 0;
}

/**
 * @brief Checks if a port is selling a specific commodity.
 * (IMPLEMENT THIS FUNCTION)
 */
static int
h_port_sells_commodity (sqlite3 *db, int port_id, const char *commodity)
{

  sqlite3_stmt *st = NULL;
  const char *SQL_SEL = "SELECT 1 FROM port_trade WHERE port_id = ?1 AND commodity = ?2 AND mode = 'sell' LIMIT 1";
  int rc = sqlite3_prepare_v2(db, SQL_SEL, -1, &st, NULL);
  if (rc != SQLITE_OK) return 0; // Fails safe (not selling)

  sqlite3_bind_int(st, 1, port_id);
  sqlite3_bind_text(st, 2, commodity, -1, SQLITE_STATIC);

  int sells = 0;
  if (sqlite3_step(st) == SQLITE_ROW) {
      sells = 1;
  }

  sqlite3_finalize(st);
  return sells; /* 1 = true (sells), 0 = false */
}

/**
 * @brief Atomically updates a player's credits.
 * (IMPLEMENT THIS FUNCTION)
 */
static int
h_update_player_credits(sqlite3 *db, int player_id, long long delta, long long *new_balance_out)
{
    /*
     * "UPDATE players
     * SET credits = CASE
     * WHEN credits + ?2 < 0 THEN RAISE(ABORT, 'SQLITE_CONSTRAINT: Insufficient funds')
     * ELSE credits + ?2
     * END
     * WHERE id = ?1
     * RETURNING credits;"
     */
    fprintf(stderr, "STUB: h_update_player_credits(%d, %lld) called\n", player_id, delta);

    /* Placeholder implementation */
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

/*
 * ============================================================================
 * COMPLETE REFATORED FUNCTION: cmd_trade_buy
 * ============================================================================
 */

int
cmd_trade_buy (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = NULL;
  json_t *receipt = NULL;
  json_t *lines = NULL;
  json_t *data = NULL;
  int sector_id = 0;
  const char *key = NULL;
  int port_id = 0;
  long long total_cost = 0;
  int total_cargo_space_needed = 0;
  int rc = 0;
  char *req_s = NULL;
  char *resp_s = NULL;

  if (!ctx || !root)
    return -1;

  db = db_get_handle ();

  TurnConsumeResult tc = h_consume_player_turn (db, ctx, "trade.buy");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "trade.buy", root,
                                            NULL);
    }

  /* --- 0. Initial Validation & Setup (Pre-Transaction) --- */

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

  /* Decloak ship */
  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);
  h_decloak_ship (db, player_ship_id);
  int player_sector = ctx->sector_id;

  /* Input parsing and validation (Non-DB) */
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

  /* Fast path: check idempotency table */
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
        send_enveloped_ok (ctx->fd, root, "trade.buy_receipt_v1",
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

  /* 1) Validate port and fetch id */
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

  /*
   * ========================================================================
   * --- PRE-TRANSACTION VALIDATION (BUY LOGIC) ---
   * ========================================================================
   */

  /*
   * We must check prices, player credits, and ship holds *before* starting
   * the transaction to avoid unnecessary rollbacks.
   */

  long long current_player_credits = 0;
  int temp_credits = 0;
  if (h_get_player_credits(db, ctx->player_id, &temp_credits) != SQLITE_OK)
  {
      send_enveloped_error(ctx->fd, root, 500, "Could not read player credits.");
      return -1;
  }
  current_player_credits = (long long)temp_credits;
  
  int current_ore, current_organics, current_equipment, current_holds;
  if (h_get_ship_cargo_and_holds(db, player_ship_id, &current_ore, &current_organics, &current_equipment, &current_holds) != SQLITE_OK)
  {
      send_enveloped_error(ctx->fd, root, 500, "Could not read ship cargo.");
      return -1;
  }
  int current_cargo_load = current_ore + current_organics + current_equipment;

  /*
   * This is a temporary array to hold the calculated line items
   * before we start the database transaction.
   */
  size_t n = json_array_size(jitems);
  struct TradeLine {
      const char* commodity;
      int amount;
      int sell_price;
      long long line_cost;
  } *trade_lines = calloc(n, sizeof(struct TradeLine));

  if (!trade_lines) {
      send_enveloped_error(ctx->fd, root, 500, "Memory allocation error.");
      return -1;
  }


  /* 3) Validate each item and calculate totals */
  for (size_t i = 0; i < n; ++i)
    {
      json_t *it = json_array_get (jitems, i);
      const char *commodity = NULL;
      int amount = 0;
      int sell_price = 0;

      json_t *jc = json_object_get (it, "commodity");
      json_t *ja = json_object_get (it, "quantity");
      if (json_is_string (jc))
        commodity = json_string_value (jc);
      if (json_is_integer (ja))
        amount = (int) json_integer_value (ja);

      /* Check for valid commodity string and amount */
      if (!commodity || amount <= 0
          || (strcmp (commodity, "ore") != 0
              && strcmp (commodity, "organics") != 0
              && strcmp (commodity, "equipment") != 0))
        {
          send_enveloped_error (ctx->fd, root, 400,
                                "items[] must contain {commodity, quantity>0}.");
          free(trade_lines);
          return -1;
        }

      /* Check if port is selling this commodity */
      if (!h_port_sells_commodity (db, port_id, commodity))
        {
          send_enveloped_error (ctx->fd, root, 1405,
                                "Port is not selling this commodity right now.");
          free(trade_lines);
          return -1;
        }

      /* Get port's sell price for this commodity */
      sell_price = h_calculate_port_sell_price (db, port_id, commodity);
      if (sell_price <= 0)
        {
          send_enveloped_error (ctx->fd, root, 1405,
                                "Port is not selling this commodity right now.");
          free(trade_lines);
          return -1;
        }

      /* Store details and update totals */
      trade_lines[i].commodity = commodity;
      trade_lines[i].amount = amount;
      trade_lines[i].sell_price = sell_price;
      trade_lines[i].line_cost = (long long)amount * (long long)sell_price;

      total_cost += trade_lines[i].line_cost;
      total_cargo_space_needed += amount;
    }

  /* 4) Final validation of totals */
  if (total_cost > current_player_credits)
  {
      send_enveloped_error(ctx->fd, root, 1402, "Insufficient credits for this purchase.");
      free(trade_lines);
      return -1;
  }

  if (total_cargo_space_needed + current_cargo_load > current_holds)
  {
      send_enveloped_error(ctx->fd, root, 1403, "Insufficient cargo space for this purchase.");
      free(trade_lines);
      return -1;
  }

  /*
   * ========================================================================
   * --- TRANSACTION BLOCK ---
   * All checks passed. Now we apply all changes in a single transaction.
   * ========================================================================
   */

  /* 5) Begin transaction */
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
      free(trade_lines);
      return -1;
    }

  /* Initialize receipt JSON */
  receipt = json_object ();
  lines = json_array ();
  if (!receipt || !lines)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, 500, "Memory allocation error.");
      free(trade_lines);
      return -1;
    }

  json_object_set_new (receipt, "sector_id", json_integer (sector_id));
  json_object_set_new (receipt, "port_id", json_integer (port_id));
  json_object_set_new (receipt, "player_id", json_integer (ctx->player_id));
  json_object_set_new (receipt, "lines", lines);


  /* 6) Apply all trade lines */
  for (size_t i = 0; i < n; ++i)
    {
      const char *commodity = trade_lines[i].commodity;
      int amount = trade_lines[i].amount;
      int sell_price = trade_lines[i].sell_price;
      long long line_cost = trade_lines[i].line_cost;
      sqlite3_stmt *st = NULL;

      /* Log trade */
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
        sqlite3_bind_double (st, 6, (double) sell_price);
        sqlite3_bind_text (st, 7, "buy", -1, SQLITE_STATIC);
        sqlite3_bind_int64 (st, 8, (sqlite3_int64) time (NULL));
        if (sqlite3_step (st) != SQLITE_DONE)
          {
            sqlite3_finalize (st);
            goto SQL_ERR;
          }
        sqlite3_finalize (st);
        st = NULL;
      }

      /* Increment player cargo (Atomic) */
      {
        int new_ship_qty = 0;
        rc = h_update_ship_cargo (db, player_ship_id, commodity, amount,
                                  &new_ship_qty);
        if (rc != SQLITE_OK)
          {
            if (rc == SQLITE_CONSTRAINT)
              {
                send_enveloped_error (ctx->fd, root, 1403,
                                      "Insufficient cargo space (atomic check).");
                goto trade_buy_done;
              }
            goto SQL_ERR;
          }
      }

      /* Decrement port stock (Atomic) */
      {
        int new_port_qty = 0;
        rc = h_update_port_stock (db, port_id, commodity, -amount,
                                  &new_port_qty);
        if (rc != SQLITE_OK)
          {
            if (rc == SQLITE_CONSTRAINT)
              {
                send_enveloped_error (ctx->fd, root, 1403,
                                      "Port is out of stock (atomic check).");
                goto trade_buy_done;
              }
            goto SQL_ERR;
          }
      }

      /* Build receipt line */
      json_t *jline = json_object ();
      json_object_set_new (jline, "commodity", json_string (commodity));
      json_object_set_new (jline, "quantity", json_integer (amount));
      json_object_set_new (jline, "unit_price", json_integer (sell_price));
      json_object_set_new (jline, "value", json_integer (line_cost));
      json_array_append_new (lines, jline);
    }

  /* 7) Deduct total cost from player */
  {
    long long new_balance = 0;
    rc = h_update_player_credits(db, ctx->player_id, -total_cost, &new_balance);
    if (rc != SQLITE_OK)
    {
        if (rc == SQLITE_CONSTRAINT)
        {
            send_enveloped_error(ctx->fd, root, 1402, "Insufficient credits (atomic check).");
            goto trade_buy_done;
        }
        goto SQL_ERR;
    }
    json_object_set_new (receipt, "credits_remaining", json_integer(new_balance));
  }


  /* Finalise receipt JSON */
  json_object_set_new (receipt, "total_cost",
                       json_integer (total_cost));

  /* 8) Persist idempotency record */
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
    goto trade_buy_cleanup_strings_only;   /* Error before ROLLBACK/DECREF */
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
    goto IDEMPOTENCY_RACE;   /* Jump to race handler */
      }
    sqlite3_finalize (st);
  }

  /* 9) Commit and reply */
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);   /* Failsafe */
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
      goto trade_buy_cleanup_strings_only;
    }
  send_enveloped_ok (ctx->fd, root, "trade.buy_receipt_v1", receipt);

  goto trade_buy_cleanup_strings_only;   /* Successful exit */

/* --- Refactored Error and Cleanup Flow ------------------------------------ */

SQL_ERR:
  /* Fallthrough for all generic SQL errors within the transaction */
  send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
  goto trade_buy_done;

IDEMPOTENCY_RACE:;
  /* Handle late-arriving duplicate idempotency insert */
  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);   /* Rollback our transaction */

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
            goto trade_buy_cleanup_strings_only;
          }
        send_enveloped_ok (ctx->fd, root, "trade.buy_receipt_v1",
                           stored_resp);
        json_decref (stored_resp);
        goto trade_buy_cleanup_strings_only;
          }
        else
          {
        send_enveloped_error (ctx->fd, root, 1105,
                              "Same idempotency_key used with different request.");
        goto trade_buy_cleanup_strings_only;
          }
      }
    sqlite3_finalize (st);
      }
    send_enveloped_error (ctx->fd, root, 500, "Could not resolve race.");
  }
  /* Fall through to main cleanup */

trade_buy_done:
  /* Cleanup path for all errors that occurred INSIDE the transaction loop. */
  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
  if (receipt)
    json_decref (receipt);

trade_buy_cleanup_strings_only:
  /* Final cleanup for dynamically allocated strings */
  if (req_s)
    free (req_s);
  if (resp_s)
    free (resp_s);
  if (trade_lines)
    free(trade_lines);
  return 0;
}


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
