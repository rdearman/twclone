#include <jansson.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>
// local includes
#include "server_cron.h"
#include "server_log.h"
#include "common.h"
#include "server_players.h"
#include "db/repo/repo_ships.h"
#include "server_universe.h"
#include "server_combat.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_config.h"
#include "globals.h"
#include "server_planets.h"
#include "server_ports.h"
#include "server_ships.h"
#include "server_sector_notices.h"
#include "game_db.h"
#include "repo_cmd.h"
#include "repo_combat.h"
#include "db/repo/repo_citadel.h"
#include "db/repo/repo_players.h"
#include "db/repo/repo_planets.h"
#include "db/repo/repo_universe.h"
#include "db/db_api.h"
#include "db/sql_driver.h"

typedef struct
{
  int id;
  int player_id;
  int corp_id;
  int hull;
  int shields;
  int fighters;
  int attack_power;
  int defense_power;
  int max_attack;
  char name[64];
  int sector;
} combat_ship_t;

static void apply_combat_damage (combat_ship_t * target, int damage,
				 int *shields_lost, int *hull_lost);
static int load_ship_combat_stats_unlocked (db_t * db, int ship_id,
					    combat_ship_t * out);
static int load_ship_combat_stats_locked (db_t * db, int ship_id,
					  combat_ship_t * out,
					  bool skip_locked);
static int h_apply_quasar_damage (client_ctx_t * ctx, int damage,
				  const char *source_desc, int planet_id,
				  json_t **outcome);
static int destroy_ship_in_transaction (db_t *db, client_ctx_t *ctx,
                                        int player_id, int ship_id,
                                        ship_kill_cause_t cause, int sector_id);
static int persist_ship_damage (db_t * db, combat_ship_t * ship,
				int fighters_lost);
static int cmd_deploy_assets_list_internal (client_ctx_t * ctx, json_t * root,
					    const char *list_type,
					    const char *asset_key,
					    int (*list_func) (db_t *, int,
							      json_t **));

static void
iss_summon (int sector_id, int player_id)
{
  LOGI ("ISS Summoned to sector %d for player %d", sector_id, player_id);
}

/**
 * fedspace_enforce_no_aggression_hard()
 * 
 * Central enforcement gate for aggression in FedSpace (sectors 1–10).
 * If attacker is in FedSpace, immediately destroy attacker's ship and pod attacker.
 * 
 * Returns 1 if punishment was applied, 0 if no violation, or a negative
 * database error code if the complete punishment could not be committed.
 */
int
fedspace_enforce_no_aggression_hard (client_ctx_t *ctx, int attacker_ship_id,
                                      int attacker_player_id, const char *reason)
{
  if (!ctx || attacker_ship_id <= 0 || attacker_player_id <= 0)
    return 0;

  /* Check if attacker is in FedSpace (sectors 1–10) */
  if (ctx->sector_id < 1 || ctx->sector_id > 10)
    return 0;  /* Not in FedSpace, no violation */

  /* CAPTAIN Z INTERVENTION: Attacker in FedSpace initiating aggression */
  db_t *db = game_db_get_handle ();
  if (!db)
    return -ERR_DB_CLOSED;

  LOGI ("CAPTAIN Z: FedSpace aggression detected! Player %d in sector %d (%s). "
        "Destroying ship %d and podding player.",
        attacker_player_id, ctx->sector_id, reason, attacker_ship_id);

  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      LOGE ("Captain Z: Could not begin enforcement transaction: %s",
            err.message);
      return -ERR_DB_TX;
    }

  ship_kill_context_t kill_ctx = {
    .victim_player_id = attacker_player_id,
    .victim_ship_id = attacker_ship_id,
    .killer_player_id = 0,
    .cause = KILL_CAUSE_OTHER,
    .sector_id = ctx->sector_id
  };
  if (handle_ship_destruction (db, &kill_ctx) != 0)
    {
      LOGE ("Failed to destroy attacker ship %d", attacker_ship_id);
      db_tx_rollback (db, NULL);
      return -ERR_DB_EXEC_FAILED;
    }
  ctx->sector_id = 1;

  if (!db_tx_commit (db, &err))
    {
      LOGE ("Captain Z: Could not commit enforcement transaction: %s",
            err.message);
      db_tx_rollback (db, NULL);
      return -ERR_DB_TX;
    }

  /* 3. Emit event: Captain Z intervention message */
  char msg[512];
  snprintf (msg, sizeof (msg),
            "CAPTAIN Z INTERVENTION: Your attempt to initiate aggression in FedSpace "
            "(Sector %d) has resulted in the destruction of your ship and your pod. "
            "FedSpace is protected space. Respect the law.",
            ctx->sector_id);

  /* Send notification to attacker */
  h_send_message_to_player (db, attacker_player_id, 2, /* fedadmin */
                            "Captain Z - FedSpace Violation", msg);

  /* Emit system event for observers (if using event plumbing) */
  LOGI ("CAPTAIN Z INTERVENTION: Player %d podded in sector %d for aggression attempt",
        attacker_player_id, ctx->sector_id);

  return 1;  /* Violation handled, stop processing */
}

json_t *db_get_stardock_sectors (void);
int handle_ship_attack (client_ctx_t * ctx, json_t * root, json_t * data,
			db_t * db);

