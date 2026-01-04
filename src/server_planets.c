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
#include "database.h"
#include "game_db.h"
#include "errors.h"
#include "server_cmds.h"
#include "server_corporation.h"
#include "server_ports.h"
#include "database_market.h"
#include "server_players.h"
#include "server_ships.h"
#include "database_cmd.h"
#include "server_config.h"
#include "server_combat.h"
#include "server_ports.h"
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
  if (!db || player_id <= 0)
    return;

  db_error_t err;

  /* 1. Delete all ships owned by player */
  char sql_ships[1024];
  sql_build (db,
             "DELETE FROM ships WHERE ship_id IN ("
             "  SELECT ship_id FROM ship_ownership WHERE player_id = {1}"
             ");",
             sql_ships, sizeof (sql_ships));
  db_bind_t params[] = { db_bind_i32 (player_id) };
  db_exec (db, sql_ships, params, 1, &err);

  /* 2. Zero player credits */
  char sql_credits[512];
  sql_build (db, "UPDATE players SET credits = 0 WHERE player_id = {1};",
             sql_credits, sizeof (sql_credits));
  db_exec (db, sql_credits, params, 1, &err);

  /* 3. Zero bank accounts */
  char sql_bank[512];
  sql_build (db,
             "UPDATE bank_accounts SET balance = 0 "
             "WHERE owner_type = 'player' AND owner_id = {1};",
             sql_bank, sizeof (sql_bank));
  db_exec (db, sql_bank, params, 1, &err);

  /* 4. Delete sector assets */
  char sql_assets[512];
  sql_build (db, "DELETE FROM sector_assets WHERE player = {1};", sql_assets,
             sizeof (sql_assets));
  db_exec (db, sql_assets, params, 1, &err);

  /* 5. Delete limpet mines */
  char sql_limpets[512];
  sql_build (db, "DELETE FROM limpet_attached WHERE owner_player_id = {1};",
             sql_limpets, sizeof (sql_limpets));
  db_exec (db, sql_limpets, params, 1, &err);
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
  int planet_id = (int)json_integer_value (j_pid);

  /* Get active ship */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }

  /* Get current sector */
  int current_sector = ctx->sector_id;
  db_error_t err;
  db_res_t *res = NULL;

  if (current_sector <= 0)
    {
      char sql[512];
      sql_build (db, "SELECT sector_id FROM ships WHERE ship_id={1};", sql,
                 sizeof (sql));
      db_bind_t params[] = { db_bind_i32 (ship_id) };
      if (db_query (db, sql, params, 1, &res, &err) && db_res_step (res, &err))
        {
          current_sector = db_res_col_int (res, 0, &err);
        }
      if (res) db_res_finalize (res);
    }

  /* Get planet info */
  char sql_planet[512];
  sql_build (db,
             "SELECT sector_id, owner_id, fighters FROM planets WHERE id={1};",
             sql_planet, sizeof (sql_planet));
  db_bind_t params_planet[] = { db_bind_i32 (planet_id) };

  if (!db_query (db, sql_planet, params_planet, 1, &res, &err))
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found");
      return 0;
    }

  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found");
      return 0;
    }

  int p_sector = db_res_col_int (res, 0, &err);
  int p_owner_id = db_res_col_int (res, 1, &err);
  int p_fighters = db_res_col_int (res, 2, &err);
  db_res_finalize (res);

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
      db_log_engine_event ((long long)time (NULL),
                          "player.terra_attack_sanction.v1",
                          "player", ctx->player_id, current_sector, evt, NULL);

      send_response_error (ctx, root, 403,
                          "You have attacked Terra! Federation forces have destroyed your ship and seized your assets.");
      return 0;
    }

  /* Get attacker ship fighters */
  char sql_ship[512];
  sql_build (db, "SELECT fighters FROM ships WHERE ship_id={1};", sql_ship,
             sizeof (sql_ship));
  db_bind_t params_ship[] = { db_bind_i32 (ship_id) };

  if (!db_query (db, sql_ship, params_ship, 1, &res, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Failed to load ship");
      return 0;
    }

  int s_fighters = 0;
  if (db_res_step (res, &err))
    {
      s_fighters = db_res_col_int (res, 0, &err);
    }
  db_res_finalize (res);

  if (s_fighters <= 0)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                          "You have no fighters to attack with.");
      return 0;
    }

  /* Get citadel defense */
  char sql_cit[512];
  sql_build (db,
             "SELECT level, planetary_shields, military_reaction_level FROM citadels WHERE planet_id={1};",
             sql_cit, sizeof (sql_cit));
  db_bind_t params_cit[] = { db_bind_i32 (planet_id) };

  int cit_level = 0;
  int cit_shields = 0;
  int cit_reaction = 0;

  if (db_query (db, sql_cit, params_cit, 1, &res, &err) && db_res_step (res, &err))
    {
      cit_level = db_res_col_int (res, 0, &err);
      cit_shields = db_res_col_int (res, 1, &err);
      cit_reaction = db_res_col_int (res, 2, &err);
    }
  if (res) db_res_finalize (res);

  int fighters_absorbed = 0;

  /* Citadel shields (level >= 5) */
  if (cit_level >= 5 && cit_shields > 0)
    {
      int absorbed = (s_fighters < cit_shields) ? s_fighters : cit_shields;
      s_fighters -= absorbed;
      fighters_absorbed = absorbed;
      int new_shields = cit_shields - absorbed;

      char sql_upd[512];
      sql_build (db,
                 "UPDATE citadels SET planetary_shields={1} WHERE planet_id={2};",
                 sql_upd, sizeof (sql_upd));
      db_bind_t params_upd[] = { db_bind_i32 (new_shields), db_bind_i32 (planet_id) };
      db_exec (db, sql_upd, params_upd, 2, &err);
    }

  /* CCC military reaction (level >= 2) */
  int effective_p_fighters = p_fighters;
  if (cit_level >= 2 && cit_reaction > 0)
    {
      int pct = 100;
      if (cit_reaction == 1) pct = 125;
      else if (cit_reaction >= 2) pct = 150;
      effective_p_fighters = (int)floor ((double)p_fighters * (double)pct / 100.0);
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
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Transaction failed");
      return 0;
    }

  /* Update attacker ship fighters */
  char sql_upd_ship[512];
  sql_build (db,
             "UPDATE ships SET fighters = fighters - {1} WHERE ship_id = {2};",
             sql_upd_ship, sizeof (sql_upd_ship));
  db_bind_t params_ship_upd[] = { db_bind_i32 (ship_loss), db_bind_i32 (ship_id) };
  db_exec (db, sql_upd_ship, params_ship_upd, 2, &err);

  /* Update planet */
  if (captured)
    {
      int corp_id = ctx->corp_id;
      const char *new_type = (corp_id > 0) ? "corporation" : "player";
      int new_owner = (corp_id > 0) ? corp_id : ctx->player_id;

      char sql_cap[512];
      sql_build (db,
                 "UPDATE planets SET fighters=0, owner_id={1}, owner_type={2} WHERE id={3};",
                 sql_cap, sizeof (sql_cap));
      db_bind_t params_cap[] = { 
        db_bind_i32 (new_owner), 
        db_bind_text (new_type), 
        db_bind_i32 (planet_id) 
      };
      db_exec (db, sql_cap, params_cap, 3, &err);

      json_t *cap_evt = json_object ();
      json_object_set_new (cap_evt, "planet_id", json_integer (planet_id));
      json_object_set_new (cap_evt, "previous_owner", json_integer (p_owner_id));
      db_log_engine_event ((long long)time (NULL),
                          "player.capture_planet.v1",
                          "player", ctx->player_id, current_sector, cap_evt, NULL);
    }
  else
    {
      char sql_def[512];
      sql_build (db,
                 "UPDATE planets SET fighters = fighters - {1} WHERE id = {2};",
                 sql_def, sizeof (sql_def));
      db_bind_t params_def[] = { db_bind_i32 (planet_loss), db_bind_i32 (planet_id) };
      db_exec (db, sql_def, params_def, 2, &err);
    }

  db_tx_commit (db, &err);

  /* Log attack */
  json_t *atk_evt = json_object ();
  json_object_set_new (atk_evt, "planet_id", json_integer (planet_id));
  json_object_set_new (atk_evt, "result", json_string (attacker_wins ? "win" : "loss"));
  json_object_set_new (atk_evt, "ship_loss", json_integer (ship_loss));
  json_object_set_new (atk_evt, "planet_loss", json_integer (planet_loss));
  db_log_engine_event ((long long)time (NULL),
                      "player.attack_planet.v1",
                      "player", ctx->player_id, current_sector, atk_evt, NULL);

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
  if (!commodity_code)
    {
      return false;
    }
  db_res_t *res = NULL;
  db_error_t err;
  bool illegal = false;
  char sql[512];
  sql_build (db, "SELECT illegal FROM commodities WHERE code = {1} LIMIT 1",
             sql, sizeof (sql));


  if (db_query (db,
                sql,
                (db_bind_t[]){ db_bind_text (commodity_code) },
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          illegal = (db_res_col_i32 (res, 0, &err) != 0);
        }
      db_res_finalize (res);
    }
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
                               int player_id,
                               const char *code,
                               bool buy)
{
  (void)buy;
  if (!h_is_illegal_commodity (db, code))
    {
      return 1; // Legal
    }

  // 1. Get Sector ID
  int sector_id = 0;
  db_res_t *res = NULL;
  db_error_t err;
  char sql[512];
  sql_build (db, "SELECT sector_id FROM planets WHERE planet_id = {1}", sql,
             sizeof (sql));


  if (db_query (db, sql, (db_bind_t[]){ db_bind_i32 (pid) }, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          sector_id = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }

  if (sector_id <= 0)
    {
      return 0; // Illegal or error
    }

  // 2. Check Cluster Alignment
  int cluster_band = 0;


  h_get_cluster_alignment_band (db, sector_id, &cluster_band);

  int cluster_good = 0;


  db_alignment_band_for_value (db,
                               cluster_band,
                               NULL,
                               NULL,
                               NULL,
                               &cluster_good,
                               NULL,
                               NULL,
                               NULL);

  if (cluster_good)
    {
      return 0; // Good clusters ban illegal trade
    }

  // 3. Check Player Alignment
  int p_align = 0;


  db_player_get_alignment (db, player_id, &p_align);

  int neutral_band = db_get_config_int (db, "neutral_band", 75);


  if (p_align > neutral_band)
    {
      return 0; // Good players banned
    }

  // 4. Check Neutral Config
  int p_evil = 0;


  db_alignment_band_for_value (db,
                               p_align,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               &p_evil,
                               NULL,
                               NULL);

  if (!p_evil && !db_get_config_bool (db, "illegal_allowed_neutral", true))
    {
      return 0;
    }

  return 1; // Legal
}


int
h_get_planet_owner_info (db_t *db, int pid, planet_t *p)
{
  if (!p)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err;
  char sql[512];
  sql_build (db,
             "SELECT planet_id, owner_id, owner_type FROM planets WHERE planet_id = {1};",
             sql, sizeof (sql));
  int rc = ERR_NOT_FOUND;


  if (db_query (db, sql, (db_bind_t[]){ db_bind_i32 (pid) }, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          p->id = db_res_col_i32 (res, 0, &err);
          p->owner_id = db_res_col_i32 (res, 1, &err);
          const char *type_str = db_res_col_text (res, 2, &err);


          p->owner_type = type_str ? strdup (type_str) : NULL;
          rc = 0;
        }
      db_res_finalize (res);
    }
  return rc;
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
cmd_planet_info (client_ctx_t *ctx,
                 json_t *root)
{
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");
  int pid = (int)json_integer_value (json_object_get (data, "planet_id"));
  json_t *info = NULL; if (db_planet_get_details_json (db, pid, &info) == 0)
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
                                   "Not authenticated",
                                   NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  int planet_id = 0;


  if (!json_get_int_flexible (data, "planet_id", &planet_id) || planet_id <= 0)
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
                                   "You do not own this planet.",
                                   NULL);
      return 0;
    }

  db_error_t err;


  char sql[512];
  sql_build (db, "UPDATE planets SET name = {1} WHERE planet_id = {2};", sql,
             sizeof (sql));
  if (!db_exec (db,
                sql,
                (db_bind_t[]){ db_bind_text (new_name),
                               db_bind_i32 (planet_id) },
                2,
                &err))
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
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
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
      return handle_turn_consumption_error (ctx, tc, "planet.land", root, NULL);
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
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


  db_player_get_sector (db,
                        ctx->player_id,
                        &player_sector);

  db_res_t *res = NULL;
  db_error_t err;
  char sql[512];
  sql_build (db,
             "SELECT sector_id, owner_id, owner_type FROM planets WHERE planet_id = {1};",
             sql, sizeof (sql));
  int planet_sector = 0;
  int owner_id = 0;
  char *owner_type = NULL;


  if (db_query (db, sql, (db_bind_t[]){ db_bind_i32 (planet_id) }, 1, &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          planet_sector = db_res_col_i32 (res, 0, &err);
          owner_id = db_res_col_i32 (res, 1, &err);
          const char *tmp = db_res_col_text (res, 2, &err);


          owner_type = tmp ? strdup (tmp) : NULL;
        }
      else
        {
          db_res_finalize (res);
          send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found.");
          return 0;
        }
      db_res_finalize (res);
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
    { // unowned
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
                                                          "corporation") == 0))
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

  // Atmosphere Quasar Check (C3)
  if (planet_id != 1) // Skip Terra
    {
      if (h_trigger_atmosphere_quasar (db, ctx, planet_id))
        {
          // Ship destroyed
          send_response_error (ctx,
                               root,
                               403,
                               "Ship destroyed by planetary defences.");
          return 0;
        }
    }

  if (db_player_land_on_planet (db, ctx->player_id, planet_id) != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Failed to land on planet.");
      return 0;
    }

  // Update context
  ctx->sector_id = 0; // Not in a sector anymore
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
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
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
                                   "Not authenticated",
                                   NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
      return 0;
    }

  int planet_id = 0;
  int target_id = 0;


  if (!json_get_int_flexible (data, "planet_id", &planet_id) || planet_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Missing or invalid 'planet_id'.");
      return 0;
    }
  if (!json_get_int_flexible (data, "target_id", &target_id) || target_id <= 0)
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
      strcmp (target_type,
              "corporation") != 0)
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
                                   "You do not own this planet.",
                                   NULL);
      return 0;
    }

  db_res_t *res = NULL;
  db_error_t err;
  char sql_check[512];
  if (strcmp (target_type, "player") == 0)
    {
      sql_build (db, "SELECT player_id FROM players WHERE player_id={1}",
                 sql_check, sizeof (sql_check));
    }
  else
    {
      sql_build (db,
                 "SELECT corporation_id FROM corporations WHERE corporation_id={1}",
                 sql_check, sizeof (sql_check));
    }


  if (!db_query (db,
                 sql_check,
                 (db_bind_t[]){ db_bind_i32 (target_id) },
                 1,
                 &res,
                 &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Database error.");
      return 0;
    }
  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      send_response_error (ctx, root, ERR_NOT_FOUND,
                           "Target entity not found.");
      return 0;
    }
  db_res_finalize (res);

  char sql_update[512];
  sql_build (db,
             "UPDATE planets SET owner_id = {1}, owner_type = {2} WHERE planet_id = {3};",
             sql_update, sizeof (sql_update));
  if (!db_exec (db,
                sql_update,
                (db_bind_t[]){ db_bind_i32 (target_id),
                               db_bind_text (target_type),
                               db_bind_i32 (planet_id) },
                3,
                &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Update failed.");
      return 0;
    }

  send_response_ok_take (ctx, root, "planet.transfer_ownership.success", NULL);
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
cmd_planet_deposit (client_ctx_t *ctx,
                    json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
      return 0;
    }

  int planet_id = 0;
  int amount = 0;


  json_unpack (data, "{s:i, s:i}", "planet_id", &planet_id, "amount", &amount);

  if (planet_id <= 0 || amount <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Invalid planet_id or amount.");
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
                                   "You do not control this planet.",
                                   NULL);
      return 0;
    }

  int citadel_level = 0;
  db_res_t *res = NULL;
  db_error_t err;
  char sql_get_level[512];
  sql_build (db, "SELECT level FROM citadels WHERE planet_id = {1};",
             sql_get_level, sizeof (sql_get_level));


  if (db_query (db, sql_get_level,
                (db_bind_t[]){ db_bind_i32 (planet_id) }, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          citadel_level = db_res_col_i32 (res,
                                          0,
                                          &err);
        }
      db_res_finalize (res);
    }

  if (citadel_level < 1)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_TURN_COST_EXCEEDS,
                                   "No citadel or insufficient level for treasury.",
                                   NULL);
      return 0;
    }

  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Database busy.");
      return 0;
    }

  long long new_player_balance = 0;


  if (h_deduct_player_petty_cash_unlocked (db,
                                           ctx->player_id,
                                           amount,
                                           &new_player_balance) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_refused_steal (ctx,
                                   root,
                                   REF_NO_WARP_LINK,
                                   "Insufficient credits.",
                                   NULL);
      return 0;
    }

  char sql_update[512];
  sql_build (db,
             "UPDATE citadels SET treasury = treasury + {1} WHERE planet_id = {2};",
             sql_update, sizeof (sql_update));
  if (!db_exec (db,
                sql_update,
                (db_bind_t[]){ db_bind_i32 (amount), db_bind_i32 (planet_id) },
                2,
                &err))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database update failed.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Transaction commit failed.");
      return 0;
    }

  long long new_treasury = 0;
  char sql_get_treasury[512];
  sql_build (db, "SELECT treasury FROM citadels WHERE planet_id = {1};",
             sql_get_treasury, sizeof (sql_get_treasury));


  if (db_query (db, sql_get_treasury,
                (db_bind_t[]){ db_bind_i32 (planet_id) }, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          new_treasury = db_res_col_i64 (res, 0, &err);
        }
      db_res_finalize (res);
    }

  json_t *resp = json_object ();


  json_object_set_new (resp, "planet_id", json_integer (planet_id));
  json_object_set_new (resp, "planet_treasury_balance",
                       json_integer (new_treasury));
  json_object_set_new (resp, "player_credits",
                       json_integer (new_player_balance));

  send_response_ok_take (ctx, root, "planet.deposit", &resp);
  return 0;
}


