#include "db/repo/repo_ships.h"
#include <strings.h>
#include <libpq-fe.h>
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
#include <uuid/uuid.h>		// For UUID generation
/* local includes */
#include "schemas.h"
#include "server_cmds.h"
#include "server_loop.h"
#include "server_ships.h"
#include "game_db.h"
#include "server_config.h"
#include "repo_cmd.h"
#include "errors.h"
#include "config.h"
#include "common.h"
#include "server_envelope.h"
#include "server_rules.h"
#include "server_players.h"
#include "server_log.h"
#include "server_ports.h"
#include "db/db_api.h"
#include "db/sql_driver.h"


#define UUID_STR_LEN 37		// 36 chars + null terminator


/* map enum → text for DB */


static const char *
kill_cause_to_text (ship_kill_cause_t cause)
{
  switch (cause)


    {
    case KILL_CAUSE_COMBAT:
      return "combat";


    case KILL_CAUSE_MINES:
      return "mines";


    case KILL_CAUSE_QUASAR:
      return "quasar";


    case KILL_CAUSE_NAVHAZ:
      return "navhaz";


    case KILL_CAUSE_SELF_DESTRUCT:
      return "self_destruct";


    default:
      return "other";
    }
}


/*


 * PostgreSQL-backed ship destruction handler
 * Replaces old SQLite implementation.
 *
 * Side-effects only:
 *   - ship destroyed
 *   - player ship detached / escape pod spawned
 *   - XP penalty applied
 *   - times_blown_up incremented
 *   - podded_status updated
 *   - engine_events row written
 */
int
handle_ship_destruction (db_t *db, ship_kill_context_t *ctx)
{
  if (!db || !ctx)
    {
      return -1;
    }

  const long long now_ts = (long long) time (NULL);


  LOGI ("handle_ship_destruction: victim_player=%d ship=%d cause=%d",
	ctx->victim_player_id, ctx->victim_ship_id, ctx->cause);

  const char *cause_text = kill_cause_to_text (ctx->cause);

  int rc = -1;
  int32_t result_code = -1;
  if (repo_ships_handle_destruction (db,
				     ctx->victim_player_id,
				     ctx->victim_ship_id,
				     ctx->killer_player_id,
				     cause_text,
				     ctx->sector_id,
				     g_cfg.death.xp_loss_flat,
				     g_cfg.death.xp_loss_percent,
				     g_cfg.death.max_per_day,
				     g_cfg.death.big_sleep_duration_seconds,
				     now_ts, &result_code) == 0)
    {
      rc = (int) result_code;
    }
  else
    {
      LOGE ("handle_ship_destruction: Repository error");
      return -1;
    }

  if (rc != 0)
    {
      LOGE ("handle_ship_destruction: DB returned error code %d", rc);
    }
  return rc;
}


int
cmd_ship_transfer_cargo (client_ctx_t *ctx, json_t *root)
{
  db_t *db_handle = game_db_get_handle ();
  h_decloak_ship (db_handle,
		  h_get_active_ship_id (db_handle, ctx->player_id));
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
  db_t *db = game_db_get_handle ();

  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_NOT_AUTHENTICATED,
				   "Auth required", NULL);
      return 0;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship.");
      return 0;
    }

  bool at_shipyard = false;
  int rc = db_port_is_shipyard (db, ctx->sector_id, &at_shipyard);


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB, "DB error checking location.");
      return 0;
    }
  if (!at_shipyard)
    {
      send_response_refused_steal (ctx, root, ERR_NOT_AT_SHIPYARD,
				   "Must be at Stardock or Class 0 port.",
				   NULL);
      return 0;
    }

  int current_hull = 0;


  rc = db_ship_get_hull (db, ship_id, &current_hull);
  if (rc == ERR_SHIP_NOT_FOUND)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "Ship not found.");
      return 0;
    }
  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB, "DB error reading hull.");
      return 0;
    }

  if (current_hull >= 100)
    {
      json_t *res = json_object ();


      json_object_set_new (res, "repaired", json_boolean (0));
      json_object_set_new (res, "cost", json_integer (0));
      json_object_set_new (res, "hull", json_integer (100));
      send_response_ok_take (ctx, root, "ship.repair", &res);
      return 0;
    }

  int to_repair = 100 - current_hull;
  int cost = to_repair * 10;

  long long new_player_credits = 0;
  db_error_t err;

  bool ok = db_ship_repair_atomic (db,
				   ctx->player_id,
				   ship_id,
				   cost,
				   (int64_t *) & new_player_credits,
				   &err);


  if (!ok)
    {
      if (err.code == ERR_INSUFFICIENT_FUNDS)
	{
	  send_response_refused_steal (ctx, root, ERR_INSUFFICIENT_FUNDS,
				       "Insufficient credits.", NULL);
	  return 0;
	}
      if (err.code == ERR_SHIP_NOT_FOUND)
	{
	  send_response_error (ctx, root, ERR_SHIP_NOT_FOUND,
			       "Ship not found or update failed.");
	  return 0;
	}
      send_response_error (ctx, root, ERR_DB, "Repair failed.");
      return 0;
    }

  json_t *res = json_object ();


  json_object_set_new (res, "repaired", json_boolean (1));
  json_object_set_new (res, "cost", json_integer (cost));
  json_object_set_new (res, "hull", json_integer (100));
  send_response_ok_take (ctx, root, "ship.repair", &res);
  return 0;
}