static inline int
require_auth (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id > 0)
    return 1;
  send_response_refused_steal (ctx, root, ERR_SECTOR_NOT_FOUND,
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

bool
is_asset_hostile (int asset_player_id, int asset_corp_id, int ship_player_id,
		  int ship_corp_id)
{
  if (asset_player_id == ship_player_id)
    return false;
  if (asset_corp_id != 0 && asset_corp_id == ship_corp_id)
    return false;
  return true;
}

bool
armid_stack_is_active (const sector_asset_t *row, time_t now)
{
  if (row->quantity <= 0)
    return false;
  if (row->ttl <= 0 || row->ttl == (time_t) - 1)
    return true;
  return row->ttl > now;
}

/* Forward declarations */
static void apply_armid_damage_to_ship (ship_t *ship, int total_damage,
                                        armid_damage_breakdown_t *breakdown);

int
apply_sector_fighters_on_entry (client_ctx_t *ctx, int sector_id)
{
  if (!ctx || sector_id <= 0)
    return 0;
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;
  int damage_per_unit = 10;
  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    return -1;
  combat_ship_t ship = { 0 };
  if (load_ship_combat_stats_locked (db, ship_id, &ship, false) != 0)
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  int *ids = NULL, *quantities = NULL, *owners = NULL, *corps = NULL, *modes =
    NULL;
  int count = 0;
  if (db_combat_get_hostile_fighters
      (db, sector_id, &ids, &quantities, &owners, &corps, &modes,
       &count) != 0)
    {
      db_tx_rollback (db, NULL);
      return -1;
    }

  int result = 0;
  for (int i = 0; i < count; i++)
    {
      if (!is_asset_hostile
	  (owners[i], corps[i], ctx->player_id, ctx->corp_id))
	{
	  continue;
	}
      int damage = quantities[i] * damage_per_unit;
      ship_t s = {.hull = ship.hull,.fighters = ship.fighters,.shields = ship.shields };
      armid_damage_breakdown_t breakdown = { 0 };
      apply_armid_damage_to_ship (&s, damage, &breakdown);
      int total_applied = breakdown.shields_lost + breakdown.fighters_lost + breakdown.hull_lost;
      if (total_applied <= 0)
        continue;
      int units_consumed = (total_applied + damage_per_unit - 1) / damage_per_unit;
      if (units_consumed > quantities[i]) units_consumed = quantities[i];

      ship.hull = s.hull;
      ship.fighters = s.fighters;
      ship.shields = s.shields;
      if (db_combat_update_ship_stats (db, ship_id, ship.hull, ship.fighters,
                                       ship.shields) != 0
          || db_combat_decrement_or_delete_asset (db, ids[i], units_consumed) != 0)
        {
          result = -1;
          goto fighters_done;
        }
      json_t *evt = json_object ();
      if (!evt)
        {
          result = -1;
          goto fighters_done;
        }
      json_object_set_new (evt, "damage", json_integer (total_applied));
      json_object_set_new (evt, "fighters_engaged", json_integer (units_consumed));
      json_object_set_new (evt, "shields_lost", json_integer (breakdown.shields_lost));
      json_object_set_new (evt, "fighters_lost", json_integer (breakdown.fighters_lost));
      json_object_set_new (evt, "hull_lost", json_integer (breakdown.hull_lost));
      if (db_log_engine_event ((long long) time (NULL), "combat.hit.fighters",
                               "player", ctx->player_id, sector_id, evt, db) != 0)
        {
          json_decref (evt);
          result = -1;
          goto fighters_done;
        }
      json_decref (evt);
      if (ship.hull <= 0)
	{
          if (destroy_ship_in_transaction (db, ctx, ctx->player_id, ship_id,
                                           KILL_CAUSE_COMBAT, sector_id) != 0)
            result = -1;
          else
            result = 1;
          break;
	}
    }

fighters_done:
  free (ids);
  free (quantities);
  free (owners);
  free (corps);
  free (modes);
  if (result < 0 || !db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  return result;
}

int
cmd_combat_attack (client_ctx_t *ctx, json_t *root)
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
  if (!data || !json_is_object (data))
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD,
			   "Missing required field: data");
      return 0;
    }
  return handle_ship_attack (ctx, root, data, db);
}

void
apply_armid_damage_to_ship (ship_t *ship, int total_damage,
			    armid_damage_breakdown_t *b)
{
  int dmg = total_damage;
  if (ship->shields > 0 && dmg > 0)
    {
      int d = MIN (dmg, ship->shields);
      ship->shields -= d;
      dmg -= d;
      if (b)
	b->shields_lost += d;
    }
  if (ship->fighters > 0 && dmg > 0)
    {
      int d = MIN (dmg, ship->fighters);
      ship->fighters -= d;
      dmg -= d;
      if (b)
	b->fighters_lost += d;
    }
  if (ship->hull > 0 && dmg > 0)
    {
      int d = MIN (dmg, ship->hull);
      ship->hull -= d;
      dmg -= d;
      if (b)
	b->hull_lost += d;
    }
}

static int
db_ship_get_combat_stats (db_t *db, int ship_id, ship_t *out_ship,
			  db_error_t *err)
{
  repo_combat_ship_t s;
  (void) err;
  if (db_combat_get_ship_stats (db, ship_id, &s) == 1)
    {
      out_ship->id = s.id;
      out_ship->hull = s.hull;
      out_ship->fighters = s.fighters;
      out_ship->shields = s.shields;
      return 1;
    }
  return 0;
}

int
apply_sector_quasar_on_entry (client_ctx_t *ctx, int sector_id)
{
  if (sector_id == 1)
    return 0;
  db_t *db = game_db_get_handle ();
  if (!db)
    return -1;
  int shot_fired = 0;
  json_t *planets = NULL;
  if (db_combat_get_planet_quasar_info (db, sector_id, &planets) != 0
      || !planets)
    {
      json_decref (planets);
      return -1;
    }
  else
    {
      size_t i;
      json_t *p;
      json_array_foreach (planets, i, p)
      {
	int planet_id = json_integer_value (json_object_get (p, "planet_id"));
	int owner_id = json_integer_value (json_object_get (p, "owner_id"));
	const char *owner_type =
	  json_string_value (json_object_get (p, "owner_type"));
	int base_strength =
	  json_integer_value (json_object_get (p, "base_strength"));
	int reaction = json_integer_value (json_object_get (p, "reaction"));
	int p_corp_id = (owner_type
			 && (strcasecmp (owner_type, "corp") == 0
			     || strcasecmp (owner_type,
					    "corporation") ==
			     0)) ? owner_id : 0;
	if (is_asset_hostile
	    (owner_id, p_corp_id, ctx->player_id, ctx->corp_id))
	  {
	    int pct = (reaction == 1) ? 125 : (reaction >= 2) ? 150 : 100;
	    int damage =
	      (int) floor ((double) base_strength * (double) pct / 100.0);
	    char source_desc[64];
	    snprintf (source_desc, sizeof (source_desc),
		      "Quasar Sector Shot (Planet %d)", planet_id);
	    int hit_rc = h_apply_quasar_damage (ctx, damage, source_desc, planet_id,
						NULL);
	    if (hit_rc == 2)
	      shot_fired = 1;
	    else if (hit_rc == 1)
	      shot_fired = 2;
	    else
	      shot_fired = -1;
	    break;
	  }
      }
      json_decref (planets);
    }
  return shot_fired < 0 ? -1 : shot_fired;
}

