#include <strings.h>#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jansson.h>            /* -ljansson */
#include <stdbool.h>
#include <uuid/uuid.h>          // For UUID generation
/* local includes */
#include "schemas.h"
#include "server_cmds.h"
#include "server_loop.h"
#include "server_ships.h"
#include "database.h"
#include "errors.h"
#include "config.h"
#include "common.h"
#include "server_envelope.h"
#include "server_rules.h"
#include "server_players.h"
#include "server_log.h"
#include "server_ports.h"


#define UUID_STR_LEN 37         // 36 chars + null terminator

// Central handler for ship destruction
int
handle_ship_destruction (db_t *db, ship_kill_context_t *ctx)
{
  db_error_t err;
  int retry_count;
  int current_timestamp = (int)time (NULL);

  for (retry_count = 0; retry_count < 3; retry_count++)
    {
      db_error_clear (&err);

      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
        {
          if (err.code == ERR_DB_BUSY)
            {
              usleep (100000); continue;
            }
          return err.code;
        }

      LOGI (
        "handle_ship_destruction: Victim Player ID: %d, Ship ID: %d, Cause: %d",
        ctx->victim_player_id,
        ctx->victim_ship_id,
        ctx->cause);

      // --- 1.3 Implement Ship Removal/Detachment ---
      int sub_rc = db_mark_ship_destroyed (db, ctx->victim_ship_id);


      if (sub_rc != 0)
        {
          LOGE (
            "handle_ship_destruction: Failed to mark ship %d as destroyed: %d",
            ctx->victim_ship_id,
            sub_rc);
          goto rollback;
        }

      sub_rc = db_clear_player_active_ship (db, ctx->victim_player_id);
      if (sub_rc != 0)
        {
          LOGE (
            "handle_ship_destruction: Failed to clear active ship for player %d: %d",
            ctx->victim_player_id,
            sub_rc);
          goto rollback;
        }

      // --- 1.4 Implement Player Stat Update ---
      (void) db_increment_player_stat (db,
                                       ctx->victim_player_id,
                                       "times_blown_up");

      int current_xp = db_get_player_xp (db, ctx->victim_player_id);
      int xp_loss = g_cfg.death.xp_loss_flat +
                    (int) (current_xp * (g_cfg.death.xp_loss_percent / 100.0));
      int new_xp = MAX (0, current_xp - xp_loss);


      (void) db_update_player_xp (db, ctx->victim_player_id, new_xp);

      // --- 1.5 Escape Pod vs. Big Sleep ---
      bool has_escape_pod = db_shiptype_has_escape_pod (db,
                                                        ctx->victim_ship_id);
      int podded_count_today = db_get_player_podded_count_today (db,
                                                                 ctx->
                                                                 victim_player_id);
      long long last_reset_timestamp = db_get_player_podded_last_reset (db,
                                                                        ctx->
                                                                        victim_player_id);


      if (current_timestamp - last_reset_timestamp >= 86400)
        {
          podded_count_today = 0;
          db_reset_player_podded_count (db,
                                        ctx->victim_player_id,
                                        current_timestamp);
        }

      if (has_escape_pod && (podded_count_today < g_cfg.death.max_per_day))
        {
          sub_rc = handle_escape_pod_spawn (db, ctx);
          if (sub_rc != 0)
            {
              LOGW (
                "handle_ship_destruction: Pod spawn failed, forcing Big Sleep: %d",
                sub_rc);
              sub_rc = handle_big_sleep (db, ctx);
              if (sub_rc != 0)
                {
                  goto rollback;
                }
            }
        }
      else
        {
          sub_rc = handle_big_sleep (db, ctx);
          if (sub_rc != 0)
            {
              goto rollback;
            }
        }

      // --- 1.6 Emit Engine Events ---
      json_t *event_payload = json_object ();


      if (event_payload)
        {
          json_object_set_new (event_payload, "victim_ship_id",
                               json_integer (ctx->victim_ship_id));
          json_object_set_new (event_payload, "victim_player_id",
                               json_integer (ctx->victim_player_id));
          json_object_set_new (event_payload, "killer_player_id",
                               json_integer (ctx->killer_player_id));
          json_object_set_new (event_payload, "cause",
                               json_string (ctx->cause ==
                                            KILL_CAUSE_COMBAT ? "combat" :
                                            ctx->cause ==
                                            KILL_CAUSE_MINES ? "mines" :
                                            ctx->cause ==
                                            KILL_CAUSE_QUASAR ? "quasar" :
                                            ctx->cause ==
                                            KILL_CAUSE_NAVHAZ ? "navhaz" :
                                            ctx->cause ==
                                            KILL_CAUSE_SELF_DESTRUCT ?
                                            "self_destruct" : "other"));
          json_object_set_new (event_payload, "sector_id",
                               json_integer (ctx->sector_id));
          (void) db_log_engine_event ((long long) current_timestamp,
                                      "ship.destroyed",
                                      "system",
                                      0,
                                      ctx->sector_id,
                                      event_payload,
                                      NULL);
        }

      if (!db_tx_commit (db, &err))
        {
          LOGE ("handle_ship_destruction: Commit failed: %s", err.message);
          goto rollback;
        }
      return 0; // Success

rollback:
      db_tx_rollback (db, &err);
      if (err.code == ERR_DB_BUSY)
        {
          usleep (100000); continue;
        }
      return (sub_rc != 0) ? sub_rc : (err.code != 0 ? err.code : -1);
    }
  return ERR_DB_BUSY;
}


