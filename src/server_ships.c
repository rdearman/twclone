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
#include <jansson.h>		/* -ljansson */
#include <stdbool.h>
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
	("handle_ship_destruction: Failed to clear active ship for player %d: %s",
	 ctx->victim_player_id, sqlite3_errmsg (db));
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
	("handle_ship_destruction: Failed to increment times_blown_up for player %d: %s",
	 ctx->victim_player_id, sqlite3_errmsg (db));
      // Continue despite error, as this is non-critical for core destruction flow
    }

  // Apply XP penalty
  int current_xp = db_get_player_xp (db, ctx->victim_player_id);
  int xp_loss =
    g_cfg.death.xp_loss_flat +
    (int) (current_xp * (g_cfg.death.xp_loss_percent / 100.0));
  int new_xp = current_xp - xp_loss;
  if (new_xp < 0)
    new_xp = 0;			// XP cannot go below 0

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
    {				// If it's been a day
      podded_count_today = 0;	// Reset for the new day
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
	    ("handle_ship_destruction: Failed to spawn escape pod for player %d, forcing Big Sleep: %s",
	     ctx->victim_player_id, sqlite3_errmsg (db));
	  handle_big_sleep (db, ctx);	// Fallback to big sleep on escape pod spawn failure
	}
    }
  else
    {
      rc = handle_big_sleep (db, ctx);
      if (rc != SQLITE_OK)
	{
	  LOGE
	    ("handle_ship_destruction: Failed to initiate Big Sleep for player %d: %s",
	     ctx->victim_player_id, sqlite3_errmsg (db));
	  // Critical error, can't recover from this without further action
	  return rc;
	}
    }


  // --- 1.6 Emit Engine Events ---
  json_t *event_payload = json_object ();
  if (!event_payload)
    {
      LOGE
	("handle_ship_destruction: Failed to allocate event_payload for engine event.");
      return SQLITE_NOMEM;	// Memory allocation failure
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
  int pod_sector_id = 1;	// Placeholder: Eventually from compute_pod_destination

  LOGI ("handle_escape_pod_spawn: Player %d spawning in escape pod.",
	ctx->victim_player_id);

  // 1. Create the pod ship
  // Get shiptype ID for Escape Pod (id = 0)
  int escape_pod_shiptype_id = 0;	// Canonical ID for Escape Pod

  // Insert new ship row of type ESCAPE_POD
  const char *sql_insert_pod = "INSERT INTO ships (name, type_id, holds, fighters, shields, sector, ported, onplanet) " "VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, 0);";	// Assume not ported or on planet initially
  rc = sqlite3_prepare_v2 (db, sql_insert_pod, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("handle_escape_pod_spawn: Failed to prepare pod insert: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  // Using canonical Escape Pod stats directly
  sqlite3_bind_text (st, 1, "Escape Pod", -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, escape_pod_shiptype_id);	// type_id 0
  sqlite3_bind_int (st, 3, 5);	// holds
  sqlite3_bind_int (st, 4, 50);	// fighters
  sqlite3_bind_int (st, 5, 50);	// shields
  sqlite3_bind_int (st, 6, pod_sector_id);	// Placeholder sector

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
  const char *sql_insert_ship_ownership = "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES (?1, ?2, 1, 1);";	// role_id 1 = owner, is_primary 1
  rc = sqlite3_prepare_v2 (db, sql_insert_ship_ownership, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
	("handle_escape_pod_spawn: Failed to prepare pod ship_ownership insert: %s",
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
	("handle_escape_pod_spawn: Failed to update player's ship/sector for pod: %s",
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
	("handle_escape_pod_spawn: Failed to update podded_status for player %d: %s",
	 ctx->victim_player_id, sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return rc;
    }
  sqlite3_finalize (st);
  st = NULL;

  LOGI
    ("handle_escape_pod_spawn: Player %d successfully spawned in escape pod %d at sector %d.",
     ctx->victim_player_id, new_pod_ship_id, pod_sector_id);
  return SQLITE_OK;
}


void handle_move_pathfind (client_ctx_t * ctx, json_t * root);

void send_enveloped_ok (int fd, json_t * root, const char *type,
			json_t * data);
void send_enveloped_error (int fd, json_t * root, int code, const char *msg);
void send_enveloped_refused (int fd, json_t * root, int code, const char *msg,
			     json_t * data_opt);


int
cmd_ship_transfer_cargo (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db_handle = db_get_handle ();


  h_decloak_ship (db_handle,
		  h_get_active_ship_id (db_handle, ctx->player_id));
  STUB_NIY (ctx, root, "ship.transfer_cargo");
}


int
cmd_ship_upgrade (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "ship.upgrade");
}

int
cmd_ship_repair (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "ship.repair");
}





/* ship.inspect */


/* ship.inspect */
int
cmd_ship_inspect (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
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
	    sector_id = s;
	}
    }

  json_t *ships = NULL;
  int rc =
    db_ships_inspectable_at_sector_json (ctx->player_id, sector_id, &ships);
  if (rc != SQLITE_OK || !ships)
    {
      send_enveloped_error (ctx->fd, root, 1500, "Database error");
      return 0;
    }

  json_t *payload =
    json_pack ("{s:i s:o}", "sector", sector_id, "ships", ships);
  send_enveloped_ok (ctx->fd, root, "ship.inspect", payload);
  json_decref (payload);
  return 0;
}

/* ship.rename + ship.reregister */
int
cmd_ship_rename (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_refused (ctx->fd, root, 1400, "Bad request", NULL);
      return 0;
    }
  json_t *j_ship = json_object_get (data, "ship_id");
  json_t *j_name = json_object_get (data, "new_name");
  if (!json_is_integer (j_ship) || !json_is_string (j_name))
    {
      send_enveloped_refused (ctx->fd, root, 1400, "Missing ship_id/new_name",
			      NULL);
      return 0;
    }
  int ship_id = (int) json_integer_value (j_ship);
  const char *new_name = json_string_value (j_name);

  int rc = db_ship_rename_if_owner (ctx->player_id, ship_id, new_name);
  if (rc == SQLITE_CONSTRAINT)
    {
      send_enveloped_refused (ctx->fd, root, 1403, "Permission denied", NULL);
      return 0;
    }
  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 1500, "Database error");
      return 0;
    }

  json_t *payload =
    json_pack ("{s:i s:s}", "ship_id", ship_id, "name", new_name);
  send_enveloped_ok (ctx->fd, root, "ship.renamed", payload);
  json_decref (payload);
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
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
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
	    sector_id = s;
	}
    }

  int ship_id = -1;
  if (json_is_object (data))
    {
      json_t *j_ship = json_object_get (data, "ship_id");
      if (json_is_integer (j_ship))
	ship_id = (int) json_integer_value (j_ship);
    }
  if (ship_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1400, "Missing ship_id", NULL);
      return 0;
    }

  json_t *ship = NULL;
  int rc = db_ship_claim (ctx->player_id, sector_id, ship_id, &ship);
  if (rc != SQLITE_OK || !ship)
    {
      send_enveloped_refused (ctx->fd, root, 1406, "Ship not claimable",
			      NULL);
      return 0;
    }

  json_t *payload = json_pack ("{s:o}", "ship", ship);
  send_enveloped_ok (ctx->fd, root, "ship.claimed", payload);
  json_decref (payload);
  return 0;
}

