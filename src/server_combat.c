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
/* Forward decls from your codebase */
json_t *db_get_stardock_sectors (void);
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
/* typedef enum { */
/*   ASSET_MINE         = 1,   /\* Armid *\/ */
/*   ASSET_FIGHTER      = 2, */
/*   ASSET_BEACON       = 3, */
/*   ASSET_LIMPET_MINE  = 4 */
/* } asset_type_t; */
/* typedef enum { */
/*   OFFENSE_TOLL   = 1, */
/*   OFFENSE_DEFEND = 2, */
/*   OFFENSE_ATTACK = 3 */
/* } offense_type_t; */
/* --- common helpers --- */
static inline int
require_auth (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id > 0)
    {
      return 1;
    }
  send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
  return 0;
}


static inline int
niy (client_ctx_t *ctx, json_t *root, const char *which)
{
  (void) which;
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented");
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


/*
 * Returns true if the mine stack is hostile to the given ship context,
 * false otherwise (friendly or neutral).
 */
bool
armid_stack_is_hostile (const sector_asset_t *mine_asset,
                        int ship_player_id, int ship_corp_id)
{
  /* Friendly if same player */
  if (mine_asset->player == ship_player_id)
    {
      return false;
    }
  /* Friendly if same non-zero corporation */
  if (mine_asset->corporation != 0 && mine_asset->corporation == ship_corp_id)
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
  // --- Process Armid Mines ---
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
          // int offensive_setting = sqlite3_column_int (st, 2); // Keep for logging if needed
          sector_asset_t mine_asset_row = {
            .id = mine_id,
            .quantity = mine_quantity,
            .player = sqlite3_column_int (st, 3),
            .corporation = sqlite3_column_int (st, 4),
            .ttl = sqlite3_column_int (st, 5)
          };
          // Get current time for TTL check
          time_t current_time = time (NULL);
          // Skip if mine is not active
          if (!armid_stack_is_active (&mine_asset_row, current_time))
            {
              continue;
            }
          // Skip if mine is not hostile
          if (!armid_stack_is_hostile
                (&mine_asset_row, ship_player_id, ship_corp_id))
            {
              continue;
            }
          // If ship->hull <= 0, stop processing further stacks.
          if (ship_stats.hull <= 0)
            {
              if (out_enc)
                {
                  out_enc->destroyed = true;
                }
              break;
            }
          // Compute trigger probability
          double p_base = g_armid_config.armid.base_trigger_chance;
          double p_max = g_armid_config.armid.max_trigger_chance;
          double p_trigger =
            1.0 - pow (1.0 - p_base, mine_asset_row.quantity);
          if (p_trigger > p_max)
            {
              p_trigger = p_max;
            }
          // Roll for trigger
          if (rand01 () > p_trigger)
            {
              continue;         // This stack doesn’t fire
            }
          // If it fires:
          double f_min = g_armid_config.armid.min_fraction_exploded;
          double f_max = g_armid_config.armid.max_fraction_exploded;
          double f = rand_range (f_min, f_max);
          f = clamp (f, 0.0, 1.0);
          int exploded = (int) ceil (mine_asset_row.quantity * f);
          exploded = clamp (exploded, 1, mine_asset_row.quantity);
          int damage = exploded * g_armid_config.armid.damage_per_mine;
          armid_damage_breakdown_t d = { 0 };
          apply_armid_damage_to_ship (&ship_stats, damage, &d);
          // Update DB:
          int remaining = mine_asset_row.quantity - exploded;
          if (remaining > 0)
            {
              sqlite3_stmt *update_st = NULL;
              const char *sql_update_mine_qty =
                "UPDATE sector_assets SET quantity = ?1 WHERE id = ?2;";
              if (sqlite3_prepare_v2
                    (db, sql_update_mine_qty, -1, &update_st,
                    NULL) != SQLITE_OK)
                {
                  LOGE
                    ("Failed to prepare mine quantity update statement: %s",
                    sqlite3_errmsg (db));
                  sqlite3_finalize (st);
                  return -1;
                }
              sqlite3_bind_int (update_st, 1, remaining);
              sqlite3_bind_int (update_st, 2, mine_id);
              if (sqlite3_step (update_st) != SQLITE_DONE)
                {
                  LOGE ("Failed to update mine quantity: %s",
                        sqlite3_errmsg (db));
                }
              sqlite3_finalize (update_st);
            }
          else
            {
              sqlite3_stmt *delete_st = NULL;
              const char *sql_delete_mine =
                "DELETE FROM sector_assets WHERE id = ?1;";
              if (sqlite3_prepare_v2
                    (db, sql_delete_mine, -1, &delete_st, NULL) != SQLITE_OK)
                {
                  LOGE ("Failed to prepare mine delete statement: %s",
                        sqlite3_errmsg (db));
                  sqlite3_finalize (st);
                  return -1;
                }
              sqlite3_bind_int (delete_st, 1, mine_id);
              if (sqlite3_step (delete_st) != SQLITE_DONE)
                {
                  LOGE ("Failed to delete mine: %s", sqlite3_errmsg (db));
                }
              sqlite3_finalize (delete_st);
            }
          // Accumulate into armid_encounter_t:
          if (out_enc)
            {
              out_enc->armid_triggered += exploded;
              out_enc->armid_remaining += MAX (remaining, 0);
              out_enc->shields_lost += d.shields_lost;
              out_enc->fighters_lost += d.fighters_lost;
              out_enc->hull_lost += d.hull_lost;
            }
          // Emit combat.hit async event
          json_t *hit_data = json_object ();
          json_object_set_new (hit_data, "v", json_integer (1));
          json_object_set_new (hit_data, "attacker_id",
                               json_integer (mine_asset_row.player));                           // Mine owner
          json_object_set_new (hit_data, "defender_id",
                               json_integer (ctx->player_id));                          // Ship owner
          json_object_set_new (hit_data, "weapon",
                               json_string ("armid_mines"));
          json_object_set_new (hit_data, "sector_id",
                               json_integer (new_sector_id));
          json_object_set_new (hit_data, "damage_total",
                               json_integer (damage));
          json_t *breakdown_obj = json_object ();
          json_object_set_new (breakdown_obj, "shields_lost",
                               json_integer (d.shields_lost));
          json_object_set_new (breakdown_obj, "fighters_lost",
                               json_integer (d.fighters_lost));
          json_object_set_new (breakdown_obj, "hull_lost",
                               json_integer (d.hull_lost));
          json_object_set_new (hit_data, "breakdown", breakdown_obj);
          json_object_set_new (hit_data, "destroyed",
                               json_boolean (ship_stats.hull <= 0));
          (void) db_log_engine_event ((long long) time (NULL),
                                      "combat.hit", NULL,
                                      mine_asset_row.player, new_sector_id,
                                      hit_data, NULL);
          // If ship is destroyed, handle it
          if (ship_stats.hull <= 0)
            {
              // Call existing ship destruction flow:
              destroy_ship_and_handle_side_effects (ctx, new_sector_id,
                                                    ctx->player_id);
              if (out_enc)
                {
                  out_enc->destroyed = true;
                }
              LOGI ("Ship %d destroyed by Armid mine %d in sector %d",
                    ship_id, mine_id, new_sector_id);
              break;            // Stop processing further stacks
            }
        }
      sqlite3_finalize (st);
    }                           // End Armid mine processing
  // --- Process Limpet Mines ---
  if (g_cfg.mines.limpet.enabled)
    {
      sqlite3_stmt *st = NULL;
      const char *sql_select_limpet_mines =
        "SELECT id, quantity, offensive_setting, player, corporation, ttl "
        "FROM sector_assets " "WHERE sector = ?1 AND asset_type = 4;";                                                                                                          // asset_type 4 for Limpet Mines
      if (sqlite3_prepare_v2 (db, sql_select_limpet_mines, -1, &st, NULL) !=
          SQLITE_OK)
        {
          LOGE ("Failed to prepare Limpet mine selection statement: %s",
                sqlite3_errmsg (db));
          return -1;            // Indicate error
        }
      sqlite3_bind_int (st, 1, new_sector_id);
      while (sqlite3_step (st) == SQLITE_ROW)
        {
          int mine_id = sqlite3_column_int (st, 0);
          int mine_quantity = sqlite3_column_int (st, 1);
          // int offensive_setting = sqlite3_column_int (st, 2); // Keep for logging if needed
          sector_asset_t mine_asset_row = {
            .id = mine_id,
            .quantity = mine_quantity,
            .player = sqlite3_column_int (st, 3),
            .corporation = sqlite3_column_int (st, 4),
            .ttl = sqlite3_column_int (st, 5)
          };
          // Get current time for TTL check
          time_t current_time = time (NULL);
          // Skip if mine is not active
          if (!armid_stack_is_active (&mine_asset_row, current_time))
            {
              continue;
            }
          // Limpets are always triggered, no hostility check needed.
          // Limpets are triggered at a fixed rate (e.g., 1 limpet per 10 fighters).
          // Limpets apply a fixed amount of damage (e.g., 1 damage per limpet).
          // Limpets are removed after triggering.
          // Limpets do *not* destroy the ship. They only apply damage.
          int limpets_to_trigger = 0;
          // Example: 1 limpet per 10 fighters, up to mine_quantity
          // This logic needs to be defined in g_cfg.mines.limpet
          // For now, let's assume a simple trigger: all limpets in the stack trigger.
          // Or, a fraction based on ship fighters.
          // Let's use a simple fixed trigger rate for now, e.g., 1 limpet per X ship fighters
          // Or, a fixed percentage of the stack.
          // For simplicity, let's say a fixed number of limpets trigger per ship fighter
          // This needs to be configurable in g_cfg.mines.limpet
          // For now, let's assume a fixed trigger rate of 1:1 with ship fighters, capped by mine_quantity
          limpets_to_trigger =
            MIN (ship_stats.fighters *
                 g_cfg.mines.limpet.entry_trigger_rate_limpets_per_fighter,
                 mine_quantity);
          if (limpets_to_trigger < 0)
            {
              limpets_to_trigger = 0;
            }
          if (limpets_to_trigger == 0)
            {
              continue;         // No limpets triggered from this stack
            }
          int damage =
            limpets_to_trigger * g_cfg.mines.limpet.entry_damage_per_limpet;
          armid_damage_breakdown_t d = { 0 };   // Re-using for Limpet damage breakdown
          apply_armid_damage_to_ship (&ship_stats, damage, &d); // Apply damage to ship
          // Update DB:
          int remaining = mine_asset_row.quantity - limpets_to_trigger;
          if (remaining > 0)
            {
              sqlite3_stmt *update_st = NULL;
              const char *sql_update_mine_qty =
                "UPDATE sector_assets SET quantity = ?1 WHERE id = ?2;";
              if (sqlite3_prepare_v2
                    (db, sql_update_mine_qty, -1, &update_st,
                    NULL) != SQLITE_OK)
                {
                  LOGE
                    ("Failed to prepare Limpet quantity update statement: %s",
                    sqlite3_errmsg (db));
                  sqlite3_finalize (st);
                  return -1;
                }
              sqlite3_bind_int (update_st, 1, remaining);
              sqlite3_bind_int (update_st, 2, mine_id);
              if (sqlite3_step (update_st) != SQLITE_DONE)
                {
                  LOGE ("Failed to update Limpet quantity: %s",
                        sqlite3_errmsg (db));
                }
              sqlite3_finalize (update_st);
            }
          else
            {
              sqlite3_stmt *delete_st = NULL;
              const char *sql_delete_mine =
                "DELETE FROM sector_assets WHERE id = ?1;";
              if (sqlite3_prepare_v2
                    (db, sql_delete_mine, -1, &delete_st, NULL) != SQLITE_OK)
                {
                  LOGE ("Failed to prepare Limpet delete statement: %s",
                        sqlite3_errmsg (db));
                  sqlite3_finalize (st);
                  return -1;
                }
              sqlite3_bind_int (delete_st, 1, mine_id);
              if (sqlite3_step (delete_st) != SQLITE_DONE)
                {
                  LOGE ("Failed to delete Limpet: %s", sqlite3_errmsg (db));
                }
              sqlite3_finalize (delete_st);
            }
          // Accumulate into armid_encounter_t:
          if (out_enc)
            {
              out_enc->limpet_triggered += limpets_to_trigger;
              out_enc->limpet_remaining += MAX (remaining, 0);
              out_enc->shields_lost += d.shields_lost;
              out_enc->fighters_lost += d.fighters_lost;
              out_enc->hull_lost += d.hull_lost;
            }
          // Emit combat.hit async event for Limpets
          json_t *hit_data = json_object ();
          json_object_set_new (hit_data, "v", json_integer (1));
          json_object_set_new (hit_data, "attacker_id",
                               json_integer (mine_asset_row.player));                           // Mine owner
          json_object_set_new (hit_data, "defender_id",
                               json_integer (ctx->player_id));                          // Ship owner
          json_object_set_new (hit_data, "weapon",
                               json_string ("limpet_mines"));
          json_object_set_new (hit_data, "sector_id",
                               json_integer (new_sector_id));
          json_object_set_new (hit_data, "damage_total",
                               json_integer (damage));
          json_t *breakdown_obj = json_object ();
          json_object_set_new (breakdown_obj, "shields_lost",
                               json_integer (d.shields_lost));
          json_object_set_new (breakdown_obj, "fighters_lost",
                               json_integer (d.fighters_lost));
          json_object_set_new (breakdown_obj, "hull_lost",
                               json_integer (d.hull_lost));
          json_object_set_new (hit_data, "breakdown", breakdown_obj);
          json_object_set_new (hit_data, "destroyed", json_boolean (false));    // Limpets do not destroy ship
          (void) db_log_engine_event ((long long) time (NULL),
                                      "combat.hit", NULL,
                                      mine_asset_row.player, new_sector_id,
                                      hit_data, NULL);
          // Limpets do not destroy the ship, so no destroy_ship_and_handle_side_effects call here.
          // If ship_stats.hull <= 0, it means Armid mines already destroyed it, or it was already destroyed.
          // We continue processing Limpets to remove them, but don't re-destroy the ship.
        }
      sqlite3_finalize (st);
    }                           // End Limpet mine processing
  // Final check for ship destruction after all mine types
  if (ship_stats.hull <= 0 && !out_enc->destroyed)
    {
      // This case should ideally be handled by Armid mines if they destroy the ship.
      // But as a safeguard, if hull is 0 and out_enc->destroyed wasn't set, set it.
      destroy_ship_and_handle_side_effects (ctx, new_sector_id,
                                            ctx->player_id);
      if (out_enc)
        {
          out_enc->destroyed = true;
        }
      LOGI ("Ship %d destroyed by mine encounter in sector %d", ship_id,
            new_sector_id);
    }
  return 0;                     // Success
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
      send_enveloped_error (ctx->fd, root, 500,
                            "Database handle not available.");
      return 0;
    }
  int self_player_id = ctx->player_id;
  // --- 2. Prepare SQL Statement ---
  sqlite3_stmt *st = NULL;
  pthread_mutex_lock (&db_mutex);
  int rc = sqlite3_prepare_v2 (db, sql_query, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      pthread_mutex_unlock (&db_mutex);
      send_enveloped_error (ctx->fd, root, 500, sqlite3_errmsg (db));
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
  pthread_mutex_unlock (&db_mutex);
  if (rc != SQLITE_DONE)
    {
      json_decref (entries);    // Clean up on error
      send_enveloped_error (ctx->fd, root, 500,
                            "Error processing asset list.");
      return 0;
    }
  // --- 6. Build Final Payload and Send Response ---
  json_t *jdata_payload = json_object ();
  json_object_set_new (jdata_payload, "total", json_integer (total_count));
  json_object_set_new (jdata_payload, "entries", entries);      // Add the array
  send_enveloped_ok (ctx->fd, root, list_type, jdata_payload);
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
  sqlite3 *db_handle = db_get_handle ();
  // Attempting to flee reveals the ship
  h_decloak_ship (db_handle,
                  h_get_active_ship_id (db_handle, (ctx->player_id)));
  TurnConsumeResult tc =
    h_consume_player_turn (db_handle, ctx, "combat.flee");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "combat.flee", root,
                                            NULL);
    }
  // --- COMBAT FLEE LOGIC GOES HERE ---
  // 2. Determine success chance based on ship speed, opponent status, etc.
  // 3. If successful, potentially move the ship one warp or clear the combat status flag.
  // 4. If unsuccessful, opponent might get a free attack or the ship remains in combat.
  // 5. Send successful ACK/status to client
  return 0;                     // Success
}