// Helper to get active ship ID for a player
int
h_get_active_ship_id (db_t *db, int player_id)
{
  int ship_id = 0;
  if (repo_ships_get_active_id (db, player_id, &ship_id) != 0)
    {
      LOGE ("Failed to get active ship id for player %d", player_id);
    }
  return ship_id;
}


/*
 * cmd_ship_self_destruct: Initiate player ship self-destruct sequence.
 */
int
cmd_ship_self_destruct (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
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
  int ship_sector = 0;


  ship_sector = db_get_ship_sector_id (db, ship_id);

  ship_kill_context_t kill_ctx = {
    .victim_player_id = ctx->player_id,
    .victim_ship_id = ship_id,
    .killer_player_id = ctx->player_id,	// Self-destruct, so killer is also the victim
    .cause = KILL_CAUSE_SELF_DESTRUCT,
    .sector_id = ship_sector > 0 ? ship_sector : 1	// Fallback to sector 1 if current sector is invalid
  };
  int destroy_rc = handle_ship_destruction (db, &kill_ctx);


  if (destroy_rc != 0)
    {
      LOGE
	("cmd_ship_self_destruct: handle_ship_destruction failed for player %d, ship %d",
	 ctx->player_id, ship_id);
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
cmd_ship_inspect (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_SECTOR_NOT_FOUND,
				   "Not authenticated", NULL);
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
    db_ships_inspectable_at_sector_json (db, ctx->player_id, sector_id,
					 &ships);


  if (rc != 0 || !ships)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Database error");
      return 0;
    }
  json_t *payload = json_object ();


  json_object_set_new (payload, "sector", json_integer (sector_id));
  json_object_set_new (payload, "ships", ships);


  send_response_ok_take (ctx, root, "ship.inspect", &payload);
  return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////


/* ship.rename + ship.reregister */
int
cmd_ship_rename (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_SECTOR_NOT_FOUND,
				   "Not authenticated", NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_NOT_IN_SECTOR, "Bad request", NULL);
      return 0;
    }
  json_t *j_ship = json_object_get (data, "ship_id");
  json_t *j_name = json_object_get (data, "new_name");


  if (!json_is_integer (j_ship) || !json_is_string (j_name))
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_NOT_IN_SECTOR,
				   "Missing ship_id/new_name", NULL);
      return 0;
    }
  int ship_id = (int) json_integer_value (j_ship);
  const char *new_name = json_string_value (j_name);
  db_t *db = game_db_get_handle ();
  int rc = db_ship_rename_if_owner (db, ctx->player_id, ship_id, new_name);


  if (rc == -1)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "Permission denied", NULL);
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
  db_t *db_handle = game_db_get_handle ();
  h_decloak_ship (db_handle,
		  h_get_active_ship_id (db_handle, ctx->player_id));
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_SECTOR_NOT_FOUND,
				   "Not authenticated", NULL);
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
				   "Missing ship_id", NULL);
      return 0;
    }
  json_t *ship = NULL;
  int rc =
    db_ship_claim (db_handle, ctx->player_id, sector_id, ship_id, &ship);


  if (rc != 0 || !ship)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_SAFE_ZONE_ONLY,
				   "Ship not claimable", NULL);
      return 0;
    }
  json_t *payload = json_object ();


  json_object_set_new (payload, "ship", ship);


  send_response_ok_take (ctx, root, "ship.claimed", &payload);
  return 0;
}


