/* src/server_planets.c */
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <jansson.h>
#include <stdbool.h>
#include <limits.h>
#include <math.h>
#include <inttypes.h>
#include <time.h>

/* local includes */
#include "server_planets.h"
#include "server_rules.h"
#include "common.h"
#include "server_log.h"
#include "db/repo/repo_database.h"
#include "db/repo/repo_planets.h"
#include "db/repo/repo_clusters.h"
#include "db/repo/repo_commodities.h"
#include "db/repo/repo_ships.h"
#include "db/repo/repo_citadel.h"
#include "db/repo/repo_corporation.h"
#include "db/repo/repo_ports.h"
#include "db/repo/repo_universe.h"
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

static int
planet_record_taxable_trade (db_t *db, int planet_id, const char *commodity,
                             int quantity, int unit_price, const char *kind,
                             json_t *root)
{
  if (!db || db_backend (db) != DB_BACKEND_POSTGRES || planet_id <= 0
      || !commodity || quantity <= 0 || unit_price < 0 || !kind)
    return db && db_backend (db) != DB_BACKEND_POSTGRES ? 0 : -1;

  const char *request_id = json_string_value (json_object_get (root, "id"));
  char fallback[37] = {0};
  if (!request_id)
    {
      h_generate_hex_uuid (fallback, sizeof fallback);
      request_id = fallback;
    }
  char key[256];
  snprintf (key, sizeof key, "planet-trade:%s:%d:%s", kind, planet_id,
            request_id);
  const char *sql =
    "INSERT INTO planet_economic_activity (planet_id,activity_type,commodity_code,quantity,unit_value,taxable_value,idempotency_key) "
    "VALUES ({1},'market_trade',{2},{3},{4},{3}::bigint*{4},{5}) ON CONFLICT (idempotency_key) DO NOTHING;";
  db_error_t err;
  db_error_clear (&err);
  if (!db_exec (db, sql,
                (db_bind_t[]){db_bind_i64 (planet_id),db_bind_text ((char *)commodity),
                              db_bind_i64 (quantity),db_bind_i64 (unit_price),
                              db_bind_text (key)}, 5, &err))
    return err.code ? err.code : -1;
  return 0;
}

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

  /* FedSpace enforcement: hard-punish aggression in sectors 1–10 */
  if (current_sector >= 1 && current_sector <= 10)
    {
      int enforcement_rc =
        fedspace_enforce_no_aggression_hard (ctx, ship_id, ctx->player_id,
                                              "combat.attack_planet");
      if (enforcement_rc == 1)
        {
          send_response_refused_steal (ctx, root, ERR_PERMISSION_DENIED,
                                       "Captain Z intervenes: aggression in FedSpace is forbidden.",
                                       NULL);
          return 0;
        }
      if (enforcement_rc < 0)
        {
          send_response_error (ctx, root, -enforcement_rc,
                               "Captain Z enforcement failed; no changes were committed.");
          return 0;
        }
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

  /* Only registered Federation assets are enforceable.  The generic
   * incident repository deliberately makes this a no-op for ordinary
   * planets, and Captain Z has already returned for sectors 1-10. */
  if (current_sector > 10)
    {
      char incident_key[128];
      snprintf (incident_key, sizeof incident_key,
                "iss:attack:planet:%d:%d:%lld", ctx->player_id, planet_id,
                (long long) time (NULL) / 60);
      (void) repo_npc_security_incident_claim
        (db, incident_key, ctx->player_id, "planet", planet_id,
         current_sector, (int64_t) time (NULL) + 86400);
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

  int player_xp = db_get_player_xp (db, player_id);
  int minimum_illegal_xp = db_get_config_int (db, "illegal_min_experience", 400);
  if (player_xp < minimum_illegal_xp)
    {
      return 0;
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

  // 3. Check alignment band: good players are blocked; neutral and evil may
  // proceed subject to the neutral setting below.
  int p_good = 0;
  int p_evil = 0;


  db_alignment_band_for_value (db,
			       p_align,
		       NULL, NULL, NULL, &p_good, &p_evil, NULL, NULL);

  if (p_good)
    {
      return 0;
    }

  // 4. Check Neutral Config

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

/* Terra is a public Federation facility. It is identified by its canonical
 * seeded identity: players may inspect and land on it, but landing never
 * grants ownership. */
static bool
h_is_terra (int planet_id, int planet_sector)
{
  return planet_id == 1 && planet_sector == 1;
}

bool
h_planet_is_terra (db_t *db, int planet_id)
{
  int planet_sector = 0;
  int owner_id = 0;
  char *owner_type = NULL;
  bool terra = db && planet_id > 0
    && db_planets_get_land_info (db, planet_id, &planet_sector, &owner_id,
                                 &owner_type) == 0
    && h_is_terra (planet_id, planet_sector);
  free (owner_type);
  return terra;
}

static int
h_refuse_terra_mutation (db_t *db, client_ctx_t *ctx, json_t *root,
                         int planet_id)
{
  if (!h_planet_is_terra (db, planet_id))
    return 0;
  send_response_error (ctx, root, ERR_PERMISSION_DENIED,
                       "Terra is a Federation colonist source; this action is not permitted.");
  return 1;
}


int
cmd_planet_info (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");
  json_t *planet_value = json_is_object (data)
    ? json_object_get (data, "planet_id") : NULL;
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Not authenticated");
      return 0;
    }
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "Database connection failed");
      return 0;
    }
  if (!json_is_integer (planet_value) || json_integer_value (planet_value) <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Missing or invalid planet_id");
      return 0;
    }
  int pid = (int) json_integer_value (planet_value);

  int planet_sector = 0;
  int owner_id = 0;
  char *owner_type = NULL;
  if (db_planets_get_land_info (db, pid, &planet_sector, &owner_id,
                                &owner_type) != 0)
    {
      free (owner_type);
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found");
      return 0;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  int landed_planet = 0;
  bool landed = ship_id > 0
    && repo_citadel_get_onplanet (db, ship_id, &landed_planet) == 0
    && landed_planet == pid;
  /* Terra's public information is readable without landing or ownership.
   * This does not authorize treasury access and does not make it claimable. */
  bool authorized = landed || h_is_terra (pid, planet_sector);
  bool treasury_authorized = false;
  if (!authorized && owner_type && strcmp (owner_type, "player") == 0
      && owner_id == ctx->player_id)
    authorized = treasury_authorized = true;
  else if (owner_type && strcmp (owner_type, "player") == 0
           && owner_id == ctx->player_id)
    treasury_authorized = true;
  if (!authorized && owner_type
      && (strcmp (owner_type, "corp") == 0
          || strcmp (owner_type, "corporation") == 0)
      && owner_id > 0)
    {
      char role[64] = {0};
      authorized = repo_corp_check_member_role (db, owner_id, ctx->player_id,
                                                 role, sizeof role) == 0;
      treasury_authorized = authorized;
    }
  else if (owner_type
           && (strcmp (owner_type, "corp") == 0
               || strcmp (owner_type, "corporation") == 0)
           && owner_id > 0)
    {
      char role[64] = {0};
      treasury_authorized = repo_corp_check_member_role
        (db, owner_id, ctx->player_id, role, sizeof role) == 0;
    }
  free (owner_type);
  if (!authorized)
    {
      send_response_error (ctx, root, ERR_FORBIDDEN_IN_SECTOR,
                           "Planet details require landing or ownership authorization");
      return 0;
    }

  json_t *info = NULL;
  if (db_planet_get_details_json (db, pid, &info) == 0)
    {
      /* The repository row normally contains this identity, but the client
       * correlates every landed-surface refresh strictly by planet_id. Set it
       * explicitly so schema/column naming differences cannot disable the
       * launch and colonist controls after a successful landing. */
      json_object_set_new (info, "planet_id", json_integer (pid));
      /* Existing detail JSON includes the planetary fighter count.  Add the
       * current citadel lifecycle and its already-supported defensive state. */
      json_t *citadel = json_object ();
      if (!citadel)
        {
          json_decref (info);
          send_response_error (ctx, root, ERR_SERVER_ERROR,
                               "Could not allocate planet details.");
          return 0;
        }
      bool citadel_exists = false;
      int32_t citadel_level = 0;
      char construction_status[32] = {0};
      int32_t target_level = 0;
      int64_t construction_start = 0, completion_time = 0;
      int construction_rc = repo_citadel_get_construction_state
        (db, pid, &citadel_level, construction_status,
         sizeof construction_status, &target_level, &construction_start,
         &completion_time);
      if (construction_rc == 0)
        {
          citadel_exists = true;
          json_object_set_new (citadel, "level", json_integer (citadel_level));
          json_object_set_new (citadel, "construction_status",
                               json_string (construction_status));
          json_object_set_new (citadel, "target_level",
                               json_integer (target_level));
          json_object_set_new (citadel, "construction_start_time",
                               json_integer (construction_start));
          json_object_set_new (citadel, "expected_completion_time",
                               json_integer (completion_time));
        }
      else if (construction_rc != ERR_DB_NOT_FOUND)
        {
          json_decref (citadel);
          json_decref (info);
          send_response_error (ctx, root, ERR_DB,
                               "Could not retrieve citadel construction state.");
          return 0;
        }
      int defense_level = 0, shields = 0, reaction = 0;
      bool has_defenses = false;
      int defense_rc = db_planets_get_citadel_defenses_ex
        (db, pid, &has_defenses, &defense_level, &shields, &reaction);
      if (defense_rc != 0)
        {
          json_decref (citadel);
          json_decref (info);
          send_response_error (ctx, root, ERR_DB,
                               "Could not retrieve citadel defensive state.");
          return 0;
        }
      if (citadel_exists)
        {
          json_object_set_new (citadel, "level", json_integer (defense_level));
          json_object_set_new (citadel, "planetary_shields",
                               json_integer (shields));
          json_object_set_new (citadel, "military_reaction_level",
                               json_integer (reaction));
          int32_t qcannon_sector = 0, qcannon_atmosphere = 0;
          int qcannon_rc = repo_citadel_get_qcannon_state
            (db, pid, &qcannon_sector, &qcannon_atmosphere);
          if (qcannon_rc != 0 && qcannon_rc != ERR_DB_NOT_FOUND)
            {
              json_decref (citadel);
              json_decref (info);
              send_response_error (ctx, root, ERR_DB,
                                   "Could not retrieve Quantum Cannon settings.");
              return 0;
            }
          if (qcannon_rc == 0)
            {
              json_object_set_new (citadel, "qCannonSector",
                                   json_integer (qcannon_sector));
              json_object_set_new (citadel, "qCannonAtmosphere",
                                   json_integer (qcannon_atmosphere));
            }
        }
      if (treasury_authorized && citadel_exists)
        {
          long long treasury = 0;
          if (db_planets_get_treasury (db, pid, &treasury) != 0)
            {
              json_decref (citadel);
              json_decref (info);
              send_response_error (ctx, root, ERR_DB,
                                   "Could not retrieve citadel treasury.");
              return 0;
            }
          json_object_set_new (citadel, "treasury_balance",
                               json_integer (treasury));
        }
      if (json_object_size (citadel) > 0)
        json_object_set_new (info, "citadel", citadel);
      else
        json_decref (citadel);
      send_response_ok_take (ctx, root, "planet.info", &info);
    }
  else
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Failed");
    }
  return 0;
}

