#include <jansson.h>
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
#include "globals.h"
#include "server_planets.h"
#include "game_db.h"
#include "database_cmd.h"
#include "db/db_api.h"


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

static void apply_combat_damage (combat_ship_t *target,
                                 int damage,
                                 int *shield_dmg,
                                 int *hull_dmg);
static int load_ship_combat_stats_unlocked (db_t *db,
                                            int ship_id,
                                            combat_ship_t *out);
static int cmd_deploy_assets_list_internal (client_ctx_t *ctx,
                                            json_t *root,
                                            const char *list_type,
                                            const char *asset_key,
                                            const char *sql_query);
static int h_apply_quasar_damage (client_ctx_t *ctx, int damage,
                                  const char *source_desc);


static void
iss_summon (int sector_id, int player_id)
{
  LOGI ("ISS Summoned to sector %d for player %d", sector_id, player_id);
}


/* Forward decls from your codebase */
json_t *db_get_stardock_sectors (void);
int handle_ship_attack (client_ctx_t *ctx,
                        json_t *root, json_t *data, db_t *db);
static int ship_consume_mines (db_t *db, int ship_id, int asset_type,
                               int amount);
static int insert_sector_mines (db_t *db, int sector_id,
                                int owner_player_id, json_t *corp_id_json,
                                int asset_type, int offense_mode, int amount);
static int sum_sector_fighters (db_t *db, int sector_id, int *total_out);
static int ship_consume_fighters (db_t *db, int ship_id, int amount);
static int insert_sector_fighters (db_t *db, int sector_id,
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
  db_error_t err = {0};

  const char *sql =
    "SELECT 1 "
    "FROM msl_sectors "
    "WHERE sector_id = $1 "
    "LIMIT 1;";

  if (!db_query (db,
                 sql,
                 (db_bind_t[]){ db_bind_i32 (sector_id) },
                 1,
                 &res,
                 &err))
    {
      LOGE ("MSL check query failed: %s", err.message);
      return false;
    }

  bool is_msl = db_res_step (res, &err); /* true if at least one row */


  db_res_finalize (res);

  if (err.code != 0)
    {
      LOGE ("MSL check result step failed: %s", err.message);
      return false;
    }

  return is_msl;
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

/* Apply sector fighter hazards when ship enters a sector */
int
apply_sector_fighters_on_entry (client_ctx_t *ctx, int sector_id)
{
  if (!ctx || sector_id <= 0)
    {
      return 0;
    }

  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return 0;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      return 0;
    }

  /* Sector fighter damage configuration (from old system) */
  int damage_per_unit = 10;   /* Hull damage per fighter */

  /* Load ship stats */
  db_error_t err;
  db_res_t *res = NULL;

  const char *sql_ship = 
    "SELECT hull, fighters, shields FROM ships WHERE ship_id = $1;";
  db_bind_t params_ship[] = { db_bind_i32 (ship_id) };

  if (!db_query (db, sql_ship, params_ship, 1, &res, &err))
    {
      return 0;
    }

  combat_ship_t ship = { 0 };
  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      return 0;
    }

  ship.hull = db_res_col_int (res, 0, &err);
  ship.fighters = db_res_col_int (res, 1, &err);
  ship.shields = db_res_col_int (res, 2, &err);
  db_res_finalize (res);

  int ship_corp_id = ctx->corp_id;

  /* Query all hostile fighters in sector */
  const char *sql_ftr =
    "SELECT id, quantity, player, corporation, offensive_setting "
    "FROM sector_assets "
    "WHERE sector_id = $1 AND asset_type = 2 AND quantity > 0;";

  db_bind_t params_ftr[] = { db_bind_i32 (sector_id) };

  if (!db_query (db, sql_ftr, params_ftr, 1, &res, &err))
    {
      return 0;
    }

  /* Process each fighter asset */
  while (db_res_step (res, &err))
    {
      int asset_id = db_res_col_int (res, 0, &err);
      int quantity = db_res_col_int (res, 1, &err);
      int owner_id = db_res_col_int (res, 2, &err);
      int corp_id = db_res_col_int (res, 3, &err);
      int mode = db_res_col_int (res, 4, &err);  /* 1=Toll, 2=Attack */

      /* Skip if asset owner is not hostile */
      if (!is_asset_hostile (owner_id, corp_id, ctx->player_id, ship_corp_id))
        {
          continue;
        }

      /* For now, always attack (toll system requires h_bank_transfer_unlocked implementation) */
      {
        int damage = quantity * damage_per_unit;

        /* Apply damage to ship */
        armid_damage_breakdown_t breakdown = { 0 };
        apply_armid_damage_to_ship (&ship, damage, &breakdown);

        /* Update ship in database */
        const char *sql_upd = 
          "UPDATE ships SET hull = $1, fighters = $2, shields = $3 WHERE ship_id = $4;";
        db_bind_t params_upd[] = {
          db_bind_i32 (ship.hull),
          db_bind_i32 (ship.fighters),
          db_bind_i32 (ship.shields),
          db_bind_i32 (ship_id)
        };

        db_error_t upd_err;
        db_exec (db, sql_upd, params_upd, 4, &upd_err);

        /* Remove the fighter asset (one-time attack) */
        const char *sql_del = "DELETE FROM sector_assets WHERE id = $1;";
        db_bind_t params_del[] = { db_bind_i32 (asset_id) };
        db_exec (db, sql_del, params_del, 1, &upd_err);

        /* Log the attack */
        json_t *evt = json_object ();
        json_object_set_new (evt, "damage", json_integer (damage));
        json_object_set_new (evt, "fighters_engaged", json_integer (quantity));
        db_log_engine_event ((long long)time (NULL),
                            "combat.hit.fighters",
                            "player", ctx->player_id,
                            sector_id, evt, NULL);

        /* Check if ship destroyed */
        if (ship.hull <= 0)
          {
            db_res_finalize (res);
            destroy_ship_and_handle_side_effects (ctx, ctx->player_id);
            return 1;  /* Ship destroyed */
          }
      }
    }

  db_res_finalize (res);
  return 0;  /* No destruction */
}


int
cmd_combat_attack (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }

  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root,
                           ERR_SERVICE_UNAVAILABLE,
                           "Database unavailable");
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx, root,
                           ERR_MISSING_FIELD,
                           "Missing required field: data");
      return 0;
    }

  return handle_ship_attack (ctx, root, data, db);
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


/* Read ship combat stats (hull/fighters/shields). */
static int
db_ship_get_combat_stats (db_t *db,
                          int ship_id,
                          ship_t *out_ship,
                          db_error_t *err)
{
  db_res_t *res = NULL;

  const char *sql =
    "SELECT ship_id, hull, fighters, shields "
    "FROM ships "
    "WHERE ship_id = $1;";

  if (!db_query (db,
                 sql,
                 (db_bind_t[]){ db_bind_i32 (ship_id) },
                 1,
                 &res,
                 err))
    {
      return -1;
    }

  if (!db_res_step (res, err))
    {
      db_res_finalize (res);
      return 0; /* ship not found */
    }

  out_ship->id = (int)db_res_col_i32 (res, 0, err);
  out_ship->hull = (int)db_res_col_i32 (res, 1, err);
  out_ship->fighters = (int)db_res_col_i32 (res, 2, err);
  out_ship->shields = (int)db_res_col_i32 (res, 3, err);

  db_res_finalize (res);

  if (err && err->code != 0)
    {
      return -1;
    }

  return 1; /* found */
}


/* Update ship combat stats. */
static bool
db_ship_update_combat_stats (db_t *db,
                             int ship_id,
                             int hull,
                             int fighters,
                             int shields,
                             db_error_t *err)
{
  const char *sql =
    "UPDATE ships "
    "SET hull = $1, fighters = $2, shields = $3 "
    "WHERE ship_id = $4;";

  return db_exec (db,
                  sql,
                  (db_bind_t[]){
    db_bind_i32 (hull),
    db_bind_i32 (fighters),
    db_bind_i32 (shields),
    db_bind_i32 (ship_id)
  },
                  4,
                  err);
}


/*
 * Select Armid mines in sector, locking rows so two ships entering concurrently
 * don’t both process the same mine stacks.
 *
 * IMPORTANT: requires an open transaction.
 */
static bool
db_armid_mines_select_locked (db_t *db,
                              int sector_id,
                              db_res_t **out_res,
                              db_error_t *err)
{
  const char *sql =
    "SELECT sector_assets_id as id, quantity, offensive_setting, owner_id as player, corporation_id as corporation, ttl "
    "FROM sector_assets "
    "WHERE sector_id = $1 AND asset_type = 1 "
    "FOR UPDATE SKIP LOCKED;";

  return db_query (db,
                   sql,
                   (db_bind_t[]){ db_bind_i32 (sector_id) },
                   1,
                   out_res,
                   err);
}


/* Consume a mine stack row. */
static bool
db_sector_asset_delete (db_t *db,
                        int asset_id,
                        db_error_t *err)
{
  const char *sql = "DELETE FROM sector_assets WHERE sector_assets_id = $1;";
  return db_exec (db,
                  sql,
                  (db_bind_t[]){ db_bind_i32 (asset_id) },
                  1,
                  err);
}


int
apply_sector_quasar_on_entry (client_ctx_t *ctx, int sector_id)
{
  if (sector_id == 1)
    {
      return 0;  /* Terra/FedSpace is safe */
    }

  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;

  db_error_t err = {0};

  /* Find planets in sector with quasar cannons (citadel level >= 3) */
  const char *sql =
    "SELECT p.planet_id, p.owner_id, p.owner_type, c.level, c.qCannonSector, c.militaryReactionLevel "
    "FROM planets p "
    "LEFT JOIN citadels c ON p.planet_id = c.planet_id "
    "WHERE p.sector_id = $1 AND c.level >= 3 AND c.qCannonSector > 0 "
    "FOR UPDATE SKIP LOCKED;";

  db_res_t *res = NULL;
  if (!db_query (db, sql, (db_bind_t[]){ db_bind_i32 (sector_id) }, 1, &res, &err))
    return 0;

  int shot_fired = 0;
  while (db_res_step (res, &err))
    {
      int planet_id = (int)db_res_col_i32 (res, 0, &err);
      int owner_id = (int)db_res_col_i32 (res, 1, &err);
      const char *owner_type = db_res_col_text (res, 2, &err);
      int base_strength = (int)db_res_col_i32 (res, 4, &err);
      int reaction = (int)db_res_col_i32 (res, 5, &err);

      /* Resolve corp ID for hostility check */
      int p_corp_id = 0;
      if (owner_type && (strcasecmp (owner_type, "corp") == 0 ||
                         strcasecmp (owner_type, "corporation") == 0))
        {
          p_corp_id = owner_id;
        }

      /* Check if player is hostile to planet owner */
      if (is_asset_hostile (owner_id, p_corp_id, ctx->player_id, ctx->corp_id))
        {
          /* Calculate damage based on reaction level */
          int pct = 100;
          if (reaction == 1)
            pct = 125;
          else if (reaction >= 2)
            pct = 150;

          int damage = (int)floor ((double)base_strength * (double)pct / 100.0);

          char source_desc[64];
          snprintf (source_desc, sizeof (source_desc),
                    "Quasar Sector Shot (Planet %d)", planet_id);

          if (h_apply_quasar_damage (ctx, damage, source_desc))
            {
              shot_fired = 1;  /* Destroyed */
            }
          else
            {
              shot_fired = 2;  /* Damaged */
            }
          break;  /* Single shot per sector entry */
        }
    }

  db_res_finalize (res);
  return (shot_fired == 1);  /* Return 1 if destroyed */
}


int
apply_armid_mines_on_entry (client_ctx_t *ctx,
                            int new_sector_id,
                            armid_encounter_t *out_enc)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      LOGE ("Database handle not available in apply_armid_mines_on_entry");
      return -1;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      /* No active ship, no mines can be triggered */
      return 0;
    }

  /* Initialise out_enc */
  if (out_enc)
    {
      memset (out_enc, 0, sizeof (*out_enc));
      out_enc->sector_id = new_sector_id;
    }

  /* Load ship stats */
  ship_t ship_stats = {0};
  int ship_player_id = ctx->player_id;
  int ship_corp_id = ctx->corp_id;

  db_error_t err;


  db_error_clear (&err);

  int ship_found = db_ship_get_combat_stats (db, ship_id, &ship_stats, &err);


  if (ship_found < 0)
    {
      LOGE ("Failed to load ship %d for mine encounter: %s",
            ship_id,
            err.message);
      return -1;
    }
  if (ship_found == 0)
    {
      LOGW ("Ship %d not found for mine encounter.", ship_id);
      return 0;
    }

  /* Config for damage */
  int damage_per_mine = 10;


  damage_per_mine = db_get_config_int (db, "armid_damage_per_mine", 25);

  /* --- Process Armid Mines (Asset Type 1) --- */
  if (g_armid_config.armid.enabled)
    {
      db_res_t *res = NULL;


      db_error_clear (&err);

      /*
       * IMPORTANT: FOR UPDATE SKIP LOCKED needs a transaction.
       * If your movement pipeline already runs in a tx, you’re fine.
       * If not, wrap the caller in a tx (preferred).
       */
      if (!db_armid_mines_select_locked (db, new_sector_id, &res, &err))
        {
          LOGE ("Failed to select Armid mines: %s", err.message);
          return -1;
        }

      while (db_res_step (res, &err))
        {
          int mine_id = (int)db_res_col_i32 (res, 0, &err);
          int mine_quantity = (int)db_res_col_i32 (res, 1, &err);

          sector_asset_t mine_asset_row = {
            .id = mine_id,
            .quantity = mine_quantity,
            .player = (int)db_res_col_i32 (res, 3, &err),
            .corporation = (int)db_res_col_i32 (res, 4, &err),
            .ttl = (int)db_res_col_i32 (res, 5, &err)
          };


          if (err.code != 0)
            {
              LOGE ("Error reading mine row: %s", err.message);
              db_res_finalize (res);
              return -1;
            }

          /* Check Hostility (unchanged) */
          if (!is_asset_hostile (mine_asset_row.player,
                                 mine_asset_row.corporation,
                                 ship_player_id,
                                 ship_corp_id))
            {
              continue;
            }

          /* Canonical Rule: All hostile mines detonate */
          int exploded = mine_quantity;
          int damage = exploded * damage_per_mine;
          armid_damage_breakdown_t d = {0};


          apply_armid_damage_to_ship (&ship_stats, damage, &d);

          /* Remove Mines (Consumed) */
          db_error_clear (&err);
          if (!db_sector_asset_delete (db, mine_id, &err))
            {
              LOGE ("Failed to delete Armid mine stack %d: %s",
                    mine_id,
                    err.message);
              db_res_finalize (res);
              return -1;
            }

          /* Accumulate stats */
          if (out_enc)
            {
              out_enc->armid_triggered += exploded;
              out_enc->shields_lost += d.shields_lost;
              out_enc->fighters_lost += d.fighters_lost;
              out_enc->hull_lost += d.hull_lost;
            }

          /* Log Event (unchanged) */
          json_t *hit_data = json_object ();


          json_object_set_new (hit_data, "v", json_integer (1));
          json_object_set_new (hit_data, "attacker_id",
                               json_integer (mine_asset_row.player));
          json_object_set_new (hit_data, "defender_id",
                               json_integer (ctx->player_id));
          json_object_set_new (hit_data, "weapon", json_string ("armid_mines"));
          json_object_set_new (hit_data, "damage_total", json_integer (damage));
          json_object_set_new (hit_data, "mines_exploded",
                               json_integer (exploded));

          (void) db_log_engine_event ((long long)time (NULL),
                                      "combat.hit",
                                      "player",
                                      ctx->player_id,
                                      new_sector_id,
                                      hit_data,
                                      NULL);

          /*
           * Persist ship damage for each stack (unchanged intent):
           * - If destroyed: persist then destroy side-effects
           * - Else: persist so next stack sees updated HP
           */
          db_error_clear (&err);
          if (!db_ship_update_combat_stats (db,
                                            ship_id,
                                            ship_stats.hull,
                                            ship_stats.fighters,
                                            ship_stats.shields,
                                            &err))
            {
              LOGE ("Failed to update ship %d combat stats: %s",
                    ship_id,
                    err.message);
              db_res_finalize (res);
              return -1;
            }

          if (ship_stats.hull <= 0)
            {
              destroy_ship_and_handle_side_effects (ctx, ctx->player_id);
              if (out_enc)
                {
                  out_enc->destroyed = true;
                }

              LOGI ("Ship %d destroyed by Armid mine %d in sector %d",
                    ship_id, mine_id, new_sector_id);
              break;
            }
        }

      /* If loop ended due to res_step() false, check if that was error */
      if (err.code != 0)
        {
          LOGE ("Error iterating mine rows: %s", err.message);
          db_res_finalize (res);
          return -1;
        }

      db_res_finalize (res);
    }

  return 0;
}


