#include <string.h>
#include <jansson.h>
#include <sqlite3.h>
#include <stdbool.h>
/* local includes */
#include "server_auth.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_envelope.h"
#include <stdlib.h>             // For rand() and srand()
#include <time.h>               // For time()
#include "server_players.h"     // For h_send_message_to_player
#include "server_config.h"
#include "database.h"
#include "db_player_settings.h"
#include "server_log.h"
extern struct twconfig *config_load (void);


static bool
player_is_sysop (sqlite3 *db, int player_id)
{
  bool is_sysop = false;
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db,
                          "SELECT COALESCE(type,2), COALESCE(flags,0) FROM players WHERE id=?1",
                          -1,
                          &st,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, player_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          int type = sqlite3_column_int (st, 0);
          int flags = sqlite3_column_int (st, 1);


          if (type == 1 || (flags & 0x1))
            {
              is_sysop = true;  /* adjust rule if needed */
            }
        }
    }
  if (st)
    {
      sqlite3_finalize (st);
    }
  return is_sysop;
}


/* --- login hydration: locked default subscriptions ----------------------- */
static int
subs_upsert_locked_defaults (sqlite3 *db, int player_id, bool is_sysop)
{
  int rc = SQLITE_OK;
  sqlite3_stmt *st = NULL;
  /* 1) 'global' */
  rc = sqlite3_prepare_v2 (db,
                           "INSERT INTO subscriptions(player_id, event_type, delivery, locked, enabled) "
                           "VALUES(?1, 'global', 'push', 1, 1) "
                           "ON CONFLICT(player_id, event_type) DO UPDATE SET locked=1, enabled=1;",
                           -1,
                           &st,
                           NULL);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  st = NULL;
  if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
      rc = SQLITE_ERROR;
      goto done;
    }
  rc = SQLITE_OK;
  /* 2) 'player.<id>' */
  char chan[64];


  snprintf (chan, sizeof (chan), "player.%d", player_id);
  rc = sqlite3_prepare_v2 (db,
                           "INSERT INTO subscriptions(player_id, event_type, delivery, locked, enabled) "
                           "VALUES(?1, ?2, 'push', 1, 1) "
                           "ON CONFLICT(player_id, event_type) DO UPDATE SET locked=1, enabled=1;",
                           -1,
                           &st,
                           NULL);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_bind_text (st, 2, chan, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  st = NULL;
  if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
      rc = SQLITE_ERROR;
      goto done;
    }
  rc = SQLITE_OK;
  /* 3) optional 'sysop' */
  if (is_sysop)
    {
      rc = sqlite3_prepare_v2 (db,
                               "INSERT INTO subscriptions(player_id, event_type, delivery, locked, enabled) "
                               "VALUES(?1, 'sysop', 'push', 1, 1) "
                               "ON CONFLICT(player_id, event_type) DO UPDATE SET locked=1, enabled=1;",
                               -1,
                               &st,
                               NULL);
      if (rc != SQLITE_OK)
        {
          goto done;
        }
      sqlite3_bind_int (st, 1, player_id);
      rc = sqlite3_step (st);
      sqlite3_finalize (st);
      st = NULL;
      if (rc != SQLITE_DONE && rc != SQLITE_ROW)
        {
          rc = SQLITE_ERROR;
          goto done;
        }
      rc = SQLITE_OK;
    }
done:
  if (st)
    {
      sqlite3_finalize (st);
    }
  return rc;
}


/* ---- hydration helpers (#194) ---------------------------------- */
/* Required, locked subscriptions (cannot be unsubscribed) */
static const char *k_required_locked_topics[] = {
  "system.notice",
  /* add more here if you need: "system.motd", ... */
};


