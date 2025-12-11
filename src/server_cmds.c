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
#include "server_players.h"     // Include for banking functions
#include "server_cron.h"        // Include for cron job functions
#include "server_log.h"
#include "server_clusters.h" // NEW
#include "database_market.h"   // NEW
#include "server_ports.h"      // NEW: For h_get_port_commodity_quantity if needed, or direct DB queries
#include "server_universe.h"   // NEW: For fer_tick

int
cmd_sys_cluster_init (client_ctx_t *ctx, json_t *root)
{
#ifdef BUILD_PRODUCTION
  send_enveloped_error(ctx->fd, root, 1403, "Command disabled in production");
  return 0;
#else
  sqlite3 *db = db_get_handle ();
  int rc = 0;
  if (ctx->player_id <= 0)   // Very basic auth check, really should be admin
    {
      send_enveloped_error (ctx->fd, root, 1401, "Auth required");
      return 0;
    }
  // Ensure player is an admin/sysop.
  if (auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_enveloped_error (ctx->fd, root, 403, "Forbidden: Must be SysOp");
      return 0;
    }
  rc = clusters_init (db);
  if (rc != 0)
    {
      send_enveloped_error (ctx->fd, root, 500, "clusters_init failed");
      return 0;
    }
  rc = clusters_seed_illegal_goods (db);
  if (rc != 0)
    {
      send_enveloped_error (ctx->fd, root, 500, "seed failed");
      return 0;
    }
  send_enveloped_ok (ctx->fd, root, "sys.cluster.init.ok", NULL);
  return 0;
#endif
}


int
cmd_sys_cluster_seed_illegal_goods (client_ctx_t *ctx, json_t *root)
{
#ifdef BUILD_PRODUCTION
  send_enveloped_error(ctx->fd, root, 1403, "Command disabled in production");
  return 0;
#else
  sqlite3 *db = db_get_handle ();
  int rc = 0;
  if (ctx->player_id <= 0)   // Very basic auth check, really should be admin
    {
      send_enveloped_error (ctx->fd, root, 1401, "Auth required");
      return 0;
    }
  // Ensure player is an admin/sysop.
  if (auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_enveloped_error (ctx->fd, root, 403, "Forbidden: Must be SysOp");
      return 0;
    }
  rc = clusters_seed_illegal_goods (db);
  if (rc != 0)
    {
      send_enveloped_error (ctx->fd, root, 500, "seed failed");
      return 0;
    }
  send_enveloped_ok (ctx->fd, root, "sys.cluster.seed_illegal_goods.ok", NULL);
  return 0;
#endif
}


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
    {
      return AUTH_ERR_BAD_REQUEST;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return AUTH_ERR_DB;
    }
  const char *sql = "SELECT id, passwd, type FROM players WHERE name=?1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return AUTH_ERR_DB;
    }
  sqlite3_bind_text (st, 1, player_name, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  if (rc != SQLITE_ROW)
    {
      sqlite3_finalize (st);
      return AUTH_ERR_INVALID_CRED;     /* no such user */
    }
  const int player_id = sqlite3_column_int (st, 0);
  const unsigned char *dbpass_u8 = sqlite3_column_text (st, 1);
  const int player_type = sqlite3_column_int (st, 2);
  /* Copy the password BEFORE finalize; column_text ptr is invalid after finalize */
  char *dbpass = dbpass_u8 ? strdup ((const char *) dbpass_u8) : NULL;


  sqlite3_finalize (st);
  /* Block NPC logins */
  if (player_type == 1)
    {
      if (dbpass)
        {
          free (dbpass);
        }
      return ERR_IS_NPC;
    }
  /* Compare password (constant-time helper) */
  LOGI ("play_login debug: player_id=%d, input_pass='%s', db_pass='%s'",
        player_id,
        password,
        dbpass ? dbpass : "(null)");
  int ok = (dbpass != NULL) && ct_str_eq (password, dbpass);


  if (dbpass)
    {
      free (dbpass);
    }
  if (!ok)
    {
      LOGI ("play_login debug: Password mismatch");
      return AUTH_ERR_INVALID_CRED;
    }
  if (out_player_id)
    {
      *out_player_id = player_id;
    }
  return AUTH_OK;
}


