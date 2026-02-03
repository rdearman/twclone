/* src/server_planets.c */
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <jansson.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>

/* local includes */
#include "server_planets.h"
#include "server_rules.h"
#include "common.h"
#include "server_log.h"
#include "db/repo/repo_database.h"
#include "db/repo/repo_planets.h"
#include "game_db.h"
#include "errors.h"
#include "server_cmds.h"
#include "server_corporation.h"
#include "server_ports.h"
#include "repo_market.h"
#include "server_players.h"
#include "server_ships.h"
#include "repo_cmd.h"
#include "server_config.h"
#include "server_combat.h"
#include "server_ports.h"
#include "db/db_api.h"
#include "db/sql_driver.h"

#ifndef GENESIS_ENABLED
#define GENESIS_ENABLED 1
#endif
#ifndef GENESIS_BLOCK_AT_CAP
#define GENESIS_BLOCK_AT_CAP 1
#endif
#ifndef GENESIS_NAVHAZ_DELTA
#define GENESIS_NAVHAZ_DELTA 5
#endif

/* Common helper: authentication check */
static inline int
require_auth (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id > 0)
    {
      return 1;
    }
  send_response_refused_steal (ctx, root, ERR_SECTOR_NOT_FOUND,
			       "Not authenticated", NULL);
  return 0;
}

/* Apply Terra sanctions (destroy all assets and zero credits) */
static void
h_apply_terra_sanctions (db_t *db, int player_id)
{
  db_planets_apply_terra_sanctions (db, player_id);
}


int
cmd_combat_attack_planet (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }

  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVICE_UNAVAILABLE,
			   "Database unavailable");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!data)
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD, "Missing data");
      return 0;
    }

  json_t *j_pid = json_object_get (data, "planet_id");
  if (!j_pid || !json_is_integer (j_pid))
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Missing planet_id");
      return 0;
    }
  int planet_id = (int) json_integer_value (j_pid);

  /* Get active ship */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }

  /* Get current sector */
  int current_sector = ctx->sector_id;

  if (current_sector <= 0)
    {
      db_planets_get_ship_sector (db, ship_id, &current_sector);
    }

  /* Get planet info */
  int p_sector, p_owner_id, p_fighters;
  if (db_planets_get_attack_info
      (db, planet_id, &p_sector, &p_owner_id, &p_fighters) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found");
      return 0;
    }

  /* Check sector match */
  if (p_sector != current_sector)
    {
      send_response_error (ctx, root, REF_NOT_IN_SECTOR,
			   "Planet not in current sector");
      return 0;
    }

  /* Terra protection: instant destruction + sanctions */
  if (planet_id == 1 || p_sector == 1)
    {
      destroy_ship_and_handle_side_effects (ctx, ctx->player_id);
      h_apply_terra_sanctions (db, ctx->player_id);

      json_t *evt = json_object ();
      json_object_set_new (evt, "planet_id", json_integer (planet_id));
      json_object_set_new (evt, "sanctioned", json_true ());
      db_log_engine_event ((long long) time (NULL),
			   "player.terra_attack_sanction.v1",
			   "player", ctx->player_id, current_sector, evt,
			   NULL);
      json_decref (evt);

      send_response_error (ctx, root, 403,
			   "You have attacked Terra! Federation forces have destroyed your ship and seized your assets.");
      return 0;
    }

  /* Get attacker ship fighters */
  int s_fighters = 0;
  if (db_planets_get_ship_fighters (db, ship_id, &s_fighters) != 0)
    {
      send_response_error (ctx, root, ERR_DB, "Failed to load ship");
      return 0;
    }

  if (s_fighters <= 0)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "You have no fighters to attack with.");
      return 0;
    }

  /* Get citadel defense */
  int cit_level = 0;
  int cit_shields = 0;
  int cit_reaction = 0;

  db_planets_get_citadel_defenses (db, planet_id, &cit_level, &cit_shields,
				   &cit_reaction);

  int fighters_absorbed = 0;

  /* Citadel shields (level >= 5) */
  if (cit_level >= 5 && cit_shields > 0)
    {
      int absorbed = (s_fighters < cit_shields) ? s_fighters : cit_shields;
      s_fighters -= absorbed;
      fighters_absorbed = absorbed;
      int new_shields = cit_shields - absorbed;

      db_planets_update_citadel_shields (db, planet_id, new_shields);
    }

  /* CCC military reaction (level >= 2) */
  int effective_p_fighters = p_fighters;
  if (cit_level >= 2 && cit_reaction > 0)
    {
      int pct = 100;
      if (cit_reaction == 1)
	pct = 125;
      else if (cit_reaction >= 2)
	pct = 150;
      effective_p_fighters =
	(int) floor ((double) p_fighters * (double) pct / 100.0);
    }

  /* Combat resolution */
  bool attacker_wins = (s_fighters > effective_p_fighters);
  int ship_loss = 0;
  int planet_loss = 0;
  bool captured = false;

  if (attacker_wins)
    {
      ship_loss = effective_p_fighters;
      planet_loss = p_fighters;
      captured = true;
    }
  else
    {
      ship_loss = s_fighters;
      planet_loss = s_fighters;
      captured = false;
    }

  ship_loss += fighters_absorbed;

  /* Update database in transaction */
  db_error_t err;
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Transaction failed");
      return 0;
    }

  /* Update attacker ship fighters */
  db_planets_update_ship_fighters (db, ship_id, ship_loss);

  /* Update planet */
  if (captured)
    {
      int corp_id = ctx->corp_id;
      const char *new_type = (corp_id > 0) ? "corporation" : "player";
      int new_owner = (corp_id > 0) ? corp_id : ctx->player_id;

      db_planets_capture (db, planet_id, new_owner, new_type);

      json_t *cap_evt = json_object ();
      json_object_set_new (cap_evt, "planet_id", json_integer (planet_id));
      json_object_set_new (cap_evt, "previous_owner",
			   json_integer (p_owner_id));
      db_log_engine_event ((long long) time (NULL),
			   "player.capture_planet.v1", "player",
			   ctx->player_id, current_sector, cap_evt, NULL);
      json_decref (cap_evt);
    }
  else
    {
      db_planets_lose_fighters (db, planet_id, planet_loss);
    }

  db_tx_commit (db, &err);

  /* Log attack */
  json_t *atk_evt = json_object ();
  json_object_set_new (atk_evt, "planet_id", json_integer (planet_id));
  json_object_set_new (atk_evt, "result",
		       json_string (attacker_wins ? "win" : "loss"));
  json_object_set_new (atk_evt, "ship_loss", json_integer (ship_loss));
  json_object_set_new (atk_evt, "planet_loss", json_integer (planet_loss));
  db_log_engine_event ((long long) time (NULL),
		       "player.attack_planet.v1",
		       "player", ctx->player_id, current_sector, atk_evt,
		       NULL);
  json_decref (atk_evt);

  /* Send response */
  json_t *resp = json_object ();
  json_object_set_new (resp, "planet_id", json_integer (planet_id));
  json_object_set_new (resp, "attacker_remaining_fighters",
		       json_integer (s_fighters - ship_loss));
  json_object_set_new (resp, "defender_remaining_fighters",
		       json_integer (p_fighters - planet_loss));
  json_object_set_new (resp, "captured", json_boolean (captured));

  send_response_ok_take (ctx, root, "combat.attack_planet", &resp);
  return 0;
}


