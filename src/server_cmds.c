#include <string.h>
#include <strings.h>
#include <sqlite3.h>
#include <jansson.h>
#include "database.h"
#include "server_cmds.h"
#include "server_auth.h"
#include "server_envelope.h"
#include "db_player_settings.h"
#include "errors.h"
#include "server_players.h"	// Include for banking functions
#include "server_cron.h"	// Include for cron job functions
#include "server_log.h"

/* Constant-time string compare to reduce timing leakage (simple variant). */
static int
ct_str_eq (const char *a, const char *b)
{
  size_t la = a ? strlen (a) : 0;
  size_t lb = b ? strlen (b) : 0;
  size_t n = (la > lb) ? la : lb;
  unsigned char diff = (unsigned char) (la ^ lb);
  for (size_t i = 0; i < n; i++)
    {
      unsigned char ca = (i < la) ? (unsigned char) a[i] : 0;
      unsigned char cb = (i < lb) ? (unsigned char) b[i] : 0;
      diff |= (unsigned char) (ca ^ cb);
    }
  return diff == 0;
}



int
play_login (const char *player_name, const char *password, int *out_player_id)
{
  if (!player_name || !password)
    return AUTH_ERR_BAD_REQUEST;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return AUTH_ERR_DB;

  LOGI ("play_login: attempting to lock db_mutex for player %s", player_name);
  pthread_mutex_lock (&db_mutex);
  LOGI ("play_login: db_mutex locked for player %s", player_name);

  const char *sql = "SELECT id, passwd, type FROM players WHERE name=?1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_DB;
    }

  sqlite3_bind_text (st, 1, player_name, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);
  if (rc != SQLITE_ROW)
    {
      sqlite3_finalize (st);
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_INVALID_CRED;	/* no such user */
    }

  const int player_id = sqlite3_column_int (st, 0);
  const unsigned char *dbpass_u8 = sqlite3_column_text (st, 1);
  const int player_type = sqlite3_column_int (st, 2);

  /* Copy the password BEFORE finalize; column_text ptr is invalid after finalize */
  char *dbpass = dbpass_u8 ? strdup ((const char *) dbpass_u8) : NULL;

  sqlite3_finalize (st);
  pthread_mutex_unlock (&db_mutex);

  /* Block NPC logins */
  if (player_type == 1)
    {
      if (dbpass)
	free (dbpass);
      return ERR_IS_NPC;
    }

  /* Compare password (constant-time helper) */
  int ok = (dbpass != NULL) && ct_str_eq (password, dbpass);
  if (dbpass)
    free (dbpass);
  if (!ok)
    return AUTH_ERR_INVALID_CRED;

  if (out_player_id)
    *out_player_id = player_id;

  return AUTH_OK;
}

/* Create new user with unique name.
   - Ensures name isn't taken.
   - Assigns legacy 'number' as MAX(number)+1 atomically via SELECT aggregate.
   - Returns new rowid as player_id. */