/* Default prefs to seed if missing */
typedef struct
{
  const char *key;
  const char *type;             /* 'bool','int','string','json' */
  const char *value;            /* stored as TEXT; server enforces type */
} default_pref_t;
static const default_pref_t k_default_prefs[] = {
  {"ui.ansi", "bool", "true"},
  {"ui.clock_24h", "bool", "true"},
  {"ui.locale", "string", "en-GB"},
  {"ui.page_length", "int", "20"},
  {"privacy.dm_allowed", "bool", "true"},
  /* add more defaults as needed */
};


/* Upsert a locked=1, enabled=1 subscription; preserve lock with MAX() */
static int
upsert_locked_subscription (sqlite3 *db, int player_id, const char *topic)
{
  static const char *SQL =
    "INSERT INTO subscriptions(player_id,event_type,delivery,filter_json,locked,enabled) "
    "VALUES(?, ?, 'internal', NULL, 1, 1) "
    "ON CONFLICT(player_id, event_type) DO UPDATE SET "
    "  enabled=1, " "  locked=MAX(subscriptions.locked, excluded.locked)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_bind_text (st, 2, topic, -1, SQLITE_STATIC);
  int rc = sqlite3_step (st);


  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


/* Insert a default pref if missing (do not overwrite user choice) */
static int
insert_default_pref_if_missing (sqlite3 *db, int player_id,
                                const char *key, const char *type,
                                const char *value)
{
  static const char *SQL =
    "INSERT INTO player_prefs(player_id,key,type,value) "
    "SELECT ?, ?, ?, ? "
    "WHERE NOT EXISTS (SELECT 1 FROM player_prefs WHERE player_id=? AND key=?)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_bind_text (st, 2, key, -1, SQLITE_STATIC);
  sqlite3_bind_text (st, 3, type, -1, SQLITE_STATIC);
  sqlite3_bind_text (st, 4, value, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 5, player_id);
  sqlite3_bind_text (st, 6, key, -1, SQLITE_STATIC);
  int rc = sqlite3_step (st);


  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


/* Run full hydration transactionally; ignore individual insert errors */
static void
hydrate_player_defaults (int player_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db || player_id <= 0)
    {
      return;
    }
  // (void) sqlite3_exec (db, "BEGIN", NULL, NULL, &errmsg);
  /* locked subs */
  for (size_t i = 0;
       i <
       sizeof (k_required_locked_topics) /
       sizeof (k_required_locked_topics[0]); ++i)
    {
      (void) upsert_locked_subscription (db, player_id,
                                         k_required_locked_topics[i]);
    }
  /* default prefs (only if missing) */
  for (size_t i = 0;
       i < sizeof (k_default_prefs) / sizeof (k_default_prefs[0]); ++i)
    {
      (void) insert_default_pref_if_missing (db, player_id,
                                             k_default_prefs[i].key,
                                             k_default_prefs[i].type,
                                             k_default_prefs[i].value);
    }
  // (void) sqlite3_exec (db, "COMMIT", NULL, NULL, &errmsg);
}


//// CMD Handlers ////////
int
cmd_auth_login (client_ctx_t *ctx, json_t *root)
{
  LOGI ("cmd_auth_login called");
  /* NEW: pull fields from the "data" object, not the root */
  json_t *jdata = json_object_get (root, "data");
  const char *name = NULL, *pass = NULL;


  if (json_is_object (jdata))
    {
      json_t *jname = json_object_get (jdata, "username");


      if (!jname)
        {
          /* Legacy support for old tests/clients */
          jname = json_object_get (jdata, "user_name");
          if (!jname)
            {
              jname = json_object_get (jdata, "player_name");
            }
        }                       // Add missing brace here
      json_t *jpass = json_object_get (jdata, "passwd");


      name = json_is_string (jname) ? json_string_value (jname) : NULL;
      pass = json_is_string (jpass) ? json_string_value (jpass) : NULL;
    }
  if (!name || !pass)
    {
      send_response_error (ctx,
                           root,
                           AUTH_ERR_BAD_REQUEST, "Missing required field");
    }
  else
    {
      int player_id = 0;
      int rc = play_login (name, pass, &player_id);


      LOGI ("play_login returned %d for player %s", rc, name);
      if (rc == AUTH_OK)
        {
          /* check is npc */
          sqlite3 *dbh_npc = db_get_handle ();


          if (h_player_is_npc (dbh_npc, player_id))
            {
              send_response_error (ctx,
                                   root,
                                   ERR_IS_NPC, "NPC login is not allowed");
              return 0;
            }
          /* check podded */
          sqlite3 *dbh = db_get_handle ();
          long long current_timestamp = time (NULL);
          char podded_status_str[32] = { 0 };
          long long big_sleep_until = 0;
          // Check player's podded status
          sqlite3_stmt *ps_st = NULL;
          const char *sql_get_podded_status =
            "SELECT status, big_sleep_until FROM podded_status WHERE player_id = ?;";
          int ps_rc =
            sqlite3_prepare_v2 (dbh, sql_get_podded_status, -1, &ps_st, NULL);


          if (ps_rc == SQLITE_OK)
            {
              sqlite3_bind_int (ps_st, 1, player_id);
              if (sqlite3_step (ps_st) == SQLITE_ROW)
                {
                  strncpy (podded_status_str,
                           (const char *) sqlite3_column_text (ps_st, 0),
                           sizeof (podded_status_str) - 1);
                  big_sleep_until = sqlite3_column_int64 (ps_st, 1);
                }
              sqlite3_finalize (ps_st);
            }
          else
            {
              LOGE
              (
                "cmd_auth_login: Failed to query podded_status for player %d: %s",
                player_id,
                sqlite3_errmsg (dbh));
              // Proceed as if not in big sleep to avoid blocking login due to DB error
            }
          if (strcmp (podded_status_str, "big_sleep") == 0)
            {
              if (current_timestamp < big_sleep_until)
                {
                  // Still in Big Sleep
                  json_t *err_data = json_object ();


                  json_object_set_new (err_data, "player_id",
                                       json_integer (player_id));
                  json_object_set_new (err_data,
                                       "big_sleep_until",
                                       json_integer (big_sleep_until));


                  send_response_refused_steal (ctx,
                                               root,
                                               ERR_REF_BIG_SLEEP,
                                               "You are currently in Big Sleep.",
                                               err_data);
                  json_decref (err_data);
                  return 0;     // Disallow login
                }
              else
                {
                  // Big Sleep has ended, respawn player
                  LOGI
                  (
                    "cmd_auth_login: Player %d's Big Sleep ended. Spawning new starter ship.",
                    player_id);
                  // Assume default safe sector is 1
                  int respawn_sector_id = 1;
                  int spawn_rc =
                    spawn_starter_ship (dbh, player_id, respawn_sector_id);


                  if (spawn_rc != SQLITE_OK)
                    {
                      LOGE
                      (
                        "cmd_auth_login: Failed to spawn starter ship for player %d after Big Sleep: %s",
                        player_id,
                        sqlite3_errmsg (dbh));
                      send_response_error (ctx,
                                           root,
                                           ERR_PLANET_NOT_FOUND,
                                           "Database error during respawn.");
                      return 0;
                    }
                  // Emit event player.big_sleep_ended
                  json_t *event_payload = json_object ();


                  json_object_set_new (event_payload, "player_id",
                                       json_integer (player_id));
                  db_log_engine_event (current_timestamp,
                                       "player.big_sleep_ended", "system",
                                       player_id, respawn_sector_id,
                                       event_payload, NULL);
                }
            }
          /* Read canonical sector from DB; fall back to 1 if missing/NULL */
          int sector_id = 0;


          if (db_player_get_sector (player_id, &sector_id) != SQLITE_OK
              || sector_id <= 0)
            {
              sector_id = 1;
            }
          LOGI ("cmd_auth_login: player %d, retrieved sector_id %d",
                player_id, sector_id);
          /* Mark connection as authenticated and sync session state */
          ctx->player_id = player_id;
          ctx->sector_id = sector_id;   /* <-- set unconditionally on login */
          // sqlite3 *dbh = db_get_handle (); // Already obtained above
          bool is_sysop = player_is_sysop (dbh, ctx->player_id);
          int subs_rc =
            subs_upsert_locked_defaults (dbh, ctx->player_id, is_sysop);


          if (subs_rc != SQLITE_OK)
            {
              send_response_error (ctx,
                                   root,
                                   ERR_CITADEL_REQUIRED,
                                   "Database error (subs upsert)");
              return 0;
            }
          /* Reply with session info, including current_sector */
          int unread_news_count = 0;
          sqlite3_stmt *stmt = NULL;
          const char *sql =
            "SELECT COUNT(*) FROM news_feed WHERE timestamp > (SELECT last_news_read_timestamp FROM players WHERE id = ?);";


          if (sqlite3_prepare_v2 (dbh, sql, -1, &stmt, NULL) == SQLITE_OK)
            {
              sqlite3_bind_int (stmt, 1, player_id);
              if (sqlite3_step (stmt) == SQLITE_ROW)
                {
                  unread_news_count = sqlite3_column_int (stmt, 0);
                }
              sqlite3_finalize (stmt);
            }
          char session_token[65];


          if (db_session_create (player_id, 86400, session_token) !=
              SQLITE_OK)
            {
              send_response_error (ctx,
                                   root,
                                   ERR_PLANET_NOT_FOUND,
                                   "Database error (session creation)");
              return 1;
            }
          json_t *data = json_object ();


          json_object_set_new (data, "player_id", json_integer (player_id));
          json_object_set_new (data, "current_sector",
                               json_integer (sector_id));
          json_object_set_new (data, "unread_news_count",
                               json_integer (unread_news_count));
          json_object_set_new (data, "session", json_string (session_token));


          if (!data)
            {
              send_response_error (ctx,
                                   root,
                                   ERR_PLANET_NOT_FOUND, "Out of memory");
              return 1;
            }
          /* Auto-subscribe this player to system.* on login (best-effort). */
          {
            sqlite3_stmt *st = NULL;


            if (sqlite3_prepare_v2 (dbh,
                                    // Use dbh which is already defined
                                    "INSERT OR IGNORE INTO subscriptions(player_id,event_type,delivery,enabled) "
                                    "VALUES(?1,'system.*','push',1);",
                                    -1,
                                    &st,
                                    NULL) == SQLITE_OK)
              {
                sqlite3_bind_int64 (st, 1, ctx->player_id);
                (void) sqlite3_step (st);       /* ignore result; UNIQUE prevents dupes */
              }
            if (st)
              {
                sqlite3_finalize (st);
              }
          }
          {
            char *cur = NULL;   // must be char*


            if (db_prefs_get_one (ctx->player_id, "ui.locale", &cur) !=
                SQLITE_OK || !cur)
              {
                (void) db_prefs_set_one (ctx->player_id, "ui.locale",
                                         PT_STRING, "en-GB");
              }
            if (cur)
              {
                free (cur);
              }
          }
          send_response_ok_take (ctx, root, "auth.session", &data);
        }
      else if (rc == AUTH_ERR_INVALID_CRED)
        {
          send_response_error (ctx,
                               root,
                               AUTH_ERR_INVALID_CRED, "Invalid credentials");
        }
      else
        {
          send_response_error (ctx, root, AUTH_ERR_DB, "Database error");
        }
    }
  hydrate_player_defaults (ctx->player_id);
  return 0;
}


/* ---------- auth.register ---------- */
int
cmd_auth_register (client_ctx_t *ctx, json_t *root)
{
  json_t *jdata = json_object_get (root, "data");
  const char *name = NULL, *pass = NULL, *ship_name = NULL;
  const char *ui_locale = NULL, *ui_timezone = NULL;
  int player_id = 0;
  int rc = 0;
  struct twconfig *cfg = NULL;
  int spawn_sector_id = 0;
  int new_ship_id = -1;
  sqlite3 *db = db_get_handle ();       // Get handle and rely on recursive mutex
  if (json_is_object (jdata))
    {
      json_t *jn = json_object_get (jdata, "username");
      json_t *jp = json_object_get (jdata, "passwd");
      json_t *jsn = json_object_get (jdata, "ship_name");
      json_t *jloc = json_object_get (jdata, "ui_locale");
      json_t *jtz = json_object_get (jdata, "ui_timezone");


      name = json_is_string (jn) ? json_string_value (jn) : NULL;
      pass = json_is_string (jp) ? json_string_value (jp) : NULL;
      ship_name = json_is_string (jsn) ? json_string_value (jsn) : NULL;
      ui_locale = json_is_string (jloc) ? json_string_value (jloc) : NULL;
      ui_timezone = json_is_string (jtz) ? json_string_value (jtz) : NULL;
    }
  if (!name || !pass)
    {
      send_response_error (ctx,
                           root, ERR_MISSING_FIELD, "Missing required field");
      return 0;
    }
  // --- Start Transaction for player creation and initial setup ---
  rc = user_create (db, name, pass, &player_id);
  if (rc == AUTH_OK)
    {
      // --- Configuration Loading ---
      cfg = config_load ();
      if (!cfg)
        {
          LOGE ("cmd_auth_register debug: config_load failed for player %d",
                player_id);
          send_response_error (ctx,
                               root,
                               ERR_PLANET_NOT_FOUND,
                               "Database error (config_load)");
          goto rollback_and_error;
        }
      // --- Create Bank Account for the new player ---
      int new_account_id = -1;
      int create_bank_rc = db_bank_create_account ("player",
                                                   player_id,
                                                   cfg->startingcredits,
                                                   &new_account_id);


      if (create_bank_rc != SQLITE_OK)
        {
          LOGE (
            "cmd_auth_register debug: Failed to create bank account for player %d: %s",
            player_id,
            sqlite3_errmsg (db));                                                                                               // db is still available for logging error messages
          send_response_error (ctx,
                               root,
                               ERR_PLANET_NOT_FOUND,
                               "Database error (bank account creation)");
          goto rollback_and_error;      // Needs proper rollback of player creation if this fails.
        }
      // --- Create Turns Entry for the new player ---
      // Use 750 as a default for starting turns.
      sqlite3_stmt *st_turns = NULL;
      int prepare_turns_rc = sqlite3_prepare_v2 (db,
                                                 "INSERT OR IGNORE INTO turns (player, turns_remaining, last_update) VALUES (?1, ?2, strftime('%s', 'now'));",
                                                 -1,
                                                 &st_turns,
                                                 NULL);


      if (prepare_turns_rc == SQLITE_OK)
        {
          sqlite3_bind_int (st_turns, 1, player_id);
          sqlite3_bind_int (st_turns, 2, 750);  // Set initial turns to 750
          int step_turns_rc = sqlite3_step (st_turns);


          if (step_turns_rc != SQLITE_DONE)
            {
              LOGE
              (
                "cmd_auth_register debug: Failed to create turns entry for player %d: %s",
                player_id,
                sqlite3_errmsg (db));
              sqlite3_finalize (st_turns);
              send_response_error (ctx,
                                   root,
                                   ERR_PLANET_NOT_FOUND,
                                   "Database error (turns entry creation)");
              goto rollback_and_error;
            }
          sqlite3_finalize (st_turns);
        }
      else
        {
          LOGE
          (
            "cmd_auth_register debug: Failed to prepare turns insert for player %d: %s",
            player_id,
            sqlite3_errmsg (db));
          send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                               "Database error (prepare turns insert)");
          goto rollback_and_error;
        }
      // --- Spawn Location ---
      spawn_sector_id = (rand () % 10) + 1;     // Random sector between 1 and 10
      // --- Ship Creation & Naming ---
      if (!ship_name || !*ship_name)
        {
          ship_name = "Used Scout Marauder";    // Default ship name
        }
      new_ship_id =
        db_create_initial_ship (player_id, ship_name, spawn_sector_id);
      if (new_ship_id == -1)
        {
          LOGE
          (
            "cmd_auth_register debug: db_create_initial_ship failed for player %d: %s",
            player_id,
            sqlite3_errmsg (db));
          send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                               "Database error (ship creation)");
          goto rollback_and_error;
        }
      // --- Update Player's Sector and Ship ---
      int set_sector_rc = db_player_set_sector (player_id, spawn_sector_id);


      if (set_sector_rc != SQLITE_OK)
        {
          LOGE
          (
            "cmd_auth_register debug: db_player_set_sector failed for player %d: %s",
            player_id,
            sqlite3_errmsg (db));
          send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                               "Database error (set player sector)");
          goto rollback_and_error;
        }
      // db_ship_claim already updates players.ship, but ensure ctx is updated
      ctx->sector_id = spawn_sector_id;
      // --- Starting Credits (Petty Cash) ---
      // Update players.credits directly so they have cash on hand.
      int start_creds = (cfg &&
                         cfg->startingcredits >
                         0) ? cfg->startingcredits : 1000;
      sqlite3_stmt *st_creds = NULL;
      int prepare_creds_rc = sqlite3_prepare_v2 (db,
                                                 // Use passed db
                                                 "UPDATE players SET credits = ?1 WHERE id = ?2;",
                                                 -1,
                                                 &st_creds,
                                                 NULL);


      if (prepare_creds_rc == SQLITE_OK)
        {
          sqlite3_bind_int (st_creds, 1, start_creds);
          sqlite3_bind_int (st_creds, 2, player_id);
          int step_creds_rc = sqlite3_step (st_creds);


          if (step_creds_rc != SQLITE_DONE)
            {
              LOGE
              (
                "cmd_auth_register debug: Failed to set starting credits for player %d: %s",
                player_id,
                sqlite3_errmsg (db));
              sqlite3_finalize (st_creds);
              send_response_error (ctx,
                                   root,
                                   ERR_PLANET_NOT_FOUND,
                                   "Database error (set credits)");
              goto rollback_and_error;
            }
          sqlite3_finalize (st_creds);
        }
      else
        {
          LOGE
          (
            "cmd_auth_register debug: Failed to prepare credits update for player %d: %s",
            player_id,
            sqlite3_errmsg (db));
          send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                               "Database error (prepare credits)");
          goto rollback_and_error;
        }
      // --- Starting Alignment ---
      int set_align_rc = db_player_set_alignment (player_id, 1);


      if (set_align_rc != SQLITE_OK)
        {
          LOGE
          (
            "cmd_auth_register debug: db_player_set_alignment failed for player %d: %s",
            player_id,
            sqlite3_errmsg (db));
          send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                               "Database error (set alignment)");
          goto rollback_and_error;
        }
      // --- Automatic Subscriptions ---
      // Already subscribed to system.*
      // Add news.* subscription
      sqlite3_stmt *st = NULL;
      int prepare_subs_rc = sqlite3_prepare_v2 (db,
                                                "INSERT OR IGNORE INTO subscriptions(player_id,event_type,delivery,enabled) "
                                                "VALUES(?1,'news.*','push',1);",
                                                -1,
                                                &st,
                                                NULL);


      if (prepare_subs_rc == SQLITE_OK)
        {
          sqlite3_bind_int64 (st, 1, player_id);
          (void) sqlite3_step (st);
        }
      if (st)
        {
          sqlite3_finalize (st);
        }
      // --- Player Preferences (Override Defaults) ---
      if (ui_locale)
        {
          (void) db_prefs_set_one (player_id,
                                   "ui.locale", PT_STRING, ui_locale);
        }
      if (ui_timezone)
        {
          (void) db_prefs_set_one (player_id,
                                   "ui.timezone", PT_STRING, ui_timezone);
        }
      // --- Welcome Message ---
      h_send_message_to_player (player_id, 0, "Welcome to TWClone!",
                                get_welcome_message (player_id));
      // --- Issue Session Token ---
      char tok[65];
      int session_create_rc = db_session_create (player_id, 86400, tok);


      if (session_create_rc != SQLITE_OK)
        {
          LOGE
          (
            "cmd_auth_register debug: db_session_create failed for player %d: %s",
            player_id,
            sqlite3_errmsg (db));
          send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                               "Database error (session token)");
          goto rollback_and_error;
        }
      else
        {
          ctx->player_id = player_id;
          json_t *data = json_object ();


          json_object_set_new (data, "player_id", json_integer (player_id));
          json_object_set_new (data, "session_token", json_string (tok));


          send_response_ok_take (ctx, root, "auth.session", &data);
        }
    }
  else if (rc == AUTH_ERR_NAME_TAKEN)
    {
      LOGW ("cmd_auth_register debug: Username '%s' already taken", name);
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NAME_TAKEN,
                                   "Username already exists",
                                   NULL);                                                       // Change to send_response_refused_steal
      if (cfg)
        {
          free (cfg);           // Free config if allocated
        }
      return 0;                 // Exit successfully after sending refused response
    }
  else
    {
      LOGE
        ("cmd_auth_register debug: user_create failed for '%s' with rc=%d: %s",
        name, rc, sqlite3_errmsg (db));
      send_response_error (ctx, root, AUTH_ERR_DB, "Database error");
      goto rollback_and_error;
    }
  if (cfg)
    {
      free (cfg);
    }
  return 0;
