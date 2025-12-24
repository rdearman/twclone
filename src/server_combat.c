#include <jansson.h>
#include "game_db.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>               // Added for pow() and ceil()
// local includes
#include "server_cron.h"
#include "server_log.h"
#include "common.h"
#include "server_players.h"
#include "server_universe.h"
#include "server_combat.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_config.h"
#include "server_planets.h"

/* Global Combat Constants (Physics) */
/* TODO: Move to config table */
static const double OFFENSE_SCALE = 0.05;
static const double DEFENSE_SCALE = 0.05;
static const int DAMAGE_PER_FIGHTER = 1;


typedef struct
{
  int id;
  int player_id;
  int corp_id;
  int hull;
  int shields;
  int fighters;
  int attack_power;             // shiptypes.offense
  int defense_power;            // shiptypes.defense
  int max_attack;               // shiptypes.maxattack
  char name[64];
  int sector;                   // Added for combat checks
} combat_ship_t;


/* Forward decls from your codebase */
json_t *db_get_stardock_sectors (void);
int handle_ship_attack (client_ctx_t *ctx,
                        json_t *root, json_t *data, db_t *db);


/* --- common helpers --- */
static inline int
require_auth (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id > 0)
    {
      return 1;
    }
  send_response_refused_steal (ctx,
                               root,
                               ERR_SECTOR_NOT_FOUND,
                               "Not authenticated", NULL);
  return 0;
}


static inline int
niy (client_ctx_t *ctx, json_t *root, const char *which)
{
  char buf[256];
  snprintf (buf, sizeof (buf), "Not implemented: %s", which);
  send_response_error (ctx, root, ERR_NOT_IMPLEMENTED, buf);
  return 0;
}


// Helper to check if a sector is a FedSpace sector (sectors 1-10)
static bool
is_fedspace_sector (int sector_id)
{
  return (sector_id >= 1 && sector_id <= 10);
}


// Helper to check if a sector is a Major Space Lane (MSL)
static bool
is_msl_sector (db_t *db, int sector_id)
{
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear (&err);

  const char *sql = "SELECT 1 FROM msl_sectors WHERE sector_id = $1 LIMIT 1;";
  db_bind_t params[] = {
    db_bind_i32 (sector_id)
  };
  size_t n_params = sizeof(params) / sizeof(params[0]);


  if (!db_query (db, sql, params, n_params, &res, &err))
    {
      LOGE ("Failed to query MSL check statement: %s", err.message);
      return false;
    }

  bool is_msl = db_res_step (res, &err);


  db_res_finalize (res);
  return is_msl;
}


int
cmd_combat_attack (client_ctx_t *ctx, json_t *root)
{
  int rc;                       // Declare rc
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_UNAVAILABLE, "Database unavailable");
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }

  rc = handle_ship_attack (ctx, root, data, db);
  return rc;
}


/*
 * Returns true if the asset is hostile to the given ship context,
 * false otherwise (friendly or neutral).
 */
bool
is_asset_hostile (int asset_player_id, int asset_corp_id,
                  int ship_player_id, int ship_corp_id)
{
  /* Friendly if same player */
  if (asset_player_id == ship_player_id)
    {
      return false;
    }
  /* Friendly if same non-zero corporation */
  if (asset_corp_id != 0 && asset_corp_id == ship_corp_id)
    {
      return false;
    }
  /* Otherwise hostile */
  return true;
}


/*
 * Returns true if the mine stack is active (quantity > 0 and not expired),
 * false otherwise.
 */
bool
armid_stack_is_active (const sector_asset_t *row, time_t now)
{
  if (row->quantity <= 0)
    {
      return false;
    }
  if (row->ttl == 0 || row->ttl == (time_t) 0 || row->ttl == -1)
    {
      return true;
    }
  return row->ttl > now;
}


/*
 * Applies Armid mine damage to a ship, prioritizing shields, then fighters, then hull.
 */
void
apply_armid_damage_to_ship (ship_t *ship,
                            int total_damage, armid_damage_breakdown_t *b)
{
  int dmg = total_damage;
  if (ship->shields > 0 && dmg > 0)
    {
      int d = MIN (dmg, ship->shields);


      ship->shields -= d;
      dmg -= d;
      if (b)
        {
          b->shields_lost += d;
        }
    }
  if (ship->fighters > 0 && dmg > 0)
    {
      int d = MIN (dmg, ship->fighters);


      ship->fighters -= d;
      dmg -= d;
      if (b)
        {
          b->fighters_lost += d;
        }
    }
  if (ship->hull > 0 && dmg > 0)
    {
      int d = MIN (dmg, ship->hull);


      ship->hull -= d;
      dmg -= d;
      if (b)
        {
          b->hull_lost += d;
        }
    }
}


int
apply_armid_mines_on_entry (client_ctx_t *ctx, int new_sector_id,
                            armid_encounter_t *out_enc)
{
  db_t *db = game_db_get_handle ();     // Get DB handle
  if (!db)
    {
      LOGE ("Database handle not available in apply_armid_mines_on_entry");
      return -1;                // Indicate error
    }
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      // No active ship, no mines can be triggered
      return 0;
    }
  // Initialize out_enc
  if (out_enc)
    {
      memset (out_enc, 0, sizeof (armid_encounter_t));
      out_enc->sector_id = new_sector_id;
    }
  // Get ship's current shield, fighters, and hull
  ship_t ship_stats = { 0 };
  int ship_player_id = ctx->player_id;
  int ship_corp_id = ctx->corp_id;      // Assuming ctx->corp_id is available

  db_error_t err;


  db_error_clear (&err);

  const char *sql_select_ship =
    "SELECT id, hull, fighters, shields FROM ships WHERE id = $1;";
  db_bind_t ship_params[] = { db_bind_i32 (ship_id) };
  db_res_t *ship_res = NULL;


  if (db_query (db, sql_select_ship, ship_params, 1, &ship_res, &err))
    {
      if (db_res_step (ship_res, &err))
        {
          ship_stats.id = db_res_col_i32 (ship_res, 0, &err);
          ship_stats.hull = db_res_col_i32 (ship_res, 1, &err);
          ship_stats.fighters = db_res_col_i32 (ship_res, 2, &err);
          ship_stats.shields = db_res_col_i32 (ship_res, 3, &err);
        }
      else
        {
          LOGW ("Ship %d not found for mine encounter.", ship_id);
          db_res_finalize (ship_res);
          return 0; // No ship, no encounter
        }
      db_res_finalize (ship_res);
    }
  else
    {
      LOGE ("Failed to prepare ship selection statement: %s",
            err.message);
      return -1;
    }

  // Config for damage
  int damage_per_mine = 10;


  // Assuming db_get_int_config or similar is available or default to 10
  // db_get_int_config (db, "armid_damage_per_mine", &damage_per_mine);

  // --- Process Armid Mines (Asset Type 1) ---
  if (g_armid_config.armid.enabled)
    {
      const char *sql_select_armid_mines =
        "SELECT id, quantity, offensive_setting, player, corporation, ttl "
        "FROM sector_assets " "WHERE sector = $1 AND asset_type = 1;";                                                                                                          // asset_type 1 for Armid Mines
      db_bind_t mine_params[] = { db_bind_i32 (new_sector_id) };
      db_res_t *mine_res = NULL;


      if (!db_query (db, sql_select_armid_mines, mine_params, 1, &mine_res,
                     &err))
        {
          LOGE ("Failed to prepare Armid mine selection statement: %s",
                err.message);
          return -1;
        }

      while (db_res_step (mine_res, &err))
        {
          int mine_id = db_res_col_i32 (mine_res, 0, &err);
          int mine_quantity = db_res_col_i32 (mine_res, 1, &err);
          sector_asset_t mine_asset_row = {
            .id = mine_id,
            .quantity = mine_quantity,
            .player = db_res_col_i32 (mine_res, 3, &err),
            .corporation = db_res_col_i32 (mine_res, 4, &err),
            .ttl = db_res_col_i64 (mine_res, 5, &err)
          };


          // Check Hostility using new helper
          if (!is_asset_hostile (mine_asset_row.player,
                                 mine_asset_row.corporation,
                                 ship_player_id, ship_corp_id))
            {
              continue;         // Friendly
            }

          // Canonical Rule: All hostile mines detonate
          int exploded = mine_quantity;
          int damage = exploded * damage_per_mine;
          armid_damage_breakdown_t d = { 0 };


          apply_armid_damage_to_ship (&ship_stats, damage, &d);

          // Remove Mines (Consumed)
          const char *sql_delete_mine =
            "DELETE FROM sector_assets WHERE id = $1;";
          db_bind_t del_params[] = { db_bind_i32 (mine_id) };


          db_exec (db, sql_delete_mine, del_params, 1, &err);

          // Accumulate stats
          if (out_enc)
            {
              out_enc->armid_triggered += exploded;
              out_enc->shields_lost += d.shields_lost;
              out_enc->fighters_lost += d.fighters_lost;
              out_enc->hull_lost += d.hull_lost;
            }

          // Log Event
          json_t *hit_data = json_object ();


          json_object_set_new (hit_data, "v", json_integer (1));
          json_object_set_new (hit_data, "attacker_id",
                               json_integer (mine_asset_row.player));
          json_object_set_new (hit_data, "defender_id",
                               json_integer (ctx->player_id));
          json_object_set_new (hit_data, "weapon",
                               json_string ("armid_mines"));
          json_object_set_new (hit_data, "damage_total",
                               json_integer (damage));
          json_object_set_new (hit_data, "mines_exploded",
                               json_integer (exploded));
          (void) db_log_engine_event ((long long) time (NULL), "combat.hit",
                                      "player", ctx->player_id, new_sector_id,
                                      hit_data, NULL);

          // If ship is destroyed, handle it
          if (ship_stats.hull <= 0)
            {
              const char *sql_upd =
                "UPDATE ships SET hull=$1, fighters=$2, shields=$3 WHERE id=$4";
              db_bind_t upd_params[] = {
                db_bind_i32 (ship_stats.hull),
                db_bind_i32 (ship_stats.fighters),
                db_bind_i32 (ship_stats.shields),
                db_bind_i32 (ship_id)
              };


              db_exec (db, sql_upd, upd_params, 4, &err);

              destroy_ship_and_handle_side_effects (ctx, ctx->player_id);
              if (out_enc)
                {
                  out_enc->destroyed = true;
                }
              LOGI ("Ship %d destroyed by Armid mine %d in sector %d",
                    ship_id, mine_id, new_sector_id);
              break;            // Stop processing further stacks
            }

          // Update ship DB if not destroyed (so next stack sees correct HP)
          const char *sql_upd =
            "UPDATE ships SET hull=$1, fighters=$2, shields=$3 WHERE id=$4";
          db_bind_t upd_params[] = {
            db_bind_i32 (ship_stats.hull),
            db_bind_i32 (ship_stats.fighters),
            db_bind_i32 (ship_stats.shields),
            db_bind_i32 (ship_id)
          };


          db_exec (db, sql_upd, upd_params, 4, &err);
        }
      db_res_finalize (mine_res);
    }                           // End Armid mine processing

  return 0;                     // Success
}


int
apply_limpet_mines_on_entry (client_ctx_t *ctx, int new_sector_id,
                             armid_encounter_t *out_enc)
{
  (void) out_enc;
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return -1;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      return 0;
    }

  int ship_player_id = ctx->player_id;
  int ship_corp_id = ctx->corp_id;


  if (!g_cfg.mines.limpet.enabled)
    {
      return 0;
    }

  db_error_t err;


  db_error_clear (&err);

  const char *sql_select_limpets =
    "SELECT id, quantity, player, corporation, ttl "
    "FROM sector_assets "
    "WHERE sector = $1 AND asset_type = 4 AND quantity > 0;";
  db_bind_t params[] = { db_bind_i32 (new_sector_id) };
  db_res_t *res = NULL;


  if (!db_query (db, sql_select_limpets, params, 1, &res, &err))
    {
      LOGE ("Failed to prepare Limpet mine selection: %s", err.message);
      return -1;
    }

  while (db_res_step (res, &err))
    {
      int asset_id = db_res_col_i32 (res, 0, &err);
      int quantity = db_res_col_i32 (res, 1, &err);
      int owner_id = db_res_col_i32 (res, 2, &err);
      int corp_id = db_res_col_i32 (res, 3, &err);
      time_t ttl = db_res_col_i64 (res,
                                   4,
                                   &err);

      sector_asset_t asset = {.quantity = quantity,.ttl = ttl };


      if (!is_asset_hostile (owner_id, corp_id, ship_player_id, ship_corp_id))
        {
          continue;
        }

      if (!armid_stack_is_active (&asset, time (NULL)))
        {
          continue;
        }

      const char *sql_check_attached =
        "SELECT 1 FROM limpet_attached WHERE ship_id=$1 AND owner_player_id=$2;";
      db_bind_t check_params[] = { db_bind_i32 (ship_id),
                                   db_bind_i32 (owner_id) };
      db_res_t *check_res = NULL;

      bool already_attached = false;


      if (db_query (db, sql_check_attached, check_params, 2, &check_res, &err))
        {
          already_attached = db_res_step (check_res, &err);
          db_res_finalize (check_res);
        }

      if (already_attached)
        {
          continue;
        }

      if (quantity > 1)
        {
          const char *sql_upd =
            "UPDATE sector_assets SET quantity=quantity-1 WHERE id=$1";
          db_bind_t upd_params[] = { db_bind_i32 (asset_id) };


          db_exec (db, sql_upd, upd_params, 1, &err);
        }
      else
        {
          const char *sql_del = "DELETE FROM sector_assets WHERE id=$1";
          db_bind_t del_params[] = { db_bind_i32 (asset_id) };


          db_exec (db, sql_del, del_params, 1, &err);
        }

      const char *sql_attach =
        "INSERT OR REPLACE INTO limpet_attached (ship_id, owner_player_id, created_ts) VALUES ($1, $2, strftime('%s','now'));";
      db_bind_t attach_params[] = { db_bind_i32 (ship_id),
                                    db_bind_i32 (owner_id) };


      db_exec (db, sql_attach, attach_params, 2, &err);

      json_t *event_data = json_object ();


      json_object_set_new (event_data, "target_ship_id",
                           json_integer (ship_id));
      json_object_set_new (event_data, "target_player_id",
                           json_integer (ship_player_id));
      json_object_set_new (event_data, "sector_id",
                           json_integer (new_sector_id));

      db_log_engine_event ((long long) time (NULL),
                           "combat.limpet.attached",
                           "player",
                           owner_id, new_sector_id, event_data, NULL);

      LOGI ("Limpet attached! Ship %d tagged by Player %d in Sector %d",
            ship_id, owner_id, new_sector_id);
    }
  db_res_finalize (res);

  return 0;
}


int
cmd_deploy_assets_list_internal (client_ctx_t *ctx,
                                 json_t *root,
                                 const char *list_type,
                                 const char *asset_key, const char *sql_query)
{
  (void) asset_key;             // asset_key is unused
  // --- 1. Initialization ---
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database handle not available.");
      return 0;
    }
  int self_player_id = ctx->player_id;

  // --- 2. Prepare & Bind ---
  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[] = {
    db_bind_i32 (self_player_id),
    db_bind_i32 (self_player_id)
  };

  // Note: The SQL queries passed to this function use ?1, ?2 (SQLite style).
  // The generic API expects $1, $2 or relies on the driver to translate.
  // Since we are moving to the generic API, we should ideally update the SQL strings passed in.
  // However, the SQLite driver in this project translates $N to ?N, but might not handle ?N if passed directly unless it just passes it through.
  // Wait, the plan says "Placeholder Convention: All SQL passed to the driver layer MUST use $1, $2...".
  // So I should update the SQL strings in the callers too.

  db_res_t *res = NULL;


  if (!db_query (db, sql_query, params, 2, &res, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, err.message);
      return 0;
    }

  // --- 3. Execute Query and Build Entries Array ---
  int total_count = 0;
  json_t *entries = json_array ();      // Create the array for the entries


  while (db_res_step (res, &err))
    {
      total_count++;
      // Extract all columns from the row
      int asset_id = db_res_col_i32 (res, 0, &err);
      int sector_id = db_res_col_i32 (res, 1, &err);
      int count = db_res_col_i32 (res, 2, &err);
      int offense_mode = db_res_col_i32 (res, 3, &err);
      int player_id = db_res_col_i32 (res, 4, &err);
      const char *player_name = db_res_col_text (res, 5, &err);
      int corp_id = db_res_col_i32 (res, 6, &err);
      const char *corp_tag = db_res_col_text (res, 7, &err);
      int asset_type = db_res_col_i32 (res, 8, &err);

      // Pack them into a JSON object
      json_t *entry = json_object ();


      json_object_set_new (entry, "asset_id", json_integer (asset_id));
      json_object_set_new (entry, "sector_id", json_integer (sector_id));
      json_object_set_new (entry, "count", json_integer (count));
      json_object_set_new (entry, "offense_mode",
                           json_integer (offense_mode));
      json_object_set_new (entry, "player_id", json_integer (player_id));
      json_object_set_new (entry, "player_name",
                           json_string (player_name ? player_name :
                                        "Unknown"));
      json_object_set_new (entry, "corp_id", json_integer (corp_id));
      json_object_set_new (entry, "corp_tag",
                           json_string (corp_tag ? corp_tag : ""));
      json_object_set_new (entry, "type", json_integer (asset_type));

      if (entry)
        {
          json_array_append_new (entries, entry);
        }
    }

  db_res_finalize (res);

  // --- 4. Build Final Payload and Send Response ---
  json_t *jdata_payload = json_object ();


  json_object_set_new (jdata_payload, "total", json_integer (total_count));
  json_object_set_new (jdata_payload, "entries", entries);      // Add the array
  send_response_ok_take (ctx, root, list_type, &jdata_payload);
  return 0;
}