// Helper to check if a commodity is illegal
static bool
h_is_illegal_commodity (db_t *db, const char *commodity_code)
{
  bool illegal = false;
  db_planets_is_commodity_illegal (db, commodity_code, &illegal);
  return illegal;
}


/**
 * @brief Logic helper to determine if a planet is NPC-controlled.
 */
bool
planet_is_npc (const planet_t *p)
{
  if (!p)
    {
      return false;
    }
  // If owner_id is 0, it's unowned or system-owned (NPC behavior)
  if (p->owner_id == 0)
    {
      return true;
    }
  // If owner_type is explicitly "npc" (if that becomes a thing), or not "player"/"corporation"
  if (p->owner_type)
    {
      if (strcasecmp (p->owner_type, "player") == 0)
	{
	  return false;
	}
      if (strcasecmp (p->owner_type, "corporation") == 0)
	{
	  return false;
	}
      if (strcasecmp (p->owner_type, "corp") == 0)
	{
	  return false;
	}
      // Any other type is considered NPC (e.g. "npc", "system", "alien")
      return true;
    }
  return true;
}


int
h_planet_check_trade_legality (db_t *db,
			       int pid,
			       int player_id, const char *code, bool buy)
{
  (void) buy;
  if (!h_is_illegal_commodity (db, code))
    {
      return 1;			// Legal
    }

  // 1. Get Sector ID
  int sector_id = 0;
  if (db_planets_get_sector (db, pid, &sector_id) != 0)
    {
      return 0;
    }

  if (sector_id <= 0)
    {
      return 0;			// Illegal or error
    }

  // 2. Check Cluster Alignment
  int cluster_band = 0;


  h_get_cluster_alignment_band (db, sector_id, &cluster_band);

  int cluster_good = 0;


  db_alignment_band_for_value (db,
			       cluster_band,
			       NULL,
			       NULL, NULL, &cluster_good, NULL, NULL, NULL);

  if (cluster_good)
    {
      return 0;			// Good clusters ban illegal trade
    }

  // 3. Check Player Alignment
  int p_align = 0;


  db_player_get_alignment (db, player_id, &p_align);

  int neutral_band = db_get_config_int (db, "neutral_band", 75);


  if (p_align > neutral_band)
    {
      return 0;			// Good players banned
    }

  // 4. Check Neutral Config
  int p_evil = 0;


  db_alignment_band_for_value (db,
			       p_align,
			       NULL, NULL, NULL, NULL, &p_evil, NULL, NULL);

  if (!p_evil && !db_get_config_bool (db, "illegal_allowed_neutral", true))
    {
      return 0;
    }

  return 1;			// Legal
}


int
h_get_planet_owner_info (db_t *db, int pid, planet_t *p)
{
  if (!p)
    {
      return ERR_DB_MISUSE;
    }
  return db_planets_get_owner_info (db, pid, &p->owner_id, &p->owner_type);
}


static void
h_free_planet_t (planet_t *p)
{
  if (p->owner_type)
    {
      free (p->owner_type);
    }
}


int
cmd_planet_info (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");
  int pid = (int) json_integer_value (json_object_get (data, "planet_id"));
  json_t *info = NULL;
  if (db_planet_get_details_json (db, pid, &info) == 0)
    {
      send_response_ok_take (ctx, root, "planet.info", &info);
    }
  else
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Failed");
    }
  return 0;
}


int
cmd_planet_rename (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NOT_AUTHENTICATED,
				   "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  int planet_id = 0;


  if (!json_get_int_flexible (data, "planet_id", &planet_id)
      || planet_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Missing or invalid 'planet_id'.");
      return 0;
    }

  const char *new_name = json_get_string_or_null (data, "new_name");


  if (!new_name || strlen (new_name) < 3 || strlen (new_name) > 32)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Name must be between 3 and 32 characters.");
      return 0;
    }

  db_t *db = game_db_get_handle ();
  planet_t p = { 0 };


  if (h_get_planet_owner_info (db, planet_id, &p) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
    }

  bool allowed = false;


  if (p.owner_type && strcmp (p.owner_type,
			      "player") == 0 && p.owner_id == ctx->player_id)
    {
      allowed = true;
    }
  else if (p.owner_type && (strcmp (p.owner_type,
				    "corp") == 0 || strcmp (p.owner_type,
							    "corporation") ==
			    0))
    {
      if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id)
	{
	  allowed = true;
	}
    }
  h_free_planet_t (&p);

  if (!allowed)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_NOT_PLANET_OWNER,
				   "You do not own this planet.", NULL);
      return 0;
    }

  if (db_planets_rename (db, planet_id, new_name) != 0)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Update failed.");
      return 0;
    }

  send_response_ok_take (ctx, root, "planet.rename.success", NULL);
  return 0;
}