int
cmd_planet_fighters_transfer (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_NOT_AUTHENTICATED,
                                   "Not authenticated", NULL);
      return 0;
    }
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "Database unavailable.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Expected a planet fighter transfer data object.");
      return 0;
    }
  int planet_id = 0, quantity = 0;
  const char *direction = json_string_value
    (json_object_get (data, "direction"));
  if (!json_get_int_flexible (data, "planet_id", &planet_id)
      || planet_id <= 0 || !json_get_int_flexible (data, "quantity", &quantity)
      || quantity <= 0 || !direction
      || (strcmp (direction, "ship_to_planet") != 0
          && strcmp (direction, "planet_to_ship") != 0))
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "planet_id, positive quantity, and a supported direction are required.");
      return 0;
    }
  if (h_refuse_terra_mutation (db, ctx, root, planet_id))
    return 0;

  planet_t planet = {0};
  if (h_get_planet_owner_info (db, planet_id, &planet) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
    }
  bool owns_planet = planet.owner_type
    && strcmp (planet.owner_type, "player") == 0
    && planet.owner_id == ctx->player_id;
  if (!owns_planet && planet.owner_type
      && (strcmp (planet.owner_type, "corp") == 0
          || strcmp (planet.owner_type, "corporation") == 0))
    {
      char role[64] = {0};
      owns_planet = repo_corp_check_member_role
        (db, planet.owner_id, ctx->player_id, role, sizeof role) == 0;
    }
  h_free_planet_t (&planet);
  if (!owns_planet)
    {
      send_response_refused_steal (ctx, root, ERR_PERMISSION_DENIED,
                                   "You do not control this planet.", NULL);
      return 0;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  int landed_planet = 0;
  if (ship_id <= 0
      || repo_citadel_get_onplanet (db, ship_id, &landed_planet) != 0
      || landed_planet != planet_id)
    {
      send_response_refused_steal (ctx, root, ERR_PERMISSION_DENIED,
                                   "Your active ship must be landed on this planet.",
                                   NULL);
      return 0;
    }

  /* The repository transaction revalidates ownership, landing, available
   * fighters, and ship capacity while holding the authoritative rows. */
  int planet_after = 0, ship_after = 0;
  int rc = repo_planets_transfer_fighters
    (db, ctx->player_id, ship_id, planet_id, direction, quantity,
     &planet_after, &ship_after);
  if (rc != 0)
    {
      send_response_error (ctx, root, rc,
                           "Fighter transfer rejected; no state was changed.");
      return 0;
    }
  json_t *response = json_object ();
  json_object_set_new (response, "planet_id", json_integer (planet_id));
  json_object_set_new (response, "direction", json_string (direction));
  json_object_set_new (response, "quantity", json_integer (quantity));
  json_object_set_new (response, "planet_fighters", json_integer (planet_after));
  json_object_set_new (response, "ship_fighters", json_integer (ship_after));
  send_response_ok_take (ctx, root, "planet.fighters.transferred_v1", &response);
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
  if (h_refuse_terra_mutation (game_db_get_handle (), ctx, root, planet_id))
    return 0;

  /* The command schema and protocol contract expose the planet name as
     `name`; keep the handler aligned with that authoritative contract. */
  const char *new_name = json_get_string_or_null (data, "name");


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


/* Resolve the existing planet-fighter/citadel combat rules for an unauthorized
 * landing attempt.  This uses the same strength comparison and citadel gates
 * as combat.attack_planet; it never changes planet ownership or grants access.
 */
static int
load_landing_citadel_defenses (db_t *db, int planet_id, int *level,
                               int *q_sector, int *q_atmosphere,
                               int *shields, int *reaction)
{
  if (!db || planet_id <= 0 || !level || !q_sector || !q_atmosphere
      || !shields || !reaction)
    return -1;
  *level = *q_sector = *q_atmosphere = *shields = *reaction = 0;
  /* Establish whether the citadel row exists separately from its active
   * defenses. A missing row is a legitimate unarmed planet; all other
   * repository failures must fail closed. */
  char status[32] = { 0 };
  int32_t target_level = 0;
  int64_t started_at = 0, completes_at = 0;
  int rc = repo_citadel_get_construction_state
    (db, planet_id, level, status, sizeof status, &target_level,
     &started_at, &completes_at);
  if (rc == ERR_DB_NOT_FOUND)
    {
      *level = 0;
      return 0;
    }
  if (rc != 0)
    return rc;
  bool has_defenses = false;
  if (db_planets_get_citadel_defenses_ex (db, planet_id, &has_defenses,
                                          level, shields, reaction) != 0)
    return -1;
  (void) has_defenses;
  *q_sector = 0;
  *q_atmosphere = 0;
  return 0;
}

static int
resolve_landing_fighter_defense (db_t *db, client_ctx_t *ctx, int planet_id,
                                 json_t **outcome)
{
  int sector = 0, owner = 0, planet_fighters = 0;
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  int ship_fighters = 0;
  int citadel_level = 0, citadel_shields = 0, reaction = 0;
  if (ship_id <= 0
      || db_planets_get_attack_info (db, planet_id, &sector, &owner,
                                     &planet_fighters) != 0
      || db_planets_get_ship_fighters (db, ship_id, &ship_fighters) != 0)
    {
      LOGE ("planet landing defense: failed to load planet/ship state (planet=%d ship=%d)",
            planet_id, ship_id);
      return -1;
    }
  (void) sector;
  (void) owner;
  int q_sector = 0, q_atmosphere = 0;
  if (load_landing_citadel_defenses (db, planet_id, &citadel_level,
                                     &q_sector, &q_atmosphere,
                                     &citadel_shields, &reaction) != 0)
    {
      LOGE ("planet landing defense: failed to load citadel state (planet=%d)",
            planet_id);
      return -1;
    }

  bool shield_grid_active = citadel_level >= 5 && citadel_shields > 0;
  if (planet_fighters <= 0 && !shield_grid_active)
    return 0;

  int fighters_after_shields = ship_fighters;
  int shield_absorbed = 0;
  int shields_after = citadel_shields;
  if (citadel_level >= 5 && citadel_shields > 0)
    {
      shield_absorbed = (fighters_after_shields < citadel_shields)
        ? fighters_after_shields : citadel_shields;
      fighters_after_shields -= shield_absorbed;
      shields_after -= shield_absorbed;
    }

  int effective_defenders = planet_fighters;
  if (citadel_level >= 2 && reaction > 0)
    {
      const int percent = reaction == 1 ? 125 : 150;
      effective_defenders = (int) floor ((double) planet_fighters
                                           * (double) percent / 100.0);
    }
  bool attacker_overran = fighters_after_shields > effective_defenders;
  int ship_loss = attacker_overran
    ? effective_defenders + shield_absorbed : ship_fighters;
  int planet_loss = attacker_overran ? planet_fighters : fighters_after_shields;
  if (ship_loss > ship_fighters)
    ship_loss = ship_fighters;
  if (planet_loss > planet_fighters)
    planet_loss = planet_fighters;

  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      LOGE ("planet landing defense: transaction begin failed (planet=%d code=%d message=%s)",
            planet_id, err.code, err.message);
      return -1;
    }
  int update_rc = 0;
  const char *failed_stage = NULL;
  if (ship_loss > 0
      && (update_rc = db_planets_update_ship_fighters
          (db, ship_id, ship_loss)) != 0)
    failed_stage = "ship fighters";
  else if (planet_loss > 0
           && (update_rc = db_planets_lose_fighters
               (db, planet_id, planet_loss)) != 0)
    failed_stage = "planet fighters";
  else if (shield_absorbed > 0
           && (update_rc = db_planets_consume_citadel_shields
               (db, planet_id, citadel_shields, shield_absorbed,
                &shields_after)) != 0)
    failed_stage = "citadel shields";
  if (failed_stage)
    {
      LOGE ("planet landing defense: %s update failed (planet=%d ship=%d code=%d)",
            failed_stage, planet_id, ship_id, update_rc);
      db_tx_rollback (db, NULL);
      return -1;
    }

  json_t *event = json_pack ("{s:i,s:i,s:i,s:i,s:i,s:i,s:b}",
                             "planet_id", planet_id,
                             "attacker_player_id", ctx->player_id,
                             "attacker_ship_id", ship_id,
                             "attacker_fighters_lost", ship_loss,
                             "planet_fighters_lost", planet_loss,
                             "citadel_shields_absorbed", shield_absorbed,
                             "attacker_overran", attacker_overran);
  if (event)
    {
      json_object_set_new (event, "attacker_fighters_remaining",
                           json_integer (ship_fighters - ship_loss));
      json_object_set_new (event, "planet_fighters_remaining",
                           json_integer (planet_fighters - planet_loss));
      json_object_set_new (event, "citadel_shields_remaining",
                           json_integer (shields_after));
      json_object_set_new (event, "result",
                           json_string (attacker_overran ? "overrun" : "repelled"));
    }
  if (!event
      || db_log_engine_event ((long long) time (NULL),
                              "combat.planet_landing_defense.v1",
                              "player", ctx->player_id, ctx->sector_id,
                              event, db) != 0)
    {
      LOGE ("planet landing defense: combat event insert failed (planet=%d player=%d)",
            planet_id, ctx->player_id);
      json_decref (event);
      db_tx_rollback (db, NULL);
      return -1;
    }
  if (!db_tx_commit (db, &err))
    {
      LOGE ("planet landing defense: transaction commit failed (planet=%d code=%d message=%s)",
            planet_id, err.code, err.message);
      json_decref (event);
      db_tx_rollback (db, NULL);
      return -1;
    }
  if (outcome)
    *outcome = event;
  else
    json_decref (event);
  return 1;
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

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data payload.");
      return 0;
    }

  /* Landing mutates the active ship's on-planet state.  Fail closed before
   * any defense resolution or turn decision when the player has no active
   * ship; player_land() otherwise has no row to update and could still
   * return success. */
  if (h_get_active_ship_id (db, ctx->player_id) <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND,
                           "No active ship found.");
      return 0;
    }
  int planet_id = 0;


  /* Treat malformed payloads exactly like a missing/invalid identifier.  Do
   * not let a failed unpack leave the default zero to flow into authoritative
   * lookups or any later turn-consuming path. */
  if (json_unpack (data, "{s:i}", "planet_id", &planet_id) != 0
      || planet_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_MISSING_FIELD,
			   "Missing or invalid 'planet_id'.");
      return 0;
    }

  // Check if player is in the same sector as the planet
  int player_sector = 0;


  /* Landing authorization is based on the authoritative persisted sector.
   * A failed lookup must not be treated as sector 0 (or as a valid match),
   * otherwise a transient database error could reach the turn transaction. */
  if (db_player_get_sector (db, ctx->player_id, &player_sector) != 0)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Could not determine your current sector.");
      return 0;
    }

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


  /* Terra is public Federation territory. This is a landing exception only:
   * the landing write records the ship location and does not change ownership. */
  if (h_is_terra (planet_id, planet_sector))
    {
      can_land = true;
    }
  else if (owner_id == 0)
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
      char role[64] = {0};
      can_land = owner_id > 0
        && repo_corp_check_member_role (db, owner_id, ctx->player_id,
                                        role, sizeof role) == 0;
    }

  /* Jurisdiction and Terra alignment are authoritative refusals.  Resolve
   * them before any hostile landing defense so a banned or Earth-refused
   * request cannot consume defensive assets or emit a combat event. */
  {
    int cluster_id = 0;
    int rc = repo_clusters_get_cluster_for_sector(db, planet_sector, &cluster_id);
    if (rc != 0)
      {
        free (owner_type);
        send_response_error (ctx, root, ERR_DB,
                             "Could not determine jurisdiction for landing.");
        return 0;
      }
    if (cluster_id > 0)
      {
        int is_banned = 0;
        if (repo_clusters_get_player_banned(db, cluster_id, ctx->player_id,
                                            &is_banned) != 0)
          {
            free (owner_type);
            send_response_error (ctx, root, ERR_DB,
                                 "Could not determine landing permissions.");
            return 0;
          }
        if (is_banned)
          {
            free (owner_type);
            send_response_error (ctx, root, ERR_BANNED_FROM_JURISDICTION,
                                "You are banned from this jurisdiction and cannot land.");
            return 0;
          }
      }
  }

  int alignment = 0;
  if (db_player_get_alignment (db, ctx->player_id, &alignment) != 0)
    {
      free (owner_type);
      send_response_error (ctx, root, ERR_DB,
                           "Could not determine alignment for landing.");
      return 0;
    }
  /* Terra is publicly inspectable and landable for ordinary/neutral players,
   * but classic TradeWars still refuses landing to evil players. */
  if (alignment < 0 && planet_sector == 1)
    {
      free (owner_type);
      send_response_refused_steal (ctx, root, REASON_EVIL_ALIGN,
                                   "Earth refuses landing to criminals.",
                                   NULL);
      return 0;
    }

  /* An unauthorized landing attempt is hostile. Resolve an existing
   * atmosphere Quasar defense before refusing access; friendly owners and
   * corporation members are exempt through the common hostility check. */
  int defense_rc = 0;
  json_t *defense_results = json_array ();
  if (!defense_results)
    {
      free (owner_type);
      send_response_error (ctx, root, ERR_MEMORY,
                           "Could not prepare planetary defense response.");
      return 0;
    }
  /* Match the existing combat protection: offensive planet defenses do not
   * fire in FedSpace (sectors 1-10), and Terra itself remains protected. */
  bool protected_sector = planet_sector >= 1 && planet_sector <= 10;
  if (!can_land && planet_id != 1 && !protected_sector)
    {
      json_t *quasar = NULL;
      defense_rc = h_trigger_atmosphere_quasar_report (db, ctx, planet_id,
                                                       &quasar);
      if (quasar)
        json_array_append_new (defense_results, quasar);
      if (defense_rc >= 0 && defense_rc != 2)
        {
          json_t *fighters = NULL;
          int fighter_rc = resolve_landing_fighter_defense
            (db, ctx, planet_id, &fighters);
          if (fighter_rc < 0)
            defense_rc = -1;
          else if (fighter_rc > 0)
            {
              if (fighters)
                json_array_append_new (defense_results, fighters);
              if (defense_rc == 0)
                defense_rc = 1;
            }
        }
    }
  if (defense_rc < 0)
    {
      free (owner_type);
      json_decref (defense_results);
      send_response_error (ctx, root, ERR_DB,
                           "Planetary defense could not resolve; landing refused.");
      return 0;
    }
  if (!can_land)
    {
      free (owner_type);
      json_t *error_data = NULL;
      if (defense_rc == 2)
        {
          error_data = json_pack ("{s:i,s:O,s:b,s:b}", "planet_id", planet_id,
                                  "combat", defense_results,
                                  "landing_allowed", false,
                                  "ship_remains_in_sector", false);
          send_response_error_data (ctx, root, ERR_PERMISSION_DENIED,
                                    "Planetary Quasar defenses destroyed your ship; landing refused.",
                                    error_data);
        }
      else if (defense_rc == 1)
        {
          error_data = json_pack ("{s:i,s:O,s:b,s:b}", "planet_id", planet_id,
                                  "combat", defense_results,
                                  "landing_allowed", false,
                                  "ship_remains_in_sector", true);
          send_response_error_data (ctx, root, ERR_PERMISSION_DENIED,
                                    "Planetary defenses engaged your ship; landing refused.",
                                    error_data);
        }
      else
        {
          error_data = json_pack ("{s:i,s:O,s:b,s:b}", "planet_id", planet_id,
                                  "combat", defense_results,
                                  "landing_allowed", false,
                                  "ship_remains_in_sector", true);
          send_response_error_data (ctx, root, ERR_PERMISSION_DENIED,
                                    "You do not have permission to land on this planet.",
                                    error_data);
        }
      if (error_data)
        json_decref (error_data);
      json_decref (defense_results);
      return 0;
    }
  json_decref (defense_results);
  free (owner_type);

  db_error_t txerr = {0};
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &txerr))
    {
      send_response_error (ctx, root, ERR_DB,
                           "Unable to begin landing transaction.");
      return 0;
    }

  /* All refusal checks precede this point. Consume the turn and persist the
   * landing in one transaction so a database failure cannot leave either
   * state half-applied. */
  /* Re-read the mutable landing predicates after taking the transaction
   * lock.  The checks above are needed for early refusal and defense
   * presentation, but cannot authorize a turn-consuming write after a
   * concurrent ownership transfer or player movement. */
  int tx_player_sector = 0;
  int tx_planet_sector = 0;
  int tx_owner_id = 0;
  int tx_ship_id = 0;
  char *tx_owner_type = NULL;
  if (db_player_get_sector (db, ctx->player_id, &tx_player_sector) != 0
      || db_planets_get_land_info (db, planet_id, &tx_planet_sector,
                                   &tx_owner_id, &tx_owner_type) != 0
      || (tx_ship_id = h_get_active_ship_id (db, ctx->player_id)) <= 0)
    {
      free (tx_owner_type);
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root,
                           tx_ship_id <= 0 ? ERR_SHIP_NOT_FOUND : ERR_DB,
                           tx_ship_id <= 0 ? "No active ship found."
                                           : "Could not revalidate landing state.");
      return 0;
    }
  bool tx_can_land = false;
  if (h_is_terra (planet_id, tx_planet_sector))
    tx_can_land = true;
  else if (tx_owner_id == 0)
    tx_can_land = true;
  else if (tx_owner_type && strcmp (tx_owner_type, "player") == 0)
    tx_can_land = tx_owner_id == ctx->player_id;
  else if (tx_owner_type
           && (strcmp (tx_owner_type, "corp") == 0
               || strcmp (tx_owner_type, "corporation") == 0))
    {
      char role[64] = {0};
      tx_can_land = repo_corp_check_member_role
        (db, tx_owner_id, ctx->player_id, role, sizeof role) == 0;
    }
  free (tx_owner_type);
  if (tx_player_sector != tx_planet_sector || !tx_can_land)
    {
      db_tx_rollback (db, NULL);
      send_response_error_data
        (ctx, root, ERR_PERMISSION_DENIED,
         "Landing authorization or location changed; landing refused.",
         json_pack ("{s:i,s:b}", "planet_id", planet_id,
                    "landing_allowed", false));
      return 0;
    }

  /* Revalidate policy gates under the same transaction as the turn and
   * landing write.  The preflight checks provide a fast refusal, but a ban
   * or alignment can change between preflight and the locked write. */
  int tx_cluster_id = 0;
  int tx_is_banned = 0;
  int tx_alignment = 0;
  if (repo_clusters_get_cluster_for_sector (db, tx_planet_sector,
                                             &tx_cluster_id) != 0
      || (tx_cluster_id > 0
          && repo_clusters_get_player_banned (db, tx_cluster_id,
                                              ctx->player_id,
                                              &tx_is_banned) != 0)
      || db_player_get_alignment (db, ctx->player_id, &tx_alignment) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB,
                           "Could not revalidate landing permissions.");
      return 0;
    }
  if (tx_is_banned
      || (tx_alignment < 0 && tx_planet_sector == 1))
    {
      db_tx_rollback (db, NULL);
      send_response_error_data
        (ctx, root,
         tx_is_banned ? ERR_BANNED_FROM_JURISDICTION : REASON_EVIL_ALIGN,
         tx_is_banned
           ? "You are banned from this jurisdiction and cannot land."
           : "Earth refuses landing to criminals.",
         json_pack ("{s:i,s:b}", "planet_id", planet_id,
                    "landing_allowed", false));
      return 0;
    }

  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);
  if (tc != TURN_CONSUME_SUCCESS)
    {
      db_tx_rollback (db, NULL);
      return handle_turn_consumption_error (ctx, tc, "planet.land", root,
                                            NULL);
    }

  if (db_player_land_on_planet (db, ctx->player_id, planet_id) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR, "Failed to land on planet.");
      return 0;
    }
  /* Keep the durable event journal in the same transaction as the turn and
   * landing state.  A successful response must never be observable without
   * its corresponding replay/acceptance event, and an event-write failure
   * must leave the landing state untouched. */
  json_t *landing_event = json_pack ("{s:i,s:i}",
                                     "player_id", ctx->player_id,
                                     "planet_id", planet_id);
  if (!landing_event
      || db_log_engine_event ((long long) time (NULL), "planet.land.v1",
                              "player", ctx->player_id, tx_planet_sector,
                              landing_event, db) != 0)
    {
      json_decref (landing_event);
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB,
                           "Landing event could not be recorded.");
      return 0;
    }
  json_decref (landing_event);
  if (!db_tx_commit (db, &txerr))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB,
                           "Landing transaction could not be committed.");
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

  /* Launch relocation and sector-entry hazards form one gameplay outcome. */
  db_error_t txerr;
  db_error_clear (&txerr);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &txerr))
    {
      send_response_error (ctx, root, ERR_DB_TX,
                           "Could not begin launch transaction");
      return 0;
    }
  int previous_sector = ctx->sector_id;

  int sector_id = 0;


  if (db_player_launch_from_planet (db, ctx->player_id, &sector_id) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR,
			   "Failed to launch from planet. Are you on a planet?");
      return 0;
    }

  // Update context
  ctx->sector_id = sector_id;

  /* Canon #471: Sector assets engage on entry */
  json_t *hazard_report = NULL;
  int hazard_rc = server_combat_apply_entry_hazards_report
    (db, ctx, sector_id, &hazard_report);
  if (hazard_rc < 0)
    {
      if (hazard_report)
        json_decref (hazard_report);
      db_tx_rollback (db, NULL);
      ctx->sector_id = previous_sector;
      send_response_error (ctx, root, ERR_DB,
                           "Launch could not resolve sector defenses; no launch was committed.");
      return 0;
    }
  if (hazard_rc == 1)
    {
      if (!db_tx_commit (db, &txerr))
        {
          if (hazard_report)
            json_decref (hazard_report);
          db_tx_rollback (db, NULL);
          ctx->sector_id = previous_sector;
          send_response_error (ctx, root, ERR_DB_TX,
                               "Launch aftermath could not be committed.");
          return 0;
        }
      json_t *data = json_object ();
      json_object_set_new (data, "sector_id", json_integer (sector_id));
      json_object_set_new (data, "ship_destroyed", json_true ());
      json_object_set_new (data, "hazards",
                           hazard_report ? hazard_report : json_array ());
      send_response_error_data (ctx, root, ERR_PERMISSION_DENIED,
                                "Ship destroyed by sector hazards on launch.",
                                data);
      json_decref (data);
      return 0;
    }

  if (!db_tx_commit (db, &txerr))
    {
      if (hazard_report)
        json_decref (hazard_report);
      db_tx_rollback (db, NULL);
      ctx->sector_id = previous_sector;
      send_response_error (ctx, root, ERR_DB_TX,
                           "Launch and entry hazards could not be committed.");
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Launched successfully."));
  json_object_set_new (response_data, "sector_id", json_integer (sector_id));
  if (hazard_report && json_array_size (hazard_report) > 0)
    json_object_set_new (response_data, "message",
                         json_string ("Launched into environmental hazards; your ship was damaged."));
  json_object_set_new (response_data, "hazards",
                       hazard_report ? hazard_report : json_array ());
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
  if (h_refuse_terra_mutation (game_db_get_handle (), ctx, root, planet_id))
    return 0;
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
  if (h_refuse_terra_mutation (db, ctx, root, planet_id))
    return 0;

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

  /* Phase 2: Verify commodity is in planet_goods (DB-driven whitelist) */
  char **allowed_codes = NULL;
  int allowed_count = 0;
  int cm_rc = repo_commodities_list_planet_transferable(db, planet_id, alignment, &allowed_codes, &allowed_count);
  if (cm_rc != 0) {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Could not query planet commodities.");
      return 0;
  }

  /* Check if requested commodity is in the allowed list */
  bool commodity_allowed = false;
  for (int i = 0; i < allowed_count; i++) {
      if (strcasecmp(commodity, allowed_codes[i]) == 0) {
          commodity_allowed = true;
          break;
      }
  }

  /* Cleanup allocated memory */
  for (int i = 0; i < allowed_count; i++) {
      free(allowed_codes[i]);
  }
  free(allowed_codes);

  if (!commodity_allowed) {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_INVALID_ARG, "Commodity not accepted by this planet.");
      return 0;
  }

  const char *target_commodity = commodity;
  /* Slave -> Colonist conversion for evil players */
  if ((strcasecmp (commodity, "SLAVES") == 0 || strcasecmp (commodity, "SLV") == 0) && alignment < 0)
    {
      target_commodity = "COLONISTS";
    }

  if (strcasecmp(target_commodity, "ORGANICS") == 0) target_commodity = "ORG";
  if (strcasecmp(target_commodity, "EQUIPMENT") == 0) target_commodity = "EQU";
  if (strcasecmp(target_commodity, "COLONISTS") != 0) {
      int current = 0, capacity = 0;
      if (db_planets_get_market_move_info(db, planet_id, target_commodity,
                                          &current, &capacity) != 0) {
          db_tx_rollback(db, NULL);
          send_response_error(ctx, root, ERR_INVALID_ARG, "Commodity is not configured for this planet.");
          return 0;
      }
      if (quantity > capacity - current) {
          db_tx_rollback(db, NULL);
          send_response_error(ctx, root, ERR_INVALID_ARG, "Planet storage capacity would be exceeded.");
          return 0;
      }
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
  else
    {
      prc = h_update_entity_stock(db, ENTITY_TYPE_PLANET, planet_id, target_commodity, quantity, NULL);
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
  if (h_refuse_terra_mutation (db, ctx, root, planet_id))
    return 0;

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
  else
    {
       const char *stock_code = commodity;
       if (strcasecmp(stock_code, "ORGANICS") == 0) stock_code = "ORG";
       if (strcasecmp(stock_code, "EQUIPMENT") == 0) stock_code = "EQU";
       int current = 0, capacity = 0;
       prc = db_planets_get_market_move_info(db, planet_id, stock_code,
                                             &current, &capacity);
       planet_qty = current;
       target_commodity = stock_code;
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
  else
    {
      prc = h_update_entity_stock(db, ENTITY_TYPE_PLANET, planet_id, target_commodity, -quantity, NULL);
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

  if (db_planets_consume_genesis (db, ship_id) != 0)
    {
      free (planet_name);
      return send_error_and_return (ctx,
				    root, ERR_DB, "Failed to consume genesis torpedo.");
    }

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
      char *request_fingerprint = json_dumps (data, JSON_COMPACT | JSON_SORT_KEYS);
      if (!payload_str || !request_fingerprint
	  || db_planets_insert_genesis_idem (db, idempotency_key,
					     request_fingerprint, payload_str,
					     current_unix_ts) != 0)
	LOGE ("Failed to persist Genesis idempotency record for planet %" PRId64,
	      new_planet_id);
      free (payload_str);
      free (request_fingerprint);
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
  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVICE_UNAVAILABLE,
			   "Database unavailable.");
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
  if (h_refuse_terra_mutation (db, ctx, root, planet_id))
    return 0;

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

  int capacity_stock = 0, planet_capacity = 0;
  if (db_planets_get_market_move_info(db, planet_id, commodity_code,
                                      &capacity_stock, &planet_capacity) != 0) {
      send_response_error(ctx, root, ERR_INVALID_ARG, "Commodity is not configured for this planet.");
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
  db_error_t txerr = {0};
  if (!db_tx_begin (db, DB_TX_DEFAULT, &txerr))
    {
      send_response_error (ctx, root, ERR_DB,
                           "Could not begin planet market sale.");
      return 0;
    }
  int stock_rc = db_ports_decrement_stock (db, ENTITY_TYPE_PLANET, planet_id,
                                           commodity_code, quantity,
                                           (int64_t) time (NULL));
  if (stock_rc != 0)
    {
      db_tx_rollback (db, NULL);
      if (stock_rc == ERR_DB_CONSTRAINT || stock_rc == ERR_DB_NOT_FOUND)
        send_response_refused_steal (ctx, root, REF_NO_WARP_LINK,
                                     "Insufficient inventory on planet.", NULL);
      else
        send_response_error (ctx, root, ERR_DB,
                             "Failed to update planet inventory.");
      return 0;
    }
  long long new_balance = 0;
  if (h_add_player_petty_cash (db, ctx->player_id, total_credits,
			       &new_balance) != 0)
    {
	  db_tx_rollback (db, NULL);
	  LOGE ("cmd_planet_market_sell: Failed to credit player %d.",
		ctx->player_id);
	  send_response_error (ctx, root, ERR_DB,
			       "Failed to credit sale proceeds.");
	  return 0;
    }

  if (planet_record_taxable_trade (db, planet_id, commodity_code, quantity,
                                   unit_price, "sell", root) != 0)
    {
      db_tx_rollback (db, NULL);
      LOGE ("cmd_planet_market_sell: failed to record taxable activity for planet %d",
	    planet_id);
      send_response_error (ctx, root, ERR_DB,
			   "Failed to record planet sale.");
      return 0;
    }

  if (!db_tx_commit (db, &txerr))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB,
			   "Could not commit planet market sale.");
      return 0;
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
  if (h_refuse_terra_mutation (db, ctx, root, planet_id))
    return 0;

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

  int capacity_stock = 0, planet_capacity = 0;
  if (db_planets_get_market_move_info(db, planet_id, commodity_code,
                                      &capacity_stock, &planet_capacity) != 0 ||
      quantity_total > planet_capacity - capacity_stock) {
      send_response_error(ctx, root, ERR_INVALID_ARG,
                          "Commodity is not configured or the order exceeds planet storage capacity.");
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
      if (capacity_stock < quantity_total)
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

      int settlement_rc = db_ports_decrement_stock
	(db, ENTITY_TYPE_PLANET, planet_id, commodity_code, quantity_total,
	 (int64_t) time (NULL));
      if (settlement_rc == 0)
	settlement_rc = h_update_ship_cargo (db, ship_id, commodity_code,
					    quantity_total, NULL);
      if (settlement_rc == 0)
	settlement_rc = h_deduct_player_petty_cash_unlocked
	  (db, ctx->player_id, cost, NULL);
      if (settlement_rc == 0)
	settlement_rc = db_planets_add_treasury_buy (db, planet_id, cost);
      if (settlement_rc != 0)
	{
	  db_tx_rollback (db, NULL);
	  if (settlement_rc == ERR_DB_CONSTRAINT
	      || settlement_rc == ERR_DB_NOT_FOUND)
	    send_response_refused_steal (ctx, root, REF_NO_WARP_LINK,
					 "Insufficient inventory for immediate illegal purchase.",
					 NULL);
	  else if (settlement_rc == ERR_HOLD_FULL)
	    send_response_refused_steal (ctx, root, ERR_HOLD_FULL,
					 "Insufficient cargo space.", NULL);
	  else
	    send_response_error (ctx, root, ERR_DB,
				 "Purchase settlement failed.");
	  return 0;
	}

      if (planet_record_taxable_trade (db, planet_id, commodity_code,
                                       quantity_total,
                                       (int)(cost / quantity_total), "buy",
                                       root) != 0)
	{
	  db_tx_rollback (db, NULL);
	  send_response_error (ctx, root, ERR_SERVER_ERROR,
	                       "Failed to record planetary trade activity.");
	  return 0;
	}

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
  if (!require_auth (ctx, root))
    return 0;

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

  int planet_id = (int) json_integer_value (json_object_get (data, "planet_id"));
  int quantity = (int) json_integer_value (json_object_get (data, "quantity"));
  const char *action = json_string_value (json_object_get (data, "action"));
  
  if (planet_id <= 0 || quantity <= 0 || !action)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Missing planet_id, quantity, or action");
      return 0;
    }

  /* Terra may only be used as a colonist source. Pickup is the sole
   * persistent action permitted there; drop-off and all other planet
   * mutations are refused below or by their respective handlers. */
  if (h_planet_is_terra (db, planet_id) && strcmp (action, "pickup") != 0)
    {
      send_response_error (ctx, root, ERR_PERMISSION_DENIED,
                           "Terra accepts colonist pickup only.");
      return 0;
    }

  if (h_planet_is_terra (db, planet_id) && strcmp (action, "pickup") == 0)
    {
      int landed_planet = 0;
      int ship_id_for_landing = h_get_active_ship_id (db, ctx->player_id);
      if (ship_id_for_landing <= 0
          || repo_citadel_get_onplanet (db, ship_id_for_landing,
                                        &landed_planet) != 0
          || landed_planet != planet_id)
        {
          send_response_error (ctx, root, ERR_PERMISSION_DENIED,
                               "You must be landed on Terra to pick up colonists.");
          return 0;
        }
    }

  /* Get active ship */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Player has no active ship");
      return 0;
    }

  if (strcmp(action, "pickup") == 0)
    {
      /* Pick up colonists from planet and add to ship */
      int64_t available = 0;
      if (db_planets_get_colonists_unassigned (db, planet_id, &available) != 0)
        {
          send_response_error (ctx, root, ERR_DB, "Database error");
          return 0;
        }

      if (available < quantity)
        {
          send_response_error (ctx, root, ERR_INSUFFICIENT_TURNS, 
                              "Not enough unassigned colonists on this planet");
          return 0;
        }

      /* Get ship cargo info */
      int ore, organics, equipment, colonists, slaves, weapons, drugs, holds;
      if (repo_ships_get_cargo_and_holds(db, ship_id, &ore, &organics, &equipment, 
                                         &colonists, &slaves, &weapons, &drugs, &holds) != 0)
        {
          send_response_error (ctx, root, ERR_DB, "Failed to get ship info");
          return 0;
        }

      /* Check hold capacity (colonists + current cargo) */
      int total_cargo = ore + organics + equipment + colonists + slaves + weapons + drugs;
      if (total_cargo + quantity > holds)
        {
          send_response_error (ctx, root, ERR_INSUFFICIENT_TURNS, "Not enough hold space");
          return 0;
        }

      /* Update ship colonists */
      if (repo_ships_update_cargo_column(db, ship_id, "colonists", colonists + quantity) != 0)
        {
          send_response_error (ctx, root, ERR_DB, "Failed to add colonists to ship");
          return 0;
        }

      /* Update planet colonists (subtract) */
      if (db_planets_add_colonists_unassigned (db, planet_id, -quantity) != 0)
        {
          send_response_error (ctx, root, ERR_DB, "Failed to remove colonists from planet");
          return 0;
        }

      json_t *payload = json_object ();
      json_object_set_new (payload, "action", json_string ("pickup"));
      json_object_set_new (payload, "quantity", json_integer (quantity));
      json_object_set_new (payload, "planet_id", json_integer (planet_id));
      json_object_set_new (payload, "ship_colonists", json_integer (colonists + quantity));
      send_response_ok_take (ctx, root, "planet.colonists.set", &payload);
    }
  else if (strcmp(action, "dropoff") == 0)
    {
      /* Get ship cargo info */
      int ore, organics, equipment, colonists, slaves, weapons, drugs, holds;
      if (repo_ships_get_cargo_and_holds(db, ship_id, &ore, &organics, &equipment, 
                                         &colonists, &slaves, &weapons, &drugs, &holds) != 0)
        {
          send_response_error (ctx, root, ERR_DB, "Failed to get ship info");
          return 0;
        }

      if (colonists < quantity)
        {
          send_response_error (ctx, root, ERR_INSUFFICIENT_TURNS, "Not enough colonists on ship");
          return 0;
        }

      /* Update ship colonists (subtract) */
      if (repo_ships_update_cargo_column(db, ship_id, "colonists", colonists - quantity) != 0)
        {
          send_response_error (ctx, root, ERR_DB, "Failed to remove colonists from ship");
          return 0;
        }

      /* Update planet colonists (add) */
      if (db_planets_add_colonists_unassigned (db, planet_id, quantity) != 0)
        {
          send_response_error (ctx, root, ERR_DB, "Failed to add colonists to planet");
          return 0;
        }

      json_t *payload = json_object ();
      json_object_set_new (payload, "action", json_string ("dropoff"));
      json_object_set_new (payload, "quantity", json_integer (quantity));
      json_object_set_new (payload, "planet_id", json_integer (planet_id));
      json_object_set_new (payload, "ship_colonists", json_integer (colonists - quantity));
      send_response_ok_take (ctx, root, "planet.colonists.set", &payload);
    }
  else
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Unknown action");
    }

  return 0;
}


