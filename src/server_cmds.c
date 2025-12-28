/* src/server_cmds.c */
#include <string.h>
#include <strings.h>
#include <jansson.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h> // For bool type

/* local includes */
#include "database.h"
#include "game_db.h"
#include "server_cmds.h"
#include "server_auth.h"
#include "errors.h" // Include errors.h for ERR_IS_NPC etc.
#include "server_players.h" // Include for PLAYER_TYPE_SYSOP
#include "server_envelope.h"
#include "db_player_settings.h"
#include "server_cron.h"
#include "server_log.h" // Include server_log.h for LOGD
#include "server_clusters.h"
#include "database_market.h"
#include "server_ports.h"
#include "server_universe.h"
#include "database_cmd.h"
#include "db/db_api.h"

/* --- Bounty Helpers --- */


// Helper: Check if sector is FedSpace
static bool
is_fedspace_sector (int sector_id)
{
  return (sector_id >= 1 && sector_id <= 10);
}


// Helper: Check if port is Black Market
static bool
is_black_market_port (db_t *db, int port_id)
{
  db_res_t *res = NULL;
  db_error_t err;
  const char *sql = "SELECT name FROM ports WHERE id = $1;";
  bool is_bm = false;


  if (db_query (db, sql, (db_bind_t[]){ db_bind_i32 (port_id) }, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          const char *name = db_res_col_text (res, 0, &err);


          if (name && strstr (name, "Black Market"))
            {
              is_bm = true;
            }
        }
      db_res_finalize (res);
    }
  return is_bm;
}


int
cmd_sys_cluster_init (client_ctx_t *ctx, json_t *root)
{
#ifdef BUILD_PRODUCTION
  send_response_error (ctx,
                       root,
                       REF_TURN_COST_EXCEEDS,
                       "Command disabled in production");
  return 0;
#else
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return AUTH_ERR_DB;
    }

  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "Auth required");
      return 0;
    }
  // Ensure player is an admin/sysop.
  if (auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Permission denied", NULL);
      return 0;
    }
  int rc = clusters_init (db);


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR,
                           "clusters_init failed");
      return 0;
    }
  rc = clusters_seed_illegal_goods (db);
  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "seed failed");
      return 0;
    }
  send_response_ok_take (ctx, root, "sys.cluster.init.ok", NULL);
  return 0;
#endif
}


int
cmd_sys_cluster_seed_illegal_goods (client_ctx_t *ctx, json_t *root)
{
#ifdef BUILD_PRODUCTION
  send_response_error (ctx,
                       root,
                       REF_TURN_COST_EXCEEDS,
                       "Command disabled in production");
  return 0;
#else
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return AUTH_ERR_DB;
    }

  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "Auth required");
      return 0;
    }
  // Ensure player is an admin/sysop.
  if (auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Permission denied", NULL);
      return 0;
    }
  int rc = clusters_seed_illegal_goods (db);


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "seed failed");
      return 0;
    }
  send_response_ok_take (ctx, root, "sys.cluster.seed_illegal_goods.ok",
                         NULL);
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
play_login (const char *user, const char *pass, int *pid)
{
  LOGD("[play_login] Attempting login for user: %s", user);
  if (!user || !pass)
    {
      LOGD("[play_login] Missing user or pass. Returning AUTH_ERR_BAD_REQUEST.");
      return AUTH_ERR_BAD_REQUEST;
    }
  db_t *db = game_db_get_handle (); if (!db)
    {
      LOGD("[play_login] DB handle is NULL. Returning AUTH_ERR_DB.");
      return AUTH_ERR_DB;
    }
  db_res_t *res = NULL; db_error_t err;
    const char *sql = "SELECT id, passwd, is_npc FROM players WHERE name = $1;";

    LOGD("[play_login] Executing query: %s for user: %s", sql, user);
  
    if (db_query (db, sql, (db_bind_t[]){db_bind_text (user)}, 1, &res, &err))
      {
        if (db_res_step (res, &err))
          {
            int player_id = db_res_col_i32 (res, 0, &err);
            const char *db_pass = db_res_col_text (res, 1, &err);
            bool is_npc_flag = (bool)db_res_col_i32 (res, 2, &err); // Get is_npc boolean
  
            LOGD("[play_login] Found user: %s (pid: %d, is_npc: %d). DB hashed pass: %s", user, player_id, is_npc_flag, db_pass);

            if (is_npc_flag)
              {
                LOGD("[play_login] User %s is an NPC. Returning ERR_IS_NPC.", user);
                db_res_finalize(res);
                return ERR_IS_NPC; // Use ERR_IS_NPC from errors.h
              }

          // TODO: Before comparing, hash the provided 'pass' using the same algorithm as 'db_pass'
          int cmp_result = ct_str_eq (db_pass, pass);
          LOGD("[play_login] Comparing DB pass with provided pass (result: %d). Client pass (plain): %s", cmp_result, pass);

          if (cmp_result)
            {
              *pid = player_id;
              db_res_finalize(res);
              LOGD("[play_login] Authentication successful for user %s. Returning AUTH_OK.", user);
              return AUTH_OK;
            }
          else
            {
              db_res_finalize(res);
              LOGD("[play_login] Password mismatch for user %s. Returning AUTH_ERR_INVALID_CRED.", user);
              return AUTH_ERR_INVALID_CRED;
            }
        }
      else
        {
          db_res_finalize(res);
          LOGD("[play_login] No user found with name %s. Returning AUTH_ERR_INVALID_CRED.", user);
          return AUTH_ERR_INVALID_CRED;
        }
    }
  else
    {
      LOGE("[play_login] DB query failed for user %s: %s (code=%d backend=%d)", user, err.message, err.code, err.backend_code);
      return AUTH_ERR_DB;
    }
}