int
handle_big_sleep (db_t *db, ship_kill_context_t *ctx)
{
  db_error_t err;
  db_error_clear (&err);
  long long big_sleep_until_ts =
    time (NULL) + g_cfg.death.big_sleep_duration_seconds;


  LOGI ("handle_big_sleep: Player %d entering Big Sleep until %lld.",
        ctx->victim_player_id, big_sleep_until_ts);

  // Update podded_status table
  const char *sql =
    "INSERT OR REPLACE INTO podded_status (player_id, status, big_sleep_until) "
    "VALUES ($1, 'big_sleep', $2);";

  db_bind_t params[] = {
    db_bind_i32 (ctx->victim_player_id),
    db_bind_i64 (big_sleep_until_ts)
  };
  size_t n_params = sizeof(params) / sizeof(params[0]);


  if (!db_exec (db, sql, params, n_params, &err))
    {
      LOGE ("handle_big_sleep: Failed to update podded_status: %s",
            err.message);
      return err.code;
    }
  return 0;
}


int
handle_escape_pod_spawn (db_t *db, ship_kill_context_t *ctx)
{
  db_error_t err;
  int retry_count;
  for (retry_count = 0; retry_count < 3; retry_count++)
    {
      db_error_clear (&err);

      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
        {
          if (err.code == ERR_DB_BUSY)
            {
              usleep (100000); continue;
            }
          return err.code;
        }

      int64_t new_pod_ship_id = -1;
      int pod_sector_id = ctx->sector_id;
      int escape_pod_shiptype_id = 0;

      // 1. Create ship record
      const char *sql_ins_ship =
        "INSERT INTO ships (name, type_id, holds, fighters, shields, sector) VALUES ($1, $2, $3, $4, $5, $6);";
      db_bind_t ins_ship_params[] = { db_bind_text ("Escape Pod"),
                                      db_bind_i32 (escape_pod_shiptype_id),
                                      db_bind_i32 (5), db_bind_i32 (50),
                                      db_bind_i32 (50),
                                      db_bind_i32 (pod_sector_id) };


      if (!db_exec (db, sql_ins_ship, ins_ship_params, 6, &err))
        {
          goto rollback;
        }
      new_pod_ship_id = db_last_insert_rowid (db, &err);

      // 2. Set ownership
      const char *sql_ins_own =
        "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES ($1, $2, 1, 1);";
      db_bind_t ins_own_params[] = { db_bind_i64 (new_pod_ship_id),
                                     db_bind_i32 (ctx->victim_player_id) };


      if (!db_exec (db, sql_ins_own, ins_own_params, 2, &err))
        {
          goto rollback;
        }

      // 3. Set primary ship
      const char *sql_upd_plr =
        "UPDATE players SET ship = $1, sector = $2 WHERE id = $3;";
      db_bind_t upd_plr_params[] = { db_bind_i64 (new_pod_ship_id),
                                     db_bind_i32 (pod_sector_id),
                                     db_bind_i32 (ctx->victim_player_id) };


      if (!db_exec (db, sql_upd_plr, upd_plr_params, 3, &err))
        {
          goto rollback;
        }

      // 4. Update podded_status
      const char *sql_upd_pod =
        "INSERT OR REPLACE INTO podded_status (player_id, status, podded_count_today, podded_last_reset) "
        "VALUES ($1, 'active', COALESCE((SELECT podded_count_today FROM podded_status WHERE player_id=$2), 0) + 1, "
        "COALESCE((SELECT podded_last_reset FROM podded_status WHERE player_id=$3), strftime('%s','now')));";
      db_bind_t upd_pod_params[] = { db_bind_i32 (ctx->victim_player_id),
                                     db_bind_i32 (ctx->victim_player_id),
                                     db_bind_i32 (ctx->victim_player_id) };


      if (!db_exec (db, sql_upd_pod, upd_pod_params, 3, &err))
        {
          goto rollback;
        }

      if (!db_tx_commit (db, &err))
        {
          goto rollback;
        }
      return 0;

rollback:
      db_tx_rollback (db, &err);
      if (err.code == ERR_DB_BUSY)
        {
          usleep (100000); continue;
        }
      return err.code;
    }
  return ERR_DB_BUSY;
}