/* Create a new player and initialise their turns row.
 *
 * On success:
 *   - inserts into players
 *   - upserts into turns (one row per player)
 *   - optionally returns player_id via out_player_id
 */
int
user_create (sqlite3 *db,
             const char *player_name,
             const char *password,
             int *out_player_id)
{
  if (!player_name || !password)
    {
      if (!db)
        {
          LOGE ("user_create: db handle is NULL");
          return AUTH_ERR_DB;
        }
    }
  int rc = 0;
  int rc_prepared = 0;
  int player_id = 0;
  sqlite3_stmt *stmt = NULL;
  const char *ins_players =
    "INSERT INTO players "
    "  (name, passwd) "
    "VALUES "
    "  (?1, ?2);";  /* NOTE: this is now an UPSERT on turns.player.
                     *
                     * - First-time registration → INSERT row with starting turns.
                     * - If a stale row already exists in turns for this player_id
                     *   (e.g. from an old test run or seed data), we UPDATE it
                     *   instead of throwing SQLITE_CONSTRAINT.
                     */
  const char *ins_turns =
    "INSERT INTO turns (player, turns_remaining, last_update) "
    "SELECT "
    " ?1, "
    " value, "
    "  strftime('%s','now') "
    "FROM config "
    "WHERE key='turnsperday' "
    "ON CONFLICT(player) DO UPDATE SET "
    "  turns_remaining = excluded.turns_remaining, "
    "  last_update    = excluded.last_update;";


  LOGE ("user_create debug: Inserting player '%s' with passwd '%s'",
        player_name, password);
  /* Insert into players */
  rc_prepared = sqlite3_prepare_v2 (db, ins_players, -1, &stmt, NULL);
  if (rc_prepared != SQLITE_OK)
    {
      LOGE ("user_create: Failed to prepare players insert: %s",
            sqlite3_errmsg (db));
      if (stmt)
        {
          sqlite3_finalize (stmt);
        }
      return AUTH_ERR_DB;
    }
  sqlite3_bind_text (stmt, 1, player_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, 2, password, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      int ext_err = sqlite3_extended_errcode (db);


      if (ext_err == SQLITE_CONSTRAINT_UNIQUE ||
          ext_err == SQLITE_CONSTRAINT_PRIMARYKEY)
        {
          LOGE ("user_create: Username '%s' already exists (constraint)",
                player_name);
          sqlite3_finalize (stmt);
          return ERR_NAME_TAKEN;
        }
      LOGE ("user_create: Failed to insert into players: rc=%d, err=%s",
            rc, sqlite3_errmsg (db));
      sqlite3_finalize (stmt);
      return AUTH_ERR_DB;
    }
  sqlite3_finalize (stmt);
  stmt = NULL;
  /* Get new player_id */
  player_id = (int) sqlite3_last_insert_rowid (db);
  if (out_player_id)
    {
      *out_player_id = player_id;
    }
  /* Upsert into turns for this player */
  rc_prepared = sqlite3_prepare_v2 (db, ins_turns, -1, &stmt, NULL);
  if (rc_prepared != SQLITE_OK)
    {
      LOGE ("user_create: Failed to prepare turns upsert for player %d: %s",
            player_id, sqlite3_errmsg (db));
      if (stmt)
        {
          sqlite3_finalize (stmt);
        }
      return AUTH_ERR_DB;
    }
  sqlite3_bind_int (stmt, 1, player_id);
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE (
        "user_create: Failed to upsert into turns table for player %d: rc=%d, err=%s",
        player_id,
        rc,
        sqlite3_errmsg (db));
      sqlite3_finalize (stmt);
      return AUTH_ERR_DB;
    }
  sqlite3_finalize (stmt);

  /* Create default bank account for this player */
  if (db_bank_account_create_default_for_player (db, player_id) != 0)
    {
      LOGE ("user_create: Failed to create default bank account for player %d",
            player_id);
      return AUTH_ERR_DB;     // Or a more specific error
    }

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
          payload = json_deep_copy (j_payload); // Make a copy as db_log_engine_event consumes reference
        }
      else
        {
          payload = json_object ();     // Empty payload if not provided
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
#ifdef BUILD_PRODUCTION
  send_enveloped_error(ctx->fd, root, 1403, "Command disabled in production");
  return 0;
#else
  json_t *jdata = json_object_get (root, "data");
  const char *sql = NULL;
  if (json_is_object (jdata))
    {
      sql = json_string_value (json_object_get (jdata, "sql"));
    }
  if (!sql)
    {
      send_enveloped_error (ctx->fd, root, 1301, "Missing sql");
      return 0;
    }
  sqlite3 *db = db_get_handle ();
  char *err = NULL;
  int rc = sqlite3_exec (db, sql, NULL, NULL, &err);
  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 1500, err ? err : "SQL error");
      if (err)
        sqlite3_free (err);
    }
  else
    {
      send_enveloped_ok (ctx->fd, root, "sys.raw_sql_exec",
                         json_string ("Executed"));
    }
  return 0;
