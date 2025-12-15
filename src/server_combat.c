#include <jansson.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <jansson.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>               // Added for pow() and ceil()
// local includes
#include "server_cron.h"
#include "server_log.h"
#include "common.h"
#include "server_players.h"
#include "server_universe.h"
#include "server_combat.h"
#include "server_envelope.h"
#include "server_log.h"
#include "errors.h"
#include "server_config.h"
#include "server_planets.h"

/* Global Combat Constants (Physics) */
/* TODO: Move to config table */
static const double OFFENSE_SCALE = 0.05;
static const double DEFENSE_SCALE = 0.05;
static const int DAMAGE_PER_FIGHTER = 1;


typedef struct {
    int id;
    int player_id;
    int corp_id;
    int hull;
    int shields;
    int fighters;
    int attack_power; // shiptypes.offense
    int defense_power; // shiptypes.defense
    int max_attack;   // shiptypes.maxattack
    char name[64];
    int sector; // Added for combat checks
} combat_ship_t;




/* Forward decls from your codebase */
json_t *db_get_stardock_sectors (void);
int handle_ship_attack(client_ctx_t *ctx, json_t *root, json_t *data, sqlite3 *db);
static int ship_consume_mines (sqlite3 *db, int ship_id, int asset_type,
                               int amount);
static int insert_sector_mines (sqlite3 *db, int sector_id,
                                int owner_player_id, json_t *corp_id_json,
                                int asset_type, int offense_mode, int amount);
static int sum_sector_fighters (sqlite3 *db, int sector_id, int *total_out);
static int ship_consume_fighters (sqlite3 *db, int ship_id, int amount);
static int insert_sector_fighters (sqlite3 *db, int sector_id,
                                   int owner_player_id, json_t *corp_id_json,
                                   int offense_mode, int amount);



/* --- common helpers --- */
static inline int
require_auth (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id > 0)
    {
      return 1;
    }
    send_response_refused(ctx, root, ERR_SECTOR_NOT_FOUND, "Not authenticated", NULL);
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
is_msl_sector (sqlite3 *db, int sector_id)
{
  sqlite3_stmt *st = NULL;
  const char *sql = "SELECT 1 FROM msl_sectors WHERE sector_id = ?1 LIMIT 1;";
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("Failed to prepare MSL check statement: %s", sqlite3_errmsg (db));
      return false;
    }
  sqlite3_bind_int (st, 1, sector_id);
  bool is_msl = (sqlite3_step (st) == SQLITE_ROW);


  sqlite3_finalize (st);
  return is_msl;
}


int
cmd_combat_attack (client_ctx_t *ctx, json_t *root)
{
  int rc; // Declare rc
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      send_response_error(ctx, root, ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_response_error(ctx, root, ERR_MISSING_FIELD, "Missing required field: data");
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
  sqlite3 *db = db_get_handle ();       // Get DB handle from context
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
  sqlite3_stmt *ship_st = NULL;
  const char *sql_select_ship =
    "SELECT id, hull, fighters, shields FROM ships WHERE id = ?1;";


  if (sqlite3_prepare_v2 (db, sql_select_ship, -1, &ship_st, NULL) !=
      SQLITE_OK)
    {
      LOGE ("Failed to prepare ship selection statement: %s",
            sqlite3_errmsg (db));
      return -1;                // Indicate error
    }
  sqlite3_bind_int (ship_st, 1, ship_id);
  if (sqlite3_step (ship_st) == SQLITE_ROW)
    {
      ship_stats.id = sqlite3_column_int (ship_st, 0);
      ship_stats.hull = sqlite3_column_int (ship_st, 1);
      ship_stats.fighters = sqlite3_column_int (ship_st, 2);
      ship_stats.shields = sqlite3_column_int (ship_st, 3);
    }
  else
    {
      LOGW ("Ship %d not found for mine encounter.", ship_id);
      sqlite3_finalize (ship_st);
      return 0;                 // No ship, no encounter
    }
  sqlite3_finalize (ship_st);
  
  // Config for damage
  int damage_per_mine = 10;
  db_get_int_config(db, "armid_damage_per_mine", &damage_per_mine);

  // --- Process Armid Mines (Asset Type 1) ---
  if (g_armid_config.armid.enabled)
    {
      sqlite3_stmt *st = NULL;
      const char *sql_select_armid_mines =
        "SELECT id, quantity, offensive_setting, player, corporation, ttl "
        "FROM sector_assets " "WHERE sector = ?1 AND asset_type = 1;";                                                                                                          // asset_type 1 for Armid Mines


      if (sqlite3_prepare_v2 (db, sql_select_armid_mines, -1, &st, NULL) !=
          SQLITE_OK)
        {
          LOGE ("Failed to prepare Armid mine selection statement: %s",
                sqlite3_errmsg (db));
          return -1;            // Indicate error
        }
      sqlite3_bind_int (st, 1, new_sector_id);
      while (sqlite3_step (st) == SQLITE_ROW)
        {
          int mine_id = sqlite3_column_int (st, 0);
          int mine_quantity = sqlite3_column_int (st, 1);
          sector_asset_t mine_asset_row = {
            .id = mine_id,
            .quantity = mine_quantity,
            .player = sqlite3_column_int (st, 3),
            .corporation = sqlite3_column_int (st, 4),
            .ttl = sqlite3_column_int (st, 5)
          };
          
          // Check Hostility using new helper
          if (!is_asset_hostile(mine_asset_row.player, mine_asset_row.corporation, ship_player_id, ship_corp_id))
            {
              continue; // Friendly
            }

          // Canonical Rule: All hostile mines detonate
          int exploded = mine_quantity;
          int damage = exploded * damage_per_mine;
          armid_damage_breakdown_t d = { 0 };

          apply_armid_damage_to_ship (&ship_stats, damage, &d);
          
          // Remove Mines (Consumed)
          sqlite3_stmt *delete_st = NULL;
          const char *sql_delete_mine = "DELETE FROM sector_assets WHERE id = ?1;";
          if (sqlite3_prepare_v2 (db, sql_delete_mine, -1, &delete_st, NULL) == SQLITE_OK)
            {
              sqlite3_bind_int (delete_st, 1, mine_id);
              sqlite3_step (delete_st);
              sqlite3_finalize (delete_st);
            }

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
          json_object_set_new (hit_data, "attacker_id", json_integer (mine_asset_row.player));
          json_object_set_new (hit_data, "defender_id", json_integer (ctx->player_id));
          json_object_set_new (hit_data, "weapon", json_string ("armid_mines"));
          json_object_set_new (hit_data, "damage_total", json_integer (damage));
          json_object_set_new (hit_data, "mines_exploded", json_integer (exploded));
          (void) db_log_engine_event ((long long) time (NULL), "combat.hit", "player", ctx->player_id, new_sector_id, hit_data, NULL);

          // If ship is destroyed, handle it
          if (ship_stats.hull <= 0)
            {
              // Persist damage first? No, destroy_ship handles it? 
              // We should update the ship stats in DB before destroying so the final state is captured?
              // destroy_ship usually reads from DB. 
              // But wait, we haven't updated DB with damage yet!
              // We must update ship DB before calling destroy or breaking.
              
              // Actually, we should update DB after *every* stack? Or accumulate and update once?
              // For safety (crash resilience), update now.
              // But to be clean, let's update at the end of loop or immediately.
              // Let's update immediately.
              sqlite3_stmt *upd_ship;
              if (sqlite3_prepare_v2(db, "UPDATE ships SET hull=?1, fighters=?2, shields=?3 WHERE id=?4", -1, &upd_ship, NULL) == SQLITE_OK) {
                  sqlite3_bind_int(upd_ship, 1, ship_stats.hull);
                  sqlite3_bind_int(upd_ship, 2, ship_stats.fighters);
                  sqlite3_bind_int(upd_ship, 3, ship_stats.shields);
                  sqlite3_bind_int(upd_ship, 4, ship_id);
                  sqlite3_step(upd_ship);
                  sqlite3_finalize(upd_ship);
              }

              destroy_ship_and_handle_side_effects (ctx, ctx->player_id);
              if (out_enc)
                {
                  out_enc->destroyed = true;
                }
              LOGI ("Ship %d destroyed by Armid mine %d in sector %d", ship_id, mine_id, new_sector_id);
              break; // Stop processing further stacks
            }
            
            // Update ship DB if not destroyed (so next stack sees correct HP)
             sqlite3_stmt *upd_ship;
              if (sqlite3_prepare_v2(db, "UPDATE ships SET hull=?1, fighters=?2, shields=?3 WHERE id=?4", -1, &upd_ship, NULL) == SQLITE_OK) {
                  sqlite3_bind_int(upd_ship, 1, ship_stats.hull);
                  sqlite3_bind_int(upd_ship, 2, ship_stats.fighters);
                  sqlite3_bind_int(upd_ship, 3, ship_stats.shields);
                  sqlite3_bind_int(upd_ship, 4, ship_id);
                  sqlite3_step(upd_ship);
                  sqlite3_finalize(upd_ship);
              }
        }
      sqlite3_finalize (st);
    }                           // End Armid mine processing
  
  return 0;                     // Success
}

int
apply_limpet_mines_on_entry (client_ctx_t *ctx, int new_sector_id,
                            armid_encounter_t *out_enc)
{
  (void) out_enc;
  sqlite3 *db = db_get_handle ();
  if (!db) return -1;

  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0) return 0;

  int ship_player_id = ctx->player_id;
  int ship_corp_id = ctx->corp_id;

  if (!g_cfg.mines.limpet.enabled)
    {
      return 0; 
    }

  sqlite3_stmt *st = NULL;
  const char *sql_select_limpets =
    "SELECT id, quantity, player, corporation, ttl "
    "FROM sector_assets "
    "WHERE sector = ?1 AND asset_type = 4 AND quantity > 0;";

  if (sqlite3_prepare_v2 (db, sql_select_limpets, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("Failed to prepare Limpet mine selection: %s", sqlite3_errmsg (db));
      return -1;
    }

  sqlite3_bind_int (st, 1, new_sector_id);

  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int asset_id = sqlite3_column_int (st, 0);
      int quantity = sqlite3_column_int (st, 1);
      int owner_id = sqlite3_column_int (st, 2);
      int corp_id = sqlite3_column_int (st, 3);
      time_t ttl = sqlite3_column_int64 (st, 4);

      sector_asset_t asset = { .quantity = quantity, .ttl = ttl };
      
      if (!is_asset_hostile(owner_id, corp_id, ship_player_id, ship_corp_id))
        {
          continue;
        }

      if (!armid_stack_is_active(&asset, time(NULL)))
        {
          continue;
        }

      sqlite3_stmt *check_st = NULL;
      const char *sql_check_attached = 
        "SELECT 1 FROM limpet_attached WHERE ship_id=?1 AND owner_player_id=?2;";
      if (sqlite3_prepare_v2(db, sql_check_attached, -1, &check_st, NULL) != SQLITE_OK)
        {
           LOGE("Failed prepare limpet check: %s", sqlite3_errmsg(db));
           continue;
        }
      sqlite3_bind_int(check_st, 1, ship_id);
      sqlite3_bind_int(check_st, 2, owner_id);
      int already_attached = (sqlite3_step(check_st) == SQLITE_ROW);
      sqlite3_finalize(check_st);

      if (already_attached)
        {
          continue;
        }

      sqlite3_stmt *upd_st = NULL;
      if (quantity > 1)
        {
           if (sqlite3_prepare_v2(db, "UPDATE sector_assets SET quantity=quantity-1 WHERE id=?1", -1, &upd_st, NULL) == SQLITE_OK)
           {
             sqlite3_bind_int(upd_st, 1, asset_id);
             sqlite3_step(upd_st);
             sqlite3_finalize(upd_st);
           }
        }
      else
        {
           if (sqlite3_prepare_v2(db, "DELETE FROM sector_assets WHERE id=?1", -1, &upd_st, NULL) == SQLITE_OK)
           {
             sqlite3_bind_int(upd_st, 1, asset_id);
             sqlite3_step(upd_st);
             sqlite3_finalize(upd_st);
           }
        }

      sqlite3_stmt *ins_st = NULL;
      const char *sql_attach = "INSERT OR REPLACE INTO limpet_attached (ship_id, owner_player_id, created_ts) VALUES (?1, ?2, strftime('%s','now'));";
      if (sqlite3_prepare_v2(db, sql_attach, -1, &ins_st, NULL) == SQLITE_OK)
        {
          sqlite3_bind_int(ins_st, 1, ship_id);
          sqlite3_bind_int(ins_st, 2, owner_id);
          sqlite3_step(ins_st);
          sqlite3_finalize(ins_st);
        }

      json_t *event_data = json_object();
      json_object_set_new(event_data, "target_ship_id", json_integer(ship_id));
      json_object_set_new(event_data, "target_player_id", json_integer(ship_player_id));
      json_object_set_new(event_data, "sector_id", json_integer(new_sector_id));
      
      db_log_engine_event(
        (long long)time(NULL),
        "combat.limpet.attached",
        "player", 
        owner_id,
        new_sector_id,
        event_data,
        NULL
      );
      
      LOGI("Limpet attached! Ship %d tagged by Player %d in Sector %d", ship_id, owner_id, new_sector_id);
    }
  sqlite3_finalize(st);

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
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error(ctx, root, ERR_SERVER_ERROR, "Database handle not available.");
      return 0;
    }
  int self_player_id = ctx->player_id;
  // --- 2. Prepare SQL Statement ---
  sqlite3_stmt *st = NULL;


  db_mutex_lock ();
  int rc = sqlite3_prepare_v2 (db, sql_query, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      db_mutex_unlock ();
      send_response_error(ctx, root, ERR_SERVER_ERROR, sqlite3_errmsg (db));
      return 0;
    }
  // --- 3. Bind Parameters ---
  sqlite3_bind_int (st, 1, self_player_id);
  sqlite3_bind_int (st, 2, self_player_id);
  // --- 4. Execute Query and Build Entries Array ---
  int total_count = 0;
  json_t *entries = json_array ();      // Create the array for the entries


  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      total_count++;
      // Extract all columns from the row
      int asset_id = sqlite3_column_int (st, 0);        // NEW
      int sector_id = sqlite3_column_int (st, 1);
      int count = sqlite3_column_int (st, 2);
      int offense_mode = sqlite3_column_int (st, 3);
      int player_id = sqlite3_column_int (st, 4);
      const char *player_name = (const char *) sqlite3_column_text (st, 5);
      int corp_id = sqlite3_column_int (st, 6);
      const char *corp_tag = (const char *) sqlite3_column_text (st, 7);
      int asset_type = sqlite3_column_int (st, 8);
      // Pack them into a JSON object
      json_t *entry =
        json_pack ("{s:i, s:i, s:i, s:i, s:i, s:s, s:i, s:s, s:i}",
                   // Added one more s:i for asset_id
                   "asset_id",
                   asset_id,
                   // NEW
                   "sector_id",
                   sector_id,
                   "count",
                   count,
                   "offense_mode",
                   offense_mode,
                   "player_id",
                   player_id,
                   "player_name",
                   player_name ? player_name : "Unknown",
                   "corp_id",
                   corp_id,
                   "corp_tag",
                   corp_tag ? corp_tag : "",
                   "type",
                   asset_type);


      if (entry)
        {
          json_array_append_new (entries, entry);
        }
    }
  // --- 5. Finalize Statement and Unlock Mutex ---
  sqlite3_finalize (st);
  db_mutex_unlock ();
  if (rc != SQLITE_DONE)
    {
      json_decref (entries);    // Clean up on error
      send_response_error(ctx, root, ERR_SERVER_ERROR, "Error processing asset list.");
      return 0;
    }
  // --- 6. Build Final Payload and Send Response ---
  json_t *jdata_payload = json_object ();


  json_object_set_new (jdata_payload, "total", json_integer (total_count));
  json_object_set_new (jdata_payload, "entries", entries);      // Add the array
  send_response_ok(ctx, root, list_type, jdata_payload);
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
  sqlite3 *db = db_get_handle ();
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0) {
      send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
  }

  h_decloak_ship (db, ship_id);
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);
  if (tc != TURN_CONSUME_SUCCESS) {
      return handle_turn_consumption_error (ctx, tc, "combat.flee", root, NULL);
  }

  // Load Engine/Mass (MVP: Engine=10 fixed, Mass=maxholds)
  int mass = 100;
  int engine = 10;
  int sector_id = 0;

  sqlite3_stmt *st;
  if (sqlite3_prepare_v2(db, "SELECT t.maxholds, s.sector FROM ships s JOIN shiptypes t ON s.type_id=t.id WHERE s.id=?1", -1, &st, NULL) == SQLITE_OK) {
      sqlite3_bind_int(st, 1, ship_id);
      if (sqlite3_step(st) == SQLITE_ROW) {
          mass = sqlite3_column_int(st, 0);
          sector_id = sqlite3_column_int(st, 1);
      }
      sqlite3_finalize(st);
  }

  // Deterministic check: (Engine * 10) / (Mass + 1) > 0.5
  double score = ((double)engine * 10.0) / ((double)mass + 1.0);
  bool success = (score > 0.5); 

  if (success) {
      // Pick first adjacent sector
      int dest = 0;
      if (sqlite3_prepare_v2(db, "SELECT to_sector FROM sector_warps WHERE from_sector=?1 ORDER BY to_sector ASC LIMIT 1", -1, &st, NULL) == SQLITE_OK) {
          sqlite3_bind_int(st, 1, sector_id);
          if (sqlite3_step(st) == SQLITE_ROW) {
              dest = sqlite3_column_int(st, 0);
          }
          sqlite3_finalize(st);
      }

      if (dest > 0) {
          char *err = NULL;
          sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
          sqlite3_stmt *ust = NULL;
          // Update Ship
          sqlite3_prepare_v2(db, "UPDATE ships SET sector=?1 WHERE id=?2", -1, &ust, NULL);
          sqlite3_bind_int(ust, 1, dest);
          sqlite3_bind_int(ust, 2, ship_id);
          sqlite3_step(ust);
          sqlite3_finalize(ust);
          
          // Update Player
          sqlite3_prepare_v2(db, "UPDATE players SET sector=?1 WHERE id=?2", -1, &ust, NULL);
          sqlite3_bind_int(ust, 1, dest);
          sqlite3_bind_int(ust, 2, ctx->player_id);
          sqlite3_step(ust);
          sqlite3_finalize(ust);
          
          sqlite3_exec(db, "COMMIT", NULL, NULL, &err);
          
          // Hazards
          h_handle_sector_entry_hazards(db, ctx, dest);
          
          json_t *res = json_object();
          json_object_set_new(res, "success", json_true());
          json_object_set_new(res, "to_sector", json_integer(dest));
          send_response_ok(ctx, root, "combat.flee", res);
          return 0;
      }
  }

  // Failure
  json_t *res = json_object();
  json_object_set_new(res, "success", json_false());
  send_response_ok(ctx, root, "combat.flee", res);
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
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error(ctx, root, ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  bool in_fed = false;
  bool in_sdock = false;
  /* Parse input */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error(ctx, root, ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_amount = json_object_get (data, "amount");
  json_t *j_offense = json_object_get (data, "offense");        /* required: 1..3 */
  json_t *j_corp_id = json_object_get (data, "corporation_id"); /* optional, nullable */


  if (!j_amount || !json_is_integer (j_amount) ||
      !j_offense || !json_is_integer (j_offense))
    {
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "Missing required field or invalid type: amount/offense");
      return 0;
    }
  int amount = (int) json_integer_value (j_amount);
  int offense = (int) json_integer_value (j_offense);


  if (amount <= 0)
    {
      send_response_error(ctx, root, ERR_NOT_FOUND, "amount must be > 0");
      return 0;
    }
  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK)
    {
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "offense must be one of {1=TOLL,2=DEFEND,3=ATTACK}");
      return 0;
    }
  /* Resolve active ship + sector */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }
  /* Decloak: visible hostile/defensive action */
  (void) h_decloak_ship (db, ship_id);
  int sector_id = -1;
  {
    sqlite3_stmt *st = NULL;


    if (sqlite3_prepare_v2
          (db, "SELECT sector FROM ships WHERE id=?1;", -1, &st,
          NULL) != SQLITE_OK)
      {
        char error_buffer[256];


        snprintf (error_buffer,
                  sizeof (error_buffer),
                  "Unable to resolve current sector - SELECT sector FROM ships WHERE id=%d;",
                  ship_id);
        char *shperror = error_buffer;


        send_response_error(ctx, root, ERR_SECTOR_NOT_FOUND, shperror);
        return 0;
      }
    sqlite3_bind_int (st, 1, ship_id);
    if (sqlite3_step (st) == SQLITE_ROW)
      {
        sector_id = sqlite3_column_int (st, 0);
      }
    sqlite3_finalize (st);
    if (sector_id <= 0)
      {
        char error_buffer[256];


        snprintf (error_buffer,
                  sizeof (error_buffer),
                  "Unable to resolve current sector - SELECT sector_id FROM ships WHERE id=%d;",
                  ship_id);
        char *scterror = error_buffer;


        send_response_error(ctx, root, ERR_SECTOR_NOT_FOUND, scterror);
        return 0;
      }
  }
  /* Transaction: debit ship, credit sector */
  char *errmsg = NULL;


  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Could not start transaction");
      return 0;
    }

  /* Sector cap check (moved inside transaction) */
  int sector_total = 0;


  if (sum_sector_fighters (db, sector_id, &sector_total) != SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, REF_NOT_IN_SECTOR, "Failed to read sector fighters");
      return 0;
    }
  if (sector_total + amount > SECTOR_FIGHTER_CAP)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, ERR_SECTOR_OVERCROWDED, "Sector fighter limit exceeded (50,000)");
      return 0;
    }

  int rc = ship_consume_fighters (db, ship_id, amount);


  if (rc == SQLITE_TOOBIG)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, REF_AMMO_DEPLETED, "Insufficient fighters on ship");
      return 0;
    }
  if (rc != SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, REF_AMMO_DEPLETED, "Failed to update ship fighters");
      return 0;
    }
  rc =
    insert_sector_fighters (db, sector_id, ctx->player_id, j_corp_id, offense,
                            amount);
  if (rc != SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, SECTOR_ERR, "Failed to create sector assets record");
      return 0;
    }
  int asset_id = (int) sqlite3_last_insert_rowid (db);  // Capture the newly created asset_id


  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Commit failed");
      return 0;
    }
  /* Fedspace/Stardock → summon ISS + warn player */
  // Check for Federation sectors (1-10)
  if (sector_id >= 1 && sector_id <= 10)
    {
      in_fed = true;
    }
  // Get the list of stardock sectors from the database
  // The function returns a new reference which MUST be freed.
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
      // h_send_message_to_player (int player_id, int sender_id, const char *subject,
      // const char *message)
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
  send_response_ok(ctx, root, "combat.fighters.deployed", out);
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
sum_sector_fighters (sqlite3 *db, int sector_id, int *total_out)
{
  *total_out = 0;
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL_SECTOR_FTR_SUM, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, sector_id);
  if ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      *total_out = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      *total_out = 0;
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