/* Local helpers (keep in same .c file) */


static bool
db_limpet_assets_select_locked (db_t *db, int sector_id, db_res_t **out_res)
{
  db_error_t err = {0};

  const char *sql =
    "SELECT sector_assets_id as id, quantity, owner_id as player, corporation_id as corporation, ttl "
    "FROM sector_assets "
    "WHERE sector_id = $1 "
    "  AND asset_type = 4 "
    "  AND quantity > 0 "
    "FOR UPDATE SKIP LOCKED;";

  if (!db_query (db,
                 sql,
                 (db_bind_t[]){ db_bind_i32 (sector_id) },
                 1,
                 out_res,
                 &err))
    {
      LOGE ("Limpet select failed: %s", err.message);
      return false;
    }

  return true;
}


static bool
db_limpet_already_attached (db_t *db, int ship_id, int owner_id, bool *out_yes)
{
  db_res_t *res = NULL;
  db_error_t err = {0};

  const char *sql =
    "SELECT 1 "
    "FROM limpet_attached "
    "WHERE ship_id = $1 AND owner_player_id = $2 "
    "LIMIT 1;";

  if (!db_query (db,
                 sql,
                 (db_bind_t[]){ db_bind_i32 (ship_id), db_bind_i32 (owner_id) },
                 2,
                 &res,
                 &err))
    {
      LOGE ("Limpet attached-check failed: %s", err.message);
      return false;
    }

  bool yes = db_res_step (res, &err);


  db_res_finalize (res);

  if (err.code != 0)
    {
      LOGE ("Limpet attached-check step failed: %s", err.message);
      return false;
    }

  *out_yes = yes;
  return true;
}


static bool
db_sector_asset_decrement_or_delete (db_t *db, int asset_id, int quantity)
{
  db_error_t err = {0};

  if (quantity > 1)
    {
      const char *sql =
        "UPDATE sector_assets "
        "SET quantity = quantity - 1 "
        "WHERE id = $1;";


      if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i32 (asset_id) }, 1, &err))
        {
          LOGE ("Limpet decrement failed (asset %d): %s", asset_id,
                err.message);
          return false;
        }
      return true;
    }

  /* quantity == 1 => delete row */
  {
    const char *sql = "DELETE FROM sector_assets WHERE sector_assets_id = $1;";


    if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i32 (asset_id) }, 1, &err))
      {
        LOGE ("Limpet delete failed (asset %d): %s", asset_id, err.message);
        return false;
      }
  }

  return true;
}


static bool
db_limpet_attach (db_t *db, int ship_id, int owner_id, int64_t created_ts)
{
  db_error_t err = {0};

  /*
   * Prefer ON CONFLICT DO NOTHING if you have a UNIQUE(ship_id, owner_player_id).
   * If you don’t, add it; otherwise duplicates are possible under concurrency.
   */
  const char *sql =
    "INSERT INTO limpet_attached (ship_id, owner_player_id, created_ts) "
    "VALUES ($1, $2, $3) "
    "ON CONFLICT (ship_id, owner_player_id) DO NOTHING;";

  if (!db_exec (db,
                sql,
                (db_bind_t[]){
    db_bind_i32 (ship_id),
    db_bind_i32 (owner_id),
    db_bind_i64 (created_ts)
  },
                3,
                &err))
    {
      LOGE ("Limpet attach insert failed: %s", err.message);
      return false;
    }

  return true;
}


/* --- Refactored function --- */


int
apply_limpet_mines_on_entry (client_ctx_t *ctx,
                             int new_sector_id,
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

  db_res_t *res = NULL;


  if (!db_limpet_assets_select_locked (db, new_sector_id, &res))
    {
      return -1;
    }

  db_error_t err = {0};
  int64_t now_ts = (int64_t)time (NULL);


  while (db_res_step (res, &err))
    {
      int asset_id = (int)db_res_col_i32 (res, 0, &err);
      int quantity = (int)db_res_col_i32 (res, 1, &err);
      int owner_id = (int)db_res_col_i32 (res, 2, &err);
      int corp_id = (int)db_res_col_i32 (res, 3, &err);
      int64_t ttl = (int64_t)db_res_col_i64 (res, 4, &err);


      if (err.code != 0)
        {
          LOGE ("Limpet row decode failed: %s", err.message);
          db_res_finalize (res);
          return -1;
        }

      sector_asset_t asset = { .quantity = quantity, .ttl = (time_t)ttl };


      if (!is_asset_hostile (owner_id, corp_id, ship_player_id, ship_corp_id))
        {
          continue;
        }

      if (!armid_stack_is_active (&asset, (time_t)now_ts))
        {
          continue;
        }

      bool already_attached = false;


      if (!db_limpet_already_attached (db, ship_id, owner_id,
                                       &already_attached))
        {
          /* keep behaviour: on check error, skip this stack rather than killing entry */
          continue;
        }
      if (already_attached)
        {
          continue;
        }

      /* Consume 1 limpet from the stack (row is locked due to FOR UPDATE SKIP LOCKED) */
      if (!db_sector_asset_decrement_or_delete (db, asset_id, quantity))
        {
          db_res_finalize (res);
          return -1;
        }

      /* Attach marker to ship */
      if (!db_limpet_attach (db, ship_id, owner_id, now_ts))
        {
          db_res_finalize (res);
          return -1;
        }

      /* Event + log (unchanged) */
      json_t *event_data = json_object ();


      json_object_set_new (event_data, "target_ship_id",
                           json_integer (ship_id));
      json_object_set_new (event_data, "target_player_id",
                           json_integer (ship_player_id));
      json_object_set_new (event_data, "sector_id",
                           json_integer (new_sector_id));

      (void) db_log_engine_event (now_ts,
                                  "combat.limpet.attached",
                                  "player",
                                  owner_id,
                                  new_sector_id,
                                  event_data,
                                  NULL);

      LOGI ("Limpet attached! Ship %d tagged by Player %d in Sector %d",
            ship_id, owner_id, new_sector_id);
    }

  if (err.code != 0)
    {
      LOGE ("Limpet select iteration failed: %s", err.message);
      db_res_finalize (res);
      return -1;
    }

  db_res_finalize (res);
  return 0;
}


int
handle_combat_flee (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVICE_UNAVAILABLE,
                           "Database unavailable");
      return 0;
    }

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
      return handle_turn_consumption_error (ctx, tc, "combat.flee", root, NULL);
    }

  /* Load Engine/Mass (MVP: Engine=10 fixed, Mass=maxholds) */
  int mass = 100;
  int engine = 10;
  int sector_id = 0;

  {
    db_res_t *res = NULL;
    db_error_t err;


    db_error_clear (&err);

    const char *sql =
      "SELECT t.maxholds, s.sector_id "
      "FROM ships s "
      "JOIN shiptypes t ON s.type_id = t.id "
      "WHERE s.id = $1;";


    if (!db_query (db,
                   sql,
                   (db_bind_t[]){ db_bind_i32 (ship_id) },
                   1,
                   &res,
                   &err))
      {
        send_response_error (ctx, root, ERR_DB,
                             (err.message[0] ? err.message :
                              "DB query failed"));
        return 0;
      }

    if (db_res_step (res, &err))
      {
        mass = (int) db_res_col_i32 (res, 0, &err);
        sector_id = (int) db_res_col_i32 (res, 1, &err);
      }

    db_res_finalize (res);

    if (err.code != 0)
      {
        send_response_error (ctx, root, ERR_DB,
                             (err.message[0] ? err.message :
                              "DB decode failed"));
        return 0;
      }
  }

  /* Deterministic check: (Engine * 10) / (Mass + 1) > 0.5 */
  double score = ((double) engine * 10.0) / ((double) mass + 1.0);
  bool success = (score > 0.5);


  if (success)
    {
      /* Pick first adjacent sector */
      int dest = 0;

      {
        db_res_t *res = NULL;
        db_error_t err;


        db_error_clear (&err);

        const char *sql =
          "SELECT to_sector "
          "FROM sector_warps "
          "WHERE from_sector = $1 "
          "ORDER BY to_sector ASC "
          "LIMIT 1;";


        if (!db_query (db,
                       sql,
                       (db_bind_t[]){ db_bind_i32 (sector_id) },
                       1,
                       &res,
                       &err))
          {
            send_response_error (ctx, root, ERR_DB,
                                 (err.message[0] ? err.message :
                                  "DB query failed"));
            return 0;
          }

        if (db_res_step (res, &err))
          {
            dest = (int) db_res_col_i32 (res, 0, &err);
          }

        db_res_finalize (res);

        if (err.code != 0)
          {
            send_response_error (ctx, root, ERR_DB,
                                 (err.message[0] ? err.message :
                                  "DB decode failed"));
            return 0;
          }
      }


      if (dest > 0)
        {
          db_error_t err;


          db_error_clear (&err);

          /* Transaction: move ship + player */
          if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
            {
              send_response_error (ctx, root, ERR_DB,
                                   (err.message[0] ? err.message :
                                    "DB tx begin failed"));
              return 0;
            }

          const char *sql_ship =
            "UPDATE ships SET sector_id = $1 WHERE ship_id = $2;";


          if (!db_exec (db,
                        sql_ship,
                        (db_bind_t[]){ db_bind_i32 (dest),
                                       db_bind_i32 (ship_id) },
                        2,
                        &err))
            {
              (void) db_tx_rollback (db, &err);
              send_response_error (ctx, root, ERR_DB,
                                   (err.message[0] ? err.message :
                                    "Ship update failed"));
              return 0;
            }

          const char *sql_player =
            "UPDATE players SET sector_id = $1 WHERE player_id = $2;";


          if (!db_exec (db,
                        sql_player,
                        (db_bind_t[]){ db_bind_i32 (dest),
                                       db_bind_i32 (ctx->player_id) },
                        2,
                        &err))
            {
              (void) db_tx_rollback (db, &err);
              send_response_error (ctx, root, ERR_DB,
                                   (err.message[0] ? err.message :
                                    "Player update failed"));
              return 0;
            }

          if (!db_tx_commit (db, &err))
            {
              (void) db_tx_rollback (db, &err);
              send_response_error (ctx, root, ERR_DB,
                                   (err.message[0] ? err.message :
                                    "DB tx commit failed"));
              return 0;
            }

          /* Hazards (post-move) */
          h_handle_sector_entry_hazards (db, ctx, dest);

          json_t *res = json_object ();


          json_object_set_new (res, "success", json_true ());
          json_object_set_new (res, "to_sector", json_integer (dest));
          send_response_ok_take (ctx, root, "combat.flee", &res);
          return 0;
        }
    }

  /* Failure */
  {
    json_t *res = json_object ();


    json_object_set_new (res, "success", json_false ());
    send_response_ok_take (ctx, root, "combat.flee", &res);
  }

  return 0;
}


//////////////////////


////////////////////////


////////////////////////


//////////////////////


int
cmd_deploy_fighters_list (client_ctx_t *ctx, json_t *root)
{
  const char *sql_query_fighters =
    "SELECT "
    "  sa.sector_assets_id AS asset_id, "
    "  sa.sector_id AS sector_id, "
    "  sa.quantity AS count, "
    "  sa.offensive_setting AS offense_mode, "
    "  sa.owner_id AS player_id, "
    "  p.name AS player_name, "
    "  c.corporation_id AS corp_id, "
    "  c.tag AS corp_tag, "
    "  sa.asset_type AS type "
    "FROM sector_assets sa "
    "JOIN players p ON sa.owner_id = p.player_id "
    "LEFT JOIN corp_members cm_player ON cm_player.player_id = sa.owner_id "
    "LEFT JOIN corporations c ON c.corporation_id = cm_player.corporation_id "
    "WHERE "
    "  sa.asset_type = 2 "
    "  AND ( "
    "    sa.owner_id = $1 "
    "    OR sa.owner_id IN ( "
    "      SELECT cm_member.player_id "
    "      FROM corp_members cm_member "
    "      WHERE cm_member.corporation_id = ( "
    "        SELECT cm_self.corporation_id "
    "        FROM corp_members cm_self "
    "        WHERE cm_self.player_id = $2 "
    "      ) "
    "    ) "
    "  ) "
    "ORDER BY sa.sector_id ASC;";

  return cmd_deploy_assets_list_internal (ctx,
                                          root,
                                          "deploy.fighters.list_v1",
                                          "fighters",
                                          sql_query_fighters);
}