#endif
}


// General JSON response helpers
int
send_error_response (client_ctx_t *ctx, json_t *root, int err_code,
                     const char *msg)
{
  // server_envelope.h's send_enveloped_error expects the original root message for id/cmd matching.
  // The err_code and msg are passed directly.
  send_enveloped_error (ctx->fd, root, err_code, msg);
  return 0;                     // Or -1 depending on desired return behavior
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
  send_enveloped_ok (ctx->fd, NULL, "ok", response_json);       // original_request_root is not needed here
  return 0;
}


/* --- Bounty Commands --- */


// Helper: Check if sector is FedSpace
static bool
is_fedspace_sector (int sector_id)
{
  return (sector_id >= 1 && sector_id <= 10);
}


// Helper: Check if port is Black Market
static bool
is_black_market_port (sqlite3 *db, int port_id)
{
  sqlite3_stmt *st;
  int rc = sqlite3_prepare_v2 (db,
                               "SELECT type, name FROM ports WHERE id = ?",
                               -1,
                               &st,
                               NULL);
  if (rc != SQLITE_OK)
    {
      return false;
    }
  sqlite3_bind_int (st, 1, port_id);
  bool is_bm = false;


  if (sqlite3_step (st) == SQLITE_ROW)
    {
      // int type = sqlite3_column_int(st, 0); // Unused
      const char *name = (const char *)sqlite3_column_text (st, 1);


      if (name && strstr (name, "Black Market"))
        {
          is_bm = true;
        }
    }
  sqlite3_finalize (st);
  return is_bm;
}