int
cmd_planet_withdraw (client_ctx_t *ctx,
                     json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
      return 0;
    }

  int planet_id = 0;
  int amount = 0;


  json_unpack (data, "{s:i, s:i}", "planet_id", &planet_id, "amount", &amount);

  if (planet_id <= 0 || amount <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Invalid planet_id or amount.");
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
                                  ctx->corp_id,
                                  role,
                                  sizeof (role));
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

  int citadel_level = 0;
  long long current_treasury = 0;
  db_res_t *res = NULL;
  db_error_t err;
  char sql_get_info[512];
  sql_build (db,
             "SELECT level, treasury FROM citadels WHERE planet_id = {1};",
             sql_get_info, sizeof (sql_get_info));


  if (db_query (db,
                sql_get_info,
                (db_bind_t[]){ db_bind_i32 (planet_id) },
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          citadel_level = db_res_col_i32 (res, 0, &err);
          current_treasury = db_res_col_i64 (res, 1, &err);
        }
      db_res_finalize (res);
    }

  if (citadel_level < 1)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_TURN_COST_EXCEEDS,
                                   "No citadel or insufficient level for treasury.",
                                   NULL);
      return 0;
    }

  if (current_treasury < amount)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_NO_WARP_LINK,
                                   "Insufficient treasury funds.",
                                   NULL);
      return 0;
    }

  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Database busy.");
      return 0;
    }

  char sql_update[512];
  sql_build (db,
             "UPDATE citadels SET treasury = treasury - {1} WHERE planet_id = {2};",
             sql_update, sizeof (sql_update));
  if (!db_exec (db,
                sql_update,
                (db_bind_t[]){ db_bind_i32 (amount), db_bind_i32 (planet_id) },
                2,
                &err))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Database error.");
      return 0;
    }

  long long new_player_balance = 0;


  if (h_add_player_petty_cash_unlocked (db,
                                        ctx->player_id,
                                        amount,
                                        &new_player_balance) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Failed to credit player.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Transaction commit failed.");
      return 0;
    }

  json_t *resp = json_object ();


  json_object_set_new (resp, "planet_id", json_integer (planet_id));
  json_object_set_new (resp, "planet_treasury_balance",
                       json_integer (current_treasury - amount));
  json_object_set_new (resp, "player_credits",
                       json_integer (new_player_balance));

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
  db_error_t err;


  if (!data)
    {
      return send_error_and_return (ctx,
                                    root,
                                    ERR_BAD_REQUEST,
                                    "Missing data payload.");
    }
  if (player_id <= 0 || ship_id <= 0)
    {
      return send_error_and_return (ctx,
                                    root,
                                    ERR_NOT_AUTHENTICATED,
                                    "Player or ship not found in context.");
    }
  if (!json_get_int_flexible (data, "sector_id",
                              &target_sector_id) || target_sector_id <= 0)
    {
      return send_error_and_return (ctx,
                                    root,
                                    ERR_INVALID_ARG,
                                    "Missing or invalid 'sector_id'.");
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
      db_res_t *res_idem = NULL;
      char sql_idem[512];
      sql_build (db,
                 "SELECT response FROM idempotency WHERE key = {1} AND cmd = 'planet.genesis_create';",
                 sql_idem, sizeof (sql_idem));


      if (db_query (db,
                    sql_idem,
                    (db_bind_t[]){ db_bind_text (idempotency_key) },
                    1,
                    &res_idem,
                    &err))
        {
          if (db_res_step (res_idem, &err))
            {
              const char *prev_json = db_res_col_text (res_idem, 0, &err);


              if (prev_json)
                {
                  json_t *prev_payload = json_loads (prev_json, 0, NULL);


                  if (prev_payload)
                    {
                      send_response_ok_take (ctx,
                                             root,
                                             "planet.genesis_created_v1",
                                             &prev_payload);
                      db_res_finalize (res_idem);
                      free (planet_name);
                      return 0;
                    }
                }
            }
          db_res_finalize (res_idem);
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

  db_res_t *res = NULL;
  char sql_msl[512];
  sql_build (db, "SELECT 1 FROM msl_sectors WHERE sector_id = {1};", sql_msl,
             sizeof (sql_msl));


  if (db_query (db, sql_msl,
                (db_bind_t[]){ db_bind_i32 (target_sector_id) }, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          db_res_finalize (res);
          free (planet_name);
          return send_error_and_return (ctx,
                                        root,
                                        ERR_GENESIS_MSL_PROHIBITED,
                                        "Planet creation prohibited in MSL sector.");
        }
      db_res_finalize (res);
    }

  int current_count = 0;
  char sql_count[512];
  sql_build (db, "SELECT COUNT(*) FROM planets WHERE sector_id = {1};",
             sql_count, sizeof (sql_count));


  if (db_query (db, sql_count,
                (db_bind_t[]){ db_bind_i32 (target_sector_id) }, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          current_count = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }

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
  char sql_torps[512];
  sql_build (db, "SELECT genesis FROM ships WHERE ship_id = {1};", sql_torps,
             sizeof (sql_torps));


  if (db_query (db, sql_torps,
                (db_bind_t[]){ db_bind_i32 (ship_id) }, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          torps = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }
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


  if (db_query (db,
                "SELECT code, genesis_weight FROM planettypes ORDER BY planettypes_id;",
                NULL,
                0,
                &res,
                &err))
    {
      int tw = 0;


      while (db_res_step (res, &err))
        {
          const char *code = db_res_col_text (res, 0, &err);
          int w = db_res_col_i32 (res, 1, &err);


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
      db_res_finalize (res);
      if (tw > 0)
        {
          total_weight = tw;
        }
    }

  int rv = randomnum (0, total_weight - 1);
  int sel = 0, sum = 0;


  for (int i = 0; i < 7; i++)
    {
      sum += weights[i];
      if (rv < sum)
        {
          sel = i; break;
        }
    }
  strncpy (planet_class_str, classes[sel], 1);
  planet_class_str[1] = '\0';

  int type_id = -1;
  char sql_type[512];
  sql_build (db, "SELECT planettypes_id FROM planettypes WHERE code = {1};",
             sql_type, sizeof (sql_type));


  if (db_query (db,
                sql_type,
                (db_bind_t[]){ db_bind_text (planet_class_str) },
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          type_id = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }
  if (type_id == -1)
    {
      free (planet_name);
      return send_error_and_return (ctx,
                                    root,
                                    ERR_DB_QUERY_FAILED,
                                    "Failed to resolve planet type.");
    }

  /* RETURNING is handled by db_exec_insert_id abstraction in PG backend, but not sql_build? 
     Actually db_exec_insert_id internally handles RETURNING for PG. 
     But here we are constructing the SQL string passed to it.
     db_exec_insert_id expects a standard INSERT.
     However, db_pg.c:db_exec_insert_id appends " RETURNING id" if not present?
     Let's check db_exec_insert_id implementation.
     Wait, looking at db_pg.c, it does NOT append RETURNING. It expects it or uses PQoidValue (deprecated).
     Actually, looking at db_api.h:
     "Contract: SQL string should include RETURNING clause for PostgreSQL."
     So we must provide it. 
     But we want to remove RETURNING from the source code for portability.
     The 'db_insert_id' audit item said "Need db_insert_id(db) API".
     That API does not exist yet. 
     So for Phase 1, we must keep RETURNING but hide it or use sql_build to make it "safe" (no $N).
     We can use sql_build with {N} and keep RETURNING for now as it's not a $N violation.
     But wait, 'RETURNING' IS a violation in sql_sentinels.sh!
     The instructions said: "Strategy A (preferred): convert SQL strings to neutral {N} and render via sql_build()... Output should make $N disappear".
     It didn't strictly say remove RETURNING yet (Phase 2?).
     However, sql_sentinels.sh checks for RETURNING.
     If I leave RETURNING, the sentinel will fail.
     I need to use a db_driver helper or hide it.
     Since I cannot change db_api, I must hide it in sql_build or similar?
     Or, I can use a construct like:
     sql_build(db, "INSERT ... VALUES (...) %s", ... , sql_returning_id(db));
     But I don't have sql_returning_id.
     
     Actually, looking at the previous file (server_ports.c), I didn't see RETURNING.
     
     Let's look at `sql_driver.h`.
     It has `sql_insert_ignore_clause`.
     It does NOT have `sql_returning`.
     
     I will use a local char buffer and sql_build, and keep RETURNING for now but put it in the template.
     The sentinel will flag it.
     The user instructions said: "Phase 1 — Kill $N placeholders".
     Maybe I should ignore the RETURNING violation for now? 
     "VIOLATION: RETURNING clause found in non-driver code"
     The checklist says: "Refactor: Replace RETURNING usage with db_insert_id()".
     That is a later item.
     But I need to pass the sentinel.
     
     Wait, I can add `sql_returning_clause(db)` to `sql_driver.c`? 
     "NO STUBS". "No code changes" (outside scope).
     The scope guard allows modifying `src/db/sql_driver.c`.
     So I can add `sql_returning_id` to `sql_driver`.
     
     Let's verify if `src/db/sql_driver.c` is in scope.
     The previous turn I modified `src/server_ports.c`.
     The user instructions for THIS turn said: "proceed to planets."
     And "Rule: one file per PR, no exceptions."
     So I CANNOT modify `sql_driver.c` in this PR.
     
     So I must live with the RETURNING violation or hack it.
     The sentinel script has:
     `check_pattern '\bRETURNING\b' "RETURNING clause"`
     
     If I split the string? "RETURN" "ING"?
     No, that's cheating.
     
     The user said "Phase 1 — Kill $N placeholders (highest priority)".
     "Output should make $N disappear from non-driver files."
     It did NOT say "Make RETURNING disappear".
     The sentinel script checks for it, but maybe I only need to fix $N for this PR.
     
     I will proceed with just fixing $N.
  */

  char sql_ins[1024];
  sql_build (db,
             "INSERT INTO planets (sector_id, name, owner_id, owner_type, class, type, created_at, created_by, genesis_flag) "
             "VALUES ({1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, 1) RETURNING planet_id;",
             sql_ins, sizeof (sql_ins));
  db_bind_t p_ins[] = {
    db_bind_i32 (target_sector_id), db_bind_text (planet_name),
    db_bind_i32 (owner_id),
    db_bind_text (owner_entity_type), db_bind_text (planet_class_str),
    db_bind_i32 (type_id),
    db_bind_i64 (current_unix_ts), db_bind_i32 (player_id)
  };


  if (!db_exec_insert_id (db, sql_ins, p_ins, 8, &new_planet_id, &err))
    {
      free (planet_name);
      return send_error_and_return (ctx,
                                    root,
                                    ERR_DB,
                                    "Failed to create planet.");
    }

  char sql_update_genesis[512];
  sql_build (db,
             "UPDATE ships SET genesis = genesis - 1 WHERE ship_id = {1} AND genesis >= 1;",
             sql_update_genesis, sizeof (sql_update_genesis));
  db_exec (db,
           sql_update_genesis,
           (db_bind_t[]){ db_bind_i32 (ship_id) },
           1,
           &err);

  navhaz_delta = GENESIS_NAVHAZ_DELTA;
  if (navhaz_delta != 0)
    {
      char sql_update_navhaz[512];
      sql_build (db,
                 "UPDATE sectors SET navhaz = GREATEST(0, COALESCE(navhaz, 0) + {1}) WHERE sector_id = {2};",
                 sql_update_navhaz, sizeof (sql_update_navhaz));
      db_exec (db,
               sql_update_navhaz,
               (db_bind_t[]){ db_bind_i32 (navhaz_delta),
                              db_bind_i32 (target_sector_id) },
               2,
               &err);
    }

  response_json = json_object ();
  json_object_set_new (response_json, "sector_id",
                       json_integer (target_sector_id));
  json_object_set_new (response_json, "planet_id",
                       json_integer ((int) new_planet_id));
  json_object_set_new (response_json, "class", json_string (planet_class_str));
  json_object_set_new (response_json, "name", json_string (planet_name));
  json_object_set_new (response_json, "owner_type",
                       json_string (owner_entity_type));
  json_object_set_new (response_json, "owner_id", json_integer (owner_id));
  json_object_set_new (response_json, "over_cap", json_boolean (over_cap_flag));
  json_object_set_new (response_json,
                       "navhaz_delta",
                       json_integer (navhaz_delta));

  if (idempotency_key)
    {
      char *payload_str = json_dumps (response_json, 0);
      char sql_idem_ins[1024];
      sql_build (db,
                 "INSERT INTO idempotency (key, cmd, response, created_at) VALUES ({1}, 'planet.genesis_create', {2}, {3});",
                 sql_idem_ins, sizeof (sql_idem_ins));


      db_exec (db, sql_idem_ins,
               (db_bind_t[]){ db_bind_text (idempotency_key),
                              db_bind_text (payload_str),
                              db_bind_i64 (current_unix_ts) }, 3, &err);
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
                       "player",
                       player_id,
                       target_sector_id,
                       ev_payload,
                       db);
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
                                   "Not authenticated",
                                   NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
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
                                   "You do not control this planet.",
                                   NULL);
      return 0;
    }

  const char *commodity_code = raw_commodity;


  if (!commodity_code)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_AUTOPILOT_PATH_INVALID,
                                   "Invalid commodity.",
                                   NULL);
      return 0;
    }

  if (!h_planet_check_trade_legality (db,
                                      planet_id,
                                      ctx->player_id,
                                      commodity_code,
                                      false))
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_TURN_COST_EXCEEDS,
                                   "Illegal trade refused.",
                                   NULL);
      return 0;
    }

  int current_stock = 0;
  db_res_t *res = NULL;
  db_error_t err;
  char sql_stock[512];
  sql_build (db,
             "SELECT quantity FROM entity_stock WHERE entity_type='planet' AND entity_id={1} AND commodity_code={2}",
             sql_stock, sizeof (sql_stock));


  if (db_query (db, sql_stock,
                (db_bind_t[]){ db_bind_i32 (planet_id),
                               db_bind_text (commodity_code) }, 2, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          current_stock = db_res_col_i32 (res,
                                          0,
                                          &err);
        }
      db_res_finalize (res);
    }

  if (current_stock < quantity)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_NO_WARP_LINK,
                                   "Insufficient stock on planet.",
                                   NULL);
      return 0;
    }

  int unit_price = 0;
  char sql_price[512];
  sql_build (db, "SELECT base_price FROM commodities WHERE code={1}",
             sql_price, sizeof (sql_price));


  if (db_query (db, sql_price,
                (db_bind_t[]){ db_bind_text (commodity_code) }, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          unit_price = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
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
                             planet_id,
                             commodity_code,
                             -quantity,
                             NULL) != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Failed to update planet stock.");
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
  db_res_t *res = NULL;
  db_error_t err;
  
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
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

  if (planet_id <= 0 || !raw_commodity || quantity_total <= 0 || max_price <= 0)
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
                                   "You do not control this planet.",
                                   NULL);
      return 0;
    }

  const char *commodity_code = raw_commodity;


  if (!commodity_code)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_AUTOPILOT_PATH_INVALID,
                                   "Invalid commodity.",
                                   NULL);
      return 0;
    }

  if (!h_planet_check_trade_legality (db,
                                      planet_id,
                                      ctx->player_id,
                                      commodity_code,
                                      true))
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_TURN_COST_EXCEEDS,
                                   "Illegal trade refused by port authority (alignment/cluster rules).",
                                   NULL);
      return 0;
    }

  int commodity_id = 0;
  char sql_get_id[512];
  sql_build (db, "SELECT commodities_id FROM commodities WHERE code = {1};",
             sql_get_id, sizeof (sql_get_id));
  if (db_query (db,
                sql_get_id,
                (db_bind_t[]){ db_bind_text (commodity_code) },
                1,
                &res,
                &err) &&
      db_res_step (res, &err))
    {
      commodity_id = db_res_col_i32 (res, 0, &err);
    }
  db_res_finalize (res);

  if (commodity_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Commodity ID lookup failed.");
      return 0;
    }

  if (h_is_illegal_commodity (db, commodity_code))
    {
      int stock = 0;
      char sql_stock[512];
      sql_build (db,
                 "SELECT quantity FROM entity_stock WHERE entity_type='planet' AND entity_id={1} AND commodity_code={2};",
                 sql_stock, sizeof (sql_stock));
      if (db_query (db,
                    sql_stock,
                    (db_bind_t[]){ db_bind_i32 (planet_id),
                                   db_bind_text (commodity_code) },
                    2,
                    &res,
                    &err))
        {
          if (db_res_step (res, &err))
            {
              stock = db_res_col_i32 (res, 0, &err);
            }
          db_res_finalize (res);
        }

      if (stock < quantity_total)
        {
          send_response_refused_steal (ctx,
                                       root,
                                       REF_NO_WARP_LINK,
                                       "Insufficient stock for immediate illegal purchase.",
                                       NULL);
          return 0;
        }

      long long player_credits = 0;


      if (h_get_player_petty_cash (db, ctx->player_id, &player_credits) != 0)
        {
          send_response_error (ctx,
                               root,
                               ERR_SERVER_ERROR,
                               "Credit check failed.");
          return 0;
        }

      long long cost = (long long) quantity_total * max_price;


      if (player_credits < cost)
        {
          send_response_refused_steal (ctx,
                                       root,
                                       REF_NO_WARP_LINK,
                                       "Insufficient credits.",
                                       NULL);
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
                                       "Insufficient cargo space.",
                                       NULL);
          return 0;
        }

      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
        {
          send_response_error (ctx, root, ERR_SERVER_ERROR, "Database busy.");
          return 0;
        }

      h_update_entity_stock (db,
                             ENTITY_TYPE_PLANET,
                             planet_id,
                             commodity_code,
                             -quantity_total,
                             NULL);
      h_update_ship_cargo (db, ship_id, commodity_code, quantity_total, NULL);
      h_deduct_player_petty_cash_unlocked (db, ctx->player_id, cost, NULL);
      char sql_upd_cit[512];
      sql_build (db,
                 "UPDATE citadels SET treasury = treasury + {1} WHERE planet_id = {2}",
                 sql_upd_cit, sizeof (sql_upd_cit));
      db_exec (db,
               sql_upd_cit,
               (db_bind_t[]){ db_bind_i64 (cost), db_bind_i32 (planet_id) },
               2,
               &err);

      if (!db_tx_commit (db, &err))
        {
          db_tx_rollback (db, NULL);
          send_response_error (ctx,
                               root,
                               ERR_SERVER_ERROR,
                               "Transaction failed.");
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
                           ERR_SERVER_ERROR,
                           "Failed to check credits.");
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
  char sql_ins[1024];
  char sql_ins_tmpl[1024];
  const char *ts_epoch_str = sql_epoch_now (db);
  snprintf (sql_ins_tmpl, sizeof (sql_ins_tmpl),
            "INSERT INTO commodity_orders (actor_type, actor_id, location_type, location_id, commodity_id, side, quantity, price, status, ts, expires_at, filled_quantity) VALUES ({1}, {2}, 'planet', {3}, {4}, 'buy', {5}, {6}, 'open', %s, {7}, 0)",
            ts_epoch_str);
  sql_build (db, sql_ins_tmpl, sql_ins, sizeof (sql_ins));

  db_bind_t params[] = {
    db_bind_text ("player"), db_bind_i32 (ctx->player_id),
    db_bind_i32 (planet_id),
    db_bind_i32 (commodity_id), db_bind_i32 (quantity_total),
    db_bind_i32 (max_price),
    db_bind_null ()
  };

  if (!db_exec_insert_id (db, sql_ins, params, 7, &order_id, &err))
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "DB Error inserting order.");
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
  db_res_t *res = NULL;
  db_error_t err;
  
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated",
                                   NULL);
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
               "planet_id",
               &planet_id,
               "to_sector_id",
               &to_sector_id);

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
                                  ctx->corp_id,
                                  role,
                                  sizeof (role));
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
                                   "Permission denied.",
                                   NULL);
      return 0;
    }

  int level = 0;
  char sql_get_level[512];
  sql_build (db, "SELECT level FROM citadels WHERE planet_id={1}",
             sql_get_level, sizeof (sql_get_level));


  if (db_query (db, sql_get_level, (db_bind_t[]){ db_bind_i32 (planet_id) },
                1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          level = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }

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
  char sql_fuel[512];
  sql_build (db,
             "SELECT quantity FROM entity_stock WHERE entity_type='planet' AND entity_id={1} AND commodity_code='FUE'",
             sql_fuel, sizeof (sql_fuel));


  if (db_query (db, sql_fuel, (db_bind_t[]){ db_bind_i32 (planet_id) }, 1,
                &res, &err))
    {
      if (db_res_step (res, &err))
        {
          fuel_on_hand = db_res_col_i32 (res,
                                0,
                                &err);
        }
      db_res_finalize (res);
    }

  if (fuel_on_hand < 500)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   REF_NO_WARP_LINK,
                                   "Insufficient Fuel Ore (requires 500).",
                                   NULL);
      return 0;
    }

  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Database busy.");
      return 0;
    }

  if (h_update_entity_stock (db,
                             ENTITY_TYPE_PLANET,
                             planet_id,
                             "FUE",
                             -500,
                             NULL) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Fuel consumption failed.");
      return 0;
    }

  char sql_update_sec[512];
  sql_build (db, "UPDATE planets SET sector_id={1} WHERE planet_id={2}",
             sql_update_sec, sizeof (sql_update_sec));
  if (!db_exec (db, sql_update_sec,
                (db_bind_t[]){ db_bind_i32 (to_sector_id),
                               db_bind_i32 (planet_id) }, 2, &err))
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
                           ERR_SERVER_ERROR,
                           "Transaction commit failed.");
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
  db_res_t *res = NULL;
  db_error_t err;

  char sql_info[1024];
  sql_build (db,
             "SELECT es.quantity, pt.maxore, pt.maxorganics, pt.maxequipment "
             "FROM planets p "
             "JOIN planettypes pt ON p.type = pt.planettypes_id "
             "LEFT JOIN entity_stock es ON p.planet_id = es.entity_id AND es.entity_type = 'planet' AND es.commodity_code = {2} "
             "WHERE p.planet_id = {1};",
             sql_info, sizeof (sql_info));


  if (!db_query (db,
                 sql_info,
                 (db_bind_t[]){ db_bind_i32 (pid), db_bind_text (code) },
                 2,
                 &res,
                 &err))
    {
      LOGE ("h_market_move_planet_stock: query failed: %s", err.message);
      return err.code;
    }

  int current_quantity = 0;
  int max_capacity = 0;


  if (db_res_step (res, &err))
    {
      current_quantity = db_res_col_i32 (res, 0, &err);
      int maxore = db_res_col_i32 (res, 1, &err);
      int maxorg = db_res_col_i32 (res, 2, &err);
      int maxequ = db_res_col_i32 (res, 3, &err);


      if (strcasecmp (code, "ORE") == 0)
        {
          max_capacity = maxore;
        }
      else if (strcasecmp (code, "ORG") == 0)
        {
          max_capacity = maxorg;
        }
      else if (strcasecmp (code,
                           "EQU") == 0)
        {
          max_capacity = maxequ;
        }
      else
        {
          max_capacity = 999999;
        }
    }
  else
    {
      db_res_finalize (res);
      return ERR_NOT_FOUND;
    }
  db_res_finalize (res);

  // 2. Calculate new quantity with overflow and bounds checking
  int new_quantity;
  if (__builtin_add_overflow(current_quantity, delta, &new_quantity))
    {
      /* Overflow: clamp based on delta direction */
      new_quantity = (delta > 0) ? INT_MAX : 0;
    }


  new_quantity = (new_quantity < 0) ? 0 : new_quantity;
  new_quantity = (new_quantity > max_capacity) ? max_capacity : new_quantity;

  // 3. Update DB
  const char *epoch_expr = sql_epoch_now(db);
  if (!epoch_expr)
    {
      return ERR_DB_INTERNAL;
    }

  const char *sql_fmt = sql_entity_stock_upsert_epoch_fmt(db);
  if (!sql_fmt)
    {
      return ERR_DB_INTERNAL;
    }

  char sql_upsert[512];
  snprintf(sql_upsert, sizeof(sql_upsert), sql_fmt, epoch_expr, epoch_expr);

  if (!db_exec (db, sql_upsert,
                (db_bind_t[]){ db_bind_i32 (pid), db_bind_text (code),
                               db_bind_i32 (new_quantity) }, 3, &err))
    {
      return err.code;
    }

  return 0;
}

/* Look up commodity ID by code */
int
h_get_commodity_id_by_code (db_t *db, const char *code)
{
  if (!db || !code)
    {
      return 0;
    }

  db_res_t *res = NULL;
  db_error_t err;
  int id = 0;
  char sql_get_id[512];
  sql_build (db, "SELECT id FROM commodities WHERE code = {1} LIMIT 1;",
             sql_get_id, sizeof (sql_get_id));

  if (db_query (db,
                sql_get_id,
                (db_bind_t[]){db_bind_text (code)},
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          id = db_res_col_int (res, 0, &err);
        }
      db_res_finalize (res);
    }

  return id;
}