/* Debit ship fighters safely (returns SQLITE_TOOBIG if insufficient). */


/* Debit ship fighters safely (returns SQLITE_TOOBIG if insufficient). */
static int
ship_consume_fighters (sqlite3 *db, int ship_id, int amount)
{
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL_SHIP_GET_FTR, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGI ("ship_consume_fighters - SQL_SHIP_GET_FTR");
      return rc;
    }
  /* --- FIX: Bind the ship_id to the ?1 placeholder --- */
  sqlite3_bind_int (st, 1, ship_id);
  int have = 0;


  if ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      have = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else
    {
      /* This now correctly logs a failure if the ship_id was invalid */
      LOGI ("DEBUGGING:\nhave=%d ship_id=%d amount=%d \n%s", have, ship_id,
            amount, SQL_SHIP_GET_FTR);
      rc = SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  if (have < amount)
    {
      LOGI ("DEBUGGING SQLITE_TOOBIG have < amount");
      return SQLITE_TOOBIG;
    }
  rc = sqlite3_prepare_v2 (db, SQL_SHIP_DEC_FTR, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGI ("%s", SQL_SHIP_DEC_FTR);
      return rc;
    }
  sqlite3_bind_int (st, 1, amount);
  sqlite3_bind_int (st, 2, ship_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}


/* Sum mines already in the sector. */
static int
sum_sector_mines (sqlite3 *db, int sector_id, int *total_out)
{
  *total_out = 0;
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL_SECTOR_MINE_SUM, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, sector_id);
  if ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      *total_out = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      *total_out = 0;
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


/* Debit ship mines safely (returns SQLITE_TOOBIG if insufficient). */
static int
ship_consume_mines (sqlite3 *db, int ship_id, int asset_type, int amount)
{
  sqlite3_stmt *st = NULL;
  const char *col_name_get;
  const char *col_name_dec;
  if (asset_type == ASSET_MINE)
    {
      col_name_get = "mines";
      col_name_dec = "mines=mines";
    }
  else if (asset_type == ASSET_LIMPET_MINE)
    {
      col_name_get = "limpets";
      col_name_dec = "limpets=limpets";
    }
  else
    {
      LOGE ("ship_consume_mines - Invalid asset_type %d", asset_type);
      return SQLITE_ERROR;      // Or a more specific error code
    }
  char sql_get_mines[256];


  snprintf (sql_get_mines, sizeof (sql_get_mines),
            "SELECT %s FROM ships WHERE id=?1;", col_name_get);
  char sql_dec_mines[256];


  snprintf (sql_dec_mines, sizeof (sql_dec_mines),
            "UPDATE ships SET %s-?1 WHERE id=?2;", col_name_dec);
  int rc = sqlite3_prepare_v2 (db, sql_get_mines, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("ship_consume_mines - SQL_SHIP_GET_%s prepare failed: %s",
            col_name_get, sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, ship_id);
  int have = 0;


  if ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      have = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else
    {
      LOGE ("ship_consume_mines - SQL_SHIP_GET_%s step failed or no row: %s",
            col_name_get, sqlite3_errmsg (db));
      rc = SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  if (have < amount)
    {
      LOGI ("ship_consume_mines - Insufficient %s: have %d, requested %d",
            col_name_get, have, amount);
      return SQLITE_TOOBIG;
    }
  rc = sqlite3_prepare_v2 (db, sql_dec_mines, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("ship_consume_mines - SQL_SHIP_DEC_%s prepare failed: %s",
            col_name_get, sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, amount);
  sqlite3_bind_int (st, 2, ship_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}


/* Insert a sector_assets row for mines. */
static int
insert_sector_mines (sqlite3 *db,
                     int sector_id, int owner_player_id,
                     json_t *corp_id_json /* nullable */,
                     int asset_type, int offense_mode, int amount)
{
  sqlite3_stmt *st = NULL;
  const char *sql_insert_mines =
    "INSERT INTO sector_assets(sector, player, corporation, "
    "                          asset_type, quantity, offensive_setting, deployed_at) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, strftime('%s','now'));";
  int rc = sqlite3_prepare_v2 (db, sql_insert_mines, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("insert_sector_mines prepare failed: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, sector_id);
  sqlite3_bind_int (st, 2, owner_player_id);
  if (corp_id_json && json_is_integer (corp_id_json))
    {
      sqlite3_bind_int (st, 3, (int) json_integer_value (corp_id_json));
    }
  else
    {
      sqlite3_bind_int (st, 3, 0);
    }
  sqlite3_bind_int (st, 4, asset_type);
  sqlite3_bind_int (st, 5, amount);
  sqlite3_bind_int (st, 6, offense_mode);
  rc = sqlite3_step (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("insert_sector_mines step failed: %s (rc=%d)",
            sqlite3_errmsg (db), rc);
    }
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}


/* Insert a sector_assets row for fighters. */


/* Insert a sector_assets row for fighters. */
static int
insert_sector_fighters (sqlite3 *db,
                        int sector_id, int owner_player_id,
                        json_t *corp_id_json /* nullable */,
                        int offense_mode, int amount)
{
  sqlite3_stmt *st = NULL;

  /* * NOTE: Make sure SQL_ASSET_INSERT_FIGHTERS uses the integer 2 for asset_type,
   * not the string 'fighters'.
   * e.g., "VALUES (?1, ?2, ?3, 2, ?4, ?5, strftime('%s','now'));"
   */
  int rc = sqlite3_prepare_v2 (db, SQL_ASSET_INSERT_FIGHTERS, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("insert_sector_fighters prepare failed: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, sector_id);
  sqlite3_bind_int (st, 2, owner_player_id);
  /* --- THIS IS THE FIX --- */
  if (corp_id_json && json_is_integer (corp_id_json))
    {
      sqlite3_bind_int (st, 3, (int) json_integer_value (corp_id_json));
    }
  else
    {
      // Bind 0 (the default value) instead of NULL
      sqlite3_bind_int (st, 3, 0);
    }
  /* --- END FIX --- */
  sqlite3_bind_int (st, 4, amount);
  sqlite3_bind_int (st, 5, offense_mode);
  rc = sqlite3_step (st);
  if (rc != SQLITE_DONE)
    {
      // Add this log to see the specific error if it's still failing
      LOGE ("insert_sector_fighters step failed: %s (rc=%d)",
            sqlite3_errmsg (db), rc);
    }
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
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
      LOGE (
        "ERROR: Failed to allocate JSON array for stardock sectors.\n");
      return NULL;
    }
  const char *sql = "SELECT sector_id FROM stardock_location;";


  // 1. Prepare the SQL statement
  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ( "DB Error: Could not prepare stardock query: %s\n",
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
          LOGE ( "ERROR: Failed to append sector ID %d to list.\n",
                 sector_id);
          json_decref (j_sector);       // Clean up the orphaned reference
          // You may choose to stop here or continue
        }
    }
  // 3. Handle step errors if the loop didn't finish normally (SQLITE_DONE)
  if (rc != SQLITE_DONE)
    {
      LOGE ( "DB Error: Failed to step stardock query: %s\n",
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
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error(ctx, root, ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  // 1. Validation
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error(ctx, root, ERR_MISSING_FIELD, "Missing required field: data");
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
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "Missing required field or invalid type: from_sector_id/target_sector_id/fighters_committed/mine_type");
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
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "Invalid mine_type. Must be 'armid' or 'limpet'.");
      return 0;
    }
  if (fighters_committed <= 0)
    {
      send_response_error(ctx, root, ERR_INVALID_ARG, "Fighters committed must be greater than 0.");
      return 0;
    }
  // Limpet-specific feature gates and validation
  if (mine_type == ASSET_LIMPET_MINE)
    {
      if (!g_cfg.mines.limpet.enabled)
        {
          send_response_refused(ctx, root, ERR_LIMPETS_DISABLED, "Limpet mine operations are disabled.",
                                  NULL);
          return 0;
        }
      if (!g_cfg.mines.limpet.sweep_enabled)
        {
          send_response_refused(ctx, root, ERR_LIMPET_SWEEP_DISABLED, "Limpet mine sweeping is disabled.", NULL);
          return 0;
        }
      if (from_sector_id != target_sector_id)
        {
          send_response_error(ctx, root, ERR_INVALID_ARG, "Limpet sweeping must occur in the current sector.");
          return 0;
        }
    }
  // Check if player's ship is in from_sector_id
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "No active ship found for player.");
      return 0;
    }
  int player_current_sector_id = -1;
  int ship_fighters_current = 0;
  int ship_corp_id = 0;
  sqlite3_stmt *stmt_player_ship = NULL;
  const char *sql_player_ship =
    "SELECT sector, fighters, corporation FROM ships WHERE id = ?1;";


  if (sqlite3_prepare_v2 (db, sql_player_ship, -1, &stmt_player_ship, NULL) !=
      SQLITE_OK)
    {
      send_response_error(ctx, root, ERR_DB, "Failed to prepare player ship query.");
      return 0;
    }
  sqlite3_bind_int (stmt_player_ship, 1, ship_id);
  if (sqlite3_step (stmt_player_ship) == SQLITE_ROW)
    {
      player_current_sector_id = sqlite3_column_int (stmt_player_ship, 0);
      ship_fighters_current = sqlite3_column_int (stmt_player_ship, 1);
      ship_corp_id = sqlite3_column_int (stmt_player_ship, 2);
    }
  sqlite3_finalize (stmt_player_ship);
  if (player_current_sector_id != from_sector_id)
    {
      json_t *d = json_pack ("{s:s}", "reason", "not_in_from_sector");
      send_response_refused(ctx, root, REF_NOT_IN_SECTOR, "Ship is not in the specified 'from_sector_id'.", d);
      json_decref(d);
      return 0;
    }
  if (fighters_committed > ship_fighters_current)
    {
      send_response_error(ctx, root, ERR_INVALID_ARG, "Not enough fighters on ship to commit.");
      return 0;
    }
  // Check for warp existence (from_sector_id to target_sector_id)
  if (!h_warp_exists (db, from_sector_id, target_sector_id))
    {
      send_response_error(ctx, root, ERR_TARGET_INVALID, "No warp exists between specified sectors.");
      return 0;
    }
  if (!g_armid_config.sweep.enabled)
    {
      send_response_error(ctx, root, ERR_CAPABILITY_DISABLED, "Mine sweeping is currently disabled.");
      return 0;
    }
  // 2. Load hostile Armid stacks in target_sector_id
  sqlite3_stmt *st = NULL;
  char sql_select_mines[256];


  snprintf (sql_select_mines, sizeof (sql_select_mines),
            "SELECT id, quantity, player, corporation, ttl "
            "FROM sector_assets "
            "WHERE sector = ?1 AND asset_type = %d AND quantity > 0;",
            mine_type);
  if (sqlite3_prepare_v2 (db, sql_select_mines, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("Failed to prepare mine selection statement for sweeping: %s",
            sqlite3_errmsg (db));
      return -1;
    }
  sqlite3_bind_int (st, 1, target_sector_id);
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


  while (sqlite3_step (st) == SQLITE_ROW)
    {
      mine_stack_info_t current_mine_stack = {
        .id = sqlite3_column_int (st, 0),
        .quantity = sqlite3_column_int (st, 1),
        .player = sqlite3_column_int (st, 2),
        .corporation = sqlite3_column_int (st, 3),
        .ttl = sqlite3_column_int (st, 4)
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
            is_asset_hostile (mine_asset_for_check.player, mine_asset_for_check.corporation,
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
  sqlite3_finalize (st);
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
      send_response_ok(ctx, root, "combat.mines_swept_v1", out);
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
      // Sweeping Limpets is *always* done by fighters.
      // Sweeping Limpets is *always* done in the current sector.
      // Limpets are swept at a fixed rate (e.g., 1 fighter sweeps 1 limpet).
      // Fighters are lost at a fixed rate (e.g., 1 fighter lost per 10 limpets swept).
      // Mines to clear: 1 fighter sweeps 1 limpet (configurable)
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
  char *errmsg = NULL;


  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Could not start transaction");
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
        sqlite3_stmt *update_st = NULL;
        const char *sql_update_mine_qty =
          "UPDATE sector_assets SET quantity = ?1 WHERE id = ?2;";


        if (sqlite3_prepare_v2 (db, sql_update_mine_qty, -1, &update_st, NULL)
            != SQLITE_OK)
          {
            LOGE
            (
              "Failed to prepare mine quantity update statement during sweep: %s",
              sqlite3_errmsg (db));
            db_safe_rollback (db, "Safe rollback");
            json_decref (hostile_stacks_json);
            return -1;
          }
        sqlite3_bind_int (update_st, 1, new_qty);
        sqlite3_bind_int (update_st, 2, mine_stack_id);
        if (sqlite3_step (update_st) != SQLITE_DONE)
          {
            LOGE ("Failed to update mine quantity during sweep: %s",
                  sqlite3_errmsg (db));
            db_safe_rollback (db, "Safe rollback");
            json_decref (hostile_stacks_json);
            return -1;
          }
        sqlite3_finalize (update_st);
      }
    else
      {
        sqlite3_stmt *delete_st = NULL;
        const char *sql_delete_mine =
          "DELETE FROM sector_assets WHERE id = ?1;";


        if (sqlite3_prepare_v2 (db, sql_delete_mine, -1, &delete_st, NULL) !=
            SQLITE_OK)
          {
            LOGE ("Failed to prepare mine delete statement during sweep: %s",
                  sqlite3_errmsg (db));
            db_safe_rollback (db, "Safe rollback");
            json_decref (hostile_stacks_json);
            return -1;
          }
        sqlite3_bind_int (delete_st, 1, mine_stack_id);
        if (sqlite3_step (delete_st) != SQLITE_DONE)
          {
            LOGE ("Failed to delete mine during sweep: %s",
                  sqlite3_errmsg (db));
            db_safe_rollback (db, "Safe rollback");
            json_decref (hostile_stacks_json);
            return -1;
          }
        sqlite3_finalize (delete_st);
      }
  }
  // 9. Update ship fighters:
  sqlite3_stmt *update_ship_st = NULL;
  const char *sql_update_ship_fighters =
    "UPDATE ships SET fighters = fighters - ?1 WHERE id = ?2;";


  if (sqlite3_prepare_v2
        (db, sql_update_ship_fighters, -1, &update_ship_st, NULL) != SQLITE_OK)
    {
      LOGE
        ("Failed to prepare ship fighters update statement during sweep: %s",
        sqlite3_errmsg (db));
      db_safe_rollback (db, "Safe rollback");
      json_decref (hostile_stacks_json);
      return -1;
    }
  sqlite3_bind_int (update_ship_st, 1, fighters_lost);
  sqlite3_bind_int (update_ship_st, 2, ship_id);
  if (sqlite3_step (update_ship_st) != SQLITE_DONE)
    {
      LOGE ("Failed to update ship fighters during sweep: %s",
            sqlite3_errmsg (db));
      db_safe_rollback (db, "Safe rollback");
      json_decref (hostile_stacks_json);
      return -1;
    }
  sqlite3_finalize (update_ship_st);
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Commit failed");
      json_decref (hostile_stacks_json);
      return 0;
    }
  // Re-calculate remaining hostile mines for response
  int mines_remaining_after_sweep = 0;
  sqlite3_stmt *recalc_st = NULL;
  const char *sql_recalc_mines_template =
    "SELECT COALESCE(SUM(quantity),0) FROM sector_assets WHERE sector = ?1 AND asset_type = %d AND quantity > 0;";
  // Dynamic asset_type
  char sql_recalc_mines_buf[256];


  snprintf (sql_recalc_mines_buf, sizeof (sql_recalc_mines_buf),
            sql_recalc_mines_template, mine_type);
  if (sqlite3_prepare_v2 (db, sql_recalc_mines_buf, -1, &recalc_st, NULL) ==
      SQLITE_OK)
    {
      sqlite3_bind_int (recalc_st, 1, target_sector_id);
      if (sqlite3_step (recalc_st) == SQLITE_ROW)
        {
          mines_remaining_after_sweep = sqlite3_column_int (recalc_st, 0);
        }
      sqlite3_finalize (recalc_st);
    }
  else
    {
      LOGE ("Failed to re-calculate remaining mines after sweep: %s",
            sqlite3_errmsg (db));
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
  send_response_ok(ctx, root, "combat.mines_swept_v1", out);
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
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error(ctx, root, ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  /* 1. Parse input */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error(ctx, root, ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_asset_id = json_object_get (data, "asset_id");


  if (!j_sector_id || !json_is_integer (j_sector_id) ||
      !j_asset_id || !json_is_integer (j_asset_id))
    {
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "Missing required field or invalid type: sector_id/asset_id");
      return 0;
    }
  int requested_sector_id = (int) json_integer_value (j_sector_id);
  int asset_id = (int) json_integer_value (j_asset_id);
  /* 2. Load player, ship, current_sector_id */
  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (player_ship_id <= 0)
    {
      send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "No active ship found for player.");
      return 0;
    }
  int player_current_sector_id = -1;
  int ship_fighters_current = 0;
  int ship_fighters_max = 0;
  int player_corp_id = 0;
  sqlite3_stmt *stmt_player_ship = NULL;
  const char *sql_player_ship =
    "SELECT s.sector, s.fighters, st.maxfighters, cm.corp_id "
    "FROM ships s "
    "JOIN shiptypes st ON s.type_id = st.id "
    "LEFT JOIN corp_members cm ON cm.player_id = ?1 " "WHERE s.id = ?2;";


  if (sqlite3_prepare_v2 (db, sql_player_ship, -1, &stmt_player_ship, NULL) !=
      SQLITE_OK)
    {
      send_response_error(ctx, root, ERR_DB, "Failed to prepare player ship query.");
      return 0;
    }
  sqlite3_bind_int (stmt_player_ship, 1, ctx->player_id);
  sqlite3_bind_int (stmt_player_ship, 2, player_ship_id);
  if (sqlite3_step (stmt_player_ship) == SQLITE_ROW)
    {
      player_current_sector_id = sqlite3_column_int (stmt_player_ship, 0);
      ship_fighters_current = sqlite3_column_int (stmt_player_ship, 1);
      ship_fighters_max = sqlite3_column_int (stmt_player_ship, 2);
      player_corp_id = sqlite3_column_int (stmt_player_ship, 3);        // 0 if NULL
    }
  sqlite3_finalize (stmt_player_ship);
  if (player_current_sector_id <= 0)
    {
      send_response_error(ctx, root, ERR_SECTOR_NOT_FOUND, "Could not determine player's current sector.");
      return 0;
    }
  /* 3. Validate sector match */
  if (player_current_sector_id != requested_sector_id)
    {
      json_t *d = json_pack ("{s:s}", "reason", "not_in_sector");
      send_response_refused(ctx, root, REF_NOT_IN_SECTOR, "Not in sector", d);
      json_decref(d);
      return 0;
    }
  /* 4. Fetch asset and validate existence */
  sqlite3_stmt *stmt_asset = NULL;
  const char *sql_asset =
    "SELECT quantity, player, corporation, offensive_setting "
    "FROM sector_assets " "WHERE id = ?1 AND sector = ?2 AND asset_type = 2;";                                                                                  // asset_type = 2 for fighters


  if (sqlite3_prepare_v2 (db, sql_asset, -1, &stmt_asset, NULL) != SQLITE_OK)
    {
      send_response_error(ctx, root, ERR_DB, "Failed to prepare asset query.");
      return 0;
    }
  sqlite3_bind_int (stmt_asset, 1, asset_id);
  sqlite3_bind_int (stmt_asset, 2, requested_sector_id);
  int asset_qty = 0;
  int asset_owner_player_id = 0;
  int asset_owner_corp_id = 0;
  int asset_offensive_setting = 0;


  if (sqlite3_step (stmt_asset) == SQLITE_ROW)
    {
      asset_qty = sqlite3_column_int (stmt_asset, 0);
      asset_owner_player_id = sqlite3_column_int (stmt_asset, 1);
      asset_owner_corp_id = sqlite3_column_int (stmt_asset, 2); // 0 if NULL
      asset_offensive_setting = sqlite3_column_int (stmt_asset, 3);
    }
  sqlite3_finalize (stmt_asset);
  if (asset_qty <= 0)
    {
      send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "Asset not found");
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
      // (Assuming player_corp_id is 0 if not in a corp, and non-zero if in one)
      is_owner = true;
    }
  if (!is_owner)
    {
      json_t *d = json_pack ("{s:s}", "reason", "not_owner");
      send_response_refused(ctx, root, ERR_TARGET_INVALID, "Not owner", d);
      json_decref(d);
      return 0;
    }
  /* 6. Compute pickup quantity */
  int available_to_recall = asset_qty;
  int capacity_left = ship_fighters_max - ship_fighters_current;
  int take = 0;


  if (capacity_left <= 0)
    {
      json_t *d = json_pack ("{s:s}", "reason", "no_capacity");
      send_response_refused(ctx, root, ERR_OUT_OF_RANGE, "No capacity", d);
      json_decref(d);
      return 0;
    }
  take =
    (available_to_recall <
     capacity_left) ? available_to_recall : capacity_left;
  if (take <= 0)                // Now this check makes sense
    {
      json_t *d = json_pack ("{s:s}", "reason", "no_fighters_or_capacity");
      send_response_refused(ctx, root, ERR_OUT_OF_RANGE, "No fighters to recall or no capacity", d);
      json_decref(d);
      return 0;
    }
  /* 7. Apply changes (transaction) */
  char *errmsg = NULL;


  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Could not start transaction");
      return 0;
    }
  /* Increment ship fighters */
  sqlite3_stmt *stmt_update_ship = NULL;
  const char *sql_update_ship =
    "UPDATE ships SET fighters = fighters + ?1 WHERE id = ?2;";


  if (sqlite3_prepare_v2 (db, sql_update_ship, -1, &stmt_update_ship, NULL) !=
      SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, ERR_DB, "Failed to prepare ship update.");
      return 0;
    }
  sqlite3_bind_int (stmt_update_ship, 1, take);
  sqlite3_bind_int (stmt_update_ship, 2, player_ship_id);
  if (sqlite3_step (stmt_update_ship) != SQLITE_DONE)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, ERR_DB, "Failed to update ship fighters.");
      return 0;
    }
  sqlite3_finalize (stmt_update_ship);
  /* Update or delete sector_assets record */
  if (take == asset_qty)
    {
      sqlite3_stmt *stmt_delete_asset = NULL;
      const char *sql_delete_asset =
        "DELETE FROM sector_assets WHERE id = ?1;";


      if (sqlite3_prepare_v2
            (db, sql_delete_asset, -1, &stmt_delete_asset, NULL) != SQLITE_OK)
        {
          db_safe_rollback (db, "Safe rollback");
          send_response_error(ctx, root, ERR_DB, "Failed to prepare asset delete.");
          return 0;
        }
      sqlite3_bind_int (stmt_delete_asset, 1, asset_id);
      if (sqlite3_step (stmt_delete_asset) != SQLITE_DONE)
        {
          db_safe_rollback (db, "Safe rollback");
          send_response_error(ctx, root, ERR_DB, "Failed to delete asset record.");
          return 0;
        }
      sqlite3_finalize (stmt_delete_asset);
    }
  else
    {
      sqlite3_stmt *stmt_update_asset = NULL;
      const char *sql_update_asset =
        "UPDATE sector_assets SET quantity = quantity - ?1 WHERE id = ?2;";


      if (sqlite3_prepare_v2
            (db, sql_update_asset, -1, &stmt_update_asset, NULL) != SQLITE_OK)
        {
          db_safe_rollback (db, "Safe rollback");
          send_response_error(ctx, root, ERR_DB, "Failed to prepare asset update.");
          return 0;
        }
      sqlite3_bind_int (stmt_update_asset, 1, take);
      sqlite3_bind_int (stmt_update_asset, 2, asset_id);
      if (sqlite3_step (stmt_update_asset) != SQLITE_DONE)
        {
          db_safe_rollback (db, "Safe rollback");
          send_response_error(ctx, root, ERR_DB, "Failed to update asset quantity.");
          return 0;
        }
      sqlite3_finalize (stmt_update_asset);
    }
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Commit failed");
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
  send_response_ok(ctx, root, "combat.fighters.deployed", out);
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
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error(ctx, root, ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  /* 1. Input Parsing */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error(ctx, root, ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_mine_type_str = json_object_get (data, "mine_type");
  json_t *j_asset_id = json_object_get (data, "asset_id");      // Optional


  if (!j_sector_id || !json_is_integer (j_sector_id) ||
      !j_mine_type_str || !json_is_string (j_mine_type_str))
    {
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "Missing required field or invalid type: sector_id/mine_type");
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
      send_response_error(ctx, root, ERR_INVALID_ARG, "Only 'limpet' mine_type can be scrubbed.");
      return 0;
    }
  /* 2. Feature Gate */
  if (!g_cfg.mines.limpet.enabled)
    {
      send_response_refused(ctx, root, ERR_LIMPETS_DISABLED, "Limpet mine operations are disabled.", NULL);
      return 0;
    }
  /* 3. Cost Check */
  int scrub_cost = g_cfg.mines.limpet.scrub_cost;


  if (scrub_cost > 0)
    {
      // Check player credits
      long long player_credits = 0;


      if (h_get_player_petty_cash (db, ctx->player_id, &player_credits) !=
          SQLITE_OK)
        {
          send_response_error(ctx, root, ERR_DB, "Failed to retrieve player credits.");
          return 0;
        }
      if (player_credits < scrub_cost)
        {
          send_response_refused(ctx, root, ERR_INSUFFICIENT_FUNDS, "Insufficient credits to scrub mines.",
                                  NULL);
          return 0;
        }
    }
  /* 4. Ownership Validation and Scrubbing Logic */
  char *errmsg = NULL;
  int total_scrubbed = 0;


  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Could not start transaction");
      return 0;
    }
  // SQL for scrubbing
  const char *sql_scrub_mines;
  sqlite3_stmt *scrub_st = NULL;
  int rc;


  if (asset_id > 0)
    {
      // Scrub specific asset
      sql_scrub_mines =
        "DELETE FROM sector_assets "
        "WHERE id = ?1 AND sector = ?2 AND asset_type = ?3 AND (player = ?4 OR corporation = ?5);";
      rc = sqlite3_prepare_v2 (db, sql_scrub_mines, -1, &scrub_st, NULL);
      if (rc != SQLITE_OK)
        {
          LOGE ("Failed to prepare specific scrub statement: %s",
                sqlite3_errmsg (db));
          db_safe_rollback (db, "Safe rollback");
          send_response_error(ctx, root, ERR_DB, "Failed to prepare scrub operation.");
          return 0;
        }
      sqlite3_bind_int (scrub_st, 1, asset_id);
      sqlite3_bind_int (scrub_st, 2, sector_id);
      sqlite3_bind_int (scrub_st, 3, ASSET_LIMPET_MINE);
      sqlite3_bind_int (scrub_st, 4, ctx->player_id);
      sqlite3_bind_int (scrub_st, 5, ctx->corp_id);
    }
  else
    {
      // Scrub all player's Limpets in sector
      sql_scrub_mines =
        "DELETE FROM sector_assets "
        "WHERE sector = ?1 AND asset_type = ?2 AND (player = ?3 OR corporation = ?4);";
      rc = sqlite3_prepare_v2 (db, sql_scrub_mines, -1, &scrub_st, NULL);
      if (rc != SQLITE_OK)
        {
          LOGE ("Failed to prepare all scrub statement: %s",
                sqlite3_errmsg (db));
          db_safe_rollback (db, "Safe rollback");
          send_response_error(ctx, root, ERR_DB, "Failed to prepare scrub operation.");
          return 0;
        }
      sqlite3_bind_int (scrub_st, 1, sector_id);
      sqlite3_bind_int (scrub_st, 2, ASSET_LIMPET_MINE);
      sqlite3_bind_int (scrub_st, 3, ctx->player_id);
      sqlite3_bind_int (scrub_st, 4, ctx->corp_id);
    }
  rc = sqlite3_step (scrub_st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("Scrub mines step failed: %s", sqlite3_errmsg (db));
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, ERR_DB, "Failed to execute scrub operation.");
      sqlite3_finalize (scrub_st);
      return 0;
    }
  total_scrubbed = sqlite3_changes (db);        // Number of rows deleted
  sqlite3_finalize (scrub_st);
  if (total_scrubbed == 0)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_refused(ctx, root, ERR_NOT_FOUND, "No Limpet mines found to scrub.", NULL);
      return 0;
    }
  /* 5. Debit Credits (if applicable) */
  if (scrub_cost > 0)
    {
      long long new_balance;


      rc =
        h_deduct_player_petty_cash (db, ctx->player_id, scrub_cost,
                                    &new_balance);
      if (rc != SQLITE_OK)
        {
          LOGE ("Failed to debit credits for scrubbing: %s",
                sqlite3_errmsg (db));
          db_safe_rollback (db, "Safe rollback");
          send_response_error(ctx, root, ERR_DB, "Failed to debit credits.");
          return 0;
        }
    }
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Commit failed");
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
  send_response_ok(ctx, root, "combat.mines_scrubbed_v1", out);
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
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error(ctx, root, ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  bool in_fed = false;
  bool in_sdock = false;
  /* Parse input */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error(ctx, root, ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_amount = json_object_get (data, "amount");
  json_t *j_offense = json_object_get (data, "offense");        /* required: 1..3 */
  json_t *j_corp_id = json_object_get (data, "corporation_id"); /* optional, nullable */
  json_t *j_mine_type = json_object_get (data, "mine_type");    /* optional, 1 for Armid, 4 for Limpet */


  if (!j_amount || !json_is_integer (j_amount) ||
      !j_offense || !json_is_integer (j_offense))
    {
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "Missing required field or invalid type: amount/offense");
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
          send_response_error(ctx, root, ERR_CURSOR_INVALID, "Invalid mine_type. Must be 1 (Armid) or 4 (Limpet).");
          return 0;
        }
    }
  if (amount <= 0)
    {
      send_response_error(ctx, root, ERR_NOT_FOUND, "amount must be > 0");
      return 0;
    }
  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK)
    {
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "offense must be one of {1=TOLL,2=DEFEND,3=ATTACK}");
      return 0;
    }
  /* Resolve active ship + sector */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }
  /* Decloak: visible hostile/defensive action */
  (void) h_decloak_ship (db, ship_id);
  int sector_id = -1;
  {
    sqlite3_stmt *st = NULL;


    if (sqlite3_prepare_v2
          (db, "SELECT sector FROM ships WHERE id=?1;", -1, &st,
          NULL) != SQLITE_OK)
      {
        char error_buffer[256];


        snprintf (error_buffer,
                  sizeof (error_buffer),
                  "Unable to resolve current sector - SELECT sector FROM ships WHERE id=%d;",
                  ship_id);
        char *shperror = error_buffer;


        send_response_error(ctx, root, ERR_SECTOR_NOT_FOUND, shperror);
        return 0;
      }
    sqlite3_bind_int (st, 1, ship_id);
    if (sqlite3_step (st) == SQLITE_ROW)
      {
        sector_id = sqlite3_column_int (st, 0);
      }
    sqlite3_finalize (st);
    if (sector_id <= 0)
      {
        char error_buffer[256];


        snprintf (error_buffer,
                  sizeof (error_buffer),
                  "Unable to resolve current sector - SELECT sector_id FROM ships WHERE id=%d;",
                  ship_id);
        char *scterror = error_buffer;


        send_response_error(ctx, root, ERR_SECTOR_NOT_FOUND, scterror);
        return 0;
      }
  }
  /* Sector cap */
  int sector_total = 0;


  if (sum_sector_mines (db, sector_id, &sector_total) != SQLITE_OK)
    {
      send_response_error(ctx, root, REF_NOT_IN_SECTOR, "Failed to read sector mines");
      return 0;
    }
  if (sector_total + amount > SECTOR_MINE_CAP)
    {                           // Assuming SECTOR_MINE_CAP is defined
      send_response_error(ctx, root, ERR_SECTOR_OVERCROWDED, "Sector mine limit exceeded (50,000)");
      return 0;
    }
  /* Transaction: debit ship, credit sector */
  char *errmsg = NULL;


  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Could not start transaction");
      return 0;
    }
  int rc = ship_consume_mines (db, ship_id, mine_type, amount);


  if (rc == SQLITE_TOOBIG)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, REF_AMMO_DEPLETED, "Insufficient mines on ship");
      return 0;
    }
  if (rc != SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, REF_AMMO_DEPLETED, "Failed to update ship mines");
      return 0;
    }
  rc =
    insert_sector_mines (db, sector_id, ctx->player_id, j_corp_id, mine_type,
                         offense, amount);
  if (rc != SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, SECTOR_ERR, "Failed to create sector assets record");
      return 0;
    }
  int asset_id = (int) sqlite3_last_insert_rowid (db);  // Capture the newly created asset_id


  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Commit failed");
      return 0;
    }
  /* Fedspace/Stardock → summon ISS + warn player */
  // Check for Federation sectors (1-10)
  if (sector_id >= 1 && sector_id <= 10)
    {
      in_fed = true;
    }
  // Get the list of stardock sectors from the database
  // The function returns a new reference which MUST be freed.
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
      // h_send_message_to_player (int player_id, int sender_id, const char *subject,
      // const char *message)
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
  send_response_ok(ctx, root, "combat.mines.deployed", out);
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
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error(ctx, root, ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  bool in_fed = false;
  // bool in_sdock = false; // Not used currently in this function for mine deployment
  /* Parse input */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error(ctx, root, ERR_MISSING_FIELD, "Missing required field: data");
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
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "Missing required field or invalid type: count/offense_mode/owner_mode/mine_type");
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
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "Invalid mine_type. Must be 'armid' or 'limpet'.");
      return 0;
    }
  if (amount <= 0)
    {
      send_response_error(ctx, root, ERR_INVALID_ARG, "Count must be > 0");
      return 0;
    }
  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK)
    {
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "offense_mode must be one of {1=TOLL,2=DEFEND,3=ATTACK}");
      return 0;
    }
  if (strcasecmp (owner_mode_str, "personal") != 0
      && strcasecmp (owner_mode_str, "corp") != 0)
    {
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "Invalid owner_mode. Must be 'personal' or 'corp'.");
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
          send_response_error(ctx, root, ERR_NOT_IN_CORP, "Cannot deploy as corp: player not in a corporation.");
          return 0;
        }
    }
  /* Feature Gate for Limpet Mines */
  if (mine_type == ASSET_LIMPET_MINE && !g_cfg.mines.limpet.enabled)
    {
      send_response_refused(ctx, root, ERR_LIMPETS_DISABLED, "Limpet mine deployment is disabled.", NULL);
      return 0;
    }
  /* Resolve active ship + sector */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }
  /* Decloak: visible hostile/defensive action */
  (void) h_decloak_ship (db, ship_id);
  int sector_id = -1;
  {
    sqlite3_stmt *st = NULL;


    if (sqlite3_prepare_v2
          (db, "SELECT sector FROM ships WHERE id=?1;", -1, &st,
          NULL) != SQLITE_OK)
      {
        char error_buffer[256];


        snprintf (error_buffer,
                  sizeof (error_buffer),
                  "Unable to resolve current sector - SELECT sector FROM ships WHERE id=%d;",
                  ship_id);
        char *shperror = error_buffer;


        send_response_error(ctx, root, ERR_SECTOR_NOT_FOUND, shperror);
        return 0;
      }
    sqlite3_bind_int (st, 1, ship_id);
    if (sqlite3_step (st) == SQLITE_ROW)
      {
        sector_id = sqlite3_column_int (st, 0);
      }
    sqlite3_finalize (st);
    if (sector_id <= 0)
      {
        char error_buffer[256];


        snprintf (error_buffer,
                  sizeof (error_buffer),
                  "Unable to resolve current sector - SELECT sector_id FROM ships WHERE id=%d;",
                  ship_id);
        char *scterror = error_buffer;


        send_response_error(ctx, root, ERR_SECTOR_NOT_FOUND, scterror);
        return 0;
      }
  }


  /* FedSpace & MSL rules for Limpet Mines */
  if (mine_type == ASSET_LIMPET_MINE)
    {
      if (!g_cfg.mines.limpet.fedspace_allowed
          && is_fedspace_sector (sector_id))
        {
          send_response_refused(ctx, root, REF_TERRITORY_UNSAFE, "Cannot deploy Limpet mines in Federation space.",
                                  NULL);
          return 0;
        }
      if (!g_cfg.mines.limpet.msl_allowed && is_msl_sector (db, sector_id))
        {
          send_response_refused(ctx, root, REF_TERRITORY_UNSAFE, "Cannot deploy Limpet mines in Major Space Lanes.",
                                  NULL);
          return 0;
        }
    }
  /* Foreign Limpets in Sector (blocks both Armid and Limpet deployment) */
  sqlite3_stmt *foreign_limpet_st = NULL;
  const char *sql_foreign_limpets =
    "SELECT 1 FROM sector_assets " "WHERE sector = ?1 " "  AND asset_type = ?2 "                                        // ASSET_LIMPET_MINE = 4
    "  AND quantity > 0 "
    "  AND NOT (player = ?3 OR (corporation != 0 AND corporation = ?4)) "
    "LIMIT 1;";


  if (sqlite3_prepare_v2
        (db, sql_foreign_limpets, -1, &foreign_limpet_st, NULL) != SQLITE_OK)
    {
      LOGE ("Failed to prepare foreign limpet check statement: %s",
            sqlite3_errmsg (db));
      send_response_error(ctx, root, ERR_DB, "Failed to check for foreign limpets.");
      return 0;
    }
  sqlite3_bind_int (foreign_limpet_st, 1, sector_id);
  sqlite3_bind_int (foreign_limpet_st, 2, ASSET_LIMPET_MINE);
  sqlite3_bind_int (foreign_limpet_st, 3, ctx->player_id);
  sqlite3_bind_int (foreign_limpet_st, 4, ctx->corp_id);        // Assuming ctx->corp_id is available
  if (sqlite3_step (foreign_limpet_st) == SQLITE_ROW)
    {
      sqlite3_finalize (foreign_limpet_st);
      send_response_refused(ctx, root, ERR_FOREIGN_LIMPETS_PRESENT, "Cannot deploy mines: foreign Limpet mines are present in this sector.",
                              NULL);
      return 0;
    }
  sqlite3_finalize (foreign_limpet_st);
  // Check ship's mine capacity and count
  int ship_mines = 0;
  int ship_limpets = 0;
  sqlite3_stmt *st = NULL;
  const char *sql_ship_mines =
    "SELECT mines, limpets FROM ships WHERE id = ?1;";


  if (sqlite3_prepare_v2 (db, sql_ship_mines, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, ship_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          ship_mines = sqlite3_column_int (st, 0);
          ship_limpets = sqlite3_column_int (st, 1);
        }
      sqlite3_finalize (st);
    }
  else
    {
      send_response_error(ctx, root, ERR_DB, "Failed to query ship's mine count.");
      return 0;
    }
  if (mine_type == ASSET_MINE)
    {
      if (ship_mines < amount)
        {
          send_response_error(ctx, root, REF_AMMO_DEPLETED, "Insufficient Armid mines on ship.");
          return 0;
        }
    }
  else if (mine_type == ASSET_LIMPET_MINE)
    {
      if (ship_limpets < amount)
        {
          send_response_error(ctx, root, REF_AMMO_DEPLETED, "Insufficient Limpet mines on ship.");
          return 0;
        }
    }
  /* Sector Caps */
  sector_mine_counts_t counts;


  if (get_sector_mine_counts (sector_id, &counts) != SQLITE_OK)
    {
      send_response_error(ctx, root, ERR_DB, "Failed to retrieve sector mine counts.");
      return 0;
    }
  // Combined Cap (Armid + Limpet)
  if (counts.total_mines + amount > SECTOR_MINE_CAP)
    {
      send_response_refused(ctx, root, ERR_SECTOR_OVERCROWDED, "Sector total mine limit exceeded (50,000).",
                              NULL);                                                                                            // Using macro for now
      return 0;
    }
  // Type-specific Cap
  if (mine_type == ASSET_MINE)
    {
      // Armid-specific cap (using existing MINE_SECTOR_CAP_PER_TYPE for now, or define a new one)
      if (counts.armid_mines + amount > MINE_SECTOR_CAP_PER_TYPE)
        {
          send_response_refused(ctx, root, ERR_SECTOR_OVERCROWDED, "Sector Armid mine limit exceeded (100).",
                                  NULL);
          return 0;
        }
    }
  else if (mine_type == ASSET_LIMPET_MINE)
    {
      if (counts.limpet_mines + amount > g_cfg.mines.limpet.per_sector_cap)
        {
          json_t *data_opt = json_pack ("{s:i}", "configured_cap",
                                        g_cfg.mines.limpet.per_sector_cap);


          send_response_refused(ctx, root, ERR_SECTOR_OVERCROWDED, "Sector Limpet mine limit exceeded.",
                                  data_opt);
          json_decref (data_opt);
          return 0;
        }
    }
  /* Transaction: debit ship, credit sector */
  char *errmsg = NULL;


  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Could not start transaction");
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
      db_safe_rollback (db, "Safe rollback");
      if (mine_type == ASSET_MINE)
        {
          send_response_error(ctx, root, REF_AMMO_DEPLETED, "Insufficient Armid mines on ship");
        }
      else
        {
          send_response_error(ctx, root, REF_AMMO_DEPLETED, "Insufficient Limpet mines on ship");
        }
      return 0;
    }
  if (rc != SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      if (mine_type == ASSET_MINE)
        {
          send_response_error(ctx, root, REF_AMMO_DEPLETED, "Failed to update ship Armid mines");
        }
      else
        {
          send_response_error(ctx, root, REF_AMMO_DEPLETED, "Failed to update ship Limpet mines");
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
  if (rc != SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, SECTOR_ERR, "Failed to create sector assets record");
      return 0;
    }
  int asset_id = (int) sqlite3_last_insert_rowid (db);  // Capture the newly created asset_id


  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Commit failed");
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
  send_response_ok(ctx, root, "combat.mines_laid_v1", out);       // Changed type
  return 0;
}