int
cmd_bounty_post_federation (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 1401, "Authentication required.");
      return 0;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      return 0;
    }
  int target_player_id = 0;
  json_t *j_target_id = json_object_get (data, "target_player_id");


  if (j_target_id)
    {
      int type = json_typeof (j_target_id);


      if (type == JSON_STRING)
        {
          LOGD ("cmd_bounty_post_federation: target_player_id is STRING: '%s'",
                json_string_value (j_target_id));
        }
      else if (type == JSON_INTEGER)
        {
          LOGD ("cmd_bounty_post_federation: target_player_id is INTEGER: %lld",
                (long long)json_integer_value (j_target_id));
        }
      else
        {
          LOGD ("cmd_bounty_post_federation: target_player_id is type %d",
                type);
        }
    }
  else
    {
      LOGD ("cmd_bounty_post_federation: target_player_id key missing.");
    }
  if (!json_get_int_flexible (data, "target_player_id",
                              &target_player_id) || target_player_id <= 0)
    {
      LOGE ("cmd_bounty_post_federation: Invalid target_player_id received: %d",
            target_player_id);
      send_enveloped_error (ctx->fd, root, 400, "Invalid target_player_id.");
      return 0;
    }
  long long amount;


  if (!json_get_int64_flexible (data, "amount", &amount) || amount <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "Invalid amount.");
      return 0;
    }
  const char *desc = json_string_value (json_object_get (data, "description"));


  if (!desc)
    {
      desc = "Wanted for crimes against the Federation.";
    }
  int sector = 0;


  db_player_get_sector (ctx->player_id, &sector);
  if (!is_fedspace_sector (sector))
    {
      send_enveloped_error (ctx->fd,
                            root,
                            1403,
                            "Must be in FedSpace to post a Federation bounty.");
      return 0;
    }
  int issuer_alignment = 0;


  db_player_get_alignment (db, ctx->player_id, &issuer_alignment);
  if (issuer_alignment < 0)
    {
      send_enveloped_error (ctx->fd,
                            root,
                            1403,
                            "Evil players cannot post Federation bounties.");
      return 0;
    }
  int target_alignment = 0;


  if (db_player_get_alignment (db, target_player_id,
                               &target_alignment) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 404, "Target player not found.");
      return 0;
    }
  if (target_alignment >= 0)
    {
      send_enveloped_error (ctx->fd,
                            root,
                            1403,
                            "Target must be an evil player.");
      return 0;
    }
  if (target_player_id == ctx->player_id)
    {
      send_enveloped_error (ctx->fd,
                            root,
                            400,
                            "Cannot post bounty on yourself.");
      return 0;
    }
  long long new_balance = 0;


  if (h_deduct_credits (db,
                        "player",
                        ctx->player_id,
                        amount,
                        "WITHDRAWAL",
                        NULL,
                        &new_balance) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 1402, "Insufficient funds.");
      return 0;
    }
  if (db_bounty_create (db,
                        "player",
                        ctx->player_id,
                        "player",
                        target_player_id,
                        amount,
                        desc) != SQLITE_OK)
    {
      h_add_credits (db,
                     "player",
                     ctx->player_id,
                     amount,
                     "DEPOSIT",
                     NULL,
                     &new_balance);
      send_enveloped_error (ctx->fd,
                            root,
                            500,
                            "Database error creating bounty.");
      return 0;
    }
  send_enveloped_ok (ctx->fd, root, "bounty.post_federation.confirmed", NULL);
  return 0;
}