int
cmd_planet_land (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return -1;
    }

  // Consume 1 turn for landing
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);


  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "planet.land", root,
					    NULL);
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data payload.");
      return 0;
    }
  int planet_id = 0;


  json_unpack (data, "{s:i}", "planet_id", &planet_id);
  if (planet_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_MISSING_FIELD,
			   "Missing or invalid 'planet_id'.");
      return 0;
    }

  // Check if player is in the same sector as the planet
  int player_sector = 0;


  db_player_get_sector (db, ctx->player_id, &player_sector);

  int planet_sector = 0;
  int owner_id = 0;
  char *owner_type = NULL;


  if (db_planets_get_land_info
      (db, planet_id, &planet_sector, &owner_id, &owner_type) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
    }

  if (player_sector != planet_sector)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "You are not in the same sector as the planet.");
      if (owner_type)
	{
	  free (owner_type);
	}
      return 0;
    }

  bool can_land = false;


  if (owner_id == 0)
    {				// unowned
      can_land = true;
    }
  else if (owner_type && strcmp (owner_type, "player") == 0)
    {
      if (owner_id == ctx->player_id)
	{
	  can_land = true;
	}
    }
  else if (owner_type && (strcmp (owner_type,
				  "corp") == 0 || strcmp (owner_type,
							  "corporation") ==
			  0))
    {
      int player_corp_id = h_get_player_corp_id (db, ctx->player_id);


      if (player_corp_id > 0 && player_corp_id == owner_id)
	{
	  can_land = true;
	}
    }
  if (owner_type)
    {
      free (owner_type);
    }

  if (!can_land)
    {
      send_response_error (ctx,
			   root,
			   ERR_PERMISSION_DENIED,
			   "You do not have permission to land on this planet.");
      return 0;
    }

  /* Task A: Block landing on Earth for evil players */
  int alignment = 0;
  db_player_get_alignment (db, ctx->player_id, &alignment);
  if (alignment < 0 && planet_sector == 1)
    {
      send_response_refused_steal (ctx, root, REASON_EVIL_ALIGN,
				   "Earth refuses landing to criminals.",
				   NULL);
      return 0;
    }

  // Atmosphere Quasar Check (C3)
  if (planet_id != 1)		// Skip Terra
    {
      if (h_trigger_atmosphere_quasar (db, ctx, planet_id))
	{
	  // Ship destroyed
	  send_response_error (ctx,
			       root,
			       403, "Ship destroyed by planetary defences.");
	  return 0;
	}
    }

  if (db_player_land_on_planet (db, ctx->player_id, planet_id) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR, "Failed to land on planet.");
      return 0;
    }

  // Update context
  ctx->sector_id = 0;		// Not in a sector anymore
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Landed successfully."));
  json_object_set_new (response_data, "planet_id", json_integer (planet_id));
  send_response_ok_take (ctx, root, "planet.land.success", &response_data);
  return 0;
}


int
cmd_planet_launch (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_NOT_AUTHENTICATED, "Authentication required.");
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return -1;
    }

  int sector_id = 0;


  if (db_player_launch_from_planet (db, ctx->player_id, &sector_id) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR,
			   "Failed to launch from planet. Are you on a planet?");
      return 0;
    }

  // Update context
  ctx->sector_id = sector_id;

  /* Canon #471: Sector assets engage on entry */
  if (server_combat_apply_entry_hazards (db, ctx, sector_id))
    {
      send_response_error (ctx, root, 403, "Ship destroyed by sector hazards on launch.");
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Launched successfully."));
  json_object_set_new (response_data, "sector_id", json_integer (sector_id));
  send_response_ok_take (ctx, root, "planet.launch.success", &response_data);
  return 0;
}


int
cmd_planet_transfer_ownership (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NOT_AUTHENTICATED,
				   "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data payload.");
      return 0;
    }

  int planet_id = 0;
  int target_id = 0;


  if (!json_get_int_flexible (data, "planet_id", &planet_id)
      || planet_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Missing or invalid 'planet_id'.");
      return 0;
    }
  if (!json_get_int_flexible (data, "target_id", &target_id)
      || target_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Missing or invalid 'target_id'.");
      return 0;
    }
  const char *target_type = json_string_value (json_object_get (data,
								"target_type"));


  if (!target_type)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Missing or invalid 'target_type'.");
      return 0;
    }

  if (strcmp (target_type, "player") != 0 && strcmp (target_type,
						     "corp") != 0 &&
      strcmp (target_type, "corporation") != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "target_type must be 'player' or 'corp'.");
      return 0;
    }

  db_t *db = game_db_get_handle ();
  planet_t p = { 0 };


  if (h_get_planet_owner_info (db, planet_id, &p) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
    }

  bool allowed = false;


  if (p.owner_type && strcmp (p.owner_type,
			      "player") == 0 && p.owner_id == ctx->player_id)
    {
      allowed = true;
    }
  else if (p.owner_type && (strcmp (p.owner_type,
				    "corp") == 0 || strcmp (p.owner_type,
							    "corporation") ==
			    0))
    {
      if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id)
	{
	  allowed = true;
	}
    }
  h_free_planet_t (&p);

  if (!allowed)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_NOT_PLANET_OWNER,
				   "You do not own this planet.", NULL);
      return 0;
    }

  bool exists = false;
  if (strcmp (target_type, "player") == 0)
    {
      db_planets_check_player_exists (db, target_id, &exists);
    }
  else
    {
      db_planets_check_corp_exists (db, target_id, &exists);
    }


  if (!exists)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND,
			   "Target entity not found.");
      return 0;
    }

  if (db_planets_transfer_ownership (db, planet_id, target_id, target_type) !=
      0)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Update failed.");
      return 0;
    }

  send_response_ok_take (ctx, root, "planet.transfer_ownership.success",
			 NULL);
  return 0;
}


int
cmd_planet_harvest (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx,
		       root,
		       ERR_NOT_IMPLEMENTED,
		       "Not implemented: planet.harvest");
  return 0;
}