int
cmd_mines_recall (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error(ctx, root, ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }
  /* 1. Parse input */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error(ctx, root, ERR_MISSING_FIELD, "Missing required field: data");
      return 0;
    }
  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_asset_id = json_object_get (data, "asset_id");


  if (!j_sector_id || !json_is_integer (j_sector_id) ||
      !j_asset_id || !json_is_integer (j_asset_id))
    {
      send_response_error(ctx, root, ERR_CURSOR_INVALID, "Missing required field or invalid type: sector_id/asset_id");
      return 0;
    }
  int requested_sector_id = (int) json_integer_value (j_sector_id);
  int asset_id = (int) json_integer_value (j_asset_id);
  /* 2. Load player, ship, current_sector_id */
  int player_ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (player_ship_id <= 0)
    {
      send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "No active ship found for player.");
      return 0;
    }
  int player_current_sector_id = -1;
  int ship_mines_current = 0;
  int ship_mines_max = 0;
  sqlite3_stmt *stmt_player_ship = NULL;
  const char *sql_player_ship =
    "SELECT s.sector, s.mines, st.maxmines "
    "FROM ships s "
    "JOIN shiptypes st ON s.type_id = st.id " "WHERE s.id = ?1;";


  if (sqlite3_prepare_v2 (db, sql_player_ship, -1, &stmt_player_ship, NULL) !=
      SQLITE_OK)
    {
      send_response_error(ctx, root, ERR_DB, "Failed to prepare player ship query.");
      return 0;
    }
  sqlite3_bind_int (stmt_player_ship, 1, player_ship_id);
  if (sqlite3_step (stmt_player_ship) == SQLITE_ROW)
    {
      player_current_sector_id = sqlite3_column_int (stmt_player_ship, 0);
      ship_mines_current = sqlite3_column_int (stmt_player_ship, 1);
      ship_mines_max = sqlite3_column_int (stmt_player_ship, 2);
    }
  sqlite3_finalize (stmt_player_ship);
  if (player_current_sector_id <= 0)
    {
      send_response_error(ctx, root, ERR_SECTOR_NOT_FOUND, "Could not determine player's current sector.");
      return 0;
    }
  /* 3. Verify asset belongs to player and is in current sector */
  sqlite3_stmt *stmt_asset = NULL;
  const char *sql_asset =
    "SELECT quantity, asset_type FROM sector_assets "
    "WHERE id = ?1 AND player = ?2 AND sector = ?3 AND asset_type IN (1, 4);";                                                                          // Mines only


  if (sqlite3_prepare_v2 (db, sql_asset, -1, &stmt_asset, NULL) != SQLITE_OK)
    {
      send_response_error(ctx, root, ERR_DB, "Failed to prepare asset query.");
      return 0;
    }
  sqlite3_bind_int (stmt_asset, 1, asset_id);
  sqlite3_bind_int (stmt_asset, 2, ctx->player_id);
  sqlite3_bind_int (stmt_asset, 3, requested_sector_id);
  int asset_quantity = 0;
  int asset_type = 0;


  if (sqlite3_step (stmt_asset) == SQLITE_ROW)
    {
      asset_quantity = sqlite3_column_int (stmt_asset, 0);
      asset_type = sqlite3_column_int (stmt_asset, 1);
    }
  sqlite3_finalize (stmt_asset);
  if (asset_quantity <= 0)
    {
      send_response_error(ctx, root, ERR_NOT_FOUND, "Mine asset not found or does not belong to you in this sector.");
      return 0;
    }
  /* 4. Check if ship has capacity for recalled mines */
  if (ship_mines_current + asset_quantity > ship_mines_max)
    {
      send_response_refused(ctx, root, REF_INSUFFICIENT_CAPACITY, "Insufficient ship capacity to recall all mines.",
                              json_pack ("{s:s}", "reason",
                                         "insufficient_mine_capacity"));
      return 0;
    }
  /* 5. Transaction: delete asset, credit ship */
  char *errmsg = NULL;


  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Could not start transaction");
      return 0;
    }
  // Delete the asset from sector_assets
  sqlite3_stmt *stmt_delete = NULL;
  const char *sql_delete = "DELETE FROM sector_assets WHERE id = ?1;";


  if (sqlite3_prepare_v2 (db, sql_delete, -1, &stmt_delete, NULL) !=
      SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, ERR_DB, "Failed to prepare delete asset query.");
      return 0;
    }
  sqlite3_bind_int (stmt_delete, 1, asset_id);
  if (sqlite3_step (stmt_delete) != SQLITE_DONE)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, ERR_DB, "Failed to delete asset from sector.");
      sqlite3_finalize (stmt_delete);
      return 0;
    }
  sqlite3_finalize (stmt_delete);
  // Credit mines to ship
  sqlite3_stmt *stmt_credit = NULL;
  const char *sql_credit =
    "UPDATE ships SET mines = mines + ?1 WHERE id = ?2;";


  if (sqlite3_prepare_v2 (db, sql_credit, -1, &stmt_credit, NULL) !=
      SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, ERR_DB, "Failed to prepare credit ship query.");
      return 0;
    }
  sqlite3_bind_int (stmt_credit, 1, asset_quantity);
  sqlite3_bind_int (stmt_credit, 2, player_ship_id);
  if (sqlite3_step (stmt_credit) != SQLITE_DONE)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error(ctx, root, ERR_DB, "Failed to credit mines to ship.");
      sqlite3_finalize (stmt_credit);
      return 0;
    }
  sqlite3_finalize (stmt_credit);
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_response_error(ctx, root, ERR_DB, "Commit failed");
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
  send_response_ok(ctx, root, "combat.mines.recalled", out);
  return 0;
}

