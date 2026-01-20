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
#include "server_universe.h"
#include "server_combat.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_config.h"
#include "globals.h"
#include "server_planets.h"
#include "game_db.h"
#include "repo_cmd.h"
#include "repo_combat.h"
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
				  const char *source_desc);
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
  repo_combat_ship_t ship;
  if (db_combat_get_ship_stats (db, ship_id, &ship) != 1)
    return 0;
  int *ids = NULL, *quantities = NULL, *owners = NULL, *corps = NULL, *modes =
    NULL;
  int count = 0;
  if (db_combat_get_hostile_fighters
      (db, sector_id, &ids, &quantities, &owners, &corps, &modes,
       &count) != 0)
    return 0;
  for (int i = 0; i < count; i++)
    {
      if (!is_asset_hostile
	  (owners[i], corps[i], ctx->player_id, ctx->corp_id))
	continue;
      int damage = quantities[i] * damage_per_unit;
      ship_t s = {.hull = ship.hull,.fighters = ship.fighters,.shields =
	  ship.shields };
      armid_damage_breakdown_t breakdown = { 0 };
      apply_armid_damage_to_ship (&s, damage, &breakdown);
      ship.hull = s.hull;
      ship.fighters = s.fighters;
      ship.shields = s.shields;
      db_combat_update_ship_stats (db, ship_id, ship.hull, ship.fighters,
				   ship.shields);
      db_combat_delete_sector_asset (db, ids[i]);
      json_t *evt = json_object ();
      json_object_set_new (evt, "damage", json_integer (damage));
      json_object_set_new (evt, "fighters_engaged",
			   json_integer (quantities[i]));
      db_log_engine_event ((long long) time (NULL), "combat.hit.fighters",
			   "player", ctx->player_id, sector_id, evt, NULL);
      json_decref (evt);
      if (ship.hull <= 0)
	{
	  free (ids);
	  free (quantities);
	  free (owners);
	  free (corps);
	  free (modes);
	  destroy_ship_and_handle_side_effects (ctx, ctx->player_id);
	  return 1;
	}
    }
  free (ids);
  free (quantities);
  free (owners);
  free (corps);
  free (modes);
  return 0;
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
    return 0;
  int shot_fired = 0;
  json_t *planets = NULL;
  if (db_combat_get_planet_quasar_info (db, sector_id, &planets) == 0)
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
	    if (h_apply_quasar_damage (ctx, damage, source_desc))
	      shot_fired = 1;
	    else
	      shot_fired = 2;
	    break;
	  }
      }
      json_decref (planets);
    }
  return (shot_fired == 1);
}

int
apply_armid_mines_on_entry (client_ctx_t *ctx, int new_sector_id,
			    armid_encounter_t *out_enc)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    return -1;
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;
  if (out_enc)
    {
      memset (out_enc, 0, sizeof (*out_enc));
      out_enc->sector_id = new_sector_id;
    }
  ship_t ship_stats = { 0 };
  db_error_t err;
  db_error_clear (&err);
  if (db_ship_get_combat_stats (db, ship_id, &ship_stats, &err) <= 0)
    return 0;
  int damage_per_mine = db_get_config_int (db, "armid_damage_per_mine", 25);
  if (g_armid_config.armid.enabled)
    {
      json_t *mines = NULL;
      if (db_combat_select_armid_mines_locked (db, new_sector_id, &mines) ==
	  0)
	{
	  size_t i;
	  json_t *m;
	  json_array_foreach (mines, i, m)
	  {
	    int mine_id = json_integer_value (json_object_get (m, "id"));
	    int mine_quantity =
	      json_integer_value (json_object_get (m, "quantity"));
	    int owner_id =
	      json_integer_value (json_object_get (m, "owner_id"));
	    int corp_id = json_integer_value (json_object_get (m, "corp_id"));
	    if (!is_asset_hostile
		(owner_id, corp_id, ctx->player_id, ctx->corp_id))
	      continue;
	    int damage = mine_quantity * damage_per_mine;
	    armid_damage_breakdown_t d = { 0 };
	    apply_armid_damage_to_ship (&ship_stats, damage, &d);
	    db_combat_delete_sector_asset (db, mine_id);
	    if (out_enc)
	      {
		out_enc->armid_triggered += mine_quantity;
		out_enc->shields_lost += d.shields_lost;
		out_enc->fighters_lost += d.fighters_lost;
		out_enc->hull_lost += d.hull_lost;
	      }
	    json_t *hit_data = json_object ();
	    json_object_set_new (hit_data, "attacker_id",
				 json_integer (owner_id));
	    json_object_set_new (hit_data, "defender_id",
				 json_integer (ctx->player_id));
	    json_object_set_new (hit_data, "weapon",
				 json_string ("armid_mines"));
	    json_object_set_new (hit_data, "damage_total",
				 json_integer (damage));
	    db_log_engine_event ((long long) time (NULL), "combat.hit",
				 "player", ctx->player_id, new_sector_id,
				 hit_data, NULL);
	    json_decref (hit_data);
	    db_combat_persist_ship_damage (db, ship_id, ship_stats.hull,
					   ship_stats.shields, 0);
	    if (ship_stats.hull <= 0)
	      {
		destroy_ship_and_handle_side_effects (ctx, ctx->player_id);
		if (out_enc)
		  out_enc->destroyed = true;
		break;
	      }
	  }
	  json_decref (mines);
	}
    }
  return 0;
}

