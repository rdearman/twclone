#include <stdatomic.h>
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
#define UUID_STR_LEN 37         // 36 chars + null terminator
#include <sqlite3.h>
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
#include <strings.h>


// Central handler for ship destruction
int
handle_ship_destruction (sqlite3 *db, ship_kill_context_t *ctx)
{
  int rc = SQLITE_OK;
  int current_timestamp = time (NULL);
  LOGI
    ("handle_ship_destruction: Victim Player ID: %d, Ship ID: %d, Cause: %d",
     ctx->victim_player_id, ctx->victim_ship_id, ctx->cause);
  // --- 1.2 Implement Loot Resolution ---
  // (Skipping for now as per design brief - "Loot is NOT part of this implementation." )
  LOGD
    ("handle_ship_destruction: Loot resolution (skipped as per design brief)");
  // --- 1.3 Implement Ship Removal/Detachment ---
  // Mark the destroyed ship as destroyed
  rc = db_mark_ship_destroyed (db, ctx->victim_ship_id);
  if (rc != SQLITE_OK)
    {
      LOGE
        ("handle_ship_destruction: Failed to mark ship %d as destroyed: %s",
	 ctx->victim_ship_id, sqlite3_errmsg (db));
      return rc;
    }
  // Remove ship from player's active ship slot
  rc = db_clear_player_active_ship (db, ctx->victim_player_id);
  if (rc != SQLITE_OK)
    {
      LOGE
	(
	 "handle_ship_destruction: Failed to clear active ship for player %d: %s",
	 ctx->victim_player_id,
	 sqlite3_errmsg (db));
      return rc;
    }
  LOGD ("handle_ship_destruction: Ship %d removed/detached from player %d.",
        ctx->victim_ship_id, ctx->victim_player_id);
  // --- 1.4 Implement Player Stat Update ---
  // Increment times_blown_up
  rc = db_increment_player_stat (db, ctx->victim_player_id, "times_blown_up");
  if (rc != SQLITE_OK)
    {
      LOGW
	(
	 "handle_ship_destruction: Failed to increment times_blown_up for player %d: %s",
	 ctx->victim_player_id,
	 sqlite3_errmsg (db));
      // Continue despite error, as this is non-critical for core destruction flow
    }
  // Apply XP penalty
  int current_xp = db_get_player_xp (db, ctx->victim_player_id);
  int xp_loss =
    g_cfg.death.xp_loss_flat +
    (int) (current_xp * (g_cfg.death.xp_loss_percent / 100.0));
  int new_xp = current_xp - xp_loss;


  if (new_xp < 0)
    {
      new_xp = 0;               // XP cannot go below 0
    }
  rc = db_update_player_xp (db, ctx->victim_player_id, new_xp);
  if (rc != SQLITE_OK)
    {
      LOGW ("handle_ship_destruction: Failed to update XP for player %d: %s",
            ctx->victim_player_id, sqlite3_errmsg (db));
      // Continue despite error
    }
  LOGD
    ("handle_ship_destruction: Player %d XP updated from %d to %d (lost %d).",
     ctx->victim_player_id, current_xp, new_xp, xp_loss);
  // --- 1.5 Implement Escape Pod vs. Big Sleep Decision Logic ---
  // Check if the ship type allows escape pods and if the player is within daily limit
  bool has_escape_pod = db_shiptype_has_escape_pod (db, ctx->victim_ship_id);
  int podded_count_today =
    db_get_player_podded_count_today (db, ctx->victim_player_id);
  // Check if podded_last_reset needs to be reset for a new day
  long long last_reset_timestamp =
    db_get_player_podded_last_reset (db, ctx->victim_player_id);


  if (current_timestamp - last_reset_timestamp >= 86400)
    {                           // If it's been a day
      podded_count_today = 0;   // Reset for the new day
      db_reset_player_podded_count (db, ctx->victim_player_id,
                                    current_timestamp);
    }
  bool can_pod = has_escape_pod
    && (podded_count_today < g_cfg.death.max_per_day);


  if (can_pod)
    {
      rc = handle_escape_pod_spawn (db, ctx);
      if (rc != SQLITE_OK)
        {
          LOGE
	    (
	     "handle_ship_destruction: Failed to spawn escape pod for player %d, forcing Big Sleep: %s",
	     ctx->victim_player_id,
	     sqlite3_errmsg (db));
          handle_big_sleep (db, ctx);   // Fallback to big sleep on escape pod spawn failure
        }
    }
  else
    {
      rc = handle_big_sleep (db, ctx);
      if (rc != SQLITE_OK)
        {
          LOGE
	    (
	     "handle_ship_destruction: Failed to initiate Big Sleep for player %d: %s",
	     ctx->victim_player_id,
	     sqlite3_errmsg (db));
          // Critical error, can't recover from this without further action
          return rc;
        }
    }
  // --- 1.6 Emit Engine Events ---
  json_t *event_payload = json_object ();


  if (!event_payload)
    {
      LOGE
	(
	 "handle_ship_destruction: Failed to allocate event_payload for engine event.");
      return SQLITE_NOMEM;      // Memory allocation failure
    }
  json_object_set_new (event_payload, "victim_ship_id",
                       json_integer (ctx->victim_ship_id));
  json_object_set_new (event_payload, "victim_player_id",
                       json_integer (ctx->victim_player_id));
  json_object_set_new (event_payload, "killer_player_id",
                       json_integer (ctx->killer_player_id));
  json_object_set_new (event_payload, "cause",
                       json_string (ctx->cause ==
                                    KILL_CAUSE_COMBAT ? "combat" : ctx->cause
                                    ==
                                    KILL_CAUSE_MINES ? "mines" : ctx->cause ==
                                    KILL_CAUSE_QUASAR ? "quasar" : ctx->cause
                                    ==
                                    KILL_CAUSE_NAVHAZ ? "navhaz" : ctx->cause
                                    ==
                                    KILL_CAUSE_SELF_DESTRUCT ? "self_destruct"
                                    : "other"));
  json_object_set_new (event_payload, "sector_id",
                       json_integer (ctx->sector_id));
  rc =
    db_log_engine_event ((long long) current_timestamp, "ship.destroyed",
                         "system", 0, ctx->sector_id, event_payload, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("handle_ship_destruction: Failed to log ship.destroyed event: %s",
            sqlite3_errmsg (db));
      // Not a critical error to stop the process, but worth logging
    }
  return SQLITE_OK;
}