int
user_create (db_t *db, const char *user, const char *pass, int *pid)
{
  if (!user || !pass)
    {
      return AUTH_ERR_BAD_REQUEST;
    }
  db_error_t err; int64_t player_id = 0;


  if (!db_exec_insert_id (db,
                          "INSERT INTO players (name, passwd) VALUES ($1, $2);",
                          (db_bind_t[]){db_bind_text (user),
                                        db_bind_text (pass)},
                          2,
                          &player_id,
                          &err))
    {
      if (err.code == ERR_DB_CONSTRAINT)
        {
          return ERR_NAME_TAKEN;
        }
      return AUTH_ERR_DB;
    }

  const char *sql_turns =
    "INSERT INTO turns (player, turns_remaining, last_update) "
    "SELECT $1, CAST(value AS INTEGER), EXTRACT(EPOCH FROM now()) "
    "FROM config "
    "WHERE key='turnsperday' "
    "ON CONFLICT(player) DO UPDATE SET "
    "  turns_remaining = excluded.turns_remaining, "
    "  last_update    = excluded.last_update;";


  if (!db_exec (db, sql_turns, (db_bind_t[]){db_bind_i64 (player_id)}, 1, &err))
    {
      LOGE ("user_create: turns upsert failed: %s", err.message);
    }

  /* Create default bank account for this player */
  int account_id = 0;


  if (db_bank_create_account (db, "player", (int)player_id, 0,
                              &account_id) != 0)
    {
      LOGE ("user_create: Failed to create default bank account: player %d",
            (int)player_id);
      return AUTH_ERR_DB;
    }

  if (pid)
    {
      *pid = (int)player_id;
    }
  return AUTH_OK;
}