int
apply_armid_mines_on_entry (client_ctx_t *ctx, int new_sector_id,
			    armid_encounter_t *out_enc)
{
  if (!ctx || new_sector_id <= 0)
    return -1;
  db_t *db = game_db_get_handle ();
  if (!db)
    return -1;
  if (out_enc)
    {
      memset (out_enc, 0, sizeof (*out_enc));
      out_enc->sector_id = new_sector_id;
    }
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;
  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    return -1;
  combat_ship_t combat = { 0 };
  if (load_ship_combat_stats_locked (db, ship_id, &combat, false) != 0)
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  ship_t ship_stats = { .hull = combat.hull, .fighters = combat.fighters,
                        .shields = combat.shields };
  int damage_per_mine = db_get_config_int (db, "armid_damage_per_mine", 25);
  if (damage_per_mine <= 0)
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  json_t *mines = NULL;
  if (db_combat_select_armid_mines_locked (db, new_sector_id, &mines) != 0
      || !mines)
    {
      json_decref (mines);
      db_tx_rollback (db, NULL);
      return -1;
    }
  int result = 0;
  size_t i;
  json_t *m;
  json_array_foreach (mines, i, m)
    {
      int mine_id = json_integer_value (json_object_get (m, "id"));
      int mine_quantity = json_integer_value (json_object_get (m, "quantity"));
      int owner_id = json_integer_value (json_object_get (m, "owner_id"));
      int corp_id = json_integer_value (json_object_get (m, "corp_id"));
      int64_t ttl = json_integer_value (json_object_get (m, "ttl"));
      sector_asset_t asset = { .quantity = mine_quantity, .ttl = (time_t) ttl };
      if (!is_asset_hostile (owner_id, corp_id, ctx->player_id, ctx->corp_id)
          || !armid_stack_is_active (&asset, time (NULL)))
        continue;
      int damage = mine_quantity * damage_per_mine;
      armid_damage_breakdown_t d = { 0 };
      apply_armid_damage_to_ship (&ship_stats, damage, &d);
      int total_applied = d.shields_lost + d.fighters_lost + d.hull_lost;
      if (total_applied <= 0)
        continue;
      int units_consumed = (total_applied + damage_per_mine - 1)
        / damage_per_mine;
      if (units_consumed > mine_quantity)
        units_consumed = mine_quantity;
      combat.hull = ship_stats.hull;
      combat.fighters = ship_stats.fighters;
      combat.shields = ship_stats.shields;
      json_t *hit_data = json_pack
        ("{s:i,s:i,s:s,s:i,s:i,s:i,s:i}",
         "attacker_id", owner_id, "defender_id", ctx->player_id,
         "weapon", "armid_mines", "damage_total", total_applied,
         "shields_lost", d.shields_lost, "fighters_lost", d.fighters_lost,
         "hull_lost", d.hull_lost);
      if (db_combat_decrement_or_delete_asset (db, mine_id, units_consumed) != 0
          || db_combat_update_ship_stats (db, ship_id, combat.hull,
                                          combat.fighters, combat.shields) != 0
          || !hit_data
          || db_log_engine_event ((long long) time (NULL), "combat.hit",
                                 "player", ctx->player_id, new_sector_id,
                                 hit_data, db) != 0)
        {
          json_decref (hit_data);
          result = -1;
          break;
        }
      json_decref (hit_data);
      if (out_enc)
        {
          out_enc->armid_triggered += units_consumed;
          out_enc->shields_lost += d.shields_lost;
          out_enc->fighters_lost += d.fighters_lost;
          out_enc->hull_lost += d.hull_lost;
        }
      if (ship_stats.hull <= 0)
        {
          if (destroy_ship_in_transaction (db, ctx, ctx->player_id, ship_id,
                                           KILL_CAUSE_MINES, new_sector_id) != 0)
            result = -1;
          else
            {
              if (out_enc)
                out_enc->destroyed = true;
              result = 1;
            }
          break;
        }
    }
  json_decref (mines);
  if (result < 0 || !db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  return result;
}

int
apply_limpet_mines_on_entry (client_ctx_t *ctx, int new_sector_id,
			     armid_encounter_t *out_enc)
{
  if (!ctx || new_sector_id <= 0)
    return -1;
  db_t *db = game_db_get_handle ();
  if (!db)
    return -1;
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;
  if (!g_cfg.mines.limpet.enabled)
    return 0;
  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    return -1;
  json_t *limpets = NULL;
  if (db_combat_select_limpets_locked (db, new_sector_id, &limpets) != 0)
    {
      json_decref (limpets);
      db_tx_rollback (db, NULL);
      return -1;
    }
  if (!limpets)
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  int64_t now_ts = (int64_t) time (NULL);
  int attached_count = 0;
  int result = 0;
  size_t i;
  json_t *limpet;
  json_array_foreach (limpets, i, limpet) {
    int asset_id = json_integer_value (json_object_get (limpet, "id"));
    int quantity = json_integer_value (json_object_get (limpet, "quantity"));
    int owner_id = json_integer_value (json_object_get (limpet, "owner_id"));
    int corp_id = json_integer_value (json_object_get (limpet, "corp_id"));
    int64_t ttl = json_integer_value (json_object_get (limpet, "ttl"));
    sector_asset_t asset = {.quantity = quantity,.ttl = (time_t) ttl };
    if (!is_asset_hostile (owner_id, corp_id, ctx->player_id, ctx->corp_id))
      continue;
    if (!armid_stack_is_active (&asset, (time_t) now_ts))
      continue;
    bool attached = false;
    if (db_combat_check_limpet_attached (db, ship_id, owner_id, &attached) != 0)
      {
        result = -1;
        break;
      }
    if (attached)
      continue;
    if (db_combat_decrement_or_delete_asset (db, asset_id, quantity) != 0
        || db_combat_attach_limpet (db, ship_id, owner_id, now_ts) != 0)
      {
	result = -1;
	break;
      }
    json_t *event_data = json_pack ("{s:i,s:i,s:i,s:i}",
                                    "target_ship_id", ship_id,
                                    "target_player_id", ctx->player_id,
                                    "asset_owner_id", owner_id,
                                    "sector_id", new_sector_id);
    if (!event_data
        || db_log_engine_event (now_ts, "combat.limpet.attached", "player",
                                owner_id, new_sector_id, event_data, db) != 0)
      {
	json_decref (event_data);
	result = -1;
	break;
      }
    json_decref (event_data);
    attached_count++;
  }
  json_decref (limpets);
  if (result < 0 || !db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  if (out_enc)
    out_enc->limpet_triggered += attached_count;
  return 0;
}

int
handle_combat_flee (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;
  h_decloak_ship (db, ship_id);
  TurnConsumeResult tc = h_consume_player_turn (db, ctx, 1);
  if (tc != TURN_CONSUME_SUCCESS)
    return handle_turn_consumption_error (ctx, tc, "combat.flee", root, NULL);
  int mass = 100, engine = 10, sector_id = 0;
  if (db_combat_get_flee_info (db, ship_id, &mass, &sector_id) != 0)
    return 0;
  if (((double) engine * 10.0) / ((double) mass + 1.0) > 0.5)
    {
      int dest = 0;
      db_combat_get_first_adjacent_sector (db, sector_id, &dest);
      if (dest > 0)
	{
	  if (db_combat_move_ship_and_player
	      (db, ship_id, ctx->player_id, dest) == 0)
	    {
	      ctx->sector_id = dest;
	      int hazard_rc = server_combat_apply_entry_hazards (db, ctx, dest);
	      if (hazard_rc < 0)
		{
		  send_response_error (ctx, root, ERR_DB,
				       "Flee succeeded, but entry defenses could not be resolved.");
		  return 0;
		}
      if (hazard_rc == 1)
	{
	  json_t *data = json_pack ("{s:i,s:b}", "to_sector", dest,
				    "ship_destroyed", true);
	  json_object_set_new (data, "current_sector_id",
	                       json_integer (ctx->sector_id));
		  send_response_error_data (ctx, root, ERR_PERMISSION_DENIED,
				    "Ship destroyed by defenses in the flee destination.", data);
		  json_decref (data);
		  return 0;
		}
	      json_t *res = json_object ();
	      json_object_set_new (res, "success", json_true ());
	      json_object_set_new (res, "to_sector", json_integer (dest));
	      send_response_ok_take (ctx, root, "combat.flee", &res);
	      return 0;
	    }
	}
    }
  json_t *res = json_object ();
  json_object_set_new (res, "success", json_false ());
  send_response_ok_take (ctx, root, "combat.flee", &res);
  return 0;
}

int
cmd_deploy_fighters_list (client_ctx_t *ctx, json_t *root)
{
  return cmd_deploy_assets_list_internal (ctx, root,
					  "deploy.fighters.list_v1",
					  "fighters",
					  db_combat_list_fighters);
}

int
cmd_deploy_mines_list (client_ctx_t *ctx, json_t *root)
{
  return cmd_deploy_assets_list_internal (ctx, root, "deploy.mines.list_v1",
					  "mines", db_combat_list_mines);
}

static int
cmd_deploy_assets_list_internal (client_ctx_t *ctx, json_t *root,
				 const char *list_type, const char *asset_key,
				 int (*list_func) (db_t *, int, json_t **))
{
  (void) asset_key;
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  json_t *entries = NULL;
  if (list_func (db, ctx->player_id, &entries) != 0)
    return 0;
  json_t *jdata = json_object ();
  json_object_set_new (jdata, "total",
		       json_integer (json_array_size (entries)));
  json_object_set_new (jdata, "entries", entries);
  send_response_ok_take (ctx, root, list_type, &jdata);
  return 0;
}

int
cmd_combat_deploy_fighters (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  json_t *data = json_object_get (root, "data");
  if (!data)
    return 0;

  int amount = 0;
  if (!json_get_int_flexible (data, "amount", &amount))
    json_get_int_flexible (data, "count", &amount);

  if (amount <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "amount must be > 0");
      return 0;
    }
  int offense = (int) json_integer_value (json_object_get (data, "offense"));
  int corp_id =
    (int) json_integer_value (json_object_get (data, "corporation_id"));
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;
  h_decloak_ship (db, ship_id);
  
  /* Check for FedSpace denial (sectors 1-10) */
  if (ctx->sector_id >= 1 && ctx->sector_id <= 10)
    {
      send_response_refused_steal (ctx, root, 1454,
                                   "Cannot deploy fighters in protected FedSpace.",
                                   NULL);
      return 0;
    }
  
  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    return 0;
  int sid = -1;
  if (db_combat_get_ship_sector_locked (db, ship_id, &sid) != 0 || sid <= 0)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_BAD_STATE, "Could not locate ship.");
      return 0;
    }
  if (db_combat_lock_sector (db, sid) != 0)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_DB_BUSY, "Sector is locked.");
      return 0;
    }
  int total = 0;
  db_combat_sum_sector_fighters (db, sid, &total);
  if (total + amount > SECTOR_FIGHTER_CAP)
    {
      db_tx_rollback (db, &err);
      send_response_refused_steal (ctx, root, ERR_SECTOR_FIGHTER_CAP,
				   "Sector fighter capacity reached.", NULL);
      return 0;
    }
  if (db_combat_consume_ship_fighters (db, ship_id, amount) != 0)
    {
      db_tx_rollback (db, &err);
      send_response_refused_steal (ctx, root, 1453,
				   "You do not carry enough fighters to deploy.",
				   NULL);
      return 0;
    }
  db_combat_insert_sector_fighters (db, sid, ctx->player_id, corp_id, offense,
				    amount);
  int64_t aid = 0;
  db_combat_get_max_asset_id (db, sid, ctx->player_id, 2, &aid);
  db_tx_commit (db, &err);
  if (sid >= 1 && sid <= 10)
    iss_summon (sid, ctx->player_id);

  json_t *notice_details = json_object ();
  if (notice_details)
    {
      json_object_set_new (notice_details, "asset_id", json_integer (aid));
      json_object_set_new (notice_details, "count", json_integer (amount));
      json_object_set_new (notice_details, "offense",
                           json_integer (offense));
      server_sector_notice_publish (ctx, root, sid, "fighters_deployed",
                                    notice_details);
      json_decref (notice_details);
    }

  json_t *out = json_object ();
  json_object_set_new (out, "asset_id", json_integer (aid));
  json_object_set_new (out, "count_added", json_integer (amount));
  json_object_set_new (out, "total_now", json_integer (total + amount));

  send_response_ok_take (ctx, root, "combat.fighters.deployed", &out);
  return 0;
}