int
cmd_deploy_mines_list (client_ctx_t *ctx, json_t *root)
{
  const char *sql_query_mines =
    "SELECT "
    "  sa.sector_assets_id AS asset_id, "
    "  sa.sector_id AS sector_id, "
    "  sa.quantity AS count, "
    "  sa.offensive_setting AS offense_mode, "
    "  sa.owner_id AS player_id, "
    "  p.name AS player_name, "
    "  c.corporation_id AS corp_id, "
    "  c.tag AS corp_tag, "
    "  sa.asset_type AS type "
    "FROM sector_assets sa "
    "JOIN players p ON sa.owner_id = p.player_id "
    "LEFT JOIN corp_members cm_player ON cm_player.player_id = sa.owner_id "
    "LEFT JOIN corporations c ON c.corporation_id = cm_player.corporation_id "
    "WHERE "
    "  sa.asset_type IN (1, 4) "
    "  AND ( "
    "    sa.owner_id = $1 "
    "    OR sa.owner_id IN ( "
    "      SELECT cm_member.player_id "
    "      FROM corp_members cm_member "
    "      WHERE cm_member.corporation_id = ( "
    "        SELECT cm_self.corporation_id "
    "        FROM corp_members cm_self "
    "        WHERE cm_self.player_id = $2 "
    "      ) "
    "    ) "
    "  ) "
    "ORDER BY sa.sector_id ASC, sa.asset_type ASC;";

  return cmd_deploy_assets_list_internal (ctx,
                                          root,
                                          "deploy.mines.list_v1",
                                          "mines",
                                          sql_query_mines);
}


int
cmd_deploy_assets_list_internal (client_ctx_t *ctx,
                                 json_t *root,
                                 const char *list_type,
                                 const char *asset_key,
                                 const char *sql_query)
{
  (void) asset_key;

  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database handle not available.");
      return 0;
    }

  const int self_player_id = ctx->player_id;

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  if (!db_query (db,
                 sql_query,
                 (db_bind_t[]){
    db_bind_i32 (self_player_id),
    db_bind_i32 (self_player_id)
  },
                 2,
                 &res,
                 &err))
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           (err.message[0] ? err.message : "DB query failed."));
      return 0;
    }

  int total_count = 0;
  json_t *entries = json_array ();


  if (!entries)
    {
      db_res_finalize (res);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Out of memory.");
      return 0;
    }

  while (db_res_step (res, &err))
    {
      total_count++;

      int asset_id = (int) db_res_col_i32 (res, 0, &err);
      int sector_id = (int) db_res_col_i32 (res, 1, &err);
      int count = (int) db_res_col_i32 (res, 2, &err);
      int offense_mode = (int) db_res_col_i32 (res, 3, &err);
      int player_id = (int) db_res_col_i32 (res, 4, &err);

      const char *player_name =
        db_res_col_is_null (res, 5) ? NULL : db_res_col_text (res, 5, &err);

      int corp_id =
        db_res_col_is_null (res, 6) ? 0 : (int) db_res_col_i32 (res, 6, &err);

      const char *corp_tag =
        db_res_col_is_null (res, 7) ? NULL : db_res_col_text (res, 7, &err);

      int asset_type = (int) db_res_col_i32 (res, 8, &err);


      if (err.code != 0)
        {
          db_res_finalize (res);
          json_decref (entries);
          send_response_error (ctx,
                               root,
                               ERR_SERVER_ERROR,
                               (err.message[0] ? err.message :
                                "DB decode failed."));
          return 0;
        }

      json_t *entry = json_object ();


      if (!entry)
        {
          db_res_finalize (res);
          json_decref (entries);
          send_response_error (ctx, root, ERR_SERVER_ERROR, "Out of memory.");
          return 0;
        }

      json_object_set_new (entry, "asset_id", json_integer (asset_id));
      json_object_set_new (entry, "sector_id", json_integer (sector_id));
      json_object_set_new (entry, "count", json_integer (count));
      json_object_set_new (entry, "offense_mode", json_integer (offense_mode));
      json_object_set_new (entry, "player_id", json_integer (player_id));
      json_object_set_new (entry, "player_name",
                           json_string (player_name ? player_name : "Unknown"));
      json_object_set_new (entry, "corp_id", json_integer (corp_id));
      json_object_set_new (entry, "corp_tag",
                           json_string (corp_tag ? corp_tag : ""));
      json_object_set_new (entry, "type", json_integer (asset_type));

      json_array_append_new (entries, entry);
    }

  if (err.code != 0)
    {
      db_res_finalize (res);
      json_decref (entries);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           (err.message[0] ? err.message :
                            "DB iteration failed."));
      return 0;
    }

  db_res_finalize (res);

  json_t *jdata_payload = json_object ();


  if (!jdata_payload)
    {
      json_decref (entries);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Out of memory.");
      return 0;
    }

  json_object_set_new (jdata_payload, "total", json_integer (total_count));
  json_object_set_new (jdata_payload, "entries", entries);

  send_response_ok_take (ctx, root, list_type, &jdata_payload);
  return 0;
}


/* ---------- combat.deploy_fighters (PG-style SQL, db_* API) ---------- */


static bool
db_i32_scalar (db_t *db,
               const char *sql,
               const db_bind_t *params,
               size_t n_params,
               int *out_val,
               db_error_t *err)
{
  db_res_t *res = NULL;
  if (!db_query (db, sql, params, n_params, &res, err))
    {
      return false;
    }

  bool ok = false;


  if (db_res_step (res, err))
    {
      *out_val = (int) db_res_col_i32 (res, 0, err);
      ok = (err->code == 0);
    }
  else
    {
      /* no row is not a decode error; caller decides */
      ok = (err->code == 0);
    }

  db_res_finalize (res);
  return ok;
}


static bool
db_i64_scalar (db_t *db,
               const char *sql,
               const db_bind_t *params,
               size_t n_params,
               int64_t *out_val,
               db_error_t *err)
{
  db_res_t *res = NULL;
  if (!db_query (db, sql, params, n_params, &res, err))
    {
      return false;
    }

  bool ok = false;


  if (db_res_step (res, err))
    {
      *out_val = (int64_t) db_res_col_i64 (res, 0, err);
      ok = (err->code == 0);
    }
  else
    {
      ok = (err->code == 0);
    }

  db_res_finalize (res);
  return ok;
}


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
                           ERR_SERVICE_UNAVAILABLE,
                           "Database unavailable");
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
                           ERR_MISSING_FIELD,
                           "Missing required field: data");
      return 0;
    }

  json_t *j_amount = json_object_get (data, "amount");
  json_t *j_offense = json_object_get (data, "offense");
  json_t *j_corp_id = json_object_get (data, "corporation_id"); /* optional/nullable */


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
      send_response_error (ctx, root, ERR_CURSOR_INVALID,
                           "offense must be one of {1=TOLL,2=DEFEND,3=ATTACK}");
      return 0;
    }

  /* Resolve active ship */
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

  /* Transaction: debit ship, credit sector */
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      send_response_error (ctx, root, ERR_DB,
                           (err.message[0] ? err.message :
                            "Could not start transaction"));
      return 0;
    }

  int sector_id = -1;

  /* 1) Lock ship row and read current sector (SKIP LOCKED) */
  {
    const char *sql =
      "SELECT sector_id "
      "FROM ships "
      "WHERE ship_id = $1 "
      "FOR UPDATE SKIP LOCKED;";


    if (!db_i32_scalar (db,
                        sql,
                        (db_bind_t[]){ db_bind_i32 (ship_id) },
                        1,
                        &sector_id,
                        &err))
      {
        (void) db_tx_rollback (db, &err);
        send_response_error (ctx, root, ERR_DB,
                             (err.message[0] ? err.message :
                              "Unable to resolve current sector"));
        return 0;
      }

    if (err.code != 0 || sector_id <= 0)
      {
        (void) db_tx_rollback (db, &err);
        /* If SKIP LOCKED skipped the row, we see “no row” -> sector_id stays -1 */
        send_response_error (ctx,
                             root,
                             ERR_SECTOR_NOT_FOUND,
                             "Unable to resolve current sector (ship locked or missing).");
        return 0;
      }
  }

  /* 2) Lock sector row (serialises sector-cap enforcement). SKIP LOCKED => fail fast. */
  {
    const char *sql =
      "SELECT sector_id "
      "FROM sectors "
      "WHERE sector_id = $1 "
      "FOR UPDATE SKIP LOCKED;";

    int tmp = 0;


    if (!db_i32_scalar (db,
                        sql,
                        (db_bind_t[]){ db_bind_i32 (sector_id) },
                        1,
                        &tmp,
                        &err) || tmp <= 0 || err.code != 0)
      {
        (void) db_tx_rollback (db, &err);
        send_response_error (ctx, root, ERR_SECTOR_NOT_FOUND,
                             "Sector locked or not found.");
        return 0;
      }
  }

  /* 3) Sector cap check (inside tx while holding sector lock) */
  int sector_total = 0;


  /* Use helper: sum_sector_fighters */
  if (sum_sector_fighters (db, sector_id, &sector_total) != 0)
    {
      (void) db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           REF_NOT_IN_SECTOR,
                           "Failed to read sector fighters");
      return 0;
    }

  if (sector_total + amount > SECTOR_FIGHTER_CAP)
    {
      (void) db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_SECTOR_OVERCROWDED,
                           "Sector fighter limit exceeded (50,000)");
      return 0;
    }

  /* 4) Consume fighters atomically using helper */
  int rc = ship_consume_fighters (db, ship_id, amount);


  if (rc != 0)
    {
      (void) db_tx_rollback (db, &err);
      if (rc == REF_AMMO_DEPLETED)
        {
          send_response_error (ctx,
                               root,
                               REF_AMMO_DEPLETED,
                               "Insufficient fighters on ship");
        }
      else
        {
          send_response_error (ctx,
                               root,
                               REF_AMMO_DEPLETED,
                               "Failed to update ship fighters");
        }
      return 0;
    }

  /* 5) Insert sector asset row using helper */
  rc = insert_sector_fighters (db,
                               sector_id,
                               ctx->player_id,
                               j_corp_id,
                               offense,
                               amount);
  if (rc != 0)
    {
      (void) db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           SECTOR_ERR,
                           "Failed to create sector assets record");
      return 0;
    }

  /* Capture asset_id_i64 for event logging.
     Since insert_sector_fighters doesn't return it, we query the MAX(id) in this tx. */
  int64_t asset_id_i64 = 0;
  {
    const char *sql_get_id =
      "SELECT MAX(sector_assets_id) FROM sector_assets WHERE sector_id=$1 AND owner_id=$2 AND asset_type=2";


    db_i64_scalar (db,
                   sql_get_id,
                   (db_bind_t[]){ db_bind_i32 (sector_id),
                                  db_bind_i32 (ctx->player_id) },
                   2,
                   &asset_id_i64,
                   &err);
  }


  if (!db_tx_commit (db, &err))
    {
      (void) db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_DB, "Commit failed");
      return 0;
    }

  const int asset_id = (int) asset_id_i64;


  /* Fedspace/Stardock → summon ISS + warn player (unchanged) */
  if (sector_id >= 1 && sector_id <= 10)
    {
      in_fed = true;
    }

  json_t *stardock_sectors = db_get_stardock_sectors ();


  if (stardock_sectors && json_is_array (stardock_sectors))
    {
      size_t index;
      json_t *sector_value = NULL;


      json_array_foreach (stardock_sectors, index, sector_value)
      {
        if (json_is_integer (sector_value))
          {
            int stardock_sector_id = (int) json_integer_value (sector_value);


            if (sector_id == stardock_sector_id)
              {
                in_sdock = true;
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
      (void) h_send_message_to_player (db,
                                       ctx->player_id,
                                       0,
                                       "Federation Warning",
                                       "Fighter deployment in protected space has triggered ISS response.");
    }

  /* Emit engine event (unchanged) */
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
    json_object_set_new (evt, "asset_id", json_integer (asset_id));
    (void) db_log_engine_event ((long long) time (NULL),
                                "combat.fighters.deployed", NULL,
                                ctx->player_id, sector_id, evt, NULL);
  }

  /* Recompute total for response convenience (outside tx ok) */
  int sector_total_after = 0;
  {
    db_error_t err2;


    db_error_clear (&err2);
    const char *sql =
      "SELECT COALESCE(SUM(quantity), 0) "
      "FROM sector_assets "
      "WHERE sector_id = $1 AND asset_type = 2;";


    (void) db_i32_scalar (db,
                          sql,
                          (db_bind_t[]){ db_bind_i32 (sector_id) },
                          1,
                          &sector_total_after,
                          &err2);
    /* ignore errors here as per prior behaviour */
  }


  LOGI (
    "DEBUG: cmd_combat_deploy_fighters - sector_id: %d, player_id: %d, amount: %d, offense: %d, sector_total: %d, asset_id: %d",
    sector_id,
    ctx->player_id,
    amount,
    offense,
    sector_total_after,
    asset_id);

  /* Response payload (unchanged fields) */
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
                       json_integer (sector_total_after));
  json_object_set_new (out, "asset_id", json_integer (asset_id));

  send_response_ok_take (ctx, root, "combat.fighters.deployed", &out);
  return 0;
}


static const char *SQL_SECTOR_FIGHTER_SUM =
  "SELECT COALESCE(SUM(quantity), 0) FROM sector_assets WHERE sector_id = $1 AND asset_type = 2;";


static int
sum_sector_fighters (db_t *db, int sector_id, int *total_out)
{
  if (!db || !total_out)
    {
      return ERR_DB_MISUSE;
    }
  *total_out = 0;
  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  if (!db_query (db, SQL_SECTOR_FIGHTER_SUM,
                 (db_bind_t[]){ db_bind_i32 (sector_id) }, 1, &res, &err))
    {
      return err.code ? err.code : ERR_DB;
    }
  if (db_res_step (res, &err))
    {
      *total_out = (int) db_res_col_i32 (res, 0, &err);
    }
  db_res_finalize (res);
  return err.code;
}