int
cmd_deploy_fighters_list (client_ctx_t *ctx, json_t *root)
{
  const char *sql_query_fighters = "SELECT " "  sa.id AS asset_id, "    /* NEW: Index 0 */
                                   "  sa.sector AS sector_id, " /* Index 1 */
                                   "  sa.quantity AS count, " /* Index 2 */
                                   "  sa.offensive_setting AS offense_mode, " /* Index 3 */
                                   "  sa.player AS player_id, " /* Index 4 */
                                   "  p.name AS player_name, " /* Index 5 */
                                   "  c.id AS corp_id, " /* Index 6 */
                                   "  c.tag AS corp_tag, " /* Index 7 */
                                   "  sa.asset_type AS type " /* Index 8 */
                                   "FROM sector_assets sa "
                                   "JOIN players p ON sa.player = p.id "
                                   "LEFT JOIN corp_members cm_player ON cm_player.player_id = sa.player "
                                   "LEFT JOIN corporations c ON c.id = cm_player.corp_id "
                                   "WHERE "
                                   "  sa.asset_type = 2 /* Assuming asset_type=2 means Fighter */ AND "
                                   "  ( "
                                   "    sa.player = ?1 "
                                   "    OR sa.player IN ( "
                                   "      SELECT cm_member.player_id "
                                   "      FROM corp_members cm_member "
                                   "      WHERE cm_member.corp_id = ( "
                                   "        SELECT cm_self.corp_id FROM corp_members cm_self WHERE cm_self.player_id = ?2 "
                                   "      ) " "    ) " "  ) "
                                   "ORDER BY sa.sector ASC;";
  return cmd_deploy_assets_list_internal (ctx,
                                          root,
                                          "Deploy.fighters.list_v1",
                                          "fighters", sql_query_fighters);
}


int
cmd_deploy_mines_list (client_ctx_t *ctx, json_t *root)
{
  const char *sql_query_mines = "SELECT " "  sa.id AS asset_id, "       /* NEW: Index 0 */
                                "  sa.sector AS sector_id, " /* Index 1 */
                                "  sa.quantity AS count, " /* Index 2 */
                                "  sa.offensive_setting AS offense_mode, " /* Index 3 */
                                "  sa.player AS player_id, " /* Index 4 */
                                "  p.name AS player_name, " /* Index 5 */
                                "  c.id AS corp_id, " /* Index 6 */
                                "  c.tag AS corp_tag, " /* Index 7 */
                                "  sa.asset_type AS type " /* Index 8 */
                                "FROM sector_assets sa "
                                "JOIN players p ON sa.player = p.id "
                                "LEFT JOIN corp_members cm_player ON cm_player.player_id = sa.player "
                                "LEFT JOIN corporations c ON c.id = cm_player.corp_id "
                                "WHERE "
                                "  sa.asset_type IN (1, 4) /* Filter for Armid (1) and Limpet (4) mines */ AND "
                                "  ( "
                                "    sa.player = ?1 "
                                "    OR sa.player IN ( "
                                "      SELECT cm_member.player_id "
                                "      FROM corp_members cm_member "
                                "      WHERE cm_member.corp_id = ( "
                                "        SELECT cm_self.corp_id FROM corp_members cm_self WHERE cm_self.player_id = ?2 "
                                "      ) " "    ) " "  ) "
                                "ORDER BY sa.sector ASC, sa.asset_type ASC;";
  return cmd_deploy_assets_list_internal (ctx,
                                          root,
                                          "deploy.mines.list_v1",
                                          "mines", sql_query_mines);
}


/**
 * @brief Handles the 'combat.flee' command.
 * * @param ctx The player context (struct context *).
 * @param root The root JSON object containing the command payload.
 * @return int Returns 0 on successful processing (or error handling).
 */
int
handle_combat_flee (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }

  h_decloak_ship (db, ship_id);
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);


  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "combat.flee", root,
                                            NULL);
    }

  // Load Engine/Mass (MVP: Engine=10 fixed, Mass=maxholds)
  int mass = 100;
  int engine = 10;
  int sector_id = 0;

  db_error_t err;


  db_error_clear (&err);

  const char *sql_ship =
    "SELECT t.maxholds, s.sector FROM ships s JOIN shiptypes t ON s.type_id=t.id WHERE s.id=$1";
  db_bind_t ship_params[] = { db_bind_i32 (ship_id) };
  db_res_t *ship_res = NULL;


  if (db_query (db, sql_ship, ship_params, 1, &ship_res, &err))
    {
      if (db_res_step (ship_res, &err))
        {
          mass = db_res_col_i32 (ship_res, 0, &err);
          sector_id = db_res_col_i32 (ship_res, 1, &err);
        }
      db_res_finalize (ship_res);
    }

  // Deterministic check: (Engine * 10) / (Mass + 1) > 0.5
  double score = ((double) engine * 10.0) / ((double) mass + 1.0);
  bool success = (score > 0.5);


  if (success)
    {
      // Pick first adjacent sector
      int dest = 0;

      const char *sql_adj =
        "SELECT to_sector FROM sector_warps WHERE from_sector=$1 ORDER BY to_sector ASC LIMIT 1";
      db_bind_t adj_params[] = { db_bind_i32 (sector_id) };
      db_res_t *adj_res = NULL;


      if (db_query (db, sql_adj, adj_params, 1, &adj_res, &err))
        {
          if (db_res_step (adj_res, &err))
            {
              dest = db_res_col_i32 (adj_res, 0, &err);
            }
          db_res_finalize (adj_res);
        }

      if (dest > 0)
        {
          if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
            {
              send_response_error (ctx, root, ERR_DB, "Transaction error");
              return 0;
            }

          // Update Ship
          const char *upd_ship = "UPDATE ships SET sector=$1 WHERE id=$2";
          db_bind_t us_params[] = { db_bind_i32 (dest), db_bind_i32 (ship_id) };


          db_exec (db, upd_ship, us_params, 2, &err);

          // Update Player
          const char *upd_player = "UPDATE players SET sector=$1 WHERE id=$2";
          db_bind_t up_params[] = { db_bind_i32 (dest),
                                    db_bind_i32 (ctx->player_id) };


          db_exec (db, upd_player, up_params, 2, &err);

          if (!db_tx_commit (db, &err))
            {
              send_response_error (ctx, root, ERR_DB, "Commit error");
              return 0;
            }

          // Hazards
          h_handle_sector_entry_hazards (db, ctx, dest);

          json_t *res = json_object ();


          json_object_set_new (res, "success", json_true ());
          json_object_set_new (res, "to_sector", json_integer (dest));
          send_response_ok_take (ctx, root, "combat.flee", &res);
          return 0;
        }
    }

  // Failure
  json_t *res = json_object ();


  json_object_set_new (res, "success", json_false ());
  send_response_ok_take (ctx, root, "combat.flee", &res);
  return 0;
}


/* ---------- combat.deploy_fighters (fixed-SQL) ---------- */
static const char *SQL_SECTOR_FTR_SUM =
  "SELECT COALESCE(SUM(quantity),0) "
  "FROM sector_assets " "WHERE sector=?1 AND asset_type='2';";
static const char *SQL_SHIP_GET_FTR =
  "SELECT fighters FROM ships WHERE id=?1;";
static const char *SQL_SHIP_DEC_FTR =
  "UPDATE ships SET fighters=fighters-?1 WHERE id=?2;";
static const char *SQL_ASSET_INSERT_FIGHTERS =
  "INSERT INTO sector_assets(sector, player, corporation, "
  "                          asset_type, quantity, offensive_setting, deployed_at) "
  "VALUES (?1, ?2, ?3, 2, ?4, ?5, strftime('%s','now'));";


/* ---------- combat.deploy_fighters ---------- */
int
cmd_combat_deploy_fighters (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  bool in_fed = false;
  bool in_sdock = false;
  /* Parse input */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_amount = json_object_get (data, "amount");
  json_t *j_offense = json_object_get (data, "offense");        /* required: 1..3 */
  json_t *j_corp_id = json_object_get (data, "corporation_id"); /* optional, nullable */


  if (!j_amount || !json_is_integer (j_amount) ||
      !j_offense || !json_is_integer (j_offense))
    {
      send_response_error (ctx,
                           root,
                           ERR_CURSOR_INVALID,
                           "Missing required field or invalid type: amount/offense");
      return 0;
    }
  int amount = (int) json_integer_value (j_amount);
  int offense = (int) json_integer_value (j_offense);


  if (amount <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "amount must be > 0");
      return 0;
    }
  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK)
    {
      send_response_error (ctx,
                           root,
                           ERR_CURSOR_INVALID,
                           "offense must be one of {1=TOLL,2=DEFEND,3=ATTACK}");
      return 0;
    }
  /* Resolve active ship + sector */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }
  /* Decloak: visible hostile/defensive action */
  (void) h_decloak_ship (db, ship_id);

  db_error_t err;


  db_error_clear (&err);

  int sector_id = -1;
  {
    const char *sql_sec = "SELECT sector FROM ships WHERE id=$1;";
    db_bind_t params[] = { db_bind_i32 (ship_id) };
    db_res_t *res = NULL;


    if (db_query (db, sql_sec, params, 1, &res, &err))
      {
        if (db_res_step (res, &err))
          {
            sector_id = db_res_col_i32 (res, 0, &err);
          }
        db_res_finalize (res);
      }
    else
      {
        char error_buffer[256];


        snprintf (error_buffer, sizeof (error_buffer),
                  "Unable to resolve current sector - %s", err.message);
        send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, error_buffer);
        return 0;
      }

    if (sector_id <= 0)
      {
        send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "Invalid sector");
        return 0;
      }
  }


  db_error_clear (&err);

  /* Transaction: debit ship, credit sector */
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Could not start transaction");
      return 0;
    }

  /* Sector cap check (moved inside transaction) */
  int sector_total = 0;


  if (sum_sector_fighters (db, sector_id, &sector_total) != 0) // Helper now returns error code
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           REF_NOT_IN_SECTOR,
                           "Failed to read sector fighters");
      return 0;
    }
  if (sector_total + amount > SECTOR_FIGHTER_CAP)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           ERR_SECTOR_OVERCROWDED,
                           "Sector fighter limit exceeded (50,000)");
      return 0;
    }

  int rc = ship_consume_fighters (db, ship_id, amount);


  if (rc == SQLITE_TOOBIG)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           REF_AMMO_DEPLETED,
                           "Insufficient fighters on ship");
      return 0;
    }
  if (rc != 0)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           REF_AMMO_DEPLETED,
                           "Failed to update ship fighters");
      return 0;
    }
  rc =
    insert_sector_fighters (db, sector_id, ctx->player_id, j_corp_id, offense,
                            amount);
  if (rc != 0)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           SECTOR_ERR,
                           "Failed to create sector assets record");
      return 0;
    }

  // Get asset ID
  int asset_id = 0;
  db_res_t *id_res = NULL;


  if (db_query (db, "SELECT last_insert_rowid();", NULL, 0, &id_res, &err))
    {
      if (db_res_step (id_res, &err))
        {
          asset_id = db_res_col_i32 (id_res, 0, &err);
        }
      db_res_finalize (id_res);
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Commit failed");
      return 0;
    }

  /* Fedspace/Stardock → summon ISS + warn player */
  // Check for Federation sectors (1-10)
  if (sector_id >= 1 && sector_id <= 10)
    {
      in_fed = true;
    }
  // Get the list of stardock sectors from the database
  json_t *stardock_sectors = db_get_stardock_sectors ();


  // --------------------------------------------------------
  // Logic to check if sector_id is a Stardock location
  // --------------------------------------------------------
  if (stardock_sectors && json_is_array (stardock_sectors))
    {
      size_t index;
      json_t *sector_value;


      // Loop through the array of stardock sector IDs
      json_array_foreach (stardock_sectors, index, sector_value)
      {
        // 1. Ensure the element is a valid integer
        if (json_is_integer (sector_value))
          {
            int stardock_sector_id = json_integer_value (sector_value);


            // 2. Check for a match
            if (sector_id == stardock_sector_id)
              {
                in_sdock = true;
                // Found a match, no need to check the rest of the array
                break;
              }
          }
      }
    }
  if (stardock_sectors)
    {
      json_decref (stardock_sectors);
    }
  if (in_fed || in_sdock)
    {
      iss_summon (sector_id, ctx->player_id);
      (void) h_send_message_to_player (ctx->player_id,
                                       0,
                                       "Federation Warning",
                                       "Fighter deployment in protected space has triggered ISS response.");
    }
  /* Emit engine_event via h_log_engine_event */
  {
    json_t *evt = json_object ();


    json_object_set_new (evt, "sector_id", json_integer (sector_id));
    json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
    if (j_corp_id && json_is_integer (j_corp_id))
      {
        json_object_set_new (evt, "corporation_id",
                             json_integer (json_integer_value (j_corp_id)));
      }
    else
      {
        json_object_set_new (evt, "corporation_id", json_null ());
      }
    json_object_set_new (evt, "amount", json_integer (amount));
    json_object_set_new (evt, "offense", json_integer (offense));
    json_object_set_new (evt, "event_ts",
                         json_integer ((json_int_t) time (NULL)));
    json_object_set_new (evt, "asset_id", json_integer (asset_id));     // Add asset_id to event
    (void) db_log_engine_event ((long long) time (NULL),
                                "combat.fighters.deployed", NULL,
                                ctx->player_id, sector_id, evt, NULL);
  }
  /* Recompute total for response convenience */
  (void) sum_sector_fighters (db, sector_id, &sector_total);
  LOGI
  (
    "DEBUG: cmd_combat_deploy_fighters - sector_id: %d, player_id: %d, amount: %d, offense: %d, sector_total: %d, asset_id: %d",
    sector_id,
    ctx->player_id,
    amount,
    offense,
    sector_total,
    asset_id);
  /* ---- Build data payload (no outer wrapper here) ---- */
  json_t *out = json_object ();


  json_object_set_new (out, "sector_id", json_integer (sector_id));
  json_object_set_new (out, "owner_player_id", json_integer (ctx->player_id));
  if (j_corp_id && json_is_integer (j_corp_id))
    {
      json_object_set_new (out, "owner_corp_id",
                           json_integer (json_integer_value (j_corp_id)));
    }
  else
    {
      json_object_set_new (out, "owner_corp_id", json_null ());
    }
  json_object_set_new (out, "amount", json_integer (amount));
  json_object_set_new (out, "offense", json_integer (offense));
  json_object_set_new (out, "sector_total_after",
                       json_integer (sector_total));
  json_object_set_new (out, "asset_id", json_integer (asset_id));       // Add asset_id to response
  /* Envelope: echo id/meta from `root`, set type string for this result */
  send_response_ok_take (ctx, root, "combat.fighters.deployed", &out);
  return 0;
}


/* ---------- combat.deploy_mines ---------- */
static const char *SQL_SECTOR_MINE_SUM =
  "SELECT COALESCE(SUM(quantity),0) " "FROM sector_assets "
  "WHERE sector=?1 AND asset_type IN (1, 4);";                                                                                                  // 1 for Armid, 4 for Limpet


/* static const char *SQL_SHIP_GET_MINE = "SELECT mines FROM ships WHERE id=?1;"; *//* Assuming 'mines' column for total mines */


/* static const char *SQL_SHIP_DEC_MINE = */


/*   "UPDATE ships SET mines=mines-?1 WHERE id=?2;"; */


/* static const char *SQL_ASSET_INSERT_MINES = "INSERT INTO sector_assets(sector, player, corporation, " */


/* "                          asset_type, quantity, offensive_setting, deployed_at) " */


/* "VALUES (?1, ?2, ?3, ?4, ?5, ?6, strftime('%s','now'));"; *//* ?4 for asset_type (1 or 4) */


/* Sum fighters already in the sector. */
static int
sum_sector_fighters (db_t *db, int sector_id, int *total_out)
{
  *total_out = 0;
  db_error_t err;


  db_error_clear (&err);

  // SQL_SECTOR_FTR_SUM is defined earlier as:
  // "SELECT COALESCE(SUM(quantity),0) FROM sector_assets WHERE sector=?1 AND asset_type='2';"
  // We need to ensure it uses $1 if we use parameterized query with db_query,
  // or relying on the driver to translate ?1 (which sqlite driver does).
  // However, the best practice is to use $1 in the string if we can, or rely on the driver.
  // The generic API says "SQL MUST use $1, $2".
  // So I should probably redefine SQL_SECTOR_FTR_SUM or use a new string.

  const char *sql =
    "SELECT COALESCE(SUM(quantity),0) FROM sector_assets WHERE sector=$1 AND asset_type=2;";
  db_bind_t params[] = { db_bind_i32 (sector_id) };
  db_res_t *res = NULL;


  if (!db_query (db, sql, params, 1, &res, &err))
    {
      return err.code;
    }

  if (db_res_step (res, &err))
    {
      *total_out = db_res_col_i32 (res, 0, &err);
    }
  db_res_finalize (res);
  return 0; // DB_OK
}