/* ship.status (canonical) */
int
cmd_ship_status (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  json_t *player_info = NULL;
  if (db_player_info_json (ctx->player_id, &player_info) != SQLITE_OK
      || !player_info)
    {
      send_enveloped_error (ctx->fd, root, 1500, "Database error");
      return 0;
    }

  // Extract ship-only view from player_info (adjust keys to match your schema)
  json_t *ship = json_object_get (player_info, "ship");
  if (!json_is_object (ship))
    {
      json_decref (player_info);
      send_enveloped_error (ctx->fd, root, 1500, "No ship info");
      return 0;
    }
  // Build payload (clone or pack fields as needed)
  json_t *payload = json_pack ("{s:O}", "ship", ship);
  send_enveloped_ok (ctx->fd, root, "ship.status", payload);
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
      send_enveloped_refused (ctx->fd, root, ERR_CONFIRMATION_REQUIRED,
			      "Self-destruct requires explicit non-zero integer confirmation.",
			      NULL);
      return -1;
    }

  /* 2. Refusal check: Cannot self-destruct in protected zones (FedSpace) */
  if (db_is_sector_fedspace (ctx->sector_id))
    {
      send_enveloped_refused (ctx->fd, root, ERR_FORBIDDEN_IN_SECTOR,
			      "Cannot self-destruct in protected FedSpace.",
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
    .killer_player_id = ctx->player_id,	// Self-destruct, so killer is also the victim
    .cause = KILL_CAUSE_SELF_DESTRUCT,
    .sector_id = ship_sector > 0 ? ship_sector : 1	// Fallback to sector 1 if current sector is invalid
  };

  int destroy_rc = handle_ship_destruction (db, &kill_ctx);
  if (destroy_rc != SQLITE_OK)
    {
      LOGE
	("cmd_ship_self_destruct: handle_ship_destruction failed for player %d, ship %d: %s",
	 ctx->player_id, ship_id, sqlite3_errmsg (db));
      send_enveloped_error (ctx->fd, root, 1500,
			    "Failed to process self-destruction.");
      return 0;
    }

  /* 4. Response: command acknowledged and processed */
  send_enveloped_ok (ctx->fd, root, "ship.self_destruct.confirmed", NULL);

  return 0;
}

// Helper to get active ship ID for a player
int
h_get_active_ship_id (sqlite3 *db, int player_id)
{
  sqlite3_stmt *stmt;
  int ship_id = 0;
  const char *sql = "SELECT ship FROM players WHERE id = ?;";

  if (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (stmt, 1, player_id);
      if (sqlite3_step (stmt) == SQLITE_ROW)
	{
	  ship_id = sqlite3_column_int (stmt, 0);
	}
    }
  else
    {
      // Log an error if prepare fails
      LOGE ("Failed to prepare statement for h_get_active_ship_id: %s",
	    sqlite3_errmsg (db));
    }
  sqlite3_finalize (stmt);
  return ship_id;
}