int
cmd_combat_deploy_mines (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  json_t *data = json_object_get (root, "data");
  if (!data)
    return 0;

  int amount = 0;
  if (!json_get_int_flexible (data, "quantity", &amount)
      && !json_get_int_flexible (data, "amount", &amount))
    json_get_int_flexible (data, "count", &amount);

  if (amount <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "amount must be > 0");
      return 0;
    }

  int mine_type = 0;
  json_t *j_mtype = json_object_get (data, "mine_type");
  if (j_mtype)
    {
      if (json_is_integer (j_mtype))
	{
	  mine_type = (int) json_integer_value (j_mtype);
	}
      else if (json_is_string (j_mtype))
	{
	  const char *s = json_string_value (j_mtype);
	  if (strcasecmp (s, "armid") == 0)
	    mine_type = 1;
	  else if (strcasecmp (s, "limpet") == 0)
	    mine_type = 4;
	}
    }

  if (mine_type == 0)
    {
      const char *type_str = json_string_value (json_object_get (data, "type"));
      if (type_str)
	{
	  if (strcasecmp (type_str, "armid") == 0)
	    mine_type = 1;
	  else if (strcasecmp (type_str, "limpet") == 0)
	    mine_type = 4;
	}
    }

  if (mine_type == 0)
    mine_type = 1;

  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;
  h_decloak_ship (db, ship_id);
  
  /* Check for FedSpace denial (sectors 1-10) */
  if (ctx->sector_id >= 1 && ctx->sector_id <= 10)
    {
      send_response_refused_steal (ctx, root, 1454,
                                   "Cannot deploy mines in protected FedSpace.",
                                   NULL);
      return 0;
    }
  
  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    return 0;
  int sid = -1;
  if (db_combat_get_ship_sector_locked (db, ship_id, &sid) != 0 || sid <= 0)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_BAD_STATE, "Could not locate ship.");
      return 0;
    }
  if (db_combat_lock_sector (db, sid) != 0)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_DB_BUSY, "Sector is locked.");
      return 0;
    }

  int total = 0;
  db_combat_sum_sector_mines (db, sid, &total);

  if (db_combat_consume_ship_mines (db, ship_id, mine_type, amount) != 0)
    {
      db_tx_rollback (db, &err);
      send_response_refused_steal (ctx, root, 1453,
				   "You do not carry enough of that commodity to deploy.",
				   NULL);
      return 0;
    }
  db_combat_insert_sector_mines (db, sid, ctx->player_id, ctx->corp_id,
				 mine_type, 1, amount);
  int64_t aid = 0;
  db_combat_get_max_asset_id (db, sid, ctx->player_id, mine_type, &aid);
  db_tx_commit (db, &err);

  json_t *out = json_object ();
  json_object_set_new (out, "asset_id", json_integer (aid));
  json_object_set_new (out, "count_added", json_integer (amount));
  json_object_set_new (out, "total_now", json_integer (total + amount));

  send_response_ok_take (ctx, root, "combat.mines.deployed", &out);
  return 0;
}

