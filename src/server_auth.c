#include "db/repo/repo_auth.h"
/* src/server_auth.c */
#include <string.h>
#include <jansson.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

/* local includes */
#include "server_auth.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_envelope.h"
#include "server_players.h"
#include "server_config.h"
#include "db/repo/repo_database.h"
#include "game_db.h"
#include "repo_player_settings.h"
#include "server_log.h"
#include "db/db_api.h"
#include "db/sql_driver.h"


static bool
player_is_sysop (db_t *db, int player_id)
{
  if (!db)
    {
      return false;
    }
  int type = 0, flags = 0;
  if (repo_auth_get_player_type_flags(db, player_id, &type, &flags) == 0)
    {
      if (type == 1 || (flags & 0x1))
        {
          return true;
        }
    }
  return false;
}


static int
subs_upsert_locked_defaults (db_t *db, int player_id, bool is_sysop)
{
  if (repo_auth_upsert_global_sub(db, player_id) != 0)
    {
      return ERR_DB;
    }

  char chan[64];
  snprintf (chan, sizeof (chan), "player.%d", player_id);
  
  if (repo_auth_upsert_player_sub(db, player_id, chan) != 0)
    {
      return ERR_DB;
    }

  if (is_sysop)
    {
      if (repo_auth_upsert_sysop_sub(db, player_id) != 0)
        {
          return ERR_DB;
        }
    }
  return 0;
}


static const char *k_required_locked_topics[] = {
  "system.notice",
};


typedef struct
{
  const char *key;
  const char *type;
  const char *value;
} default_pref_t;

static const default_pref_t k_default_prefs[] = {
  {"ui.ansi", "bool", "true"},
  {"ui.clock_24h", "bool", "true"},
  {"ui.locale", "string", "en-GB"},
  {"ui.page_length", "int", "20"},
  {"privacy.dm_allowed", "bool", "true"},
};


static int
upsert_locked_subscription (db_t *db, int player_id, const char *topic)
{
  return repo_auth_upsert_locked_sub(db, player_id, topic);
}


static int
insert_default_pref_if_missing (db_t *db, int player_id,
                                const char *key, const char *type,
                                const char *value)
{
  return repo_auth_insert_pref_if_missing(db, player_id, key, type, value);
}


static void
hydrate_player_defaults (int player_id)
{
  db_t *db = game_db_get_handle ();
  if (!db || player_id <= 0)
    {
      return;
    }
  for (size_t i = 0;
       i <
       sizeof (k_required_locked_topics) / sizeof (k_required_locked_topics[0]);
       ++i)
    {
      upsert_locked_subscription (db, player_id, k_required_locked_topics[i]);
    }
  for (size_t i = 0;
       i < sizeof (k_default_prefs) / sizeof (k_default_prefs[0]);
       ++i)
    {
      insert_default_pref_if_missing (db,
                                      player_id,
                                      k_default_prefs[i].key,
                                      k_default_prefs[i].type,
                                      k_default_prefs[i].value);
    }
}