/* ship.status (canonical) */
int
cmd_ship_status (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_SECTOR_NOT_FOUND,
				   "Not authenticated", NULL);
      return 0;
    }
  json_t *player_info = NULL;


  if (db_player_info_json (db, ctx->player_id, &player_info) != 0
      || !player_info)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Database error");
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
				   "Not authenticated", NULL);
      return 0;
    }


  int player_ship_id = h_get_active_ship_id (db,
					     ctx->player_id);


  if (player_ship_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NO_ACTIVE_SHIP,
				   "You do not have an active ship.", NULL);
      return 0;
    }

  /* 1. Get Player's Current Tow Status */
  int current_towing_ship_id = 0;
  if (repo_ships_get_towing_id (db, player_ship_id, &current_towing_ship_id)
      != 0)
    {
      LOGE ("cmd_ship_tow: DB error checking status for ship %d",
	    player_ship_id);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Database error");
      return 0;
    }

  /* 2. Parse Input */
  json_t *data = json_object_get (root, "data");
  int target_ship_id = 0;


  if (json_is_object (data))
    {
      json_t *j_target = json_object_get (data, "target_ship_id");


      if (json_is_integer (j_target))
	{
	  target_ship_id = (int) json_integer_value (j_target);
	}
    }

  /* 3. DISENGAGE LOGIC */
  if (current_towing_ship_id != 0)
    {
      if (target_ship_id == 0 || target_ship_id == current_towing_ship_id)
	{
	  db_error_t err;
	  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
	    {
	      goto rollback;
	    }

	  // 3a. Clear player's towing field
	  if (repo_ships_clear_towing_id (db, player_ship_id) != 0)
	    {
	      goto rollback;
	    }

	  // 3b. Clear target's "being towed" field
	  if (repo_ships_clear_is_being_towed_by (db, current_towing_ship_id)
	      != 0)
	    {
	      goto rollback;
	    }

	  if (!db_tx_commit (db, &err))
	    {
	      goto rollback;
	    }

	  {
	    json_t *tmp = json_object ();


	    json_object_set_new (tmp, "status",
				 json_string ("Towing beam disengaged"));
	    json_object_set_new (tmp, "towee_ship_id",
				 json_integer (current_towing_ship_id));


	    send_response_ok_take (ctx, root, "ship.tow.disengaged", &tmp);
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

  /* 4. ENGAGE LOGIC - Prerequisites */
  if (target_ship_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_MISSING_FIELD,
				   "Invalid target ship ID.", NULL);
      return 0;
    }
  if (target_ship_id == player_ship_id)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TARGET_SHIP_INVALID,
				   "You cannot tow yourself.", NULL);
      return 0;
    }

  int target_sector = 0;


  target_sector = db_get_ship_sector_id (db, target_ship_id);
  int player_sector = 0;


  player_sector = db_get_ship_sector_id (db, player_ship_id);


  if (target_sector <= 0 || target_sector != player_sector)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TARGET_SHIP_INVALID,
				   "Target ship not in your sector.", NULL);
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
				   "You do not own that ship.", NULL);
      return 0;
    }

  // Unmanned Check
  if (db_is_ship_piloted (db, target_ship_id))
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_SHIP_NOT_OWNED_OR_PILOTED,
				   "Target ship is currently piloted.", NULL);
      return 0;
    }

  // Already Towed Check
  int is_being_towed = 0;
  if (repo_ships_get_is_being_towed_by (db, target_ship_id, &is_being_towed)
      != 0)
    {
      LOGE ("cmd_ship_tow: DB error checking towed status for ship %d",
	    target_ship_id);
    }

  if (is_being_towed != 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_ALREADY_BEING_TOWED,
				   "Target is already under tow.", NULL);
      return 0;
    }

  /* 5. ENGAGE LOGIC - Execution */
  db_error_t err;
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      goto rollback;
    }

  // 5a. Set player's towing_ship_id -> target
  if (repo_ships_set_towing_id (db, player_ship_id, target_ship_id) != 0)
    {
      goto rollback;
    }

  // 5b. Set target's is_being_towed_by -> player
  if (repo_ships_set_is_being_towed_by (db, target_ship_id, player_ship_id) !=
      0)
    {
      goto rollback;
    }

  if (!db_tx_commit (db, &err))
    {
      goto rollback;
    }

  {
    json_t *tmp = json_object ();


    json_object_set_new (tmp, "status", json_string ("Towing beam engaged"));
    json_object_set_new (tmp, "towee_ship_id", json_integer (target_ship_id));


    send_response_ok_take (ctx, root, "ship.tow.engaged", &tmp);
  }
  return 0;

rollback:
  db_tx_rollback (db, NULL);
  LOGE ("cmd_ship_tow: DB Transaction failed: %s", err.message);
  send_response_error (ctx,
		       root,
		       ERR_DB_QUERY_FAILED, "Database transaction error");
  return 0;
}


int
cmd_ship_list (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx,
		       root,
		       ERR_NOT_IMPLEMENTED, "Not implemented: ship.list");
  return 0;
}


int
cmd_ship_sell (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx,
		       root,
		       ERR_NOT_IMPLEMENTED, "Not implemented: ship.sell");
  return 0;
}