// Implements Phase 2.1: Apply Big Sleep status
int
handle_big_sleep (sqlite3 *db, ship_kill_context_t *ctx)
{
  int rc;
  sqlite3_stmt *st = NULL;
  long long big_sleep_until_ts =
    time (NULL) + g_cfg.death.big_sleep_duration_seconds;
  LOGI ("handle_big_sleep: Player %d entering Big Sleep until %lld.",
        ctx->victim_player_id, big_sleep_until_ts);
  // Update podded_status table
  const char *sql_update_podded_status =
    "INSERT OR REPLACE INTO podded_status (player_id, status, big_sleep_until) "
    "VALUES (?1, 'big_sleep', ?2);";


  rc = sqlite3_prepare_v2 (db, sql_update_podded_status, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("handle_big_sleep: Failed to prepare podded_status update: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, ctx->victim_player_id);
  sqlite3_bind_int64 (st, 2, big_sleep_until_ts);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE
        ("handle_big_sleep: Failed to update podded_status for player %d: %s",
	 ctx->victim_player_id, sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return rc;
    }
  sqlite3_finalize (st);
  // Emit event
  json_t *event_payload = json_object ();


  json_object_set_new (event_payload, "player_id",
                       json_integer (ctx->victim_player_id));
  json_object_set_new (event_payload, "until",
                       json_integer (big_sleep_until_ts));
  db_log_engine_event (time (NULL), "player.big_sleep_started", "system",
                       ctx->victim_player_id, 0, event_payload, NULL);
  return SQLITE_OK;
}


// Implements Phase 3: Escape Pod Spawn Behaviour
int
handle_escape_pod_spawn (sqlite3 *db, ship_kill_context_t *ctx)
{
  int rc;
  sqlite3_stmt *st = NULL;
  int new_pod_ship_id = 0;
  int pod_sector_id = 1;        // Placeholder: Eventually from compute_pod_destination
  LOGI ("handle_escape_pod_spawn: Player %d spawning in escape pod.",
        ctx->victim_player_id);
  // 1. Create the pod ship
  // Get shiptype ID for Escape Pod (id = 0)
  int escape_pod_shiptype_id = 0;       // Canonical ID for Escape Pod
  // Insert new ship row of type ESCAPE_POD
  const char *sql_insert_pod =
    "INSERT INTO ships (name, type_id, holds, fighters, shields, sector, ported, onplanet) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, 0);";                                                                                                                           // Assume not ported or on planet initially


  rc = sqlite3_prepare_v2 (db, sql_insert_pod, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("handle_escape_pod_spawn: Failed to prepare pod insert: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  // Using canonical Escape Pod stats directly
  sqlite3_bind_text (st, 1, "Escape Pod", -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, escape_pod_shiptype_id);     // type_id 0
  sqlite3_bind_int (st, 3, 5);  // holds
  sqlite3_bind_int (st, 4, 50); // fighters
  sqlite3_bind_int (st, 5, 50); // shields
  sqlite3_bind_int (st, 6, pod_sector_id);      // Placeholder sector
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE ("handle_escape_pod_spawn: Failed to insert new escape pod: %s",
            sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return rc;
    }
  new_pod_ship_id = sqlite3_last_insert_rowid (db);
  sqlite3_finalize (st);
  st = NULL;
  // 2. Assign ship ownership via ship_ownership table
  const char *sql_insert_ship_ownership =
    "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES (?1, ?2, 1, 1);";


  // role_id 1 = owner, is_primary 1
  rc = sqlite3_prepare_v2 (db, sql_insert_ship_ownership, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
	(
	 "handle_escape_pod_spawn: Failed to prepare pod ship_ownership insert: %s",
	 sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, new_pod_ship_id);
  sqlite3_bind_int (st, 2, ctx->victim_player_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE
        ("handle_escape_pod_spawn: Failed to insert pod ship_ownership: %s",
	 sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return rc;
    }
  sqlite3_finalize (st);
  st = NULL;
  // 3. Update player's active ship and sector in the players table
  const char *sql_update_player =
    "UPDATE players SET ship = ?, sector = ? WHERE id = ?;";


  rc = sqlite3_prepare_v2 (db, sql_update_player, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("handle_escape_pod_spawn: Failed to prepare player update: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, new_pod_ship_id);
  sqlite3_bind_int (st, 2, pod_sector_id);
  sqlite3_bind_int (st, 3, ctx->victim_player_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE
	(
	 "handle_escape_pod_spawn: Failed to update player's ship/sector for pod: %s",
	 sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return rc;
    }
  sqlite3_finalize (st);
  st = NULL;
  // 4. Update podded_status table
  const char *sql_update_podded_status =
    "INSERT OR REPLACE INTO podded_status (player_id, podded_count_today, podded_last_reset, status, big_sleep_until) "
    "VALUES (?1, (SELECT podded_count_today FROM podded_status WHERE player_id = ?1) + 1, (SELECT podded_last_reset FROM podded_status WHERE player_id = ?1), 'alive', 0);";


  rc = sqlite3_prepare_v2 (db, sql_update_podded_status, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
        ("handle_escape_pod_spawn: Failed to prepare podded_status update: %s",
	 sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, ctx->victim_player_id);
  sqlite3_bind_int (st, 2, ctx->victim_player_id);
  sqlite3_bind_int (st, 3, ctx->victim_player_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE
	(
	 "handle_escape_pod_spawn: Failed to update podded_status for player %d: %s",
	 ctx->victim_player_id,
	 sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return rc;
    }
  sqlite3_finalize (st);
  st = NULL;
  LOGI
    (
     "handle_escape_pod_spawn: Player %d successfully spawned in escape pod %d at sector %d.",
     ctx->victim_player_id,
     new_pod_ship_id,
     pod_sector_id);
  return SQLITE_OK;
}


void handle_move_pathfind (client_ctx_t *ctx, json_t *root);





int
cmd_ship_transfer_cargo (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db_handle = db_get_handle ();
  h_decloak_ship (db_handle,
                  h_get_active_ship_id (db_handle, ctx->player_id));
  send_response_error(ctx, root, ERR_NOT_IMPLEMENTED, "Not implemented: " "ship.transfer_cargo");
  return 0;
}


int
cmd_ship_upgrade (client_ctx_t *ctx, json_t *root)
{
  send_response_error(ctx, root, ERR_NOT_IMPLEMENTED, "Use hardware.buy for components or shipyard.upgrade for hull exchange.");
  return 0;
}


int
cmd_ship_repair (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  if (ctx->player_id <= 0)
    {
      send_response_refused (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required", NULL);
      return 0;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship.");
      return 0;
    }

  /* Check Location (Stardock type 9 or Class 0 type 0) */
  sqlite3_stmt *st = NULL;
  const char *sql_loc =
    "SELECT type FROM ports WHERE sector = ? AND (type = 9 OR type = 0);";
  bool at_shipyard = false;
  if (sqlite3_prepare_v2 (db, sql_loc, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, ctx->sector_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          at_shipyard = true;
        }
      sqlite3_finalize (st);
    }

  if (!at_shipyard)
    {
      send_response_refused (ctx, root, ERR_NOT_AT_SHIPYARD,
                             "Must be at Stardock or Class 0 port.", NULL);
      return 0;
    }

  /* Check Hull */
  int current_hull = 0;
  if (sqlite3_prepare_v2 (db, "SELECT hull FROM ships WHERE id = ?", -1, &st,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, ship_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          current_hull = sqlite3_column_int (st, 0);
        }
      sqlite3_finalize (st);
    }

  if (current_hull >= 100)
    {
      json_t *res =
        json_pack ("{s:b, s:i, s:i}", "repaired", 0, "cost", 0, "hull", 100);
      send_response_ok (ctx, root, "ship.repair", res);
      //json_decref (res);
      return 0;
    }

  int to_repair = 100 - current_hull;
  int cost = to_repair * 10;    /* 10 credits per point */

  /* Transaction context is managed by the caller/server loop. DO NOT BEGIN/COMMIT/ROLLBACK here. */

  /* 1. Deduct credits */
  long long new_player_credits;
  (void) new_player_credits;
    new_player_credits =0;
  
  if (sqlite3_prepare_v2(db, "UPDATE players SET credits = credits - ?1 WHERE id = ?2 AND credits >= ?1 RETURNING credits;", -1, &st, NULL) != SQLITE_OK) {
      send_response_error (ctx, root, ERR_DB, "DB Error preparing credit deduction.");
      return 0;
  }
  sqlite3_bind_int(st, 1, cost);
  sqlite3_bind_int(st, 2, ctx->player_id);
  
  int step_rc = sqlite3_step(st);
  
  if (step_rc == SQLITE_ROW) {
      new_player_credits = sqlite3_column_int64(st, 0);
  } else {
      sqlite3_finalize(st);
      if (step_rc == SQLITE_DONE) {
          /* UPDATE executed but no rows returned -> condition failed (insufficient funds) */
          send_response_refused (ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits.", NULL);
      } else {
          send_response_error (ctx, root, ERR_DB, "Credit update failed.");
      }
      return 0;
  }
  sqlite3_finalize(st);

  /* 2. Update Hull */
  if (sqlite3_prepare_v2
      (db, "UPDATE ships SET hull = 100 WHERE id = ?", -1, &st,
       NULL) != SQLITE_OK)
    {
      /* Refund attempt */
      h_add_player_petty_cash_unlocked (db, ctx->player_id, cost, NULL);
      send_response_error (ctx, root, ERR_DB, "DB Error preparing hull update");
      return 0;
    }
  sqlite3_bind_int (st, 1, ship_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      sqlite3_finalize (st);
      /* Refund attempt */
      h_add_player_petty_cash_unlocked (db, ctx->player_id, cost, NULL);
      send_response_error (ctx, root, ERR_DB, "Hull update step failed");
      return 0;
    }
  
  if (sqlite3_changes(db) == 0) {
      sqlite3_finalize (st);
      /* Refund attempt */
      h_add_player_petty_cash_unlocked (db, ctx->player_id, cost, NULL);
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "Ship not found or update failed.");
      return 0;
  }
  sqlite3_finalize (st);

  /* No COMMIT */

  json_t *res =
    json_pack ("{s:b, s:i, s:i}", "repaired", 1, "cost", cost, "hull", 100);
  send_response_ok (ctx, root, "ship.repair", res);
  //json_decref (res);
  return 0;
}


/* ship.inspect */


/* ship.inspect */
int
cmd_ship_inspect (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused(ctx, root, ERR_SECTOR_NOT_FOUND, "Not authenticated", NULL);
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
  json_t *ships = NULL;
  int rc =
    db_ships_inspectable_at_sector_json (ctx->player_id, sector_id, &ships);


  if (rc != SQLITE_OK || !ships)
    {
      send_response_error(ctx, root, ERR_PLANET_NOT_FOUND, "Database error");
      return 0;
    }
  json_t *payload =
    json_pack ("{s:i s:o}", "sector", sector_id, "ships", ships);


  send_response_ok(ctx, root, "ship.inspect", payload);
  //json_decref (payload);
  return 0;
}


/* ship.rename + ship.reregister */
int
cmd_ship_rename (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused(ctx, root, ERR_SECTOR_NOT_FOUND, "Not authenticated", NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_refused(ctx, root, REF_NOT_IN_SECTOR, "Bad request", NULL);
      return 0;
    }
  json_t *j_ship = json_object_get (data, "ship_id");
  json_t *j_name = json_object_get (data, "new_name");


  if (!json_is_integer (j_ship) || !json_is_string (j_name))
    {
      send_response_refused(ctx, root, REF_NOT_IN_SECTOR, "Missing ship_id/new_name",  NULL);
      return 0;
    }
  int ship_id = (int) json_integer_value (j_ship);
  const char *new_name = json_string_value (j_name);
  sqlite3 *db = db_get_handle ();
  int rc = db_ship_rename_if_owner (db, ctx->player_id, ship_id, new_name);


  if (rc == SQLITE_CONSTRAINT)
    {
      send_response_refused(ctx, root, REF_TURN_COST_EXCEEDS, "Permission denied", NULL);
      return 0;
    }
  if (rc != SQLITE_OK)
    {
      send_response_error(ctx, root, ERR_PLANET_NOT_FOUND, "Database error");
      return 0;
    }
  json_t *payload =
    json_pack ("{s:i s:s}", "ship_id", ship_id, "name", new_name);


  send_response_ok(ctx, root, "ship.renamed", payload);
  //json_decref (payload);
  return 0;
}


/* ship.claim */
int
cmd_ship_claim (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db_handle = db_get_handle ();
  h_decloak_ship (db_handle,
                  h_get_active_ship_id (db_handle, ctx->player_id));
  if (ctx->player_id <= 0)
    {
      send_response_refused(ctx, root, ERR_SECTOR_NOT_FOUND, "Not authenticated", NULL);
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
      send_response_refused(ctx, root, REF_NOT_IN_SECTOR, "Missing ship_id", NULL);
      return 0;
    }
  json_t *ship = NULL;
  int rc = db_ship_claim (db_handle, ctx->player_id, sector_id, ship_id, &ship);


  if (rc != SQLITE_OK || !ship)
    {
      send_response_refused(ctx, root, REF_SAFE_ZONE_ONLY, "Ship not claimable",  NULL);
      return 0;
    }
  json_t *payload = json_pack ("{s:o}", "ship", ship);


  send_response_ok(ctx, root, "ship.claimed", payload);
  //json_decref (payload);
  return 0;
}


/* ship.status (canonical) */
int
cmd_ship_status (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused(ctx, root, ERR_SECTOR_NOT_FOUND, "Not authenticated", NULL);
      return 0;
    }
  json_t *player_info = NULL;


  if (db_player_info_json (ctx->player_id, &player_info) != SQLITE_OK
      || !player_info)
    {
      send_response_error(ctx, root, ERR_PLANET_NOT_FOUND, "Database error");
      return 0;
    }
  // Extract ship-only view from player_info (adjust keys to match your schema)
  json_t *ship = json_object_get (player_info, "ship");


  if (!json_is_object (ship))
    {
      json_decref (player_info);
      send_response_error(ctx, root, ERR_PLANET_NOT_FOUND, "No ship info");
      return 0;
    }
  // Build payload (clone or pack fields as needed)
  json_t *payload = json_pack ("{s:O}", "ship", ship);


  send_response_ok(ctx, root, "ship.status", payload);
  json_decref (payload);
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
      send_response_refused(ctx, root, ERR_CONFIRMATION_REQUIRED, "Self-destruct requires explicit non-zero integer confirmation.",
                              NULL);
      return -1;
    }
  /* 2. Refusal check: Cannot self-destruct in protected zones (FedSpace) */
  if (db_is_sector_fedspace (ctx->sector_id))
    {
      send_response_refused(ctx, root, ERR_FORBIDDEN_IN_SECTOR, "Cannot self-destruct in protected FedSpace.",
                              NULL);
      return -1;
    }
  /* 3. Invoke handle_ship_destruction for self-destruct logic */
  sqlite3 *db = db_get_handle ();
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


  if (destroy_rc != SQLITE_OK)
    {
      LOGE
	(
	 "cmd_ship_self_destruct: handle_ship_destruction failed for player %d, ship %d: %s",
	 ctx->player_id,
	 ship_id,
	 sqlite3_errmsg (db));
      send_response_error(ctx, root, ERR_PLANET_NOT_FOUND, "Failed to process self-destruction.");
      return 0;
    }
  /* 4. Response: command acknowledged and processed */
  send_response_ok(ctx, root, "ship.self_destruct.confirmed", NULL);
  return 0;
}


int
cmd_ship_tow (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      send_response_error(ctx, root, ERR_DB, "No database handle");
      return 0;
    }

  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused(ctx, root, ERR_NOT_AUTHENTICATED, "Not authenticated",
                              NULL);
      return 0;
    }


  int player_ship_id = h_get_active_ship_id (db,
                                             ctx->player_id);
  
  if (player_ship_id <= 0)
    {
      send_response_refused(ctx, root, ERR_NO_ACTIVE_SHIP, "You do not have an active ship.",
			      NULL);
      return 0;
    }

  /* -----------------------------------------------------------
     1. Get Player's Current Tow Status
     ----------------------------------------------------------- */
  int current_towing_ship_id = 0;
  sqlite3_stmt *stmt = NULL;
  const char *sql_get_tow = "SELECT towing_ship_id FROM ships WHERE id = ?;";


  if (sqlite3_prepare_v2 (db, sql_get_tow, -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (stmt, 1, player_ship_id);
      if (sqlite3_step (stmt) == SQLITE_ROW)
        {
          current_towing_ship_id = sqlite3_column_int (stmt, 0);
        }
      sqlite3_finalize (stmt);
    }
  else
    {
      LOGE ("cmd_ship_tow: DB error checking status: %s", sqlite3_errmsg (db));
      send_response_error(ctx, root, ERR_DB_QUERY_FAILED, "Database error");
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
          sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);

          // 3a. Clear player's towing field
          const char *sql_untow_player =
            "UPDATE ships SET towing_ship_id = 0 WHERE id = ?;";


          if (sqlite3_prepare_v2 (db, sql_untow_player, -1, &stmt,
                                  NULL) != SQLITE_OK)
            {
              goto rollback;
            }
          sqlite3_bind_int (stmt, 1, player_ship_id);
          if (sqlite3_step (stmt) != SQLITE_DONE)
            {
              goto rollback;
            }
          sqlite3_finalize (stmt);

          // 3b. Clear target's "being towed" field
          const char *sql_untow_target =
            "UPDATE ships SET is_being_towed_by = 0 WHERE id = ?;";


          if (sqlite3_prepare_v2 (db, sql_untow_target, -1, &stmt,
                                  NULL) != SQLITE_OK)
            {
              goto rollback;
            }
          sqlite3_bind_int (stmt, 1, current_towing_ship_id);
          if (sqlite3_step (stmt) != SQLITE_DONE)
            {
              goto rollback;
            }
          sqlite3_finalize (stmt);

          sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);

          send_response_ok(ctx, root, "ship.tow.disengaged", json_pack ("{s:s,s:i}",
                                        "message",
                                        "Tow cable disengaged.",
                                        "towed_ship_id",
                                        current_towing_ship_id));
          return 0;
        }
      else
        {
          send_response_refused(ctx, root, REF_ALREADY_TOWING, "You are already towing another ship. Release it first.",
                                  NULL);
          return 0;
        }
    }

  /* -----------------------------------------------------------
     4. ENGAGE LOGIC - Prerequisites
     ----------------------------------------------------------- */
  if (target_ship_id <= 0)
    {
      send_response_refused(ctx, root, ERR_MISSING_FIELD, "Invalid target ship ID.",
                              NULL);
      return 0;
    }
  if (target_ship_id == player_ship_id)
    {
      send_response_refused(ctx, root, REF_TARGET_SHIP_INVALID, "You cannot tow yourself.",
                              NULL);
      return 0;
    }

  // Helper lookups (Assuming these helpers exist and work as expected)
  int target_sector = db_get_ship_sector_id (db, target_ship_id);
  int player_sector = db_get_ship_sector_id (db, player_ship_id);


  if (target_sector <= 0 || target_sector != player_sector)
    {
      send_response_refused(ctx, root, REF_TARGET_SHIP_INVALID, "Target ship not in your sector.",
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
      send_response_refused(ctx, root, REF_SHIP_NOT_OWNED_OR_PILOTED, "You do not own that ship.",
                              NULL);
      return 0;
    }

  // Unmanned Check
  if (db_is_ship_piloted (db, target_ship_id))
    {
      send_response_refused(ctx, root, REF_SHIP_NOT_OWNED_OR_PILOTED, "Target ship is currently piloted.",
                              NULL);
      return 0;
    }

  // Already Towed Check
  const char *sql_check_towed =
    "SELECT is_being_towed_by FROM ships WHERE id = ?;";
  int is_being_towed = 0;


  if (sqlite3_prepare_v2 (db, sql_check_towed, -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (stmt, 1, target_ship_id);
      if (sqlite3_step (stmt) == SQLITE_ROW)
        {
          is_being_towed = sqlite3_column_int (stmt, 0);
        }
      sqlite3_finalize (stmt);
    }

  if (is_being_towed != 0)
    {
      send_response_refused(ctx, root, REF_ALREADY_BEING_TOWED, "Target is already under tow.",
                              NULL);
      return 0;
    }

  /* -----------------------------------------------------------
     5. ENGAGE LOGIC - Execution
     ----------------------------------------------------------- */
  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);

  // 5a. Set player's towing_ship_id -> target
  const char *sql_set_tow = "UPDATE ships SET towing_ship_id = ? WHERE id = ?;";


  if (sqlite3_prepare_v2 (db, sql_set_tow, -1, &stmt, NULL) != SQLITE_OK)
    {
      goto rollback;
    }
  sqlite3_bind_int (stmt, 1, target_ship_id);
  sqlite3_bind_int (stmt, 2, player_ship_id);
  if (sqlite3_step (stmt) != SQLITE_DONE)
    {
      goto rollback;
    }
  sqlite3_finalize (stmt);

  // 5b. Set target's is_being_towed_by -> player
  const char *sql_set_towed_by =
    "UPDATE ships SET is_being_towed_by = ? WHERE id = ?;";


  if (sqlite3_prepare_v2 (db, sql_set_towed_by, -1, &stmt, NULL) != SQLITE_OK)
    {
      goto rollback;
    }
  sqlite3_bind_int (stmt, 1, player_ship_id);
  sqlite3_bind_int (stmt, 2, target_ship_id);
  if (sqlite3_step (stmt) != SQLITE_DONE)
    {
      goto rollback;
    }
  sqlite3_finalize (stmt);

  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);

  send_response_ok(ctx, root, "ship.tow.engaged", json_pack ("{s:s,s:i}",
                                "message",
                                "Tow cable engaged.",
                                "towed_ship_id",
                                target_ship_id));
  return 0;

 rollback:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
  LOGE ("cmd_ship_tow: DB Transaction failed: %s", sqlite3_errmsg (db));
  send_response_error(ctx, root, ERR_DB_QUERY_FAILED, "Database transaction error");
  return 0;
}


// Helper to get active ship ID for a player
int
h_get_active_ship_id (sqlite3 *db, int player_id)
{
  LOGE("DEBUG: h_get_active_ship_id called for player_id=%d", player_id); // NEW
  sqlite3_stmt *stmt = NULL; // Initialize to NULL for safety
  int ship_id = 0;
  const char *sql = "SELECT ship FROM players WHERE id = ?;";
  if (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (stmt, 1, player_id);
      if (sqlite3_step (stmt) == SQLITE_ROW)
        {
          ship_id = sqlite3_column_int (stmt, 0);
        }
      sqlite3_finalize (stmt); // Finalize here if prepare was OK
    }
  else
    {
      // Log an error if prepare fails
      LOGE ("Failed to prepare statement for h_get_active_ship_id: %s",
            sqlite3_errmsg (db));
      // No finalize needed if prepare failed, as stmt is NULL or invalid
    }
  return ship_id; // Always return ship_id at the end of the function
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
h_update_ship_cargo (sqlite3 *db, int ship_id, const char *commodity_code, int delta, int *new_quantity_out)
{
  if (!db || ship_id <= 0 || !commodity_code)
    {
      if (new_quantity_out) *new_quantity_out = 0; // Indicate no change
      return SQLITE_MISUSE;
    }

  // Determine column name based on commodity code
  const char *col_name = NULL;
  if (strcasecmp(commodity_code, "ORE") == 0) col_name = "ore";
  else if (strcasecmp(commodity_code, "ORG") == 0) col_name = "organics";
  else if (strcasecmp(commodity_code, "EQU") == 0) col_name = "equipment";
  else if (strcasecmp(commodity_code, "COLONISTS") == 0) col_name = "colonists";
  else if (strcasecmp(commodity_code, "SLAVES") == 0) col_name = "slaves";
  else if (strcasecmp(commodity_code, "WEAPONS") == 0) col_name = "weapons";
  else if (strcasecmp(commodity_code, "DRUGS") == 0) col_name = "drugs";
  else {
    LOGE("h_update_ship_cargo: Invalid commodity code %s for ship %d", commodity_code, ship_id);
    if (new_quantity_out) *new_quantity_out = 0; // Indicate no change
    return SQLITE_MISUSE;
  }

  sqlite3_stmt *stmt = NULL;
  // Check current values and capacity (including all cargo types)
  const char *sql_check = "SELECT ore, organics, equipment, colonists, slaves, weapons, drugs, holds FROM ships WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    LOGE("h_update_ship_cargo: Failed to prepare check statement: %s", sqlite3_errmsg(db));
    if (new_quantity_out) *new_quantity_out = 0;
    return rc;
  }

  sqlite3_bind_int(stmt, 1, ship_id);
  
  int ore = 0, org = 0, equ = 0, holds = 0, colonists = 0, slaves = 0, weapons = 0, drugs = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    ore = sqlite3_column_int(stmt, 0);
    org = sqlite3_column_int(stmt, 1);
    equ = sqlite3_column_int(stmt, 2);
    colonists = sqlite3_column_int(stmt, 3);
    slaves = sqlite3_column_int(stmt, 4);
    weapons = sqlite3_column_int(stmt, 5);
    drugs = sqlite3_column_int(stmt, 6);
    holds = sqlite3_column_int(stmt, 7);
  } else {
    sqlite3_finalize(stmt);
    LOGE("h_update_ship_cargo: Ship %d not found for cargo update.", ship_id);
    if (new_quantity_out) *new_quantity_out = 0;
    return SQLITE_NOTFOUND;
  }
  sqlite3_finalize(stmt); // Finalize the SELECT statement

  int current_qty_for_commodity = 0;
  if (strcasecmp(commodity_code, "ORE") == 0) current_qty_for_commodity = ore;
  else if (strcasecmp(commodity_code, "ORG") == 0) current_qty_for_commodity = org;
  else if (strcasecmp(commodity_code, "EQU") == 0) current_qty_for_commodity = equ;
  else if (strcasecmp(commodity_code, "COLONISTS") == 0) current_qty_for_commodity = colonists;
  else if (strcasecmp(commodity_code, "SLAVES") == 0) current_qty_for_commodity = slaves;
  else if (strcasecmp(commodity_code, "WEAPONS") == 0) current_qty_for_commodity = weapons;
  else if (strcasecmp(commodity_code, "DRUGS") == 0) current_qty_for_commodity = drugs;
  // Note: An invalid commodity code would have returned SQLITE_MISUSE earlier.

  int new_qty_for_commodity = current_qty_for_commodity + delta;
  
  // Calculate current total cargo load from all relevant commodities
  int current_total_load = ore + org + equ + colonists + slaves + weapons + drugs;
  // Calculate new total cargo load assuming the change goes through
  int new_total_load = current_total_load - current_qty_for_commodity + new_qty_for_commodity;

  // Invariant 1: No negative quantities for any specific commodity
  if (new_qty_for_commodity < 0) {
    LOGW("h_update_ship_cargo: Resulting quantity negative for ship %d, commodity %s (curr=%d, delta=%d)", 
         ship_id, commodity_code, current_qty_for_commodity, delta);
    if (new_quantity_out) *new_quantity_out = current_qty_for_commodity; // Return current_qty if clamped
    return SQLITE_CONSTRAINT; // Indicate violation
  }

  // Invariant 2: Total cargo <= holds (max capacity)
  if (new_total_load > holds) {
    LOGW("h_update_ship_cargo: Ship %d cargo capacity exceeded (holds=%d, new_total_load=%d)", ship_id, holds, new_total_load);
    if (new_quantity_out) *new_quantity_out = current_qty_for_commodity; // Return current_qty if clamped
    return SQLITE_CONSTRAINT; // Indicate violation
  }

  // Perform Update
  char dynamic_sql[512]; // Increased size for longer column names
  snprintf(dynamic_sql, sizeof(dynamic_sql), 
           "UPDATE ships SET %s = ? WHERE id = ?;", col_name);
  
  rc = sqlite3_prepare_v2(db, dynamic_sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    LOGE("h_update_ship_cargo: Failed to prepare update statement: %s", sqlite3_errmsg(db));
    if (new_quantity_out) *new_quantity_out = current_qty_for_commodity;
    return rc;
  }

  sqlite3_bind_int(stmt, 1, new_qty_for_commodity);
  sqlite3_bind_int(stmt, 2, ship_id);
  
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt); // Finalize the UPDATE statement
  
  if (rc != SQLITE_DONE) {
    LOGE("h_update_ship_cargo: Update failed for ship %d, commodity %s: %s", ship_id, commodity_code, sqlite3_errmsg(db));
    if (new_quantity_out) *new_quantity_out = current_qty_for_commodity;
    return SQLITE_ERROR; // Indicate update failure
  }

  if (new_quantity_out) {
    *new_quantity_out = new_qty_for_commodity;
  }

  return SQLITE_OK;
}



int h_get_ship_cargo_and_holds(sqlite3 *db, int ship_id, 
                               int *ore, int *organics, int *equipment, 
                               int *colonists, int *slaves, int *weapons, int *drugs,
                               int *holds) {
  if (!db || ship_id <= 0) {
    return SQLITE_MISUSE;
  }

  sqlite3_stmt *stmt = NULL;
  const char *sql = "SELECT ore, organics, equipment, colonists, slaves, weapons, drugs, holds FROM ships WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    LOGE("h_get_ship_cargo_and_holds: Failed to prepare statement: %s", sqlite3_errmsg(db));
    return rc;
  }

  sqlite3_bind_int(stmt, 1, ship_id);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    if (ore) *ore = sqlite3_column_int(stmt, 0);
    if (organics) *organics = sqlite3_column_int(stmt, 1);
    if (equipment) *equipment = sqlite3_column_int(stmt, 2);
    if (colonists) *colonists = sqlite3_column_int(stmt, 3);
    if (slaves) *slaves = sqlite3_column_int(stmt, 4);
    if (weapons) *weapons = sqlite3_column_int(stmt, 5);
    if (drugs) *drugs = sqlite3_column_int(stmt, 6);
    if (holds) *holds = sqlite3_column_int(stmt, 7);
  } else {
    LOGW("h_get_ship_cargo_and_holds: Ship ID %d not found.", ship_id);
    rc = SQLITE_NOTFOUND;
  }

  sqlite3_finalize(stmt);
  return rc;
}