int
cmd_sys_test_news_cron (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return AUTH_ERR_DB;
    }

  if (ctx->player_id <= 0 ||
      auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Permission denied", NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  const char *subcommand = NULL;
  int rc = 0;


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  json_t *j_subcommand = json_object_get (data, "subcommand");


  if (json_is_string (j_subcommand))
    {
      subcommand = json_string_value (j_subcommand);
    }
  else
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
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
          send_response_error (ctx,
                               root,
                               ERR_BAD_REQUEST,
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
          payload = json_deep_copy (j_payload);
        }
      else
        {
          payload = json_object ();
        }
      rc =
        db_log_engine_event ((long long) time (NULL), event_type, "player",
                             actor_player_id, sector_id, payload, db);
      if (rc == 0)
        {
          send_response_ok_take (ctx,
                                 root,
                                 "sys.test_news_cron.event_generated", NULL);
        }
      else
        {
          send_response_error (ctx,
                               root,
                               ERR_SERVER_ERROR,
                               "Failed to generate engine event.");
        }
    }
  else if (strcasecmp (subcommand, "run_compiler") == 0)
    {
      rc = h_daily_news_compiler (db, (long long) time (NULL));
      if (rc == 0)
        {
          send_response_ok_take (ctx,
                                 root,
                                 "sys.test_news_cron.compiler_ran", NULL);
        }
      else
        {
          send_response_error (ctx,
                               root,
                               ERR_SERVER_ERROR,
                               "Failed to run news compiler.");
        }
    }
  else if (strcasecmp (subcommand, "run_cleanup") == 0)
    {
      rc = h_cleanup_old_news (db, (long long) time (NULL));
      if (rc == 0)
        {
          send_response_ok_take (ctx,
                                 root,
                                 "sys.test_news_cron.cleanup_ran", NULL);
        }
      else
        {
          send_response_error (ctx,
                               root,
                               ERR_SERVER_ERROR,
                               "Failed to run news cleanup.");
        }
    }
  else
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Unknown subcommand.");
    }
  return 0;
}


int
cmd_sys_raw_sql_exec (client_ctx_t *ctx, json_t *root)
{
#ifdef BUILD_PRODUCTION
  send_response_error (ctx,
                       root,
                       REF_TURN_COST_EXCEEDS,
                       "Command disabled in production");
  return 0;
#else
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return AUTH_ERR_DB;
    }

  if (ctx->player_id <= 0 ||
      auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Permission denied", NULL);
      return 0;
    }
  json_t *jdata = json_object_get (root, "data");
  const char *sql = NULL;


  if (json_is_object (jdata))
    {
      sql = json_string_value (json_object_get (jdata, "sql"));
    }
  if (!sql)
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD, "Missing sql");
      return 0;
    }

  db_error_t err;


  if (!db_exec (db, sql, NULL, 0, &err))
    {
      send_response_error (ctx,
                           root,
                           ERR_PLANET_NOT_FOUND,
                           err.message[0] ? err.message : "SQL error");
    }
  else
    {
      json_t *res = json_string ("Executed");


      send_response_ok_take (ctx, root, "sys.raw_sql_exec", &res);
    }
  return 0;
#endif
}




int
cmd_bounty_post_federation (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SECTOR_NOT_FOUND, "Authentication required.");
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return AUTH_ERR_DB;
    }

  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  int target_player_id = 0;


  if (!json_get_int_flexible (data, "target_player_id",
                              &target_player_id) || target_player_id <= 0)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Invalid target_player_id.");
      return 0;
    }
  long long amount;


  if (!json_get_int64_flexible (data, "amount", &amount) || amount <= 0)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Invalid amount.");
      return 0;
    }
  const char *desc =
    json_string_value (json_object_get (data, "description"));


  if (!desc)
    {
      desc = "Wanted for crimes against the Federation.";
    }
  int sector = 0;


  db_player_get_sector (db, ctx->player_id, &sector);
  if (!is_fedspace_sector (sector))
    {
      send_response_error (ctx,
                           root,
                           REF_TURN_COST_EXCEEDS,
                           "Must be in FedSpace to post a Federation bounty.");
      return 0;
    }
  int issuer_alignment = 0;


  db_player_get_alignment (db, ctx->player_id, &issuer_alignment);
  if (issuer_alignment < 0)
    {
      send_response_error (ctx,
                           root,
                           REF_TURN_COST_EXCEEDS,
                           "Evil players cannot post Federation bounties.");
      return 0;
    }
  int target_alignment = 0;


  if (db_player_get_alignment (db, target_player_id,
                               &target_alignment) != 0)
    {
      send_response_error (ctx, root, 404, "Target player not found.");
      return 0;
    }
  if (target_alignment >= 0)
    {
      send_response_error (ctx,
                           root,
                           REF_TURN_COST_EXCEEDS,
                           "Target must be an evil player.");
      return 0;
    }
  if (target_player_id == ctx->player_id)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Cannot post bounty on yourself.");
      return 0;
    }
  long long new_balance = 0;


  if (h_deduct_credits (db,
                        "player",
                        ctx->player_id,
                        amount,
                        "WITHDRAWAL", NULL, &new_balance) != 0)
    {
      send_response_error (ctx, root, REF_NO_WARP_LINK,
                           "Insufficient funds.");
      return 0;
    }
  if (db_bounty_create (db,
                        "player",
                        ctx->player_id,
                        "player",
                        target_player_id, amount, desc) != 0)
    {
      h_add_credits (db,
                     "player",
                     ctx->player_id, amount, "DEPOSIT", NULL, &new_balance);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error creating bounty.");
      return 0;
    }
  send_response_ok_take (ctx, root, "bounty.post_federation.confirmed", NULL);
  return 0;
}