int
cmd_auth_login (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      LOGD ("[auth.login] DB handle is NULL. Returning -1.");
      return -1;
    }

  json_t *jdata = json_object_get (root,
                                   "data");
  const char *name = NULL, *pass = NULL;


  LOGD ("[auth.login] Received login attempt.");

  if (json_is_object (jdata))
    {
      json_t *jname = json_object_get (jdata, "username");


      if (!jname)
        {
          jname = json_object_get (jdata, "user_name");
          if (!jname)
            {
              jname = json_object_get (jdata, "player_name");
            }
        }
      json_t *jpass = json_object_get (jdata, "passwd");


      name = json_string_value (jname);
      pass = json_string_value (jpass);
    }

  if (!name || !pass)
    {
      LOGD ("[auth.login] Missing username or password in request.");
      send_response_error (ctx, root, AUTH_ERR_BAD_REQUEST, "Missing fields");
      return 0;
    }

  LOGD ("[auth.login] Attempting login for user: %s", name); // Do not log password
  int pid = 0;


  int auth_rc = play_login (name, pass, &pid);


  LOGD ("[auth.login] play_login returned: %d, pid: %d", auth_rc, pid);

  if (auth_rc == AUTH_OK)
    {
      if (h_player_is_npc (db, pid))
        {
          send_response_error (ctx, root, ERR_IS_NPC, "NPC login denied");
          return 0;
        }

      long long current_ts = time (NULL);
      char podded_status[32] = { 0 };
      long long big_sleep_until = 0;
      
      if (repo_auth_get_podded_status(db, pid, podded_status, sizeof(podded_status), &big_sleep_until) != 0)
        {
          podded_status[0] = '\0';
          big_sleep_until = 0;
        }

      if (strcmp (podded_status, "big_sleep") == 0)
        {
          if (current_ts < big_sleep_until)
            {
              json_t *err_data = json_object ();


              json_object_set_new (err_data, "player_id", json_integer (pid));
              json_object_set_new (err_data, "big_sleep_until",
                                   json_integer (big_sleep_until));
              send_response_refused_steal (ctx,
                                           root,
                                           ERR_REF_BIG_SLEEP,
                                           "You are currently in Big Sleep.",
                                           err_data);
              return 0;
            }
          else
            {
              int respawn_sid = 1;


              if (spawn_starter_ship (db, pid, respawn_sid) == 0)
                {
                  json_t *ev_payload = json_object ();


                  json_object_set_new (ev_payload, "player_id",
                                       json_integer (pid));
                  db_log_engine_event (current_ts,
                                       "player.big_sleep_ended",
                                       "system",
                                       pid,
                                       respawn_sid,
                                       ev_payload,
                                       db);
                  json_decref (ev_payload);
                }
            }
        }

      int sid = 1;


      db_player_get_sector (db, pid, &sid);
      if (sid <= 0)
        {
          sid = 1;
        }

      ctx->player_id = pid;
      ctx->sector_id = sid;

      bool is_sysop = player_is_sysop (db, pid);


      subs_upsert_locked_defaults (db, pid, is_sysop);
      hydrate_player_defaults (pid);

      int unread_news = 0;
      if (repo_auth_get_unread_news_count(db, pid, &unread_news) != 0)
        {
          unread_news = 0;
        }

      char tok[65];


      h_generate_hex_uuid (tok, sizeof (tok));

      db_session_create (pid, tok, (long long)time (NULL) + 86400);

      json_t *resp = json_object ();


      json_object_set_new (resp, "player_id", json_integer (pid));
      json_object_set_new (resp, "current_sector", json_integer (sid));
      json_object_set_new (resp, "unread_news_count",
                           json_integer (unread_news));
      json_object_set_new (resp, "session_token", json_string (tok));

      repo_auth_upsert_system_sub(db, pid);

      char *cur_locale = NULL;


      if (db_prefs_get_one (db, pid, "ui.locale",
                            &cur_locale) != 0 || !cur_locale)
        {
          db_prefs_set_one (db, pid, "ui.locale", PT_STRING, "en-GB");
        }
      if (cur_locale)
        {
          free (cur_locale);
        }

      send_response_ok_take (ctx, root, "auth.session", &resp);
    }
  else
    {
      send_response_error (ctx, root, AUTH_ERR_INVALID_CRED, "Invalid login");
    }
  return 0;
}


int
cmd_auth_register (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }

  json_t *jdata = json_object_get (root, "data");
  const char *name = NULL, *pass = NULL, *ship_name = NULL;
  const char *ui_locale = NULL, *ui_timezone = NULL;


  if (json_is_object (jdata))
    {
      name = json_string_value (json_object_get (jdata, "username"));
      pass = json_string_value (json_object_get (jdata, "passwd"));
      ship_name = json_string_value (json_object_get (jdata, "ship_name"));
      ui_locale = json_string_value (json_object_get (jdata, "ui_locale"));
      ui_timezone = json_string_value (json_object_get (jdata, "ui_timezone"));
    }

  if (!name || !pass)
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD,
                           "Missing required field");
      return 0;
    }

  int spawn_sid = (rand () % 10) + 1;


  if (!ship_name || !*ship_name)
    {
      ship_name = "Used Scout Marauder";
    }

  db_error_t err;
  int64_t player_id = 0;


  /* Call register_player with sector specified */
  if (repo_auth_register_player(db, name, pass, ship_name, spawn_sid, &player_id) != 0)
    {
      if (db_error_is_constraint_violation(&err)) // Wait, err is not filled by repo if it returns non-zero code. 
                                                  // I should check code or category.
        {
          send_response_error (ctx,
                               root,
                               ERR_NAME_TAKEN,
                               "Username already exists");
        }
      else
        {
          send_response_error (ctx,
                               root,
                               AUTH_ERR_DB,
                               "Failed to create account");
        }
      return 0;
    }

  int pid = (int)player_id;

  struct twconfig *cfg = config_load ();


  if (!cfg)
    {
      send_response_error (ctx,
                           root,
                           ERR_PLANET_NOT_FOUND,
                           "Database error (config)");
      return 0;
    }

  int new_acc_id = -1;


  db_bank_create_account (db,
                          "player",
                          pid,
                          cfg->startingcredits,
                          &new_acc_id);

  const char *now_ts = sql_now_timestamptz(db);
  if (!now_ts)
    {
      send_response_error (ctx, root, ERR_DB, "Unsupported database backend.");
      return 0;
    }
  
  repo_auth_insert_initial_turns(db, now_ts, pid);

  ctx->sector_id = spawn_sid;

  int start_creds = cfg->startingcredits > 0 ? cfg->startingcredits : 1000;
  repo_auth_update_player_credits(db, start_creds, pid);

  db_player_set_alignment (db, pid, 1);

  repo_auth_upsert_news_sub(db, pid);

  if (ui_locale)
    {
      db_prefs_set_one (db, pid, "ui.locale", PT_STRING, ui_locale);
    }
  if (ui_timezone)
    {
      db_prefs_set_one (db, pid, "ui.timezone", PT_STRING, ui_timezone);
    }

  h_send_message_to_player (db, pid,
                            0,
                            "Welcome to TWClone!",
                            get_welcome_message (pid));

  char tok[65];


  h_generate_hex_uuid (tok, sizeof (tok));
  if (db_session_create (pid, tok, (long long)time (NULL) + 86400) != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_PLANET_NOT_FOUND,
                           "Database error (session)");
      if (cfg)
        {
          free (cfg);
        }
      return 0;
    }

  ctx->player_id = pid;
  json_t *resp_data = json_object ();


  json_object_set_new (resp_data, "player_id", json_integer (pid));
  json_object_set_new (resp_data, "session_token", json_string (tok));
  send_response_ok_take (ctx, root, "auth.session", &resp_data);

  if (cfg)
    {
      free (cfg);
    }
  return 0;
}