int
apply_limpet_mines_on_entry (client_ctx_t *ctx, int new_sector_id,
			     armid_encounter_t *out_enc)
{
  (void) out_enc;
  db_t *db = game_db_get_handle ();
  if (!db)
    return -1;
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;
  if (!g_cfg.mines.limpet.enabled)
    return 0;
  json_t *limpets = NULL;
  if (db_combat_select_limpets_locked (db, new_sector_id, &limpets) != 0)
    return -1;
  int64_t now_ts = (int64_t) time (NULL);
  size_t i;
  json_t *limpet;
  json_array_foreach (limpets, i, limpet)
  {
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
    if (db_combat_check_limpet_attached (db, ship_id, owner_id, &attached) !=
	0 || attached)
      continue;
    if (db_combat_decrement_or_delete_asset (db, asset_id, quantity) == 0)
      {
	db_combat_attach_limpet (db, ship_id, owner_id, now_ts);
	json_t *event_data = json_object ();
	json_object_set_new (event_data, "target_ship_id",
			     json_integer (ship_id));
	json_object_set_new (event_data, "target_player_id",
			     json_integer (ctx->player_id));
	db_log_engine_event (now_ts, "combat.limpet.attached", "player",
			     owner_id, new_sector_id, event_data, NULL);
	json_decref (event_data);
      }
  }
  json_decref (limpets);
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
	      h_handle_sector_entry_hazards (db, ctx, dest);
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
    {
      json_get_int_flexible (data, "count", &amount);
    }

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
  if (!json_get_int_flexible (data, "amount", &amount))
    {
      json_get_int_flexible (data, "count", &amount);
    }

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
  json_get_int_flexible (data, "target_ship_id", &target_id);
  int attacker_id = h_get_active_ship_id (db, ctx->player_id);
  if (attacker_id <= 0)
    return 0;
  db_error_t err;
  db_error_clear (&err);
  db_tx_begin (db, DB_TX_DEFAULT, &err);
  combat_ship_t att = { 0 }, def = { 0 };
  load_ship_combat_stats_locked (db, attacker_id, &att, true);
  load_ship_combat_stats_locked (db, target_id, &def, true);
  int s_lost = 0, h_lost = 0;
  apply_combat_damage (&def, 10, &s_lost, &h_lost);
  persist_ship_damage (db, &def, 0);
  db_tx_commit (db, &err);
  json_t *resp = json_object ();
  json_object_set_new (resp, "success", json_true ());
  send_response_ok_take (ctx, root, "combat.attack.result", &resp);
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
h_apply_quasar_damage (client_ctx_t *ctx, int damage, const char *source_desc)
{
  (void) source_desc;
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    return 0;
  db_error_t err;
  db_error_clear (&err);
  db_tx_begin (db, DB_TX_DEFAULT, &err);
  combat_ship_t ship = { 0 };
  load_ship_combat_stats_locked (db, ship_id, &ship, true);
  ship.hull -= damage;
  persist_ship_damage (db, &ship, 0);
  db_tx_commit (db, &err);
  if (ship.hull <= 0)
    destroy_ship_and_handle_side_effects (ctx, ctx->player_id);
  return 0;
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
      target->hull -= rem;
      *hull_lost = rem;
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

int
destroy_ship_and_handle_side_effects (client_ctx_t *ctx, int player_id)
{
  (void) ctx;
  (void) player_id;
  return 0;
}

int
h_decloak_ship (db_t *db, int ship_id)
{
  (void) db;
  (void) ship_id;
  return 0;
}

int
h_handle_sector_entry_hazards (db_t *db, client_ctx_t *ctx, int sector_id)
{
  (void) db;
  int ship_id = h_get_active_ship_id (game_db_get_handle (), ctx->player_id);
  if (ship_id <= 0)
    return 0;
  json_t *f = NULL;
  db_combat_get_sector_fighters_locked (game_db_get_handle (), sector_id, &f);
  size_t i;
  json_t *item;
  json_array_foreach (f, i, item)
  {
    int owner = json_integer_value (json_object_get (item, "owner_id"));
    if (is_asset_hostile (owner, 0, ctx->player_id, ctx->corp_id))
      {
	h_apply_quasar_damage (ctx, 10, "Fighters");
      }
  }
  json_decref (f);
  return 0;
}

int
h_trigger_atmosphere_quasar (db_t *db, client_ctx_t *ctx, int planet_id)
{
  int owner = 0, base = 0, react = 0;
  char type[32] = { 0 };
  if (db_combat_get_planet_atmosphere_quasar
      (db, planet_id, &owner, type, &base, &react) == 0)
    {
      if (is_asset_hostile (owner, 0, ctx->player_id, ctx->corp_id))
	h_apply_quasar_damage (ctx, base, "Quasar");
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
  db_t *db = game_db_get_handle ();
  if (!db)
    return 0;
  json_t *data = json_object_get (root, "data");
  int tsid =
    (int) json_integer_value (json_object_get (data, "target_sector_id"));
  json_t *mines = NULL;
  db_combat_select_mines_locked (db, tsid, 1, &mines);
  size_t i;
  json_t *m;
  json_array_foreach (mines, i, m)
  {
    int aid = (int) json_integer_value (json_object_get (m, "id"));
    db_combat_delete_sector_asset (db, aid);
  }
  json_decref (mines);
  json_t *out = json_object ();
  json_object_set_new (out, "success", json_true ());
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