void handle_move_pathfind (client_ctx_t *ctx, json_t *root);


int
cmd_ship_transfer_cargo (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  h_decloak_ship (db,
                  h_get_active_ship_id (db, ctx->player_id));
  send_response_error (ctx,
                       root,
                       ERR_NOT_IMPLEMENTED,
                       "Not implemented: " "ship.transfer_cargo");
  return 0;
}


int
cmd_ship_upgrade (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx,
                       root,
                       ERR_NOT_IMPLEMENTED,
                       "Use hardware.buy for components or shipyard.upgrade for hull exchange.");
  return 0;
}


int


cmd_ship_repair (client_ctx_t *ctx, json_t *root)


{
  if (!require_auth (ctx, root))


    {
      return 0;
    }


  db_t *db = game_db_get_handle ();


  if (!db)


    {
      send_response_error (ctx, root, ERR_DB_UNAVAILABLE,
                           "Database unavailable");


      return 0;
    }


  int ship_id = h_get_active_ship_id (db,
                                      ctx->player_id);


  if (ship_id <= 0)


    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");


      return 0;
    }


  db_error_t err;


  int retry_count;


  for (retry_count = 0; retry_count < 3; retry_count++)
    {
      db_error_clear (&err);


      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
        {
          if (err.code == ERR_DB_BUSY)
            {
              usleep (100000); continue;
            }


          send_response_error (ctx, root, err.code, "Transaction failed");


          return 0;
        }


      // 1. Verify Location (Must be at Port or Planet)


      bool at_base = false;


      db_res_t *loc_res = NULL;


      const char *sql_loc =
        "SELECT 1 FROM ships WHERE id=

 AND (ported=1 OR onplanet=1) FOR UPDATE;";


      db_bind_t loc_params[] = { db_bind_i32 (ship_id) };


      if (db_query (db, sql_loc, loc_params, 1, &loc_res, &err))
        {
          at_base = db_res_step (loc_res, &err);


          db_res_finalize (loc_res);
        }


      if (!at_base)
        {
          send_response_error (ctx,
                               root,
                               ERR_BAD_STATE,
                               "Must be at a port or planet to repair.");


          goto rollback;
        }


      // 2. Get Current Hull


      int current_hull = 0;


      db_res_t *hull_res = NULL;


      const char *sql_hull = "SELECT hull FROM ships WHERE id = 

 FOR UPDATE;";


      db_bind_t hull_params[] = { db_bind_i32 (ship_id) };


      if (db_query (db, sql_hull, hull_params, 1, &hull_res, &err))
        {
          if (db_res_step (hull_res, &err))
            {
              current_hull = db_res_col_i32 (hull_res, 0, &err);
            }


          db_res_finalize (hull_res);
        }


      if (current_hull >= 100)
        {
          send_response_error (ctx,
                               root,
                               ERR_BAD_STATE,
                               "Ship is already at full hull strength.");


          goto rollback;
        }


      // 3. Calculate Cost


      int damage = 100 - current_hull;


      int cost = damage * 50; // 50 credits per % hull


      // 4. Debit Player


      int64_t new_player_credits = 0;


      const char *sql_debit =
        "UPDATE players SET credits = credits - 

 WHERE id = $2 AND credits >= 

 RETURNING credits;";


      db_bind_t debit_params[] = { db_bind_i32 (cost),
                                   db_bind_i32 (ctx->player_id) };


      db_res_t *debit_res = NULL;


      if (!db_query (db, sql_debit, debit_params, 2, &debit_res, &err))
        {
          goto rollback;
        }


      if (db_res_step (debit_res, &err))
        {
          new_player_credits = db_res_col_i64 (debit_res, 0, &err);


          db_res_finalize (debit_res);
        }
      else
        {
          db_res_finalize (debit_res);


          send_response_error (ctx,
                               root,
                               ERR_INSUFFICIENT_FUNDS,
                               "Insufficient credits for repair.");


          goto rollback;
        }


      // 5. Update Ship


      const char *sql_upd = "UPDATE ships SET hull = 100 WHERE id = 

;";


      db_bind_t upd_params[] = { db_bind_i32 (ship_id) };


      if (!db_exec (db, sql_upd, upd_params, 1, &err))
        {
          goto rollback;
        }


      if (!db_tx_commit (db, &err))
        {
          goto rollback;
        }


      // Success response


      json_t *resp = json_object ();


      json_object_set_new (resp, "hull_after", json_integer (100));


      json_object_set_new (resp, "credits_spent", json_integer (cost));


      json_object_set_new (resp, "petty_cash",
                           json_integer (new_player_credits));


      send_response_ok_take (ctx, root, "ship.repair.result", &resp);


      return 1;


rollback:


      db_tx_rollback (db, &err);


      if (err.code == ERR_DB_BUSY)
        {
          usleep (100000); continue;
        }


      return 0;
    }


  return 0;
}