/* ---------- combat.deploy_mines ---------- */
static const char *SQL_SECTOR_MINE_SUM =
  "SELECT COALESCE(SUM(quantity), 0) "
  "FROM sector_assets "
  "WHERE sector_id = $1 AND asset_type IN (1, 4);"; /* 1 Armid, 4 Limpet */


static int
sum_sector_mines (db_t *db, int sector_id, int *total_out)
{
  if (!db || !total_out)
    {
      return ERR_DB_MISUSE;
    }

  *total_out = 0;

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  if (!db_query (db,
                 SQL_SECTOR_MINE_SUM,
                 (db_bind_t[]){ db_bind_i32 (sector_id) },
                 1,
                 &res,
                 &err))
    {
      return err.code ? err.code : ERR_DB;
    }

  if (db_res_step (res, &err))
    {
      if (err.code == 0)
        {
          *total_out = (int)db_res_col_i32 (res, 0, &err);
        }
    }
  /* no rows => total_out stays 0; not an error */

  db_res_finalize (res);

  return err.code ? err.code : 0;
}


/* ---------- helpers: db scalar decode ---------- */


static bool
db_try_step_one_i32 (db_t *db,
                     const char *sql,
                     const db_bind_t *params,
                     size_t n_params,
                     int *out_i32,
                     db_error_t *err)
{
  db_res_t *res = NULL;
  if (!db_query (db, sql, params, n_params, &res, err))
    {
      return false;
    }

  bool have_row = db_res_step (res, err);


  if (have_row && err->code == 0)
    {
      *out_i32 = (int) db_res_col_i32 (res,
                                       0,
                                       err);
    }

  db_res_finalize (res);
  return have_row && err->code == 0;
}


/* ---------- fighters ---------- */


/* Debit ship fighters safely (returns REF_AMMO_DEPLETED if insufficient). */
static int
ship_consume_fighters (db_t *db, int ship_id, int amount)
{
  if (!db || ship_id <= 0 || amount <= 0)
    {
      return ERR_DB_MISUSE;
    }

  db_error_t err;


  db_error_clear (&err);

  /*
     Atomic + lock-friendly:
      - Locks the ship row (SKIP LOCKED) so we don’t block.
      - Only succeeds if fighters >= amount.
      - RETURNING gives us “did we update?”
   */
  const char *sql =
    "UPDATE ships "
    "SET fighters = fighters - $1 "
    "WHERE id = $2 "
    "  AND fighters >= $1 "
    "RETURNING fighters;";

  int new_fighters = 0;
  bool ok = db_try_step_one_i32 (db,
                                 sql,
                                 (db_bind_t[]){ db_bind_i32 (amount),
                                                db_bind_i32 (ship_id) },
                                 2,
                                 &new_fighters,
                                 &err);


  if (ok)
    {
      return 0;
    }

  if (err.code != 0)
    {
      return err.code;
    }

  /* No row returned: either ship missing OR insufficient fighters. */
  return REF_AMMO_DEPLETED;
}


/* ---------- mines: sector sum ---------- */

/* ---------- mines: ship consume ---------- */


/* Debit ship mines safely (returns REF_AMMO_DEPLETED if insufficient). */
static int
ship_consume_mines (db_t *db, int ship_id, int asset_type, int amount)
{
  if (!db || ship_id <= 0 || amount <= 0)
    {
      return ERR_DB_MISUSE;
    }

  const char *sql = NULL;


  /* Choose fixed SQL (don’t build dynamic column names). */
  if (asset_type == ASSET_MINE)
    {
      sql =
        "UPDATE ships "
        "SET mines = mines - $1 "
        "WHERE ship_id = $2 "
        "  AND mines >= $1 "
        "RETURNING mines;";
    }
  else if (asset_type == ASSET_LIMPET_MINE)
    {
      sql =
        "UPDATE ships "
        "SET limpets = limpets - $1 "
        "WHERE ship_id = $2 "
        "  AND limpets >= $1 "
        "RETURNING limpets;";
    }
  else
    {
      LOGE ("ship_consume_mines - Invalid asset_type %d",
            asset_type);
      return ERR_DB_MISUSE;
    }

  db_error_t err;


  db_error_clear (&err);

  int new_count = 0;
  bool ok = db_try_step_one_i32 (db,
                                 sql,
                                 (db_bind_t[]){ db_bind_i32 (amount),
                                                db_bind_i32 (ship_id) },
                                 2,
                                 &new_count,
                                 &err);


  if (ok)
    {
      return 0;
    }

  if (err.code != 0)
    {
      return err.code;
    }

  /* No row returned: ship missing OR insufficient mines. */
  return REF_AMMO_DEPLETED;
}


/* Insert a sector_assets row for mines. */
static int
insert_sector_mines (db_t *db,
                     int sector_id,
                     int owner_player_id,
                     json_t *corp_id_json /* nullable */,
                     int asset_type,
                     int offense_mode,
                     int amount)
{
  if (!db || sector_id <= 0 || owner_player_id <= 0 || amount <= 0)
    {
      return ERR_DB_MISUSE;
    }

  int corp_id = 0;


  if (corp_id_json && json_is_integer (corp_id_json))
    {
      corp_id = (int) json_integer_value (corp_id_json);
    }

  db_error_t err;


  db_error_clear (&err);

  const char *sql =
    "INSERT INTO sector_assets ("
    "  sector_id, owner_id, corporation_id, asset_type, quantity, offensive_setting, deployed_at"
    ") VALUES ("
    "  $1, $2, $3, $4, $5, $6, EXTRACT(EPOCH FROM NOW())::bigint"
    ");";


  if (!db_exec (db,
                sql,
                (db_bind_t[]){
    db_bind_i32 (sector_id),
    db_bind_i32 (owner_player_id),
    db_bind_i32 (corp_id),
    db_bind_i32 (asset_type),
    db_bind_i32 (amount),
    db_bind_i32 (offense_mode),
  },
                6,
                &err))
    {
      LOGE ("insert_sector_mines failed: %s", err.message);
      return err.code ? err.code : ERR_DB;
    }

  return 0;
}


/* Insert a sector_assets row for fighters. */
static int
insert_sector_fighters (db_t *db,
                        int sector_id,
                        int owner_player_id,
                        json_t *corp_id_json /* nullable */,
                        int offense_mode,
                        int amount)
{
  if (!db || sector_id <= 0 || owner_player_id <= 0 || amount <= 0)
    {
      return ERR_DB_MISUSE;
    }

  int corp_id = 0;


  if (corp_id_json && json_is_integer (corp_id_json))
    {
      corp_id = (int) json_integer_value (corp_id_json);
    }

  db_error_t err;


  db_error_clear (&err);

  /* asset_type = 2 for fighters (as per your comment) */
  const char *sql =
    "INSERT INTO sector_assets ("
    "  sector_id, owner_id, corporation_id, asset_type, quantity, offensive_setting, deployed_at"
    ") VALUES ("
    "  $1, $2, $3, 2, $4, $5, EXTRACT(EPOCH FROM NOW())::bigint"
    ");";


  if (!db_exec (db,
                sql,
                (db_bind_t[]){
    db_bind_i32 (sector_id),
    db_bind_i32 (owner_player_id),
    db_bind_i32 (corp_id),
    db_bind_i32 (amount),
    db_bind_i32 (offense_mode),
  },
                5,
                &err))
    {
      LOGE ("insert_sector_fighters failed: %s", err.message);
      return err.code ? err.code : ERR_DB;
    }

  return 0;
}


typedef struct {
  int total_mines;
  int armid_mines;
  int limpet_mines;
} sector_mine_counts_t;


/*
 * Populates sector_mine_counts_t with counts of different mine types in a sector.
 */
int
get_sector_mine_counts (int sector_id, sector_mine_counts_t *out)
{
  if (!out)
    {
      return ERR_DB_MISUSE;
    }

  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return ERR_DB;
    }

  memset (out, 0, sizeof (*out));

  db_error_t err;


  db_error_clear (&err);

  db_res_t *res = NULL;

  const char *sql =
    "SELECT "
    "  COALESCE(SUM(CASE WHEN asset_type IN (1, 4) THEN quantity ELSE 0 END), 0) AS total_mines, "
    "  COALESCE(SUM(CASE WHEN asset_type = 1 THEN quantity ELSE 0 END), 0)      AS armid_mines, "
    "  COALESCE(SUM(CASE WHEN asset_type = 4 THEN quantity ELSE 0 END), 0)      AS limpet_mines "
    "FROM sector_assets "
    "WHERE sector_id = $1;";


  if (!db_query (db,
                 sql,
                 (db_bind_t[]){ db_bind_i32 (sector_id) },
                 1,
                 &res,
                 &err))
    {
      LOGE ("get_sector_mine_counts query failed: %s", err.message);
      return err.code ? err.code : ERR_DB;
    }

  if (db_res_step (res, &err))
    {
      if (err.code == 0)
        {
          out->total_mines = (int) db_res_col_i32 (res, 0, &err);
          out->armid_mines = (int) db_res_col_i32 (res, 1, &err);
          out->limpet_mines = (int) db_res_col_i32 (res, 2, &err);
        }
    }

  db_res_finalize (res);

  if (err.code != 0)
    {
      LOGE ("get_sector_mine_counts decode/step failed: %s", err.message);
      return err.code;
    }

  return 0;
}


/* ---------- combat.sweep_mines (PG/db_* refactor, no sqlite) ---------- */

typedef struct mine_stack_info_s
{
  int id;
  int quantity;
  int player;
  int corporation;
  int64_t ttl;
} mine_stack_info_t;