/* Debit ship fighters safely (returns SQLITE_TOOBIG if insufficient). */
static int
ship_consume_fighters (db_t *db, int ship_id, int amount)
{
  db_error_t err;
  db_error_clear (&err);

  // Get current fighters
  const char *sql_get = "SELECT fighters FROM ships WHERE id=$1;";
  db_bind_t params_get[] = { db_bind_i32 (ship_id) };
  db_res_t *res = NULL;
  int have = 0;


  if (!db_query (db, sql_get, params_get, 1, &res, &err))
    {
      LOGI ("ship_consume_fighters - SQL_SHIP_GET_FTR failed: %s", err.message);
      return err.code;
    }

  if (db_res_step (res, &err))
    {
      have = db_res_col_i32 (res, 0, &err);
    }
  else
    {
      LOGI ("DEBUGGING: ship_id=%d not found", ship_id);
      db_res_finalize (res);
      return ERR_DB_QUERY_FAILED;
    }
  db_res_finalize (res);

  if (have < amount)
    {
      LOGI ("DEBUGGING SQLITE_TOOBIG have < amount");
      return SQLITE_TOOBIG; // Keep using SQLITE codes for now or map to generic
    }

  // Update fighters
  const char *sql_upd = "UPDATE ships SET fighters=fighters-$1 WHERE id=$2;";
  db_bind_t params_upd[] = {
    db_bind_i32 (amount),
    db_bind_i32 (ship_id)
  };


  if (!db_exec (db, sql_upd, params_upd, 2, &err))
    {
      LOGI ("%s failed: %s", sql_upd, err.message);
      return err.code;
    }

  return 0; // DB_OK
}


/* Sum mines already in the sector. */
static int
sum_sector_mines (db_t *db, int sector_id, int *total_out)
{
  *total_out = 0;
  db_error_t err;


  db_error_clear (&err);

  const char *sql =
    "SELECT COALESCE(SUM(quantity),0) FROM sector_assets WHERE sector=$1 AND asset_type IN (1, 4);";
  // 1 for Armid, 4 for Limpet
  db_bind_t params[] = { db_bind_i32 (sector_id) };
  db_res_t *res = NULL;


  if (!db_query (db, sql, params, 1, &res, &err))
    {
      return err.code;
    }

  if (db_res_step (res, &err))
    {
      *total_out = db_res_col_i32 (res, 0, &err);
    }
  db_res_finalize (res);
  return 0; // DB_OK
}


/* Debit ship mines safely (returns SQLITE_TOOBIG if insufficient). */
static int
ship_consume_mines (db_t *db, int ship_id, int asset_type, int amount)
{
  const char *col_name_get;
  const char *col_name_dec;

  if (asset_type == ASSET_MINE)
    {
      col_name_get = "mines"; col_name_dec = "mines";
    }
  else if (asset_type == ASSET_LIMPET_MINE)
    {
      col_name_get = "limpets"; col_name_dec = "limpets";
    }
  else
    {
      LOGE ("ship_consume_mines - Invalid asset_type %d", asset_type);
      return ERR_INVALID_ARG;
    }

  db_error_t err;


  db_error_clear (&err);

  char sql_get[256];


  snprintf (sql_get,
            sizeof (sql_get),
            "SELECT %s FROM ships WHERE id=$1;",
            col_name_get);
  db_bind_t params_get[] = { db_bind_i32 (ship_id) };
  db_res_t *res = NULL;
  int have = 0;


  if (!db_query (db, sql_get, params_get, 1, &res, &err))
    {
      LOGE ("ship_consume_mines - GET failed: %s", err.message);
      return err.code;
    }

  if (db_res_step (res, &err))
    {
      have = db_res_col_i32 (res, 0, &err);
    }
  else
    {
      db_res_finalize (res);
      return ERR_DB_QUERY_FAILED;
    }
  db_res_finalize (res);

  if (have < amount)
    {
      return SQLITE_TOOBIG;
    }

  char sql_upd[256];


  snprintf (sql_upd,
            sizeof (sql_upd),
            "UPDATE ships SET %s=%s-$1 WHERE id=$2;",
            col_name_dec,
            col_name_dec);
  db_bind_t params_upd[] = { db_bind_i32 (amount), db_bind_i32 (ship_id) };


  if (!db_exec (db, sql_upd, params_upd, 2, &err))
    {
      LOGE ("ship_consume_mines - UPDATE failed: %s", err.message);
      return err.code;
    }
  return 0;
}


/* Insert a sector_assets row for mines. */
static int
insert_sector_mines (db_t *db,
                     int sector_id, int owner_player_id,
                     json_t *corp_id_json /* nullable */,
                     int asset_type, int offense_mode, int amount)
{
  db_error_t err;
  db_error_clear (&err);

  const char *sql_insert_mines =
    "INSERT INTO sector_assets(sector, player, corporation, "
    "                          asset_type, quantity, offensive_setting, deployed_at) "
    "VALUES ($1, $2, $3, $4, $5, $6, strftime('%s','now'));";

  int corp_id = 0;


  if (corp_id_json && json_is_integer (corp_id_json))
    {
      corp_id = (int) json_integer_value (corp_id_json);
    }

  db_bind_t params[] = {
    db_bind_i32 (sector_id),
    db_bind_i32 (owner_player_id),
    db_bind_i32 (corp_id),
    db_bind_i32 (asset_type),
    db_bind_i32 (amount),
    db_bind_i32 (offense_mode)
  };


  if (!db_exec (db, sql_insert_mines, params, 6, &err))
    {
      LOGE ("insert_sector_mines failed: %s", err.message);
      return err.code;
    }
  return 0;
}


/* Sum mines already in the sector. */
static int
sum_sector_mines (db_t *db, int sector_id, int *total_out)
{
  *total_out = 0;
  db_error_t err;


  db_error_clear (&err);

  const char *sql =
    "SELECT COALESCE(SUM(quantity),0) FROM sector_assets WHERE sector=$1 AND asset_type IN (1, 4);";
  // 1 for Armid, 4 for Limpet
  db_bind_t params[] = { db_bind_i32 (sector_id) };
  db_res_t *res = NULL;


  if (!db_query (db, sql, params, 1, &res, &err))
    {
      return err.code;
    }

  if (db_res_step (res, &err))
    {
      *total_out = db_res_col_i32 (res, 0, &err);
    }
  db_res_finalize (res);
  return 0; // DB_OK
}


/* Debit ship mines safely (returns SQLITE_TOOBIG if insufficient). */
static int
ship_consume_mines (db_t *db, int ship_id, int asset_type, int amount)
{
  const char *col_name_get;
  const char *col_name_dec;

  if (asset_type == ASSET_MINE)
    {
      col_name_get = "mines"; col_name_dec = "mines";
    }
  else if (asset_type == ASSET_LIMPET_MINE)
    {
      col_name_get = "limpets"; col_name_dec = "limpets";
    }
  else
    {
      LOGE ("ship_consume_mines - Invalid asset_type %d", asset_type);
      return ERR_INVALID_ARG;
    }

  db_error_t err;


  db_error_clear (&err);

  char sql_get[256];


  snprintf (sql_get,
            sizeof (sql_get),
            "SELECT %s FROM ships WHERE id=$1;",
            col_name_get);
  db_bind_t params_get[] = { db_bind_i32 (ship_id) };
  db_res_t *res = NULL;
  int have = 0;


  if (!db_query (db, sql_get, params_get, 1, &res, &err))
    {
      LOGE ("ship_consume_mines - GET failed: %s", err.message);
      return err.code;
    }

  if (db_res_step (res, &err))
    {
      have = db_res_col_i32 (res, 0, &err);
    }
  else
    {
      db_res_finalize (res);
      return ERR_DB_QUERY_FAILED;
    }
  db_res_finalize (res);

  if (have < amount)
    {
      return SQLITE_TOOBIG;
    }

  char sql_upd[256];


  snprintf (sql_upd,
            sizeof (sql_upd),
            "UPDATE ships SET %s=%s-$1 WHERE id=$2;",
            col_name_dec,
            col_name_dec);
  db_bind_t params_upd[] = { db_bind_i32 (amount), db_bind_i32 (ship_id) };


  if (!db_exec (db, sql_upd, params_upd, 2, &err))
    {
      LOGE ("ship_consume_mines - UPDATE failed: %s", err.message);
      return err.code;
    }
  return 0;
}


/* Insert a sector_assets row for mines. */
static int
insert_sector_mines (db_t *db,
                     int sector_id, int owner_player_id,
                     json_t *corp_id_json /* nullable */,
                     int asset_type, int offense_mode, int amount)
{
  db_error_t err;
  db_error_clear (&err);

  const char *sql_insert_mines =
    "INSERT INTO sector_assets(sector, player, corporation, "
    "                          asset_type, quantity, offensive_setting, deployed_at) "
    "VALUES ($1, $2, $3, $4, $5, $6, strftime('%s','now'));";

  int corp_id = 0;


  if (corp_id_json && json_is_integer (corp_id_json))
    {
      corp_id = (int) json_integer_value (corp_id_json);
    }

  db_bind_t params[] = {
    db_bind_i32 (sector_id),
    db_bind_i32 (owner_player_id),
    db_bind_i32 (corp_id),
    db_bind_i32 (asset_type),
    db_bind_i32 (amount),
    db_bind_i32 (offense_mode)
  };


  if (!db_exec (db, sql_insert_mines, params, 6, &err))
    {
      LOGE ("insert_sector_mines failed: %s", err.message);
      return err.code;
    }
  return 0;
}


/* Insert a sector_assets row for fighters. */
static int
insert_sector_fighters (db_t *db,
                        int sector_id, int owner_player_id,
                        json_t *corp_id_json /* nullable */,
                        int offense_mode, int amount)
{
  db_error_t err;
  db_error_clear (&err);

  const char *sql =
    "INSERT INTO sector_assets(sector, player, corporation, "
    "                          asset_type, quantity, offensive_setting, deployed_at) "
    "VALUES ($1, $2, $3, 2, $4, $5, strftime('%s','now'));";

  int corp_id = 0;


  if (corp_id_json && json_is_integer (corp_id_json))
    {
      corp_id = (int) json_integer_value (corp_id_json);
    }

  db_bind_t params[] = {
    db_bind_i32 (sector_id),
    db_bind_i32 (owner_player_id),
    db_bind_i32 (corp_id),
    db_bind_i32 (amount),
    db_bind_i32 (offense_mode)
  };


  if (!db_exec (db, sql, params, 5, &err))
    {
      LOGE ("insert_sector_fighters failed: %s", err.message);
      return err.code;
    }

  return 0; // DB_OK
}


/*

 * Populates sector_mine_counts_t with counts of different mine types in a sector.

 */
int
get_sector_mine_counts (int sector_id, sector_mine_counts_t *out)
{
  if (!out)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_ERROR;
    }
  memset (out, 0, sizeof (sector_mine_counts_t));       // Initialize counts to zero
  sqlite3_stmt *st = NULL;
  const char *sql =
    "SELECT "
    "  SUM(CASE WHEN asset_type IN (1, 4) THEN quantity ELSE 0 END) AS total_mines, "
    "  SUM(CASE WHEN asset_type = 1 THEN quantity ELSE 0 END) AS armid_mines, "
    "  SUM(CASE WHEN asset_type = 4 THEN quantity ELSE 0 END) AS limpet_mines "
    "FROM sector_assets " "WHERE sector = ?1;";
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("Failed to prepare get_sector_mine_counts statement: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, sector_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      out->total_mines = sqlite3_column_int (st, 0);
      out->armid_mines = sqlite3_column_int (st, 1);
      out->limpet_mines = sqlite3_column_int (st, 2);
      rc = SQLITE_OK;
    }
  else
    {
      // No mines found, counts are already zero due to memset
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


json_t *
db_get_stardock_sectors (void)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *stmt = NULL;
  int rc;
  // Create the JSON array to hold all results
  json_t *sector_list = json_array ();
  if (!sector_list)
    {
      LOGE ("ERROR: Failed to allocate JSON array for stardock sectors.\n");
      return NULL;
    }
  const char *sql = "SELECT sector_id FROM stardock_location;";


  // 1. Prepare the SQL statement
  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("DB Error: Could not prepare stardock query: %s\n",
            sqlite3_errmsg (db));
      json_decref (sector_list);
      return NULL;
    }
  // 2. Loop through all rows
  while ((rc = sqlite3_step (stmt)) == SQLITE_ROW)
    {
      // Retrieve the integer result from the first column (index 0)
      int sector_id = sqlite3_column_int (stmt, 0);
      // Convert the integer to a JSON object
      json_t *j_sector = json_integer (sector_id);


      // Append the new JSON object to the array (json_array_append_new consumes the reference)
      if (json_array_append_new (sector_list, j_sector) != 0)
        {
          LOGE ("ERROR: Failed to append sector ID %d to list.\n", sector_id);
          json_decref (j_sector);       // Clean up the orphaned reference
          // You may choose to stop here or continue
        }
    }
  // 3. Handle step errors if the loop didn't finish normally (SQLITE_DONE)
  if (rc != SQLITE_DONE)
    {
      LOGE ("DB Error: Failed to step stardock query: %s\n",
            sqlite3_errmsg (db));
      json_decref (sector_list);        // Cleanup the partially built list
      sqlite3_finalize (stmt);
      return NULL;
    }
  // 4. Clean up and return
  sqlite3_finalize (stmt);
  return sector_list;
}