/* ship.inspect */


/* ship.inspect */
int
cmd_ship_inspect (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }
  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 1;
  json_t *jdata = json_object_get (root, "data");


  if (json_is_object (jdata))
    {
      json_t *jsec = json_object_get (jdata, "sector_id");


      if (json_is_integer (jsec))
        {
          int s = (int) json_integer_value (jsec);


          if (s > 0)
            {
              sector_id = s;
            }
        }
    }
  
  db_t *db = game_db_get_handle();
  json_t *ships = NULL;
  int rc = db_ships_inspectable_at_sector_json (db, ctx->player_id, sector_id, &ships);


  if (rc != 0 || !ships)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Database error");
      return 0;
    }
  json_t *payload = json_object ();


  json_object_set_new (payload, "sector", json_integer (sector_id));
  json_object_set (payload, "ships", ships);


  send_response_ok_take (ctx, root, "ship.inspect", &payload);
  return 0;
}


/* ship.rename + ship.reregister */
int
cmd_ship_rename (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_NOT_IN_SECTOR,
                                   "Bad request",
                                   NULL);
      return 0;
    }
  json_t *j_ship = json_object_get (data, "ship_id");
  json_t *j_name = json_object_get (data, "new_name");


  if (!json_is_integer (j_ship) || !json_is_string (j_name))
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_NOT_IN_SECTOR,
                                   "Missing ship_id/new_name",
                                   NULL);
      return 0;
    }
  int ship_id = (int) json_integer_value (j_ship);
  const char *new_name = json_string_value (j_name);
  db_t *db = game_db_get_handle ();
  int rc = db_ship_rename_if_owner (db, ctx->player_id, ship_id, new_name);


  if (rc == ERR_DB_CONSTRAINT)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_TURN_COST_EXCEEDS,
                                   "Permission denied",
                                   NULL);
      return 0;
    }
  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Database error");
      return 0;
    }
  json_t *payload = json_object ();


  json_object_set_new (payload, "ship_id", json_integer (ship_id));
  json_object_set_new (payload, "name", json_string (new_name));


  send_response_ok_take (ctx, root, "ship.renamed", &payload);
  return 0;
}


/* ship.claim */
int
cmd_ship_claim (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  h_decloak_ship (db,
                  h_get_active_ship_id (db, ctx->player_id));
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }
  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 1;
  json_t *data = json_object_get (root, "data");


  if (json_is_object (data))
    {
      json_t *jsec = json_object_get (data, "sector_id");


      if (json_is_integer (jsec))
        {
          int s = (int) json_integer_value (jsec);


          if (s > 0)
            {
              sector_id = s;
            }
        }
    }
  int ship_id = -1;


  if (json_is_object (data))
    {
      json_t *j_ship = json_object_get (data, "ship_id");


      if (json_is_integer (j_ship))
        {
          ship_id = (int) json_integer_value (j_ship);
        }
    }
  if (ship_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_NOT_IN_SECTOR,
                                   "Missing ship_id",
                                   NULL);
      return 0;
    }
  json_t *ship = NULL;
  int rc = db_ship_claim (db, ctx->player_id, sector_id, ship_id, &ship);


  if (rc != 0 || !ship)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_SAFE_ZONE_ONLY,
                                   "Ship not claimable",
                                   NULL);
      return 0;
    }
  json_t *payload = json_object ();


  json_object_set (payload, "ship", ship);


  send_response_ok_take (ctx, root, "ship.claimed", &payload);
  return 0;
}