int
cmd_bounty_post_hitlist (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 1401, "Authentication required.");
      return 0;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      return 0;
    }
  int target_player_id = 0;
  json_t *j_target_id = json_object_get (data, "target_player_id");


  if (j_target_id)
    {
      int type = json_typeof (j_target_id);


      if (type == JSON_STRING)
        {
          LOGD ("cmd_bounty_post_hitlist: target_player_id is STRING: '%s'",
                json_string_value (j_target_id));
        }
      else if (type == JSON_INTEGER)
        {
          LOGD ("cmd_bounty_post_hitlist: target_player_id is INTEGER: %lld",
                (long long)json_integer_value (j_target_id));
        }
      else
        {
          LOGD ("cmd_bounty_post_hitlist: target_player_id is type %d", type);
        }
    }
  else
    {
      LOGD ("cmd_bounty_post_hitlist: target_player_id key missing.");
    }
  if (!json_get_int_flexible (data, "target_player_id",
                              &target_player_id) || target_player_id <= 0)
    {
      LOGE ("cmd_bounty_post_hitlist: Invalid target_player_id received: %d",
            target_player_id);
      send_enveloped_error (ctx->fd, root, 400, "Invalid target_player_id.");
      return 0;
    }
  long long amount;


  if (!json_get_int64_flexible (data, "amount", &amount) || amount <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "Invalid amount.");
      return 0;
    }
  const char *desc = json_string_value (json_object_get (data, "description"));


  if (!desc)
    {
      desc = "Hit ordered.";
    }
  int sector = 0;


  db_player_get_sector (ctx->player_id, &sector);
  int port_id = db_get_port_id_by_sector (sector);


  if (port_id <= 0 || !is_black_market_port (db, port_id))
    {
      send_enveloped_error (ctx->fd,
                            root,
                            1403,
                            "Must be at a Black Market port to post a Hit List contract.");
      return 0;
    }
  int issuer_alignment = 0;


  db_player_get_alignment (db,
                           ctx->player_id,
                           &issuer_alignment);
  if (issuer_alignment > 0)
    {
      send_enveloped_error (ctx->fd,
                            root,
                            1403,
                            "Good players cannot post Hit List contracts.");
      return 0;
    }
  int target_alignment = 0;


  if (db_player_get_alignment (db, target_player_id,
                               &target_alignment) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 404, "Target player not found.");
      return 0;
    }
  if (target_alignment <= 0)
    {
      send_enveloped_error (ctx->fd, root, 1403,
                            "Target must be a good player.");
      return 0;
    }
  if (target_player_id == ctx->player_id)
    {
      send_enveloped_error (ctx->fd, root, 400,
                            "Cannot put a hit on yourself.");
      return 0;
    }
  long long new_balance = 0;


  if (h_deduct_credits (db,
                        "player",
                        ctx->player_id,
                        amount,
                        "WITHDRAWAL",
                        NULL,
                        &new_balance) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 1402, "Insufficient funds.");
      return 0;
    }
  if (db_bounty_create (db,
                        "player",
                        ctx->player_id,
                        "player",
                        target_player_id,
                        amount,
                        desc) != SQLITE_OK)
    {
      h_add_credits (db,
                     "player",
                     ctx->player_id,
                     amount,
                     "DEPOSIT",
                     NULL,
                     &new_balance);
      send_enveloped_error (ctx->fd,
                            root,
                            500,
                            "Database error creating bounty.");
      return 0;
    }
  send_enveloped_ok (ctx->fd, root, "bounty.post_hitlist.confirmed", NULL);
  return 0;
}


int
cmd_bounty_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 1401, "Authentication required.");
      return 0;
    }
  sqlite3 *db = db_get_handle ();
  int alignment = 0;


  db_player_get_alignment (db, ctx->player_id, &alignment);
  char *sql = NULL;


  if (alignment >= 0)
    {
      sql = sqlite3_mprintf (
        "SELECT b.id, b.target_id, p.name, b.reward, b.posted_by_type "
        "FROM bounties b "
        "JOIN players p ON b.target_id = p.id "
        "WHERE b.status = 'open' AND p.alignment < 0 "
        "ORDER BY b.reward DESC LIMIT 20;"
        );
    }
  else
    {
      sql = sqlite3_mprintf (
        "SELECT b.id, b.target_id, p.name, b.reward, b.posted_by_type "
        "FROM bounties b "
        "JOIN players p ON b.target_id = p.id "
        "WHERE b.status = 'open' AND (p.alignment > 0 OR b.posted_by_type = 'gov') "
        "ORDER BY b.reward DESC LIMIT 20;"
        );
    }
  sqlite3_stmt *st = NULL;


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      sqlite3_free (sql);
      send_enveloped_error (ctx->fd,
                            root,
                            500,
                            "Database error listing bounties.");
      return 0;
    }
  sqlite3_free (sql);
  json_t *arr = json_array ();


  while (sqlite3_step (st) == SQLITE_ROW)
    {
      json_t *item = json_object ();


      json_object_set_new (item, "bounty_id",
                           json_integer (sqlite3_column_int (st,
                                                             0)));
      json_object_set_new (item, "target_id",
                           json_integer (sqlite3_column_int (st,
                                                             1)));
json_object_set_new (item, "target_name",
                           json_string (sqlite3_column_text (st, 2)));
      json_object_set_new (item, "reward",
                           json_integer (sqlite3_column_int (st, 3)));
      json_object_set_new (item, "posted_by_type",
                           json_string (sqlite3_column_text (st, 4)));
      json_array_append_new (arr, item);
    }
  sqlite3_finalize (st);
  json_t *payload = json_object ();


  json_object_set_new (payload, "bounties", arr);
  send_enveloped_ok (ctx->fd, root, "bounty.list.success", payload);
  json_decref (payload);
  return 0;
}