int
cmd_combat_sweep_mines (client_ctx_t *ctx,
                        json_t *root)
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
                           ERR_SERVICE_UNAVAILABLE,
                           "Database unavailable");
      return 0;
    }

  /* 1) Validation (unchanged) */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD,
                           "Missing required field: data");
      return 0;
    }

  json_t *j_from_sector_id = json_object_get (data, "from_sector_id");
  json_t *j_target_sector_id = json_object_get (data, "target_sector_id");
  json_t *j_fighters_committed = json_object_get (data, "fighters_committed");
  json_t *j_mine_type_str = json_object_get (data, "mine_type");


  if (!j_from_sector_id || !json_is_integer (j_from_sector_id) ||
      !j_target_sector_id || !json_is_integer (j_target_sector_id) ||
      !j_fighters_committed || !json_is_integer (j_fighters_committed) ||
      !j_mine_type_str || !json_is_string (j_mine_type_str))
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
      send_response_error (ctx, root, ERR_CURSOR_INVALID,
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

  /* Limpet-specific gates (unchanged) */
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
          send_response_refused_steal (ctx, root, ERR_LIMPET_SWEEP_DISABLED,
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

  /* Need the player’s active ship */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND,
                           "No active ship found for player.");
      return 0;
    }

  /* Warp existence check (unchanged) */
  if (!h_warp_exists (db, from_sector_id, target_sector_id))
    {
      send_response_error (ctx, root, ERR_TARGET_INVALID,
                           "No warp exists between specified sectors.");
      return 0;
    }

  if (!g_armid_config.sweep.enabled)
    {
      send_response_error (ctx, root, ERR_CAPABILITY_DISABLED,
                           "Mine sweeping is currently disabled.");
      return 0;
    }

  db_error_t err;


  db_error_clear (&err);

  /* 2) Transaction begins (replaces BEGIN IMMEDIATE) */
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      send_response_error (ctx, root, ERR_DB,
                           (err.message[0] ? err.message :
                            "Could not start transaction"));
      return 0;
    }

  /* 3) Lock + load player ship (sector/fighters/corp) with FOR UPDATE SKIP LOCKED */
  int player_current_sector_id = -1;
  int ship_fighters_current = 0;
  int ship_corp_id = 0;

  {
    db_res_t *res = NULL;
    const char *sql_ship =
      "SELECT sector_id, fighters, corporation_id "
      "FROM ships "
      "WHERE ship_id = $1 "
      "FOR UPDATE SKIP LOCKED;";

    db_bind_t p[] = { db_bind_i32 (ship_id) };


    if (!db_query (db, sql_ship, p, 1, &res, &err))
      {
        db_tx_rollback (db, &err);
        send_response_error (ctx, root, ERR_DB,
                             (err.message[0] ? err.message :
                              "Failed to query player ship"));
        return 0;
      }

    if (!db_res_step (res, &err))
      {
        /* Either ship row missing, or locked by another tx and SKIP LOCKED skipped it */
        db_res_finalize (res);
        db_tx_rollback (db, &err);
        send_response_error (ctx, root, ERR_DB,
                             (err.message[0] ? err.message :
                              "Ship row unavailable (locked?)"));
        return 0;
      }

    player_current_sector_id = (int) db_res_col_i32 (res, 0, &err);
    ship_fighters_current = (int) db_res_col_i32 (res, 1, &err);
    ship_corp_id = (int) db_res_col_i32 (res, 2, &err);

    db_res_finalize (res);

    if (err.code != 0)
      {
        db_tx_rollback (db, &err);
        send_response_error (ctx, root, ERR_DB,
                             (err.message[0] ? err.message :
                              "Failed to decode ship row"));
        return 0;
      }
  }


  if (player_current_sector_id != from_sector_id)
    {
      db_tx_rollback (db, &err);

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
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Not enough fighters on ship to commit.");
      return 0;
    }

  /* 4) Lock + load mine stacks in target sector (FOR UPDATE SKIP LOCKED) */
  mine_stack_info_t *stacks = NULL;
  size_t stacks_cap = 0, stacks_len = 0;

  int total_hostile = 0;
  int64_t current_time = (int64_t) time (NULL);

  {
    db_res_t *res = NULL;
    const char *sql_mines =
      "SELECT sector_assets_id as id, quantity, owner_id as player, corporation_id as corporation, ttl "
      "FROM sector_assets "
      "WHERE sector_id = $1 AND asset_type = $2 AND quantity > 0 "
      "FOR UPDATE SKIP LOCKED "
      "ORDER BY sector_assets_id ASC;";

    db_bind_t p[] = { db_bind_i32 (target_sector_id), db_bind_i32 (mine_type) };


    if (!db_query (db, sql_mines, p, 2, &res, &err))
      {
        db_tx_rollback (db, &err);
        send_response_error (ctx, root, ERR_DB,
                             (err.message[0] ? err.message :
                              "Failed to query mine stacks"));
        return 0;
      }

    while (db_res_step (res, &err))
      {
        mine_stack_info_t ms;


        ms.id = (int) db_res_col_i32 (res, 0, &err);
        ms.quantity = (int) db_res_col_i32 (res, 1, &err);
        ms.player = (int) db_res_col_i32 (res, 2, &err);
        ms.corporation = (int) db_res_col_i32 (res, 3, &err);
        ms.ttl = (int64_t) db_res_col_i64 (res, 4, &err);

        if (err.code != 0)
          {
            break;
          }

        sector_asset_t mine_asset_for_check = {
          .quantity = ms.quantity,
          .player = ms.player,
          .corporation = ms.corporation,
          .ttl = (time_t) ms.ttl
        };

        bool targetable = false;


        if (mine_type == ASSET_MINE)
          {
            targetable = is_asset_hostile (mine_asset_for_check.player,
                                           mine_asset_for_check.corporation,
                                           ctx->player_id, ship_corp_id);
          }
        else /* limpet */
          {
            /* Limpets always targetable for sweeping */
            targetable = true;
          }

        if (armid_stack_is_active (&mine_asset_for_check,
                                   (time_t) current_time) && targetable)
          {
            if (stacks_len == stacks_cap)
              {
                size_t new_cap = (stacks_cap == 0) ? 16 : stacks_cap * 2;
                mine_stack_info_t *new_mem =
                  (mine_stack_info_t *) realloc (stacks,
                                                 new_cap * sizeof (*stacks));


                if (!new_mem)
                  {
                    db_res_finalize (res);
                    free (stacks);
                    db_tx_rollback (db, &err);
                    send_response_error (ctx,
                                         root,
                                         ERR_SERVER_ERROR,
                                         "Out of memory");
                    return 0;
                  }
                stacks = new_mem;
                stacks_cap = new_cap;
              }

            stacks[stacks_len++] = ms;
            total_hostile += ms.quantity;
          }
      }

    db_res_finalize (res);

    if (err.code != 0)
      {
        free (stacks);
        db_tx_rollback (db, &err);
        send_response_error (ctx, root, ERR_DB,
                             (err.message[0] ? err.message :
                              "Mine iteration failed"));
        return 0;
      }
  }


  /* 5) If none targetable/active: reply removed=0 and stop (unchanged outcome) */
  if (total_hostile <= 0)
    {
      free (stacks);
      db_tx_rollback (db, &err); /* release ship lock etc */

      json_t *out = json_object ();


      json_object_set_new (out, "from_sector_id",
                           json_integer (from_sector_id));
      json_object_set_new (out, "target_sector_id",
                           json_integer (target_sector_id));
      json_object_set_new (out, "fighters_sent",
                           json_integer (fighters_committed));
      json_object_set_new (out, "fighters_lost",      json_integer (0));
      json_object_set_new (out, "mines_removed",      json_integer (0));
      json_object_set_new (out, "mines_remaining",    json_integer (0));
      json_object_set_new (out, "mine_type",
                           json_string (mine_type_name));
      json_object_set_new (out, "success",            json_boolean (false));

      send_response_ok_take (ctx, root, "combat.mines_swept_v1", &out);
      return 0;
    }

  /* 6) Compute mines_to_clear and fighters_lost (UNCHANGED) */
  int F = fighters_committed;
  int mines_to_clear = 0;
  int fighters_lost = 0;


  if (mine_type == ASSET_MINE)
    {
      double mean = g_armid_config.sweep.mines_per_fighter_avg;
      double var = g_armid_config.sweep.mines_per_fighter_var;
      double eff = mean + rand_range (-var, var);


      if (eff < 0)
        {
          eff = 0;
        }

      int raw_capacity = (int) floor (F * eff);

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

      fighters_lost =
        (int) ceil (mines_to_clear *
                    g_armid_config.sweep.fighter_loss_per_mine);
      if (fighters_lost > F)
        {
          fighters_lost = F;
        }
    }
  else /* limpet */
    {
      mines_to_clear = F * g_cfg.mines.limpet.sweep_rate_mines_per_fighter;
      if (mines_to_clear > total_hostile)
        {
          mines_to_clear = total_hostile;
        }
      if (mines_to_clear < 0)
        {
          mines_to_clear = 0;
        }

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

  /* 7) Apply removals to stacks (already locked) */
  int remaining_to_clear = mines_to_clear;
  int mines_removed = 0;


  for (size_t i = 0; i < stacks_len && remaining_to_clear > 0; i++)
    {
      int remove = MIN (remaining_to_clear, stacks[i].quantity);
      int new_qty = stacks[i].quantity - remove;


      mines_removed += remove;
      remaining_to_clear -= remove;

      if (new_qty > 0)
        {
          const char *sql_upd =
            "UPDATE sector_assets SET quantity = $1 WHERE sector_assets_id = $2;";
          db_bind_t p[] = { db_bind_i32 (new_qty), db_bind_i32 (stacks[i].id) };


          if (!db_exec (db, sql_upd, p, 2, &err))
            {
              free (stacks);
              db_tx_rollback (db, &err);
              send_response_error (ctx, root, ERR_DB,
                                   (err.message[0] ? err.message :
                                    "Failed to update mine stack"));
              return 0;
            }
        }
      else
        {
          const char *sql_del =
            "DELETE FROM sector_assets WHERE sector_assets_id = $1;";
          db_bind_t p[] = { db_bind_i32 (stacks[i].id) };


          if (!db_exec (db, sql_del, p, 1, &err))
            {
              free (stacks);
              db_tx_rollback (db, &err);
              send_response_error (ctx,
                                   root,
                                   ERR_DB,
                                   (err.message[0] ? err.message :
                                    "Failed to delete mine stack"));
              return 0;
            }
        }
    }

  free (stacks);

  /* 8) Update ship fighters (locked) */
  {
    const char *sql_ship_f =
      "UPDATE ships SET fighters = fighters - $1 WHERE ship_id = $2;";
    db_bind_t p[] = { db_bind_i32 (fighters_lost), db_bind_i32 (ship_id) };


    if (!db_exec (db, sql_ship_f, p, 2, &err))
      {
        db_tx_rollback (db, &err);
        send_response_error (ctx, root, ERR_DB,
                             (err.message[0] ? err.message :
                              "Failed to update ship fighters"));
        return 0;
      }
  }

  /* 9) Re-calc remaining mines (not locked; read is fine after our tx changes) */
  int mines_remaining_after_sweep = 0;
  {
    db_res_t *res = NULL;
    const char *sql_rem =
      "SELECT COALESCE(SUM(quantity),0) "
      "FROM sector_assets "
      "WHERE sector_id = $1 AND asset_type = $2 AND quantity > 0;";

    db_bind_t p[] = { db_bind_i32 (target_sector_id), db_bind_i32 (mine_type) };


    if (!db_query (db, sql_rem, p, 2, &res, &err))
      {
        db_tx_rollback (db, &err);
        send_response_error (ctx, root, ERR_DB,
                             (err.message[0] ? err.message :
                              "Failed to recalc remaining mines"));
        return 0;
      }

    if (db_res_step (res, &err))
      {
        mines_remaining_after_sweep = (int) db_res_col_i32 (res, 0, &err);
      }

    db_res_finalize (res);

    if (err.code != 0)
      {
        db_tx_rollback (db, &err);
        send_response_error (ctx, root, ERR_DB,
                             (err.message[0] ? err.message :
                              "Failed to decode remaining mines"));
        return 0;
      }
  }


  /* 10) Commit */
  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_DB,
                           (err.message[0] ? err.message : "Commit failed"));
      return 0;
    }

  /* Reply (unchanged fields/meaning; uses your newer names) */
  json_t *out = json_object ();


  json_object_set_new (out, "from_sector_id",   json_integer (from_sector_id));
  json_object_set_new (out, "target_sector_id",
                       json_integer (target_sector_id));
  json_object_set_new (out,
                       "fighters_sent",
                       json_integer (fighters_committed));
  json_object_set_new (out, "fighters_lost",    json_integer (fighters_lost));
  json_object_set_new (out, "mines_removed",    json_integer (mines_removed));
  json_object_set_new (out, "mines_remaining",
                       json_integer (mines_remaining_after_sweep));
  json_object_set_new (out, "mine_type",        json_string (mine_type_name));
  json_object_set_new (out, "success",
                       json_boolean (mines_removed > 0));

  send_response_ok_take (ctx, root, "combat.mines_swept_v1", &out);

  /* Optional broadcast (unchanged) */
  if (mines_removed > 0)
    {
      json_t *broadcast_data = json_object ();


      json_object_set_new (broadcast_data, "v",            json_integer (1));
      json_object_set_new (broadcast_data, "sweeper_id",
                           json_integer (ctx->player_id));
      json_object_set_new (broadcast_data, "sector_id",
                           json_integer (target_sector_id));
      json_object_set_new (broadcast_data, "mines_removed",
                           json_integer (mines_removed));
      json_object_set_new (broadcast_data, "fighters_lost",
                           json_integer (fighters_lost));
      json_object_set_new (broadcast_data, "mine_type",
                           json_string (mine_type_name));

      (void) db_log_engine_event ((long long) time (NULL),
                                  "combat.mines_swept",
                                  NULL,
                                  ctx->player_id,
                                  target_sector_id,
                                  broadcast_data,
                                  NULL);
    }

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
      send_response_error (ctx, root,
                           ERR_SERVICE_UNAVAILABLE,
                           "Database unavailable");
      return 0;
    }

  /* ---------- 1. Parse input ---------- */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx, root,
                           ERR_MISSING_FIELD,
                           "Missing required field: data");
      return 0;
    }

  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_asset_id = json_object_get (data, "asset_id");


  if (!json_is_integer (j_sector_id) || !json_is_integer (j_asset_id))
    {
      send_response_error (ctx, root,
                           ERR_CURSOR_INVALID,
                           "Missing or invalid sector_id / asset_id");
      return 0;
    }

  int sector_id = (int)json_integer_value (j_sector_id);
  int asset_id = (int)json_integer_value (j_asset_id);

  /* ---------- 2. Load ship + player state ---------- */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx, root,
                           ERR_SHIP_NOT_FOUND,
                           "No active ship");
      return 0;
    }

  const char *SQL_SHIP_STATE =
    "SELECT s.sector_id, s.fighters, st.maxfighters, COALESCE(cm.corporation_id,0) "
    "FROM ships s "
    "JOIN shiptypes st ON s.type_id = st.shiptypes_id "
    "LEFT JOIN corp_members cm ON cm.player_id = $1 "
    "WHERE s.ship_id = $2;";

  db_res_t *res = NULL;
  db_error_t err = {0};


  if (!db_query (db,
                 SQL_SHIP_STATE,
                 (db_bind_t[]){
    db_bind_i32 (ctx->player_id),
    db_bind_i32 (ship_id)
  },
                 2, &res, &err))
    {
      send_response_error (ctx, root, ERR_DB, err.message);
      return 0;
    }

  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      send_response_error (ctx, root,
                           ERR_SECTOR_NOT_FOUND,
                           "Ship state unavailable");
      return 0;
    }

  int ship_sector = db_res_col_i32 (res, 0, &err);
  int ship_fighters = db_res_col_i32 (res, 1, &err);
  int ship_fighters_max = db_res_col_i32 (res, 2, &err);
  int player_corp_id = db_res_col_i32 (res, 3, &err);


  db_res_finalize (res);

  if (ship_sector != sector_id)
    {
      json_t *d = json_object ();


      json_object_set_new (d, "reason", json_string ("not_in_sector"));
      send_response_refused_steal (ctx, root,
                                   REF_NOT_IN_SECTOR,
                                   "Not in sector", d);
      json_decref (d);
      return 0;
    }

  /* ---------- 3. Lock and load asset ---------- */
  const char *SQL_ASSET =
    "SELECT quantity, owner_id as player, corporation_id as corporation, offensive_setting "
    "FROM sector_assets "
    "WHERE sector_assets_id = $1 AND sector_id = $2 AND asset_type = 2 "
    "FOR UPDATE SKIP LOCKED;";


  if (!db_query (db,
                 SQL_ASSET,
                 (db_bind_t[]){
    db_bind_i32 (asset_id),
    db_bind_i32 (sector_id)
  },
                 2, &res, &err))
    {
      send_response_error (ctx, root, ERR_DB, err.message);
      return 0;
    }

  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      send_response_error (ctx, root,
                           ERR_SHIP_NOT_FOUND,
                           "Asset not found");
      return 0;
    }

  int asset_qty = db_res_col_i32 (res, 0, &err);
  int asset_player = db_res_col_i32 (res, 1, &err);
  int asset_corp = db_res_col_i32 (res, 2, &err);
  int asset_mode = db_res_col_i32 (res, 3, &err);


  db_res_finalize (res);

  /* ---------- 4. Ownership ---------- */
  bool is_owner =
    (asset_player == ctx->player_id) ||
    (asset_corp && asset_corp == player_corp_id);


  if (!is_owner)
    {
      json_t *d = json_object ();


      json_object_set_new (d, "reason", json_string ("not_owner"));
      send_response_refused_steal (ctx, root,
                                   ERR_TARGET_INVALID,
                                   "Not owner", d);
      json_decref (d);
      return 0;
    }

  /* ---------- 5. Capacity ---------- */
  int capacity = ship_fighters_max - ship_fighters;


  if (capacity <= 0)
    {
      json_t *d = json_object ();


      json_object_set_new (d, "reason", json_string ("no_capacity"));
      send_response_refused_steal (ctx, root,
                                   ERR_OUT_OF_RANGE,
                                   "No capacity", d);
      json_decref (d);
      return 0;
    }

  int take = (asset_qty < capacity) ? asset_qty : capacity;


  if (take <= 0)
    {
      return 0;
    }

  /* ---------- 6. Transaction ---------- */
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      send_response_error (ctx, root, ERR_DB, err.message);
      return 0;
    }

  if (!db_exec (db,
                "UPDATE ships SET fighters = fighters + $1 WHERE ship_id = $2;",
                (db_bind_t[]){
    db_bind_i32 (take),
    db_bind_i32 (ship_id)
  }, 2, &err))
    {
      goto rollback;
    }

  if (take == asset_qty)
    {
      if (!db_exec (db,
                    "DELETE FROM sector_assets WHERE sector_assets_id = $1;",
                    (db_bind_t[]){ db_bind_i32 (asset_id) },
                    1, &err))
        {
          goto rollback;
        }
    }
  else
    {
      if (!db_exec (db,
                    "UPDATE sector_assets "
                    "SET quantity = quantity - $1 WHERE sector_assets_id = $2;",
                    (db_bind_t[]){
      db_bind_i32 (take),
      db_bind_i32 (asset_id)
    }, 2, &err))
        {
          goto rollback;
        }
    }

  if (!db_tx_commit (db, &err))
    {
      goto rollback;
    }

  /* ---------- 7. Event ---------- */
  json_t *evt = json_object ();


  json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
  json_object_set_new (evt, "ship_id", json_integer (ship_id));
  json_object_set_new (evt, "sector_id", json_integer (sector_id));
  json_object_set_new (evt, "asset_id", json_integer (asset_id));
  json_object_set_new (evt, "recalled", json_integer (take));
  json_object_set_new (evt, "remaining_in_sector",
                       json_integer (asset_qty - take));

  const char *mode =
    (asset_mode == 1) ? "offensive" :
    (asset_mode == 2) ? "defensive" :
    (asset_mode == 3) ? "toll" : "unknown";


  json_object_set_new (evt, "mode", json_string (mode));

  db_log_engine_event ((long long)time (NULL),
                       "fighters.recalled",
                       NULL,
                       ctx->player_id,
                       sector_id,
                       evt, NULL);

  /* ---------- 8. Response ---------- */
  json_t *out = json_object ();


  json_object_set_new (out, "sector_id", json_integer (sector_id));
  json_object_set_new (out, "asset_id", json_integer (asset_id));
  json_object_set_new (out, "recalled", json_integer (take));
  json_object_set_new (out, "remaining_in_sector",
                       json_integer (asset_qty - take));

  send_response_ok_take (ctx, root,
                         "combat.fighters.deployed", &out);
  return 0;