/* ship.status (canonical) */
int
cmd_ship_status (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }
  
  db_t *db = game_db_get_handle();
  json_t *player_info = NULL;


  if (db_player_info_json (db, ctx->player_id, &player_info) != 0
      || !player_info)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Database error");
      return 0;
    }
      return 0;
    }
  // Extract ship-only view from player_info (adjust keys to match your schema)
  json_t *ship = json_object_get (player_info, "ship");


  if (!json_is_object (ship))
    {
      json_decref (player_info);
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "No ship info");
      return 0;
    }
  // Build payload (clone or pack fields as needed)
  json_t *payload = json_object ();


  json_object_set_new (payload, "ship", json_incref (ship));


  send_response_ok_take (ctx, root, "ship.status", &payload);
  json_decref (player_info);
  return 0;
}


/* ship.info legacy alias → call status then mark deprecated (in envelope meta) */
int
cmd_ship_info_compat (client_ctx_t *ctx, json_t *root)
{
  // Call canonical
  int rc = cmd_ship_status (ctx, root);
  // If your envelope code supports meta injection per request, you can set it there.
  // Otherwise leave as-is; the handler name + docs indicate deprecation.
  return rc;
}


/*
 * cmd_ship_self_destruct: Initiate player ship self-destruct sequence.
 *
 * This command requires a non-zero integer confirmation in the payload
 * (e.g., { "data": { "confirmation": 42 } }).
 * It is explicitly refused in protected sectors (FedSpace).
 *
 * Request: { "command": "ship.self_destruct", "data": { "confirmation": 42 } }
 * Success: { "status": "ok", "type": "ship.self_destruct.confirmed" }
 * Refused: { "status": "refused", "error": { "code": 1007|1506 } }
 */
int
cmd_ship_self_destruct (client_ctx_t *ctx, json_t *root)
{
  int confirmation = 0;
  json_t *data = json_object_get (root, "data");
  if (!data || json_unpack (data, "{s:i}", "confirmation", &confirmation) != 0
      || confirmation == 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_CONFIRMATION_REQUIRED,
                                   "Self-destruct requires explicit non-zero integer confirmation.",
                                   NULL);
      return -1;
    }
  
  db_t *db = game_db_get_handle ();
  /* 2. Refusal check: Cannot self-destruct in protected zones (FedSpace) */
  if (db_is_sector_fedspace (db, ctx->sector_id))
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_FORBIDDEN_IN_SECTOR,
                                   "Cannot self-destruct in protected FedSpace.",
                                   NULL);
      return -1;
    }
  /* 3. Invoke handle_ship_destruction for self-destruct logic */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  int ship_sector = db_get_ship_sector_id (db, ship_id);
  ship_kill_context_t kill_ctx = {
    .victim_player_id = ctx->player_id,
    .victim_ship_id = ship_id,
    .killer_player_id = ctx->player_id, // Self-destruct, so killer is also the victim
    .cause = KILL_CAUSE_SELF_DESTRUCT,
    .sector_id = ship_sector > 0 ? ship_sector : 1      // Fallback to sector 1 if current sector is invalid
  };
  int destroy_rc = handle_ship_destruction (db, &kill_ctx);


  if (destroy_rc != 0)
    {
      LOGE
      (
        "cmd_ship_self_destruct: handle_ship_destruction failed for player %d, ship %d: %d",
        ctx->player_id,
        ship_id,
        destroy_rc);
      send_response_error (ctx,
                           root,
                           ERR_PLANET_NOT_FOUND,
                           "Failed to process self-destruction.");
      return 0;
    }
  /* 4. Response: command acknowledged and processed */
  send_response_ok_take (ctx, root, "ship.self_destruct.confirmed", NULL);
  return 0;
}