/* --- Phase 3: Ship vs Ship Combat --- */
/* Helper to get full ship combat stats */
static int load_ship_combat_stats(sqlite3 *db, int ship_id, combat_ship_t *out) {
    const char *sql = 
        "SELECT s.id, s.hull, s.shields, s.fighters, s.sector, s.name, "
        "       st.offense, st.defense, st.maxattack, "
        "       op.player_id, cm.corp_id "
        "FROM ships s "
        "JOIN shiptypes st ON s.type_id = st.id "
        "JOIN ship_ownership op ON op.ship_id = s.id AND op.is_primary = 1 "
        "LEFT JOIN corp_members cm ON cm.player_id = op.player_id "
        "WHERE s.id = ?1;";
    
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_int(st, 1, ship_id);
    
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        out->id = sqlite3_column_int(st, 0);
        out->hull = sqlite3_column_int(st, 1);
        out->shields = sqlite3_column_int(st, 2);
        out->fighters = sqlite3_column_int(st, 3);
        out->sector = sqlite3_column_int(st, 4); // New: Assign sector
        strncpy(out->name, (const char*)sqlite3_column_text(st, 5), sizeof(out->name)-1);
        out->attack_power = sqlite3_column_int(st, 6);
        out->defense_power = sqlite3_column_int(st, 7);
        out->max_attack = sqlite3_column_int(st, 8);
        out->player_id = sqlite3_column_int(st, 9);
        out->corp_id = sqlite3_column_int(st, 10);
        sqlite3_finalize(st);
        return 0;
    }
    sqlite3_finalize(st);
    return -1; // Not found
}