int
cmd_combat_lay_mines (client_ctx_t *ctx, json_t *root)
{
  return cmd_combat_deploy_mines (ctx, root);
}

static int
load_ship_combat_stats_locked (db_t *db, int ship_id, combat_ship_t *out,
			       bool skip_locked)
{
  repo_combat_ship_full_t fs;
  if (db_combat_load_ship_full_locked (db, ship_id, &fs, skip_locked) != 0)
    return -1;
  out->id = fs.id;
  out->hull = fs.hull;
  out->shields = fs.shields;
  out->fighters = fs.fighters;
  out->sector = fs.sector;
  strncpy (out->name, fs.name, sizeof (out->name));
  out->attack_power = fs.attack_power;
  out->defense_power = fs.defense_power;
  out->max_attack = fs.max_attack;
  out->player_id = fs.player_id;
  out->corp_id = fs.corp_id;
  return 0;
}

static int
load_ship_combat_stats_unlocked (db_t *db, int ship_id, combat_ship_t *out)
{
  repo_combat_ship_full_t fs;
  if (db_combat_load_ship_full_unlocked (db, ship_id, &fs) != 0)
    return -1;
  out->id = fs.id;
  out->hull = fs.hull;
  out->shields = fs.shields;
  out->fighters = fs.fighters;
  out->sector = fs.sector;
  strncpy (out->name, fs.name, sizeof (out->name));
  out->attack_power = fs.attack_power;
  out->defense_power = fs.defense_power;
  out->max_attack = fs.max_attack;
  out->player_id = fs.player_id;
  out->corp_id = fs.corp_id;
  return 0;
}

static int
persist_ship_damage (db_t *db, combat_ship_t *ship, int fighters_lost)
{
  return db_combat_persist_ship_damage (db, ship->id, ship->hull,
					ship->shields, fighters_lost);
}

int
handle_ship_attack (client_ctx_t *ctx, json_t *root, json_t *data, db_t *db)
{
  int target_id = 0;
  if (!json_get_int_flexible (data, "target_ship_id", &target_id)
      || target_id <= 0)
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD,
                           "Missing required field: target_ship_id");
      return 0;
    }
  int attacker_id = h_get_active_ship_id (db, ctx->player_id);
  if (attacker_id <= 0)
    {
      send_response_error (ctx, root, ERR_NO_ACTIVE_SHIP,
                           "No active ship found.");
      return 0;
    }

  /* FedSpace enforcement: hard-punish aggression in sectors 1–10 */
  int enforcement_rc =
    fedspace_enforce_no_aggression_hard (ctx, attacker_id, ctx->player_id,
                                          "ship.attack");
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

  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      LOGE ("combat.attack: Could not begin transaction: %s", err.message);
      send_response_error (ctx, root, ERR_DB_TX,
                           "Could not begin combat transaction");
      return 0;
    }

  combat_ship_t att = { 0 }, def = { 0 };
  int rc = load_ship_combat_stats_locked (db, attacker_id, &att, true);
  if (rc != 0)
    {
      LOGE ("combat.attack: Failed to load attacker ship %d (rc=%d)",
            attacker_id, rc);
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to load attacker combat state");
      return 0;
    }

  rc = load_ship_combat_stats_locked (db, target_id, &def, true);
  if (rc != 0)
    {
      LOGE ("combat.attack: Failed to load target ship %d (rc=%d)",
            target_id, rc);
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Failed to load target combat state");
      return 0;
    }

  /* A Ferengi attack is both a combat offence and a wanted-list event.  The
   * handler is idempotent and runs in this transaction, so a failed combat
   * operation cannot leave a bounty or hunt record behind. */
  int ferengi_rc = ferengi_handle_hostile_attack
    (db, ctx->player_id, target_id, ctx->sector_id, (int64_t) time (NULL));
  if (ferengi_rc < 0)
    {
      LOGE ("combat.attack: could not record Ferengi hostile-action state for ship %d",
            target_id);
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB_EXEC_FAILED,
                           "Could not persist Ferengi retaliation state; combat was not applied");
      return 0;
    }

  /* Outside FedSpace, attacks on explicitly registered Federation assets
   * create a persistent ISS incident.  Captain Z has already handled sectors
   * 1-10 above, so this hook cannot duplicate immediate enforcement. */
  char incident_key[128];
  snprintf (incident_key, sizeof incident_key, "iss:attack:%d:%d:%lld",
            ctx->player_id, target_id, (long long) time (NULL) / 60);
  (void) repo_npc_security_incident_claim (db, incident_key, ctx->player_id,
                                            "ship", target_id, ctx->sector_id,
                                            (int64_t) time (NULL) + 86400);

  int s_lost = 0, h_lost = 0;
  apply_combat_damage (&def, 10, &s_lost, &h_lost);
  int defender_is_npc = 0;
  if (def.player_id > 0
      && repo_players_is_npc (db, def.player_id, &defender_is_npc) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Could not resolve target ownership for destruction");
      return 0;
    }
  if (def.hull <= 0 && def.player_id > 0 && !defender_is_npc)
    {
      ship_kill_context_t kill_ctx = {
        .victim_player_id = def.player_id,
        .victim_ship_id = target_id,
        .killer_player_id = ctx->player_id,
        .cause = KILL_CAUSE_COMBAT,
        .sector_id = ctx->sector_id
      };
      rc = handle_ship_destruction (db, &kill_ctx);
    }
  else
    rc = persist_ship_damage (db, &def, 0);
  if (rc != 0)
    {
      LOGE ("combat.attack: Failed to persist target ship %d damage (rc=%d)",
            target_id, rc);
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB_EXEC_FAILED,
                           "Failed to persist combat damage");
      return 0;
    }

  json_t *resp = json_object ();
  if (!resp || json_object_set_new (resp, "success", json_true ()) != 0)
    {
      json_decref (resp);
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_NOMEM,
                           "Could not build combat response");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      LOGE ("combat.attack: Could not commit transaction: %s", err.message);
      db_tx_rollback (db, NULL);
      json_decref (resp);
      send_response_error (ctx, root, ERR_DB_TX,
                           "Could not commit combat transaction");
      return 0;
    }

  send_response_ok_take (ctx, root, "combat.attack.result", &resp);
  return 0;
}