rollback_and_error:
  LOGW
  (
    "cmd_auth_register debug: Rolling back transaction for player registration of '%s'",
    name);
  if (cfg)
    {
      free (cfg);
    }
  return 0;
}


const char *
get_welcome_message (int player_id)
{
  (void) player_id;             // Suppress unused parameter warning
  return "Hello Player";
}


/* ---------- auth.logout ---------- */
int
cmd_auth_logout (client_ctx_t *ctx, json_t *root)
{
  json_t *jdata = json_object_get (root, "data");
  const char *tok = NULL;
  if (json_is_object (jdata))
    {
      json_t *jt = json_object_get (jdata, "session_token");


      if (json_is_string (jt))
        {
          tok = json_string_value (jt);
        }
    }
  /* If no token provided, log out the connection; if token given, revoke it. */
  if (tok)
    {
      (void) db_session_revoke (tok);
    }
  ctx->player_id = 0;           /* drop connection auth state */
  json_t *data = json_object ();


  json_object_set_new (data, "message", json_string ("Logged out"));


  send_response_ok_take (ctx, root, "auth.logged_out", &data);
  return 0;
}


int
cmd_user_create (client_ctx_t *ctx, json_t *root)
{
  json_t *jdata = json_object_get (root, "data");
  const char *name = NULL, *pass = NULL;
  if (json_is_object (jdata))
    {
      json_t *jname = json_object_get (jdata, "username");
      json_t *jpass = json_object_get (jdata, "passwd");


      name = json_is_string (jname) ? json_string_value (jname) : NULL;
      pass = json_is_string (jpass) ? json_string_value (jpass) : NULL;
    }
  if (!name || !pass)
    {
      send_response_error (ctx,
                           root,
                           AUTH_ERR_BAD_REQUEST, "Missing required field");
    }
  else
    {
      int player_id = 0;
      int rc = user_create (db_get_handle (), name, pass, &player_id);


      if (rc == AUTH_OK)
        {
          json_t *data = json_object ();


          json_object_set_new (data, "player_id", json_integer (player_id));


          send_response_ok_take (ctx, root, "user.created", &data);
        }
      else if (rc == AUTH_ERR_NAME_TAKEN)
        {
          send_response_error (ctx,
                               root,
                               AUTH_ERR_NAME_TAKEN,
                               "Username already exists");
        }
      else
        {
          send_response_error (ctx, root, AUTH_ERR_DB, "Database error");
        }
    }
  return 0;
}