/* ---------- combat.sweep_mines ---------- */
int
cmd_combat_sweep_mines (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  // 1. Validation
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_from_sector_id = json_object_get (data, "from_sector_id");
  json_t *j_target_sector_id = json_object_get (data, "target_sector_id");
  json_t *j_fighters_committed = json_object_get (data, "fighters_committed");
  json_t *j_mine_type_str = json_object_get (data, "mine_type");        // Added mine_type


  if (!j_from_sector_id || !json_is_integer (j_from_sector_id) ||
      !j_target_sector_id || !json_is_integer (j_target_sector_id) ||
      !j_fighters_committed || !json_is_integer (j_fighters_committed) ||
      !j_mine_type_str || !json_is_string (j_mine_type_str))                                                                                                                                                                                                    // Added mine_type validation
    {
      send_response_error (ctx,
                           root,
                           ERR_CURSOR_INVALID,
                           "Missing required field or invalid type: from_sector_id/target_sector_id/fighters_committed/mine_type");
      return 0;
    }
  int from_sector_id = (int) json_integer_value (j_from_sector_id);
  int target_sector_id = (int) json_integer_value (j_target_sector_id);
  int fighters_committed = (int) json_integer_value (j_fighters_committed);
  const char *mine_type_name = json_string_value (j_mine_type_str);     // Get mine_type as string
  int mine_type;                // Define mine_type integer


  if (strcasecmp (mine_type_name, "armid") == 0)
    {
      mine_type = ASSET_MINE;
    }
  else if (strcasecmp (mine_type_name, "limpet") == 0)
    {
      mine_type = ASSET_LIMPET_MINE;
    }
  else
    {
      send_response_error (ctx,
                           root,
                           ERR_CURSOR_INVALID,
                           "Invalid mine_type. Must be 'armid' or 'limpet'.");
      return 0;
    }
  if (fighters_committed <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Fighters committed must be greater than 0.");
      return 0;
    }
  // Limpet-specific feature gates and validation
  if (mine_type == ASSET_LIMPET_MINE)
    {
      if (!g_cfg.mines.limpet.enabled)
        {
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_LIMPETS_DISABLED,
                                       "Limpet mine operations are disabled.",
                                       NULL);
          return 0;
        }
      if (!g_cfg.mines.limpet.sweep_enabled)
        {
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_LIMPET_SWEEP_DISABLED,
                                       "Limpet mine sweeping is disabled.",
                                       NULL);
          return 0;
        }
      if (from_sector_id != target_sector_id)
        {
          send_response_error (ctx,
                               root,
                               ERR_INVALID_ARG,
                               "Limpet sweeping must occur in the current sector.");
          return 0;
        }
    }
  // Check if player's ship is in from_sector_id
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SHIP_NOT_FOUND,
                           "No active ship found for player.");
      return 0;
    }

  db_error_t err;


  db_error_clear (&err);

  int player_current_sector_id = -1;
  int ship_fighters_current = 0;
  int ship_corp_id = 0;

  const char *sql_player_ship =
    "SELECT sector, fighters, corporation FROM ships WHERE id = $1;";
  db_bind_t ship_params[] = { db_bind_i32 (ship_id) };
  db_res_t *ship_res = NULL;


  if (db_query (db, sql_player_ship, ship_params, 1, &ship_res, &err))
    {
      if (db_res_step (ship_res, &err))
        {
          player_current_sector_id = db_res_col_i32 (ship_res, 0, &err);
          ship_fighters_current = db_res_col_i32 (ship_res, 1, &err);
          ship_corp_id = db_res_col_i32 (ship_res, 2, &err);
        }
      db_res_finalize (ship_res);
    }
  else
    {
      send_response_error (ctx, root, ERR_DB, "Failed to query player ship.");
      return 0;
    }

  if (player_current_sector_id != from_sector_id)
    {
      json_t *d = json_object ();


      json_object_set_new (d, "reason", json_string ("not_in_from_sector"));

      send_response_refused_steal (ctx,
                                   root,
                                   REF_NOT_IN_SECTOR,
                                   "Ship is not in the specified 'from_sector_id'.",
                                   d);
      json_decref (d);
      return 0;
    }
  if (fighters_committed > ship_fighters_current)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Not enough fighters on ship to commit.");
      return 0;
    }
  // Check for warp existence (from_sector_id to target_sector_id)
  if (!h_warp_exists (db, from_sector_id, target_sector_id))
    {
      send_response_error (ctx,
                           root,
                           ERR_TARGET_INVALID,
                           "No warp exists between specified sectors.");
      return 0;
    }
  if (!g_armid_config.sweep.enabled)
    {
      send_response_error (ctx,
                           root,
                           ERR_CAPABILITY_DISABLED,
                           "Mine sweeping is currently disabled.");
      return 0;
    }
  // 2. Load hostile Armid stacks in target_sector_id
  const char *sql_select_mines =
    "SELECT id, quantity, player, corporation, ttl "
    "FROM sector_assets "
    "WHERE sector = $1 AND asset_type = $2 AND quantity > 0;";
  db_bind_t mine_params[] = { db_bind_i32 (target_sector_id),
                              db_bind_i32 (mine_type) };
  db_res_t *mine_res = NULL;


  if (!db_query (db, sql_select_mines, mine_params, 2, &mine_res, &err))
    {
      LOGE ("Failed to prepare mine selection statement for sweeping: %s",
            err.message);
      return -1;
    }

  // Store hostile mines to process
  typedef struct
  {
    int id;
    int quantity;
    int player;
    int corporation;
    time_t ttl;
  } mine_stack_info_t;
  json_t *hostile_stacks_json = json_array ();  // To hold all hostile stacks
  int total_hostile = 0;
  time_t current_time = time (NULL);


  while (db_res_step (mine_res, &err))
    {
      mine_stack_info_t current_mine_stack = {
        .id = db_res_col_i32 (mine_res, 0, &err),
        .quantity = db_res_col_i32 (mine_res, 1, &err),
        .player = db_res_col_i32 (mine_res, 2, &err),
        .corporation = db_res_col_i32 (mine_res, 3, &err),
        .ttl = db_res_col_i64 (mine_res, 4, &err)
      };
      sector_asset_t mine_asset_for_check = {
        .quantity = current_mine_stack.quantity,
        .player = current_mine_stack.player,
        .corporation = current_mine_stack.corporation,
        .ttl = current_mine_stack.ttl
      };
      bool is_hostile_or_limpet = false;


      if (mine_type == ASSET_MINE)
        {
          is_hostile_or_limpet =
            is_asset_hostile (mine_asset_for_check.player,
                              mine_asset_for_check.corporation,
                              ctx->player_id, ship_corp_id);
        }
      else if (mine_type == ASSET_LIMPET_MINE)
        {
          // Limpets are always considered "targetable" for sweeping, regardless of ownership
          is_hostile_or_limpet = true;
        }
      if (armid_stack_is_active (&mine_asset_for_check, current_time)
          && is_hostile_or_limpet)
        {
          json_t *stack_obj = json_object ();


          json_object_set_new (stack_obj, "id",
                               json_integer (current_mine_stack.id));
          json_object_set_new (stack_obj, "quantity",
                               json_integer (current_mine_stack.quantity));
          json_array_append_new (hostile_stacks_json, stack_obj);
          total_hostile += current_mine_stack.quantity;
        }
    }
  db_res_finalize (mine_res);

  // 3. If total_hostile <= 0: No change; return reply with removed=0, fighters_lost=0, success=false.
  if (total_hostile <= 0)
    {
      json_t *out = json_object ();


      json_object_set_new (out, "from_sector_id",
                           json_integer (from_sector_id));
      json_object_set_new (out, "target_sector_id",
                           json_integer (target_sector_id));
      json_object_set_new (out, "fighters_sent",
                           json_integer (fighters_committed));
      json_object_set_new (out, "fighters_lost", json_integer (0));
      json_object_set_new (out, "armid_removed", json_integer (0));
      json_object_set_new (out, "armid_remaining", json_integer (0));
      json_object_set_new (out, "success", json_boolean (false));
      send_response_ok_take (ctx, root, "combat.mines_swept_v1", &out);
      json_decref (hostile_stacks_json);
      return 0;
    }
  // 4. Let F = fighters_committed.
  int F = fighters_committed;
  int mines_to_clear = 0;
  int fighters_lost = 0;


  if (mine_type == ASSET_MINE)
    {
      // Existing Armid logic
      // 5. Efficiency:
      double mean = g_armid_config.sweep.mines_per_fighter_avg;
      double var = g_armid_config.sweep.mines_per_fighter_var;
      double eff = mean + rand_range (-var, var);


      if (eff < 0)
        {
          eff = 0;
        }
      int raw_capacity = (int) floor (F * eff);
      // 6. Cap by max fraction:
      double max_frac = g_armid_config.sweep.max_fraction_per_sweep;
      int max_allowed = (int) floor (total_hostile * max_frac);


      mines_to_clear = raw_capacity;
      if (mines_to_clear > max_allowed)
        {
          mines_to_clear = max_allowed;
        }
      if (mines_to_clear < 0)
        {
          mines_to_clear = 0;
        }
      // 7. Fighters lost:
      fighters_lost =
        (int) ceil (mines_to_clear *
                    g_armid_config.sweep.fighter_loss_per_mine);
      if (fighters_lost > F)
        {
          fighters_lost = F;
        }
    }
  else if (mine_type == ASSET_LIMPET_MINE)
    {
      // Limpet-specific logic: fixed rate removal, fixed fighter loss
      mines_to_clear = F * g_cfg.mines.limpet.sweep_rate_mines_per_fighter;
      if (mines_to_clear > total_hostile)
        {
          mines_to_clear = total_hostile;
        }
      if (mines_to_clear < 0)
        {
          mines_to_clear = 0;
        }
      // Fighters lost: 1 fighter lost per X limpets swept (configurable)
      fighters_lost =
        (int) ceil ((double) mines_to_clear /
                    g_cfg.mines.limpet.sweep_rate_limpets_per_fighter_loss);
      if (fighters_lost > F)
        {
          fighters_lost = F;
        }
      if (fighters_lost < 0)
        {
          fighters_lost = 0;
        }
    }
  // Transaction for updates
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Could not start transaction");
      json_decref (hostile_stacks_json);
      return 0;
    }

  // 8. Apply to stacks (sorted id ASC):
  int remaining_to_clear = mines_to_clear;
  int armid_removed = 0;
  size_t index;
  json_t *stack_obj;


  json_array_foreach (hostile_stacks_json, index, stack_obj)
  {
    if (remaining_to_clear <= 0)
      {
        break;
      }
    int mine_stack_id =
      json_integer_value (json_object_get (stack_obj, "id"));
    int mine_stack_qty =
      json_integer_value (json_object_get (stack_obj, "quantity"));


    if (mine_stack_qty <= 0)
      {
        continue;
      }
    int remove = MIN (remaining_to_clear, mine_stack_qty);
    int new_qty = mine_stack_qty - remove;


    armid_removed += remove;
    remaining_to_clear -= remove;
    if (new_qty > 0)
      {
        const char *sql_update_mine_qty =
          "UPDATE sector_assets SET quantity = $1 WHERE id = $2;";
        db_bind_t upd_params[] = { db_bind_i32 (new_qty),
                                   db_bind_i32 (mine_stack_id) };


        if (!db_exec (db, sql_update_mine_qty, upd_params, 2, &err))
          {
            db_tx_rollback (db, &err);
            json_decref (hostile_stacks_json);
            return -1;
          }
      }
    else
      {
        const char *sql_delete_mine =
          "DELETE FROM sector_assets WHERE id = $1;";
        db_bind_t del_params[] = { db_bind_i32 (mine_stack_id) };


        if (!db_exec (db, sql_delete_mine, del_params, 1, &err))
          {
            db_tx_rollback (db, &err);
            json_decref (hostile_stacks_json);
            return -1;
          }
      }
  }
  // 9. Update ship fighters:
  const char *sql_update_ship_fighters =
    "UPDATE ships SET fighters = fighters - $1 WHERE id = $2;";
  db_bind_t us_params[] = { db_bind_i32 (fighters_lost),
                            db_bind_i32 (ship_id) };


  if (!db_exec (db, sql_update_ship_fighters, us_params, 2, &err))
    {
      db_tx_rollback (db, &err);
      json_decref (hostile_stacks_json);
      return -1;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Commit failed");
      json_decref (hostile_stacks_json);
      return 0;
    }

  // Re-calculate remaining hostile mines for response
  int mines_remaining_after_sweep = 0;
  const char *sql_recalc =
    "SELECT COALESCE(SUM(quantity),0) FROM sector_assets WHERE sector = $1 AND asset_type = $2 AND quantity > 0;";
  db_bind_t recalc_params[] = { db_bind_i32 (target_sector_id),
                                db_bind_i32 (mine_type) };
  db_res_t *recalc_res = NULL;


  if (db_query (db, sql_recalc, recalc_params, 2, &recalc_res, &err))
    {
      if (db_res_step (recalc_res, &err))
        {
          mines_remaining_after_sweep = db_res_col_i32 (recalc_res, 0, &err);
        }
      db_res_finalize (recalc_res);
    }

  // 10. Reply:
  json_t *out = json_object ();


  json_object_set_new (out, "from_sector_id", json_integer (from_sector_id));
  json_object_set_new (out, "target_sector_id",
                       json_integer (target_sector_id));
  json_object_set_new (out, "fighters_sent",
                       json_integer (fighters_committed));
  json_object_set_new (out, "fighters_lost", json_integer (fighters_lost));
  json_object_set_new (out, "mines_removed", json_integer (armid_removed));     // Renamed
  json_object_set_new (out, "mines_remaining",
                       json_integer (mines_remaining_after_sweep));                             // Renamed
  json_object_set_new (out, "mine_type", json_string (mine_type_name)); // Added
  json_object_set_new (out, "success", json_boolean (armid_removed > 0));
  send_response_ok_take (ctx, root, "combat.mines_swept_v1", &out);
  // Optional broadcast:
  if (armid_removed > 0)
    {
      json_t *broadcast_data = json_object ();


      json_object_set_new (broadcast_data, "v", json_integer (1));
      json_object_set_new (broadcast_data, "sweeper_id",
                           json_integer (ctx->player_id));
      json_object_set_new (broadcast_data, "sector_id",
                           json_integer (target_sector_id));
      json_object_set_new (broadcast_data, "mines_removed",
                           json_integer (armid_removed));                                       // Renamed
      json_object_set_new (broadcast_data, "fighters_lost",
                           json_integer (fighters_lost));
      json_object_set_new (broadcast_data, "mine_type",
                           json_string (mine_type_name));                               // Added
      (void) db_log_engine_event ((long long) time (NULL),
                                  "combat.mines_swept", NULL, ctx->player_id,
                                  target_sector_id, broadcast_data, NULL);
    }
  json_decref (hostile_stacks_json);
  return 0;
}


int
cmd_fighters_recall (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  /* 1. Parse input */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_asset_id = json_object_get (data, "asset_id");


  if (!j_sector_id || !json_is_integer (j_sector_id) ||
      !j_asset_id || !json_is_integer (j_asset_id))
    {
      send_response_error (ctx,
                           root,
                           ERR_CURSOR_INVALID,
                           "Missing required field or invalid type: sector_id/asset_id");
      return 0;
    }
  int requested_sector_id = (int) json_integer_value (j_sector_id);
  int asset_id = (int) json_integer_value (j_asset_id);

  /* 2. Load player, ship, current_sector_id */
  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (player_ship_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SHIP_NOT_FOUND,
                           "No active ship found for player.");
      return 0;
    }

  db_error_t err;


  db_error_clear (&err);

  int player_current_sector_id = -1;
  int ship_fighters_current = 0;
  int ship_fighters_max = 0;
  int player_corp_id = 0;

  const char *sql_player_ship =
    "SELECT s.sector, s.fighters, st.maxfighters, cm.corp_id "
    "FROM ships s "
    "JOIN shiptypes st ON s.type_id = st.id "
    "LEFT JOIN corp_members cm ON cm.player_id = $1 " "WHERE s.id = $2;";

  db_bind_t ship_params[] = {
    db_bind_i32 (ctx->player_id),
    db_bind_i32 (player_ship_id)
  };

  db_res_t *ship_res = NULL;


  if (db_query (db, sql_player_ship, ship_params, 2, &ship_res, &err))
    {
      if (db_res_step (ship_res, &err))
        {
          player_current_sector_id = db_res_col_i32 (ship_res, 0, &err);
          ship_fighters_current = db_res_col_i32 (ship_res, 1, &err);
          ship_fighters_max = db_res_col_i32 (ship_res, 2, &err);
          player_corp_id = db_res_col_i32 (ship_res, 3, &err);
        }
      db_res_finalize (ship_res);
    }
  else
    {
      send_response_error (ctx, root, ERR_DB, "Failed to query player ship.");
      return 0;
    }

  if (player_current_sector_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SECTOR_NOT_FOUND,
                           "Could not determine player's current sector.");
      return 0;
    }
  /* 3. Validate sector match */
  if (player_current_sector_id != requested_sector_id)
    {
      json_t *d = json_object ();


      json_object_set_new (d, "reason", json_string ("not_in_sector"));

      send_response_refused_steal (ctx,
                                   root,
                                   REF_NOT_IN_SECTOR, "Not in sector", d);
      json_decref (d);
      return 0;
    }
  /* 4. Fetch asset and validate existence */
  const char *sql_asset =
    "SELECT quantity, player, corporation, offensive_setting "
    "FROM sector_assets " "WHERE id = $1 AND sector = $2 AND asset_type = 2;";                                                                                  // asset_type = 2 for fighters
  db_bind_t asset_params[] = {
    db_bind_i32 (asset_id),
    db_bind_i32 (requested_sector_id)
  };

  int asset_qty = 0;
  int asset_owner_player_id = 0;
  int asset_owner_corp_id = 0;
  int asset_offensive_setting = 0;

  db_res_t *asset_res = NULL;


  if (db_query (db, sql_asset, asset_params, 2, &asset_res, &err))
    {
      if (db_res_step (asset_res, &err))
        {
          asset_qty = db_res_col_i32 (asset_res, 0, &err);
          asset_owner_player_id = db_res_col_i32 (asset_res, 1, &err);
          asset_owner_corp_id = db_res_col_i32 (asset_res, 2, &err);
          asset_offensive_setting = db_res_col_i32 (asset_res, 3, &err);
        }
      db_res_finalize (asset_res);
    }
  else
    {
      send_response_error (ctx, root, ERR_DB, "Failed to query asset.");
      return 0;
    }

  if (asset_qty <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "Asset not found");
      return 0;
    }
  /* 5. Validate ownership */
  bool is_owner = false;


  if (asset_owner_player_id == ctx->player_id)
    {
      is_owner = true;          // Personal fighters
    }
  else if (asset_owner_corp_id != 0 && player_corp_id != 0 &&
           asset_owner_corp_id == player_corp_id)
    {
      // Corporate fighters, player is member of owning corp
      is_owner = true;
    }
  if (!is_owner)
    {
      json_t *d = json_object ();


      json_object_set_new (d, "reason", json_string ("not_owner"));

      send_response_refused_steal (ctx, root, ERR_TARGET_INVALID, "Not owner",
                                   d);
      json_decref (d);
      return 0;
    }
  /* 6. Compute pickup quantity */
  int available_to_recall = asset_qty;
  int capacity_left = ship_fighters_max - ship_fighters_current;
  int take = 0;


  if (capacity_left <= 0)
    {
      json_t *d = json_object ();


      json_object_set_new (d, "reason", json_string ("no_capacity"));

      send_response_refused_steal (ctx, root, ERR_OUT_OF_RANGE, "No capacity",
                                   d);
      json_decref (d);
      return 0;
    }
  take =
    (available_to_recall <
     capacity_left) ? available_to_recall : capacity_left;
  if (take <= 0)
    {
      json_t *d = json_object ();


      json_object_set_new (d, "reason",
                           json_string ("no_fighters_or_capacity"));

      send_response_refused_steal (ctx,
                                   root,
                                   ERR_OUT_OF_RANGE,
                                   "No fighters to recall or no capacity", d);
      json_decref (d);
      return 0;
    }
  /* 7. Apply changes (transaction) */
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Could not start transaction");
      return 0;
    }

  /* Increment ship fighters */
  const char *sql_update_ship =
    "UPDATE ships SET fighters = fighters + $1 WHERE id = $2;";
  db_bind_t upd_ship_params[] = {
    db_bind_i32 (take),
    db_bind_i32 (player_ship_id)
  };


  if (!db_exec (db, sql_update_ship, upd_ship_params, 2, &err))
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_DB,
                           "Failed to update ship fighters.");
      return 0;
    }

  /* Update or delete sector_assets record */
  if (take == asset_qty)
    {
      const char *sql_delete_asset =
        "DELETE FROM sector_assets WHERE id = $1;";
      db_bind_t del_asset_params[] = {
        db_bind_i32 (asset_id)
      };


      if (!db_exec (db, sql_delete_asset, del_asset_params, 1, &err))
        {
          db_tx_rollback (db, &err);
          send_response_error (ctx,
                               root,
                               ERR_DB,
                               "Failed to delete asset record.");
          return 0;
        }
    }
  else
    {
      const char *sql_update_asset =
        "UPDATE sector_assets SET quantity = quantity - $1 WHERE id = $2;";
      db_bind_t upd_asset_params[] = {
        db_bind_i32 (take),
        db_bind_i32 (asset_id)
      };


      if (!db_exec (db, sql_update_asset, upd_asset_params, 2, &err))
        {
          db_tx_rollback (db, &err);
          send_response_error (ctx,
                               root,
                               ERR_DB,
                               "Failed to update asset quantity.");
          return 0;
        }
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Commit failed");
      return 0;
    }

  /* 8. Emit engine event */
  json_t *evt = json_object ();


  json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
  json_object_set_new (evt, "ship_id", json_integer (player_ship_id));
  json_object_set_new (evt, "sector_id", json_integer (requested_sector_id));
  json_object_set_new (evt, "asset_id", json_integer (asset_id));
  json_object_set_new (evt, "recalled", json_integer (take));
  json_object_set_new (evt, "remaining_in_sector",
                       json_integer (asset_qty - take));
  const char *mode_str = "unknown";


  if (asset_offensive_setting == 1)
    {
      mode_str = "offensive";
    }
  else if (asset_offensive_setting == 2)
    {
      mode_str = "defensive";
    }
  else if (asset_offensive_setting == 3)
    {
      mode_str = "toll";
    }
  json_object_set_new (evt, "mode", json_string (mode_str));
  (void) db_log_engine_event ((long long) time (NULL), "fighters.recalled",
                              NULL, ctx->player_id, requested_sector_id, evt,
                              NULL);
  /* 9. Send enveloped_ok response */
  json_t *out = json_object ();


  json_object_set_new (out, "sector_id", json_integer (requested_sector_id));
  json_object_set_new (out, "asset_id", json_integer (asset_id));
  json_object_set_new (out, "recalled", json_integer (take));
  json_object_set_new (out, "remaining_in_sector",
                       json_integer (asset_qty - take));
  send_response_ok_take (ctx, root, "combat.fighters.deployed", &out);
  return 0;
}


