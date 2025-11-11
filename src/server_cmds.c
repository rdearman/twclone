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
#include "server_players.h" // Include for banking functions
#include "server_cron.h" // Include for cron job functions

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

  pthread_mutex_lock (&db_mutex);

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

  rc = sqlite3_prepare_v2(db, ins_turns, -1, &st, NULL);

  if (rc == SQLITE_OK) {
    // ?1 is the player ID
    sqlite3_bind_int(st, 1, player_id); 
    sqlite3_step(st);
    sqlite3_finalize(st);
  }


  // Create ship
  const char *ins_ship = "INSERT INTO ships (name, type_id, sector) VALUES ('new ship', 1, 1);";
  rc = sqlite3_prepare_v2(db, ins_ship, -1, &st, NULL);
  if (rc == SQLITE_OK) {
    sqlite3_step(st);
    sqlite3_finalize(st);
  }

  int ship_id = (int) sqlite3_last_insert_rowid(db);

  // Update player's ship
  const char *upd_player = "UPDATE players SET ship = ?1 WHERE id = ?2;";
  rc = sqlite3_prepare_v2(db, upd_player, -1, &st, NULL);
  if (rc == SQLITE_OK) {
    sqlite3_bind_int(st, 1, ship_id);
    sqlite3_bind_int(st, 2, player_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
  }

  pthread_mutex_unlock (&db_mutex);

  return AUTH_OK;
}

int
cmd_sys_test_news_cron(client_ctx_t *ctx, json_t *root)
{
    sqlite3 *db = db_get_handle();
    json_t *data = json_object_get(root, "data");
    const char *subcommand = NULL;
    int rc = 0;

    if (!json_is_object(data)) {
        send_enveloped_error(ctx->fd, root, 400, "Missing data object.");
        return 0;
    }

    json_t *j_subcommand = json_object_get(data, "subcommand");
    if (json_is_string(j_subcommand)) {
        subcommand = json_string_value(j_subcommand);
    } else {
        send_enveloped_error(ctx->fd, root, 400, "Missing or invalid 'subcommand'.");
        return 0;
    }

    if (strcasecmp(subcommand, "generate_event") == 0) {
        const char *event_type = NULL;
        int actor_player_id = 0;
        int sector_id = 0;
        json_t *payload = NULL;

        json_t *j_event_type = json_object_get(data, "event_type");
        if (json_is_string(j_event_type)) {
            event_type = json_string_value(j_event_type);
        } else {
            send_enveloped_error(ctx->fd, root, 400, "Missing or invalid 'event_type'.");
            return 0;
        }

        json_t *j_actor_player_id = json_object_get(data, "actor_player_id");
        if (json_is_integer(j_actor_player_id)) {
            actor_player_id = json_integer_value(j_actor_player_id);
        }

        json_t *j_sector_id = json_object_get(data, "sector_id");
        if (json_is_integer(j_sector_id)) {
            sector_id = json_integer_value(j_sector_id);
        }

        json_t *j_payload = json_object_get(data, "payload");
        if (j_payload) {
            payload = json_deep_copy(j_payload); // Make a copy as db_log_engine_event consumes reference
        } else {
            payload = json_object(); // Empty payload if not provided
        }

        rc = db_log_engine_event((long long)time(NULL), event_type, "player", actor_player_id, sector_id, payload, NULL);
        if (rc == SQLITE_OK) {
            send_enveloped_ok(ctx->fd, root, "sys.test_news_cron.event_generated", NULL);
        } else {
            send_enveloped_error(ctx->fd, root, 500, "Failed to generate engine event.");
        }
    } else if (strcasecmp(subcommand, "run_compiler") == 0) {
        rc = h_daily_news_compiler(db, (long long)time(NULL));
        if (rc == SQLITE_OK) {
            send_enveloped_ok(ctx->fd, root, "sys.test_news_cron.compiler_ran", NULL);
        } else {
            send_enveloped_error(ctx->fd, root, 500, "Failed to run news compiler.");
        }
    } else if (strcasecmp(subcommand, "run_cleanup") == 0) {
        rc = h_cleanup_old_news(db, (long long)time(NULL));
        if (rc == SQLITE_OK) {
            send_enveloped_ok(ctx->fd, root, "sys.test_news_cron.cleanup_ran", NULL);
        } else {
            send_enveloped_error(ctx->fd, root, 500, "Failed to run news cleanup.");
        }
    } else {
        send_enveloped_error(ctx->fd, root, 400, "Unknown subcommand.");
    }

    return 0;
}

int
cmd_sys_raw_sql_exec(client_ctx_t *ctx, json_t *root)
{
    sqlite3 *db = db_get_handle();
    json_t *data = json_object_get(root, "data");
    const char *sql_str = NULL;
    char *err_msg = NULL;
    int rc = 0;

    if (!json_is_object(data)) {
        send_enveloped_error(ctx->fd, root, 400, "Missing data object.");
        return 0;
    }

    json_t *j_sql = json_object_get(data, "sql");
    if (json_is_string(j_sql)) {
        sql_str = json_string_value(j_sql);
    } else {
        send_enveloped_error(ctx->fd, root, 400, "Missing or invalid 'sql' string.");
        return 0;
    }

    rc = sqlite3_exec(db, sql_str, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        send_enveloped_error(ctx->fd, root, 500, err_msg ? err_msg : "Failed to execute SQL.");
        sqlite3_free(err_msg);
    } else {
        send_enveloped_ok(ctx->fd, root, "sys.raw_sql_exec.success", NULL);
    }
    return 0;
}

int
cmd_player_set_trade_account_preference(client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0) {
    send_enveloped_error(ctx->fd, root, 1401, "Authentication required.");
    return -1;
  }

  json_t *data = json_object_get(root, "data");
  if (!json_is_object(data)) {
    send_enveloped_error(ctx->fd, root, 400, "Missing data object.");
    return -1;
  }

  json_t *j_preference = json_object_get(data, "preference");
  if (!json_is_integer(j_preference)) {
    send_enveloped_error(ctx->fd, root, 400, "Missing or invalid 'preference' (must be 0 or 1).");
    return -1;
  }

  int preference = (int)json_integer_value(j_preference);
  if (preference != 0 && preference != 1) {
    send_enveloped_error(ctx->fd, root, 400, "Invalid preference value. Must be 0 (petty cash) or 1 (bank).");
    return -1;
  }

  char pref_str[16];
  snprintf(pref_str, sizeof(pref_str), "%d", preference);
  int rc = db_prefs_set_one(ctx->player_id, "trade.default_account", PT_INT, pref_str);
  if (rc != SQLITE_OK) {
    send_enveloped_error(ctx->fd, root, 500, "Failed to set trade account preference.");
    return -1;
  }

  send_enveloped_ok(ctx->fd, root, "player.set_trade_account_preference", NULL);
  return 0;
}