rollback:
  db_tx_rollback (db, NULL);
  send_response_error (ctx, root, ERR_DB,
                       err.message[0] ? err.message : "DB error");
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
      send_response_error (ctx, root,
                           ERR_SERVICE_UNAVAILABLE,
                           "Database unavailable");
      return 0;
    }

  /* 1. Input Parsing */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx, root,
                           ERR_MISSING_FIELD,
                           "Missing required field: data");
      return 0;
    }

  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_mine_type_str = json_object_get (data, "mine_type");
  json_t *j_asset_id = json_object_get (data, "asset_id");     /* optional */


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
  int asset_id = 0;


  if (j_asset_id && json_is_integer (j_asset_id))
    {
      asset_id = (int) json_integer_value (j_asset_id);
    }

  /* Only Limpet mines can be scrubbed */
  if (strcasecmp (mine_type_name, "limpet") != 0)
    {
      send_response_error (ctx, root,
                           ERR_INVALID_ARG,
                           "Only 'limpet' mine_type can be scrubbed.");
      return 0;
    }

  /* 2. Feature Gate */
  if (!g_cfg.mines.limpet.enabled)
    {
      send_response_refused_steal (ctx, root,
                                   ERR_LIMPETS_DISABLED,
                                   "Limpet mine operations are disabled.",
                                   NULL);
      return 0;
    }

  /* 3. Cost */
  const int scrub_cost = g_cfg.mines.limpet.scrub_cost;

  /* 4. Transaction: (a) delete eligible mines (b) debit cost */
  db_error_t err;


  db_error_clear (&err);

  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           err.message[0] ? err.message :
                           "Could not start transaction");
      return 0;
    }

  /* Delete (with SKIP LOCKED) and count rows deleted.
     Note: We lock rows in a CTE then delete those locked ids. */
  const char *SQL_DEL_ALL =
    "WITH locked AS ( "
    "  SELECT sector_assets_id as id "
    "  FROM sector_assets "
    "  WHERE sector_id = $1 "
    "    AND asset_type = $2 "
    "    AND (owner_id = $3 OR corporation_id = $4) "
    "  FOR UPDATE SKIP LOCKED "
    ") "
    "DELETE FROM sector_assets sa "
    "USING locked "
    "WHERE sa.sector_assets_id = locked.id "
    "RETURNING sa.sector_assets_id;";

  const char *SQL_DEL_ONE =
    "WITH locked AS ( "
    "  SELECT sector_assets_id as id "
    "  FROM sector_assets "
    "  WHERE sector_assets_id = $1 "
    "    AND sector_id = $2 "
    "    AND asset_type = $3 "
    "    AND (owner_id = $4 OR corporation_id = $5) "
    "  FOR UPDATE SKIP LOCKED "
    ") "
    "DELETE FROM sector_assets sa "
    "USING locked "
    "WHERE sa.sector_assets_id = locked.id "
    "RETURNING sa.sector_assets_id;";

  db_res_t *res = NULL;
  int total_scrubbed = 0;


  if (asset_id > 0)
    {
      if (!db_query (db,
                     SQL_DEL_ONE,
                     (db_bind_t[]){
      db_bind_i32 (asset_id),
      db_bind_i32 (sector_id),
      db_bind_i32 (ASSET_LIMPET_MINE),
      db_bind_i32 (ctx->player_id),
      db_bind_i32 (ctx->corp_id)
    },
                     5, &res, &err))
        {
          goto rollback_db;
        }

      while (db_res_step (res, &err))
        {
          total_scrubbed++;
        }

      db_res_finalize (res);
      res = NULL;

      if (err.code != 0)
        {
          goto rollback_db;
        }
    }
  else
    {
      if (!db_query (db,
                     SQL_DEL_ALL,
                     (db_bind_t[]){
      db_bind_i32 (sector_id),
      db_bind_i32 (ASSET_LIMPET_MINE),
      db_bind_i32 (ctx->player_id),
      db_bind_i32 (ctx->corp_id)
    },
                     4, &res, &err))
        {
          goto rollback_db;
        }

      while (db_res_step (res, &err))
        {
          total_scrubbed++;
        }

      db_res_finalize (res);
      res = NULL;

      if (err.code != 0)
        {
          goto rollback_db;
        }
    }

  if (total_scrubbed == 0)
    {
      (void) db_tx_rollback (db, NULL);
      send_response_refused_steal (ctx, root,
                                   ERR_NOT_FOUND,
                                   "No Limpet mines found to scrub.",
                                   NULL);
      return 0;
    }

  /* Debit Credits (if applicable) — do it atomically in-tx */
  if (scrub_cost > 0)
    {
      const char *SQL_DEBIT =
        "UPDATE players "
        "SET credits = credits - $1 "
        "WHERE player_id = $2 AND credits >= $1 "
        "RETURNING credits;";


      if (!db_query (db,
                     SQL_DEBIT,
                     (db_bind_t[]){
      db_bind_i32 (scrub_cost),
      db_bind_i32 (ctx->player_id)
    },
                     2, &res, &err))
        {
          goto rollback_db;
        }

      /* If no row returned => insufficient funds */
      if (!db_res_step (res, &err))
        {
          db_res_finalize (res);
          res = NULL;
          (void) db_tx_rollback (db, NULL);

          send_response_refused_steal (ctx, root,
                                       ERR_INSUFFICIENT_FUNDS,
                                       "Insufficient credits to scrub mines.",
                                       NULL);
          return 0;
        }

      /* Consume the single row; ignore returned credits (optional) */
      (void) db_res_col_i64 (res, 0, &err);
      db_res_finalize (res);
      res = NULL;

      if (err.code != 0)
        {
          goto rollback_db;
        }
    }

  if (!db_tx_commit (db, &err))
    {
      (void) db_tx_rollback (db, NULL);
      send_response_error (ctx, root,
                           ERR_DB,
                           err.message[0] ? err.message : "Commit failed");
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
                              "combat.mines_scrubbed",
                              NULL,
                              ctx->player_id,
                              sector_id,
                              evt, NULL);

  /* 7. Response */
  json_t *out = json_object ();


  json_object_set_new (out, "sector_id", json_integer (sector_id));
  json_object_set_new (out, "mine_type", json_string ("limpet"));
  json_object_set_new (out, "scrubbed_count", json_integer (total_scrubbed));
  json_object_set_new (out, "cost", json_integer (scrub_cost));
  if (asset_id > 0)
    {
      json_object_set_new (evt, "asset_id", json_integer (asset_id));
    }

  send_response_ok_take (ctx, root, "combat.mines_scrubbed_v1", &out);
  return 0;

rollback_db:
  if (res)
    {
      db_res_finalize (res);
    }
  (void) db_tx_rollback (db, NULL);

  send_response_error (ctx, root,
                       ERR_DB,
                       err.message[0] ? err.message : "DB error");
  return 0;
}


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
      send_response_error (ctx, root,
                           ERR_SERVICE_UNAVAILABLE,
                           "Database unavailable");
      return 0;
    }

  /* ---------- Input parsing ---------- */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx, root,
                           ERR_MISSING_FIELD,
                           "Missing required field: data");
      return 0;
    }

  json_t *j_amount = json_object_get (data, "amount");
  json_t *j_offense = json_object_get (data, "offense");
  json_t *j_corp_id = json_object_get (data, "corporation_id");
  json_t *j_mine_type = json_object_get (data, "mine_type");


  if (!j_amount || !json_is_integer (j_amount) ||
      !j_offense || !json_is_integer (j_offense))
    {
      send_response_error (ctx, root,
                           ERR_CURSOR_INVALID,
                           "Missing or invalid amount/offense");
      return 0;
    }

  int amount = (int)json_integer_value (j_amount);
  int offense = (int)json_integer_value (j_offense);
  int mine_type = ASSET_MINE;


  if (j_mine_type && json_is_integer (j_mine_type))
    {
      mine_type = (int)json_integer_value (j_mine_type);
      if (mine_type != ASSET_MINE && mine_type != ASSET_LIMPET_MINE)
        {
          send_response_error (ctx, root,
                               ERR_CURSOR_INVALID,
                               "Invalid mine_type");
          return 0;
        }
    }

  if (amount <= 0)
    {
      send_response_error (ctx, root,
                           ERR_NOT_FOUND,
                           "amount must be > 0");
      return 0;
    }

  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK)
    {
      send_response_error (ctx, root,
                           ERR_CURSOR_INVALID,
                           "Invalid offense");
      return 0;
    }

  /* ---------- Resolve ship + sector ---------- */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx, root,
                           ERR_SHIP_NOT_FOUND,
                           "No active ship");
      return 0;
    }

  (void)h_decloak_ship (db, ship_id);

  int sector_id = -1;
  {
    db_res_t *res = NULL;
    db_error_t err = {0};


    if (!db_query (db,
                   "SELECT sector_id FROM ships WHERE ship_id=$1",
                   (db_bind_t[]){ db_bind_i32 (ship_id) },
                   1,
                   &res,
                   &err))
      {
        send_response_error (ctx, root,
                             ERR_SECTOR_NOT_FOUND,
                             err.message);
        return 0;
      }

    if (db_res_step (res, &err))
      {
        sector_id = (int)db_res_col_i32 (res, 0, &err);
      }

    db_res_finalize (res);

    if (sector_id <= 0)
      {
        send_response_error (ctx, root,
                             ERR_SECTOR_NOT_FOUND,
                             "Unable to resolve sector");
        return 0;
      }
  }

  /* ---------- Sector cap check ---------- */
  int sector_total = 0;


  if (sum_sector_mines (db, sector_id, &sector_total) != 0)
    {
      send_response_error (ctx, root,
                           REF_NOT_IN_SECTOR,
                           "Failed to read sector mines");
      return 0;
    }

  if (sector_total + amount > SECTOR_MINE_CAP)
    {
      send_response_error (ctx, root,
                           ERR_SECTOR_OVERCROWDED,
                           "Sector mine limit exceeded");
      return 0;
    }

  /* ---------- Transaction ---------- */
  db_error_t err = {0};


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root,
                           ERR_DB,
                           err.message);
      return 0;
    }

  int rc = ship_consume_mines (db, ship_id, mine_type, amount);


  if (rc != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root,
                           REF_AMMO_DEPLETED,
                           "Insufficient mines on ship");
      return 0;
    }

  int64_t asset_id = -1;
  {
    const char *sql =
      "INSERT INTO sector_assets "
      "(sector_id, owner_id, corporation_id, asset_type, quantity, offensive_setting, deployed_at) "
      "VALUES ($1,$2,$3,$4,$5,$6,EXTRACT(EPOCH FROM NOW())) "
      "RETURNING sector_assets_id";


    if (!db_exec_insert_id (db,
                            sql,
                            (db_bind_t[]){
      db_bind_i32 (sector_id),
      db_bind_i32 (ctx->player_id),
      (j_corp_id && json_is_integer (j_corp_id))
              ? db_bind_i32 ((int)json_integer_value (j_corp_id))
              : db_bind_null (),
      db_bind_i32 (mine_type),
      db_bind_i32 (amount),
      db_bind_i32 (offense)
    },
                            6,
                            &asset_id,
                            &err))
      {
        db_tx_rollback (db, NULL);
        send_response_error (ctx, root,
                             ERR_DB,
                             err.message);
        return 0;
      }
  }


  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root,
                           ERR_DB,
                           err.message);
      return 0;
    }

  /* ---------- Federation / Stardock enforcement (unchanged logic) ---------- */
  bool in_fed = (sector_id >= 1 && sector_id <= 10);
  bool in_sdock = false;

  json_t *stardock_sectors = db_get_stardock_sectors ();


  if (stardock_sectors)
    {
      size_t i;
      json_t *v = NULL;


      json_array_foreach (stardock_sectors, i, v)
      {
        if (json_is_integer (v) &&
            json_integer_value (v) == sector_id)
          {
            in_sdock = true;
            break;
          }
      }
      json_decref (stardock_sectors);
    }

  if (in_fed || in_sdock)
    {
      iss_summon (sector_id, ctx->player_id);
      h_send_message_to_player (db,
                                ctx->player_id,
                                0,
                                "Federation Warning",
                                "Mine deployment in protected space has triggered ISS response.");
    }

  /* ---------- Engine event ---------- */
  {
    json_t *evt = json_object ();


    json_object_set_new (evt, "sector_id", json_integer (sector_id));
    json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
    json_object_set_new (evt, "amount", json_integer (amount));
    json_object_set_new (evt, "offense", json_integer (offense));
    json_object_set_new (evt, "mine_type", json_integer (mine_type));
    json_object_set_new (evt, "asset_id", json_integer (asset_id));

    db_log_engine_event (time (NULL),
                         "combat.mines.deployed",
                         NULL,
                         ctx->player_id,
                         sector_id,
                         evt,
                         NULL);
  }

  /* ---------- Response ---------- */
  sum_sector_mines (db, sector_id, &sector_total);

  json_t *out = json_object ();


  json_object_set_new (out, "sector_id", json_integer (sector_id));
  json_object_set_new (out, "owner_player_id", json_integer (ctx->player_id));
  json_object_set_new (out, "amount", json_integer (amount));
  json_object_set_new (out, "offense", json_integer (offense));
  json_object_set_new (out, "mine_type", json_integer (mine_type));
  json_object_set_new (out, "sector_total_after", json_integer (sector_total));
  json_object_set_new (out, "asset_id", json_integer (asset_id));

  send_response_ok_take (ctx, root,
                         "combat.mines.deployed",
                         &out);
  return 0;
}