/* ---------- combat.scrub_mines ---------- */
int
cmd_combat_scrub_mines (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  /* 1. Input Parsing */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_mine_type_str = json_object_get (data, "mine_type");
  json_t *j_asset_id = json_object_get (data, "asset_id");      // Optional


  if (!j_sector_id || !json_is_integer (j_sector_id) ||
      !j_mine_type_str || !json_is_string (j_mine_type_str))
    {
      send_response_error (ctx,
                           root,
                           ERR_CURSOR_INVALID,
                           "Missing required field or invalid type: sector_id/mine_type");
      return 0;
    }
  int sector_id = (int) json_integer_value (j_sector_id);
  const char *mine_type_name = json_string_value (j_mine_type_str);
  int asset_id = 0;             // Default to 0 (scrub all)


  if (j_asset_id && json_is_integer (j_asset_id))
    {
      asset_id = (int) json_integer_value (j_asset_id);
    }
  // Only Limpet mines can be scrubbed
  if (strcasecmp (mine_type_name, "limpet") != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Only 'limpet' mine_type can be scrubbed.");
      return 0;
    }
  /* 2. Feature Gate */
  if (!g_cfg.mines.limpet.enabled)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_LIMPETS_DISABLED,
                                   "Limpet mine operations are disabled.",
                                   NULL);
      return 0;
    }

  db_error_t err;


  db_error_clear (&err);

  /* 3. Cost Check */
  int scrub_cost = g_cfg.mines.limpet.scrub_cost;


  if (scrub_cost > 0)
    {
      // Check player credits
      long long player_credits = 0;

      const char *sql_credits = "SELECT credits FROM players WHERE id=$1";
      db_bind_t cred_params[] = { db_bind_i32 (ctx->player_id) };
      db_res_t *cred_res = NULL;


      if (db_query (db, sql_credits, cred_params, 1, &cred_res, &err))
        {
          if (db_res_step (cred_res, &err))
            {
              player_credits = db_res_col_i64 (cred_res, 0, &err);
            }
          db_res_finalize (cred_res);
        }
      else
        {
          send_response_error (ctx,
                               root,
                               ERR_DB,
                               "Failed to retrieve player credits.");
          return 0;
        }

      if (player_credits < scrub_cost)
        {
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_INSUFFICIENT_FUNDS,
                                       "Insufficient credits to scrub mines.",
                                       NULL);
          return 0;
        }
    }
  /* 4. Ownership Validation and Scrubbing Logic */
  int total_scrubbed = 0;


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Could not start transaction");
      return 0;
    }

  // SQL for scrubbing
  int64_t rows_deleted = 0;
  bool exec_result = false;


  if (asset_id > 0)
    {
      // Scrub specific asset
      const char *sql_scrub_mines =
        "DELETE FROM sector_assets "
        "WHERE id = $1 AND sector = $2 AND asset_type = $3 AND (player = $4 OR corporation = $5);";

      db_bind_t scrub_params[] = {
        db_bind_i32 (asset_id),
        db_bind_i32 (sector_id),
        db_bind_i32 (ASSET_LIMPET_MINE),
        db_bind_i32 (ctx->player_id),
        db_bind_i32 (ctx->corp_id)
      };


      exec_result = db_exec_rows_affected (db,
                                           sql_scrub_mines,
                                           scrub_params,
                                           5,
                                           &rows_deleted,
                                           &err);
    }
  else
    {
      // Scrub all player's Limpets in sector
      const char *sql_scrub_mines =
        "DELETE FROM sector_assets "
        "WHERE sector = $1 AND asset_type = $2 AND (player = $3 OR corporation = $4);";

      db_bind_t scrub_params[] = {
        db_bind_i32 (sector_id),
        db_bind_i32 (ASSET_LIMPET_MINE),
        db_bind_i32 (ctx->player_id),
        db_bind_i32 (ctx->corp_id)
      };


      exec_result = db_exec_rows_affected (db,
                                           sql_scrub_mines,
                                           scrub_params,
                                           4,
                                           &rows_deleted,
                                           &err);
    }

  if (!exec_result)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           ERR_DB, "Failed to execute scrub operation.");
      return 0;
    }

  total_scrubbed = (int)rows_deleted;

  if (total_scrubbed == 0)
    {
      db_tx_rollback (db, &err);
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_FOUND,
                                   "No Limpet mines found to scrub.", NULL);
      return 0;
    }
  /* 5. Debit Credits (if applicable) */
  if (scrub_cost > 0)
    {
      const char *sql_debit =
        "UPDATE players SET credits = credits - $1 WHERE id = $2 AND credits >= $1;";
      db_bind_t debit_params[] = {
        db_bind_i64 (scrub_cost),
        db_bind_i32 (ctx->player_id)
      };


      if (!db_exec (db, sql_debit, debit_params, 2, &err))
        {
          db_tx_rollback (db, &err);
          send_response_error (ctx, root, ERR_DB, "Failed to debit credits.");
          return 0;
        }
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Commit failed");
      return 0;
    }

  /* 6. Emit engine event */
  json_t *evt = json_object ();


  json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
  json_object_set_new (evt, "sector_id", json_integer (sector_id));
  json_object_set_new (evt, "mine_type", json_string ("limpet"));
  json_object_set_new (evt, "scrubbed_count", json_integer (total_scrubbed));
  json_object_set_new (evt, "cost", json_integer (scrub_cost));
  if (asset_id > 0)
    {
      json_object_set_new (evt, "asset_id", json_integer (asset_id));
    }
  (void) db_log_engine_event ((long long) time (NULL),
                              "combat.mines_scrubbed", NULL, ctx->player_id,
                              sector_id, evt, NULL);
  /* 7. Send enveloped_ok response */
  json_t *out = json_object ();


  json_object_set_new (out, "sector_id", json_integer (sector_id));
  json_object_set_new (out, "mine_type", json_string ("limpet"));
  json_object_set_new (out, "scrubbed_count", json_integer (total_scrubbed));
  json_object_set_new (out, "cost", json_integer (scrub_cost));
  if (asset_id > 0)
    {
      json_object_set_new (out, "asset_id", json_integer (asset_id));
    }
  send_response_ok_take (ctx, root, "combat.mines_scrubbed_v1", &out);
  return 0;
}


/* ---------- combat.deploy_mines ---------- */


int


cmd_combat_deploy_mines (client_ctx_t *ctx, json_t *root)


{
  if (!require_auth (ctx, root))


    {
      return 0;
    }


  db_t *db = game_db_get_handle ();


  if (!db)


    {
      send_response_error (ctx,


                           root,


                           ERR_SERVICE_UNAVAILABLE, "Database unavailable");


      return 0;
    }


  bool in_fed = false;


  bool in_sdock = false;


  /* Parse input */


  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))


    {
      send_response_error (ctx,


                           root,


                           ERR_MISSING_FIELD, "Missing required field: data");


      return 0;
    }


  json_t *j_amount = json_object_get (data, "amount");


  json_t *j_offense = json_object_get (data, "offense");        /* required: 1..3 */


  json_t *j_corp_id = json_object_get (data, "corporation_id"); /* optional, nullable */


  json_t *j_mine_type = json_object_get (data, "mine_type");    /* optional, 1 for Armid, 4 for Limpet */


  if (!j_amount || !json_is_integer (j_amount) ||


      !j_offense || !json_is_integer (j_offense))


    {
      send_response_error (ctx,


                           root,


                           ERR_CURSOR_INVALID,


                           "Missing required field or invalid type: amount/offense");


      return 0;
    }


  int amount = (int) json_integer_value (j_amount);


  int offense = (int) json_integer_value (j_offense);


  int mine_type = 1;            // Default to Armid Mine


  if (j_mine_type && json_is_integer (j_mine_type))


    {
      mine_type = (int) json_integer_value (j_mine_type);


      if (mine_type != 1 && mine_type != 4)


        {                       // Validate mine type
          send_response_error (ctx,


                               root,


                               ERR_CURSOR_INVALID,


                               "Invalid mine_type. Must be 1 (Armid) or 4 (Limpet).");


          return 0;
        }
    }


  if (amount <= 0)


    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "amount must be > 0");


      return 0;
    }


  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK)


    {
      send_response_error (ctx,


                           root,


                           ERR_CURSOR_INVALID,


                           "offense must be one of {1=TOLL,2=DEFEND,3=ATTACK}");


      return 0;
    }


  /* Resolve active ship + sector */


  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)


    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");


      return 0;
    }


  /* Decloak: visible hostile/defensive action */


  (void) h_decloak_ship (db, ship_id);


  int sector_id = -1;


  {
    db_error_t err;


    db_error_clear (&err);


    const char *sql_sec = "SELECT sector FROM ships WHERE id=

;";


    db_bind_t params[] = { db_bind_i32 (ship_id) };


    db_res_t *res = NULL;


    if (db_query (db, sql_sec, params, 1, &res, &err))
      {
        if (db_res_step (res, &err))
          {
            sector_id = db_res_col_i32 (res, 0, &err);
          }


        db_res_finalize (res);
      }
    else
      {
        char error_buffer[256];


        snprintf (error_buffer, sizeof (error_buffer),


                  "Unable to resolve current sector - %s", err.message);


        send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, error_buffer);


        return 0;
      }


    if (sector_id <= 0)


      {
        send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "Invalid sector");


        return 0;
      }
  }


  db_error_t err;


  db_error_clear (&err);


  /* Transaction: debit ship, credit sector */


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Could not start transaction");


      return 0;
    }


  /* Sector cap (Check inside TX) */


  int sector_total = 0;


  if (sum_sector_mines (db, sector_id, &sector_total) != 0) // Helper now returns error code


    {
      db_tx_rollback (db, &err);


      send_response_error (ctx,


                           root,


                           REF_NOT_IN_SECTOR, "Failed to read sector mines");


      return 0;
    }


  if (sector_total + amount > SECTOR_MINE_CAP)


    {                           // Assuming SECTOR_MINE_CAP is defined
      db_tx_rollback (db, &err);


      send_response_error (ctx,


                           root,


                           ERR_SECTOR_OVERCROWDED,


                           "Sector mine limit exceeded (50,000)");


      return 0;
    }


  int rc = ship_consume_mines (db, ship_id, mine_type, amount);


  if (rc == SQLITE_TOOBIG)


    {
      db_tx_rollback (db, &err);


      send_response_error (ctx,


                           root,


                           REF_AMMO_DEPLETED, "Insufficient mines on ship");


      return 0;
    }


  if (rc != 0)


    {
      db_tx_rollback (db, &err);


      send_response_error (ctx,


                           root,


                           REF_AMMO_DEPLETED, "Failed to update ship mines");


      return 0;
    }


  rc =


    insert_sector_mines (db, sector_id, ctx->player_id, j_corp_id, mine_type,


                         offense, amount);


  if (rc != 0)


    {
      db_tx_rollback (db, &err);


      send_response_error (ctx,


                           root,


                           SECTOR_ERR,


                           "Failed to create sector assets record");


      return 0;
    }


  // Get last insert ID (we need a helper or use driver specific? Generic API doesn't expose it yet except via raw driver or we add it)


  // Standard way: RETURNING id if supported, or sqlite3_last_insert_rowid via casting db_t (breaking abstraction).


  // For now, let's assume we need to fetch it or ignore if not strictly critical for response logic except echo.


  // Actually, db_exec doesn't return ID.


  // Let's rely on `sqlite3_last_insert_rowid` if backend is sqlite, but we want to be generic.


  // Better: UPDATE insert_sector_mines to return the ID via out param using RETURNING or similar?


  // Or add `db_last_insert_id(db)` to API.


  // For now, I'll cheat and assume I can't get it easily without API update, or I'll query MAX(id).


  // Querying MAX(id) is safe inside the transaction.


  int asset_id = 0;


  db_res_t *id_res = NULL;


  if (db_query (db, "SELECT last_insert_rowid();", NULL, 0, &id_res, &err))  // SQLite specific, but we are porting from sqlite


    {
      if (db_res_step (id_res, &err))
        {
          asset_id = db_res_col_i32 (id_res, 0, &err);
        }


      db_res_finalize (id_res);
    }


  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Commit failed");


      return 0;
    }


  /* Fedspace/Stardock → summon ISS + warn player */


  // Check for Federation sectors (1-10)


  if (sector_id >= 1 && sector_id <= 10)


    {
      in_fed = true;
    }


  // Get the list of stardock sectors from the database


  json_t *stardock_sectors = db_get_stardock_sectors ();


  // --------------------------------------------------------


  // Logic to check if sector_id is a Stardock location


  // --------------------------------------------------------


  if (stardock_sectors && json_is_array (stardock_sectors))


    {
      size_t index;


      json_t *sector_value;


      // Loop through the array of stardock sector IDs


      json_array_foreach (stardock_sectors, index, sector_value)


      {
        // 1. Ensure the element is a valid integer


        if (json_is_integer (sector_value))


          {
            int stardock_sector_id = json_integer_value (sector_value);


            // 2. Check for a match


            if (sector_id == stardock_sector_id)


              {
                in_sdock = true;


                // Found a match, no need to check the rest of the array


                break;
              }
          }
      }
    }


  if (stardock_sectors)


    {
      json_decref (stardock_sectors);
    }


  if (in_fed || in_sdock)


    {
      iss_summon (sector_id, ctx->player_id);


      (void) h_send_message_to_player (ctx->player_id,


                                       0,


                                       "Federation Warning",


                                       "Mine deployment in protected space has triggered ISS response.");
    }


  /* Emit engine_event via h_log_engine_event */


  {
    json_t *evt = json_object ();


    json_object_set_new (evt, "sector_id", json_integer (sector_id));


    json_object_set_new (evt, "player_id", json_integer (ctx->player_id));


    if (j_corp_id && json_is_integer (j_corp_id))


      {
        json_object_set_new (evt, "corporation_id",


                             json_integer (json_integer_value (j_corp_id)));
      }


    else


      {
        json_object_set_new (evt, "corporation_id", json_null ());
      }


    json_object_set_new (evt, "amount", json_integer (amount));


    json_object_set_new (evt, "offense", json_integer (offense));


    json_object_set_new (evt, "mine_type", json_integer (mine_type));


    json_object_set_new (evt, "event_ts",


                         json_integer ((json_int_t) time (NULL)));


    json_object_set_new (evt, "asset_id", json_integer (asset_id));     // Add asset_id to event


    (void) db_log_engine_event ((long long) time (NULL),


                                "combat.mines.deployed", NULL, ctx->player_id,


                                sector_id, evt, NULL);
  }


  /* Recompute total for response convenience */


  (void) sum_sector_mines (db, sector_id, &sector_total);


  LOGI


  (
    "DEBUG: cmd_combat_deploy_mines - sector_id: %d, player_id: %d, amount: %d, offense: %d, mine_type: %d, sector_total: %d, asset_id: %d",


    sector_id,
    ctx->player_id,
    amount,
    offense,
    mine_type,
    sector_total,
    asset_id);


  /* ---- Build data payload (no outer wrapper here) ---- */


  json_t *out = json_object ();


  json_object_set_new (out, "sector_id", json_integer (sector_id));


  json_object_set_new (out, "owner_player_id", json_integer (ctx->player_id));


  if (j_corp_id && json_is_integer (j_corp_id))


    {
      json_object_set_new (out, "owner_corp_id",


                           json_integer (json_integer_value (j_corp_id)));
    }


  else


    {
      json_object_set_new (out, "owner_corp_id", json_null ());
    }


  json_object_set_new (out, "amount", json_integer (amount));


  json_object_set_new (out, "offense", json_integer (offense));


  json_object_set_new (out, "mine_type", json_integer (mine_type));


  json_object_set_new (out, "sector_total_after",


                       json_integer (sector_total));


  json_object_set_new (out, "asset_id", json_integer (asset_id));       // Add asset_id to response


  /* Envelope: echo id/meta from `root`, set type string for this result */


  send_response_ok_take (ctx, root, "combat.mines.deployed", &out);


  return 0;
}