int
cmd_bounty_post_hitlist (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SECTOR_NOT_FOUND, "Authentication required.");
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return AUTH_ERR_DB;
    }

  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  int target_player_id = 0;


  if (!json_get_int_flexible (data, "target_player_id",
                              &target_player_id) || target_player_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Invalid target_player_id.");
      return 0;
    }
  long long amount;


  if (!json_get_int64_flexible (data, "amount", &amount) || amount <= 0)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Invalid amount.");
      return 0;
    }
  const char *desc =
    json_string_value (json_object_get (data, "description"));


  if (!desc)
    {
      desc = "Hit ordered.";
    }
  int sector = 0;


  db_player_get_sector (db, ctx->player_id, &sector);
  int port_id = db_get_port_id_by_sector (db, sector);


  if (port_id <= 0 || !is_black_market_port (db, port_id))
    {
      send_response_error (ctx,
                           root,
                           REF_TURN_COST_EXCEEDS,
                           "Must be at a Black Market port to post a Hit List contract.");
      return 0;
    }
  int issuer_alignment = 0;


  db_player_get_alignment (db, ctx->player_id, &issuer_alignment);
  if (issuer_alignment > 0)
    {
      send_response_error (ctx,
                           root,
                           REF_TURN_COST_EXCEEDS,
                           "Good players cannot post Hit List contracts.");
      return 0;
    }
  int target_alignment = 0;


  if (db_player_get_alignment (db, target_player_id,
                               &target_alignment) != 0)
    {
      send_response_error (ctx, root, 404, "Target player not found.");
      return 0;
    }
  if (target_alignment <= 0)
    {
      send_response_error (ctx,
                           root,
                           REF_TURN_COST_EXCEEDS,
                           "Target must be a good player.");
      return 0;
    }
  if (target_player_id == ctx->player_id)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Cannot put a hit on yourself.");
      return 0;
    }
  long long new_balance = 0;


  if (h_deduct_credits (db,
                        "player",
                        ctx->player_id,
                        amount,
                        "WITHDRAWAL", NULL, &new_balance) != 0)
    {
      send_response_error (ctx, root, REF_NO_WARP_LINK,
                           "Insufficient funds.");
      return 0;
    }
  if (db_bounty_create (db,
                        "player",
                        ctx->player_id,
                        "player",
                        target_player_id, amount, desc) != 0)
    {
      h_add_credits (db,
                     "player",
                     ctx->player_id, amount, "DEPOSIT", NULL, &new_balance);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error creating bounty.");
      return 0;
    }
  send_response_ok_take (ctx, root, "bounty.post_hitlist.confirmed", NULL);
  return 0;
}