int
npc_combat_attack_ship (db_t *db, int attacker_ship_id, int target_ship_id,
                        int sector_id)
{
  if (!db || attacker_ship_id <= 0 || target_ship_id <= 0 ||
      attacker_ship_id == target_ship_id || sector_id <= 10)
    return ERR_INVALID_ARG;
  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err)) return ERR_DB_TX;
  combat_ship_t attacker = {0}, target = {0};
  if (load_ship_combat_stats_locked (db, attacker_ship_id, &attacker, true) != 0 ||
      load_ship_combat_stats_locked (db, target_ship_id, &target, true) != 0)
    {
      db_tx_rollback (db, NULL);
      return ERR_DB_NOT_FOUND;
    }
  int shields_lost = 0, hull_lost = 0;
  apply_combat_damage (&target, 10, &shields_lost, &hull_lost);
  if (target.hull <= 0 && target.player_id > 0)
    {
      ship_kill_context_t kill_ctx = {
        .victim_player_id = target.player_id,
        .victim_ship_id = target_ship_id,
        .killer_player_id = 0,
        .cause = KILL_CAUSE_COMBAT,
        .sector_id = sector_id
      };
      if (handle_ship_destruction (db, &kill_ctx) != 0)
        {
          db_tx_rollback (db, NULL);
          return ERR_DB_EXEC_FAILED;
        }
    }
  else if (persist_ship_damage (db, &target, 0) != 0)
    {
      db_tx_rollback (db, NULL);
      return ERR_DB_EXEC_FAILED;
    }
  int target_cargo[7] = {0}, attacker_cargo[7] = {0};
  int target_holds = 0, attacker_holds = 0;
  if (h_get_ship_cargo_and_holds (db, target_ship_id,
                                  &target_cargo[0], &target_cargo[1],
                                  &target_cargo[2], &target_cargo[3],
                                  &target_cargo[4], &target_cargo[5],
                                  &target_cargo[6], &target_holds) == 0 &&
      h_get_ship_cargo_and_holds (db, attacker_ship_id,
                                  &attacker_cargo[0], &attacker_cargo[1],
                                  &attacker_cargo[2], &attacker_cargo[3],
                                  &attacker_cargo[4], &attacker_cargo[5],
                                  &attacker_cargo[6], &attacker_holds) == 0)
    {
      (void) target_holds;
      int used = 0;
      for (size_t i = 0; i < 7; ++i) used += attacker_cargo[i];
      static const char *const cargo_codes[] =
        {"ORE", "ORG", "EQU", "COL", "SLV", "WPN", "DRG"};
      for (size_t i = 0; i < 7 && attacker_holds - used > 0; ++i)
        if (target_cargo[i] > 0)
          {
            if (h_update_ship_cargo (db, target_ship_id, cargo_codes[i], -1,
                                     NULL) != 0 ||
                h_update_ship_cargo (db, attacker_ship_id, cargo_codes[i], 1,
                                     NULL) != 0)
              {
                db_tx_rollback (db, NULL);
                return ERR_DB_EXEC_FAILED;
              }
            break;
          }
    }
  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      return ERR_DB_EXEC_FAILED;
    }
  return 0;
}

int
cmd_combat_status (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;
  combat_ship_t ship = { 0 };
  load_ship_combat_stats_unlocked (db, ship_id, &ship);
  json_t *res = json_object ();
  json_object_set_new (res, "hull", json_integer (ship.hull));
  send_response_ok_take (ctx, root, "combat.status", &res);
  return 0;
}

static int
h_apply_quasar_damage (client_ctx_t *ctx, int damage, const char *source_desc,
                       int planet_id, json_t **outcome)
{
  db_t *db = game_db_get_handle ();
  if (!db || !ctx || damage <= 0)
    return -1;
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return -1;
  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    return -1;
  combat_ship_t ship = { 0 };
  if (load_ship_combat_stats_locked (db, ship_id, &ship, true) != 0)
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  int shields_lost = 0, hull_lost = 0;
  apply_combat_damage (&ship, damage, &shields_lost, &hull_lost);
  if (persist_ship_damage (db, &ship, 0) != 0)
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  bool destroyed = ship.hull <= 0;
  if (destroyed
      && destroy_ship_in_transaction (db, ctx, ctx->player_id, ship_id,
                                      KILL_CAUSE_QUASAR, ctx->sector_id) != 0)
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  json_t *event = json_object ();
  if (!event)
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  json_object_set_new (event, "target_ship_id", json_integer (ship_id));
  json_object_set_new (event, "target_player_id", json_integer (ctx->player_id));
  json_object_set_new (event, "damage", json_integer (damage));
  json_object_set_new (event, "shields_lost", json_integer (shields_lost));
  json_object_set_new (event, "hull_lost", json_integer (hull_lost));
  json_object_set_new (event, "shields_remaining", json_integer (ship.shields));
  json_object_set_new (event, "hull_remaining", json_integer (ship.hull));
  json_object_set_new (event, "planet_id", json_integer (planet_id));
  json_object_set_new (event, "source", json_string (source_desc ? source_desc : "Quasar"));
  json_object_set_new (event, "destroyed", json_boolean (destroyed));
  if (db_log_engine_event ((long long) time (NULL), "combat.hit.quasar",
                           "player", ctx->player_id, ship.sector, event, db) != 0)
    {
      json_decref (event);
      db_tx_rollback (db, NULL);
      return -1;
    }
  json_decref (event);
  json_t *result = NULL;
  if (outcome)
    {
      result = json_pack ("{s:s,s:i,s:i,s:i,s:i,s:i,s:i,s:b}",
                          "defense", "atmosphere_quasar",
                          "planet_id", planet_id, "damage", damage,
                          "shields_lost", shields_lost, "hull_lost", hull_lost,
                          "shields_remaining", ship.shields,
                          "hull_remaining", ship.hull,
                          "destroyed", destroyed);
      if (!result)
        {
          db_tx_rollback (db, NULL);
          return -1;
        }
    }
  if (!db_tx_commit (db, &err))
    {
      json_decref (result);
      db_tx_rollback (db, NULL);
      return -1;
    }
  if (outcome)
    *outcome = result;
  return destroyed ? 2 : 1;
}

static void
apply_combat_damage (combat_ship_t *target, int damage, int *shields_lost,
		     int *hull_lost)
{
  *shields_lost = 0;
  *hull_lost = 0;
  if (!target || damage <= 0)
    return;
  int rem = damage;
  if (target->shields > 0)
    {
      int a = MIN (rem, target->shields);
      target->shields -= a;
      rem -= a;
      *shields_lost = a;
    }
  if (rem > 0)
    {
      /* Damage cannot reduce persisted hull below zero. Keeping the actual
       * hull loss here also lets the normal destruction transaction run for
       * lethal overkill instead of rejecting a negative database value. */
      *hull_lost = MIN (rem, target->hull);
      target->hull -= *hull_lost;
    }
}

