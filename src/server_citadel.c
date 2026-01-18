#include "db/repo/repo_citadel.h"
#include "db/repo/repo_cmd.h"
#include "server_citadel.h"
#include "db/sql_driver.h"
#include "server_envelope.h"
#include "db/sql_driver.h"
#include "errors.h"
#include "db/sql_driver.h"
#include "config.h"
#include "db/sql_driver.h"
#include "server_players.h"
#include "db/sql_driver.h"
#include "server_log.h"
#include "db/sql_driver.h"
#include <jansson.h>
#include <strings.h>
#include "server_corporation.h"
#include "db/sql_driver.h"
#include "game_db.h"
#include "db/sql_driver.h"


// Helper to get the player's current planet_id from their active ship.
// Returns planet_id > 0 on success, 0 if not on a planet or error.
static int
get_player_planet (db_t *db, int player_id)
{
  int ship_id = h_get_active_ship_id (db, player_id);
  if (ship_id <= 0)
    {
      return 0;
    }
  int planet_id = 0;
  if (repo_citadel_get_onplanet (db, ship_id, &planet_id) != 0)
    {
      return 0;
    }
  return planet_id;
}


/* Require auth before touching citadel features */
static inline int
require_auth (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id > 0)
    {
      return 1;
    }
  send_response_refused_steal (ctx,
			       root,
			       ERR_NOT_AUTHENTICATED,
			       "Not authenticated", NULL);
  return 0;
}


int
cmd_citadel_build (client_ctx_t *ctx, json_t *root)
{
  // This command is an alias for upgrading from level 0.
  return cmd_citadel_upgrade (ctx, root);
}