int
cmd_bounty_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SECTOR_NOT_FOUND, "Authentication required.");
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return AUTH_ERR_DB;
    }

  int alignment = 0;


  db_player_get_alignment (db, ctx->player_id, &alignment);

  const char *sql = NULL;


  if (alignment >= 0)
    {
      sql =
        "SELECT b.id, b.target_id, p.name, b.reward, b.posted_by_type "
        "FROM bounties b "
        "JOIN players p ON b.target_id = p.id "
        "WHERE b.status = 'open' AND p.alignment < 0 "
        "ORDER BY b.reward DESC LIMIT 20;";
    }
  else
    {
      sql =
        "SELECT b.id, b.target_id, p.name, b.reward, b.posted_by_type "
        "FROM bounties b "
        "JOIN players p ON b.target_id = p.id "
        "WHERE b.status = 'open' AND (p.alignment > 0 OR b.posted_by_type = 'gov') "
        "ORDER BY b.reward DESC LIMIT 20;";
    }

  db_res_t *res = NULL;
  db_error_t err;


  if (db_query (db, sql, NULL, 0, &res, &err))
    {
      json_t *arr = json_array ();


      while (db_res_step (res, &err))
        {
          json_t *item = json_object ();


          json_object_set_new (item, "bounty_id",
                               json_integer (db_res_col_i32 (res,
                                                             0,
                                                             &err)));
          json_object_set_new (item, "target_id",
                               json_integer (db_res_col_i32 (res,
                                                             1,
                                                             &err)));
          json_object_set_new (item,
                               "target_name",
                               json_string (db_res_col_text (res, 2,
                                                             &err) ?: ""));
          json_object_set_new (item, "reward",
                               json_integer (db_res_col_i64 (res,
                                                             3,
                                                             &err)));
          json_object_set_new (item,
                               "posted_by_type",
                               json_string (db_res_col_text (res, 4,
                                                             &err) ?: ""));
          json_array_append_new (arr, item);
        }
      db_res_finalize (res);
      json_t *payload = json_object ();


      json_object_set_new (payload, "bounties", arr);
      send_response_ok_take (ctx, root, "bounty.list.success", &payload);
    }
  else
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error listing bounties.");
    }
  return 0;
}


int
cmd_sys_econ_planet_status (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return AUTH_ERR_DB;
    }

  if (ctx->player_id <= 0 ||
      auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Permission denied", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }

  int planet_id = json_integer_value (json_object_get (data, "planet_id"));


  if (planet_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Invalid or missing planet_id.");
      return 0;
    }

  json_t *response = json_object ();
  db_error_t err;
  db_res_t *res = NULL;

  // 1. Planet Basic Info
  const char *sql_planet =
    "SELECT id, name, type, owner_id, owner_type FROM planets WHERE id = $1;";


  if (db_query (db,
                sql_planet,
                (db_bind_t[]){db_bind_i32 (planet_id)},
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          json_object_set_new (response, "id",
                               json_integer (db_res_col_i32 (res,
                                                             0,
                                                             &err)));
          json_object_set_new (response, "name",
                               json_string (db_res_col_text (res,
                                                             1,
                                                             &err) ?: ""));
          json_object_set_new (response, "type",
                               json_integer (db_res_col_i32 (res,
                                                             2,
                                                             &err)));
          json_object_set_new (response, "owner_id",
                               json_integer (db_res_col_i32 (res, 3, &err)));
          json_object_set_new (response,
                               "owner_type",
                               json_string (db_res_col_text (res, 4,
                                                             &err) ?: ""));
        }
      else
        {
          send_response_error (ctx, root, 404, "Planet not found.");
          db_res_finalize (res);
          json_decref (response);
          return 0;
        }
      db_res_finalize (res);
    }
  else
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "DB error fetching planet info.");
      json_decref (response);
      return 0;
    }

  // 2. Current Stock
  json_t *stock_arr = json_array ();
  const char *sql_stock =
    "SELECT es.commodity_code, c.id, es.quantity "
    "FROM entity_stock es "
    "JOIN commodities c ON es.commodity_code = c.code "
    "WHERE es.entity_type = 'planet' AND es.entity_id = $1;";


  if (db_query (db,
                sql_stock,
                (db_bind_t[]){db_bind_i32 (planet_id)},
                1,
                &res,
                &err))
    {
      while (db_res_step (res, &err))
        {
          json_t *item = json_object ();


          json_object_set_new (item, "commodity_id",
                               json_integer (db_res_col_i32 (res, 1, &err)));
          json_object_set_new (item, "quantity",
                               json_integer (db_res_col_i32 (res,
                                                             2,
                                                             &err)));
          json_array_append_new (stock_arr, item);
        }
      db_res_finalize (res);
    }
  json_object_set_new (response, "stock", stock_arr);

  // 3. Open Orders
  json_t *orders = db_list_actor_orders (db, "planet", planet_id);


  json_object_set_new (response, "orders", orders);

  // 4. Bank Balance
  long long balance = 0;


  db_get_planet_bank_balance (db, planet_id, &balance);
  json_object_set_new (response, "bank_balance", json_integer (balance));

  send_response_ok_take (ctx, root, "sys.econ.planet_status", &response);
  return 0;
}