json_t *
db_get_stardock_sectors (void)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    return NULL;
  int *s = NULL;
  int count = 0;
  db_combat_get_stardock_locations (db, &s, &count);
  json_t *arr = json_array ();
  for (int i = 0; i < count; i++)
    json_array_append_new (arr, json_integer (s[i]));
  free (s);
  return arr;
}

static int
destroy_ship_in_transaction (db_t *db, client_ctx_t *ctx,
                             int player_id, int ship_id,
                             ship_kill_cause_t cause, int sector_id)
{
  if (!db || !ctx || player_id <= 0 || ship_id <= 0)
    return -1;
  ship_kill_context_t kill_ctx = {
    .victim_player_id = player_id,
    .victim_ship_id = ship_id,
    .killer_player_id = 0,
    .cause = cause,
    .sector_id = sector_id
  };
  int rc = handle_ship_destruction (db, &kill_ctx);
  if (rc == 0)
    ctx->sector_id = 1;
  return rc;
}

int
destroy_ship_and_handle_side_effects (client_ctx_t *ctx, int player_id)
{
  if (!ctx || player_id <= 0)
    return -1;
  db_t *db = game_db_get_handle ();
  if (!db)
    return -1;
  int ship_id = h_get_active_ship_id (db, player_id);
  if (ship_id <= 0)
    return 0;
  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    return -1;
  if (destroy_ship_in_transaction (db, ctx, player_id, ship_id,
                                   KILL_CAUSE_OTHER, ctx->sector_id) != 0)
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      return -1;
    }
  return 0;
}

int
h_decloak_ship (db_t *db, int ship_id)
{
  int rc = repo_ships_decloak (db, ship_id);
  if (rc != 0)
    LOGE ("Could not decloak ship %d (db error %d)", ship_id, rc);
  return rc;
}

static int
server_combat_apply_environmental_hazards (db_t *db, client_ctx_t *ctx,
                                           int sector_id, json_t **report)
{
  if (!db || !ctx || sector_id <= 0)
    return 0;

  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;

  json_t *hazards = NULL;
  if (db_combat_get_sector_hazards (db, sector_id, &hazards) != 0 || !hazards)
    {
      LOGE ("Could not read environmental hazards for sector %d", sector_id);
      json_decref (hazards);
      return -1;
    }

  ship_t ship = { 0 };
  if (db_ship_get_combat_stats (db, ship_id, &ship, NULL) != 1)
    {
      json_decref (hazards);
      return -1;
    }

  json_t *results = report && *report ? *report : json_array ();
  if (!results)
    {
      json_decref (hazards);
      return 0;
    }
  int destroyed = 0;
  size_t index;
  json_t *hazard;
  json_array_foreach (hazards, index, hazard)
    {
      const char *type = json_string_value (json_object_get (hazard, "type"));
      int severity = (int) json_integer_value (json_object_get (hazard, "severity"));
      if (!type || severity <= 0)
        continue;

      armid_damage_breakdown_t damage = { 0 };
      apply_armid_damage_to_ship (&ship, severity, &damage);
      int applied = damage.shields_lost + damage.fighters_lost + damage.hull_lost;
      if (applied == 0)
        continue;

      if (db_combat_persist_ship_damage (db, ship_id, ship.hull, ship.shields,
                                         damage.fighters_lost) != 0)
        {
          LOGE ("Failed to persist %s hazard damage to ship %d", type, ship_id);
          json_decref (hazards);
          if (report)
            *report = results;
          else
            json_decref (results);
          return -1;
        }

      json_t *event = json_object ();
      if (event)
        {
          /* Preserve the established hazards[].type response while exposing
           * the explicit hazard_type field used by newer consumers. */
          json_object_set_new (event, "type", json_string (type));
          json_object_set_new (event, "hazard_type", json_string (type));
          json_object_set_new (event, "severity", json_integer (severity));
          json_object_set_new (event, "damage", json_integer (applied));
          json_object_set_new (event, "shields_lost", json_integer (damage.shields_lost));
          json_object_set_new (event, "fighters_lost", json_integer (damage.fighters_lost));
          json_object_set_new (event, "hull_lost", json_integer (damage.hull_lost));
          if (db_log_engine_event ((long long) time (NULL), "combat.hit.environment",
                                   "player", ctx->player_id, sector_id, event,
                                   game_db_get_handle ()) != 0)
            {
              json_decref (event);
              json_decref (hazards);
              if (report)
                *report = results;
              else
                json_decref (results);
              return -1;
            }

          json_t *shown = json_copy (event);
          if (shown && results)
            {
              char message[128];
              snprintf (message, sizeof message,
                        "Environmental hazard (%s) caused %d damage.", type,
                        applied);
              json_object_set_new (shown, "message", json_string (message));
              json_array_append_new (results, shown);
            }
          json_decref (event);
        }

      if (ship.hull <= 0)
        {
          if (destroy_ship_and_handle_side_effects (ctx, ctx->player_id) != 0)
            {
              json_decref (hazards);
              if (report)
                *report = results;
              else
                json_decref (results);
              return -1;
            }
          destroyed = 1;
          break;
        }
    }

  json_decref (hazards);
  if (report)
    *report = results;
  else
    json_decref (results);
  return destroyed;
}

static int
server_combat_apply_entry_hazards_single (db_t *db, client_ctx_t *ctx, int sector_id,
                                          json_t **report)
{
  armid_encounter_t enc = { 0 };

  /* 1. Quasars fire first */
  int quasar_rc = apply_sector_quasar_on_entry (ctx, sector_id);
  if (quasar_rc < 0)
    return -1;
  if (quasar_rc == 1)
    { ctx->sector_id = 1; return 1; } /* Ship destroyed and podded. */

  /* 2. Mines detonate next */
  int mine_rc = apply_armid_mines_on_entry (ctx, sector_id, &enc);
  if (mine_rc < 0)
    return -1;
  if (enc.destroyed)
    { ctx->sector_id = 1; return 1; }

  /* 3. Fighters engage */
  int fighters_rc = apply_sector_fighters_on_entry (ctx, sector_id);
  if (fighters_rc < 0)
    return -1;
  if (fighters_rc == 1)
    { ctx->sector_id = 1; return 1; }

  /* 4. Limpets attach last */
  if (apply_limpet_mines_on_entry (ctx, sector_id, &enc) < 0)
    return -1;

  /* 5. Environmental hazards resolve after existing armed defenses. */
  int environment_rc = server_combat_apply_environmental_hazards
    (db, ctx, sector_id, report);
  if (environment_rc < 0)
    return -1;
  if (environment_rc == 1)
    { ctx->sector_id = 1; return 1; }

  return 0;
}