int
user_create (const char *player_name, const char *password,
	     int *out_player_id)
{
  if (!player_name || !password)
    return AUTH_ERR_BAD_REQUEST;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return AUTH_ERR_DB;

  int rc;
  sqlite3_stmt *st = NULL;

  pthread_mutex_lock (&db_mutex);

  /* 1) Name uniqueness check */
  const char *chk = "SELECT 1 FROM players WHERE name=?1 LIMIT 1;";
  rc = sqlite3_prepare_v2 (db, chk, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_DB;
    }
  sqlite3_bind_text (st, 1, player_name, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc == SQLITE_ROW)
    {
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_NAME_TAKEN;
    }

  /* 2) Insert new player with sequential 'number' */
  const char *ins =
    "INSERT INTO players (name, passwd, number) "
    "SELECT ?1, ?2, COALESCE(MAX(number),0)+1 FROM players;";

  rc = sqlite3_prepare_v2 (db, ins, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_DB;
    }

  sqlite3_bind_text (st, 1, player_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 2, password, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);
  int done_ok = (rc == SQLITE_DONE);
  sqlite3_finalize (st);

  if (!done_ok)
    {
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_DB;
    }

  if (out_player_id)
    *out_player_id = (int) sqlite3_last_insert_rowid (db);

  int player_id = (int) sqlite3_last_insert_rowid (db);

  // Corrected code with INSERT...SELECT on turns
  const char *ins_turns =
    "INSERT INTO turns (player, turns_remaining, last_update) "
    "SELECT ?1, turnsperday, strftime('%s', 'now') FROM config WHERE id = 1;";

  rc = sqlite3_prepare_v2 (db, ins_turns, -1, &st, NULL);

  if (rc == SQLITE_OK)
    {
      // ?1 is the player ID
      sqlite3_bind_int (st, 1, player_id);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }


  // Create ship
  const char *ins_ship =
    "INSERT INTO ships (name, type_id, sector) VALUES ('new ship', 1, 1);";
  rc = sqlite3_prepare_v2 (db, ins_ship, -1, &st, NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_step (st);
      sqlite3_finalize (st);
    }

  int ship_id = (int) sqlite3_last_insert_rowid (db);

  // Update player's ship
  const char *upd_player = "UPDATE players SET ship = ?1 WHERE id = ?2;";
  rc = sqlite3_prepare_v2 (db, upd_player, -1, &st, NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, ship_id);
      sqlite3_bind_int (st, 2, player_id);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }

  pthread_mutex_unlock (&db_mutex);

  return AUTH_OK;
}

int
cmd_sys_test_news_cron (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");
  const char *subcommand = NULL;
  int rc = 0;

  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      return 0;
    }

  json_t *j_subcommand = json_object_get (data, "subcommand");
  if (json_is_string (j_subcommand))
    {
      subcommand = json_string_value (j_subcommand);
    }
  else
    {
      send_enveloped_error (ctx->fd, root, 400,
			    "Missing or invalid 'subcommand'.");
      return 0;
    }

  if (strcasecmp (subcommand, "generate_event") == 0)
    {
      const char *event_type = NULL;
      int actor_player_id = 0;
      int sector_id = 0;
      json_t *payload = NULL;

      json_t *j_event_type = json_object_get (data, "event_type");
      if (json_is_string (j_event_type))
	{
	  event_type = json_string_value (j_event_type);
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, 400,
				"Missing or invalid 'event_type'.");
	  return 0;
	}

      json_t *j_actor_player_id = json_object_get (data, "actor_player_id");
      if (json_is_integer (j_actor_player_id))
	{
	  actor_player_id = json_integer_value (j_actor_player_id);
	}

      json_t *j_sector_id = json_object_get (data, "sector_id");
      if (json_is_integer (j_sector_id))
	{
	  sector_id = json_integer_value (j_sector_id);
	}

      json_t *j_payload = json_object_get (data, "payload");
      if (j_payload)
	{
	  payload = json_deep_copy (j_payload);	// Make a copy as db_log_engine_event consumes reference
	}
      else
	{
	  payload = json_object ();	// Empty payload if not provided
	}

      rc =
	db_log_engine_event ((long long) time (NULL), event_type, "player",
			     actor_player_id, sector_id, payload, NULL);
      if (rc == SQLITE_OK)
	{
	  send_enveloped_ok (ctx->fd, root,
			     "sys.test_news_cron.event_generated", NULL);
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, 500,
				"Failed to generate engine event.");
	}
    }
  else if (strcasecmp (subcommand, "run_compiler") == 0)
    {
      rc = h_daily_news_compiler (db, (long long) time (NULL));
      if (rc == SQLITE_OK)
	{
	  send_enveloped_ok (ctx->fd, root, "sys.test_news_cron.compiler_ran",
			     NULL);
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, 500,
				"Failed to run news compiler.");
	}
    }
  else if (strcasecmp (subcommand, "run_cleanup") == 0)
    {
      rc = h_cleanup_old_news (db, (long long) time (NULL));
      if (rc == SQLITE_OK)
	{
	  send_enveloped_ok (ctx->fd, root, "sys.test_news_cron.cleanup_ran",
			     NULL);
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, 500,
				"Failed to run news cleanup.");
	}
    }
  else
    {
      send_enveloped_error (ctx->fd, root, 400, "Unknown subcommand.");
    }

  return 0;
}