int
cmd_ship_transfer (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx,
		       root,
		       ERR_NOT_IMPLEMENTED, "Not implemented: ship.transfer");
  return 0;
}


int
h_update_ship_cargo (db_t *db,
		     int ship_id,
		     const char *commodity_code,
		     int delta, int *new_quantity_out)
{
  LOGI ("h_update_ship_cargo: entered for ship_id=%d, commodity=%s, delta=%d",
	ship_id, commodity_code, delta);
  if (!db || ship_id <= 0 || !commodity_code)
    {
      LOGE
	("h_update_ship_cargo: invalid params: db=%p, ship_id=%d, commodity_code=%s",
	 (void *) db, ship_id, commodity_code ? commodity_code : "NULL");
      if (new_quantity_out)
	{
	  *new_quantity_out = 0;
	}
      return ERR_DB_MISUSE;
    }

  const char *col_name = NULL;


  if (strcasecmp (commodity_code, "ORE") == 0)
    {
      col_name = "ore";
    }
  else if (strcasecmp (commodity_code, "ORG") == 0)
    {
      col_name = "organics";
    }
  else if (strcasecmp (commodity_code, "EQU") == 0)
    {
      col_name = "equipment";
    }
  else if (strcasecmp (commodity_code, "COLONISTS") == 0)
    {
      col_name = "colonists";
    }
  else if (strcasecmp (commodity_code, "SLAVES") == 0)
    {
      col_name = "slaves";
    }
  else if (strcasecmp (commodity_code, "WEAPONS") == 0)
    {
      col_name = "weapons";
    }
  else if (strcasecmp (commodity_code, "DRUGS") == 0)
    {
      col_name = "drugs";
    }
  else
    {
      LOGE ("h_update_ship_cargo: unknown commodity_code: '%s'",
	    commodity_code);
      if (new_quantity_out)
	{
	  *new_quantity_out = 0;
	}
      return ERR_DB_MISUSE;
    }

  int ore = 0, org = 0, equ = 0, holds = 0, colonists = 0, slaves = 0,
    weapons = 0, drugs = 0;


  if (repo_ships_get_cargo_and_holds (db,
				      ship_id,
				      &ore,
				      &org,
				      &equ,
				      &colonists,
				      &slaves, &weapons, &drugs, &holds) != 0)
    {
      return ERR_SHIP_NOT_FOUND;
    }

  int current_qty = 0;


  if (strcasecmp (commodity_code, "ORE") == 0)
    {
      current_qty = ore;
    }
  else if (strcasecmp (commodity_code, "ORG") == 0)
    {
      current_qty = org;
    }
  else if (strcasecmp (commodity_code, "EQU") == 0)
    {
      current_qty = equ;
    }
  else if (strcasecmp (commodity_code, "COLONISTS") == 0)
    {
      current_qty = colonists;
    }
  else if (strcasecmp (commodity_code, "SLAVES") == 0)
    {
      current_qty = slaves;
    }
  else if (strcasecmp (commodity_code, "WEAPONS") == 0)
    {
      current_qty = weapons;
    }
  else if (strcasecmp (commodity_code, "DRUGS") == 0)
    {
      current_qty = drugs;
    }

  int new_qty = current_qty + delta;
  int current_total = ore + org + equ + colonists + slaves + weapons + drugs;
  int new_total = current_total - current_qty + new_qty;


  if (new_qty < 0)
    {
      if (new_quantity_out)
	{
	  *new_quantity_out = current_qty;
	}
      return ERR_INSUFFICIENT_FUNDS;	// Or generic constraint violation
    }

  if (new_total > holds && delta > 0)
    {
      if (new_quantity_out)
	{
	  *new_quantity_out = current_qty;
	}
      return ERR_HOLD_FULL;
    }

  int rc = repo_ships_update_cargo_column (db, ship_id, col_name, new_qty);
  if (rc != 0)
    {
      if (new_quantity_out)
	{
	  *new_quantity_out = current_qty;
	}
      return rc;
    }

  if (new_quantity_out)
    {
      *new_quantity_out = new_qty;
    }
  return 0;
}


int
h_get_ship_cargo_and_holds (db_t *db,
			    int ship_id,
			    int *ore,
			    int *organics,
			    int *equipment,
			    int *colonists,
			    int *slaves, int *weapons, int *drugs, int *holds)
{
  if (!db || ship_id <= 0)
    {
      return ERR_DB_MISUSE;
    }

  return repo_ships_get_cargo_and_holds (db,
					 ship_id,
					 ore,
					 organics,
					 equipment,
					 colonists,
					 slaves, weapons, drugs, holds);
}