int
cmd_auth_logout (client_ctx_t *ctx, json_t *root)
{
  json_t *jdata = json_object_get (root, "data");
  json_t *jauth = json_object_get (root, "auth");
  const char *tok = NULL;

  if (json_is_object (jdata))
    {
      json_t *jt = json_object_get (jdata, "session_token");


      if (json_is_string (jt))
        {
          tok = json_string_value (jt);
        }
    }
  
  if (!tok && json_is_object (jauth))
    {
      json_t *jt = json_object_get (jauth, "session");
      if (json_is_string (jt))
        {
          tok = json_string_value (jt);
        }
    }

  if (tok)
    {
      db_session_revoke (tok);
    }
  ctx->player_id = 0;
  json_t *data = json_object ();


  json_object_set_new (data, "message", json_string ("Logged out"));
  send_response_ok_take (ctx, root, "auth.logged_out", &data);
  return 0;
}


int
cmd_user_create (client_ctx_t *ctx, json_t *root)
{
  return cmd_auth_register (ctx, root);
}


int
cmd_auth_refresh (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }

  int pid = 0;
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

  if (!tok && ctx->player_id > 0)
    {
      bool is_sysop = player_is_sysop (db, ctx->player_id);


      subs_upsert_locked_defaults (db, ctx->player_id, is_sysop);

      char newtok[65];


      h_generate_hex_uuid (newtok,
                           sizeof (newtok));
      if (db_session_create (ctx->player_id,
                             newtok,
                             (long long)time (NULL) + 86400) != 0)
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
      char newtok[65];


      h_generate_hex_uuid (newtok, sizeof (newtok));
      if (db_session_refresh (tok, 86400, newtok, &pid) == 0)
        {
          bool is_sysop = player_is_sysop (db, pid);


          subs_upsert_locked_defaults (db, pid, is_sysop);

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
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD,
                           "Missing required field");
    }
  return 0;
}


int
cmd_auth_session (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, AUTH_ERR_INVALID_CRED, "Not logged in");
      return 0;
    }
  json_t *resp = json_object ();


  json_object_set_new (resp, "player_id", json_integer (ctx->player_id));
  json_object_set_new (resp, "current_sector", json_integer (ctx->sector_id));
  send_response_ok_take (ctx, root, "auth.session", &resp);
  return 0;
}


int
cmd_auth_check_username (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  json_t *jdata = json_object_get (root, "data");
  const char *user = json_string_value (json_object_get (jdata, "username"));


  if (!user)
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD, "Missing username");
      return 0;
    }
  int exists = 0;
  repo_auth_check_username_exists(db, user, &exists);

  json_t *resp = json_object ();


  json_object_set_new (resp, "available", json_boolean (!exists));
  send_response_ok_take (ctx, root, "auth.check_username", &resp);
  return 0;
}


int
cmd_auth_reset_password (client_ctx_t *ctx, json_t *root)
{
  send_response_refused_steal (ctx,
                               root,
                               ERR_CAPABILITY_DISABLED,
                               "Password reset is disabled on this server",
                               NULL);
  return 0;
}


int
cmd_auth_mfa_totp_setup (client_ctx_t *ctx, json_t *root)
{
  send_response_refused_steal (ctx,
                               root,
                               ERR_CAPABILITY_DISABLED,
                               "MFA setup is disabled on this server",
                               NULL);
  return 0;
}


int
cmd_auth_mfa_totp_verify (client_ctx_t *ctx, json_t *root)
{
  send_response_refused_steal (ctx,
                               root,
                               ERR_CAPABILITY_DISABLED,
                               "MFA is not enabled on this server",
                               NULL);
  return 0;
}


const char *
get_welcome_message (int pid)
{
  (void)pid; return "Hello Player";
}


int
auth_player_get_type (int player_id)
{
  db_t *db = game_db_get_handle ();
  if (player_is_sysop (db, player_id))
    {
      return PLAYER_TYPE_SYSOP;
    }
  return PLAYER_TYPE_PLAYER;
}