int
cmd_sys_raw_sql_exec (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");
  const char *sql_str = NULL;
  sqlite3_stmt *stmt = NULL;
  int rc = 0;

  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      return 0;
    }

  json_t *j_sql = json_object_get (data, "sql");
  if (json_is_string (j_sql))
    {
      sql_str = json_string_value (j_sql);
    }
  else
    {
      send_enveloped_error (ctx->fd, root, 400,
			    "Missing or invalid 'sql' string.");
      return 0;
    }

  rc = sqlite3_prepare_v2 (db, sql_str, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg(db));
      return 0;
    }

  json_t *rows = json_array();
  int col_count = sqlite3_column_count(stmt);

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
      json_t *row = json_array();
      for (int i = 0; i < col_count; i++) {
          int type = sqlite3_column_type(stmt, i);
          switch (type) {
              case SQLITE_INTEGER:
                  json_array_append_new(row, json_integer(sqlite3_column_int64(stmt, i)));
                  break;
              case SQLITE_FLOAT:
                  json_array_append_new(row, json_real(sqlite3_column_double(stmt, i)));
                  break;
              case SQLITE_TEXT:
                  json_array_append_new(row, json_string((const char *)sqlite3_column_text(stmt, i)));
                  break;
              case SQLITE_NULL:
                  json_array_append_new(row, json_null());
                  break;
              default:
                  // Blob or other? Treat as string for now or null
                  json_array_append_new(row, json_string("BLOB/UNKNOWN"));
                  break;
          }
      }
      json_array_append_new(rows, row);
  }

  if (rc != SQLITE_DONE) {
       send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg(db));
       sqlite3_finalize(stmt);
       json_decref(rows);
       return 0;
  }

  sqlite3_finalize(stmt);

  json_t *resp = json_object();
  json_object_set_new(resp, "rows", rows);
  send_enveloped_ok (ctx->fd, root, "sys.raw_sql_exec.success", resp);
  return 0;
}

int
cmd_player_set_trade_account_preference (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_enveloped_error (ctx->fd, root, 1401, "Authentication required.");
      return -1;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      return -1;
    }

  json_t *j_preference = json_object_get (data, "preference");
  if (!json_is_integer (j_preference))
    {
      send_enveloped_error (ctx->fd, root, 400,
			    "Missing or invalid 'preference' (must be 0 or 1).");
      return -1;
    }

  int preference = (int) json_integer_value (j_preference);
  if (preference != 0 && preference != 1)
    {
      send_enveloped_error (ctx->fd, root, 400,
			    "Invalid preference value. Must be 0 (petty cash) or 1 (bank).");
      return -1;
    }

  char pref_str[16];
  snprintf (pref_str, sizeof (pref_str), "%d", preference);
  int rc = db_prefs_set_one (ctx->player_id, "trade.default_account", PT_INT,
			     pref_str);
  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500,
			    "Failed to set trade account preference.");
      return -1;
    }

  send_enveloped_ok (ctx->fd, root, "player.set_trade_account_preference",
		     NULL);
  return 0;
}

// General JSON response helpers
int
send_error_response (client_ctx_t *ctx, json_t *root, int err_code,
		     const char *msg)
{
  // server_envelope.h's send_enveloped_error expects the original root message for id/cmd matching.
  // The err_code and msg are passed directly.
  send_enveloped_error (ctx->fd, root, err_code, msg);
  return 0;			// Or -1 depending on desired return behavior
}