int
cmd_planet_deposit (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NOT_AUTHENTICATED,
				   "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data payload.");
      return 0;
    }

  int planet_id = 0;
  int quantity = 0;
  const char *commodity = json_string_value (json_object_get (data, "commodity"));

  if (!json_get_int_flexible (data, "quantity", &quantity) || quantity <= 0)
    {
      // Fallback to legacy 'amount'
      if (!json_get_int_flexible (data, "amount", &quantity) || quantity <= 0)
        {
           send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid or missing quantity/amount.");
           return 0;
        }
    }

  if (!json_get_int_flexible (data, "planet_id", &planet_id) || planet_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "Invalid planet_id.");
      return 0;
    }

  planet_t p = { 0 };


  if (h_get_planet_owner_info (db, planet_id, &p) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
    }

  bool allowed = false;


  if (p.owner_type && strcmp (p.owner_type,
			      "player") == 0 && p.owner_id == ctx->player_id)
    {
      allowed = true;
    }
  else if (p.owner_type && (strcmp (p.owner_type,
				    "corp") == 0 || strcmp (p.owner_type,
							    "corporation") ==
			    0))
    {
      if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id)
	{
	  allowed = true;
	}
    }
  h_free_planet_t (&p);

  if (!allowed)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "You do not control this planet.", NULL);
      return 0;
    }

  /* Handle Treasury (CREDITS) */
  if (!commodity || strcasecmp (commodity, "CREDITS") == 0)
    {
      int citadel_level = 0;
      db_planets_get_citadel_level (db, planet_id, &citadel_level);

      if (citadel_level < 1)
	{
	  send_response_refused_steal (ctx,
				       root,
				       REF_TURN_COST_EXCEEDS,
				       "No citadel or insufficient level for treasury.",
				       NULL);
	  return 0;
	}

      db_error_t err;
      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
	{
	  send_response_error (ctx, root, ERR_SERVER_ERROR, "Database busy.");
	  return 0;
	}

      long long new_player_balance = 0;


      if (h_deduct_player_petty_cash_unlocked (db,
					       ctx->player_id,
					       quantity, &new_player_balance) != 0)
	{
	  db_tx_rollback (db, NULL);
	  send_response_refused_steal (ctx,
				       root,
				       REF_NO_WARP_LINK,
				       "Insufficient credits.", NULL);
	  return 0;
	}

      if (db_planets_add_treasury (db, planet_id, quantity) != 0)
	{
	  db_tx_rollback (db, NULL);
	  send_response_error (ctx,
			       root, ERR_SERVER_ERROR, "Database update failed.");
	  return 0;
	}

      if (!db_tx_commit (db, &err))
	{
	  db_tx_rollback (db, NULL);
	  send_response_error (ctx,
			       root,
			       ERR_SERVER_ERROR, "Transaction commit failed.");
	  return 0;
	}

      long long new_treasury = 0;
      db_planets_get_treasury (db, planet_id, &new_treasury);

      json_t *resp = json_object ();


      json_object_set_new (resp, "planet_id", json_integer (planet_id));
      json_object_set_new (resp, "planet_treasury_balance",
			   json_integer (new_treasury));
      json_object_set_new (resp, "player_credits",
			   json_integer (new_player_balance));

      send_response_ok_take (ctx, root, "planet.deposit", &resp);
      return 0;
    }

  /* Handle Commodities */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
       send_response_error (ctx, root, ERR_NO_ACTIVE_SHIP, "No active ship found.");
       return 0;
    }

  db_error_t err;
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Database busy.");
      return 0;
    }

  int alignment = 0;
  db_player_get_alignment (db, ctx->player_id, &alignment);

  const char *target_commodity = commodity;
  /* Slave -> Colonist conversion for evil players */
  if (strcasecmp (commodity, "SLAVES") == 0 && alignment < 0)
    {
      target_commodity = "COLONISTS";
    }

  /* Deduct from ship */
  int rc = h_update_ship_cargo (db, ship_id, commodity, -quantity, NULL);
  if (rc != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, rc, "Insufficient cargo on ship.");
      return 0;
    }

  /* Add to planet */
  int prc = 0;
  if (strcasecmp (target_commodity, "COLONISTS") == 0)
    {
      prc = db_planets_add_colonists_unassigned (db, planet_id, quantity);
    }
  else if (strcasecmp (target_commodity, "ORE") == 0)
    {
      prc = db_planets_add_ore_on_hand (db, planet_id, quantity);
    }
  else if (strcasecmp (target_commodity, "ORG") == 0 || strcasecmp (target_commodity, "ORGANICS") == 0)
    {
      prc = db_planets_add_organics_on_hand (db, planet_id, quantity);
    }
  else if (strcasecmp (target_commodity, "EQU") == 0 || strcasecmp (target_commodity, "EQUIPMENT") == 0)
    {
      prc = db_planets_add_equipment_on_hand (db, planet_id, quantity);
    }
  else
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_INVALID_ARG, "Unsupported commodity for deposit.");
      return 0;
    }

  if (prc != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Planet update failed.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Transaction commit failed.");
      return 0;
    }

  json_t *resp = json_object ();
  json_object_set_new (resp, "planet_id", json_integer (planet_id));
  json_object_set_new (resp, "commodity", json_string (commodity));
  json_object_set_new (resp, "quantity", json_integer (quantity));
  if (strcasecmp (target_commodity, commodity) != 0)
    {
      json_object_set_new (resp, "converted_to", json_string (target_commodity));
    }

  send_response_ok_take (ctx, root, "planet.deposit", &resp);
  return 0;
}