int
cmd_combat_lay_mines (client_ctx_t *ctx, json_t *root)
{
  return cmd_combat_deploy_mines (ctx, root);
}


/* Helper to get full ship combat stats */
static int
load_ship_combat_stats_locked (db_t *db, int ship_id, combat_ship_t *out,
                               bool skip_locked)
{
  if (!db || !out || ship_id <= 0)
    {
      return -1;
    }

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  const char *sql =
    "SELECT s.ship_id, s.hull, s.shields, s.fighters, s.sector_id, s.name, "
    "       st.offense, st.defense, st.maxattack, "
    "       op.player_id, cm.corporation_id "
    "FROM ships s "
    "JOIN shiptypes st ON s.type_id = st.shiptypes_id "
    "JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE "
    "LEFT JOIN corp_members cm ON cm.player_id = op.player_id "
    "WHERE s.ship_id = $1 "
    "FOR UPDATE ";

  const char *sql_skip =
    "SELECT s.ship_id, s.hull, s.shields, s.fighters, s.sector_id, s.name, "
    "       st.offense, st.defense, st.maxattack, "
    "       op.player_id, cm.corporation_id "
    "FROM ships s "
    "JOIN shiptypes st ON s.type_id = st.shiptypes_id "
    "JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE "
    "LEFT JOIN corp_members cm ON cm.player_id = op.player_id "
    "WHERE s.ship_id = $1 "
    "FOR UPDATE SKIP LOCKED";


  if (!db_query (db,
                 skip_locked ? sql_skip : sql,
                 (db_bind_t[]){ db_bind_i32 (ship_id) },
                 1,
                 &res,
                 &err))
    {
      /* keep it simple: caller maps err->response */
      return -1;
    }

  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      return -1; /* not found or locked (SKIP LOCKED) */
    }

  /* decode row */
  memset (out, 0, sizeof(*out));

  out->id = (int)db_res_col_i32 (res, 0, &err);
  out->hull = (int)db_res_col_i32 (res, 1, &err);
  out->shields = (int)db_res_col_i32 (res, 2, &err);
  out->fighters = (int)db_res_col_i32 (res, 3, &err);
  out->sector = (int)db_res_col_i32 (res, 4, &err);

  const char *nm = db_res_col_is_null (res, 5) ? NULL : db_res_col_text (res,
                                                                         5,
                                                                         &err);


  if (nm)
    {
      strncpy (out->name, nm, sizeof(out->name) - 1);
      out->name[sizeof(out->name) - 1] = '\0';
    }
  else
    {
      out->name[0] = '\0';
    }

  out->attack_power = (int)db_res_col_i32 (res, 6, &err);
  out->defense_power = (int)db_res_col_i32 (res, 7, &err);
  out->max_attack = (int)db_res_col_i32 (res, 8, &err);
  out->player_id = (int)db_res_col_i32 (res, 9, &err);
  out->corp_id = db_res_col_is_null (res,10) ? 0 : (int)db_res_col_i32 (res,
                                                                        10,
                                                                        &err);

  db_res_finalize (res);

  if (err.code != 0)
    {
      return -1;
    }

  return 0;
}


static int
persist_ship_damage (db_t *db, combat_ship_t *ship, int fighters_lost)
{
  if (!db || !ship || ship->id <= 0)
    {
      return -1;
    }

  db_error_t err;


  db_error_clear (&err);

  const int hull = MAX (0, ship->hull);
  const int shields = MAX (0, ship->shields);

  const char *sql =
    "UPDATE ships "
    "SET hull = $1, "
    "    shields = $2, "
    "    fighters = GREATEST(0, fighters - $3) "
    "WHERE ship_id = $4";

  int64_t rows = 0;


  if (!db_exec_rows_affected (db,
                              sql,
                              (db_bind_t[]){
    db_bind_i32 (hull),
    db_bind_i32 (shields),
    db_bind_i32 (fighters_lost),
    db_bind_i32 (ship->id)
  },
                              4,
                              &rows,
                              &err))
    {
      return -1;
    }

  return (rows == 1) ? 0 : -1;
}


int
handle_ship_attack (client_ctx_t *ctx,
                    json_t *root,
                    json_t *data,
                    db_t *db)
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

  int attacker_ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (attacker_ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }

  db_error_t err;


  db_error_clear (&err);

  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      send_response_error (ctx, root, ERR_DB,
                           (err.message[0] ? err.message :
                            "Could not start transaction"));
      return 0;
    }

  combat_ship_t attacker = {0};
  combat_ship_t defender = {0};


  /* Lock attacker first (deterministic lock order helps) */
  if (load_ship_combat_stats_locked (db, attacker_ship_id, &attacker,
                                     true) != 0)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           ERR_SHIP_NOT_FOUND,
                           "Attacker ship not found or busy");
      return 0;
    }

  if (load_ship_combat_stats_locked (db, target_ship_id, &defender, true) != 0)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           ERR_SHIP_NOT_FOUND,
                           "Target ship not found or busy");
      return 0;
    }

  /* Validate Sector (must be co-located) */
  int att_sector = attacker.sector;
  int def_sector = defender.sector;


  if (att_sector != def_sector || att_sector <= 0)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx,
                           root,
                           ERR_TARGET_INVALID,
                           "Target not in same sector");
      return 0;
    }

  /* --- Round 1: Attacker Strikes --- */
  int att_committed = attacker.max_attack;


  if (req_fighters > 0)
    {
      att_committed = MIN (req_fighters, attacker.max_attack);
    }
  att_committed = MIN (att_committed, attacker.fighters);
  if (att_committed < 0)
    {
      att_committed = 0;
    }

  double att_mult = 1.0 + ((double)attacker.attack_power * OFFENSE_SCALE);
  int att_raw_dmg = att_committed * DAMAGE_PER_FIGHTER;
  int att_total_dmg = (int)(att_raw_dmg * att_mult);

  double def_factor = 1.0 + ((double)defender.defense_power * DEFENSE_SCALE);
  int effective_dmg_to_def = (int)(att_total_dmg / def_factor);

  int def_shields_lost = 0, def_hull_lost = 0;


  apply_combat_damage (&defender,
                       effective_dmg_to_def,
                       &def_shields_lost,
                       &def_hull_lost);

  bool defender_destroyed = (defender.hull <= 0);


  if (!defender_destroyed)
    {
      if (persist_ship_damage (db, &defender, 0) != 0)
        {
          db_tx_rollback (db, &err);
          send_response_error (ctx,
                               root,
                               ERR_DB,
                               "Failed to persist defender damage");
          return 0;
        }
    }
  else
    {
      /* destroy reads/writes a bunch; keep it inside the tx if it uses db_t */
      destroy_ship_and_handle_side_effects (NULL, defender.player_id);
    }

  /* --- Round 2: Defender Counter-Fire (if alive) --- */
  int att_shields_lost = 0, att_hull_lost = 0;
  bool attacker_destroyed = false;


  if (!defender_destroyed)
    {
      int def_committed = MIN (defender.fighters, defender.max_attack);

      double def_mult = 1.0 + ((double)defender.attack_power * OFFENSE_SCALE);
      int def_raw_dmg = def_committed * DAMAGE_PER_FIGHTER;
      int def_total_dmg = (int)(def_raw_dmg * def_mult);

      double att_def_factor = 1.0 +
                              ((double)attacker.defense_power * DEFENSE_SCALE);
      int effective_dmg_to_att = (int)(def_total_dmg / att_def_factor);


      apply_combat_damage (&attacker,
                           effective_dmg_to_att,
                           &att_shields_lost,
                           &att_hull_lost);
      attacker_destroyed = (attacker.hull <= 0);

      if (!attacker_destroyed)
        {
          if (persist_ship_damage (db, &attacker, 0) != 0)
            {
              db_tx_rollback (db, &err);
              send_response_error (ctx,
                                   root,
                                   ERR_DB,
                                   "Failed to persist attacker damage");
              return 0;
            }
        }
      else
        {
          destroy_ship_and_handle_side_effects (ctx, attacker.player_id);
        }
    }

  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_DB,
                           (err.message[0] ? err.message : "Commit failed"));
      return 0;
    }

  /* Response (unchanged) */
  json_t *resp = json_object ();


  json_object_set_new (resp, "fighters_committed",
                       json_integer (att_committed));
  json_object_set_new (resp, "damage_dealt",
                       json_integer (effective_dmg_to_def));
  json_object_set_new (resp, "damage_received",
                       json_integer (defender_destroyed ? 0 : att_hull_lost +
                                     att_shields_lost));
  json_object_set_new (resp, "defender_destroyed",
                       json_boolean (defender_destroyed));
  json_object_set_new (resp, "attacker_destroyed",
                       json_boolean (attacker_destroyed));

  send_response_ok_take (ctx, root, "combat.attack.result", &resp);
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
      send_response_error (ctx,
                           root,
                           ERR_SERVICE_UNAVAILABLE,
                           "Database unavailable");
      return 0;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship");
      return 0;
    }

  combat_ship_t ship = {0};


  /* Read-only load: NO FOR UPDATE */
  if (load_ship_combat_stats_unlocked (db, ship_id, &ship) != 0)
    {
      send_response_error (ctx, root, ERR_DB, "Failed to load ship stats");
      return 0;
    }

  json_t *res = json_object ();


  json_object_set_new (res, "hull",          json_integer (ship.hull));
  json_object_set_new (res, "shields",       json_integer (ship.shields));
  json_object_set_new (res, "fighters",      json_integer (ship.fighters));
  json_object_set_new (res, "attack_power",  json_integer (ship.attack_power));
  json_object_set_new (res, "defense_power", json_integer (ship.defense_power));
  json_object_set_new (res, "max_attack",    json_integer (ship.max_attack));

  send_response_ok_take (ctx, root, "combat.status", &res);
  return 0;
}


static int
load_ship_combat_stats_unlocked (db_t *db, int ship_id, combat_ship_t *out)
{
  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear (&err);

  const char *sql =
    "SELECT s.ship_id, s.hull, s.shields, s.fighters, s.sector_id, s.name, "
    "       st.offense, st.defense, st.maxattack, "
    "       op.player_id, cm.corporation_id "
    "FROM ships s "
    "JOIN shiptypes st ON s.type_id = st.shiptypes_id "
    "JOIN ship_ownership op ON op.ship_id = s.ship_id AND op.is_primary = TRUE "
    "LEFT JOIN corp_members cm ON cm.player_id = op.player_id "
    "WHERE s.ship_id = $1";


  if (!db_query (db, sql, (db_bind_t[]){ db_bind_i32 (ship_id) }, 1, &res,
                 &err))
    {
      return -1;
    }

  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      return -1;
    }

  memset (out, 0, sizeof(*out));
  out->id = (int)db_res_col_i32 (res, 0, &err);
  out->hull = (int)db_res_col_i32 (res, 1, &err);
  out->shields = (int)db_res_col_i32 (res, 2, &err);
  out->fighters = (int)db_res_col_i32 (res, 3, &err);
  out->sector = (int)db_res_col_i32 (res, 4, &err);

  const char *nm = db_res_col_is_null (res, 5) ? NULL : db_res_col_text (res,
                                                                         5,
                                                                         &err);


  if (nm)
    {
      strncpy (out->name, nm, sizeof(out->name) - 1);
      out->name[sizeof(out->name) - 1] = '\0';
    }

  out->attack_power = (int)db_res_col_i32 (res, 6, &err);
  out->defense_power = (int)db_res_col_i32 (res, 7, &err);
  out->max_attack = (int)db_res_col_i32 (res, 8, &err);
  out->player_id = (int)db_res_col_i32 (res, 9, &err);
  out->corp_id = db_res_col_is_null (res,10) ? 0 : (int)db_res_col_i32 (res,
                                                                        10,
                                                                        &err);

  db_res_finalize (res);
  return (err.code == 0) ? 0 : -1;
}


static int
h_apply_quasar_damage (client_ctx_t *ctx, int damage, const char *source_desc)
{
  if (damage <= 0)
    {
      return 0;
    }

  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return 0;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      return 0;
    }

  db_error_t err;


  db_error_clear (&err);

  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      return 0;
    }

  combat_ship_t ship = {0};


  /* We mutate; lock row (skip locked) */
  if (load_ship_combat_stats_locked (db, ship_id, &ship, true) != 0)
    {
      db_tx_rollback (db, &err);
      return 0;
    }

  int remaining = damage;
  int fighters_lost = 0;
  int shields_lost = 0;
  int hull_lost = 0;


  if (ship.fighters > 0)
    {
      int absorb = MIN (remaining, ship.fighters);


      ship.fighters -= absorb;
      remaining -= absorb;
      fighters_lost = absorb;
    }

  if (remaining > 0 && ship.shields > 0)
    {
      int absorb = MIN (remaining, ship.shields);


      ship.shields -= absorb;
      remaining -= absorb;
      shields_lost = absorb;
    }

  if (remaining > 0)
    {
      ship.hull -= remaining;
      hull_lost = remaining;
    }

  if (persist_ship_damage (db, &ship, fighters_lost) != 0)
    {
      db_tx_rollback (db, &err);
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, &err);
      return 0;
    }

  json_t *hit_data = json_object ();


  json_object_set_new (hit_data, "damage_total",  json_integer (damage));
  json_object_set_new (hit_data, "fighters_lost", json_integer (fighters_lost));
  json_object_set_new (hit_data, "shields_lost",  json_integer (shields_lost));
  json_object_set_new (hit_data, "hull_lost",     json_integer (hull_lost));
  json_object_set_new (hit_data, "source",
                       json_string (source_desc ? source_desc : ""));

  db_log_engine_event ((long long)time (NULL),
                       "combat.hit",
                       "player",
                       ctx->player_id,
                       ship.sector,
                       hit_data,
                       NULL);

  if (ship.hull <= 0)
    {
      destroy_ship_and_handle_side_effects (ctx, ctx->player_id);
      return 1;
    }

  return 0;
}