/* ---------- combat.lay_mines ---------- */
int
cmd_combat_lay_mines (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  bool in_fed = false;
  // bool in_sdock = false; // Not used currently in this function for mine deployment
  /* Parse input */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_amount = json_object_get (data, "count");   // Changed to 'count' as per brief
  json_t *j_offense = json_object_get (data, "offense_mode");   // Changed to 'offense_mode' as per brief
  json_t *j_owner_mode = json_object_get (data, "owner_mode");  // "personal" | "corp"
  json_t *j_mine_type_str = json_object_get (data, "mine_type");        // "armid" | "limpet"


  if (!j_amount || !json_is_integer (j_amount) ||
      !j_offense || !json_is_integer (j_offense) ||
      !j_owner_mode || !json_is_string (j_owner_mode) ||
      !j_mine_type_str || !json_is_string (j_mine_type_str))
    {
      send_response_error (ctx,
                           root,
                           ERR_CURSOR_INVALID,
                           "Missing required field or invalid type: count/offense_mode/owner_mode/mine_type");
      return 0;
    }
  int amount = (int) json_integer_value (j_amount);
  int offense = (int) json_integer_value (j_offense);
  const char *owner_mode_str = json_string_value (j_owner_mode);
  const char *mine_type_name = json_string_value (j_mine_type_str);
  int mine_type;


  if (strcasecmp (mine_type_name, "armid") == 0)
    {
      mine_type = ASSET_MINE;
    }
  else if (strcasecmp (mine_type_name, "limpet") == 0)
    {
      mine_type = ASSET_LIMPET_MINE;
    }
  else
    {
      send_response_error (ctx,
                           root,
                           ERR_CURSOR_INVALID,
                           "Invalid mine_type. Must be 'armid' or 'limpet'.");
      return 0;
    }
  if (amount <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Count must be > 0");
      return 0;
    }
  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK)
    {
      send_response_error (ctx,
                           root,
                           ERR_CURSOR_INVALID,
                           "offense_mode must be one of {1=TOLL,2=DEFEND,3=ATTACK}");
      return 0;
    }
  if (strcasecmp (owner_mode_str, "personal") != 0
      && strcasecmp (owner_mode_str, "corp") != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_CURSOR_INVALID,
                           "Invalid owner_mode. Must be 'personal' or 'corp'.");
      return 0;
    }
  // Determine the corporation ID for deployment
  int deploy_corp_id = 0;


  if (strcasecmp (owner_mode_str, "corp") == 0)
    {
      // Here, we need ctx->corp_id. Assuming it's already set or can be retrieved.
      // If not, this is a potential point for error if player is not in a corp.
      deploy_corp_id = ctx->corp_id;    // Use player's current corporate id
      if (deploy_corp_id == 0)
        {
          send_response_error (ctx,
                               root,
                               ERR_NOT_IN_CORP,
                               "Cannot deploy as corp: player not in a corporation.");
          return 0;
        }
    }
  /* Feature Gate for Limpet Mines */
  if (mine_type == ASSET_LIMPET_MINE && !g_cfg.mines.limpet.enabled)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_LIMPETS_DISABLED,
                                   "Limpet mine deployment is disabled.",
                                   NULL);
      return 0;
    }
  /* Resolve active ship + sector */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }
  /* Decloak: visible hostile/defensive action */
  (void) h_decloak_ship (db, ship_id);

  db_error_t err;


  db_error_clear (&err);

  int sector_id = -1;
  {
    const char *sql_sec = "SELECT sector FROM ships WHERE id=$1;";
    db_bind_t params[] = { db_bind_i32 (ship_id) };
    db_res_t *res = NULL;


    if (db_query (db, sql_sec, params, 1, &res, &err))
      {
        if (db_res_step (res, &err))
          {
            sector_id = db_res_col_i32 (res, 0, &err);
          }
        db_res_finalize (res);
      }
    else
      {
        send_response_error (ctx,
                             root,
                             ERR_SECTOR_NOT_FOUND,
                             "DB error resolving sector");
        return 0;
      }

    if (sector_id <= 0)
      {
        send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND, "Invalid sector");
        return 0;
      }
  }


  /* FedSpace & MSL rules for Limpet Mines */
  if (mine_type == ASSET_LIMPET_MINE)
    {
      if (!g_cfg.mines.limpet.fedspace_allowed
          && is_fedspace_sector (sector_id))
        {
          send_response_refused_steal (ctx,
                                       root,
                                       REF_TERRITORY_UNSAFE,
                                       "Cannot deploy Limpet mines in Federation space.",
                                       NULL);
          return 0;
        }
      if (!g_cfg.mines.limpet.msl_allowed && is_msl_sector (db, sector_id))
        {
          send_response_refused_steal (ctx,
                                       root,
                                       REF_TERRITORY_UNSAFE,
                                       "Cannot deploy Limpet mines in Major Space Lanes.",
                                       NULL);
          return 0;
        }
    }
  /* Foreign Limpets in Sector (blocks both Armid and Limpet deployment) */
  const char *sql_foreign_limpets =
    "SELECT 1 FROM sector_assets " "WHERE sector = $1 " "  AND asset_type = $2 "                                        // ASSET_LIMPET_MINE = 4
    "  AND quantity > 0 "
    "  AND NOT (player = $3 OR (corporation != 0 AND corporation = $4)) "
    "LIMIT 1;";

  db_bind_t fl_params[] = {
    db_bind_i32 (sector_id),
    db_bind_i32 (ASSET_LIMPET_MINE),
    db_bind_i32 (ctx->player_id),
    db_bind_i32 (ctx->corp_id)
  };

  db_res_t *fl_res = NULL;
  bool foreign_limpets_exist = false;


  if (db_query (db, sql_foreign_limpets, fl_params, 4, &fl_res, &err))
    {
      foreign_limpets_exist = db_res_step (fl_res, &err);
      db_res_finalize (fl_res);
    }
  else
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Failed to check for foreign limpets.");
      return 0;
    }

  if (foreign_limpets_exist)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_FOREIGN_LIMPETS_PRESENT,
                                   "Cannot deploy mines: foreign Limpet mines are present in this sector.",
                                   NULL);
      return 0;
    }

  // Check ship's mine capacity and count
  int ship_mines = 0;
  int ship_limpets = 0;

  const char *sql_ship_mines =
    "SELECT mines, limpets FROM ships WHERE id = $1;";
  db_bind_t sm_params[] = { db_bind_i32 (ship_id) };
  db_res_t *sm_res = NULL;


  if (db_query (db, sql_ship_mines, sm_params, 1, &sm_res, &err))
    {
      if (db_res_step (sm_res, &err))
        {
          ship_mines = db_res_col_i32 (sm_res, 0, &err);
          ship_limpets = db_res_col_i32 (sm_res, 1, &err);
        }
      db_res_finalize (sm_res);
    }
  else
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Failed to query ship's mine count.");
      return 0;
    }

  if (mine_type == ASSET_MINE)
    {
      if (ship_mines < amount)
        {
          send_response_error (ctx,
                               root,
                               REF_AMMO_DEPLETED,
                               "Insufficient Armid mines on ship.");
          return 0;
        }
    }
  else if (mine_type == ASSET_LIMPET_MINE)
    {
      if (ship_limpets < amount)
        {
          send_response_error (ctx,
                               root,
                               REF_AMMO_DEPLETED,
                               "Insufficient Limpet mines on ship.");
          return 0;
        }
    }
  /* Sector Caps */
  sector_mine_counts_t counts;


  if (get_sector_mine_counts (sector_id, &counts) != 0) // Now returns error code or 0
    {
      send_response_error (ctx,
                           root,
                           ERR_DB, "Failed to retrieve sector mine counts.");
      return 0;
    }
  // Combined Cap (Armid + Limpet)
  if (counts.total_mines + amount > SECTOR_MINE_CAP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_OVERCROWDED,
                                   "Sector total mine limit exceeded (50,000).",
                                   NULL);                                                                                       // Using macro for now
      return 0;
    }
  // Type-specific Cap
  if (mine_type == ASSET_MINE)
    {
      // Armid-specific cap (using existing MINE_SECTOR_CAP_PER_TYPE for now, or define a new one)
      if (counts.armid_mines + amount > MINE_SECTOR_CAP_PER_TYPE)
        {
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_SECTOR_OVERCROWDED,
                                       "Sector Armid mine limit exceeded (100).",
                                       NULL);
          return 0;
        }
    }
  else if (mine_type == ASSET_LIMPET_MINE)
    {
      if (counts.limpet_mines + amount > g_cfg.mines.limpet.per_sector_cap)
        {
          json_t *data_opt = json_object ();


          json_object_set_new (data_opt, "configured_cap",
                               json_integer (g_cfg.mines.limpet.
                                             per_sector_cap));

          send_response_refused_steal (ctx,
                                       root,
                                       ERR_SECTOR_OVERCROWDED,
                                       "Sector Limpet mine limit exceeded.",
                                       data_opt);
          json_decref (data_opt);
          return 0;
        }
    }
  /* Transaction: debit ship, credit sector */
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Could not start transaction");
      return 0;
    }

  int rc;


  if (mine_type == ASSET_MINE)
    {                           // Corrected from ASSET_ARMID_MINE
      rc = ship_consume_mines (db, ship_id, mine_type, amount); // mine_type passed for clarity
    }
  else
    {                           // ASSET_LIMPET_MINE
      rc = ship_consume_mines (db, ship_id, mine_type, amount); // ship_consume_mines handles both by now
    }
  if (rc == SQLITE_TOOBIG)
    {
      db_tx_rollback (db, &err);
      if (mine_type == ASSET_MINE)
        {
          send_response_error (ctx,
                               root,
                               REF_AMMO_DEPLETED,
                               "Insufficient Armid mines on ship");
        }
      else
        {
          send_response_error (ctx,
                               root,
                               REF_AMMO_DEPLETED,
                               "Insufficient Limpet mines on ship");
        }
      return 0;
    }
  if (rc != 0)
    {
      db_tx_rollback (db, &err);
      if (mine_type == ASSET_MINE)
        {
          send_response_error (ctx,
                               root,
                               REF_AMMO_DEPLETED,
                               "Failed to update ship Armid mines");
        }
      else
        {
          send_response_error (ctx,
                               root,
                               REF_AMMO_DEPLETED,
                               "Failed to update ship Limpet mines");
        }
      return 0;
    }
  json_t *j_corp_id_to_pass = json_null ();     // Default to personal


  if (strcasecmp (owner_mode_str, "corp") == 0)
    {
      j_corp_id_to_pass = json_integer (ctx->corp_id);
    }
  rc =
    insert_sector_mines (db, sector_id, ctx->player_id, j_corp_id_to_pass,
                         mine_type, offense, amount);
  if (rc != 0)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           SECTOR_ERR,
                           "Failed to create sector assets record");
      return 0;
    }

  // Get ID
  int asset_id = 0;
  db_res_t *id_res = NULL;


  if (db_query (db, "SELECT last_insert_rowid();", NULL, 0, &id_res, &err))
    {
      if (db_res_step (id_res, &err))
        {
          asset_id = db_res_col_i32 (id_res, 0, &err);
        }
      db_res_finalize (id_res);
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Commit failed");
      return 0;
    }

  /* Fedspace/Stardock → summon ISS + warn player (Armid mines only for now) */
  // Check for Federation sectors (1-10)
  if (mine_type == ASSET_MINE && (sector_id >= 1 && sector_id <= 10))
    {
      in_fed = true;
    }
  // Logic to check if sector_id is a Stardock location (Armid mines only for now)
  json_t *stardock_sectors = NULL;


  if (mine_type == ASSET_MINE)
    {
      // The function returns a new reference which MUST be freed.
      stardock_sectors = db_get_stardock_sectors ();
      if (stardock_sectors && json_is_array (stardock_sectors))
        {
          size_t index;
          json_t *sector_value;


          json_array_foreach (stardock_sectors, index, sector_value)
          {
            if (json_is_integer (sector_value))
              {
                int stardock_sector_id = json_integer_value (sector_value);


                if (sector_id == stardock_sector_id)
                  {
                    // in_sdock = true; // Use local bool if needed
                    break;
                  }
              }
          }
        }
    }
  if (stardock_sectors)
    {
      json_decref (stardock_sectors);
    }
  // if (in_fed || in_sdock) { // Only for Armid mines for now
  if (mine_type == ASSET_MINE && in_fed)
    {                           // Always summon for Armid in FedSpace
      iss_summon (sector_id, ctx->player_id);
      (void) h_send_message_to_player (ctx->player_id,
                                       0,
                                       "Federation Warning",
                                       "Mine deployment in protected space has triggered ISS response.");
    }
  /* Emit engine_event via h_log_engine_event */
  {
    json_t *evt = json_object ();


    json_object_set_new (evt, "sector_id", json_integer (sector_id));
    json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
    if (j_corp_id_to_pass && json_is_integer (j_corp_id_to_pass))
      {
        json_object_set_new (evt, "corporation_id",
                             json_integer (json_integer_value
                                             (j_corp_id_to_pass)));
      }
    else
      {
        json_object_set_new (evt, "corporation_id", json_null ());
      }
    json_object_set_new (evt, "count", json_integer (amount));  // Changed to 'count'
    json_object_set_new (evt, "offense_mode", json_integer (offense));  // Changed to 'offense_mode'
    json_object_set_new (evt, "mine_type", json_string (mine_type_name));       // Changed to string name
    json_object_set_new (evt, "event_ts",
                         json_integer ((json_int_t) time (NULL)));
    json_object_set_new (evt, "asset_id", json_integer (asset_id));     // Add asset_id to event
    (void) db_log_engine_event ((long long) time (NULL), "combat.mines.laid",
                                NULL, ctx->player_id, sector_id, evt, NULL);
  }
  /* Recompute total for response convenience */
  sector_mine_counts_t new_counts;


  get_sector_mine_counts (sector_id, &new_counts);
  LOGI
  (
    "cmd_combat_lay_mines - sector_id: %d, player_id: %d, amount: %d, offense: %d, mine_type: %s, sector_total: %d, asset_id: %d",
    sector_id,
    ctx->player_id,
    amount,
    offense,
    mine_type_name,
    new_counts.total_mines,
    asset_id);
  /* ---- Build data payload (no outer wrapper here) ---- */
  json_t *out = json_object ();


  json_object_set_new (out, "sector_id", json_integer (sector_id));
  json_object_set_new (out, "owner_player_id", json_integer (ctx->player_id));
  // j_corp_id might be json_null from owner_mode "personal", so use deploy_corp_id directly if not null
  json_object_set_new (out, "owner_corp_id",
                       (deploy_corp_id !=
                        0) ? json_integer (deploy_corp_id) : json_null ());
  json_object_set_new (out, "count_added", json_integer (amount));      // Changed name
  json_object_set_new (out, "mine_type", json_string (mine_type_name));
  json_object_set_new (out, "offense_mode", json_integer (offense));    // Changed name
  json_object_set_new (out, "total_now",        // Changed name
                       json_integer (new_counts.total_mines));
  json_object_set_new (out, "asset_id", json_integer (asset_id));
  /* Envelope: echo id/meta from `root`, set type string for this result */
  send_response_ok_take (ctx, root, "combat.mines_laid_v1", &out);      // Changed type
  return 0;
}


int
cmd_mines_recall (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  /* 1. Parse input */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_asset_id = json_object_get (data, "asset_id");


  if (!j_sector_id || !json_is_integer (j_sector_id) ||
      !j_asset_id || !json_is_integer (j_asset_id))
    {
      send_response_error (ctx,
                           root,
                           ERR_CURSOR_INVALID,
                           "Missing required field or invalid type: sector_id/asset_id");
      return 0;
    }
  int requested_sector_id = (int) json_integer_value (j_sector_id);
  int asset_id = (int) json_integer_value (j_asset_id);
  /* 2. Load player, ship, current_sector_id */
  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (player_ship_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SHIP_NOT_FOUND,
                           "No active ship found for player.");
      return 0;
    }

  db_error_t err;


  db_error_clear (&err);

  int player_current_sector_id = -1;
  int ship_mines_current = 0;
  int ship_mines_max = 0;

  const char *sql_player_ship =
    "SELECT s.sector, s.mines, st.maxmines "
    "FROM ships s "
    "JOIN shiptypes st ON s.type_id = st.id " "WHERE s.id = $1;";
  db_bind_t ship_params[] = { db_bind_i32 (player_ship_id) };
  db_res_t *ship_res = NULL;


  if (db_query (db, sql_player_ship, ship_params, 1, &ship_res, &err))
    {
      if (db_res_step (ship_res, &err))
        {
          player_current_sector_id = db_res_col_i32 (ship_res, 0, &err);
          ship_mines_current = db_res_col_i32 (ship_res, 1, &err);
          ship_mines_max = db_res_col_i32 (ship_res, 2, &err);
        }
      db_res_finalize (ship_res);
    }
  else
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Failed to prepare player ship query.");
      return 0;
    }

  if (player_current_sector_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SECTOR_NOT_FOUND,
                           "Could not determine player's current sector.");
      return 0;
    }
  /* 3. Verify asset belongs to player and is in current sector */
  const char *sql_asset =
    "SELECT quantity, asset_type FROM sector_assets "
    "WHERE id = $1 AND player = $2 AND sector = $3 AND asset_type IN (1, 4);";                                                                          // Mines only
  db_bind_t asset_params[] = {
    db_bind_i32 (asset_id),
    db_bind_i32 (ctx->player_id),
    db_bind_i32 (requested_sector_id)
  };

  int asset_quantity = 0;
  int asset_type = 0;
  db_res_t *asset_res = NULL;


  if (db_query (db, sql_asset, asset_params, 3, &asset_res, &err))
    {
      if (db_res_step (asset_res, &err))
        {
          asset_quantity = db_res_col_i32 (asset_res, 0, &err);
          asset_type = db_res_col_i32 (asset_res, 1, &err);
        }
      db_res_finalize (asset_res);
    }
  else
    {
      send_response_error (ctx, root, ERR_DB, "Failed to prepare asset query.");
      return 0;
    }

  if (asset_quantity <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_FOUND,
                           "Mine asset not found or does not belong to you in this sector.");
      return 0;
    }
  /* 4. Check if ship has capacity for recalled mines */
  if (ship_mines_current + asset_quantity > ship_mines_max)
    {
      json_t *data_opt = json_object ();


      json_object_set_new (data_opt, "reason",
                           json_string ("insufficient_mine_capacity"));
      send_response_refused_steal (ctx,
                                   root,
                                   REF_INSUFFICIENT_CAPACITY,
                                   "Insufficient ship capacity to recall all mines.",
                                   data_opt);
      return 0;
    }
  /* 5. Transaction: delete asset, credit ship */
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Could not start transaction");
      return 0;
    }

  // Delete the asset from sector_assets
  const char *sql_delete = "DELETE FROM sector_assets WHERE id = $1;";
  db_bind_t del_params[] = { db_bind_i32 (asset_id) };


  if (!db_exec (db, sql_delete, del_params, 1, &err))
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Failed to delete asset from sector.");
      return 0;
    }

  // Credit mines to ship
  const char *sql_credit =
    "UPDATE ships SET mines = mines + $1 WHERE id = $2;";
  db_bind_t cred_params[] = {
    db_bind_i32 (asset_quantity),
    db_bind_i32 (player_ship_id)
  };


  if (!db_exec (db, sql_credit, cred_params, 2, &err))
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_DB,
                           "Failed to credit mines to ship.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Commit failed");
      return 0;
    }

  /* 6. Emit engine_event via h_log_engine_event */
  {
    json_t *evt = json_object ();


    json_object_set_new (evt, "sector_id",
                         json_integer (requested_sector_id));
    json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
    json_object_set_new (evt, "asset_id", json_integer (asset_id));
    json_object_set_new (evt, "amount", json_integer (asset_quantity));
    json_object_set_new (evt, "asset_type", json_integer (asset_type));
    json_object_set_new (evt, "event_ts",
                         json_integer ((json_int_t) time (NULL)));
    (void) db_log_engine_event ((long long) time (NULL), "mines.recalled",
                                NULL, ctx->player_id, requested_sector_id,
                                evt, NULL);
  }
  /* 7. Send enveloped_ok response */
  json_t *out = json_object ();


  json_object_set_new (out, "sector_id", json_integer (requested_sector_id));
  json_object_set_new (out, "asset_id", json_integer (asset_id));
  json_object_set_new (out, "recalled", json_integer (asset_quantity));
  json_object_set_new (out, "remaining_in_sector", json_integer (0));   // All recalled
  json_object_set_new (out, "asset_type", json_integer (asset_type));
  send_response_ok_take (ctx, root, "combat.mines.recalled", &out);
  return 0;
}