int
cmd_planet_withdraw (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NOT_AUTHENTICATED,
				   "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data payload.");
      return 0;
    }

  int planet_id = 0;
  int quantity = 0;
  const char *commodity = json_string_value (json_object_get (data, "commodity"));

  if (!json_get_int_flexible (data, "quantity", &quantity) || quantity <= 0)
    {
      // Fallback to legacy 'amount'
      if (!json_get_int_flexible (data, "amount", &quantity) || quantity <= 0)
        {
          send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid or missing quantity/amount.");
          return 0;
        }
    }

  if (!json_get_int_flexible (data, "planet_id", &planet_id) || planet_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG, "Invalid planet_id.");
      return 0;
    }

  planet_t p = { 0 };


  if (h_get_planet_owner_info (db, planet_id, &p) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
    }

  bool allowed = false;


  if (p.owner_type && strcmp (p.owner_type,
			      "player") == 0 && p.owner_id == ctx->player_id)
    {
      allowed = true;
    }
  else if (p.owner_type && (strcmp (p.owner_type,
				    "corp") == 0 || strcmp (p.owner_type,
							    "corporation") ==
			    0))
    {
      if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id)
	{
	  char role[16];


	  h_get_player_corp_role (db,
				  ctx->player_id,
				  ctx->corp_id, role, sizeof (role));
	  if (strcasecmp (role, "Leader") == 0 || strcasecmp (role,
							      "Officer") == 0)
	    {
	      allowed = true;
	    }
	}
    }
  h_free_planet_t (&p);

  if (!allowed)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "You do not control this planet or lack permissions.",
				   NULL);
      return 0;
    }

  /* Handle Treasury (CREDITS) */
  if (!commodity || strcasecmp (commodity, "CREDITS") == 0)
    {
      int citadel_level = 0;
      long long current_treasury = 0;
      db_planets_get_citadel_info (db, planet_id, &citadel_level,
				   &current_treasury);

      if (citadel_level < 1)
	{
	  send_response_refused_steal (ctx,
				       root,
				       REF_TURN_COST_EXCEEDS,
				       "No citadel or insufficient level for treasury.",
				       NULL);
	  return 0;
	}

      if (current_treasury < quantity)
	{
	  send_response_refused_steal (ctx,
				       root,
				       REF_NO_WARP_LINK,
				       "Insufficient treasury funds.", NULL);
	  return 0;
	}

      db_error_t err;
      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
	{
	  send_response_error (ctx, root, ERR_SERVER_ERROR, "Database busy.");
	  return 0;
	}

      if (db_planets_deduct_treasury (db, planet_id, quantity) != 0)
	{
	  db_tx_rollback (db, NULL);
	  send_response_error (ctx, root, ERR_SERVER_ERROR, "Database error.");
	  return 0;
	}

      long long new_player_balance = 0;


      if (h_add_player_petty_cash_unlocked (db,
					    ctx->player_id,
					    quantity, &new_player_balance) != 0)
	{
	  db_tx_rollback (db, NULL);
	  send_response_error (ctx,
			       root,
			       ERR_SERVER_ERROR, "Failed to credit player.");
	  return 0;
	}

      if (!db_tx_commit (db, &err))
	{
	  db_tx_rollback (db, NULL);
	  send_response_error (ctx,
			       root,
			       ERR_SERVER_ERROR, "Transaction commit failed.");
	  return 0;
	}

      json_t *resp = json_object ();


      json_object_set_new (resp, "planet_id", json_integer (planet_id));
      json_object_set_new (resp, "planet_treasury_balance",
			   json_integer (current_treasury - quantity));
      json_object_set_new (resp, "player_credits",
			   json_integer (new_player_balance));

      send_response_ok_take (ctx, root, "planet.withdraw", &resp);
      return 0;
    }

  /* Handle Commodities */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
       send_response_error (ctx, root, ERR_NO_ACTIVE_SHIP, "No active ship found.");
       return 0;
    }

  int alignment = 0;
  db_player_get_alignment (db, ctx->player_id, &alignment);

  const char *target_commodity = commodity;
  /* Colonist -> Slave conversion for evil players */
  if (strcasecmp (commodity, "COLONISTS") == 0 && alignment < 0)
    {
       target_commodity = "SLAVES";
    }

  /* Check planet stock */
  int64_t planet_qty = 0;
  int prc = 0;
  if (strcasecmp (commodity, "COLONISTS") == 0)
    {
      prc = db_planets_get_colonists_unassigned (db, planet_id, &planet_qty);
    }
  else if (strcasecmp (commodity, "ORE") == 0)
    {
      prc = db_planets_get_ore_on_hand (db, planet_id, &planet_qty);
    }
  else if (strcasecmp (commodity, "ORG") == 0 || strcasecmp (commodity, "ORGANICS") == 0)
    {
      prc = db_planets_get_organics_on_hand (db, planet_id, &planet_qty);
    }
  else if (strcasecmp (commodity, "EQU") == 0 || strcasecmp (commodity, "EQUIPMENT") == 0)
    {
      prc = db_planets_get_equipment_on_hand (db, planet_id, &planet_qty);
    }
  else
    {
       send_response_error (ctx, root, ERR_INVALID_ARG, "Unsupported commodity for withdrawal.");
       return 0;
    }

  if (prc != 0)
    {
       send_response_error (ctx, root, ERR_SERVER_ERROR, "Failed to check planet stock.");
       return 0;
    }

  if (planet_qty < quantity)
    {
       send_response_refused_steal (ctx, root, REF_NOT_ENOUGH_COMMODITY, "Insufficient stock on planet.", NULL);
       return 0;
    }

  db_error_t err;
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Database busy.");
      return 0;
    }

  /* Deduct from planet */
  if (strcasecmp (commodity, "COLONISTS") == 0)
    {
      prc = db_planets_add_colonists_unassigned (db, planet_id, -quantity);
    }
  else if (strcasecmp (commodity, "ORE") == 0)
    {
      prc = db_planets_add_ore_on_hand (db, planet_id, -quantity);
    }
  else if (strcasecmp (commodity, "ORG") == 0 || strcasecmp (commodity, "ORGANICS") == 0)
    {
      prc = db_planets_add_organics_on_hand (db, planet_id, -quantity);
    }
  else if (strcasecmp (commodity, "EQU") == 0 || strcasecmp (commodity, "EQUIPMENT") == 0)
    {
      prc = db_planets_add_equipment_on_hand (db, planet_id, -quantity);
    }

  if (prc != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Planet update failed.");
      return 0;
    }

  /* Add to ship (this also checks for holds) */
  int rc = h_update_ship_cargo (db, ship_id, target_commodity, quantity, NULL);
  if (rc != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, rc, "Insufficient holds or ship update failed.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Transaction commit failed.");
      return 0;
    }

  json_t *resp = json_object ();
  json_object_set_new (resp, "planet_id", json_integer (planet_id));
  json_object_set_new (resp, "commodity", json_string (commodity));
  json_object_set_new (resp, "quantity", json_integer (quantity));
  if (strcasecmp (target_commodity, commodity) != 0)
    {
      json_object_set_new (resp, "converted_to", json_string (target_commodity));
    }

  send_response_ok_take (ctx, root, "planet.withdraw", &resp);
  return 0;
}


static int
send_error_and_return (client_ctx_t *ctx, json_t *root, int err_code,
		       const char *msg)
{
  send_response_error (ctx, root, err_code, msg);
  return 0;
}