int
cmd_planet_colonists_get (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;

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

  int planet_id = (int) json_integer_value (json_object_get (data, "planet_id"));
  
  if (planet_id <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Missing planet_id");
      return 0;
    }

  /* Get planet's unassigned colonists */
  int64_t planet_colonists = 0;
  if (db_planets_get_colonists_unassigned (db, planet_id, &planet_colonists) != 0)
    {
      send_response_error (ctx, root, ERR_DB, "Database error");
      return 0;
    }

  /* Get active ship */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Player has no active ship");
      return 0;
    }

  /* Get ship's colonist count and holds */
  int ore, organics, equipment, colonists, slaves, weapons, drugs, holds;
  if (repo_ships_get_cargo_and_holds(db, ship_id, &ore, &organics, &equipment, 
                                     &colonists, &slaves, &weapons, &drugs, &holds) != 0)
    {
      send_response_error (ctx, root, ERR_DB, "Failed to get ship info");
      return 0;
    }

  int total_cargo = ore + organics + equipment + colonists + slaves + weapons + drugs;
  int available_holds = holds - total_cargo;

  json_t *payload = json_object ();
  json_object_set_new (payload, "planet_id", json_integer (planet_id));
  json_object_set_new (payload, "planet_colonists", json_integer ((int) planet_colonists));
  json_object_set_new (payload, "ship_colonists", json_integer (colonists));
  json_object_set_new (payload, "ship_holds_available", json_integer (available_holds));
  send_response_ok_take (ctx, root, "planet.colonists.get", &payload);

  return 0;
}