int
cmd_auth_refresh (client_ctx_t *ctx, json_t *root)
{
  int pid = 0;                  /* Declare pid at function scope */
  /* Try data.session_token */
  json_t *jdata = json_object_get (root, "data");
  const char *tok = NULL;
  if (json_is_object (jdata))
    {
      json_t *jt = json_object_get (jdata, "session_token");


      if (json_is_string (jt))
        {
          tok = json_string_value (jt);
        }
    }
  /* Try meta.session_token */
  if (!tok)
    {
      json_t *jmeta = json_object_get (root, "meta");


      if (json_is_object (jmeta))
        {
          json_t *jt = json_object_get (jmeta, "session_token");


          if (json_is_string (jt))
            {
              tok = json_string_value (jt);
            }
        }
    }
  /* If still no token, fall back to the connection’s logged-in player */
  if (!tok && ctx->player_id > 0)
    {
      sqlite3 *dbh = db_get_handle ();
      bool is_sysop = player_is_sysop (dbh, ctx->player_id);
      int rc = subs_upsert_locked_defaults (dbh,
                                            ctx->player_id,
                                            is_sysop);


      if (rc != SQLITE_OK)
        {
          send_response_error (ctx,
                               root,
                               ERR_CITADEL_REQUIRED,
                               "Database error (subs upsert)");
          return 0;
        }
      char newtok[65];


      if (db_session_create (ctx->player_id, 86400, newtok) != SQLITE_OK)
        {
          send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                               "Database error");
        }
      else
        {
          json_t *data = json_object ();


          json_object_set_new (data, "player_id",
                               json_integer (ctx->player_id));
          json_object_set_new (data, "session_token", json_string (newtok));


          send_response_ok_take (ctx, root, "auth.session", &data);
        }
    }
  else if (tok)
    {
      sqlite3 *dbh = db_get_handle ();
      bool is_sysop = player_is_sysop (dbh, pid);


      if (subs_upsert_locked_defaults (dbh, pid, is_sysop) != SQLITE_OK)
        {
          send_response_error (ctx,
                               root,
                               ERR_CITADEL_REQUIRED,
                               "Database error (subs upsert)");
          return 0;
        }
      /* Rotate provided token */
      char newtok[65];
      int rc = db_session_refresh (tok, 86400, newtok, &pid);


      if (rc == SQLITE_OK)
        {
          ctx->player_id = pid;
          if (ctx->sector_id <= 0)
            {
              ctx->sector_id = 1;
            }
          json_t *data = json_object ();


          json_object_set_new (data, "player_id", json_integer (pid));
          json_object_set_new (data, "session_token", json_string (newtok));


          send_response_ok_take (ctx, root, "auth.session", &data);
        }
      else
        {
          send_response_error (ctx,
                               root,
                               ERR_SECTOR_NOT_FOUND,
                               "Invalid or expired session");
        }
    }
  else
    {
      /* Not logged in and no token supplied */
      send_response_error (ctx,
                           root, ERR_MISSING_FIELD, "Missing required field");
    }
  return 0;
}


int
cmd_auth_mfa_totp_verify (client_ctx_t *ctx, json_t *root)
{
  // You can parse the code now if you like, but we’ll NIY this for the moment.
  // json_t *data = json_object_get(root, "data");
  // const char *code = data && json_is_string(json_object_get(data, "code"))
  //     ? json_string_value(json_object_get(data, "code")) : NULL;
  send_response_refused_steal (ctx,
                               root,
                               ERR_CAPABILITY_DISABLED,
                               "MFA is not enabled on this server", NULL);
  return 0;
}