int
cmd_ship_tow (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }

  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }


  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (player_ship_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NO_ACTIVE_SHIP,
                                   "You do not have an active ship.",
                                   NULL);
      return 0;
    }

  /* -----------------------------------------------------------
     1. Get Player's Current Tow Status
     ----------------------------------------------------------- */
  db_error_t err;
  db_error_clear(&err);
  
  int current_towing_ship_id = 0;
  const char *sql_get_tow = "SELECT towing_ship_id FROM ships WHERE id = $1;";
  db_bind_t params_get[] = { db_bind_i32(player_ship_id) };
  db_res_t *res_get = NULL;

  if (db_query(db, sql_get_tow, params_get, 1, &res_get, &err)) {
      if (db_res_step(res_get, &err)) {
          current_towing_ship_id = db_res_col_i32(res_get, 0, &err);
      }
      db_res_finalize(res_get);
  } else {
      LOGE ("cmd_ship_tow: DB error checking status: %s", err.message);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Database error");
      return 0;
  }

  /* -----------------------------------------------------------
     2. Parse Input
     ----------------------------------------------------------- */
  json_t *data = json_object_get (root, "data");
  int target_ship_id = 0;


  if (json_is_object (data))
    {
      json_t *j_target = json_object_get (data, "target_ship_id");


      if (json_is_integer (j_target))
        {
          target_ship_id = (int)json_integer_value (j_target);
        }
    }

  /* -----------------------------------------------------------
     3. DISENGAGE LOGIC
     If we are towing, and target is 0 OR matches current, drop it.
     ----------------------------------------------------------- */
  if (current_towing_ship_id != 0)
    {
      if (target_ship_id == 0 || target_ship_id == current_towing_ship_id)
        {
          int retry_count;
          for (retry_count = 0; retry_count < 3; retry_count++) {
              if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) {
                  if (err.code == ERR_DB_BUSY) { usleep(100000); continue; }
                  goto rollback;
              }

              const char *sql_untow_player = "UPDATE ships SET towing_ship_id = 0 WHERE id = $1;";
              db_bind_t p1[] = { db_bind_i32(player_ship_id) };
              if (!db_exec(db, sql_untow_player, p1, 1, &err)) goto rollback_inner;

              const char *sql_untow_target = "UPDATE ships SET is_being_towed_by = 0 WHERE id = $1;";
              db_bind_t p2[] = { db_bind_i32(current_towing_ship_id) };
              if (!db_exec(db, sql_untow_target, p2, 1, &err)) goto rollback_inner;

              if (!db_tx_commit(db, &err)) goto rollback_inner;

              json_t *tmp = json_object ();
              json_object_set_new (tmp, "status", json_string ("Towing beam disengaged"));
              json_object_set_new (tmp, "towee_ship_id", json_integer (current_towing_ship_id));
              send_response_ok_take (ctx, root, "ship.tow.disengaged", &tmp);
              return 0;

rollback_inner:
              db_tx_rollback(db, &err);
              if (err.code == ERR_DB_BUSY) { usleep(100000); continue; }
              goto rollback;
          }
          return 0;
        }
      else
        {
          send_response_refused_steal (ctx,
                                       root,
                                       REF_ALREADY_TOWING,
                                       "You are already towing another ship. Release it first.",
                                       NULL);
          return 0;
        }
    }

  /* -----------------------------------------------------------
     4. ENGAGE LOGIC - Prerequisites
     ----------------------------------------------------------- */
  if (target_ship_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_MISSING_FIELD,
                                   "Invalid target ship ID.",
                                   NULL);
      return 0;
    }
  if (target_ship_id == player_ship_id)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_TARGET_SHIP_INVALID,
                                   "You cannot tow yourself.",
                                   NULL);
      return 0;
    }

  // Helper lookups
  int target_sector = db_get_ship_sector_id (db, target_ship_id);
  int player_sector = db_get_ship_sector_id (db, player_ship_id);


  if (target_sector <= 0 || target_sector != player_sector)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_TARGET_SHIP_INVALID,
                                   "Target ship not in your sector.",
                                   NULL);
      return 0;
    }

  // Ownership Check
  int owner_id = 0, corp_id = 0;
  db_get_ship_owner_id (db, target_ship_id, &owner_id, &corp_id);

  bool is_mine = (owner_id == ctx->player_id);
  bool is_corp = (ctx->corp_id > 0 && corp_id == ctx->corp_id);


  if (!is_mine && !is_corp)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_SHIP_NOT_OWNED_OR_PILOTED,
                                   "You do not own that ship.",
                                   NULL);
      return 0;
    }

  // Unmanned Check
  if (db_is_ship_piloted (db, target_ship_id))
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_SHIP_NOT_OWNED_OR_PILOTED,
                                   "Target ship is currently piloted.",
                                   NULL);
      return 0;
    }

  // Already Towed Check
  int is_being_towed = 0;
  const char *sql_check_towed = "SELECT is_being_towed_by FROM ships WHERE id = $1;";
  db_bind_t p_check[] = { db_bind_i32(target_ship_id) };
  db_res_t *res_check = NULL;
  if (db_query(db, sql_check_towed, p_check, 1, &res_check, &err)) {
      if (db_res_step(res_check, &err)) {
          is_being_towed = db_res_col_i32(res_check, 0, &err);
      }
      db_res_finalize(res_check);
  }

  if (is_being_towed != 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_ALREADY_BEING_TOWED,
                                   "Target is already under tow.",
                                   NULL);
      return 0;
    }

  /* -----------------------------------------------------------
     5. ENGAGE LOGIC - Execution
     ----------------------------------------------------------- */
  int retry_count;
  for (retry_count = 0; retry_count < 3; retry_count++) {
      if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) {
          if (err.code == ERR_DB_BUSY) { usleep(100000); continue; }
          goto rollback;
      }

      const char *sql_set_tow = "UPDATE ships SET towing_ship_id = $1 WHERE id = $2;";
      db_bind_t p1[] = { db_bind_i32(target_ship_id), db_bind_i32(player_ship_id) };
      if (!db_exec(db, sql_set_tow, p1, 2, &err)) goto rollback_inner_engage;

      const char *sql_set_towed_by = "UPDATE ships SET is_being_towed_by = $1 WHERE id = $2;";
      db_bind_t p2[] = { db_bind_i32(player_ship_id), db_bind_i32(target_ship_id) };
      if (!db_exec(db, sql_set_towed_by, p2, 2, &err)) goto rollback_inner_engage;

      if (!db_tx_commit(db, &err)) goto rollback_inner_engage;

      json_t *tmp = json_object ();
      json_object_set_new (tmp, "status", json_string ("Towing beam engaged"));
      json_object_set_new (tmp, "towee_ship_id", json_integer (target_ship_id));
      send_response_ok_take (ctx, root, "ship.tow.engaged", &tmp);
      return 0;