int
cmd_planet_colonists_allocate (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  int planet_id = 0, ore = 0, organics = 0, equipment = 0, weapons = 0;
  if (!json_get_int_flexible (data, "planet_id", &planet_id) || planet_id <= 0
      || !json_get_int_flexible (data, "ore", &ore) || ore < 0
      || !json_get_int_flexible (data, "organics", &organics) || organics < 0
      || !json_get_int_flexible (data, "equipment", &equipment) || equipment < 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Provide planet_id and non-negative ore, organics, and equipment worker targets.");
      return 0;
    }
  if (h_refuse_terra_mutation (db, ctx, root, planet_id))
    return 0;
  json_t *weapons_value = json_object_get (data, "weapons");
  if (weapons_value
      && (!json_is_integer (weapons_value)
          || (weapons = (int) json_integer_value (weapons_value)) < 0))
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Weapons workers must be a non-negative integer.");
      return 0;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  int landed_planet_id = 0;
  if (ship_id <= 0 || repo_citadel_get_onplanet (db, ship_id, &landed_planet_id) != 0
      || landed_planet_id != planet_id)
    {
      send_response_refused_steal (ctx, root, ERR_PERMISSION_DENIED,
                                   "You must be landed on this planet to assign its colonists.", NULL);
      return 0;
    }

  db_error_t err;
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Planet is busy; try again.");
      return 0;
    }
  char sql[1024];
  const char *query =
    "SELECT p.owner_id, p.owner_type, p.colonists_unassigned, p.colonists_ore, p.colonists_org, p.colonists_eq, p.colonists_weapons, "
    "COALESCE((SELECT pt.maxColonist_ore FROM planettypes pt WHERE pt.planettypes_id=p.type),0), "
    "COALESCE((SELECT pt.maxColonist_organics FROM planettypes pt WHERE pt.planettypes_id=p.type),0), "
    "COALESCE((SELECT pt.maxColonist_equipment FROM planettypes pt WHERE pt.planettypes_id=p.type),0) "
    "FROM planets p WHERE p.planet_id={1} FOR UPDATE;";
  sql_build (db, query, sql, sizeof sql);
  db_res_t *res = NULL;
  if (!db_query (db, sql, (db_bind_t[]){db_bind_i64 (planet_id)}, 1, &res, &err)
      || !db_res_step (res, &err))
    {
      if (res) db_res_finalize (res);
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found.");
      return 0;
    }
  int owner_id = db_res_col_i32 (res, 0, &err);
  const char *owner_type_db = db_res_col_text (res, 1, &err);
  char *owner_type = owner_type_db ? strdup (owner_type_db) : NULL;
  int64_t unassigned = db_res_col_i64 (res, 2, &err);
  int64_t old_ore = db_res_col_i64 (res, 3, &err);
  int64_t old_org = db_res_col_i64 (res, 4, &err);
  int64_t old_eq = db_res_col_i64 (res, 5, &err);
  int64_t old_weapons = db_res_col_i64 (res, 6, &err);
  int max_ore = db_res_col_i32 (res, 7, &err);
  int max_org = db_res_col_i32 (res, 8, &err);
  int max_eq = db_res_col_i32 (res, 9, &err);
  db_res_finalize (res);
  if (!weapons_value)
    weapons = (int) old_weapons;

  bool allowed = owner_type && strcmp (owner_type, "player") == 0
    && owner_id == ctx->player_id;
  if (owner_type
      && (strcmp (owner_type, "corp") == 0
          || strcmp (owner_type, "corporation") == 0)
      && owner_id > 0)
    {
      char role[64] = {0};
      /* The session's cached corp_id can be stale after a kick or leave.
       * Recheck membership against PostgreSQL inside the allocation
       * transaction instead of trusting that cached identifier. */
      allowed = repo_corp_check_member_role (db, owner_id, ctx->player_id,
                                             role, sizeof role) == 0;
    }
  free (owner_type);
  if (!allowed)
    {
      db_tx_rollback (db, NULL);
      send_response_refused_steal (ctx, root, ERR_PERMISSION_DENIED,
                                   "You do not control this planet.", NULL);
      return 0;
    }
  if (ore > max_ore || organics > max_org || equipment > max_eq)
    {
      db_tx_rollback (db, NULL);
      json_t *details = json_object ();
      json_object_set_new (details, "max_ore_workers", json_integer (max_ore));
      json_object_set_new (details, "max_organics_workers", json_integer (max_org));
      json_object_set_new (details, "max_equipment_workers", json_integer (max_eq));
      json_object_set_new (details, "max_weapons_workers", json_integer (unassigned + old_ore + old_org + old_eq + old_weapons));
      send_response_refused_steal (ctx, root, ERR_INVALID_ARG,
                                   "Worker assignment exceeds this planet class capacity.", details);
      return 0;
    }
  int64_t next_unassigned = unassigned + old_ore + old_org + old_eq + old_weapons - ore - organics - equipment - weapons;
  if (next_unassigned < 0)
    {
      db_tx_rollback (db, NULL);
      send_response_refused_steal (ctx, root, ERR_INVALID_ARG,
                                   "There are not enough unassigned colonists for these jobs.", NULL);
      return 0;
    }
  const char *update = "UPDATE planets SET population=GREATEST(COALESCE(population,0), {1}+{2}+{3}+{4}+{5}), colonists_ore={1}, colonists_org={2}, colonists_eq={3}, colonists_weapons={4}, colonists_unassigned={5} WHERE planet_id={6};";
  sql_build (db, update, sql, sizeof sql);
  if (!db_exec (db, sql,
                (db_bind_t[]){db_bind_i64 (ore), db_bind_i64 (organics),
                              db_bind_i64 (equipment), db_bind_i64 (weapons),
                              db_bind_i64 (next_unassigned),
                              db_bind_i64 (planet_id)}, 6, &err)
      || !db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Could not save colonist assignments.");
      return 0;
    }
  json_t *payload = json_object ();
  json_object_set_new (payload, "planet_id", json_integer (planet_id));
  json_object_set_new (payload, "colonists_unassigned", json_integer ((json_int_t) next_unassigned));
  json_object_set_new (payload, "colonists_ore", json_integer (ore));
  json_object_set_new (payload, "colonists_org", json_integer (organics));
  json_object_set_new (payload, "colonists_eq", json_integer (equipment));
  json_object_set_new (payload, "colonists_weapons", json_integer (weapons));
  send_response_ok_take (ctx, root, "planet.colonists.allocate", &payload);
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

  if (db_planets_get_market_move_info
      (db, pid, code, &current_quantity, &max_capacity) != 0)
    {
      return ERR_NOT_FOUND;
    }

  /* Q46 returns the configured per-commodity planet_goods capacity. */

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