/* ---------- combat.attack ---------- */
int
cmd_combat_attack (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, ERR_SERVICE_UNAVAILABLE,
                            "Database unavailable");
      return 0;
    }
  // All combat actions reveal the ship
  h_decloak_ship (db, h_get_active_ship_id (db, ctx->player_id));
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, "combat.attack");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "combat.attack", root,
                                            NULL);
    }
  /* 1. Input Parsing */
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
                            "Missing required field: data");
      return 0;
    }
  json_t *j_mine_type_str = json_object_get (data, "mine_type");
  json_t *j_asset_id = json_object_get (data, "asset_id");
  json_t *j_fighters_committed = json_object_get (data, "fighters_committed");
  if (!j_mine_type_str || !json_is_string (j_mine_type_str) ||
      !j_asset_id || !json_is_integer (j_asset_id) ||
      !j_fighters_committed || !json_is_integer (j_fighters_committed))
    {
      send_enveloped_error (ctx->fd,
                            root,
                            ERR_CURSOR_INVALID,
                            "Missing required field or invalid type: mine_type/asset_id/fighters_committed");
      return 0;
    }
  const char *mine_type_name = json_string_value (j_mine_type_str);
  int asset_id = (int) json_integer_value (j_asset_id);
  int fighters_committed = (int) json_integer_value (j_fighters_committed);
  // Only Limpet mines can be attacked via this command
  if (strcasecmp (mine_type_name, "limpet") != 0)
    {
      send_enveloped_error (ctx->fd,
                            root,
                            ERR_INVALID_ARG,
                            "Only 'limpet' mine_type can be attacked via this command.");
      return 0;
    }
  if (fighters_committed <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
                            "Fighters committed must be > 0.");
      return 0;
    }
  /* 2. Feature Gates */
  if (!g_cfg.mines.limpet.enabled)
    {
      send_enveloped_refused (ctx->fd, root, ERR_LIMPETS_DISABLED,
                              "Limpet mine operations are disabled.", NULL);
      return 0;
    }
  if (!g_cfg.mines.limpet.attack_enabled)
    {
      send_enveloped_refused (ctx->fd, root, ERR_LIMPET_ATTACK_DISABLED,
                              "Limpet mine attacking is disabled.", NULL);
      return 0;
    }
  /* 3. Target Validation */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_SHIP_NOT_FOUND,
                            "No active ship.");
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to prepare player ship query.");
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
  if (player_current_sector_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
                            "Could not determine player's current sector.");
      return 0;
    }
  if (fighters_committed > ship_fighters_current)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
                            "Not enough fighters on ship to commit.");
      return 0;
    }
  // Fetch target Limpet mine stack details
  sqlite3_stmt *stmt_asset = NULL;
  const char *sql_asset =
    "SELECT quantity, player, corporation, offensive_setting, sector, ttl "
    "FROM sector_assets " "WHERE id = ?1 AND asset_type = ?2;";                                                                                                 // asset_type = 4 for Limpet Mines
  if (sqlite3_prepare_v2 (db, sql_asset, -1, &stmt_asset, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to prepare asset query.");
      return 0;
    }
  sqlite3_bind_int (stmt_asset, 1, asset_id);
  sqlite3_bind_int (stmt_asset, 2, ASSET_LIMPET_MINE);
  int asset_qty = 0;
  int asset_owner_player_id = 0;
  int asset_owner_corp_id = 0;
  int asset_sector_id = 0;
  time_t asset_ttl = 0;
  if (sqlite3_step (stmt_asset) == SQLITE_ROW)
    {
      asset_qty = sqlite3_column_int (stmt_asset, 0);
      asset_owner_player_id = sqlite3_column_int (stmt_asset, 1);
      asset_owner_corp_id = sqlite3_column_int (stmt_asset, 2);
      asset_sector_id = sqlite3_column_int (stmt_asset, 4);
      asset_ttl = sqlite3_column_int (stmt_asset, 5);
    }
  sqlite3_finalize (stmt_asset);
  if (asset_qty <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_TARGET_INVALID,
                            "Limpet mine stack not found or already destroyed.");
      return 0;
    }
  if (asset_sector_id != player_current_sector_id)
    {
      send_enveloped_error (ctx->fd, root, ERR_TARGET_INVALID,
                            "Limpet mine stack is not in your current sector.");
      return 0;
    }
  // Check if active
  sector_asset_t mine_asset_for_check = {
    .quantity = asset_qty,
    .player = asset_owner_player_id,
    .corporation = asset_owner_corp_id,
    .ttl = asset_ttl
  };
  if (!armid_stack_is_active (&mine_asset_for_check, time (NULL)))
    {
      send_enveloped_error (ctx->fd,
                            root,
                            ERR_TARGET_INVALID,
                            "Limpet mine stack is inactive (expired or quantity zero).");
      return 0;
    }
  // Check hostility (Limpets are only attacked if hostile)
  if (!armid_stack_is_hostile
        (&mine_asset_for_check, ctx->player_id, ship_corp_id))
    {
      send_enveloped_refused (ctx->fd, root, REF_FRIENDLY_FIRE_BLOCKED,
                              "Cannot attack friendly or neutral Limpet mines.",
                              NULL);
      return 0;
    }
  /* 4. Attack Logic */
  int limpets_destroyed = 0;
  int fighters_lost = 0;
  // Limpets destroyed: 1 fighter destroys 1 limpet (configurable)
  limpets_destroyed =
    fighters_committed * g_cfg.mines.limpet.attack_rate_limpets_per_fighter;
  if (limpets_destroyed > asset_qty)
    {
      limpets_destroyed = asset_qty;
    }
  if (limpets_destroyed < 0)
    {
      limpets_destroyed = 0;
    }
  // Fighters lost: 1 fighter lost per X limpets destroyed (configurable)
  fighters_lost =
    (int) ceil ((double) limpets_destroyed /
                g_cfg.mines.limpet.attack_rate_limpets_per_fighter_loss);
  if (fighters_lost > fighters_committed)
    {
      fighters_lost = fighters_committed;
    }
  if (fighters_lost < 0)
    {
      fighters_lost = 0;
    }
  if (limpets_destroyed == 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_TARGET_INVALID,
                              "No Limpet mines were destroyed.", NULL);
      return 0;
    }
  /* Transaction for updates */
  char *errmsg = NULL;
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Could not start transaction");
      return 0;
    }
  // Update sector_assets
  int new_asset_qty = asset_qty - limpets_destroyed;
  if (new_asset_qty > 0)
    {
      sqlite3_stmt *update_st = NULL;
      const char *sql_update_mine_qty =
        "UPDATE sector_assets SET quantity = ?1 WHERE id = ?2;";
      if (sqlite3_prepare_v2 (db, sql_update_mine_qty, -1, &update_st, NULL)
          != SQLITE_OK)
        {
          LOGE
          (
            "Failed to prepare mine quantity update statement during attack: %s",
            sqlite3_errmsg (db));
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to update mine quantity.");
          return 0;
        }
      sqlite3_bind_int (update_st, 1, new_asset_qty);
      sqlite3_bind_int (update_st, 2, asset_id);
      if (sqlite3_step (update_st) != SQLITE_DONE)
        {
          LOGE ("Failed to update mine quantity during attack: %s",
                sqlite3_errmsg (db));
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to update mine quantity.");
          return 0;
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
          LOGE ("Failed to prepare mine delete statement during attack: %s",
                sqlite3_errmsg (db));
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to delete mine.");
          return 0;
        }
      sqlite3_bind_int (delete_st, 1, asset_id);
      if (sqlite3_step (delete_st) != SQLITE_DONE)
        {
          LOGE ("Failed to delete mine during attack: %s",
                sqlite3_errmsg (db));
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to delete mine.");
          return 0;
        }
      sqlite3_finalize (delete_st);
    }
  // Update ship fighters
  sqlite3_stmt *update_ship_st = NULL;
  const char *sql_update_ship_fighters =
    "UPDATE ships SET fighters = fighters - ?1 WHERE id = ?2;";
  if (sqlite3_prepare_v2
        (db, sql_update_ship_fighters, -1, &update_ship_st, NULL) != SQLITE_OK)
    {
      LOGE
        ("Failed to prepare ship fighters update statement during attack: %s",
        sqlite3_errmsg (db));
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to update ship fighters.");
      return 0;
    }
  sqlite3_bind_int (update_ship_st, 1, fighters_lost);
  sqlite3_bind_int (update_ship_st, 2, ship_id);
  if (sqlite3_step (update_ship_st) != SQLITE_DONE)
    {
      LOGE ("Failed to update ship fighters during attack: %s",
            sqlite3_errmsg (db));
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to update ship fighters.");
      return 0;
    }
  sqlite3_finalize (update_ship_st);
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
      return 0;
    }
  /* 5. Response */
  json_t *out = json_object ();
  json_object_set_new (out, "sector_id",
                       json_integer (player_current_sector_id));
  json_object_set_new (out, "asset_id", json_integer (asset_id));
  json_object_set_new (out, "mine_type", json_string ("limpet"));
  json_object_set_new (out, "limpets_destroyed",
                       json_integer (limpets_destroyed));
  json_object_set_new (out, "fighters_lost", json_integer (fighters_lost));
  json_object_set_new (out, "limpets_remaining",
                       json_integer (new_asset_qty > 0 ? new_asset_qty : 0));
  json_object_set_new (out, "success", json_boolean (limpets_destroyed > 0));
  send_enveloped_ok (ctx->fd, root, "combat.mines_attacked_v1", out);
  // Optional broadcast:
  if (limpets_destroyed > 0)
    {
      json_t *broadcast_data = json_object ();
      json_object_set_new (broadcast_data, "v", json_integer (1));
      json_object_set_new (broadcast_data, "attacker_id",
                           json_integer (ctx->player_id));
      json_object_set_new (broadcast_data, "target_asset_id",
                           json_integer (asset_id));
      json_object_set_new (broadcast_data, "target_owner_id",
                           json_integer (asset_owner_player_id));
      json_object_set_new (broadcast_data, "sector_id",
                           json_integer (player_current_sector_id));
      json_object_set_new (broadcast_data, "mine_type",
                           json_string ("limpet"));
      json_object_set_new (broadcast_data, "limpets_destroyed",
                           json_integer (limpets_destroyed));
      json_object_set_new (broadcast_data, "fighters_lost",
                           json_integer (fighters_lost));
      (void) db_log_engine_event ((long long) time (NULL),
                                  "combat.mines_attacked", NULL,
                                  ctx->player_id, player_current_sector_id,
                                  broadcast_data, NULL);
    }
  return 0;
}