static void
apply_combat_damage (combat_ship_t *target,
                     int damage, int *shields_lost, int *hull_lost)
{
  *shields_lost = 0;
  *hull_lost = 0;

  if (!target || damage <= 0)
    {
      return;
    }

  int remaining = damage;


  if (target->shields > 0)
    {
      int absorb = MIN (remaining, target->shields);


      target->shields -= absorb;
      remaining -= absorb;
      *shields_lost = absorb;
    }

  if (remaining > 0)
    {
      target->hull -= remaining;
      *hull_lost = remaining;
    }
}


json_t *
db_get_stardock_sectors (void)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return NULL;
    }

  json_t *sector_list = json_array ();


  if (!sector_list)
    {
      LOGE ("ERROR: Failed to allocate JSON array for stardock sectors.");
      return NULL;
    }

  const char *sql = "SELECT sector_id FROM stardock_location;";
  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  if (!db_query (db, sql, NULL, 0, &res, &err))
    {
      LOGE ("DB Error: Could not query stardock sectors: %s", err.message);
      json_decref (sector_list);
      return NULL;
    }

  while (db_res_step (res, &err))
    {
      int sector_id = (int)db_res_col_i32 (res, 0, &err);


      json_array_append_new (sector_list, json_integer (sector_id));
    }

  db_res_finalize (res);

  if (err.code != 0)
    {
      LOGE ("DB Error: Iteration failed: %s", err.message);
      json_decref (sector_list);
      return NULL;
    }

  return sector_list;
}


#include "server_ships.h"


int
destroy_ship_and_handle_side_effects (client_ctx_t *ctx, int player_id)
{
  return 0;
}


int
h_decloak_ship (db_t *db, int ship_id)
{
  return 0;
}


int
h_handle_sector_entry_hazards (db_t *db, client_ctx_t *ctx, int sector_id)
{
  if (!db || !ctx || sector_id <= 0)
    return 0;

  db_error_t err = {0};
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;

  /* Config: toll and damage per unit of fighters */
  long long toll_per_unit = 5;
  long long damage_per_unit = 10;

  /* Query fighters in sector (asset_type=2) */
  const char *sql_ftr =
    "SELECT sector_assets_id as id, quantity, offensive_setting, owner_id as player, "
    "       corporation_id as corporation "
    "FROM sector_assets "
    "WHERE sector_id = $1 AND asset_type = 2 AND quantity > 0 "
    "FOR UPDATE SKIP LOCKED;";

  db_res_t *res = NULL;
  if (!db_query (db, sql_ftr, (db_bind_t[]){ db_bind_i32 (sector_id) }, 1, &res, &err))
    {
      return 0;
    }

  int result = 0;
  while (db_res_step (res, &err))
    {
      int asset_id = (int)db_res_col_i32 (res, 0, &err);
      int quantity = (int)db_res_col_i32 (res, 1, &err);
      int mode = (int)db_res_col_i32 (res, 2, &err);  /* 1=offensive, 2=defensive, 3=toll */
      int owner_id = (int)db_res_col_i32 (res, 3, &err);
      int corp_id = (int)db_res_col_i32 (res, 4, &err);

      /* Check if fighters are hostile to player */
      if (!is_asset_hostile (owner_id, corp_id, ctx->player_id, ctx->corp_id))
        continue;

      bool attack = true;

      /* Toll Mode (mode == 3) */
      if (mode == 3)
        {
          long long toll_cost = (long long)quantity * toll_per_unit;
          long long player_creds = 0;

          h_get_player_petty_cash (db, ctx->player_id, &player_creds);

          if (player_creds >= toll_cost)
            {
              /* Auto-pay toll */
              int dest_id = (corp_id > 0 && owner_id == 0) ? corp_id : owner_id;
              const char *dest_type = (corp_id > 0 && owner_id == 0) ? "corp" : "player";

              int transfer_rc = db_bank_transfer (db,
                                                   "player",
                                                   ctx->player_id,
                                                   dest_type,
                                                   dest_id,
                                                   toll_cost);

              if (transfer_rc == 0)
                {
                  attack = false;

                  /* Log toll payment */
                  json_t *evt = json_object ();
                  json_object_set_new (evt, "amount", json_integer (toll_cost));
                  json_object_set_new (evt, "fighters", json_integer (quantity));
                  db_log_engine_event ((long long)time (NULL),
                                       "combat.toll.paid",
                                       "player",
                                       ctx->player_id, sector_id, evt, NULL);
                  json_decref (evt);
                }
            }
        }

      /* Attack if not paid toll or mode != 3 */
      if (attack)
        {
          /* Load player ship stats */
          ship_t ship = {0};
          const char *sql_ship =
            "SELECT hull, fighters, shields FROM ships WHERE ship_id = $1;";
          db_res_t *ship_res = NULL;

          if (db_query (db, sql_ship, (db_bind_t[]){ db_bind_i32 (ship_id) }, 1, &ship_res, &err))
            {
              if (db_res_step (ship_res, &err))
                {
                  ship.hull = (int)db_res_col_i32 (ship_res, 0, &err);
                  ship.fighters = (int)db_res_col_i32 (ship_res, 1, &err);
                  ship.shields = (int)db_res_col_i32 (ship_res, 2, &err);
                }
              db_res_finalize (ship_res);
            }

          /* Apply damage: fighters -> shields -> hull */
          int damage = quantity * damage_per_unit;
          armid_damage_breakdown_t breakdown = {0};
          apply_armid_damage_to_ship (&ship, damage, &breakdown);

          /* Update ship in DB */
          const char *sql_upd =
            "UPDATE ships SET hull = $1, fighters = $2, shields = $3 WHERE ship_id = $4;";
          db_exec (db,
                   sql_upd,
                   (db_bind_t[]){ db_bind_i32 (ship.hull),
                                  db_bind_i32 (ship.fighters),
                                  db_bind_i32 (ship.shields),
                                  db_bind_i32 (ship_id) },
                   4,
                   &err);

          /* Delete fighter asset (one-time attack) */
          const char *sql_del =
            "DELETE FROM sector_assets WHERE sector_assets_id = $1;";
          db_exec (db, sql_del, (db_bind_t[]){ db_bind_i32 (asset_id) }, 1, &err);

          /* Log attack */
          json_t *evt = json_object ();
          json_object_set_new (evt, "damage", json_integer (damage));
          json_object_set_new (evt, "fighters_engaged", json_integer (quantity));
          json_object_set_new (evt, "hull_remaining", json_integer (ship.hull));
          db_log_engine_event ((long long)time (NULL),
                               "combat.hit.fighters",
                               "player",
                               ctx->player_id, sector_id, evt, NULL);
          json_decref (evt);

          /* Check if ship destroyed */
          if (ship.hull <= 0)
            {
              db_res_finalize (res);
              destroy_ship_and_handle_side_effects (ctx, ctx->player_id);
              return 1;  /* Destroyed */
            }
        }
    }

  db_res_finalize (res);
  return result;
}


int
h_trigger_atmosphere_quasar (db_t *db, client_ctx_t *ctx, int planet_id)
{
  if (!db || !ctx || planet_id <= 0)
    return 0;

  db_error_t err = {0};

  /* Get planet and citadel info (for atmosphere quasar) */
  const char *sql =
    "SELECT p.owner_id, p.owner_type, c.level, c.qCannonAtmosphere, c.militaryReactionLevel "
    "FROM planets p "
    "LEFT JOIN citadels c ON p.planet_id = c.planet_id "
    "WHERE p.planet_id = $1 AND c.level >= 3 AND c.qCannonAtmosphere > 0;";

  db_res_t *res = NULL;
  if (!db_query (db, sql, (db_bind_t[]){ db_bind_i32 (planet_id) }, 1, &res, &err))
    return 0;

  int result = 0;
  if (db_res_step (res, &err))
    {
      int owner_id = (int)db_res_col_i32 (res, 0, &err);
      const char *owner_type = db_res_col_text (res, 1, &err);
      int base_strength = (int)db_res_col_i32 (res, 3, &err);
      int reaction = (int)db_res_col_i32 (res, 4, &err);

      int p_corp_id = 0;
      if (owner_type && (strcasecmp (owner_type, "corp") == 0 ||
                         strcasecmp (owner_type, "corporation") == 0))
        {
          p_corp_id = owner_id;
        }

      /* Check if player is hostile to planet owner */
      if (is_asset_hostile (owner_id, p_corp_id, ctx->player_id, ctx->corp_id))
        {
          /* Reaction modifies damage */
          int pct = 100;
          if (reaction == 1)
            pct = 125;
          else if (reaction >= 2)
            pct = 150;

          int damage = (int)floor ((double)base_strength * (double)pct / 100.0);

          char source_desc[64];
          snprintf (source_desc, sizeof (source_desc),
                    "Quasar Atmosphere Shot (Planet %d)", planet_id);

          if (h_apply_quasar_damage (ctx, damage, source_desc))
            {
              result = 1;  /* Destroyed */
            }
        }
    }

  db_res_finalize (res);
  return result;
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
                           ERR_SERVICE_UNAVAILABLE,
                           "Database unavailable");
      return 0;
    }

  /* 1. Parse input */
  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD,
                           "Missing required field: data");
      return 0;
    }
  json_t *j_sector_id = json_object_get (data, "sector_id");
  json_t *j_asset_id = json_object_get (data, "asset_id");


  if (!j_sector_id || !json_is_integer (j_sector_id) || !j_asset_id ||
      !json_is_integer (j_asset_id))
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

  int player_current_sector_id = -1;
  int ship_mines_current = 0;
  int ship_mines_max = 0;

  {
    const char *sql_player_ship =
      "SELECT s.sector_id, s.mines, st.maxmines FROM ships s JOIN shiptypes st ON s.type_id = st.shiptypes_id WHERE s.ship_id = $1;";
    db_res_t *res = NULL;
    db_error_t err;


    memset (&err, 0, sizeof (err));

    if (db_query (db, sql_player_ship,
                  (db_bind_t[]){ db_bind_i32 (player_ship_id) }, 1, &res, &err))
      {
        if (db_res_step (res, &err))
          {
            player_current_sector_id = db_res_col_i32 (res, 0, &err);
            ship_mines_current = db_res_col_i32 (res, 1, &err);
            ship_mines_max = db_res_col_i32 (res, 2, &err);
          }
        db_res_finalize (res);
      }

    if (player_current_sector_id <= 0)
      {
        send_response_error (ctx,
                             root,
                             ERR_SECTOR_NOT_FOUND,
                             "Could not determine player's current sector.");
        return 0;
      }
  }

  /* 3. Verify asset belongs to player and is in current sector */
  int asset_quantity = 0;
  int asset_type = 0;
  {
    const char *sql_asset =
      "SELECT quantity, asset_type FROM sector_assets WHERE sector_assets_id = $1 AND owner_id = $2 AND sector_id = $3 AND asset_type IN (1, 4);";
    db_res_t *res = NULL;
    db_error_t err;


    memset (&err, 0, sizeof (err));

    db_bind_t params[] = { db_bind_i32 (asset_id), db_bind_i32 (ctx->player_id),
                           db_bind_i32 (requested_sector_id) };


    if (db_query (db, sql_asset, params, 3, &res, &err))
      {
        if (db_res_step (res, &err))
          {
            asset_quantity = db_res_col_i32 (res, 0, &err);
            asset_type = db_res_col_i32 (res, 1, &err);
          }
        db_res_finalize (res);
      }

    if (asset_quantity <= 0)
      {
        send_response_error (ctx,
                             root,
                             ERR_NOT_FOUND,
                             "Mine asset not found or does not belong to you in this sector.");
        return 0;
      }
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
  {
    db_error_t dberr;


    memset (&dberr, 0, sizeof (dberr));

    if (!db_tx_begin (db, DB_TX_IMMEDIATE, &dberr))
      {
        send_response_error (ctx, root, ERR_DB, "Could not start transaction");
        return 0;
      }

    const char *sql_delete =
      "DELETE FROM sector_assets WHERE sector_assets_id = $1;";
    db_bind_t del_params[] = { db_bind_i32 (asset_id) };


    if (!db_exec (db, sql_delete, del_params, 1, &dberr))
      {
        (void) db_tx_rollback (db, &dberr);
        send_response_error (ctx,
                             root,
                             ERR_DB,
                             "Failed to delete asset from sector.");
        return 0;
      }

    const char *sql_credit =
      "UPDATE ships SET mines = mines + $1 WHERE ship_id = $2;";
    db_bind_t credit_params[] = { db_bind_i32 (asset_quantity),
                                  db_bind_i32 (player_ship_id) };


    if (!db_exec (db, sql_credit, credit_params, 2, &dberr))
      {
        (void) db_tx_rollback (db, &dberr);
        send_response_error (ctx,
                             root,
                             ERR_DB,
                             "Failed to credit mines to ship.");
        return 0;
      }

    if (!db_tx_commit (db, &dberr))
      {
        (void) db_tx_rollback (db, &dberr);
        send_response_error (ctx, root, ERR_DB, "Commit failed");
        return 0;
      }
  }

  /* 6. Emit engine_event via db_log_engine_event */
  {
    json_t *evt = json_object ();


    json_object_set_new (evt, "sector_id", json_integer (requested_sector_id));
    json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
    json_object_set_new (evt, "asset_id", json_integer (asset_id));
    json_object_set_new (evt, "amount", json_integer (asset_quantity));
    json_object_set_new (evt, "asset_type", json_integer (asset_type));
    json_object_set_new (evt,
                         "event_ts",
                         json_integer ((json_int_t) time (NULL)));
    (void) db_log_engine_event ((long long) time (NULL),
                                "mines.recalled",
                                NULL,
                                ctx->player_id,
                                requested_sector_id,
                                evt,
                                NULL);
  }

  /* 7. Send enveloped_ok response */
  json_t *out = json_object ();


  json_object_set_new (out, "sector_id", json_integer (requested_sector_id));
  json_object_set_new (out, "asset_id", json_integer (asset_id));
  json_object_set_new (out, "recalled", json_integer (asset_quantity));
  json_object_set_new (out, "remaining_in_sector", json_integer (0));
  json_object_set_new (out, "asset_type", json_integer (asset_type));
  send_response_ok_take (ctx, root, "combat.mines.recalled", &out);
  return 0;
}