rollback_inner_engage:
      db_tx_rollback(db, &err);
      if (err.code == ERR_DB_BUSY) { usleep(100000); continue; }
      goto rollback;
  }

  return 0;

rollback:
  LOGE ("cmd_ship_tow: DB error: %s", err.message);
  send_response_error (ctx,
                       root,
                       ERR_DB_QUERY_FAILED,
                       "Database error");
  return 0;
}


int
h_get_active_ship_id (db_t *db, int player_id)
{
  if (!db || player_id <= 0)
    {
      return -1;
    }

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);
  int ship_id = -1;

  const char *sql = "SELECT ship FROM players WHERE id = $1;";
  db_bind_t params[] = { db_bind_i32 (player_id) };


  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          ship_id = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }
  else
    {
      LOGE ("h_get_active_ship_id: query failed: %s", err.message);
    }
  return ship_id;
}


/**
 * @brief Updates the cargo of a ship.
 *
 * Enforces non-negative quantities and ensures total cargo does not exceed holds.
 *
 * @param db The SQLite database handle.
 * @param ship_id The ID of the ship to update.
 * @param commodity_code The commodity code ("ORE", "ORG", "EQU").
 * @param delta The amount to change the quantity by (positive or negative).
 * @return SQLITE_OK on success, SQLITE_CONSTRAINT if limits violated, or error code.
 */