int
send_json_response (client_ctx_t *ctx, json_t *response_json)
{
  // server_envelope.h's send_enveloped_ok expects the original root and a data object.
  // Here, response_json *is* the data object we want to send.
  // We create a temporary object to hold the response_json under a "data" key,
  // or you might adjust send_enveloped_ok to directly accept the data.
  // For now, let's assume response_json contains the actual data to be sent.
  // If response_json needs to be wrapped in a "data" field, we'd do:
  // json_t *wrapper = json_object();
  // json_object_set_new(wrapper, "data", response_json);
  // send_enveloped_ok(ctx->fd, root, "ok", wrapper);
  // json_decref(wrapper); // assuming send_enveloped_ok takes ownership or copies.

  // A more direct approach if send_enveloped_ok just sends "ok" and an arbitrary json_t
  // For now, mimicking existing pattern where "data" is often implicitly handled,
  // or the `response_json` is the actual payload.
  // Looking at `send_enveloped_ok` in `server_envelope.h`... it takes `json_t *data`.
  // So, we just pass the `response_json` directly as data.
  // If `response_json` should be nested under a specific key, that logic would be here.
  // Given the prototype: `send_enveloped_ok(int fd, json_t *original_request_root, const char *command_name, json_t *data)`
  // `response_json` should likely be the `data` parameter.
  send_enveloped_ok (ctx->fd, NULL, "ok", response_json);	// original_request_root is not needed here
  return 0;
}

/* --- Bounty Commands --- */

// Helper: Check if sector is FedSpace
static bool is_fedspace_sector(int sector_id) {
    return (sector_id >= 1 && sector_id <= 10);
}

// Helper: Check if port is Black Market
static bool is_black_market_port(sqlite3 *db, int port_id) {
    sqlite3_stmt *st;
    int rc = sqlite3_prepare_v2(db, "SELECT type, name FROM ports WHERE id = ?", -1, &st, NULL);
    if (rc != SQLITE_OK) return false;
    sqlite3_bind_int(st, 1, port_id);
    bool is_bm = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        // int type = sqlite3_column_int(st, 0); // Unused
        const char *name = (const char *)sqlite3_column_text(st, 1);
        if (name && strstr(name, "Black Market")) is_bm = true;
    }
    sqlite3_finalize(st);
    return is_bm;
}