int
cmd_planet_genesis_create (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return ERR_DB_CLOSED;
    }

  json_t *data = json_object_get (root, "data");
  int player_id = ctx->player_id;
  int ship_id = ctx->ship_id;
  int target_sector_id;
  const char *owner_entity_type = NULL;
  const char *idempotency_key = NULL;
  int owner_id = 0;
  char planet_class_str[2] = { 0 };
  int64_t new_planet_id = -1;
  bool over_cap_flag = false;
  int navhaz_delta = 0;
  int64_t current_unix_ts = (int64_t) time (NULL);
  json_t *response_json = NULL;


  if (!data)
    {
      return send_error_and_return (ctx,
				    root,
				    ERR_BAD_REQUEST, "Missing data payload.");
    }

  if (player_id <= 0)
    {
      return send_error_and_return (ctx, root, ERR_NOT_AUTHENTICATED,
				    "Not authenticated.");
    }

  if (ship_id <= 0)
    {
      ship_id = h_get_active_ship_id (db, player_id);
      if (ship_id > 0)
	{
	  ctx->ship_id = ship_id;
	}
    }

  if (ship_id <= 0)
    {
      return send_error_and_return (ctx,
				    root,
				    ERR_NOT_AUTHENTICATED,
				    "No active ship found.");
    }
  if (!json_get_int_flexible (data, "sector_id",
			      &target_sector_id) || target_sector_id <= 0)
    {
      return send_error_and_return (ctx,
				    root,
				    ERR_INVALID_ARG,
				    "Missing or invalid 'sector_id'.");
    }

  /* FedSpace protection: sectors 1-10 are restricted */
  if (target_sector_id <= 10)
    {
      return send_error_and_return (ctx,
				    root,
				    ERR_FORBIDDEN_IN_SECTOR,
				    "Planet creation prohibited in FedSpace.");
    }

  const char *requested_name = json_get_string_or_null (data, "name");


  if (!requested_name || strlen (requested_name) == 0)
    {
      return send_error_and_return (ctx,
				    root,
				    ERR_MISSING_FIELD,
				    "Missing 'name' for the new planet.");
    }
  char *planet_name = strdup (requested_name);


  owner_entity_type = json_get_string_or_null (data, "owner_entity_type");
  if (!owner_entity_type || (strcasecmp (owner_entity_type,
					 "player") != 0 &&
			     strcasecmp (owner_entity_type,
					 "corporation") != 0))
    {
      free (planet_name);
      return send_error_and_return (ctx,
				    root,
				    ERR_INVALID_OWNER_TYPE,
				    "Invalid 'owner_entity_type'. Must be 'player' or 'corporation'.");
    }
  idempotency_key = json_get_string_or_null (data, "idempotency_key");

  if (idempotency_key)
    {
      char *prev_json = NULL;
      if (db_planets_lookup_genesis_idem (db, idempotency_key, &prev_json) ==
	  0 && prev_json)
	{
	  json_t *prev_payload = json_loads (prev_json, 0, NULL);
	  free (prev_json);

	  if (prev_payload)
	    {
	      send_response_ok_take (ctx,
				     root,
				     "planet.genesis_created_v1",
				     &prev_payload);
	      free (planet_name);
	      return 0;
	    }
	}
    }

  if (!GENESIS_ENABLED)
    {
      free (planet_name);
      return send_error_and_return (ctx,
				    root,
				    ERR_GENESIS_DISABLED,
				    "Genesis torpedo feature is currently disabled.");
    }

  bool is_msl = false;
  if (db_planets_is_msl_sector (db, target_sector_id, &is_msl) == 0 && is_msl)
    {
      free (planet_name);
      return send_error_and_return (ctx,
				    root,
				    ERR_GENESIS_MSL_PROHIBITED,
				    "Planet creation prohibited in MSL sector.");
    }

  int current_count = 0;
  db_planets_count_in_sector (db, target_sector_id, &current_count);

  int max_per_sector = db_get_config_int (db, "max_planets_per_sector", 6);


  over_cap_flag = (current_count + 1 > max_per_sector);
  if (GENESIS_BLOCK_AT_CAP && current_count >= max_per_sector)
    {
      free (planet_name);
      return send_error_and_return (ctx,
				    root,
				    ERR_GENESIS_SECTOR_FULL,
				    "Sector has reached maximum planets.");
    }

  int max_name_len = db_get_config_int (db, "max_name_length", 50);


  if (strlen (planet_name) > (size_t) max_name_len)
    {
      free (planet_name);
      return send_error_and_return (ctx,
				    root,
				    ERR_INVALID_PLANET_NAME_LENGTH,
				    "Planet name too long.");
    }

  if (strcasecmp (owner_entity_type, "corporation") == 0)
    {
      if (ctx->corp_id <= 0)
	{
	  free (planet_name);
	  return send_error_and_return (ctx,
					root,
					ERR_NO_CORPORATION,
					"Player is not in a corporation to create a corporate planet.");
	}
      owner_id = ctx->corp_id;
    }
  else
    {
      owner_id = player_id;
    }

  int torps = 0;
  db_planets_get_ship_genesis (db, ship_id, &torps);

  if (torps < 1)
    {
      free (planet_name);
      return send_error_and_return (ctx,
				    root,
				    ERR_NO_GENESIS_TORPEDO,
				    "Insufficient Genesis torpedoes on your ship.");
    }

  const char *classes[] = { "M", "K", "O", "L", "C", "H", "U" };
  int weights[7] = { 10, 10, 10, 10, 10, 10, 5 };
  int total_weight = 65;

  json_t *weights_array = NULL;
  if (db_planets_get_type_weights (db, &weights_array) == 0)
    {
      int tw = 0;
      size_t index;
      json_t *value;
      json_array_foreach (weights_array, index, value)
      {
	const char *code =
	  json_string_value (json_object_get (value, "code"));
	int w = (int) json_integer_value (json_object_get (value, "weight"));
	for (int i = 0; i < 7; i++)
	  {
	    if (strcasecmp (code, classes[i]) == 0)
	      {
		weights[i] = (w < 0) ? 0 : w;
		tw += weights[i];
		break;
	      }
	  }
      }
      json_decref (weights_array);
      if (tw > 0)
	total_weight = tw;
    }

  int rv = randomnum (0, total_weight - 1);
  int sel = 0, sum = 0;


  for (int i = 0; i < 7; i++)
    {
      sum += weights[i];
      if (rv < sum)
	{
	  sel = i;
	  break;
	}
    }
  strncpy (planet_class_str, classes[sel], 1);
  planet_class_str[1] = '\0';

  int type_id = -1;
  db_planets_get_type_id_by_code (db, planet_class_str, &type_id);

  if (type_id == -1)
    {
      free (planet_name);
      return send_error_and_return (ctx,
				    root,
				    ERR_DB_QUERY_FAILED,
				    "Failed to resolve planet type.");
    }

  if (db_planets_create
      (db, target_sector_id, planet_name, owner_id, owner_entity_type,
       planet_class_str, type_id, current_unix_ts, player_id,
       &new_planet_id) != 0)
    {
      free (planet_name);
      return send_error_and_return (ctx,
				    root, ERR_DB, "Failed to create planet.");
    }

  db_planets_consume_genesis (db, ship_id);

  navhaz_delta = GENESIS_NAVHAZ_DELTA;
  if (navhaz_delta != 0)
    {
      db_planets_update_navhaz (db, target_sector_id, navhaz_delta);
    }

  response_json = json_object ();
  json_object_set_new (response_json, "sector_id",
		       json_integer (target_sector_id));
  json_object_set_new (response_json, "planet_id",
		       json_integer ((int) new_planet_id));
  json_object_set_new (response_json, "class",
		       json_string (planet_class_str));
  json_object_set_new (response_json, "name", json_string (planet_name));
  json_object_set_new (response_json, "owner_type",
		       json_string (owner_entity_type));
  json_object_set_new (response_json, "owner_id", json_integer (owner_id));
  json_object_set_new (response_json, "over_cap",
		       json_boolean (over_cap_flag));
  json_object_set_new (response_json, "navhaz_delta",
		       json_integer (navhaz_delta));

  if (idempotency_key)
    {
      char *payload_str = json_dumps (response_json, 0);
      db_planets_insert_genesis_idem (db, idempotency_key, payload_str,
				      current_unix_ts);
      free (payload_str);
    }

  send_response_ok_take (ctx, root, "planet.genesis_created_v1",
			 &response_json);

  json_t *ev_payload = json_object ();


  json_object_set_new (ev_payload, "planet_id",
		       json_integer ((int) new_planet_id));
  json_object_set_new (ev_payload, "class", json_string (planet_class_str));
  json_object_set_new (ev_payload, "name", json_string (planet_name));
  json_object_set_new (ev_payload, "owner_id", json_integer (player_id));
  json_object_set_new (ev_payload, "sector_id",
		       json_integer (target_sector_id));

  db_log_engine_event (current_unix_ts,
		       "planet.genesis_created",
		       "player", player_id, target_sector_id, ev_payload, db);
  json_decref (ev_payload);
  free (planet_name);
  return 0;
}