int
cmd_sys_econ_planet_status (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  if (ctx->player_id <= 0 || auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_enveloped_error (ctx->fd, root, 403, "Forbidden: Must be SysOp");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!data) {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      return 0;
  }

  int planet_id = json_integer_value(json_object_get(data, "planet_id"));
  if (planet_id <= 0) {
      send_enveloped_error (ctx->fd, root, 400, "Invalid or missing planet_id.");
      return 0;
  }

  json_t *response = json_object();
  
  // 1. Planet Basic Info
  sqlite3_stmt *st_planet = NULL;
  const char *sql_planet = "SELECT id, name, type, owner_id, owner_type FROM planets WHERE id = ?;";
  if (sqlite3_prepare_v2(db, sql_planet, -1, &st_planet, NULL) == SQLITE_OK) {
      sqlite3_bind_int(st_planet, 1, planet_id);
      if (sqlite3_step(st_planet) == SQLITE_ROW) {
          json_object_set_new(response, "id", json_integer(sqlite3_column_int(st_planet, 0)));
          json_object_set_new(response, "name", json_string((const char*)sqlite3_column_text(st_planet, 1)));
          json_object_set_new(response, "type", json_integer(sqlite3_column_int(st_planet, 2)));
          json_object_set_new(response, "owner_id", json_integer(sqlite3_column_int(st_planet, 3)));
          json_object_set_new(response, "owner_type", json_string((const char*)sqlite3_column_text(st_planet, 4)));
      } else {
        send_enveloped_error (ctx->fd, root, 404, "Planet not found.");
        sqlite3_finalize(st_planet);
        json_decref(response);
        return 0;
      }
      sqlite3_finalize(st_planet);
  } else {
      send_enveloped_error (ctx->fd, root, 500, "DB error fetching planet info.");
      json_decref(response);
      return 0;
  }

  // 2. Current Stock
  json_t *stock_arr = json_array();
  sqlite3_stmt *st_stock = NULL;
  const char *sql_stock = 
      "SELECT es.commodity_code, c.id, es.quantity "
      "FROM entity_stock es "
      "JOIN commodities c ON es.commodity_code = c.code "
      "WHERE es.entity_type = 'planet' AND es.entity_id = ?;";
  
  if (sqlite3_prepare_v2(db, sql_stock, -1, &st_stock, NULL) == SQLITE_OK) {
      sqlite3_bind_int(st_stock, 1, planet_id);
      while (sqlite3_step(st_stock) == SQLITE_ROW) {
          json_t *item = json_object();
          int cid = sqlite3_column_int(st_stock, 1);
          int qty = sqlite3_column_int(st_stock, 2);
          
          json_object_set_new(item, "commodity_id", json_integer(cid));
          json_object_set_new(item, "quantity", json_integer(qty));
          json_array_append_new(stock_arr, item);
      }
      sqlite3_finalize(st_stock);
  } else {
      LOGE("cmd_sys_econ_planet_status: DB error fetching stock: %s", sqlite3_errmsg(db));
  }
  json_object_set_new(response, "stock", stock_arr);

  // 3. Open Orders
  json_t *orders = db_list_actor_orders(db, "planet", planet_id);
  json_object_set_new(response, "orders", orders);

  // 4. Bank Balance
  long long balance = 0;
  db_get_planet_bank_balance(planet_id, &balance); // Assuming this helper exists
  json_object_set_new(response, "bank_balance", json_integer(balance));

  send_enveloped_ok (ctx->fd, root, "sys.econ.planet_status", response);
  json_decref(response);
  return 0;
}