/* ---------- combat.deploy_fighters ---------- */
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
      send_enveloped_error (ctx->fd, root, ERR_SERVICE_UNAVAILABLE,
                            "Database unavailable");
      return 0;
    }
  bool in_fed = false;
  bool in_sdock = false;
  /* Parse input */
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
                            "Missing required field: data");
      return 0;
    }
  json_t *j_amount = json_object_get (data, "amount");
  json_t *j_offense = json_object_get (data, "offense");        /* required: 1..3 */
  json_t *j_corp_id = json_object_get (data, "corporation_id"); /* optional, nullable */
  if (!j_amount || !json_is_integer (j_amount) ||
      !j_offense || !json_is_integer (j_offense))
    {
      send_enveloped_error (ctx->fd,
                            root,
                            ERR_CURSOR_INVALID,
                            "Missing required field or invalid type: amount/offense");
      return 0;
    }
  int amount = (int) json_integer_value (j_amount);
  int offense = (int) json_integer_value (j_offense);
  if (amount <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_FOUND,
                            "amount must be > 0");
      return 0;
    }
  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK)
    {
      send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
                            "offense must be one of {1=TOLL,2=DEFEND,3=ATTACK}");
      return 0;
    }
  /* Resolve active ship + sector */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_SHIP_NOT_FOUND,
                            "No active ship");
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
        send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND, shperror);
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
        send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND, scterror);
        return 0;
      }
  }
  /* Sector cap */
  int sector_total = 0;
  if (sum_sector_fighters (db, sector_id, &sector_total) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, REF_NOT_IN_SECTOR,
                            "Failed to read sector fighters");
      return 0;
    }
  if (sector_total + amount > SECTOR_FIGHTER_CAP)
    {
      send_enveloped_error (ctx->fd, root, ERR_SECTOR_OVERCROWDED,
                            "Sector fighter limit exceeded (50,000)");
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Could not start transaction");
      return 0;
    }
  int rc = ship_consume_fighters (db, ship_id, amount);
  if (rc == SQLITE_TOOBIG)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
                            "Insufficient fighters on ship");
      return 0;
    }
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
                            "Failed to update ship fighters");
      return 0;
    }
  rc =
    insert_sector_fighters (db, sector_id, ctx->player_id, j_corp_id, offense,
                            amount);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, SECTOR_ERR,
                            "Failed to create sector assets record");
      return 0;
    }
  int asset_id = (int) sqlite3_last_insert_rowid (db);  // Capture the newly created asset_id
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
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
  send_enveloped_ok (ctx->fd, root, "combat.fighters.deployed", out);
  json_decref (out);
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
      fprintf (stderr,
               "ERROR: Failed to allocate JSON array for stardock sectors.\n");
      return NULL;
    }
  const char *sql = "SELECT sector_id FROM stardock_location;";
  // 1. Prepare the SQL statement
  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "DB Error: Could not prepare stardock query: %s\n",
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
          fprintf (stderr, "ERROR: Failed to append sector ID %d to list.\n",
                   sector_id);
          json_decref (j_sector);       // Clean up the orphaned reference
          // You may choose to stop here or continue
        }
    }
  // 3. Handle step errors if the loop didn't finish normally (SQLITE_DONE)
  if (rc != SQLITE_DONE)
    {
      fprintf (stderr, "DB Error: Failed to step stardock query: %s\n",
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
      send_enveloped_error (ctx->fd, root, ERR_SERVICE_UNAVAILABLE,
                            "Database unavailable");
      return 0;
    }
  // 1. Validation
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
                            "Missing required field: data");
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
      send_enveloped_error (ctx->fd,
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
      send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
                            "Invalid mine_type. Must be 'armid' or 'limpet'.");
      return 0;
    }
  if (fighters_committed <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
                            "Fighters committed must be greater than 0.");
      return 0;
    }
  // Limpet-specific feature gates and validation
  if (mine_type == ASSET_LIMPET_MINE)
    {
      if (!g_cfg.mines.limpet.enabled)
        {
          send_enveloped_refused (ctx->fd, root, ERR_LIMPETS_DISABLED,
                                  "Limpet mine operations are disabled.",
                                  NULL);
          return 0;
        }
      if (!g_cfg.mines.limpet.sweep_enabled)
        {
          send_enveloped_refused (ctx->fd, root, ERR_LIMPET_SWEEP_DISABLED,
                                  "Limpet mine sweeping is disabled.", NULL);
          return 0;
        }
      if (from_sector_id != target_sector_id)
        {
          send_enveloped_error (ctx->fd,
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
      send_enveloped_error (ctx->fd, root, ERR_SHIP_NOT_FOUND,
                            "No active ship found for player.");
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to prepare player ship query.");
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
      send_enveloped_refused (ctx->fd, root, REF_NOT_IN_SECTOR,
                              "Ship is not in the specified 'from_sector_id'.",
                              json_pack ("{s:s}", "reason",
                                         "not_in_from_sector"));
      return 0;
    }
  if (fighters_committed > ship_fighters_current)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
                            "Not enough fighters on ship to commit.");
      return 0;
    }
  // Check for warp existence (from_sector_id to target_sector_id)
  if (!h_warp_exists (db, from_sector_id, target_sector_id))
    {
      send_enveloped_error (ctx->fd, root, ERR_TARGET_INVALID,
                            "No warp exists between specified sectors.");
      return 0;
    }
  if (!g_armid_config.sweep.enabled)
    {
      send_enveloped_error (ctx->fd, root, ERR_CAPABILITY_DISABLED,
                            "Mine sweeping is currently disabled.");
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
            armid_stack_is_hostile (&mine_asset_for_check, ctx->player_id,
                                    ship_corp_id);
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
      send_enveloped_ok (ctx->fd, root, "combat.mines_swept_v1", out);
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Could not start transaction");
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
            sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
            json_decref (hostile_stacks_json);
            return -1;
          }
        sqlite3_bind_int (update_st, 1, new_qty);
        sqlite3_bind_int (update_st, 2, mine_stack_id);
        if (sqlite3_step (update_st) != SQLITE_DONE)
          {
            LOGE ("Failed to update mine quantity during sweep: %s",
                  sqlite3_errmsg (db));
            sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
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
            sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
            json_decref (hostile_stacks_json);
            return -1;
          }
        sqlite3_bind_int (delete_st, 1, mine_stack_id);
        if (sqlite3_step (delete_st) != SQLITE_DONE)
          {
            LOGE ("Failed to delete mine during sweep: %s",
                  sqlite3_errmsg (db));
            sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
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
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      json_decref (hostile_stacks_json);
      return -1;
    }
  sqlite3_bind_int (update_ship_st, 1, fighters_lost);
  sqlite3_bind_int (update_ship_st, 2, ship_id);
  if (sqlite3_step (update_ship_st) != SQLITE_DONE)
    {
      LOGE ("Failed to update ship fighters during sweep: %s",
            sqlite3_errmsg (db));
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
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
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
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
  send_enveloped_ok (ctx->fd, root, "combat.mines_swept_v1", out);
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


/* ---------- combat.status ---------- */
int
cmd_combat_status (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  // TODO: return sector combat snapshot (entities, mines, fighters, cooldowns)
  return niy (ctx, root, "combat.status");
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
      send_enveloped_error (ctx->fd, root, ERR_SERVICE_UNAVAILABLE,
                            "Database unavailable");
      return 0;
    }
  /* 1. Parse input */
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
                            "Missing required field: data");
      return 0;
    }
  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_asset_id = json_object_get (data, "asset_id");
  if (!j_sector_id || !json_is_integer (j_sector_id) ||
      !j_asset_id || !json_is_integer (j_asset_id))
    {
      send_enveloped_error (ctx->fd,
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
      send_enveloped_error (ctx->fd, root, ERR_SHIP_NOT_FOUND,
                            "No active ship found for player.");
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to prepare player ship query.");
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
      send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
                            "Could not determine player's current sector.");
      return 0;
    }
  /* 3. Validate sector match */
  if (player_current_sector_id != requested_sector_id)
    {
      send_enveloped_refused (ctx->fd, root, REF_NOT_IN_SECTOR,
                              "Not in sector", json_pack ("{s:s}", "reason",
                                                          "not_in_sector"));
      return 0;
    }
  /* 4. Fetch asset and validate existence */
  sqlite3_stmt *stmt_asset = NULL;
  const char *sql_asset =
    "SELECT quantity, player, corporation, offensive_setting "
    "FROM sector_assets " "WHERE id = ?1 AND sector = ?2 AND asset_type = 2;";                                                                                  // asset_type = 2 for fighters
  if (sqlite3_prepare_v2 (db, sql_asset, -1, &stmt_asset, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to prepare asset query.");
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
      send_enveloped_error (ctx->fd, root, ERR_SHIP_NOT_FOUND,
                            "Asset not found");
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
      send_enveloped_refused (ctx->fd, root, ERR_TARGET_INVALID, "Not owner",
                              json_pack ("{s:s}", "reason", "not_owner"));
      return 0;
    }
  /* 6. Compute pickup quantity */
  int available_to_recall = asset_qty;
  int capacity_left = ship_fighters_max - ship_fighters_current;
  int take = 0;
  if (capacity_left <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_OUT_OF_RANGE, "No capacity",
                              json_pack ("{s:s}", "reason", "no_capacity"));
      return 0;
    }
  take =
    (available_to_recall <
     capacity_left) ? available_to_recall : capacity_left;
  if (take <= 0)                // Now this check makes sense
    {
      send_enveloped_refused (ctx->fd, root, ERR_OUT_OF_RANGE,
                              "No fighters to recall or no capacity",
                              json_pack ("{s:s}", "reason",
                                         "no_fighters_or_capacity"));
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Could not start transaction");
      return 0;
    }
  /* Increment ship fighters */
  sqlite3_stmt *stmt_update_ship = NULL;
  const char *sql_update_ship =
    "UPDATE ships SET fighters = fighters + ?1 WHERE id = ?2;";
  if (sqlite3_prepare_v2 (db, sql_update_ship, -1, &stmt_update_ship, NULL) !=
      SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to prepare ship update.");
      return 0;
    }
  sqlite3_bind_int (stmt_update_ship, 1, take);
  sqlite3_bind_int (stmt_update_ship, 2, player_ship_id);
  if (sqlite3_step (stmt_update_ship) != SQLITE_DONE)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to update ship fighters.");
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
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to prepare asset delete.");
          return 0;
        }
      sqlite3_bind_int (stmt_delete_asset, 1, asset_id);
      if (sqlite3_step (stmt_delete_asset) != SQLITE_DONE)
        {
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to delete asset record.");
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
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to prepare asset update.");
          return 0;
        }
      sqlite3_bind_int (stmt_update_asset, 1, take);
      sqlite3_bind_int (stmt_update_asset, 2, asset_id);
      if (sqlite3_step (stmt_update_asset) != SQLITE_DONE)
        {
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to update asset quantity.");
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
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
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
  send_enveloped_ok (ctx->fd, root, "combat.fighters.deployed", out);
  json_decref (out);
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
      send_enveloped_error (ctx->fd, root, ERR_SERVICE_UNAVAILABLE,
                            "Database unavailable");
      return 0;
    }
  /* 1. Input Parsing */
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
                            "Missing required field: data");
      return 0;
    }
  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_mine_type_str = json_object_get (data, "mine_type");
  json_t *j_asset_id = json_object_get (data, "asset_id");      // Optional
  if (!j_sector_id || !json_is_integer (j_sector_id) ||
      !j_mine_type_str || !json_is_string (j_mine_type_str))
    {
      send_enveloped_error (ctx->fd,
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
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
                            "Only 'limpet' mine_type can be scrubbed.");
      return 0;
    }
  /* 2. Feature Gate */
  if (!g_cfg.mines.limpet.enabled)
    {
      send_enveloped_refused (ctx->fd, root, ERR_LIMPETS_DISABLED,
                              "Limpet mine operations are disabled.", NULL);
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
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to retrieve player credits.");
          return 0;
        }
      if (player_credits < scrub_cost)
        {
          send_enveloped_refused (ctx->fd, root, ERR_INSUFFICIENT_FUNDS,
                                  "Insufficient credits to scrub mines.",
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Could not start transaction");
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
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to prepare scrub operation.");
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
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to prepare scrub operation.");
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
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to execute scrub operation.");
      sqlite3_finalize (scrub_st);
      return 0;
    }
  total_scrubbed = sqlite3_changes (db);        // Number of rows deleted
  sqlite3_finalize (scrub_st);
  if (total_scrubbed == 0)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_refused (ctx->fd, root, ERR_NOT_FOUND,
                              "No Limpet mines found to scrub.", NULL);
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
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          send_enveloped_error (ctx->fd, root, ERR_DB,
                                "Failed to debit credits.");
          return 0;
        }
    }
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
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
  send_enveloped_ok (ctx->fd, root, "combat.mines_scrubbed_v1", out);
  json_decref (out);
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
      send_enveloped_error (ctx->fd, root, ERR_SERVICE_UNAVAILABLE,
                            "Database unavailable");
      return 0;
    }
  bool in_fed = false;
  bool in_sdock = false;
  /* Parse input */
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
                            "Missing required field: data");
      return 0;
    }
  json_t *j_amount = json_object_get (data, "amount");
  json_t *j_offense = json_object_get (data, "offense");        /* required: 1..3 */
  json_t *j_corp_id = json_object_get (data, "corporation_id"); /* optional, nullable */
  json_t *j_mine_type = json_object_get (data, "mine_type");    /* optional, 1 for Armid, 4 for Limpet */
  if (!j_amount || !json_is_integer (j_amount) ||
      !j_offense || !json_is_integer (j_offense))
    {
      send_enveloped_error (ctx->fd,
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
          send_enveloped_error (ctx->fd,
                                root,
                                ERR_CURSOR_INVALID,
                                "Invalid mine_type. Must be 1 (Armid) or 4 (Limpet).");
          return 0;
        }
    }
  if (amount <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_FOUND,
                            "amount must be > 0");
      return 0;
    }
  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK)
    {
      send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
                            "offense must be one of {1=TOLL,2=DEFEND,3=ATTACK}");
      return 0;
    }
  /* Resolve active ship + sector */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_SHIP_NOT_FOUND,
                            "No active ship");
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
        send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND, shperror);
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
        send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND, scterror);
        return 0;
      }
  }
  /* Sector cap */
  int sector_total = 0;
  if (sum_sector_mines (db, sector_id, &sector_total) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, REF_NOT_IN_SECTOR,
                            "Failed to read sector mines");
      return 0;
    }
  if (sector_total + amount > SECTOR_MINE_CAP)
    {                           // Assuming SECTOR_MINE_CAP is defined
      send_enveloped_error (ctx->fd, root, ERR_SECTOR_OVERCROWDED,
                            "Sector mine limit exceeded (50,000)");
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Could not start transaction");
      return 0;
    }
  int rc = ship_consume_mines (db, ship_id, mine_type, amount);
  if (rc == SQLITE_TOOBIG)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
                            "Insufficient mines on ship");
      return 0;
    }
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
                            "Failed to update ship mines");
      return 0;
    }
  rc =
    insert_sector_mines (db, sector_id, ctx->player_id, j_corp_id, mine_type,
                         offense, amount);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, SECTOR_ERR,
                            "Failed to create sector assets record");
      return 0;
    }
  int asset_id = (int) sqlite3_last_insert_rowid (db);  // Capture the newly created asset_id
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
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
  send_enveloped_ok (ctx->fd, root, "combat.mines.deployed", out);
  json_decref (out);
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
      send_enveloped_error (ctx->fd, root, ERR_SERVICE_UNAVAILABLE,
                            "Database unavailable");
      return 0;
    }
  bool in_fed = false;
  // bool in_sdock = false; // Not used currently in this function for mine deployment
  /* Parse input */
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
                            "Missing required field: data");
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
      send_enveloped_error (ctx->fd,
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
      send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
                            "Invalid mine_type. Must be 'armid' or 'limpet'.");
      return 0;
    }
  if (amount <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
                            "Count must be > 0");
      return 0;
    }
  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK)
    {
      send_enveloped_error (ctx->fd,
                            root,
                            ERR_CURSOR_INVALID,
                            "offense_mode must be one of {1=TOLL,2=DEFEND,3=ATTACK}");
      return 0;
    }
  if (strcasecmp (owner_mode_str, "personal") != 0
      && strcasecmp (owner_mode_str, "corp") != 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
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
          send_enveloped_error (ctx->fd,
                                root,
                                ERR_NOT_IN_CORP,
                                "Cannot deploy as corp: player not in a corporation.");
          return 0;
        }
    }
  /* Feature Gate for Limpet Mines */
  if (mine_type == ASSET_LIMPET_MINE && !g_cfg.mines.limpet.enabled)
    {
      send_enveloped_refused (ctx->fd, root, ERR_LIMPETS_DISABLED,
                              "Limpet mine deployment is disabled.", NULL);
      return 0;
    }
  /* Resolve active ship + sector */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_SHIP_NOT_FOUND,
                            "No active ship");
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
        send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND, shperror);
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
        send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND, scterror);
        return 0;
      }
  }
  /* FedSpace & MSL rules for Limpet Mines */
  if (mine_type == ASSET_LIMPET_MINE)
    {
      if (!g_cfg.mines.limpet.fedspace_allowed
          && is_fedspace_sector (sector_id))
        {
          send_enveloped_refused (ctx->fd,
                                  root,
                                  TERRITORY_UNSAFE,
                                  "Cannot deploy Limpet mines in Federation space.",
                                  NULL);
          return 0;
        }
      if (!g_cfg.mines.limpet.msl_allowed && is_msl_sector (db, sector_id))
        {
          send_enveloped_refused (ctx->fd,
                                  root,
                                  TERRITORY_UNSAFE,
                                  "Cannot deploy Limpet mines in Major Space Lanes.",
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to check for foreign limpets.");
      return 0;
    }
  sqlite3_bind_int (foreign_limpet_st, 1, sector_id);
  sqlite3_bind_int (foreign_limpet_st, 2, ASSET_LIMPET_MINE);
  sqlite3_bind_int (foreign_limpet_st, 3, ctx->player_id);
  sqlite3_bind_int (foreign_limpet_st, 4, ctx->corp_id);        // Assuming ctx->corp_id is available
  if (sqlite3_step (foreign_limpet_st) == SQLITE_ROW)
    {
      sqlite3_finalize (foreign_limpet_st);
      send_enveloped_refused (ctx->fd,
                              root,
                              ERR_FOREIGN_LIMPETS_PRESENT,
                              "Cannot deploy mines: foreign Limpet mines are present in this sector.",
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to query ship's mine count.");
      return 0;
    }
  if (mine_type == ASSET_MINE)
    {
      if (ship_mines < amount)
        {
          send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
                                "Insufficient Armid mines on ship.");
          return 0;
        }
    }
  else if (mine_type == ASSET_LIMPET_MINE)
    {
      if (ship_limpets < amount)
        {
          send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
                                "Insufficient Limpet mines on ship.");
          return 0;
        }
    }
  /* Sector Caps */
  sector_mine_counts_t counts;
  if (get_sector_mine_counts (sector_id, &counts) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to retrieve sector mine counts.");
      return 0;
    }
  // Combined Cap (Armid + Limpet)
  if (counts.total_mines + amount > SECTOR_MINE_CAP)
    {
      send_enveloped_refused (ctx->fd,
                              root,
                              ERR_SECTOR_OVERCROWDED,
                              "Sector total mine limit exceeded (50,000).",
                              NULL);                                                                                            // Using macro for now
      return 0;
    }
  // Type-specific Cap
  if (mine_type == ASSET_MINE)
    {
      // Armid-specific cap (using existing MINE_SECTOR_CAP_PER_TYPE for now, or define a new one)
      if (counts.armid_mines + amount > MINE_SECTOR_CAP_PER_TYPE)
        {
          send_enveloped_refused (ctx->fd, root, ERR_SECTOR_OVERCROWDED,
                                  "Sector Armid mine limit exceeded (100).",
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
          send_enveloped_refused (ctx->fd, root, ERR_SECTOR_OVERCROWDED,
                                  "Sector Limpet mine limit exceeded.",
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Could not start transaction");
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
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      if (mine_type == ASSET_MINE)
        {
          send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
                                "Insufficient Armid mines on ship");
        }
      else
        {
          send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
                                "Insufficient Limpet mines on ship");
        }
      return 0;
    }
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      if (mine_type == ASSET_MINE)
        {
          send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
                                "Failed to update ship Armid mines");
        }
      else
        {
          send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
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
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, SECTOR_ERR,
                            "Failed to create sector assets record");
      return 0;
    }
  int asset_id = (int) sqlite3_last_insert_rowid (db);  // Capture the newly created asset_id
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
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
  send_enveloped_ok (ctx->fd, root, "combat.mines_laid_v1", out);       // Changed type
  json_decref (out);
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
      send_enveloped_error (ctx->fd, root, ERR_SERVICE_UNAVAILABLE,
                            "Database unavailable");
      return 0;
    }
  /* 1. Parse input */
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
                            "Missing required field: data");
      return 0;
    }
  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_asset_id = json_object_get (data, "asset_id");
  if (!j_sector_id || !json_is_integer (j_sector_id) ||
      !j_asset_id || !json_is_integer (j_asset_id))
    {
      send_enveloped_error (ctx->fd,
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
      send_enveloped_error (ctx->fd, root, ERR_SHIP_NOT_FOUND,
                            "No active ship found for player.");
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to prepare player ship query.");
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
      send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
                            "Could not determine player's current sector.");
      return 0;
    }
  /* 3. Verify asset belongs to player and is in current sector */
  sqlite3_stmt *stmt_asset = NULL;
  const char *sql_asset =
    "SELECT quantity, asset_type FROM sector_assets "
    "WHERE id = ?1 AND player = ?2 AND sector = ?3 AND asset_type IN (1, 4);";                                                                          // Mines only
  if (sqlite3_prepare_v2 (db, sql_asset, -1, &stmt_asset, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to prepare asset query.");
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
      send_enveloped_error (ctx->fd,
                            root,
                            ERR_NOT_FOUND,
                            "Mine asset not found or does not belong to you in this sector.");
      return 0;
    }
  /* 4. Check if ship has capacity for recalled mines */
  if (ship_mines_current + asset_quantity > ship_mines_max)
    {
      send_enveloped_refused (ctx->fd, root, REF_INSUFFICIENT_CAPACITY,
                              "Insufficient ship capacity to recall all mines.",
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
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Could not start transaction");
      return 0;
    }
  // Delete the asset from sector_assets
  sqlite3_stmt *stmt_delete = NULL;
  const char *sql_delete = "DELETE FROM sector_assets WHERE id = ?1;";
  if (sqlite3_prepare_v2 (db, sql_delete, -1, &stmt_delete, NULL) !=
      SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to prepare delete asset query.");
      return 0;
    }
  sqlite3_bind_int (stmt_delete, 1, asset_id);
  if (sqlite3_step (stmt_delete) != SQLITE_DONE)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to delete asset from sector.");
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
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to prepare credit ship query.");
      return 0;
    }
  sqlite3_bind_int (stmt_credit, 1, asset_quantity);
  sqlite3_bind_int (stmt_credit, 2, player_ship_id);
  if (sqlite3_step (stmt_credit) != SQLITE_DONE)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_DB,
                            "Failed to credit mines to ship.");
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
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
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
  send_enveloped_ok (ctx->fd, root, "combat.mines.recalled", out);
  json_decref (out);
  return 0;
}