int
h_update_ship_cargo (db_t *db,
                     int ship_id,
                     const char *commodity_code,
                     int delta,
                     int *new_quantity_out)
{
  if (!db || ship_id <= 0 || !commodity_code)
    {
      if (new_quantity_out)
        {
          *new_quantity_out = 0;
        }
      return ERR_DB_MISUSE;
    }

  // Map commodity code to column name
  const char *col_name = NULL;
  if (strcasecmp (commodity_code, "ORE") == 0) col_name = "ore";
  else if (strcasecmp (commodity_code, "ORG") == 0) col_name = "organics";
  else if (strcasecmp (commodity_code, "EQU") == 0) col_name = "equipment";
  else if (strcasecmp (commodity_code, "COLONISTS") == 0) col_name = "colonists";
  else if (strcasecmp (commodity_code, "SLAVES") == 0) col_name = "slaves";
  else if (strcasecmp (commodity_code, "WEAPONS") == 0) col_name = "weapons";
  else if (strcasecmp (commodity_code, "DRUGS") == 0) col_name = "drugs";
  else
    {
      LOGE ("h_update_ship_cargo: Invalid commodity code %s", commodity_code);
      return ERR_INVALID_ARG;
    }

  db_error_t err;
  db_error_clear(&err);

  // Optimization: Use stored procedure if Postgres
  if (db_backend(db) == DB_BACKEND_POSTGRES) {
      const char *sql_proc = "SELECT code, message, id FROM ship_update_cargo($1, $2, $3);";
      db_bind_t params[] = {
          db_bind_i32(ship_id),
          db_bind_text(col_name),
          db_bind_i32(delta)
      };
      db_res_t *res = NULL;
      if (db_query(db, sql_proc, params, 3, &res, &err)) {
          if (db_res_step(res, &err)) {
              int code = db_res_col_i32(res, 0, &err);
              int64_t new_qty = db_res_col_i64(res, 2, &err);
              if (code == 0) {
                  if (new_quantity_out) *new_quantity_out = (int)new_qty;
                  db_res_finalize(res);
                  return 0; // Success
              }
              db_res_finalize(res);
              return code; // Return mapped error from proc
          }
          db_res_finalize(res);
      }
      return err.code;
  }

  // Fallback for SQLite or if proc fails
  // ... existing manual logic ported to db_t ...
  // Note: For brevity in this turn, I'll implement the full refactored logic.
  
  int retry_count;
  for (retry_count = 0; retry_count < 3; retry_count++) {
      if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) {
          if (err.code == ERR_DB_BUSY) { usleep(100000); continue; }
          return err.code;
      }

      int ore=0, org=0, equ=0, colonists=0, slaves=0, weapons=0, drugs=0, holds=0;
      db_res_t *res = NULL;
      const char *sql_sel = "SELECT ore, organics, equipment, colonists, slaves, weapons, drugs, holds FROM ships WHERE id = $1 FOR UPDATE;";
      db_bind_t params_sel[] = { db_bind_i32(ship_id) };
      
      if (!db_query(db, sql_sel, params_sel, 1, &res, &err)) goto rollback;
      if (!db_res_step(res, &err)) { db_res_finalize(res); err.code = ERR_NOT_FOUND; goto rollback; }
      
      ore = db_res_col_i32(res, 0, &err);
      org = db_res_col_i32(res, 1, &err);
      equ = db_res_col_i32(res, 2, &err);
      colonists = db_res_col_i32(res, 3, &err);
      slaves = db_res_col_i32(res, 4, &err);
      weapons = db_res_col_i32(res, 5, &err);
      drugs = db_res_col_i32(res, 6, &err);
      holds = db_res_col_i32(res, 7, &err);
      db_res_finalize(res);

      int current_qty = 0;
      if (strcasecmp (commodity_code, "ORE") == 0) current_qty = ore;
      else if (strcasecmp (commodity_code, "ORG") == 0) current_qty = org;
      else if (strcasecmp (commodity_code, "EQU") == 0) current_qty = equ;
      else if (strcasecmp (commodity_code, "COLONISTS") == 0) current_qty = colonists;
      else if (strcasecmp (commodity_code, "SLAVES") == 0) current_qty = slaves;
      else if (strcasecmp (commodity_code, "WEAPONS") == 0) current_qty = weapons;
      else if (strcasecmp (commodity_code, "DRUGS") == 0) current_qty = drugs;

      int new_qty = current_qty + delta;
      int current_total = ore + org + equ + colonists + slaves + weapons + drugs;
      int new_total = current_total + delta;

      if (new_qty < 0) { err.code = ERR_OUT_OF_RANGE; goto rollback; }
      if (new_total > holds) { err.code = ERR_OUT_OF_RANGE; goto rollback; }

      char sql_upd[128];
      snprintf(sql_upd, sizeof(sql_upd), "UPDATE ships SET %s = $1 WHERE id = $2;", col_name);
      db_bind_t params_upd[] = { db_bind_i32(new_qty), db_bind_i32(ship_id) };
      if (!db_exec(db, sql_upd, params_upd, 2, &err)) goto rollback;

      if (!db_tx_commit(db, &err)) goto rollback;
      
      if (new_quantity_out) *new_quantity_out = new_qty;
      return 0;

rollback:
      db_tx_rollback(db, &err);
      if (err.code == ERR_DB_BUSY) { usleep(100000); continue; }
      return err.code;
  }
  return ERR_DB_BUSY;
}


int
h_get_ship_cargo_and_holds (db_t *db,
                            int ship_id,
                            int *ore,
                            int *organics,
                            int *equipment,
                            int *colonists,
                            int *slaves,
                            int *weapons,
                            int *drugs,
                            int *holds)
{
  if (!db || ship_id <= 0)
    {
      return ERR_DB_MISUSE;
    }

  db_error_t err;
  db_error_clear(&err);

  const char *sql =
    "SELECT ore, organics, equipment, colonists, slaves, weapons, drugs, holds FROM ships WHERE id = $1;";
  db_bind_t params[] = { db_bind_i32(ship_id) };
  db_res_t *res = NULL;

  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          if (ore) *ore = db_res_col_i32 (res, 0, &err);
          if (organics) *organics = db_res_col_i32 (res, 1, &err);
          if (equipment) *equipment = db_res_col_i32 (res, 2, &err);
          if (colonists) *colonists = db_res_col_i32 (res, 3, &err);
          if (slaves) *slaves = db_res_col_i32 (res, 4, &err);
          if (weapons) *weapons = db_res_col_i32 (res, 5, &err);
          if (drugs) *drugs = db_res_col_i32 (res, 6, &err);
          if (holds) *holds = db_res_col_i32 (res, 7, &err);
          db_res_finalize (res);
          return 0; // Success
        }
      db_res_finalize (res);
      return ERR_NOT_FOUND;
    }

  return err.code;
}