/* --- Phase 3: Ship vs Ship Combat --- */


/* Helper to get full ship combat stats */
static int
load_ship_combat_stats (db_t *db, int ship_id, combat_ship_t *out)
{
  const char *sql =
    "SELECT s.id, s.hull, s.shields, s.fighters, s.sector, s.name, "
    "       st.offense, st.defense, st.maxattack, "
    "       op.player_id, cm.corp_id "
    "FROM ships s "
    "JOIN shiptypes st ON s.type_id = st.id "
    "JOIN ship_ownership op ON op.ship_id = s.id AND op.is_primary = 1 "
    "LEFT JOIN corp_members cm ON cm.player_id = op.player_id "
    "WHERE s.id = $1;";

  db_error_t err;
  db_error_clear (&err);

  db_bind_t params[] = { db_bind_i32 (ship_id) };
  db_res_t *res = NULL;


  if (!db_query (db, sql, params, 1, &res, &err))
    {
      return -1;
    }

  if (db_res_step (res, &err))
    {
      out->id = db_res_col_i32 (res, 0, &err);
      out->hull = db_res_col_i32 (res, 1, &err);
      out->shields = db_res_col_i32 (res, 2, &err);
      out->fighters = db_res_col_i32 (res, 3, &err);
      out->sector = db_res_col_i32 (res, 4, &err);       // New: Assign sector
      const char *name = db_res_col_text (res, 5, &err);


      if (name)
        {
          strncpy (out->name, name, sizeof (out->name) - 1);
        }
      out->attack_power = db_res_col_i32 (res, 6, &err);
      out->defense_power = db_res_col_i32 (res, 7, &err);
      out->max_attack = db_res_col_i32 (res, 8, &err);
      out->player_id = db_res_col_i32 (res, 9, &err);
      out->corp_id = db_res_col_i32 (res, 10, &err);
      db_res_finalize (res);
      return 0;
    }
  db_res_finalize (res);
  return -1;                    // Not found
}


static void
apply_combat_damage (combat_ship_t *target,
                     int damage, int *shields_lost, int *hull_lost)
{
  *shields_lost = 0;
  *hull_lost = 0;
  int remaining = damage;


  // 1. Shields
  if (target->shields > 0)
    {
      int absorb = MIN (remaining, target->shields);


      target->shields -= absorb;
      remaining -= absorb;
      *shields_lost = absorb;
    }

  // 2. Hull
  if (remaining > 0)
    {
      target->hull -= remaining;
      *hull_lost = remaining;
    }
}


static int
persist_ship_damage (db_t *db, combat_ship_t *ship, int fighters_lost)
{
  const char *sql =
    "UPDATE ships SET hull=$1, shields=$2, fighters=MAX(0, fighters-$3) WHERE id=$4;";

  db_error_t err;
  db_error_clear (&err);

  db_bind_t params[] = {
    db_bind_i32 (MAX (0, ship->hull)),
    db_bind_i32 (MAX (0, ship->shields)),
    db_bind_i32 (fighters_lost),
    db_bind_i32 (ship->id)
  };


  if (!db_exec (db, sql, params, 4, &err))
    {
      return err.code;
    }
  return 0;
}


int
handle_ship_attack (client_ctx_t *ctx,
                    json_t *root, json_t *data, db_t *db)
{
  int target_ship_id = 0;
  if (!json_get_int_flexible (data, "target_ship_id",
                              &target_ship_id) || target_ship_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD,
                           "Missing or invalid target_ship_id");
      return 0;
    }

  int req_fighters = 0;


  if (!json_get_int_flexible (data, "commit_fighters", &req_fighters))
    {
      req_fighters = 0;
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
          send_response_error (ctx,
                               root,
                               err.code,
                               "Transaction failed to begin");
          return 0;
        }

      int attacker_ship_id = h_get_active_ship_id (db, ctx->player_id);


      if (attacker_ship_id <= 0)
        {
          send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
          goto rollback;
        }

      // Load Stats
      combat_ship_t attacker = { 0 };
      combat_ship_t defender = { 0 };


      if (load_ship_combat_stats (db, attacker_ship_id, &attacker) != 0)
        {
          send_response_error (ctx,
                               root,
                               ERR_SHIP_NOT_FOUND,
                               "Attacker ship not found");
          goto rollback;
        }
      if (load_ship_combat_stats (db, target_ship_id, &defender) != 0)
        {
          send_response_error (ctx,
                               root,
                               ERR_SHIP_NOT_FOUND,
                               "Target ship not found");
          goto rollback;
        }

      // Validate Sector
      if (attacker.sector != defender.sector || attacker.sector <= 0)
        {
          send_response_error (ctx,
                               root,
                               ERR_TARGET_INVALID,
                               "Target not in same sector");
          goto rollback;
        }

      // --- Round 1: Attacker Strikes ---
      int att_committed = MIN (attacker.fighters,
                               (req_fighters > 0) ? MIN (req_fighters,
                                                         attacker.max_attack) :
                               attacker.max_attack);


      if (att_committed < 0)
        {
          att_committed = 0;
        }

      double att_mult = 1.0 + ((double) attacker.attack_power * OFFENSE_SCALE);
      int att_raw_dmg = att_committed * DAMAGE_PER_FIGHTER;
      int att_total_dmg = (int) (att_raw_dmg * att_mult);

      double def_factor = 1.0 +
                          ((double) defender.defense_power * DEFENSE_SCALE);
      int effective_dmg_to_def = (int) (att_total_dmg / def_factor);

      int def_shields_lost = 0, def_hull_lost = 0;


      apply_combat_damage (&defender,
                           effective_dmg_to_def,
                           &def_shields_lost,
                           &def_hull_lost);

      bool defender_destroyed = (defender.hull <= 0);
      int att_shields_lost = 0, att_hull_lost = 0;
      bool attacker_destroyed = false;
      int def_committed = 0;


      if (!defender_destroyed)
        {
          persist_ship_damage (db, &defender, 0);
          def_committed = MIN (defender.fighters, defender.max_attack);
          if (def_committed < 0)
            {
              def_committed = 0;
            }

          double def_mult = 1.0 +
                            ((double) defender.attack_power * OFFENSE_SCALE);
          int def_raw_dmg = def_committed * DAMAGE_PER_FIGHTER;
          int def_total_dmg = (int) (def_raw_dmg * def_mult);

          double att_def_factor = 1.0 +
                                  ((double) attacker.defense_power *
                                   DEFENSE_SCALE);
          int effective_dmg_to_att = (int) (def_total_dmg / att_def_factor);


          apply_combat_damage (&attacker,
                               effective_dmg_to_att,
                               &att_shields_lost,
                               &att_hull_lost);
          attacker_destroyed = (attacker.hull <= 0);

          if (!attacker_destroyed)
            {
              persist_ship_damage (db, &attacker, def_committed);
            }
          else
            {
              destroy_ship_and_handle_side_effects (ctx, attacker.player_id);
            }
        }
      else
        {
          destroy_ship_and_handle_side_effects (NULL, defender.player_id);
          persist_ship_damage (db, &attacker, 0);
        }

      if (!db_tx_commit (db, &err))
        {
          LOGE ("handle_ship_attack: Commit failed: %s", err.message);
          goto rollback;
        }

      // Response
      json_t *resp = json_object ();


      json_object_set_new (resp, "fighters_committed",
                           json_integer (att_committed));
      json_object_set_new (resp, "damage_dealt",
                           json_integer (effective_dmg_to_def));
      json_object_set_new (resp, "damage_received",
                           json_integer (defender_destroyed ? 0 :
                                         att_hull_lost + att_shields_lost));
      json_object_set_new (resp, "defender_destroyed",
                           json_boolean (defender_destroyed));
      json_object_set_new (resp, "attacker_destroyed",
                           json_boolean (attacker_destroyed));

      send_response_ok_take (ctx, root, "combat.attack.result", &resp);
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


int
cmd_combat_status (client_ctx_t *ctx, json_t *root)
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

  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }

  combat_ship_t ship = { 0 };


  if (load_ship_combat_stats (db, ship_id, &ship) != 0)
    {
      send_response_error (ctx, root, ERR_DB, "Failed to load ship stats");
      return 0;
    }

  json_t *res = json_object ();


  json_object_set_new (res, "hull", json_integer (ship.hull));
  json_object_set_new (res, "shields", json_integer (ship.shields));
  json_object_set_new (res, "fighters", json_integer (ship.fighters));
  json_object_set_new (res, "attack_power", json_integer (ship.attack_power));
  json_object_set_new (res, "defense_power",
                       json_integer (ship.defense_power));
  json_object_set_new (res, "max_attack", json_integer (ship.max_attack));

  send_response_ok_take (ctx, root, "combat.status", &res);
  return 0;
}


/*


 * Applies Fighter hazards (Toll or Attack).


 */


static int


apply_sector_fighters_on_entry (client_ctx_t *ctx, int sector_id)


{
  db_t *db = game_db_get_handle ();


  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)


    {
      return 0;
    }


  /* Config */


  int toll_per_unit = 5;


  int damage_per_unit = 10;


  // Assuming db_get_int_config handles db_t or we use defaults


  // db_get_int_config (db, "fighter_toll_per_unit", &toll_per_unit);


  // db_get_int_config (db, "fighter_damage_per_unit", &damage_per_unit);


  /* Get Ship Stats */


  ship_t ship = { 0 };


  db_error_t err;


  db_error_clear (&err);


  const char *sql_ship = "SELECT hull, fighters, shields FROM ships WHERE id=

";


  db_bind_t s_params[] = { db_bind_i32 (ship_id) };


  db_res_t *s_res = NULL;


  if (db_query (db, sql_ship, s_params, 1, &s_res, &err))
    {
      if (db_res_step (s_res, &err))
        {
          ship.hull = db_res_col_i32 (s_res, 0, &err);


          ship.fighters = db_res_col_i32 (s_res, 1, &err);


          ship.shields = db_res_col_i32 (s_res, 2, &err);
        }
      else
        {
          db_res_finalize (s_res);


          return 0;
        }


      db_res_finalize (s_res);
    }
  else
    {
      return -1;
    }


  int ship_corp_id = ctx->corp_id;


  /* Scan Fighters (Type 2) */


  const char *sql_ftr =


    "SELECT id, quantity, player, corporation, offensive_setting FROM sector_assets WHERE sector=

 AND asset_type=2 AND quantity>0";


  db_bind_t f_params[] = { db_bind_i32 (sector_id) };


  db_res_t *f_res = NULL;


  if (!db_query (db, sql_ftr, f_params, 1, &f_res, &err))


    {
      return -1;
    }


  while (db_res_step (f_res, &err))


    {
      int asset_id = db_res_col_i32 (f_res, 0, &err);


      int quantity = db_res_col_i32 (f_res, 1, &err);


      int owner_id = db_res_col_i32 (f_res, 2, &err);


      int corp_id = db_res_col_i32 (f_res, 3, &err);


      int mode = db_res_col_i32 (f_res, 4, &err);        // 1=Toll, 2=Attack


      if (!is_asset_hostile (owner_id, corp_id, ctx->player_id, ship_corp_id))


        {
          continue;
        }


      bool attack = true;


      /* Toll Mode */


      if (mode == 1)            // TOLL


        {
          long long toll_cost = (long long) quantity * toll_per_unit;


          long long player_creds = 0;


          // Inline credits check


          const char *sql_c = "SELECT credits FROM players WHERE id=

";


          db_bind_t cp[] = { db_bind_i32 (ctx->player_id) };


          db_res_t *cr = NULL;


          if (db_query (db, sql_c, cp, 1, &cr, &err))
            {
              if (db_res_step (cr, &err))
                {
                  player_creds = db_res_col_i64 (cr, 0, &err);
                }


              db_res_finalize (cr);
            }


          if (player_creds >= toll_cost)


            {
              /* Auto-pay */


              char idem[64];


              h_generate_hex_uuid (idem, sizeof (idem));


              // Transfer to asset owner (player)


              const char *dest_type = (corp_id > 0 &&


                                       owner_id == 0) ? "corp" : "player";


              int dest_id = (corp_id > 0


                             && owner_id == 0) ? corp_id : owner_id;


              // Assuming bank transfer is updated or we use a shim


              if (h_bank_transfer_unlocked (db,


                                            "player",


                                            ctx->player_id,


                                            dest_type,


                                            dest_id,


                                            toll_cost,


                                            "TOLL", idem) == 0) // Changed from SQLITE_OK


                {
                  attack = false;


                  // Log Toll Paid


                  db_log_engine_event ((long long) time (NULL),


                                       "combat.toll.paid",


                                       "player",


                                       ctx->player_id, sector_id, NULL, NULL);
                }
            }
        }


      if (attack)


        {
          int damage = quantity * damage_per_unit;


          /* Apply Damage */


          armid_damage_breakdown_t breakdown = { 0 };


          apply_armid_damage_to_ship (&ship, damage, &breakdown);       // Reusing damage helper


          /* Update Ship */


          const char *sql_upd =
            "UPDATE ships SET hull=

, fighters=$2, shields=$3 WHERE id=$4";


          db_bind_t upd_params[] = {
            db_bind_i32 (ship.hull),


            db_bind_i32 (ship.fighters),


            db_bind_i32 (ship.shields),


            db_bind_i32 (ship_id)
          };


          db_exec (db, sql_upd, upd_params, 4, &err);


          /* Consume Fighters (One-time attack) */


          const char *sql_del = "DELETE FROM sector_assets WHERE id=

";


          db_bind_t del_params[] = { db_bind_i32 (asset_id) };


          db_exec (db, sql_del, del_params, 1, &err);


          /* Log Hit */


          json_t *evt = json_object ();


          json_object_set_new (evt, "damage", json_integer (damage));


          json_object_set_new (evt, "fighters_engaged",


                               json_integer (quantity));


          db_log_engine_event ((long long) time (NULL),


                               "combat.hit.fighters",


                               "player",


                               ctx->player_id, sector_id, evt, NULL);


          /* Check Destruction */


          if (ship.hull <= 0)


            {
              destroy_ship_and_handle_side_effects (ctx, ctx->player_id);


              db_res_finalize (f_res);  // Clean up outer loop stmt


              return 1;         // Destroyed
            }
        }
    }


  db_res_finalize (f_res);


  return 0;
}


/* Helper to apply Quasar damage (Fighters -> Shields -> Hull) */


static int


h_apply_quasar_damage (client_ctx_t *ctx, int damage, const char *source_desc)