int
cmd_sys_econ_port_status (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  if (ctx->player_id <= 0 || auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_enveloped_error (ctx->fd, root, 403, "Forbidden: Must be SysOp");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!data) {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object");
      return 0;
  }

  int port_id = json_integer_value(json_object_get(data, "port_id"));
  if (port_id <= 0) {
      send_enveloped_error (ctx->fd, root, 400, "Invalid or missing port_id");
      return 0;
  }

  json_t *response = json_object();
  
  // 1. Port Basic Info
  sqlite3_stmt *st_port = NULL;
  const char *sql_port = "SELECT id, name, size FROM ports WHERE id = ?;";
  if (sqlite3_prepare_v2(db, sql_port, -1, &st_port, NULL) == SQLITE_OK) {
      sqlite3_bind_int(st_port, 1, port_id);
      if (sqlite3_step(st_port) == SQLITE_ROW) {
          json_object_set_new(response, "id", json_integer(sqlite3_column_int(st_port, 0)));
          json_object_set_new(response, "name", json_string((const char*)sqlite3_column_text(st_port, 1)));
          json_object_set_new(response, "size", json_integer(sqlite3_column_int(st_port, 2)));
      }
      sqlite3_finalize(st_port);
  } else {
      send_enveloped_error (ctx->fd, root, 500, "DB error fetching port");
      json_decref(response);
      return 0;
  }

  // 2. Current Stock
  json_t *stock_arr = json_array();
  sqlite3_stmt *st_stock = NULL;
  const char *sql_stock = 
      "SELECT es.commodity_code, c.id, es.quantity "
      "FROM entity_stock es "
      "JOIN commodities c ON es.commodity_code = c.code "
      "WHERE es.entity_type = 'port' AND es.entity_id = ?;";
  
  if (sqlite3_prepare_v2(db, sql_stock, -1, &st_stock, NULL) == SQLITE_OK) {
      sqlite3_bind_int(st_stock, 1, port_id);
      while (sqlite3_step(st_stock) == SQLITE_ROW) {
          json_t *item = json_object();
          // const char *code = (const char*)sqlite3_column_text(st_stock, 0);
          int cid = sqlite3_column_int(st_stock, 1);
          int qty = sqlite3_column_int(st_stock, 2);
          
          json_object_set_new(item, "commodity_id", json_integer(cid));
          json_object_set_new(item, "quantity", json_integer(qty));
          json_array_append_new(stock_arr, item);
      }
      sqlite3_finalize(st_stock);
  }
  json_object_set_new(response, "stock", stock_arr);

  // 3. Open Orders
  json_t *orders = db_list_port_orders(db, port_id);
  json_object_set_new(response, "orders", orders);

  send_enveloped_ok (ctx->fd, root, "sys.econ.port_status", response);
  // json_decref(response); // send_enveloped_ok might assume ownership or not? 
  // Usually send_enveloped_ok copies or takes. Looking at other cmds...
  // Other cmds do json_decref(payload) after send. So I should too.
  json_decref(response);
  return 0;
}

int
cmd_sys_econ_orders_summary (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  if (ctx->player_id <= 0 || auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_enveloped_error (ctx->fd, root, 403, "Forbidden: Must be SysOp");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  int commodity_id = 0;
  if (data) {
      json_t *jcomm = json_object_get(data, "commodity_id");
      if (json_is_integer(jcomm)) {
          commodity_id = json_integer_value(jcomm);
      }
  }

  json_t *summary = db_orders_summary(db, commodity_id);
  send_enveloped_ok (ctx->fd, root, "sys.econ.orders_summary", summary);
  json_decref(summary);
  return 0;
}


int
cmd_sys_npc_ferengi_tick_once (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0 || auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_enveloped_error (ctx->fd, root, 403, "Forbidden: Must be SysOp");
      return 0;
    }

  // Trigger one tick of Ferengi logic
  // Passing 0 as now_ms as it's reserved/unused for rate limiting in the current implementation
  fer_tick(0);

  send_enveloped_ok (ctx->fd, root, "sys.npc.ferengi_tick_once", NULL);
  return 0;
}