int cmd_bounty_post_federation(client_ctx_t *ctx, json_t *root) {
    if (ctx->player_id <= 0) {
        send_enveloped_error(ctx->fd, root, 1401, "Authentication required.");
        return 0;
    }
    sqlite3 *db = db_get_handle();
    
    json_t *data = json_object_get(root, "data");
    if (!json_is_object(data)) {
        send_enveloped_error(ctx->fd, root, 400, "Missing data object.");
        return 0;
    }
    
    int target_player_id = 0;
    json_t *j_target_id = json_object_get(data, "target_player_id");
    if (j_target_id) {
        int type = json_typeof(j_target_id);
        if (type == JSON_STRING) {
            LOGD("cmd_bounty_post_federation: target_player_id is STRING: '%s'", json_string_value(j_target_id));
        } else if (type == JSON_INTEGER) {
            LOGD("cmd_bounty_post_federation: target_player_id is INTEGER: %lld", (long long)json_integer_value(j_target_id));
        } else {
            LOGD("cmd_bounty_post_federation: target_player_id is type %d", type);
        }
    } else {
        LOGD("cmd_bounty_post_federation: target_player_id key missing.");
    }

    if (!json_get_int_flexible(data, "target_player_id", &target_player_id) || target_player_id <= 0) {
        LOGE("cmd_bounty_post_federation: Invalid target_player_id received: %d", target_player_id);
        send_enveloped_error(ctx->fd, root, 400, "Invalid target_player_id.");
        return 0;
    }
    
    long long amount;
    if (!json_get_int64_flexible(data, "amount", &amount) || amount <= 0) {
        send_enveloped_error(ctx->fd, root, 400, "Invalid amount.");
        return 0;
    }
    
    const char *desc = json_string_value(json_object_get(data, "description"));
    if (!desc) desc = "Wanted for crimes against the Federation.";

    int sector = 0;
    db_player_get_sector(ctx->player_id, &sector);
    if (!is_fedspace_sector(sector)) {
        send_enveloped_error(ctx->fd, root, 1403, "Must be in FedSpace to post a Federation bounty.");
        return 0;
    }

    int issuer_alignment = 0;
    db_player_get_alignment(db, ctx->player_id, &issuer_alignment);
    if (issuer_alignment < 0) {
        send_enveloped_error(ctx->fd, root, 1403, "Evil players cannot post Federation bounties.");
        return 0;
    }

    int target_alignment = 0;
    if (db_player_get_alignment(db, target_player_id, &target_alignment) != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, 404, "Target player not found.");
        return 0;
    }
    if (target_alignment >= 0) {
        send_enveloped_error(ctx->fd, root, 1403, "Target must be an evil player.");
        return 0;
    }
    if (target_player_id == ctx->player_id) {
        send_enveloped_error(ctx->fd, root, 400, "Cannot post bounty on yourself.");
        return 0;
    }

    long long new_balance = 0;
    if (h_deduct_credits(db, "player", ctx->player_id, amount, "WITHDRAWAL", NULL, &new_balance) != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, 1402, "Insufficient funds.");
        return 0;
    }

    if (db_bounty_create(db, "player", ctx->player_id, "player", target_player_id, amount, desc) != SQLITE_OK) {
        h_add_credits(db, "player", ctx->player_id, amount, "DEPOSIT", NULL, &new_balance);
        send_enveloped_error(ctx->fd, root, 500, "Database error creating bounty.");
        return 0;
    }

    send_enveloped_ok(ctx->fd, root, "bounty.post_federation.confirmed", NULL);
    return 0;
}

int cmd_bounty_post_hitlist(client_ctx_t *ctx, json_t *root) {
    if (ctx->player_id <= 0) {
        send_enveloped_error(ctx->fd, root, 1401, "Authentication required.");
        return 0;
    }
    sqlite3 *db = db_get_handle();
    
    json_t *data = json_object_get(root, "data");
    if (!json_is_object(data)) {
        send_enveloped_error(ctx->fd, root, 400, "Missing data object.");
        return 0;
    }
    
    int target_player_id = 0;
    json_t *j_target_id = json_object_get(data, "target_player_id");
    if (j_target_id) {
        int type = json_typeof(j_target_id);
        if (type == JSON_STRING) {
            LOGD("cmd_bounty_post_hitlist: target_player_id is STRING: '%s'", json_string_value(j_target_id));
        } else if (type == JSON_INTEGER) {
            LOGD("cmd_bounty_post_hitlist: target_player_id is INTEGER: %lld", (long long)json_integer_value(j_target_id));
        } else {
            LOGD("cmd_bounty_post_hitlist: target_player_id is type %d", type);
        }
    } else {
        LOGD("cmd_bounty_post_hitlist: target_player_id key missing.");
    }

    if (!json_get_int_flexible(data, "target_player_id", &target_player_id) || target_player_id <= 0) {
        LOGE("cmd_bounty_post_hitlist: Invalid target_player_id received: %d", target_player_id);
        send_enveloped_error(ctx->fd, root, 400, "Invalid target_player_id.");
        return 0;
    }
    
    long long amount;
    if (!json_get_int64_flexible(data, "amount", &amount) || amount <= 0) {
        send_enveloped_error(ctx->fd, root, 400, "Invalid amount.");
        return 0;
    }
    
    const char *desc = json_string_value(json_object_get(data, "description"));
    if (!desc) desc = "Hit ordered.";

    int sector = 0;
    db_player_get_sector(ctx->player_id, &sector);

    int port_id = db_get_port_id_by_sector(sector);
    if (port_id <= 0 || !is_black_market_port(db, port_id)) {
         send_enveloped_error(ctx->fd, root, 1403, "Must be at a Black Market port to post a Hit List contract.");
         return 0;
    }

    int issuer_alignment = 0;
    db_player_get_alignment(db, ctx->player_id, &issuer_alignment);
    if (issuer_alignment > 0) {
        send_enveloped_error(ctx->fd, root, 1403, "Good players cannot post Hit List contracts.");
        return 0;
    }

    int target_alignment = 0;
    if (db_player_get_alignment(db, target_player_id, &target_alignment) != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, 404, "Target player not found.");
        return 0;
    }
    if (target_alignment <= 0) {
        send_enveloped_error(ctx->fd, root, 1403, "Target must be a good player.");
        return 0;
    }
    if (target_player_id == ctx->player_id) {
        send_enveloped_error(ctx->fd, root, 400, "Cannot put a hit on yourself.");
        return 0;
    }

    long long new_balance = 0;
    if (h_deduct_credits(db, "player", ctx->player_id, amount, "WITHDRAWAL", NULL, &new_balance) != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, 1402, "Insufficient funds.");
        return 0;
    }

    if (db_bounty_create(db, "player", ctx->player_id, "player", target_player_id, amount, desc) != SQLITE_OK) {
        h_add_credits(db, "player", ctx->player_id, amount, "DEPOSIT", NULL, &new_balance);
        send_enveloped_error(ctx->fd, root, 500, "Database error creating bounty.");
        return 0;
    }

    send_enveloped_ok(ctx->fd, root, "bounty.post_hitlist.confirmed", NULL);
    return 0;
}