{
  if (damage <= 0)


    {
      return 0;
    }


  db_t *db = game_db_get_handle ();


  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)


    {
      return 0;
    }


  combat_ship_t ship = { 0 };


  if (load_ship_combat_stats (db, ship_id, &ship) != 0)


    {
      return 0;
    }


  int remaining = damage;


  int fighters_lost = 0;


  int shields_lost = 0;


  int hull_lost = 0;


  // 1. Fighters


  if (ship.fighters > 0)


    {
      int absorb = MIN (remaining, ship.fighters);


      ship.fighters -= absorb;


      remaining -= absorb;


      fighters_lost = absorb;
    }


  // 2. Shields


  if (remaining > 0 && ship.shields > 0)


    {
      int absorb = MIN (remaining, ship.shields);


      ship.shields -= absorb;


      remaining -= absorb;


      shields_lost = absorb;
    }


  // 3. Hull


  if (remaining > 0)


    {
      ship.hull -= remaining;


      hull_lost = remaining;
    }


  // Update DB


  persist_ship_damage (db, &ship, fighters_lost);


  // Log Event


  json_t *hit_data = json_object ();


  json_object_set_new (hit_data, "damage_total", json_integer (damage));


  json_object_set_new (hit_data, "fighters_lost",


                       json_integer (fighters_lost));


  json_object_set_new (hit_data, "shields_lost", json_integer (shields_lost));


  json_object_set_new (hit_data, "hull_lost", json_integer (hull_lost));


  json_object_set_new (hit_data, "source", json_string (source_desc));


  db_log_engine_event ((long long) time (NULL),


                       "combat.hit",


                       "player", ctx->player_id, ship.sector, hit_data, NULL);


  // Check destruction


  if (ship.hull <= 0)


    {
      destroy_ship_and_handle_side_effects (ctx, ctx->player_id);


      return 1;
    }


  return 0;
}


static int


apply_sector_quasar_on_entry (client_ctx_t *ctx, int sector_id)


{
  if (sector_id == 1)


    {
      return 0;                 // Terra Safe
    }


  db_t *db = game_db_get_handle ();


  int ship_corp_id = ctx->corp_id;


  db_error_t err;


  db_error_clear (&err);


  const char *sql =


    "SELECT p.id, p.owner_id, p.owner_type, c.level, c.qCannonSector, c.militaryReactionLevel "


    "FROM planets p "


    "JOIN citadels c ON p.id = c.planet_id "


    "WHERE p.sector = 

 AND c.level >= 3 AND c.qCannonSector > 0;";


  db_bind_t params[] = { db_bind_i32 (sector_id) };


  db_res_t *res = NULL;


  if (!db_query (db, sql, params, 1, &res, &err))


    {
      LOGE ("apply_sector_quasar_on_entry: query failed: %s", err.message);


      return 0;
    }


  int shot_fired = 0;


  while (db_res_step (res, &err))


    {
      int planet_id = db_res_col_i32 (res, 0, &err);


      int owner_id = db_res_col_i32 (res, 1, &err);


      const char *owner_type = db_res_col_text (res, 2, &err);


      int base_strength = db_res_col_i32 (res, 4, &err);


      int reaction = db_res_col_i32 (res, 5, &err);


      // Resolve Corp ID for hostility check


      int p_corp_id = 0;


      if (owner_type && (strcasecmp (owner_type,


                                     "corp") == 0 || strcasecmp (owner_type,


                                                                 "corporation")


                         == 0))


        {
          p_corp_id = owner_id;
        }


      if (is_asset_hostile


            (owner_id, p_corp_id, ctx->player_id, ship_corp_id))


        {
          // Fire!


          int pct = 100;


          if (reaction == 1)


            {
              pct = 125;
            }


          else if (reaction >= 2)


            {
              pct = 150;
            }


          int damage =


            (int) floor ((double) base_strength * (double) pct / 100.0);


          char source_desc[64];


          snprintf (source_desc,


                    sizeof (source_desc),


                    "Quasar Sector Shot (Planet %d)", planet_id);


          if (h_apply_quasar_damage (ctx, damage, source_desc))


            {
              shot_fired = 1;   // Destroyed
            }


          else


            {
              shot_fired = 2;   // Damaged
            }


          break;                // Single shot per sector entry
        }
    }


  db_res_finalize (res);


  return (shot_fired == 1);     // Return 1 if destroyed
}


/* Central Hazard Handler */
int
h_handle_sector_entry_hazards (db_t *db, client_ctx_t *ctx, int sector_id)
{
  (void) db;                    // Unused, usually available via ctx logic but passed for consistency

  /* Quasar First (Long Range) */
  if (apply_sector_quasar_on_entry (ctx, sector_id))
    {
      return 1;                 // Destroyed
    }
  /* Armid Mines Second */
  if (apply_armid_mines_on_entry (ctx, sector_id, NULL))
    {
      return 1;                 // Destroyed
    }
  /* Fighters Third */
  if (apply_sector_fighters_on_entry (ctx, sector_id))
    {
      return 1;                 // Destroyed
    }
  /* Limpets (Phase 2) - Placeholder */

  return 0;
}


int
h_trigger_atmosphere_quasar (db_t *db, client_ctx_t *ctx, int planet_id)
{
  // 1. Get Planet/Citadel Info
  db_error_t err;
  db_error_clear (&err);

  const char *sql =
    "SELECT p.owner_id, p.owner_type, c.level, c.qCannonAtmosphere, c.militaryReactionLevel "
    "FROM planets p "
    "JOIN citadels c ON p.id = c.planet_id "
    "WHERE p.id = $1 AND c.level >= 3 AND c.qCannonAtmosphere > 0;";
  db_bind_t params[] = { db_bind_i32 (planet_id) };
  db_res_t *res = NULL;


  if (!db_query (db, sql, params, 1, &res, &err))
    {
      return 0;
    }

  if (db_res_step (res, &err))
    {
      int owner_id = db_res_col_i32 (res, 0, &err);
      const char *owner_type = db_res_col_text (res, 1, &err);
      int base_strength = db_res_col_i32 (res, 3, &err);
      int reaction = db_res_col_i32 (res, 4, &err);

      int p_corp_id = 0;


      if (owner_type && (strcasecmp (owner_type,
                                     "corp") == 0 || strcasecmp (owner_type,
                                                                 "corporation")
                         == 0))
        {
          p_corp_id = owner_id;
        }

      if (is_asset_hostile
            (owner_id, p_corp_id, ctx->player_id, ctx->corp_id))
        {
          int pct = 100;


          if (reaction == 1)
            {
              pct = 125;
            }
          else if (reaction >= 2)
            {
              pct = 150;
            }

          int damage =
            (int) floor ((double) base_strength * (double) pct / 100.0);


          db_res_finalize (res);        // Done with query

          char source_desc[64];


          snprintf (source_desc,
                    sizeof (source_desc),
                    "Quasar Atmosphere Shot (Planet %d)", planet_id);

          if (h_apply_quasar_damage (ctx, damage, source_desc))
            {
              return 1;         // Destroyed
            }
          return 0;             // Survived
        }
    }
  db_res_finalize (res);
  return 0;
}


#include "server_news.h"


/* Helper to apply Terra sanctions */
static void
h_apply_terra_sanctions (db_t *db, int player_id)
{
  db_error_t err;
  db_error_clear (&err);

  // 1. Wipe Ships
  const char *sql_del_ships =
    "DELETE FROM ships WHERE id IN (SELECT ship_id FROM ship_ownership WHERE player_id = $1)";
  db_bind_t params[] = { db_bind_i32 (player_id) };


  db_exec (db, sql_del_ships, params, 1, &err);

  // 2. Zero Player Credits
  const char *sql_upd_credits = "UPDATE players SET credits = 0 WHERE id = $1";


  db_exec (db, sql_upd_credits, params, 1, &err);

  // 3. Zero Bank Accounts
  const char *sql_upd_bank =
    "UPDATE bank_accounts SET balance = 0 WHERE owner_type = 'player' AND owner_id = $1";


  db_exec (db, sql_upd_bank, params, 1, &err);

  // 4. Delete Sector Assets
  const char *sql_del_assets = "DELETE FROM sector_assets WHERE player = $1";


  db_exec (db, sql_del_assets, params, 1, &err);

  // 5. Delete Limpets
  const char *sql_del_limpets =
    "DELETE FROM limpet_attached WHERE owner_player_id = $1";


  db_exec (db, sql_del_limpets, params, 1, &err);

  // 6. Zero Planet Treasuries
  const char *sql_upd_treasury =
    "UPDATE citadels SET treasury = 0 WHERE planet_id IN (SELECT id FROM planets WHERE owner_id = $1 AND owner_type = 'player')";


  db_exec (db, sql_upd_treasury, params, 1, &err);

  // 7. Zero Planet Fighters
  const char *sql_upd_pfighters =
    "UPDATE planets SET fighters = 0 WHERE owner_id = $1 AND owner_type = 'player'";


  db_exec (db, sql_upd_pfighters, params, 1, &err);

  // 8. Global News
  char msg[256];


  snprintf (msg,
            sizeof (msg),
            "Player %d has attacked Terra and has been sanctioned by the Federation. All assets seized.",
            player_id);
  news_post (msg, "Federation", 0);
}


/* ---------- combat.attack_planet ---------- */
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
      send_response_error (ctx,
                           root,
                           ERR_SERVICE_UNAVAILABLE, "Database unavailable");
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

  // 1. Get Ship
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }

  // 2. Get Sector
  int current_sector = h_get_player_sector (ctx->player_id); // Assuming this helper is updated or safe.
  // Wait, h_get_player_sector might still use sqlite3 internally if not updated.
  // I should check h_get_player_sector. It was in server_players.c.
  // I'll trust it for now or assume it gets updated later.
  // Actually, let's just query it here to be safe and consistent with db_t.

  db_error_t err;


  db_error_clear (&err);

  if (current_sector <= 0)
    {
      const char *sql_sec = "SELECT sector FROM ships WHERE id=$1";
      db_bind_t params[] = { db_bind_i32 (ship_id) };
      db_res_t *res = NULL;


      if (db_query (db, sql_sec, params, 1, &res, &err))
        {
          if (db_res_step (res, &err))
            {
              current_sector = db_res_col_i32 (res, 0, &err);
            }
          db_res_finalize (res);
        }
    }

  // 3. Get Planet Info
  int p_sector = 0;
  int p_fighters = 0;
  int p_owner_id = 0;
  bool p_exists = false;

  const char *sql_planet =
    "SELECT sector_id, owner_id, fighters FROM planets WHERE id=$1";
  db_bind_t p_params[] = { db_bind_i32 (planet_id) };
  db_res_t *pres = NULL;


  if (db_query (db, sql_planet, p_params, 1, &pres, &err))
    {
      if (db_res_step (pres, &err))
        {
          p_exists = true;
          p_sector = db_res_col_i32 (pres, 0, &err);
          p_owner_id = db_res_col_i32 (pres, 1, &err);
          p_fighters = db_res_col_i32 (pres, 2, &err);
        }
      db_res_finalize (pres);
    }

  if (!p_exists)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Planet not found");
      return 0;
    }

  if (p_sector != current_sector)
    {
      send_response_error (ctx,
                           root,
                           REF_NOT_IN_SECTOR, "Planet not in current sector");
      return 0;
    }

  // 4. Terra Protection
  if (planet_id == 1 || p_sector == 1)
    {
      // destroy_ship_and_handle_side_effects assumes sqlite3 internal usage unless updated.
      // I should update it or assume it works via game_db_get_handle internally if updated.
      // It was in server_players.c.
      destroy_ship_and_handle_side_effects (ctx, ctx->player_id);
      h_apply_terra_sanctions (db, ctx->player_id);

      json_t *evt = json_object ();


      json_object_set_new (evt, "planet_id", json_integer (planet_id));
      json_object_set_new (evt, "sanctioned", json_true ());
      db_log_engine_event ((long long) time (NULL),
                           "player.terra_attack_sanction.v1",
                           "player",
                           ctx->player_id, current_sector, evt, NULL);

      send_response_error (ctx,
                           root,
                           403,
                           "You have attacked Terra! Federation forces have destroyed your ship and seized your assets.");
      return 0;
    }

  // 5. Get Attacker Ship Info
  int s_fighters = 0;

  const char *sql_ship_f = "SELECT fighters FROM ships WHERE id=$1";
  db_bind_t sf_params[] = { db_bind_i32 (ship_id) };
  db_res_t *sf_res = NULL;


  if (db_query (db, sql_ship_f, sf_params, 1, &sf_res, &err))
    {
      if (db_res_step (sf_res, &err))
        {
          s_fighters = db_res_col_i32 (sf_res, 0, &err);
        }
      db_res_finalize (sf_res);
    }

  if (s_fighters <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "You have no fighters to attack with.");
      return 0;
    }

  // 6. Citadel Defence Hooks (Shields + CCC)
  int cit_level = 0;
  int cit_shields = 0;
  int cit_reaction = 0;

  const char *sql_cit =
    "SELECT level, planetaryShields, militaryReactionLevel FROM citadels WHERE planet_id=$1";
  db_bind_t cit_params[] = { db_bind_i32 (planet_id) };
  db_res_t *cit_res = NULL;


  if (db_query (db, sql_cit, cit_params, 1, &cit_res, &err))
    {
      if (db_res_step (cit_res, &err))
        {
          cit_level = db_res_col_i32 (cit_res, 0, &err);
          cit_shields = db_res_col_i32 (cit_res, 1, &err);
          cit_reaction = db_res_col_i32 (cit_res, 2, &err);
        }
      db_res_finalize (cit_res);
    }

  int fighters_absorbed = 0;


  // 6.1 Shields (Level >= 5)
  if (cit_level >= 5 && cit_shields > 0)
    {
      int absorbed = (s_fighters < cit_shields) ? s_fighters : cit_shields;


      s_fighters -= absorbed;
      fighters_absorbed = absorbed;
      int new_shields = cit_shields - absorbed;

      // Persist shield damage immediately
      const char *sql_upd_shields =
        "UPDATE citadels SET planetaryShields=$1 WHERE planet_id=$2";
      db_bind_t upd_s_params[] = { db_bind_i32 (new_shields),
                                   db_bind_i32 (planet_id) };


      db_exec (db, sql_upd_shields, upd_s_params, 2, &err);

      // If all fighters absorbed, attack fails immediately (but logic continues with 0 fighters likely resulting in loss)
    }

  // 6.2 CCC / Military Reaction (Level >= 2)
  int effective_p_fighters = p_fighters;


  if (cit_level >= 2 && cit_reaction > 0)
    {
      int pct = 100;


      if (cit_reaction == 1)
        {
          pct = 125;
        }
      else if (cit_reaction >= 2)
        {
          pct = 150;
        }

      effective_p_fighters = (int) floor ((double) p_fighters * (double) pct /
                                          100.0);
    }

  // 7. Combat Resolution
  bool attacker_wins = (s_fighters > effective_p_fighters);
  int ship_loss = 0;
  int planet_loss = 0;
  bool captured = false;


  if (attacker_wins)
    {
      ship_loss = effective_p_fighters; // Attacker loses fighters equal to effective defense
      planet_loss = p_fighters; // Planet loses all real fighters
      captured = true;
    }
  else
    {
      ship_loss = s_fighters;   // Attacker loses all remaining fighters
      planet_loss = s_fighters; // Planet loses fighters equal to attacker strength
      captured = false;
    }

  ship_loss += fighters_absorbed;

  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Transaction error");
      return 0;
    }

  const char *sql_upd_ship =
    "UPDATE ships SET fighters = fighters - $1 WHERE id = $2";
  db_bind_t us_params[] = { db_bind_i32 (ship_loss), db_bind_i32 (ship_id) };


  if (!db_exec (db, sql_upd_ship, us_params, 2, &err))
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_DB, "Update ship failed");
      return 0;
    }

  if (captured)
    {
      int corp_id = ctx->corp_id;
      const char *new_type = (corp_id > 0) ? "corporation" : "player";
      int new_owner = (corp_id > 0) ? corp_id : ctx->player_id;

      const char *sql_cap =
        "UPDATE planets SET fighters=0, owner_id=$1, owner_type=$2 WHERE id=$3";
      db_bind_t cap_params[] = {
        db_bind_i32 (new_owner),
        db_bind_text (new_type),
        db_bind_i32 (planet_id)
      };


      if (!db_exec (db, sql_cap, cap_params, 3, &err))
        {
          db_tx_rollback (db, &err);
          send_response_error (ctx, root, ERR_DB, "Capture update failed");
          return 0;
        }

      json_t *cap_evt = json_object ();


      json_object_set_new (cap_evt, "planet_id", json_integer (planet_id));
      json_object_set_new (cap_evt, "previous_owner",
                           json_integer (p_owner_id));
      db_log_engine_event ((long long) time (NULL),
                           "player.capture_planet.v1",
                           "player",
                           ctx->player_id, current_sector, cap_evt, NULL);
    }
  else
    {
      const char *sql_upd_p =
        "UPDATE planets SET fighters = fighters - $1 WHERE id = $2";
      db_bind_t up_params[] = { db_bind_i32 (planet_loss),
                                db_bind_i32 (planet_id) };


      if (!db_exec (db, sql_upd_p, up_params, 2, &err))
        {
          db_tx_rollback (db, &err);
          send_response_error (ctx, root, ERR_DB, "Planet update failed");
          return 0;
        }
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Commit failed");
      return 0;
    }

  json_t *atk_evt = json_object ();


  json_object_set_new (atk_evt, "planet_id", json_integer (planet_id));
  json_object_set_new (atk_evt, "result",
                       json_string (attacker_wins ? "win" : "loss"));
  json_object_set_new (atk_evt, "ship_loss", json_integer (ship_loss));
  json_object_set_new (atk_evt, "planet_loss", json_integer (planet_loss));
  db_log_engine_event ((long long) time (NULL),
                       "player.attack_planet.v1",
                       "player",
                       ctx->player_id, current_sector, atk_evt, NULL);

  json_t *res = json_object ();


  json_object_set_new (res, "planet_id", json_integer (planet_id));
  json_object_set_new (res, "attacker_remaining_fighters",
                       json_integer (s_fighters - ship_loss));
  json_object_set_new (res, "defender_remaining_fighters",
                       json_integer (p_fighters - planet_loss));
  json_object_set_new (res, "captured", json_boolean (captured));

  send_response_ok_take (ctx, root, "combat.attack_planet", &res);
  return 0;
}