int
cmd_sys_econ_port_status (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return AUTH_ERR_DB;
    }

  if (ctx->player_id <= 0 ||
      auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Permission denied", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data object");
      return 0;
    }

  int port_id = json_integer_value (json_object_get (data, "port_id"));


  if (port_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Invalid or missing port_id");
      return 0;
    }

  json_t *response = json_object ();
  db_error_t err;
  db_res_t *res = NULL;

  // 1. Port Basic Info
  const char *sql_port = "SELECT id, name, size FROM ports WHERE id = $1;";


  if (db_query (db,
                sql_port,
                (db_bind_t[]){db_bind_i32 (port_id)},
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          json_object_set_new (response, "id",
                               json_integer (db_res_col_i32 (res,
                                                             0,
                                                             &err)));
          json_object_set_new (response, "name",
                               json_string (db_res_col_text (res,
                                                             1,
                                                             &err) ?: ""));
          json_object_set_new (response, "size",
                               json_integer (db_res_col_i32 (res,
                                                             2,
                                                             &err)));
        }
      db_res_finalize (res);
    }
  else
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR,
                           "DB error fetching port");
      json_decref (response);
      return 0;
    }

  // 2. Current Stock
  json_t *stock_arr = json_array ();
  const char *sql_stock =
    "SELECT es.commodity_code, c.id, es.quantity "
    "FROM entity_stock es "
    "JOIN commodities c ON es.commodity_code = c.code "
    "WHERE es.entity_type = 'port' AND es.entity_id = $1;";


  if (db_query (db,
                sql_stock,
                (db_bind_t[]){db_bind_i32 (port_id)},
                1,
                &res,
                &err))
    {
      while (db_res_step (res, &err))
        {
          json_t *item = json_object ();


          json_object_set_new (item, "commodity_id",
                               json_integer (db_res_col_i32 (res, 1, &err)));
          json_object_set_new (item, "quantity",
                               json_integer (db_res_col_i32 (res,
                                                             2,
                                                             &err)));
          json_array_append_new (stock_arr, item);
        }
      db_res_finalize (res);
    }
  json_object_set_new (response, "stock", stock_arr);

  // 3. Open Orders
  json_t *orders = db_list_port_orders (db, port_id);


  json_object_set_new (response, "orders", orders);

  send_response_ok_take (ctx, root, "sys.econ.port_status", &response);
  return 0;
}


int
cmd_sys_econ_orders_summary (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return AUTH_ERR_DB;
    }

  if (ctx->player_id <= 0 ||
      auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Permission denied", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  int commodity_id = 0;


  if (data)
    {
      json_t *jcomm = json_object_get (data, "commodity_id");


      if (json_is_integer (jcomm))
        {
          commodity_id = json_integer_value (jcomm);
        }
    }

  json_t *summary = db_orders_summary (db, commodity_id);


  send_response_ok_take (ctx, root, "sys.econ.orders_summary", &summary);
  return 0;
}


int
cmd_sys_npc_ferengi_tick_once (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0 ||
      auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Permission denied", NULL);
      return 0;
    }

  // Trigger one tick of Ferengi logic
  // Passing 0 as now_ms as it's reserved/unused for rate limiting in the current implementation
  fer_tick (0);

  send_response_ok_take (ctx, root, "sys.npc.ferengi_tick_once", NULL);
  return 0;
}


int
cmd_debug_run_fedspace_cleanup (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_NOT_AUTHENTICATED,
                                   "Authentication required.", NULL);
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "Database unavailable.");
      return 0;
    }
  // Ensure player is an admin/sysop.
  if (auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Permission denied", NULL);
      return 0;
    }
  // Pass current time to h_fedspace_cleanup
  h_fedspace_cleanup (db, time (NULL));
  send_response_ok_take (ctx, root, "debug.fedspace_cleanup_run", NULL);
  return 0;
}