int cmd_bounty_list(client_ctx_t *ctx, json_t *root) {
    if (ctx->player_id <= 0) {
        send_enveloped_error(ctx->fd, root, 1401, "Authentication required.");
        return 0;
    }
    sqlite3 *db = db_get_handle();

    int alignment = 0;
    db_player_get_alignment(db, ctx->player_id, &alignment);

    char *sql = NULL;
    if (alignment >= 0) {
        sql = sqlite3_mprintf(
            "SELECT b.id, b.target_id, p.name, b.reward, b.posted_by_type "
            "FROM bounties b "
            "JOIN players p ON b.target_id = p.id "
            "WHERE b.status = 'open' AND p.alignment < 0 "
            "ORDER BY b.reward DESC LIMIT 20;"
        );
    } else {
        sql = sqlite3_mprintf(
            "SELECT b.id, b.target_id, p.name, b.reward, b.posted_by_type "
            "FROM bounties b "
            "JOIN players p ON b.target_id = p.id "
            "WHERE b.status = 'open' AND (p.alignment > 0 OR b.posted_by_type = 'gov') "
            "ORDER BY b.reward DESC LIMIT 20;"
        );
    }

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
        sqlite3_free(sql);
        send_enveloped_error(ctx->fd, root, 500, "Database error listing bounties.");
        return 0;
    }
    sqlite3_free(sql);

    json_t *arr = json_array();
    while (sqlite3_step(st) == SQLITE_ROW) {
        json_t *item = json_object();
        json_object_set_new(item, "bounty_id", json_integer(sqlite3_column_int(st, 0)));
        json_object_set_new(item, "target_id", json_integer(sqlite3_column_int(st, 1)));
        json_object_set_new(item, "target_name", json_string((const char*)sqlite3_column_text(st, 2)));
        json_object_set_new(item, "reward", json_integer(sqlite3_column_int64(st, 3)));
        json_object_set_new(item, "posted_by_type", json_string((const char*)sqlite3_column_text(st, 4)));
        json_array_append_new(arr, item);
    }
    sqlite3_finalize(st);

    json_t *resp = json_object();
    json_object_set_new(resp, "bounties", arr);
    send_enveloped_ok(ctx->fd, root, "bounty.list.success", resp);
    return 0;
}