static void apply_combat_damage(combat_ship_t *target, int damage, int *shields_lost, int *hull_lost) {
    *shields_lost = 0;
    *hull_lost = 0;
    int remaining = damage;
    
    // 1. Shields
    if (target->shields > 0) {
        int absorb = MIN(remaining, target->shields);
        target->shields -= absorb;
        remaining -= absorb;
        *shields_lost = absorb;
    }
    
    // 2. Hull
    if (remaining > 0) {
        target->hull -= remaining;
        *hull_lost = remaining;
    }
}

static int persist_ship_damage(sqlite3 *db, combat_ship_t *ship, int fighters_lost)
{
    const char *sql = "UPDATE ships SET hull=?1, shields=?2, fighters=MAX(0, fighters-?3) WHERE id=?4;";
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(db, sql, -1, &st, NULL);
    sqlite3_bind_int(st, 1, MAX(0, ship->hull));
    sqlite3_bind_int(st, 2, MAX(0, ship->shields));
    sqlite3_bind_int(st, 3, fighters_lost);
    sqlite3_bind_int(st, 4, ship->id);
    sqlite3_step(st);
    sqlite3_finalize(st);
    return 0;
}

int handle_ship_attack(client_ctx_t *ctx, json_t *root, json_t *data, sqlite3 *db)
{
  int target_ship_id = 0;
  if (!json_get_int_flexible(data, "target_ship_id", &target_ship_id) || target_ship_id <= 0) {
    send_response_error(ctx, root, ERR_MISSING_FIELD, "Missing or invalid target_ship_id");
    return 0;
  }
    
  int req_fighters = 0;
  if (!json_get_int_flexible(data, "commit_fighters", &req_fighters)) {
    // Optional, defaults to 0 (all available logic handled below)
    req_fighters = 0;
  }

  int attacker_ship_id = h_get_active_ship_id(db, ctx->player_id);
  if (attacker_ship_id <= 0) {
    send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
    return 0;
  }

  // Load Stats
  combat_ship_t attacker = {0};
  combat_ship_t defender = {0};
  if (load_ship_combat_stats(db, attacker_ship_id, &attacker) != 0) {
    send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "Attacker ship not found");
    return 0;
  }
  if (load_ship_combat_stats(db, target_ship_id, &defender) != 0) {
    send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "Target ship not found");
    return 0;
  }

  // Validate Sector (must be co-located)
  int att_sector = attacker.sector;
  int def_sector = defender.sector;
  LOGI("DEBUG: handle_ship_attack: attacker %d sector %d, defender %d sector %d", attacker.id, att_sector, defender.id, def_sector);
  if (att_sector != def_sector || att_sector <= 0) {
    send_response_error(ctx, root, ERR_TARGET_INVALID, "Target not in same sector");
    return 0;
  }

  // --- Round 1: Attacker Strikes ---
    
  // Calculate Committed Fighters (Invariant: <= MaxAttack, <= Onboard)
  int att_committed = attacker.max_attack; 
  if (req_fighters > 0) att_committed = MIN(req_fighters, attacker.max_attack);
  att_committed = MIN(att_committed, attacker.fighters);
    
  if (att_committed < 0) att_committed = 0;

  // Calculate Attack Strength
  // atk = commit * (1 + offense * scale)
  double att_mult = 1.0 + ((double)attacker.attack_power * OFFENSE_SCALE);
  int att_raw_dmg = att_committed * DAMAGE_PER_FIGHTER;
  int att_total_dmg = (int)(att_raw_dmg * att_mult);
  LOGI("DEBUG: combat: ATTACKER att_committed=%d, att_power=%d, OFFENSE_SCALE=%.2f, DAMAGE_PER_FIGHTER=%d, att_mult=%.2f, att_raw_dmg=%d, att_total_dmg=%d",
       att_committed, attacker.attack_power, OFFENSE_SCALE, DAMAGE_PER_FIGHTER, att_mult, att_raw_dmg, att_total_dmg);

  // Calculate Defense Strength
  // def_factor = 1 + defense * scale
  double def_factor = 1.0 + ((double)defender.defense_power * DEFENSE_SCALE);
  int effective_dmg_to_def = (int)(att_total_dmg / def_factor);
  LOGI("DEBUG: combat: DEFENDER def_power=%d, DEFENSE_SCALE=%.2f, def_factor=%.2f, effective_dmg_to_def=%d",
       defender.defense_power, DEFENSE_SCALE, def_factor, effective_dmg_to_def);

  // Apply Damage
  int def_shields_lost = 0, def_hull_lost = 0;
  apply_combat_damage(&defender, effective_dmg_to_def, &def_shields_lost, &def_hull_lost);
    
  bool defender_destroyed = (defender.hull <= 0);
    
  // Persist Defender
  if (!defender_destroyed) {
    // Defender survives, update HP/Shields.
    persist_ship_damage(db, &defender, 0);
  } else {
    // Destroy!
    destroy_ship_and_handle_side_effects(NULL, defender.player_id);
  }

  // --- Round 2: Defender Counter-Fire (if alive) ---
  int att_shields_lost = 0, att_hull_lost = 0;
  bool attacker_destroyed = false;
  int def_committed = 0;

  if (!defender_destroyed) {
    // Defender commits max possible
    def_committed = MIN(defender.fighters, defender.max_attack);
        
    double def_mult = 1.0 + ((double)defender.attack_power * OFFENSE_SCALE);
    int def_raw_dmg = def_committed * DAMAGE_PER_FIGHTER;
    int def_total_dmg = (int)(def_raw_dmg * def_mult);
    LOGI("DEBUG: combat: DEFENDER def_committed=%d, def_power=%d, OFFENSE_SCALE=%.2f, DAMAGE_PER_FIGHTER=%d, def_mult=%.2f, def_raw_dmg=%d, def_total_dmg=%d",
         def_committed, defender.attack_power, OFFENSE_SCALE, DAMAGE_PER_FIGHTER, def_mult, def_raw_dmg, def_total_dmg);
        
    double att_def_factor = 1.0 + ((double)attacker.defense_power * DEFENSE_SCALE);
    int effective_dmg_to_att = (int)(def_total_dmg / att_def_factor);
    LOGI("DEBUG: combat: ATTACKER att_defense_power=%d, DEFENSE_SCALE=%.2f, att_def_factor=%.2f, effective_dmg_to_att=%d",
         attacker.defense_power, DEFENSE_SCALE, att_def_factor, effective_dmg_to_att);
        
    apply_combat_damage(&attacker, effective_dmg_to_att, &att_shields_lost, &att_hull_lost);
    attacker_destroyed = (attacker.hull <= 0);
        
    if (!attacker_destroyed) {
      // Persist Attacker
      persist_ship_damage(db, &attacker, 0); 
    } else {
      destroy_ship_and_handle_side_effects(ctx, attacker.player_id);
    }
  }
    
  LOGI("DEBUG: combat: FINAL effective_dmg_to_def=%d, att_shields_lost=%d, att_hull_lost=%d, defender_destroyed=%s, attacker_destroyed=%s",
       effective_dmg_to_def, att_shields_lost, att_hull_lost, defender_destroyed ? "true" : "false", attacker_destroyed ? "true" : "false");

  // Response
  json_t *resp = json_object();
  json_object_set_new(resp, "fighters_committed", json_integer(att_committed));
  json_object_set_new(resp, "damage_dealt", json_integer(effective_dmg_to_def));
  json_object_set_new(resp, "damage_received", json_integer(defender_destroyed ? 0 : att_hull_lost + att_shields_lost)); 
  json_object_set_new(resp, "defender_destroyed", json_boolean(defender_destroyed));
  json_object_set_new(resp, "attacker_destroyed", json_boolean(attacker_destroyed));
    
  send_response_ok(ctx, root, "combat.attack.result", resp);
  return 0;
}