int
cmd_planet_market_sell (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NOT_AUTHENTICATED,
				   "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data payload.");
      return 0;
    }

  int planet_id = 0;


  json_unpack (data, "{s:i}", "planet_id", &planet_id);
  const char *raw_commodity = json_string_value (json_object_get (data,
								  "commodity"));
  int quantity = 0;


  json_unpack (data, "{s:i}", "quantity", &quantity);

  if (planet_id <= 0 || !raw_commodity || quantity <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid parameters.");
      return 0;
    }

  planet_t p = { 0 };


  if (h_get_planet_owner_info (db, planet_id, &p) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
    }

  if (planet_is_npc (&p))
    {
      h_free_planet_t (&p);
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "Cannot manually trade with NPC planets.",
				   NULL);
      return 0;
    }

  bool allowed = false;


  if (p.owner_type && strcmp (p.owner_type,
			      "player") == 0 && p.owner_id == ctx->player_id)
    {
      allowed = true;
    }
  else if (p.owner_type && (strcmp (p.owner_type,
				    "corp") == 0 || strcmp (p.owner_type,
							    "corporation") ==
			    0))
    {
      if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id)
	{
	  allowed = true;
	}
    }
  h_free_planet_t (&p);

  if (!allowed)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "You do not control this planet.", NULL);
      return 0;
    }

  const char *commodity_code = raw_commodity;


  if (!commodity_code)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_AUTOPILOT_PATH_INVALID,
				   "Invalid commodity.", NULL);
      return 0;
    }

  if (!h_planet_check_trade_legality (db,
				      planet_id,
				      ctx->player_id, commodity_code, false))
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "Illegal trade refused.", NULL);
      return 0;
    }

  int current_stock = 0;
  db_planets_get_stock (db, planet_id, commodity_code, &current_stock);

  if (current_stock < quantity)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_NO_WARP_LINK,
				   "Insufficient inventory on planet.", NULL);
      return 0;
    }

  int unit_price = 0;
  if (db_planets_get_commodity_price (db, commodity_code, &unit_price) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR,
			   "Could not determine commodity price.");
      return 0;
    }

  if (unit_price <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR,
			   "Could not determine commodity price.");
      return 0;
    }

  long long total_credits = (long long) quantity * unit_price;


  if (h_update_entity_stock (db,
			     ENTITY_TYPE_PLANET,
			     planet_id, commodity_code, -quantity, NULL) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR,
			   "Failed to update planet inventory.");
      return 0;
    }

  long long new_balance = 0;


  if (h_add_player_petty_cash (db, ctx->player_id, total_credits,
			       &new_balance) != 0)
    {
      LOGE ("cmd_planet_market_sell: Failed to credit player %d.",
	    ctx->player_id);
    }

  json_t *resp = json_object ();


  json_object_set_new (resp, "planet_id", json_integer (planet_id));
  json_object_set_new (resp, "commodity", json_string (commodity_code));
  json_object_set_new (resp, "quantity_sold", json_integer (quantity));
  json_object_set_new (resp, "total_credits_received",
		       json_integer (total_credits));

  send_response_ok_take (ctx, root, "planet.market.sell", &resp);
  return 0;
}


int
cmd_planet_market_buy_order (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();

  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NOT_AUTHENTICATED,
				   "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data payload.");
      return 0;
    }

  int planet_id = 0;


  json_unpack (data, "{s:i}", "planet_id", &planet_id);
  const char *raw_commodity = json_string_value (json_object_get (data,
								  "commodity"));
  int quantity_total = 0;


  json_unpack (data, "{s:i}", "quantity_total", &quantity_total);
  int max_price = 0;


  json_unpack (data, "{s:i}", "max_price", &max_price);

  if (planet_id <= 0 || !raw_commodity || quantity_total <= 0
      || max_price <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid parameters.");
      return 0;
    }

  planet_t p = { 0 };


  if (h_get_planet_owner_info (db, planet_id, &p) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
    }

  if (planet_is_npc (&p))
    {
      h_free_planet_t (&p);
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "Cannot manually trade with NPC planets.",
				   NULL);
      return 0;
    }

  bool allowed = false;


  if (p.owner_type && strcmp (p.owner_type,
			      "player") == 0 && p.owner_id == ctx->player_id)
    {
      allowed = true;
    }
  else if (p.owner_type && (strcmp (p.owner_type,
				    "corp") == 0 || strcmp (p.owner_type,
							    "corporation") ==
			    0))
    {
      if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id)
	{
	  allowed = true;
	}
    }
  h_free_planet_t (&p);

  if (!allowed)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "You do not control this planet.", NULL);
      return 0;
    }

  const char *commodity_code = raw_commodity;


  if (!commodity_code)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_AUTOPILOT_PATH_INVALID,
				   "Invalid commodity.", NULL);
      return 0;
    }

  if (!h_planet_check_trade_legality (db,
				      planet_id,
				      ctx->player_id, commodity_code, true))
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "Illegal trade refused by port authority (alignment/cluster rules).",
				   NULL);
      return 0;
    }

  int commodity_id = 0;
  db_planets_get_commodity_id (db, commodity_code, &commodity_id);

  if (commodity_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR, "Commodity ID lookup failed.");
      return 0;
    }

  if (h_is_illegal_commodity (db, commodity_code))
    {
      int stock = 0;
      db_planets_get_stock (db, planet_id, commodity_code, &stock);

      if (stock < quantity_total)
	{
	  send_response_refused_steal (ctx,
				       root,
				       REF_NO_WARP_LINK,
				       "Insufficient inventory for immediate illegal purchase.",
				       NULL);
	  return 0;
	}

      long long player_credits = 0;


      if (h_get_player_petty_cash (db, ctx->player_id, &player_credits) != 0)
	{
	  send_response_error (ctx,
			       root,
			       ERR_SERVER_ERROR, "Credit check failed.");
	  return 0;
	}

      long long cost = (long long) quantity_total * max_price;


      if (player_credits < cost)
	{
	  send_response_refused_steal (ctx,
				       root,
				       REF_NO_WARP_LINK,
				       "Insufficient credits.", NULL);
	  return 0;
	}

      int ship_id = h_get_active_ship_id (db, ctx->player_id);
      int free_holds = 0;


      if (h_get_ship_cargo_and_holds (db,
				      ship_id,
				      NULL,
				      NULL,
				      NULL,
				      NULL,
				      NULL,
				      NULL,
				      NULL,
				      &free_holds) != 0 ||
	  free_holds < quantity_total)
	{
	  send_response_refused_steal (ctx,
				       root,
				       REF_NO_WARP_LINK,
				       "Insufficient cargo space.", NULL);
	  return 0;
	}

      db_error_t err;
      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
	{
	  send_response_error (ctx, root, ERR_SERVER_ERROR, "Database busy.");
	  return 0;
	}

      h_update_entity_stock (db,
			     ENTITY_TYPE_PLANET,
			     planet_id,
			     commodity_code, -quantity_total, NULL);
      h_update_ship_cargo (db, ship_id, commodity_code, quantity_total, NULL);
      h_deduct_player_petty_cash_unlocked (db, ctx->player_id, cost, NULL);
      db_planets_add_treasury_buy (db, planet_id, cost);

      if (!db_tx_commit (db, &err))
	{
	  db_tx_rollback (db, NULL);
	  send_response_error (ctx,
			       root, ERR_SERVER_ERROR, "Transaction failed.");
	  return 0;
	}

      json_t *resp = json_object ();


      json_object_set_new (resp, "status", json_string ("complete"));
      json_object_set_new (resp, "quantity", json_integer (quantity_total));
      json_object_set_new (resp, "total_cost", json_integer (cost));
      send_response_ok_take (ctx, root, "planet.market.buy.complete", &resp);
      return 0;
    }

  long long worst_case_cost = (long long) quantity_total * max_price;
  long long player_cash = 0;


  if (h_get_player_petty_cash (db, ctx->player_id, &player_cash) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR, "Failed to check credits.");
      return 0;
    }

  if (player_cash < worst_case_cost)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_NO_WARP_LINK,
				   "Insufficient credits for max cost.",
				   NULL);
      return 0;
    }

  int64_t order_id = 0;
  if (db_planets_insert_buy_order
      (db, ctx->player_id, planet_id, commodity_id, quantity_total, max_price,
       &order_id) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR, "DB Error inserting order.");
      return 0;
    }

  json_t *resp = json_object ();


  json_object_set_new (resp, "order_id", json_integer (order_id));
  json_object_set_new (resp, "planet_id", json_integer (planet_id));
  json_object_set_new (resp, "commodity", json_string (commodity_code));
  json_object_set_new (resp, "quantity_total", json_integer (quantity_total));
  json_object_set_new (resp, "max_price", json_integer (max_price));

  send_response_ok_take (ctx, root, "planet.market.buy_order", &resp);
  return 0;
}