int
cmd_citadel_upgrade (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "Database unavailable.");
      return 0;
    }
  // 1. Get Player Location & Planet Info
  int planet_id = get_player_planet (db, ctx->player_id);


  if (planet_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_AUTOPILOT_PATH_INVALID,
				   "You must be landed on a planet to build or upgrade a citadel.",
				   NULL);
      return 0;
    }

  db_res_t *p_res = NULL;
  db_error_t err;

  int planet_type = 0, owner_id = 0;
  const char *owner_type_db = NULL;
  char *owner_type = NULL;
  long long p_colonists = 0, p_ore = 0, p_org = 0, p_equip = 0;

  if (repo_citadel_get_planet_info (db, planet_id, &p_res) == 0)
    {
      if (db_res_step (p_res, &err))
	{
	  planet_type = db_res_col_i32 (p_res, 0, &err);
	  owner_id = db_res_col_i32 (p_res, 1, &err);
	  owner_type_db = db_res_col_text (p_res, 2, &err);
	  owner_type = owner_type_db ? strdup (owner_type_db) : NULL;
	  p_colonists = db_res_col_i64 (p_res, 3, &err);
	  p_ore = db_res_col_i64 (p_res, 4, &err);
	  p_org = db_res_col_i64 (p_res, 5, &err);
	  p_equip = db_res_col_i64 (p_res, 6, &err);
	}
      else
	{
	  db_res_finalize (p_res);
	  send_response_error (ctx, root, ERR_DB,
			       "Failed to query planet data.");
	  return 0;
	}
      db_res_finalize (p_res);
    }
  else
    {
      send_response_error (ctx, root, ERR_DB, "Failed to query planet data.");
      return 0;
    }

  bool can_build = false;


  if (owner_type && strcmp (owner_type, "player") == 0)
    {
      if (owner_id == ctx->player_id)
	{
	  can_build = true;
	}
    }
  else if (owner_type && strcmp (owner_type, "corp") == 0)
    {
      int player_corp_id = h_get_player_corp_id (db, ctx->player_id);


      if (player_corp_id > 0 && player_corp_id == owner_id)
	{
	  can_build = true;
	}
    }
  free (owner_type);
  if (!can_build)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "You do not have permission to build on this planet.",
				   NULL);
      return 0;
    }
  // 2. Get Citadel State
  int current_level = 0;
  const char *construction_status_db = NULL;
  char *construction_status = NULL;
  db_res_t *c_res = NULL;

  if (repo_citadel_get_status (db, planet_id, &c_res) == 0)
    {
      if (db_res_step (c_res, &err))
	{
	  current_level = db_res_col_i32 (c_res, 0, &err);
	  construction_status_db = db_res_col_text (c_res, 1, &err);
	  construction_status =
	    construction_status_db ? strdup (construction_status_db) : NULL;
	}
      db_res_finalize (c_res);
    }
  if (!construction_status)
    {
      construction_status = strdup ("idle");
    }

  if (construction_status && strcasecmp (construction_status, "idle") != 0)
    {
      send_response_refused_steal (ctx, root, ERR_SERIALIZATION,
				   "An upgrade is already in progress.",
				   NULL);
      free (construction_status);
      return 0;
    }
  free (construction_status);
  if (current_level >= 6)
    {
      send_response_refused_steal (ctx, root, ERR_VERSION_NOT_SUPPORTED,
				   "Citadel is already at maximum level.",
				   NULL);
      return 0;
    }
  int target_level = current_level + 1;
  // 3. Get Upgrade Requirements
  long long r_colonists = 0, r_ore = 0, r_org = 0, r_equip = 0;
  int r_days = 0;
  db_res_t *r_res = NULL;

  if (repo_citadel_get_upgrade_reqs (db, target_level, planet_type, &r_res) ==
      0)
    {
      if (db_res_step (r_res, &err))
	{
	  r_colonists = db_res_col_i64 (r_res, 0, &err);
	  r_ore = db_res_col_i64 (r_res, 1, &err);
	  r_org = db_res_col_i64 (r_res, 2, &err);
	  r_equip = db_res_col_i64 (r_res, 3, &err);
	  r_days = db_res_col_i32 (r_res, 4, &err);
	}
      db_res_finalize (r_res);
    }

  if (r_days <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR,
			   "Could not retrieve upgrade requirements for this planet type.");
      return 0;
    }
  // 4. Check Resources
  if (p_colonists < r_colonists || p_ore < r_ore || p_org < r_org
      || p_equip < r_equip)
    {
      json_t *missing = json_object ();


      if (p_colonists < r_colonists)
	{
	  json_object_set_new (missing, "colonists",
			       json_integer (r_colonists - p_colonists));
	}
      if (p_ore < r_ore)
	{
	  json_object_set_new (missing, "ore", json_integer (r_ore - p_ore));
	}
      if (p_org < r_org)
	{
	  json_object_set_new (missing, "organics",
			       json_integer (r_org - p_org));
	}
      if (p_equip < r_equip)
	{
	  json_object_set_new (missing, "equipment",
			       json_integer (r_equip - p_equip));
	}
      json_t *meta = json_object ();


      json_object_set_new (meta, "missing", missing);
      send_response_refused_steal (ctx,
				   root,
				   REF_NO_WARP_LINK,
				   "Insufficient resources on planet to begin upgrade.",
				   meta);
      return 0;
    }
  // 5. Execute Upgrade
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, err.code, "Database busy (upgrade)");
      return 0;
    }

  // Deduct resources
  if (repo_citadel_deduct_resources (db, r_ore, r_org, r_equip, planet_id) !=
      0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB, "Failed to deduct resources.");
      return 0;
    }

  // Start construction
  long long start_time = time (NULL);
  long long end_time = start_time + (r_days * 86400);

  if (repo_citadel_start_construction
      (db, planet_id, current_level, ctx->player_id, target_level, start_time,
       end_time) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx,
			   root,
			   ERR_DB, "Failed to start citadel construction.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code, "Commit failed.");
      return 0;
    }

  // Log the event for news generation
  json_t *event_payload = json_object ();


  json_object_set_new (event_payload, "planet_id", json_integer (planet_id));
  json_object_set_new (event_payload, "current_level",
		       json_integer (current_level));
  json_object_set_new (event_payload, "target_level",
		       json_integer (target_level));
  json_object_set_new (event_payload, "days_to_complete",
		       json_integer (r_days));
  db_log_engine_event ((long long) time (NULL), "citadel.upgrade_started",
		       "player", ctx->player_id, 0, event_payload, NULL);
  json_decref (event_payload);
  // 6. Send Response
  json_t *payload_pl = json_object ();


  json_object_set_new (payload_pl, "planet_id", json_integer (planet_id));
  json_object_set_new (payload_pl, "target_level",
		       json_integer (target_level));
  json_object_set_new (payload_pl, "completion_time",
		       json_integer (end_time));
  json_object_set_new (payload_pl, "days_to_complete", json_integer (r_days));
  send_response_ok_take (ctx, root, "citadel.upgrade_started", &payload_pl);
  return 0;
}