int cmd_combat_status(client_ctx_t *ctx, json_t *root) {
  if (!require_auth(ctx, root)) return 0;
  sqlite3 *db = db_get_handle();
    
  int ship_id = h_get_active_ship_id(db, ctx->player_id);
  if (ship_id <= 0) {
    send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
    return 0;
  }
    
  combat_ship_t ship = {0};
  if (load_ship_combat_stats(db, ship_id, &ship) != 0) {
    send_response_error(ctx, root, ERR_DB, "Failed to load ship stats");
    return 0;
  }
    
  json_t *res = json_object();
  json_object_set_new(res, "hull", json_integer(ship.hull));
  json_object_set_new(res, "shields", json_integer(ship.shields));
  json_object_set_new(res, "fighters", json_integer(ship.fighters));
  json_object_set_new(res, "attack_power", json_integer(ship.attack_power));
  json_object_set_new(res, "defense_power", json_integer(ship.defense_power));
  json_object_set_new(res, "max_attack", json_integer(ship.max_attack));
    
  send_response_ok(ctx, root, "combat.status", res);
  return 0;
}

/*
 * Applies Fighter hazards (Toll or Attack).
 */
static int
apply_sector_fighters_on_entry(client_ctx_t *ctx, int sector_id)
{
  sqlite3 *db = db_get_handle();
  int ship_id = h_get_active_ship_id(db, ctx->player_id);
  if (ship_id <= 0) return 0;

  /* Config */
  int toll_per_unit = 5;
  int damage_per_unit = 10;
  db_get_int_config(db, "fighter_toll_per_unit", &toll_per_unit);
  db_get_int_config(db, "fighter_damage_per_unit", &damage_per_unit);

  /* Get Ship Stats */
  ship_t ship = {0};
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2(db, "SELECT hull, fighters, shields FROM ships WHERE id=?1", -1, &st, NULL) != SQLITE_OK) return -1;
  sqlite3_bind_int(st, 1, ship_id);
  if (sqlite3_step(st) == SQLITE_ROW) {
      ship.hull = sqlite3_column_int(st, 0);
      ship.fighters = sqlite3_column_int(st, 1);
      ship.shields = sqlite3_column_int(st, 2);
  } else {
      sqlite3_finalize(st);
      return 0;
  }
  sqlite3_finalize(st);

  int ship_corp_id = ctx->corp_id;

  /* Scan Fighters (Type 2) */
  const char *sql_ftr = "SELECT id, quantity, player, corporation, offensive_setting FROM sector_assets WHERE sector=?1 AND asset_type=2 AND quantity>0";
  if (sqlite3_prepare_v2(db, sql_ftr, -1, &st, NULL) != SQLITE_OK) return -1;
  sqlite3_bind_int(st, 1, sector_id);

  while (sqlite3_step(st) == SQLITE_ROW) {
      int asset_id = sqlite3_column_int(st, 0);
      int quantity = sqlite3_column_int(st, 1);
      int owner_id = sqlite3_column_int(st, 2);
      int corp_id = sqlite3_column_int(st, 3);
      int mode = sqlite3_column_int(st, 4); // 1=Toll, 2=Attack

      if (!is_asset_hostile(owner_id, corp_id, ctx->player_id, ship_corp_id)) {
          continue;
      }

      bool attack = true;

      /* Toll Mode */
      if (mode == 1) { // TOLL
          long long toll_cost = (long long)quantity * toll_per_unit;
          long long player_creds = 0;
          h_get_player_petty_cash(db, ctx->player_id, &player_creds);

          if (player_creds >= toll_cost) {
              /* Auto-pay */
              char idem[64];
              h_generate_hex_uuid(idem, sizeof(idem));
              // Transfer to asset owner (player)
              const char *dest_type = (corp_id > 0 && owner_id == 0) ? "corp" : "player";
              int dest_id = (corp_id > 0 && owner_id == 0) ? corp_id : owner_id;

              if (h_bank_transfer_unlocked(db, "player", ctx->player_id, dest_type, dest_id, toll_cost, "TOLL", idem) == SQLITE_OK) {
                  attack = false;
                  // Log Toll Paid
                  db_log_engine_event((long long)time(NULL), "combat.toll.paid", "player", ctx->player_id, sector_id, NULL, NULL);
              }
          }
      }

      if (attack) {
          int damage = quantity * damage_per_unit;
          
          /* Apply Damage */
          armid_damage_breakdown_t breakdown = {0};
          apply_armid_damage_to_ship(&ship, damage, &breakdown); // Reusing damage helper

          /* Update Ship */
          sqlite3_stmt *upd;
          if (sqlite3_prepare_v2(db, "UPDATE ships SET hull=?1, fighters=?2, shields=?3 WHERE id=?4", -1, &upd, NULL) == SQLITE_OK) {
              sqlite3_bind_int(upd, 1, ship.hull);
              sqlite3_bind_int(upd, 2, ship.fighters);
              sqlite3_bind_int(upd, 3, ship.shields);
              sqlite3_bind_int(upd, 4, ship_id);
              sqlite3_step(upd);
              sqlite3_finalize(upd);
          }

          /* Consume Fighters (One-time attack) */
          sqlite3_stmt *del_st;
          if (sqlite3_prepare_v2(db, "DELETE FROM sector_assets WHERE id=?1", -1, &del_st, NULL) == SQLITE_OK) {
              sqlite3_bind_int(del_st, 1, asset_id);
              sqlite3_step(del_st);
              sqlite3_finalize(del_st);
          }

          /* Log Hit */
          json_t *evt = json_object();
          json_object_set_new(evt, "damage", json_integer(damage));
          json_object_set_new(evt, "fighters_engaged", json_integer(quantity));
          db_log_engine_event((long long)time(NULL), "combat.hit.fighters", "player", ctx->player_id, sector_id, evt, NULL);

          /* Check Destruction */
          if (ship.hull <= 0) {
              destroy_ship_and_handle_side_effects(ctx, ctx->player_id);
              sqlite3_finalize(st); // Clean up outer loop stmt
              return 1; // Destroyed
          }
      }
  }
  sqlite3_finalize(st);
  return 0;
}