int
server_combat_apply_entry_hazards (db_t *db, client_ctx_t *ctx, int sector_id)
{
  return server_combat_apply_entry_hazards_report (db, ctx, sector_id, NULL);
}

int
server_combat_apply_entry_hazards_report (db_t *db, client_ctx_t *ctx,
                                          int sector_id, json_t **report)
{
  if (!db || !ctx || ctx->player_id <= 0 || sector_id <= 0)
    return 0;
  if (report)
    {
      *report = json_array ();
      if (!*report)
        return -1;
    }
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  
  /* Apply to entering ship */
  int destroyed = server_combat_apply_entry_hazards_single (db, ctx, sector_id,
                                                            report);
  
  /* Apply to towed ship if any */
  if (ship_id > 0)
    {
      int towed_sid = 0, towed_pid = 0, towed_cid = 0;
      if (repo_ships_get_towed_ship_info (db, ship_id, &towed_sid, &towed_pid, &towed_cid) == 0)
        {
          /* Create a temporary context for the towed ship's owner */
          client_ctx_t towed_ctx = *ctx;
          towed_ctx.player_id = towed_pid;
          towed_ctx.corp_id = towed_cid;
          towed_ctx.sector_id = sector_id;
          
          int towed_rc = server_combat_apply_entry_hazards_single
            (db, &towed_ctx, sector_id, NULL);
          if (towed_rc < 0)
            return -1;
          if (towed_rc == 1)
            {
              /* Towed ship destroyed! Disengage beam. */
              if (repo_ships_clear_is_being_towed_by (db, towed_sid) != 0
                  || repo_ships_clear_towing_id (db, ship_id) != 0)
                return -1;
            }
        }
    }

  return destroyed;
}

int
h_trigger_atmosphere_quasar (db_t *db, client_ctx_t *ctx, int planet_id)
{
  return h_trigger_atmosphere_quasar_report (db, ctx, planet_id, NULL);
}

int
h_trigger_atmosphere_quasar_report (db_t *db, client_ctx_t *ctx, int planet_id,
                                    json_t **outcome)
{
  int owner = 0, base = 0, react = 0;
  char type[32] = { 0 };
  if (!db || !ctx || planet_id <= 0)
    return -1;
  /* The construction repository distinguishes a planet with no citadel from
   * a failed database read. Do not turn an ambiguous defense lookup failure
   * into an unarmed planet. */
  int32_t level = 0, target_level = 0;
  int64_t started_at = 0, completes_at = 0;
  char status[32] = { 0 };
  int state_rc = repo_citadel_get_construction_state
    (db, planet_id, &level, status, sizeof status, &target_level,
     &started_at, &completes_at);
  if (state_rc == ERR_DB_NOT_FOUND)
    return 0;
  if (state_rc != 0)
    return -1;
  if (level < 3)
    return 0;
  int quasar_rc = db_combat_get_planet_atmosphere_quasar
    (db, planet_id, &owner, type, &base, &react);
  if (quasar_rc == ERR_DB_NOT_FOUND)
    return 0;
  if (quasar_rc != 0)
    return -1;
  int corp_id = (strcasecmp (type, "corp") == 0
                 || strcasecmp (type, "corporation") == 0) ? owner : 0;
  if (is_asset_hostile (owner, corp_id, ctx->player_id, ctx->corp_id))
    {
      int percent = react == 1 ? 125 : react >= 2 ? 150 : 100;
      int damage = (int) floor ((double) base * (double) percent / 100.0);
      return h_apply_quasar_damage (ctx, damage,
                                  "Planet atmosphere Quasar", planet_id,
                                  outcome);
    }
  return 0;
}

int
cmd_mines_recall (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  json_t *data = json_object_get (root, "data");
  int sid = (int) json_integer_value (json_object_get (data, "sector_id"));
  int aid = (int) json_integer_value (json_object_get (data, "asset_id"));
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  int cur = 0, max = 0, ship_sid = 0;
  db_combat_get_ship_mine_capacity (db, ship_id, &ship_sid, &cur, &max);
  int qty = 0, type = 0;
  db_combat_get_asset_info (db, aid, ctx->player_id, sid, &qty, &type);
  db_error_t err;
  db_tx_begin (db, DB_TX_DEFAULT, &err);
  db_combat_recall_mines (db, aid, ship_id, qty);
  db_tx_commit (db, &err);
  json_t *out = json_object ();
  json_object_set_new (out, "recalled", json_integer (qty));
  send_response_ok_take (ctx, root, "combat.mines.recalled", &out);
  return 0;
}

int
cmd_combat_sweep_mines (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "target_sector_id is required");
      return 0;
    }
  int tsid =
    (int) json_integer_value (json_object_get (data, "target_sector_id"));
  if (tsid <= 0 || tsid != ctx->sector_id)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "target_sector_id must be the current sector");
      return 0;
    }
  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR,
                           "Could not begin mine sweep");
      return 0;
    }

  json_t *mines = NULL;
  if (db_combat_select_mines_locked (db, tsid, 1, &mines) != 0 || !mines)
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_SERVER_ERROR,
                           "Could not read sector mines");
      return 0;
    }
  size_t i;
  json_t *m;
  int removed = 0;
  json_array_foreach (mines, i, m)
  {
    int aid = (int) json_integer_value (json_object_get (m, "id"));
    if (db_combat_delete_sector_asset (db, aid) == 0)
      removed++;
  }
  json_decref (mines);
  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_SERVER_ERROR,
                           "Mine sweep could not be committed");
      return 0;
    }
  if (removed > 0)
    {
      json_t *notice_details = json_object ();
      if (notice_details)
        {
          json_object_set_new (notice_details, "mines_removed",
                               json_integer (removed));
          server_sector_notice_publish (ctx, root, tsid, "mine_swept",
                                        notice_details);
          json_decref (notice_details);
        }
    }
  json_t *out = json_object ();
  json_object_set_new (out, "success", json_true ());
  json_object_set_new (out, "mines_removed", json_integer (removed));
  send_response_ok_take (ctx, root, "combat.mines_swept_v1", &out);
  return 0;
}

int
cmd_fighters_recall (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  json_t *data = json_object_get (root, "data");
  int aid = (int) json_integer_value (json_object_get (data, "asset_id"));
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  db_combat_add_ship_fighters (db, ship_id, 10);
  db_combat_delete_sector_asset (db, aid);
  json_t *out = json_object ();
  json_object_set_new (out, "success", json_true ());
  send_response_ok_take (ctx, root, "combat.fighters.deployed", &out);
  return 0;
}

int
cmd_combat_scrub_mines (client_ctx_t *ctx, json_t *root)
{
  (void) ctx;
  (void) root;
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  json_t *out = json_object ();
  json_object_set_new (out, "success", json_true ());
  send_response_ok_take (ctx, root, "combat.mines_scrubbed_v1", &out);
  return 0;
}