int
cmd_planet_colonists_set (client_ctx_t *ctx, json_t *root)
{
  // TODO: P3B Schema update pending. Stub for now.
  send_response_error (ctx,
		       root,
		       ERR_NOT_IMPLEMENTED,
		       "Not implemented: planet.colonists.set");
  return 0;
}


int
cmd_planet_colonists_get (client_ctx_t *ctx, json_t *root)
{
  // TODO: P3B Schema update pending. Stub for now.
  send_response_error (ctx,
		       root,
		       ERR_NOT_IMPLEMENTED,
		       "Not implemented: planet.colonists.get");
  return 0;
}


int
cmd_planet_transwarp (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NOT_AUTHENTICATED,
				   "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data");
      return 0;
    }

  int planet_id = 0;
  int to_sector_id = 0;


  json_unpack (data,
	       "{s:i, s:i}",
	       "planet_id", &planet_id, "to_sector_id", &to_sector_id);

  if (planet_id <= 1 || to_sector_id <= 1)
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_ARG,
			   "Invalid planet or restricted sector.");
      return 0;
    }

  db_t *db = game_db_get_handle ();
  planet_t p = { 0 };


  if (h_get_planet_owner_info (db, planet_id, &p) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
    }

  bool allowed = false;


  if (p.owner_type && strcmp (p.owner_type,
			      "player") == 0 && p.owner_id == ctx->player_id)
    {
      allowed = true;
    }
  else if (p.owner_type && (strcmp (p.owner_type,
				    "corp") == 0 || strcmp (p.owner_type,
							    "corporation") ==
			    0))
    {
      if (ctx->corp_id > 0 && ctx->corp_id == p.owner_id)
	{
	  char role[16];


	  h_get_player_corp_role (db,
				  ctx->player_id,
				  ctx->corp_id, role, sizeof (role));
	  if (strcasecmp (role, "Leader") == 0 || strcasecmp (role,
							      "Officer") == 0)
	    {
	      allowed = true;
	    }
	}
    }
  h_free_planet_t (&p);

  if (!allowed)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "Permission denied.", NULL);
      return 0;
    }

  int level = 0;
  db_planets_get_citadel_level (db, planet_id, &level);

  if (level < 4)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_NO_WARP_LINK,
				   "Citadel level too low (requires L4).",
				   NULL);
      return 0;
    }

  int fuel_on_hand = 0;
  db_planets_get_fuel_stock (db, planet_id, &fuel_on_hand);

  if (fuel_on_hand < 500)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_NO_WARP_LINK,
				   "Insufficient Fuel Ore (requires 500).",
				   NULL);
      return 0;
    }

  db_error_t err;
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Database busy.");
      return 0;
    }

  if (h_update_entity_stock (db,
			     ENTITY_TYPE_PLANET,
			     planet_id, "FUE", -500, NULL) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR, "Fuel consumption failed.");
      return 0;
    }

  if (db_planets_set_sector (db, planet_id, to_sector_id) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Update failed.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR, "Transaction commit failed.");
      return 0;
    }

  json_t *resp = json_object ();


  json_object_set_new (resp, "planet_id", json_integer (planet_id));
  json_object_set_new (resp, "new_sector_id", json_integer (to_sector_id));
  send_response_ok_take (ctx, root, "planet.transwarp.success", &resp);
  return 0;
}


int
h_market_move_planet_stock (db_t *db, int pid, const char *code, int delta)
{
  if (!db || pid <= 0 || !code)
    {
      return ERR_DB_MISUSE;
    }

  if (delta == 0)
    {
      return 0;
    }

  // 1. Get current quantity and capacity
  int current_quantity = 0;
  int max_capacity = 0;
  int maxore, maxorg, maxequ;

  if (db_planets_get_market_move_info
      (db, pid, code, &current_quantity, &maxore, &maxorg, &maxequ) != 0)
    {
      return ERR_NOT_FOUND;
    }

  if (strcasecmp (code, "ORE") == 0)
    {
      max_capacity = maxore;
    }
  else if (strcasecmp (code, "ORG") == 0)
    {
      max_capacity = maxorg;
    }
  else if (strcasecmp (code, "EQU") == 0)
    {
      max_capacity = maxequ;
    }
  else
    {
      max_capacity = 999999;
    }

  // 2. Calculate new quantity with overflow and bounds checking
  int new_quantity;
  if (__builtin_add_overflow (current_quantity, delta, &new_quantity))
    {
      /* Overflow: clamp based on delta direction */
      new_quantity = (delta > 0) ? INT_MAX : 0;
    }


  new_quantity = (new_quantity < 0) ? 0 : new_quantity;
  new_quantity = (new_quantity > max_capacity) ? max_capacity : new_quantity;

  // 3. Update DB
  return db_planets_upsert_stock (db, pid, code, new_quantity);
}

/* Look up commodity ID by code */
int
h_get_commodity_id_by_code (db_t *db, const char *code)
{
  int id = 0;
  db_planets_get_commodity_id_v2 (db, code, &id);
  return id;
}