/* Helper to apply Quasar damage (Fighters -> Shields -> Hull) */
static int
h_apply_quasar_damage (client_ctx_t *ctx, int damage, const char *source_desc)
{
  if (damage <= 0) return 0;

  sqlite3 *db = db_get_handle();
  int ship_id = h_get_active_ship_id(db, ctx->player_id);
  if (ship_id <= 0) return 0;

  combat_ship_t ship = {0};
  if (load_ship_combat_stats(db, ship_id, &ship) != 0) return 0;

  int remaining = damage;
  int fighters_lost = 0;
  int shields_lost = 0;
  int hull_lost = 0;

  // 1. Fighters
  if (ship.fighters > 0) {
      int absorb = MIN(remaining, ship.fighters);
      ship.fighters -= absorb;
      remaining -= absorb;
      fighters_lost = absorb;
  }

  // 2. Shields
  if (remaining > 0 && ship.shields > 0) {
      int absorb = MIN(remaining, ship.shields);
      ship.shields -= absorb;
      remaining -= absorb;
      shields_lost = absorb;
  }

  // 3. Hull
  if (remaining > 0) {
      ship.hull -= remaining;
      hull_lost = remaining;
  }

  // Update DB
  persist_ship_damage(db, &ship, fighters_lost);

  // Log Event
  json_t *hit_data = json_object();
  json_object_set_new(hit_data, "damage_total", json_integer(damage));
  json_object_set_new(hit_data, "fighters_lost", json_integer(fighters_lost));
  json_object_set_new(hit_data, "shields_lost", json_integer(shields_lost));
  json_object_set_new(hit_data, "hull_lost", json_integer(hull_lost));
  json_object_set_new(hit_data, "source", json_string(source_desc));
  
  db_log_engine_event((long long)time(NULL), "combat.hit", "player", ctx->player_id, ship.sector, hit_data, NULL);

  // Check destruction
  if (ship.hull <= 0) {
      destroy_ship_and_handle_side_effects(ctx, ctx->player_id);
      return 1;
  }

  return 0;
}

static int
apply_sector_quasar_on_entry (client_ctx_t *ctx, int sector_id)
{
  if (sector_id == 1) return 0; // Terra Safe

  sqlite3 *db = db_get_handle();
  int ship_corp_id = ctx->corp_id;

  sqlite3_stmt *st = NULL;
  const char *sql = 
      "SELECT p.id, p.owner_id, p.owner_type, c.level, c.qCannonSector, c.militaryReactionLevel "
      "FROM planets p "
      "JOIN citadels c ON p.id = c.planet_id "
      "WHERE p.sector = ?1 AND c.level >= 3 AND c.qCannonSector > 0;";
  
  if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) {
      LOGE("apply_sector_quasar_on_entry: prepare failed: %s", sqlite3_errmsg(db));
      return 0;
  }

  sqlite3_bind_int(st, 1, sector_id);
  int shot_fired = 0;

  while (sqlite3_step(st) == SQLITE_ROW) {
      int planet_id = sqlite3_column_int(st, 0);
      int owner_id = sqlite3_column_int(st, 1);
      const char *owner_type = (const char*)sqlite3_column_text(st, 2);
      // int level = sqlite3_column_int(st, 3); // Unused, filter handles it
      int base_strength = sqlite3_column_int(st, 4);
      int reaction = sqlite3_column_int(st, 5);

      // Resolve Corp ID for hostility check
      int p_corp_id = 0;
      if (owner_type && (strcasecmp(owner_type, "corp") == 0 || strcasecmp(owner_type, "corporation") == 0)) {
          p_corp_id = owner_id;
      }

      if (is_asset_hostile(owner_id, p_corp_id, ctx->player_id, ship_corp_id)) {
          // Fire!
          int pct = 100;
          if (reaction == 1) pct = 125;
          else if (reaction >= 2) pct = 150;

          int damage = (int)floor((double)base_strength * (double)pct / 100.0);
          
          char source_desc[64];
          snprintf(source_desc, sizeof(source_desc), "Quasar Sector Shot (Planet %d)", planet_id);
          
          if (h_apply_quasar_damage(ctx, damage, source_desc)) {
              shot_fired = 1; // Destroyed
          } else {
              shot_fired = 2; // Damaged
          }
          break; // Single shot per sector entry
      }
  }
  sqlite3_finalize(st);

  return (shot_fired == 1); // Return 1 if destroyed
}

/* Central Hazard Handler */
int
h_handle_sector_entry_hazards(sqlite3 *db, client_ctx_t *ctx, int sector_id)
{
    (void)db; // Unused, usually available via ctx logic but passed for consistency
    
    /* Quasar First (Long Range) */
    if (apply_sector_quasar_on_entry(ctx, sector_id)) return 1; // Destroyed

    /* Armid Mines Second */
    if (apply_armid_mines_on_entry(ctx, sector_id, NULL)) return 1; // Destroyed

    /* Fighters Third */
    if (apply_sector_fighters_on_entry(ctx, sector_id)) return 1; // Destroyed

    /* Limpets (Phase 2) - Placeholder */
    
    return 0;
}

int
h_trigger_atmosphere_quasar(sqlite3 *db, client_ctx_t *ctx, int planet_id)
{
    // 1. Get Planet/Citadel Info
    sqlite3_stmt *st = NULL;
    const char *sql = 
        "SELECT p.owner_id, p.owner_type, c.level, c.qCannonAtmosphere, c.militaryReactionLevel "
        "FROM planets p "
        "JOIN citadels c ON p.id = c.planet_id "
        "WHERE p.id = ?1 AND c.level >= 3 AND c.qCannonAtmosphere > 0;";
    
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(st, 1, planet_id);
    
    if (sqlite3_step(st) == SQLITE_ROW) {
        int owner_id = sqlite3_column_int(st, 0);
        const char *owner_type = (const char*)sqlite3_column_text(st, 1);
        int base_strength = sqlite3_column_int(st, 3);
        int reaction = sqlite3_column_int(st, 4);
        
        int p_corp_id = 0;
        if (owner_type && (strcasecmp(owner_type, "corp") == 0 || strcasecmp(owner_type, "corporation") == 0)) {
            p_corp_id = owner_id;
        }
        
        if (is_asset_hostile(owner_id, p_corp_id, ctx->player_id, ctx->corp_id)) {
            int pct = 100;
            if (reaction == 1) pct = 125;
            else if (reaction >= 2) pct = 150;

            int damage = (int)floor((double)base_strength * (double)pct / 100.0);
            
            sqlite3_finalize(st); // Done with query
            
            char source_desc[64];
            snprintf(source_desc, sizeof(source_desc), "Quasar Atmosphere Shot (Planet %d)", planet_id);
            
            if (h_apply_quasar_damage(ctx, damage, source_desc)) {
                return 1; // Destroyed
            }
            return 0; // Survived
        }
    }
    sqlite3_finalize(st);
    return 0;
}

#include "server_news.h"

/* Helper to apply Terra sanctions */
static void h_apply_terra_sanctions(sqlite3 *db, int player_id) {
    sqlite3_stmt *st = NULL;

    // 1. Wipe Ships
    if (sqlite3_prepare_v2(db, "DELETE FROM ships WHERE id IN (SELECT ship_id FROM ship_ownership WHERE player_id = ?1)", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, player_id);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    // 2. Zero Player Credits
    if (sqlite3_prepare_v2(db, "UPDATE players SET credits = 0 WHERE id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, player_id);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    // 3. Zero Bank Accounts
    if (sqlite3_prepare_v2(db, "UPDATE bank_accounts SET balance = 0 WHERE owner_type = 'player' AND owner_id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, player_id);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    // 4. Delete Sector Assets
    if (sqlite3_prepare_v2(db, "DELETE FROM sector_assets WHERE player = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, player_id);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    // 5. Delete Limpets
    if (sqlite3_prepare_v2(db, "DELETE FROM limpet_attached WHERE owner_player_id = ?1", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, player_id);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    // 6. Zero Planet Treasuries
    if (sqlite3_prepare_v2(db, "UPDATE citadels SET treasury = 0 WHERE planet_id IN (SELECT id FROM planets WHERE owner_id = ?1 AND owner_type = 'player')", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, player_id);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    // 7. Zero Planet Fighters
    if (sqlite3_prepare_v2(db, "UPDATE planets SET fighters = 0 WHERE owner_id = ?1 AND owner_type = 'player'", -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, player_id);
        sqlite3_step(st);
        sqlite3_finalize(st);
    }

    // 8. Global News
    char msg[256];
    snprintf(msg, sizeof(msg), "Player %d has attacked Terra and has been sanctioned by the Federation. All assets seized.", player_id);
    news_post(msg, "Federation", 0);
}

/* ---------- combat.attack_planet ---------- */
int
cmd_combat_attack_planet (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root)) return 0;
  
  sqlite3 *db = db_get_handle ();
  if (!db) {
      send_response_error(ctx, root, ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
  }

  json_t *data = json_object_get (root, "data");
  if (!data) {
      send_response_error(ctx, root, ERR_MISSING_FIELD, "Missing data");
      return 0;
  }

  json_t *j_pid = json_object_get (data, "planet_id");
  if (!j_pid || !json_is_integer (j_pid)) {
      send_response_error(ctx, root, ERR_INVALID_ARG, "Missing planet_id");
      return 0;
  }
  int planet_id = (int)json_integer_value(j_pid);

  // 1. Get Ship
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0) {
      send_response_error(ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
  }
  
  // 2. Get Sector
  int current_sector = h_get_player_sector(ctx->player_id); 
  if (current_sector <= 0) {
       sqlite3_stmt *st = NULL;
       sqlite3_prepare_v2(db, "SELECT sector FROM ships WHERE id=?1", -1, &st, NULL);
       sqlite3_bind_int(st, 1, ship_id);
       if (sqlite3_step(st) == SQLITE_ROW) current_sector = sqlite3_column_int(st, 0);
       sqlite3_finalize(st);
  }

  // 3. Get Planet Info
  int p_sector = 0;
  int p_fighters = 0;
  int p_owner_id = 0;
  bool p_exists = false;
  
  sqlite3_stmt *pst = NULL;
  if (sqlite3_prepare_v2(db, "SELECT sector_id, owner_id, fighters FROM planets WHERE id=?1", -1, &pst, NULL) == SQLITE_OK) {
      sqlite3_bind_int(pst, 1, planet_id);
      if (sqlite3_step(pst) == SQLITE_ROW) {
          p_exists = true;
          p_sector = sqlite3_column_int(pst, 0);
          p_owner_id = sqlite3_column_int(pst, 1);
          p_fighters = sqlite3_column_int(pst, 2);
      }
      sqlite3_finalize(pst);
  }

  if (!p_exists) {
      send_response_error(ctx, root, ERR_NOT_FOUND, "Planet not found");
      return 0;
  }

  if (p_sector != current_sector) {
      send_response_error(ctx, root, REF_NOT_IN_SECTOR, "Planet not in current sector");
      return 0;
  }

  // 4. Terra Protection
  if (planet_id == 1 || p_sector == 1) {
      destroy_ship_and_handle_side_effects(ctx, ctx->player_id);
      h_apply_terra_sanctions(db, ctx->player_id);
      
      json_t *evt = json_object();
      json_object_set_new(evt, "planet_id", json_integer(planet_id));
      json_object_set_new(evt, "sanctioned", json_true());
      db_log_engine_event((long long)time(NULL), "player.terra_attack_sanction.v1", "player", ctx->player_id, current_sector, evt, NULL);

      send_response_error(ctx, root, 403, "You have attacked Terra! Federation forces have destroyed your ship and seized your assets.");
      return 0;
  }

  // 5. Get Attacker Ship Info
  int s_fighters = 0;
  if (sqlite3_prepare_v2(db, "SELECT fighters FROM ships WHERE id=?1", -1, &pst, NULL) == SQLITE_OK) {
      sqlite3_bind_int(pst, 1, ship_id);
      if (sqlite3_step(pst) == SQLITE_ROW) s_fighters = sqlite3_column_int(pst, 0);
      sqlite3_finalize(pst);
  }

  if (s_fighters <= 0) {
      send_response_error(ctx, root, ERR_BAD_REQUEST, "You have no fighters to attack with.");
      return 0;
  }

  // 6. Citadel Defence Hooks (Shields + CCC)
  int cit_level = 0;
  int cit_shields = 0;
  int cit_reaction = 0;
  
  if (sqlite3_prepare_v2(db, "SELECT level, planetaryShields, militaryReactionLevel FROM citadels WHERE planet_id=?1", -1, &pst, NULL) == SQLITE_OK) {
      sqlite3_bind_int(pst, 1, planet_id);
      if (sqlite3_step(pst) == SQLITE_ROW) {
          cit_level = sqlite3_column_int(pst, 0);
          cit_shields = sqlite3_column_int(pst, 1);
          cit_reaction = sqlite3_column_int(pst, 2);
      }
      sqlite3_finalize(pst);
  }

  int fighters_absorbed = 0;

  // 6.1 Shields (Level >= 5)
  if (cit_level >= 5 && cit_shields > 0) {
      int absorbed = (s_fighters < cit_shields) ? s_fighters : cit_shields;
      s_fighters -= absorbed;
      fighters_absorbed = absorbed;
      int new_shields = cit_shields - absorbed;
      
      // Persist shield damage immediately
      sqlite3_stmt *upd = NULL;
      if (sqlite3_prepare_v2(db, "UPDATE citadels SET planetaryShields=?1 WHERE planet_id=?2", -1, &upd, NULL) == SQLITE_OK) {
          sqlite3_bind_int(upd, 1, new_shields);
          sqlite3_bind_int(upd, 2, planet_id);
          sqlite3_step(upd);
          sqlite3_finalize(upd);
      }
      
      // If all fighters absorbed, attack fails immediately (but logic continues with 0 fighters likely resulting in loss)
  }

  // 6.2 CCC / Military Reaction (Level >= 2)
  int effective_p_fighters = p_fighters;
  if (cit_level >= 2 && cit_reaction > 0) {
      int pct = 100;
      if (cit_reaction == 1) pct = 125;
      else if (cit_reaction >= 2) pct = 150;
      
      effective_p_fighters = (int)floor((double)p_fighters * (double)pct / 100.0);
  }

  // 7. Combat Resolution
  bool attacker_wins = (s_fighters > effective_p_fighters);
  int ship_loss = 0;
  int planet_loss = 0;
  bool captured = false;

  if (attacker_wins) {
      ship_loss = effective_p_fighters; // Attacker loses fighters equal to effective defense
      planet_loss = p_fighters;         // Planet loses all real fighters
      captured = true;
  } else {
      ship_loss = s_fighters;           // Attacker loses all remaining fighters
      planet_loss = s_fighters;         // Planet loses fighters equal to attacker strength
      captured = false;
  }
  
  ship_loss += fighters_absorbed;

  char *err = NULL;
  sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, NULL);

  sqlite3_stmt *ust = NULL;
  sqlite3_prepare_v2(db, "UPDATE ships SET fighters = fighters - ?1 WHERE id = ?2", -1, &ust, NULL);
  sqlite3_bind_int(ust, 1, ship_loss);
  sqlite3_bind_int(ust, 2, ship_id);
  sqlite3_step(ust);
  sqlite3_finalize(ust);

  if (captured) {
      int corp_id = ctx->corp_id;
      const char *new_type = (corp_id > 0) ? "corporation" : "player";
      int new_owner = (corp_id > 0) ? corp_id : ctx->player_id;

      sqlite3_prepare_v2(db, "UPDATE planets SET fighters=0, owner_id=?1, owner_type=?2 WHERE id=?3", -1, &ust, NULL);
      sqlite3_bind_int(ust, 1, new_owner);
      sqlite3_bind_text(ust, 2, new_type, -1, SQLITE_STATIC);
      sqlite3_bind_int(ust, 3, planet_id);
      sqlite3_step(ust);
      sqlite3_finalize(ust);
      
      json_t *cap_evt = json_object();
      json_object_set_new(cap_evt, "planet_id", json_integer(planet_id));
      json_object_set_new(cap_evt, "previous_owner", json_integer(p_owner_id));
      db_log_engine_event((long long)time(NULL), "player.capture_planet.v1", "player", ctx->player_id, current_sector, cap_evt, NULL);

  } else {
      sqlite3_prepare_v2(db, "UPDATE planets SET fighters = fighters - ?1 WHERE id = ?2", -1, &ust, NULL);
      sqlite3_bind_int(ust, 1, planet_loss);
      sqlite3_bind_int(ust, 2, planet_id);
      sqlite3_step(ust);
      sqlite3_finalize(ust);
  }

  sqlite3_exec(db, "COMMIT", NULL, NULL, &err);

  json_t *atk_evt = json_object();
  json_object_set_new(atk_evt, "planet_id", json_integer(planet_id));
  json_object_set_new(atk_evt, "result", json_string(attacker_wins ? "win" : "loss"));
  json_object_set_new(atk_evt, "ship_loss", json_integer(ship_loss));
  json_object_set_new(atk_evt, "planet_loss", json_integer(planet_loss));
  db_log_engine_event((long long)time(NULL), "player.attack_planet.v1", "player", ctx->player_id, current_sector, atk_evt, NULL);

  json_t *res = json_object();
  json_object_set_new(res, "planet_id", json_integer(planet_id));
  json_object_set_new(res, "attacker_remaining_fighters", json_integer(s_fighters - ship_loss));
  json_object_set_new(res, "defender_remaining_fighters", json_integer(p_fighters - planet_loss));
  json_object_set_new(res, "captured", json_boolean(captured));
  
  send_response_ok(ctx, root, "combat.attack_planet", res);
  return 0;
}
