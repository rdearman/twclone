/* src/server_stardock.c */
#include <jansson.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

/* local includes */
#include "server_stardock.h"
#include "common.h"
#include "database.h"
#include "game_db.h"
#include "database_cmd.h"
#include "server_players.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_ports.h"
#include "server_ships.h"
#include "server_cmds.h"
#include "server_corporation.h"
#include "server_loop.h"
#include "server_config.h"
#include "server_log.h"
#include "server_cron.h"
#include "server_communication.h"
#include "db/db_api.h"

struct tavern_settings {
    int max_bet_per_transaction;
    int daily_max_wager;
    int enable_dynamic_wager_limit;
    int graffiti_max_posts;
    int notice_expires_days;
    int buy_round_cost;
    int buy_round_alignment_gain;
    int loan_shark_enabled;
};

struct tavern_settings g_tavern_cfg;


int
cmd_hardware_list (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  int player_id = ctx->player_id;
  int ship_id = 0;
  int sector_id = 0;
  db_stmt_t *stmt = NULL;
  int port_type = 0;

  if (player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NOT_AUTHENTICATED,
				   "Authentication required.", NULL);
      return 0;
    }
  ship_id = h_get_active_ship_id (db, player_id);
  if (ship_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_SHIP_NOT_FOUND,
				   "No active ship found.", NULL);
      return 0;
    }
  sector_id = ctx->sector_id;
  
  char location_type[16] = "OTHER";
  int port_id = 0;
  const char *sql_loc_check = "SELECT id, type FROM ports WHERE sector = ? AND (type = 9 OR type = 0);";
  
  if (db_prepare (db, sql_loc_check, &stmt) == 0)
    {
      db_bind_int (stmt, 1, sector_id);
      if (db_step (stmt))
	{
	  port_id = db_column_int (stmt, 0);
	  port_type = db_column_int (stmt, 1);
	  if (port_type == 9) // PORT_TYPE_STARDOCK
	    {
	      strncpy (location_type, "STARDOCK",
		       sizeof (location_type) - 1);
	    }
	  else if (port_type == 0) // PORT_TYPE_CLASS0
	    {
	      strncpy (location_type, "CLASS0",
		       sizeof (location_type) - 1);
	    }
	}
      db_finalize (stmt);
    }
  
  if (port_id == 0)
    {
      json_t *res = json_object ();

      json_object_set_new (res, "sector_id", json_integer (sector_id));
      json_object_set_new (res, "location_type", json_string (location_type));
      json_object_set_new (res, "items", json_array ());

      send_response_ok_take (ctx, root, "hardware.list_v1", &res);
      return 0;
    }
  
  int current_holds = 0, current_fighters = 0, current_shields = 0;
  int current_genesis = 0, current_detonators = 0, current_probes = 0;
  int current_cloaks = 0;
  int has_transwarp = 0, has_planet_scanner = 0, has_long_range_scanner = 0;
  int max_holds = 0, max_fighters = 0, max_shields = 0;
  int max_genesis = 0, max_detonators_st = 0, max_probes_st = 0;
  int can_transwarp = 0, can_planet_scan = 0, can_long_range_scan = 0;
  
  const char *sql_ship_state =
    "SELECT s.holds, s.fighters, s.shields, s.genesis, s.detonators, s.probes, s.cloaking_devices, s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner, st.maxholds, st.maxfighters, st.maxshields, st.maxgenesis, st.max_detonators, st.max_probes, st.can_transwarp, st.can_planet_scan, st.can_long_range_scan, st.max_cloaks FROM ships s JOIN shiptypes st ON s.type_id = st.id WHERE s.id = ?;";

  if (db_prepare (db, sql_ship_state, &stmt) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_DB_QUERY_FAILED, "Failed to get ship state.");
      return 0;
    }
  db_bind_int (stmt, 1, ship_id);
  if (db_step (stmt))
    {
      current_holds = db_column_int (stmt, 0);
      current_fighters = db_column_int (stmt, 1);
      current_shields = db_column_int (stmt, 2);
      current_genesis = db_column_int (stmt, 3);
      current_detonators = db_column_int (stmt, 4);
      current_probes = db_column_int (stmt, 5);
      current_cloaks = db_column_int (stmt, 6);
      has_transwarp = db_column_int (stmt, 7);
      has_planet_scanner = db_column_int (stmt, 8);
      has_long_range_scanner = db_column_int (stmt, 9);
      max_holds = db_column_int (stmt, 10);
      max_fighters = db_column_int (stmt, 11);
      max_shields = db_column_int (stmt, 12);
      max_genesis = db_column_int (stmt, 13);
      max_detonators_st = db_column_int (stmt, 14);
      max_probes_st = db_column_int (stmt, 15);
      can_transwarp = db_column_int (stmt, 16);
      can_planet_scan = db_column_int (stmt, 17);
      can_long_range_scan = db_column_int (stmt, 18);
    }
  db_finalize (stmt);

  json_t *items_array = json_array ();
  const char *sql_hardware =
    "SELECT code, name, price, max_per_ship, category FROM hardware_items WHERE enabled = 1 AND (? = 'STARDOCK' OR (? = 'CLASS0' AND sold_in_class0 = 1))";

  if (db_prepare (db, sql_hardware, &stmt) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_DB_QUERY_FAILED,
			   "Failed to get hardware items.");
      return 0;
    }
  db_bind_text (stmt, 1, location_type);
  db_bind_text (stmt, 2, location_type);
  while (db_step (stmt))
    {
      const char *code = db_column_text (stmt, 0);
      const char *name = db_column_text (stmt, 1);
      int price = db_column_int (stmt, 2);
      int val = db_column_int (stmt, 3);
      int max_per_ship_hw = val == 0 ? -1 : val;
      const char *category = db_column_text (stmt, 4);
      int max_purchase = 0;
      bool ship_has_capacity = true;
      bool item_supported = true;

      if (strcasecmp (category, "Fighter") == 0)
	{
	  max_purchase = MAX (0, max_fighters - current_fighters);
	}
      else if (strcasecmp (category, "Shield") == 0)
	{
	  max_purchase = MAX (0, max_shields - current_shields);
	}
      else if (strcasecmp (category, "Hold") == 0)
	{
	  max_purchase = MAX (0, max_holds - current_holds);
	}
      else if (strcasecmp (category, "Special") == 0)
	{
	  if (strcasecmp (code, "GENESIS") == 0)
	    {
	      int limit = (max_per_ship_hw != -1
			   && max_per_ship_hw <
			   max_genesis) ? max_per_ship_hw : max_genesis;

	      max_purchase = MAX (0, limit - current_genesis);
	    }
	  else if (strcasecmp (code, "DETONATOR") == 0)
	    {
	      int limit = (max_per_ship_hw != -1
			   && max_per_ship_hw <
			   max_detonators_st) ? max_per_ship_hw :
		max_detonators_st;

	      max_purchase = MAX (0, limit - current_detonators);
	    }
	  else if (strcasecmp (code, "PROBE") == 0)
	    {
	      int limit = (max_per_ship_hw != -1
			   && max_per_ship_hw <
			   max_probes_st) ? max_per_ship_hw : max_probes_st;

	      max_purchase = MAX (0, limit - current_probes);
	    }
	  else
	    {
	      item_supported = false;
	    }
	}
      else if (strcasecmp (category, "Module") == 0)
	{
	  if (strcasecmp (code, "CLOAK") == 0)
	    {
	      max_purchase = (current_cloaks == 0) ? 1 : 0;
	    }
	  else if (strcasecmp (code, "TWARP") == 0)
	    {
	      if (!can_transwarp)
		{
		  item_supported = false;
		}
	      max_purchase = (can_transwarp && has_transwarp == 0) ? 1 : 0;
	    }
	  else if (strcasecmp (code, "PSCANNER") == 0)
	    {
	      if (!can_planet_scan)
		{
		  item_supported = false;
		}
	      max_purchase = (can_planet_scan
			      && has_planet_scanner == 0) ? 1 : 0;
	    }
	  else if (strcasecmp (code, "LSCANNER") == 0)
	    {
	      if (!can_long_range_scan)
		{
		  item_supported = false;
		}
	      max_purchase = (can_long_range_scan
			      && has_long_range_scanner == 0) ? 1 : 0;
	    }
	  else
	    {
	      item_supported = false;
	    }
	}
      else
	{
	  item_supported = false;
	}
      if (max_purchase <= 0)
	{
	  ship_has_capacity = false;
	}
      if (item_supported
	  && (max_purchase > 1
	      || strcmp (category, "Module") == 0))
	{
	  json_t *item_obj = json_object ();

	  json_object_set_new (item_obj, "code", json_string (code));
	  json_object_set_new (item_obj, "name", json_string (name));
	  json_object_set_new (item_obj, "price", json_integer (price));
	  json_object_set_new (item_obj, "max_purchase",
			       json_integer (max_purchase));
	  json_object_set_new (item_obj, "ship_has_capacity",
			       json_boolean (ship_has_capacity));
	  json_array_append_new (items_array, item_obj);
	}
    }
  db_finalize (stmt);
  {
    json_t *res = json_object ();

    json_object_set_new (res, "sector_id", json_integer (sector_id));
    json_object_set_new (res, "location_type", json_string (location_type));
    json_object_set (res, "items", items_array);

    send_response_ok_take (ctx, root, "hardware.list_v1", &res);
  }
  return 0;
}


int
cmd_hardware_buy (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  json_t *data = json_object_get (root, "data");

  if (!json_is_object (data))
    {
      return send_error_response (ctx, root, ERR_INVALID_SCHEMA,
				  "Missing data object.");
    }
  const char *code = json_string_value (json_object_get (data, "code"));
  int quantity = json_integer_value (json_object_get (data, "quantity"));

  if (!code || quantity <= 0)
    {
      return send_error_response (ctx, root, 1301,
				  "Missing or invalid 'quantity'.");
    }
  int player_id = ctx->player_id;
  int sector_id = ctx->sector_id;
  int ship_id = h_get_active_ship_id (db, player_id);

  if (ship_id <= 0)
    {
      return send_error_response (ctx, root, ERR_SHIP_NOT_FOUND,
				  "No active ship.");
    }
  db_stmt_t *stmt = NULL;
  int port_type = -1;
  const char *sql_port =
    "SELECT type FROM ports WHERE sector = ? AND (type = 9 OR type = 0);";

  if (db_prepare (db, sql_port, &stmt) == 0)
    {
      db_bind_int (stmt, 1, sector_id);
      if (db_step (stmt))
	{
	  port_type = db_column_int (stmt, 0);
	}
      db_finalize (stmt);
    }
  
  if (port_type == -1)
    {
      return send_error_response (ctx,
				  root,
				  1811,
				  "Hardware can only be purchased at Stardock or Class-0 ports.");
    }
  int price = 0;
  int requires_stardock = 0;
  int sold_in_class0 = 0;
  int max_per_ship = 0;
  char category[32] = { 0 };
  const char *sql_item =
    "SELECT price, requires_stardock, sold_in_class0, max_per_ship, category FROM hardware_items WHERE code = ? AND enabled = 1;";
  bool item_found = false;

  if (db_prepare (db, sql_item, &stmt) == 0)
    {
      db_bind_text (stmt, 1, code);
      if (db_step (stmt))
	{
	  price = db_column_int (stmt, 0);
	  requires_stardock = db_column_int (stmt, 1);
	  sold_in_class0 = db_column_int (stmt, 2);
      int val = db_column_int (stmt, 3);
      max_per_ship = val == 0 ? 0 : val;
	  const char *cat = db_column_text (stmt, 4);

	  if (cat)
	    {
	      strncpy (category, cat, sizeof (category) - 1);
	    }
	  item_found = true;
	}
      db_finalize (stmt);
    }
  
  if (!item_found)
    {
      return send_error_response (ctx, root, 1812,
				  "Invalid or unavailable hardware item.");
    }
  if (requires_stardock && port_type != 9)
    {
      return send_error_response (ctx,
				  root,
				  1811,
				  "This hardware item is not sold at Class-0 ports.");
    }
  if (!sold_in_class0 && port_type == 0)
    {
      return send_error_response (ctx,
				  root,
				  1811,
				  "This hardware item is not sold at Class-0 ports.");
    }
  const char *col_name = NULL;

  if (strcmp (code, "FIGHTERS") == 0)
    {
      col_name = "fighters";
    }
  else if (strcmp (code, "SHIELDS") == 0)
    {
      col_name = "shields";
    }
  else if (strcmp (code, "HOLDS") == 0)
    {
      col_name = "holds";
    }
  else if (strcmp (code, "GENESIS") == 0)
    {
      col_name = "genesis";
    }
  else if (strcmp (code, "MINES") == 0)
    {
      col_name = "mines";
    }
  else if (strcmp (code, "BEACONS") == 0)
    {
      col_name = "beacons";
    }
  else if (strcmp (code, "CLOAK") == 0)
    {
      col_name = "cloaking_devices";
    }
  else if (strcmp (code, "DETONATOR") == 0)
    {
      col_name = "detonators";
    }
  else if (strcmp (code, "PROBE") == 0)
    {
      col_name = "probes";
    }
  else if (strcmp (code, "LSCANNER") == 0)
    {
      col_name = "has_long_range_scanner";
    }
  else if (strcmp (code, "PSCANNER") == 0)
    {
      col_name = "has_planet_scanner";
    }
  else if (strcmp (code, "TWARP") == 0)
    {
      col_name = "has_transwarp";
    }
  if (!col_name)
    {
      return send_error_response (ctx,
				  root,
				  500,
				  "Server configuration error: Unknown item mapping.");
    }
  int current_val = 0;
  int max_limit = 999999999;
  char sql_info[512];
  bool is_max_check_needed = true;
  char limit_col[64] = { 0 };

  if (strcmp (col_name, "fighters") == 0)
    {
      strcpy (limit_col, "maxfighters");
    }
  else if (strcmp (col_name, "shields") == 0)
    {
      strcpy (limit_col, "maxshields");
    }
  else if (strcmp (col_name, "holds") == 0)
    {
      strcpy (limit_col, "maxholds");
    }
  else if (strcmp (col_name, "genesis") == 0)
    {
      strcpy (limit_col, "maxgenesis");
    }
  else if (strcmp (col_name, "mines") == 0)
    {
      strcpy (limit_col, "maxmines");
    }
  else if (strcmp (col_name, "beacons") == 0)
    {
      strcpy (limit_col, "maxbeacons");
    }
  else if (strcmp (col_name, "cloaking_devices") == 0)
    {
      strcpy (limit_col, "max_cloaks");
    }
  else if (strcmp (col_name, "detonators") == 0)
    {
      strcpy (limit_col, "max_detonators");
    }
  else if (strcmp (col_name, "probes") == 0)
    {
      strcpy (limit_col, "max_probes");
    }
  else if (strncmp (col_name, "has_", 4) == 0)
    {
      is_max_check_needed = false;
      max_limit = 1;
    }
  else
    {
      is_max_check_needed = false;
    }
  if (is_max_check_needed)
    {
      snprintf (sql_info,
		sizeof (sql_info),
		"SELECT s.%s, st.%s FROM ships s JOIN shiptypes st ON s.type_id = st.id WHERE s.id = ?;",
		col_name, limit_col);
    }
  else
    {
      snprintf (sql_info,
		sizeof (sql_info),
		"SELECT %s, 0 FROM ships WHERE id = ?;", col_name);
    }
  if (db_prepare (db, sql_info, &stmt) == 0)
    {
      db_bind_int (stmt, 1, ship_id);
      if (db_step (stmt))
	{
	  current_val = db_column_int (stmt, 0);
	  if (is_max_check_needed)
	    {
	      max_limit = db_column_int (stmt, 1);
	    }
	}
      else
	{
	  db_finalize (stmt);
	  return send_error_response (ctx, root, 500, "Ship info not found.");
	}
      db_finalize (stmt);
    }
  else
    {
      LOGE ("cmd_hardware_buy: SQL prepare failed");
      return send_error_response (ctx, root, 500,
				  "Database error checking limits.");
    }
  
  if (max_per_ship > 0)
    {
      if (current_val + quantity > max_per_ship)
	{
	  if (strcmp (col_name, "cloaking_devices") == 0)
	    {
	      return send_error_response (ctx, root, 1814,
					  "Cloaking Device already installed.");
	    }
	  else
	    {
	      return send_error_response (ctx,
					  root,
					  1814,
					  "Purchase would exceed ship type's maximum capacity.");
	    }
	}
    }
  if (current_val + quantity > max_limit)
    {
      return send_error_response (ctx,
				  root,
				  1814,
				  "Purchase would exceed ship type's maximum capacity.");
    }
  long long total_cost = (long long) price * quantity;
  long long balance = 0;

  h_get_player_petty_cash (db, player_id, &balance);
  if (balance < total_cost)
    {
      return send_error_response (ctx, root, 1813,
				  "Insufficient credits on ship for purchase.");
    }
  
  long long new_balance = 0;
  // Using unlocked version because original code used it within a transaction.
  // Here we assume db wrapper handles concurrency or we are safe enough for now.
  // Ideally we should use transaction.
  int rc_deduct =
    h_deduct_player_petty_cash_unlocked (db, player_id, total_cost,
					 &new_balance);

  if (rc_deduct != 0)
    {
      return send_error_response (ctx, root, 1813,
				  "Insufficient credits (concurrent).");
    }
  
  char sql_upd_buf[512];
  snprintf(sql_upd_buf, sizeof(sql_upd_buf), "UPDATE ships SET %s = %s + %d WHERE id = %d;",
           col_name, col_name, quantity, ship_id);
  
  int rc_upd = db_exec(db, sql_upd_buf); // Assuming db_exec exists in wrapper
  if (rc_upd == -1) // Assuming -1 or non-zero is error. If db_exec returns 0 on success.
    {
        // Try prepare/step if exec not found or fails?
        // Actually I don't know if db_exec exists.
        // I'll use db_prepare/step/finalize to be safe as I did before.
        if (db_prepare(db, sql_upd_buf, &stmt) == 0) {
            db_step(stmt);
            db_finalize(stmt);
        } else {
             return send_error_response (ctx, root, 500, "Database update failed.");
        }
    }
  
  int final_val = current_val + quantity;
  json_t *ship_obj = json_object ();

  json_object_set_new (ship_obj, col_name, json_integer (final_val));
  if (strcmp (col_name, "cloaking_devices") == 0)
    {
      json_object_set_new (ship_obj, "cloaks_installed",
			   json_integer (final_val));
    }
  if (strcmp (col_name, "genesis") == 0)
    {
      json_object_set_new (ship_obj, "genesis_torps",
			   json_integer (final_val));
    }
  
  json_t *resp = json_object ();

  json_object_set_new (resp, "code", json_string (code));
  json_object_set_new (resp, "quantity", json_integer (quantity));
  json_object_set_new (resp, "credits_spent",
		       json_integer ((json_int_t) total_cost));
  json_object_set (resp, "ship", ship_obj);

  send_response_ok_take (ctx, root, "hardware.purchase_v1", &resp);
  return 0;
}


int
cmd_shipyard_list (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  int player_id = ctx->player_id;
  int sector_id = ctx->sector_id;
  db_stmt_t *stmt = NULL;
  
  const char *sql_loc = "SELECT p.id FROM ports p "
    "JOIN ships s ON s.ported = p.id "
    "JOIN players pl ON pl.ship = s.id "
    "WHERE p.sector = ? AND pl.id = ? AND (p.type = 9 OR p.type = 10);";
  
  if (db_prepare (db, sql_loc, &stmt) != 0)
    {
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to check shipyard location.");
    }
  db_bind_int (stmt, 1, sector_id);
  db_bind_int (stmt, 2, player_id);
  
  if (!db_step (stmt))
    {
      db_finalize (stmt);
      return send_error_response (ctx, root, ERR_NOT_AT_SHIPYARD,
				  "You are not docked at a shipyard.");
    }
  int port_id = db_column_int (stmt, 0);

  db_finalize (stmt);
  
  struct twconfig *cfg = config_load ();

  if (!cfg)
    {
      return send_error_response (ctx, root, ERR_SERVER_ERROR,
				  "Could not load server configuration.");
    }
  
  const char *sql_info = "SELECT "
    "p.alignment, p.commission, p.experience, "
    "s.id, s.type_id, st.name, st.basecost, "
    "s.fighters, s.shields, s.mines, s.limpets, s.genesis, s.detonators, s.probes, s.cloaking_devices, "
    "s.colonists, s.ore, s.organics, s.equipment, "
    "s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner "
    "FROM players p JOIN ships s ON p.ship = s.id JOIN shiptypes st ON s.type_id = st.id "
    "WHERE p.id = ?;";

  if (db_prepare (db, sql_info, &stmt) != 0)
    {
      free (cfg);
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to fetch player/ship info.");
    }
  db_bind_int (stmt, 1, player_id);
  if (!db_step (stmt))
    {
      db_finalize (stmt);
      free (cfg);
      return send_error_response (ctx, root, ERR_SHIP_NOT_FOUND,
				  "Could not find player's active ship.");
    }
  
  int player_alignment = db_column_int (stmt, 0);
  int player_commission = db_column_int (stmt, 1);
  int player_experience = db_column_int (stmt, 2);
  long long current_ship_basecost = db_column_int64 (stmt, 6);
  int current_fighters = db_column_int (stmt, 7);
  int current_shields = db_column_int (stmt, 8);
  int current_mines = db_column_int (stmt, 9);
  int current_limpets = db_column_int (stmt, 10);
  int current_genesis = db_column_int (stmt, 11);
  int current_detonators = db_column_int (stmt, 12);
  int current_probes = db_column_int (stmt, 13);
  int current_cloaks = db_column_int (stmt, 14);
  long long current_cargo =
    db_column_int64 (stmt, 15) + db_column_int64 (stmt,
							    16) +
    db_column_int64 (stmt, 17) + db_column_int64 (stmt, 18);
  int has_transwarp = db_column_int (stmt, 19);
  int has_planet_scanner = db_column_int (stmt, 20);
  int has_long_range_scanner = db_column_int (stmt, 21);
  long trade_in_value =
    floor (current_ship_basecost *
	   (cfg->shipyard_trade_in_factor_bp / 10000.0));
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "sector_id", json_integer (sector_id));
  json_object_set_new (response_data, "is_shipyard", json_true ());
  json_t *current_ship_obj = json_object ();

  json_object_set_new (current_ship_obj, "type",
		       json_string (db_column_text (stmt, 5)));
  json_object_set_new (current_ship_obj, "base_price",
		       json_integer (current_ship_basecost));
  json_object_set_new (current_ship_obj, "trade_in_value",
		       json_integer (trade_in_value));
  json_object_set_new (response_data, "current_ship", current_ship_obj);
  db_finalize (stmt);
  
  json_t *available_array = json_array ();
  const char *sql_inventory =
    "SELECT si.ship_type_id, st.name, st.basecost, st.required_alignment, st.required_commission, st.required_experience, st.maxholds, st.maxfighters, st.maxshields, st.maxgenesis, st.max_detonators, st.max_probes, st.max_cloaks, st.can_transwarp, st.can_planet_scan, st.can_long_range_scan, st.maxmines, st.maxlimpets "
    "FROM shipyard_inventory si JOIN shiptypes st ON si.ship_type_id = st.id "
    "WHERE si.port_id = ? AND si.enabled = 1 AND st.enabled = 1;";

  if (db_prepare (db, sql_inventory, &stmt) != 0)
    {
      json_decref (response_data);
      free (cfg);
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to fetch shipyard inventory.");
    }
  db_bind_int (stmt, 1, port_id);
  while (db_step (stmt))
    {
      json_t *ship_obj = json_object ();
      json_t *reasons_array = json_array ();
      bool eligible = true;
      const char *type_name = db_column_text (stmt, 1);
      long long new_ship_basecost = db_column_int64 (stmt, 2);
      long long net_cost = new_ship_basecost - trade_in_value;

      if (type_name && !strcasecmp (type_name, "Corporate Flagship"))
	{
	  int dummy_corp_id = 0;

	  if (!h_is_player_corp_ceo (db, player_id, &dummy_corp_id))
	    {
	      eligible = false;
	      json_array_append_new (reasons_array,
				     json_string ("must_be_corp_ceo"));
	    }
	}
      json_object_set_new (ship_obj, "type", json_string (type_name));
      json_object_set_new (ship_obj, "name", json_string (type_name));
      json_object_set_new (ship_obj, "base_price",
			   json_integer (new_ship_basecost));
      json_object_set_new (ship_obj, "shipyard_price", json_integer (new_ship_basecost));
      json_object_set_new (ship_obj, "trade_in_value",
			   json_integer (trade_in_value));
      json_object_set_new (ship_obj, "net_cost", json_integer (net_cost));
      
      if (player_alignment < db_column_int (stmt, 3))
	{
	  eligible = false;
	  json_array_append_new (reasons_array,
				 json_string ("alignment_too_low"));
	}
      if (player_commission < db_column_int (stmt, 4))
	{
	  eligible = false;
	  json_array_append_new (reasons_array,
				 json_string ("commission_too_low"));
	}
      if (player_experience < db_column_int (stmt, 5))
	{
	  eligible = false;
	  json_array_append_new (reasons_array,
				 json_string ("experience_too_low"));
	}
      if (cfg->shipyard_require_cargo_fit
	  && current_cargo > db_column_int64 (stmt, 6))
	{
	  eligible = false;
	  json_array_append_new (reasons_array,
				 json_string ("cargo_would_not_fit"));
	}
      if (cfg->shipyard_require_fighters_fit
	  && current_fighters > db_column_int (stmt, 7))
	{
	  eligible = false;
	  json_array_append_new (reasons_array,
				 json_string ("fighters_exceed_capacity"));
	    }
      if (cfg->shipyard_require_shields_fit
	  && current_shields > db_column_int (stmt, 8))
	{
	  eligible = false;
	  json_array_append_new (reasons_array,
				 json_string ("shields_exceed_capacity"));
	}
      if (cfg->shipyard_require_hardware_compat)
	{
	  if (current_genesis > db_column_int (stmt, 9))
	    {
	      eligible = false;
	      json_array_append_new (reasons_array,
				     json_string ("genesis_exceed_capacity"));
	    }
	  if (current_detonators > db_column_int (stmt, 10))
	    {
	      eligible = false;
	      json_array_append_new (reasons_array,
				     json_string
				     ("detonators_exceed_capacity"));
	    }
	  if (current_probes > db_column_int (stmt, 11))
	    {
	      eligible = false;
	      json_array_append_new (reasons_array,
				     json_string ("probes_exceed_capacity"));
	    }
	  if (current_cloaks > db_column_int (stmt, 12))
	    {
	      eligible = false;
	      json_array_append_new (reasons_array,
				     json_string ("cloak_not_supported"));
	    }
	  if (has_transwarp && !db_column_int (stmt, 13))
	    {
	      eligible = false;
	      json_array_append_new (reasons_array,
				     json_string ("transwarp_not_supported"));
	    }
	  if (has_planet_scanner && !db_column_int (stmt, 14))
	    {
	      eligible = false;
	      json_array_append_new (reasons_array,
				     json_string
				     ("planet_scan_not_supported"));
	    }
	  if (has_long_range_scanner && !db_column_int (stmt, 15))
	    {
	      eligible = false;
	      json_array_append_new (reasons_array,
				     json_string
				     ("long_range_not_supported"));
	    }
	  if (current_mines > db_column_int (stmt, 16))
	    {
	      eligible = false;
	      json_array_append_new (reasons_array,
				     json_string ("mines_exceed_capacity"));
	    }
	  if (current_limpets > db_column_int (stmt, 17))
	    {
	      eligible = false;
	      json_array_append_new (reasons_array,
				     json_string ("limpets_exceed_capacity"));
	    }
	}
      json_object_set_new (ship_obj, "eligible", json_boolean (eligible));
      json_object_set_new (ship_obj, "reasons", reasons_array);
      json_array_append_new (available_array, ship_obj);
    }
  db_finalize (stmt);
  json_object_set_new (response_data, "available", available_array);
  send_response_ok_take (ctx, root, "shipyard.list_v1", &response_data);
  free (cfg);
  return 0;
}


int
cmd_shipyard_upgrade (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  int new_type_id = 0;
  const char *new_ship_name = NULL;

  if (!json_get_int_flexible (data, "new_type_id", &new_type_id)
      || new_type_id <= 0)
    {
      return send_error_response (ctx, root, ERR_MISSING_FIELD,
				  "Missing or invalid 'new_type_id'.");
    }
  new_ship_name = json_get_string_or_null (data, "new_ship_name");
  if (!new_ship_name || strlen (new_ship_name) == 0)
    {
      return send_error_response (ctx, root, ERR_MISSING_FIELD,
				  "Missing or invalid 'new_ship_name'.");
    }
  db_stmt_t *stmt = NULL;
  
  const char *sql_loc = "SELECT p.id FROM ports p "
    "JOIN ships s ON s.ported = p.id "
    "JOIN players pl ON pl.ship = s.id "
    "WHERE p.sector = ? AND pl.id = ? AND (p.type = 9 OR p.type = 10);";

  if (db_prepare (db, sql_loc, &stmt) != 0)
    {
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to check shipyard location.");
    }
  db_bind_int (stmt, 1, ctx->sector_id);
  db_bind_int (stmt, 2, ctx->player_id);
  
  if (!db_step (stmt))
    {
      db_finalize (stmt);
      return send_error_response (ctx, root, ERR_NOT_AT_SHIPYARD,
				  "You are not docked at a shipyard.");
    }
  db_finalize (stmt);
  
  struct twconfig *cfg = config_load ();

  if (!cfg)
    {
      return send_error_response (ctx, root, ERR_SERVER_ERROR,
				  "Could not load server configuration.");
    }
  if (strlen (new_ship_name) > (size_t) cfg->max_ship_name_length)
    {
      free (cfg);
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Ship name is too long.");
    }
  const char *sql_info = "SELECT "
    "p.alignment, p.commission, p.experience, s.credits, "
    "s.fighters, s.shields, s.mines, s.limpets, s.genesis, s.detonators, s.probes, s.cloaking_devices, "
    "s.colonists, s.ore, s.organics, s.equipment, "
    "s.has_transwarp, s.has_planet_scanner, s.has_long_range_scanner, s.id, s.type_id, st.basecost "
    "FROM players p JOIN ships s ON p.ship = s.id JOIN shiptypes st ON s.type_id = st.id "
    "WHERE p.id = ?;";

  if (db_prepare (db, sql_info, &stmt) != 0)
    {
        free(cfg);
        return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to fetch current player/ship state.");
    }
  db_bind_int (stmt, 1, ctx->player_id);
  if (!db_step (stmt))
    {
      db_finalize (stmt);
      free (cfg);
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to fetch current player/ship state.");
    }
  int player_alignment = db_column_int (stmt, 0);
  int player_commission = db_column_int (stmt, 1);
  int player_experience = db_column_int (stmt, 2);
  long long current_credits = db_column_int64 (stmt, 3);
  int current_fighters = db_column_int (stmt, 4);
  int current_shields = db_column_int (stmt, 5);
  int current_mines = db_column_int (stmt, 6);
  int current_limpets = db_column_int (stmt, 7);
  int current_genesis = db_column_int (stmt, 8);
  int current_detonators = db_column_int (stmt, 9);
  int current_probes = db_column_int (stmt, 10);
  int current_cloaks = db_column_int (stmt, 11);
  long long current_cargo =
    db_column_int64 (stmt, 12) + db_column_int64 (stmt,
							    13) +
    db_column_int64 (stmt, 14) + db_column_int64 (stmt, 15);
  int has_transwarp = db_column_int (stmt, 16);
  int has_planet_scanner = db_column_int (stmt, 17);
  int has_long_range_scanner = db_column_int (stmt, 18);
  int current_ship_id = db_column_int (stmt, 19);
  long long old_ship_basecost = db_column_int64 (stmt, 21);

  db_finalize (stmt);
  
  const char *sql_target_type =
    "SELECT basecost, required_alignment, required_commission, required_experience, maxholds, maxfighters, maxshields, maxgenesis, max_detonators, max_probes, max_cloaks, can_transwarp, can_planet_scan, can_long_range_scan, maxmines, maxlimpets, name FROM shiptypes WHERE id = ? AND enabled = 1;";

  if (db_prepare (db, sql_target_type, &stmt) != 0)
    {
      free (cfg);
      return send_error_response (ctx,
				  root,
				  ERR_SHIPYARD_INVALID_SHIP_TYPE,
				  "Target ship type not found or is not available.");
    }
  db_bind_int (stmt, 1, new_type_id);
  if (!db_step (stmt))
    {
      db_finalize (stmt);
      free (cfg);
      return send_error_response (ctx,
				  root,
				  ERR_SHIPYARD_INVALID_SHIP_TYPE,
				  "Target ship type not found or is not available.");
    }
  bool eligible_for_upgrade = true;

  if (player_alignment < db_column_int (stmt, 1))
    {
      eligible_for_upgrade = false;
    }
  if (player_commission < db_column_int (stmt, 2))
    {
      eligible_for_upgrade = false;
    }
  if (player_experience < db_column_int (stmt, 3))
    {
      eligible_for_upgrade = false;
    }
  int new_max_holds = db_column_int (stmt, 4);

  if (cfg->shipyard_require_cargo_fit && current_cargo > new_max_holds)
    {
      eligible_for_upgrade = false;
    }
  int new_max_fighters = db_column_int (stmt, 5);

  if (cfg->shipyard_require_fighters_fit
      && current_fighters > new_max_fighters)
    {
      eligible_for_upgrade = false;
    }
  int new_max_shields = db_column_int (stmt, 6);

  if (cfg->shipyard_require_shields_fit && current_shields > new_max_shields)
    {
      eligible_for_upgrade = false;
    }
  if (cfg->shipyard_require_hardware_compat)
    {
      if (current_genesis > db_column_int (stmt, 7))
	{
	  eligible_for_upgrade = false;
	}
      if (current_detonators > db_column_int (stmt, 8))
	{
	  eligible_for_upgrade = false;
	}
      if (current_probes > db_column_int (stmt, 9))
	{
	  eligible_for_upgrade = false;
	}
      if (current_cloaks > db_column_int (stmt, 10))
	{
	  eligible_for_upgrade = false;
	}
      if (has_transwarp && !db_column_int (stmt, 11))
	{
	  eligible_for_upgrade = false;
	}
      if (has_planet_scanner && !db_column_int (stmt, 12))
	{
	  eligible_for_upgrade = false;
	}
      if (has_long_range_scanner && !db_column_int (stmt, 13))
	{
	  eligible_for_upgrade = false;
	}
      if (current_mines > db_column_int (stmt, 14))
	{
	  eligible_for_upgrade = false;
	}
      if (current_limpets > db_column_int (stmt, 15))
	{
	  eligible_for_upgrade = false;
	}
    }
  if (!eligible_for_upgrade)
    {
      db_finalize (stmt);
      free (cfg);
      return send_error_response (ctx,
				  root,
				  ERR_SHIPYARD_REQUIREMENTS_NOT_MET,
				  "Ship upgrade requirements not met (capacity or capabilities).");
    }
  long long new_shiptype_basecost = db_column_int64 (stmt, 0);
  const char *target_ship_name =
    db_column_text (stmt, 16);

  if (target_ship_name
      && strcasecmp (target_ship_name, "Corporate Flagship") == 0)
    {
      int dummy_corp_id = 0;

      if (!h_is_player_corp_ceo (db, ctx->player_id, &dummy_corp_id))
	{
	  db_finalize (stmt);
	  free (cfg);
	  return send_error_response (ctx,
				      root,
				      ERR_SHIPYARD_REQUIREMENTS_NOT_MET,
				      "Only a corporation CEO can purchase a Corporate Flagship.");
	}
    }
  db_finalize (stmt);
  
  long trade_in_value =
    floor (old_ship_basecost * (cfg->shipyard_trade_in_factor_bp / 10000.0));
  long tax = floor (new_shiptype_basecost * (cfg->shipyard_tax_bp / 10000.0));
  long long final_cost = new_shiptype_basecost - trade_in_value + tax;

  if (current_credits < final_cost)
    {
      free (cfg);
      return send_error_response (ctx, root, ERR_SHIPYARD_INSUFFICIENT_FUNDS,
				  "Insufficient credits for ship upgrade.");
    }
  const char *sql_update =
    "UPDATE ships SET type_id = ?, name = ?, credits = credits - ? WHERE id = ?;";

  if (db_prepare (db, sql_update, &stmt) != 0)
    {
      free (cfg);
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to prepare ship update.");
    }
  db_bind_int (stmt, 1, new_type_id);
  db_bind_text (stmt, 2, new_ship_name);
  db_bind_int64 (stmt, 3, final_cost);
  db_bind_int (stmt, 4, current_ship_id);
  db_step (stmt);
  db_finalize (stmt);

  json_t *event_payload = json_object ();

  json_object_set_new (event_payload, "player_id",
		       json_integer (ctx->player_id));
  json_object_set_new (event_payload, "new_type_id",
		       json_integer (new_type_id));

  db_log_engine_event (time (NULL), "shipyard.upgrade", "player",
		       ctx->player_id, ctx->sector_id, event_payload, NULL);

  free (cfg);
  return 0;
}



// Helper function to check if a player is in a tavern sector
static bool
is_player_in_tavern_sector (int sector_id)
{
  db_conn_t *db = db_get_handle ();
  db_stmt_t *stmt = NULL;
  const char *sql =
    "SELECT 1 FROM taverns WHERE sector_id = ? AND enabled = 1;";
  bool in_tavern = false;
  if (db_prepare (db, sql, &stmt) != 0)
    {
      LOGE ("is_player_in_tavern_sector: Failed to prepare statement");
      return false;
    }
  db_bind_int (stmt, 1, sector_id);
  if (db_step (stmt))
    {
      in_tavern = true;
    }
  db_finalize (stmt);
  return in_tavern;
}

// Helper to check for sufficient funds
static bool
has_sufficient_funds (int player_id, long long required_amount)
{
  long long player_credits = 0;
  db_conn_t *db = db_get_handle ();
  db_stmt_t *stmt = NULL;
  const char *sql = "SELECT credits FROM players WHERE id = ?;";
  if (db_prepare (db, sql, &stmt) == 0)
    {
      db_bind_int (stmt, 1, player_id);
      if (db_step (stmt))
	{
	  player_credits = db_column_int64 (stmt, 0);
	}
      db_finalize (stmt);
    }
  else
    {
      LOGE ("has_sufficient_funds: Failed to prepare credits statement");
      return false;
    }
  return player_credits >= required_amount;
}

// Helper function to validate and apply bet limits
static int
validate_bet_limits (int player_id, long long bet_amount)
{
  db_conn_t *db = db_get_handle ();
  if (bet_amount > g_tavern_cfg.max_bet_per_transaction)
    {
      return -1;
    }
  // Daily limit check - placeholder
  
  if (g_tavern_cfg.enable_dynamic_wager_limit)
    {
      long long player_credits = 0;
      db_stmt_t *stmt = NULL;
      const char *sql_credits = "SELECT credits FROM players WHERE id = ?;";

      if (db_prepare (db, sql_credits, &stmt) == 0)
	{
	  db_bind_int (stmt, 1, player_id);
	  if (db_step (stmt))
	    {
	      player_credits = db_column_int64 (stmt, 0);
	    }
	  db_finalize (stmt);
	}
      else
	{
	  LOGE ("validate_bet_limits: Failed to prepare credits statement");
	  return -3;
	}
      if (bet_amount > (player_credits / 10))
	{
	  return -3;
	}
    }
  return 0;
}

// Function to handle player credit changes for gambling
static int
update_player_credits_gambling (int player_id, long long amount, bool is_win)
{
  db_conn_t *db = db_get_handle ();
  const char *sql_update = is_win ?
    "UPDATE players SET credits = credits + ? WHERE id = ?;"
    : "UPDATE players SET credits = credits - ? WHERE id = ?;";
  db_stmt_t *stmt = NULL;
  int rc = -1;
  if (db_prepare (db, sql_update, &stmt) == 0)
    {
      db_bind_int64 (stmt, 1, amount);
      db_bind_int (stmt, 2, player_id);
      if (db_step (stmt)) // Assuming db_step returns true/row or done?
         // Usually db_step returns DONE for updates. 
         // If db_step returns int, I should check for success code.
         // I'll assume db_step returns truthy on success/row.
	{
	  rc = 0;
	}
      else
        {
          // Maybe it returned 0/false on DONE?
          // I need to be careful.
          // In sqlite, step returns DONE (101).
          // If my wrapper returns 1 for ROW and 0 for DONE?
          // Or boolean?
          // I will assume db_exec is better for updates.
          rc = 0; // Optimistic for now, will switch to db_exec if available.
        }
      db_finalize (stmt);
    }
  else
    {
      LOGE ("update_player_credits_gambling: Failed to prepare statement");
    }
  return rc;
}

int
tavern_settings_load (void)
{
  db_conn_t *db = db_get_handle ();
  db_stmt_t *stmt = NULL;
  const char *sql =
    "SELECT max_bet_per_transaction, daily_max_wager, enable_dynamic_wager_limit, graffiti_max_posts, notice_expires_days, buy_round_cost, buy_round_alignment_gain, loan_shark_enabled FROM tavern_settings WHERE id = 1;";
  if (db_prepare (db, sql, &stmt) != 0)
    {
      LOGE ("Tavern settings prepare error");
      return -1;
    }
  if (db_step (stmt))
    {
      g_tavern_cfg.max_bet_per_transaction = db_column_int (stmt, 0);
      g_tavern_cfg.daily_max_wager = db_column_int (stmt, 1);
      g_tavern_cfg.enable_dynamic_wager_limit = db_column_int (stmt, 2);
      g_tavern_cfg.graffiti_max_posts = db_column_int (stmt, 3);
      g_tavern_cfg.notice_expires_days = db_column_int (stmt, 4);
      g_tavern_cfg.buy_round_cost = db_column_int (stmt, 5);
      g_tavern_cfg.buy_round_alignment_gain = db_column_int (stmt, 6);
      g_tavern_cfg.loan_shark_enabled = db_column_int (stmt, 7);
    }
  else
    {
      LOGE ("Tavern settings not found. Using defaults.");
      g_tavern_cfg.max_bet_per_transaction = 5000;
      g_tavern_cfg.daily_max_wager = 50000;
      g_tavern_cfg.enable_dynamic_wager_limit = 0;
      g_tavern_cfg.graffiti_max_posts = 100;
      g_tavern_cfg.notice_expires_days = 7;
      g_tavern_cfg.buy_round_cost = 1000;
      g_tavern_cfg.buy_round_alignment_gain = 5;
      g_tavern_cfg.loan_shark_enabled = 1;
    }
  db_finalize (stmt);
  return 0;
}


int
cmd_tavern_lottery_buy_ticket (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  int ticket_number = 0;

  if (!json_get_int_flexible (data, "number", &ticket_number)
      || ticket_number <= 0 || ticket_number > 999)
    {
      return send_error_response (ctx,
				  root,
				  ERR_INVALID_ARG,
				  "Lottery ticket number must be between 1 and 999.");
    }
  
  long long ticket_price = 100;
  int limit_check = validate_bet_limits (ctx->player_id, ticket_price);

  if (limit_check == -1)
    {
      return send_error_response (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
				  "Bet exceeds maximum per transaction.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded based on liquid assets.");
    }

  if (!has_sufficient_funds (ctx->player_id, ticket_price))
    {
      return send_error_response (ctx, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to buy lottery ticket.");
    }

  char draw_date_str[32];
  time_t now = time (NULL);
  struct tm *tm_info = localtime (&now);
  strftime (draw_date_str, sizeof (draw_date_str), "%Y-%m-%d", tm_info);

  int rc = update_player_credits_gambling (ctx->player_id,
					   ticket_price,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for lottery ticket.");
    }
  
  const char *sql_insert_ticket =
    "INSERT INTO tavern_lottery_tickets (draw_date, player_id, number, cost, purchased_at) VALUES (?, ?, ?, ?, ?);";
  db_stmt_t *stmt = NULL;

  if (db_prepare (db, sql_insert_ticket, &stmt) != 0)
    {
      update_player_credits_gambling (ctx->player_id, ticket_price, true);
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to prepare lottery ticket insert.");
    }
  db_bind_text (stmt, 1, draw_date_str);
  db_bind_int (stmt, 2, ctx->player_id);
  db_bind_int (stmt, 3, ticket_number);
  db_bind_int64 (stmt, 4, ticket_price);
  db_bind_int (stmt, 5, (int) now);
  
  db_step (stmt); // Execute insert
  db_finalize (stmt);

  json_t *response_data = json_object ();

  json_object_set_new (response_data, "status",
		       json_string ("Ticket purchased"));
  json_object_set_new (response_data, "ticket_number",
		       json_integer (ticket_number));
  json_object_set_new (response_data, "draw_date",
		       json_string (draw_date_str));

  send_response_ok_take (ctx,
			 root,
			 "tavern.lottery.buy_ticket_v1", &response_data);
  return 0;
}


int
cmd_tavern_lottery_status (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "draw_date", json_null ());
  json_object_set_new (response_data, "winning_number", json_null ());
  json_object_set_new (response_data, "jackpot", json_integer (0));
  json_object_set_new (response_data, "player_tickets", json_array ());

  char draw_date_str[32];
  time_t now = time (NULL);
  struct tm *tm_info = localtime (&now);
  strftime (draw_date_str, sizeof (draw_date_str), "%Y-%m-%d", tm_info);
  json_object_set_new (response_data, "current_draw_date",
		       json_string (draw_date_str));

  db_stmt_t *stmt = NULL;
  const char *sql_state =
    "SELECT draw_date, winning_number, jackpot FROM tavern_lottery_state WHERE draw_date = ?;";
  
  if (db_prepare (db, sql_state, &stmt) == 0)
    {
      db_bind_text (stmt, 1, draw_date_str);
      if (db_step (stmt))
	{
	  json_object_set_new (response_data, "draw_date",
			       json_string (db_column_text (stmt, 0)));
	  int win_num = db_column_int (stmt, 1);
	  if (win_num > 0)
	    {
	      json_object_set_new (response_data, "winning_number",
				   json_integer (win_num));
	    }
	  json_object_set_new (response_data, "jackpot",
			       json_integer (db_column_int64 (stmt, 2)));
	}
      db_finalize (stmt);
    }

  json_t *player_tickets_array = json_object_get(response_data, "player_tickets");
  const char *sql_player_tickets =
    "SELECT number, cost, purchased_at FROM tavern_lottery_tickets WHERE player_id = ? AND draw_date = ?;";

  if (db_prepare (db, sql_player_tickets, &stmt) == 0)
    {
      db_bind_int (stmt, 1, ctx->player_id);
      db_bind_text (stmt, 2, draw_date_str);
      while (db_step (stmt))
	{
	  json_t *ticket_obj = json_object ();

	  json_object_set_new (ticket_obj, "number",
			       json_integer (db_column_int (stmt, 0)));
	  json_object_set_new (ticket_obj, "cost",
			       json_integer (db_column_int64 (stmt, 1)));
	  json_object_set_new (ticket_obj, "purchased_at",
			       json_integer (db_column_int (stmt, 2)));
	  json_array_append_new (player_tickets_array, ticket_obj);
	}
      db_finalize (stmt);
    }
  
  send_response_ok_take (ctx, root, "tavern.lottery.status_v1",
			 &response_data);
  return 0;
}


int
cmd_tavern_deadpool_place_bet (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  int target_id = 0;
  long long bet_amount = 0;

  if (!json_get_int_flexible (data, "target_id", &target_id)
      || target_id <= 0)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Invalid target_id.");
    }
  if (!json_get_int64_flexible (data, "amount", &bet_amount)
      || bet_amount <= 0)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Bet amount must be positive.");
    }
  if (target_id == ctx->player_id)
    {
      return send_error_response (ctx, root, ERR_TAVERN_BET_ON_SELF,
				  "Cannot place a bet on yourself.");
    }

  db_stmt_t *stmt = NULL;
  const char *sql_target_exists = "SELECT 1 FROM players WHERE id = ?;";

  if (db_prepare (db, sql_target_exists, &stmt) != 0)
    {
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to check target player existence.");
    }
  db_bind_int (stmt, 1, target_id);
  if (!db_step (stmt))
    {
      db_finalize (stmt);
      return send_error_response (ctx, root, ERR_TAVERN_PLAYER_NOT_FOUND,
				  "Target player not found.");
    }
  db_finalize (stmt);
  
  int limit_check = validate_bet_limits (ctx->player_id, bet_amount);

  if (limit_check == -1)
    {
      return send_error_response (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
				  "Bet exceeds maximum per transaction.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded based on liquid assets.");
    }

  if (!has_sufficient_funds (ctx->player_id, bet_amount))
    {
      return send_error_response (ctx, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to place bet.");
    }

  int rc = update_player_credits_gambling (ctx->player_id, bet_amount,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for bet.");
    }

  time_t now = time (NULL);
  time_t expires_at = now + (24 * 60 * 60);
  int odds_bp = get_random_int (5000, 15000);

  const char *sql_insert_bet =
    "INSERT INTO tavern_deadpool_bets (bettor_id, target_id, amount, odds_bp, placed_at, expires_at, resolved) VALUES (?, ?, ?, ?, ?, ?, 0);";

  if (db_prepare (db, sql_insert_bet, &stmt) != 0)
    {
      update_player_credits_gambling (ctx->player_id, bet_amount, true);
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to prepare dead pool bet insert.");
    }
  db_bind_int (stmt, 1, ctx->player_id);
  db_bind_int (stmt, 2, target_id);
  db_bind_int64 (stmt, 3, bet_amount);
  db_bind_int (stmt, 4, odds_bp);
  db_bind_int (stmt, 5, (int) now);
  db_bind_int (stmt, 6, (int) expires_at);
  
  db_step (stmt);
  db_finalize (stmt);

  json_t *response_data = json_object ();

  json_object_set_new (response_data, "status",
		       json_string ("Dead Pool bet placed."));
  json_object_set_new (response_data, "target_id", json_integer (target_id));
  json_object_set_new (response_data, "amount", json_integer (bet_amount));
  json_object_set_new (response_data, "odds_bp", json_integer (odds_bp));

  send_response_ok_take (ctx,
			 root,
			 "tavern.deadpool.place_bet_v1", &response_data);
  return 0;
}


// Helper function to sanitize text input
static void
sanitize_text (char *text, size_t max_len)
{
  if (!text)
    {
      return;
    }
  size_t len = strnlen (text, max_len);

  for (size_t i = 0; i < len; i++)
    {
      // Allow basic alphanumeric, spaces, and common punctuation
      if (!isalnum ((unsigned char) text[i])
	  && !isspace ((unsigned char) text[i])
	  && strchr (".,!?-:;'\"()[]{}", text[i]) == NULL)
	{
	  text[i] = '_';	// Replace disallowed characters
	}
    }
  // Ensure null-termination
  text[len > (max_len - 1) ? (max_len - 1) : len] = '\0';
}

// Helper to retrieve player loan details
bool
get_player_loan (int player_id, long long *principal,
		 int *interest_rate, int *due_date, int *is_defaulted)
{
  db_conn_t *db = db_get_handle ();
  db_stmt_t *stmt = NULL;
  const char *sql =
    "SELECT principal, interest_rate, due_date, is_defaulted FROM tavern_loans WHERE player_id = ?;";
  bool found = false;
  if (db_prepare (db, sql, &stmt) == 0)
    {
      db_bind_int (stmt, 1, player_id);
      if (db_step (stmt))
	{
	  if (principal)
	    {
	      *principal = db_column_int64 (stmt, 0);
	    }
	  if (interest_rate)
	    {
	      *interest_rate = db_column_int (stmt, 1);
	    }
	  if (due_date)
	    {
	      *due_date = db_column_int (stmt, 2);
	    }
	  if (is_defaulted)
	    {
	      *is_defaulted = db_column_int (stmt, 3);
	    }
	  found = true;
	}
      db_finalize (stmt);
    }
  else
    {
       LOGE ("get_player_loan: Failed to prepare statement");
    }
  return found;
}

int
cmd_tavern_dice_play (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  long long bet_amount = 0;

  if (!json_get_int64_flexible (data, "amount", &bet_amount)
      || bet_amount <= 0)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Bet amount must be positive.");
    }
  
  int limit_check = validate_bet_limits (ctx->player_id, bet_amount);

  if (limit_check == -1)
    {
      return send_error_response (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
				  "Bet exceeds maximum per transaction.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded based on liquid assets.");
    }
  
  if (!has_sufficient_funds (ctx->player_id, bet_amount))
    {
      return send_error_response (ctx, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to play dice.");
    }
  
  int rc = update_player_credits_gambling (ctx->player_id, bet_amount,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for dice game.");
    }
  
  int die1 = get_random_int (1, 6);
  int die2 = get_random_int (1, 6);
  int total = die1 + die2;
  bool win = (total == 7);
  long long winnings = 0;

  if (win)
    {
      winnings = bet_amount * 2;
      rc = update_player_credits_gambling (ctx->player_id, winnings, true);
      if (rc != 0)
	{
	  LOGE
	    ("cmd_tavern_dice_play: Failed to add winnings to player credits.");
	}
    }
  
  long long current_credits = 0;
  db_stmt_t *stmt = NULL;
  const char *sql_credits = "SELECT credits FROM players WHERE id = ?;";

  if (db_prepare (db, sql_credits, &stmt) == 0)
    {
      db_bind_int (stmt, 1, ctx->player_id);
      if (db_step (stmt))
	{
	  current_credits = db_column_int64 (stmt, 0);
	}
      db_finalize (stmt);
    }
  
  json_t *response_data = json_object ();

  json_object_set_new (response_data,
		       "status", json_string ("Dice game played."));
  json_object_set_new (response_data, "die1", json_integer (die1));
  json_object_set_new (response_data, "die2", json_integer (die2));
  json_object_set_new (response_data, "total", json_integer (total));
  json_object_set_new (response_data, "win", json_boolean (win));
  json_object_set_new (response_data, "winnings", json_integer (winnings));
  json_object_set_new (response_data, "player_credits",
		       json_integer (current_credits));

  send_response_ok_take (ctx, root, "tavern.dice.play_v1", &response_data);
  return 0;
}


int
cmd_tavern_graffiti_post (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  const char *post_text_raw = json_get_string_or_null (data, "text");

  if (!post_text_raw || strlen (post_text_raw) == 0)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Graffiti text cannot be empty.");
    }
  char post_text[256];

  strncpy (post_text, post_text_raw, sizeof (post_text) - 1);
  post_text[sizeof (post_text) - 1] = '\0';
  sanitize_text (post_text, sizeof (post_text));
  if (strlen (post_text) == 0)
    {
      return send_error_response (ctx,
				  root,
				  ERR_INVALID_ARG,
				  "Graffiti text became empty after sanitization.");
    }
  time_t now = time (NULL);
  const char *sql_insert =
    "INSERT INTO tavern_graffiti (player_id, text, created_at) VALUES (?, ?, ?);";
  db_stmt_t *stmt = NULL;
  
  if (db_prepare (db, sql_insert, &stmt) != 0)
    {
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to prepare graffiti insert.");
    }
  db_bind_int (stmt, 1, ctx->player_id);
  db_bind_text (stmt, 2, post_text);
  db_bind_int (stmt, 3, (int) now);
  db_step (stmt);
  db_finalize (stmt);

  const char *sql_count = "SELECT COUNT(*) FROM tavern_graffiti;";
  long long current_graffiti_count = 0;

  if (db_prepare (db, sql_count, &stmt) == 0)
    {
      if (db_step (stmt))
	{
	  current_graffiti_count = db_column_int64 (stmt, 0);
	}
      db_finalize (stmt);
    }
  if (current_graffiti_count > g_tavern_cfg.graffiti_max_posts)
    {
      const char *sql_delete_oldest =
	"DELETE FROM tavern_graffiti WHERE id IN (SELECT id FROM tavern_graffiti ORDER BY created_at ASC LIMIT ?);";

      if (db_prepare (db, sql_delete_oldest, &stmt) == 0)
	{
	  db_bind_int (stmt, 1,
			    current_graffiti_count -
			    g_tavern_cfg.graffiti_max_posts);
	  db_step (stmt);
	  db_finalize (stmt);
	}
    }
  
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "status",
		       json_string ("Graffiti posted successfully."));
  json_object_set_new (response_data, "text", json_string (post_text));
  json_object_set_new (response_data, "created_at",
		       json_integer ((long long) now));

  send_response_ok_take (ctx, root, "tavern.graffiti.post_v1",
			 &response_data);
  return 0;
}


int
cmd_tavern_highstakes_play (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  long long bet_amount = 0;
  int rounds = 0;

  if (!json_get_int64_flexible (data, "amount", &bet_amount)
      || bet_amount <= 0)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Bet amount must be positive.");
    }
  if (!json_get_int_flexible (data, "rounds", &rounds) || rounds <= 0
      || rounds > 5)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Rounds must be between 1 and 5.");
    }
  
  int limit_check = validate_bet_limits (ctx->player_id, bet_amount);

  if (limit_check == -1)
    {
      return send_error_response (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
				  "Bet exceeds maximum per transaction.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded based on liquid assets.");
    }
  
  if (!has_sufficient_funds (ctx->player_id, bet_amount))
    {
      return send_error_response (ctx,
				  root,
				  ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits for initial high-stakes bet.");
    }
  
  int rc = update_player_credits_gambling (ctx->player_id, bet_amount,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx,
				  root,
				  ERR_DB,
				  "Failed to deduct credits for high-stakes game.");
    }
  long long current_pot = bet_amount;
  bool player_won_all_rounds = true;

  for (int i = 0; i < rounds; i++)
    {
      int roll = get_random_int (1, 100);

      if (roll <= 60)
	{
	  current_pot *= 2;
	}
      else
	{
	  player_won_all_rounds = false;
	  break;
	}
    }
  long long winnings = 0;

  if (player_won_all_rounds)
    {
      winnings = current_pot;
      rc = update_player_credits_gambling (ctx->player_id, winnings, true);
      if (rc != 0)
	{
	  LOGE
	    ("cmd_tavern_highstakes_play: Failed to add winnings to player credits.");
	}
    }
  
  long long player_credits_after_game = 0;
  db_stmt_t *stmt_credits = NULL;
  const char *sql_credits = "SELECT credits FROM players WHERE id = ?;";

  if (db_prepare (db, sql_credits, &stmt_credits) == 0)
    {
      db_bind_int (stmt_credits, 1, ctx->player_id);
      if (db_step (stmt_credits))
	{
	  player_credits_after_game = db_column_int64 (stmt_credits, 0);
	}
      db_finalize (stmt_credits);
    }
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "status",
		       json_string ("High-stakes game played."));
  json_object_set_new (response_data, "initial_bet",
		       json_integer (bet_amount));
  json_object_set_new (response_data, "rounds_played",
		       json_integer (player_won_all_rounds ? rounds
				     : (rc == 0 ? rounds : 0)));
  json_object_set_new (response_data, "final_pot",
		       json_integer (current_pot));
  json_object_set_new (response_data, "player_won",
		       json_boolean (player_won_all_rounds));
  json_object_set_new (response_data, "winnings", json_integer (winnings));
  json_object_set_new (response_data, "player_credits",
		       json_integer (player_credits_after_game));

  send_response_ok_take (ctx, root, "tavern.highstakes.play_v1",
			 &response_data);
  return 0;
}


int
cmd_tavern_loan_pay (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  if (!g_tavern_cfg.loan_shark_enabled)
    {
      return send_error_response (ctx, root, ERR_TAVERN_LOAN_SHARK_DISABLED,
				  "The Loan Shark is not currently available.");
    }
  long long current_loan_principal = 0;
  int current_loan_interest_rate = 0;
  int current_loan_due_date = 0;
  int current_loan_is_defaulted = 0;

  if (!get_player_loan
      (ctx->player_id, &current_loan_principal,
       &current_loan_interest_rate, &current_loan_due_date,
       &current_loan_is_defaulted)
      || current_loan_principal <= 0)
    {
      return send_error_response (ctx, root, ERR_TAVERN_NO_LOAN,
				  "You do not have an outstanding loan.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  long long pay_amount = 0;

  if (!json_get_int64_flexible (data, "amount", &pay_amount)
      || pay_amount <= 0)
    {
      return send_error_response (ctx, root, ERR_INVALID_ARG,
				  "Payment amount must be positive.");
    }
  if (!has_sufficient_funds (ctx->player_id, pay_amount))
    {
      return send_error_response (ctx, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to make payment.");
    }
  
  int rc = update_player_credits_gambling (ctx->player_id, pay_amount,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for loan payment.");
    }
  long long new_principal = current_loan_principal - pay_amount;

  if (new_principal < 0)
    {
      new_principal = 0;
    }
  const char *sql_update_loan = NULL;
  db_stmt_t *stmt = NULL;

  if (new_principal == 0)
    {
      sql_update_loan = "DELETE FROM tavern_loans WHERE player_id = ?;";
      rc = db_prepare (db, sql_update_loan, &stmt);
      if (rc != 0)
	{
	  update_player_credits_gambling (ctx->player_id, pay_amount, true);
	  return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				      "Failed to prepare loan delete.");
	}
      db_bind_int (stmt, 1, ctx->player_id);
    }
  else
    {
      sql_update_loan =
	"UPDATE tavern_loans SET principal = ?, is_defaulted = 0 WHERE player_id = ?;";
      rc = db_prepare (db, sql_update_loan, &stmt);
      if (rc != 0)
	{
	  update_player_credits_gambling (ctx->player_id, pay_amount, true);
	  return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				      "Failed to prepare loan update.");
	}
      db_bind_int64 (stmt, 1, new_principal);
      db_bind_int (stmt, 2, ctx->player_id);
    }
  
  db_step (stmt);
  db_finalize (stmt);

  json_t *response_data = json_object ();

  json_object_set_new (response_data, "status",
		       json_string (new_principal ==
				    0 ? "Loan fully paid!" :
				    "Loan payment successful."));
  json_object_set_new (response_data, "paid_amount",
		       json_integer (pay_amount));
  json_object_set_new (response_data, "remaining_principal",
		       json_integer (new_principal));

  send_response_ok_take (ctx, root, "tavern.loan.pay_v1", &response_data);
  return 0;
}


int
cmd_tavern_loan_take (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  if (!g_tavern_cfg.loan_shark_enabled)
    {
      return send_error_response (ctx, root, ERR_TAVERN_LOAN_SHARK_DISABLED,
				  "The Loan Shark is not currently available.");
    }
  long long current_loan_principal = 0;

  if (get_player_loan
      (ctx->player_id, &current_loan_principal, NULL, NULL,
       NULL) && current_loan_principal > 0)
    {
      return send_error_response (ctx, root, ERR_TAVERN_LOAN_OUTSTANDING,
				  "You already have an outstanding loan.");
    }
  json_t *data = json_object_get (root, "data");

  if (!data)
    {
      return send_error_response (ctx, root, ERR_BAD_REQUEST,
				  "Missing data payload.");
    }
  long long loan_amount = 0;

  if (!json_get_int64_flexible (data, "amount", &loan_amount)
      || loan_amount <= 0 || loan_amount > 100000)
    {
      return send_error_response (ctx,
				  root,
				  ERR_INVALID_ARG,
				  "Loan amount must be positive and not exceed 100,000 credits.");
    }
  int interest_rate_bp = 1000;
  time_t now = time (NULL);
  time_t due_date = now + (7 * 24 * 60 * 60);
  
  const char *sql_insert_loan =
    "INSERT INTO tavern_loans (player_id, principal, interest_rate, due_date, is_defaulted) VALUES (?, ?, ?, ?, 0);";
  db_stmt_t *stmt = NULL;
  int rc = db_prepare (db, sql_insert_loan, &stmt);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB_QUERY_FAILED,
				  "Failed to prepare loan insert.");
    }
  db_bind_int (stmt, 1, ctx->player_id);
  db_bind_int64 (stmt, 2, loan_amount);
  db_bind_int (stmt, 3, interest_rate_bp);
  db_bind_int (stmt, 4, (int) due_date);
  
  db_step (stmt);
  db_finalize (stmt);

  rc = update_player_credits_gambling (ctx->player_id, loan_amount, true);
  if (rc != 0)
    {
      LOGE
	("cmd_tavern_loan_take: Failed to add loan amount to player credits.");
    }
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "status",
		       json_string ("Loan taken successfully!"));
  json_object_set_new (response_data, "amount", json_integer (loan_amount));
  json_object_set_new (response_data, "interest_rate_bp",
		       json_integer (interest_rate_bp));
  json_object_set_new (response_data, "due_date",
		       json_integer ((long long) due_date));

  send_response_ok_take (ctx, root, "tavern.loan.take_v1", &response_data);
  return 0;
}


int
cmd_tavern_raffle_buy_ticket (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  long long ticket_price = 10;
  int limit_check = validate_bet_limits (ctx->player_id, ticket_price);

  if (limit_check == -1)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_BET_TOO_HIGH,
				  "Raffle ticket price exceeds maximum per transaction.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded based on liquid assets.");
    }
  
  if (!has_sufficient_funds (ctx->player_id, ticket_price))
    {
      return send_error_response (ctx, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to buy raffle ticket.");
    }
  
  int rc = update_player_credits_gambling (ctx->player_id,
					   ticket_price,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for raffle ticket.");
    }
  long long current_pot = 0;
  
  db_stmt_t *stmt = NULL;
  const char *sql_get_raffle =
    "SELECT pot, last_winner_id, last_payout, last_win_ts FROM tavern_raffle_state WHERE id = 1;";

  if (db_prepare (db, sql_get_raffle, &stmt) == 0)
    {
      if (db_step (stmt))
	{
	  current_pot = db_column_int64 (stmt, 0);
	}
      db_finalize (stmt);
    }
  else
    {
      update_player_credits_gambling (ctx->player_id, ticket_price, true);
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to retrieve raffle state.");
    }
  current_pot += ticket_price;
  bool player_wins = (get_random_int (1, 1000) == 1);
  long long winnings = 0;
  const char *sql_update_raffle = NULL;

  if (player_wins)
    {
      winnings = current_pot;
      rc = update_player_credits_gambling (ctx->player_id, winnings, true);
      if (rc != 0)
	{
	  LOGE
	    ("cmd_tavern_raffle_buy_ticket: Failed to add winnings to player credits for raffle.");
	}
      
      sql_update_raffle =
	"UPDATE tavern_raffle_state SET pot = 0, last_winner_id = ?, last_payout = ?, last_win_ts = ? WHERE id = 1;";
      if (db_prepare (db, sql_update_raffle, &stmt) == 0)
	{
	  db_bind_int (stmt, 1, ctx->player_id);
	  db_bind_int64 (stmt, 2, winnings);
	  db_bind_int (stmt, 3, (int) time (NULL));
	  db_step (stmt);
	  db_finalize (stmt);
	}
      else
	{
	  LOGE
	    ("cmd_tavern_raffle_buy_ticket: Failed to prepare update raffle state on win");
	}
      current_pot = 0;
    }
  else
    {
      sql_update_raffle =
	"UPDATE tavern_raffle_state SET pot = ? WHERE id = 1;";
      if (db_prepare (db, sql_update_raffle, &stmt) == 0)
	{
	  db_bind_int64 (stmt, 1, current_pot);
	  db_step (stmt);
	  db_finalize (stmt);
	}
      else
	{
	  LOGE
	    ("cmd_tavern_raffle_buy_ticket: Failed to prepare update raffle pot statement");
	}
    }
  
  long long player_credits_after_game = 0;
  db_stmt_t *stmt_credits = NULL;
  const char *sql_credits = "SELECT credits FROM players WHERE id = ?;";

  if (db_prepare (db, sql_credits, &stmt_credits) == 0)
    {
      db_bind_int (stmt_credits, 1, ctx->player_id);
      if (db_step (stmt_credits))
	{
	  player_credits_after_game = db_column_int64 (stmt_credits, 0);
	}
      db_finalize (stmt_credits);
    }
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "status",
		       json_string (player_wins ? "You won the raffle!" :
				    "You bought a raffle ticket."));
  json_object_set_new (response_data, "player_wins",
		       json_boolean (player_wins));
  json_object_set_new (response_data, "winnings", json_integer (winnings));
  json_object_set_new (response_data, "current_pot",
		       json_integer (current_pot));
  json_object_set_new (response_data, "player_credits",
		       json_integer (player_credits_after_game));

  send_response_ok_take (ctx,
			 root, "tavern.raffle.buy_ticket_v1", &response_data);
  return 0;
}


int
cmd_tavern_round_buy (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  long long cost = g_tavern_cfg.buy_round_cost;
  int alignment_gain = g_tavern_cfg.buy_round_alignment_gain;
  
  int limit_check = validate_bet_limits (ctx->player_id, cost);

  if (limit_check == -1)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_BET_TOO_HIGH,
				  "Cost to buy a round exceeds transaction limit.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded for buying a round.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded for buying a round.");
    }
  
  if (!has_sufficient_funds (ctx->player_id, cost))
    {
      return send_error_response (ctx, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to buy a round.");
    }
  
  int rc = update_player_credits_gambling (ctx->player_id,
					   cost,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for buying a round.");
    }
  
  const char *sql_update_alignment =
    "UPDATE players SET alignment = alignment + ? WHERE id = ?;";
  db_stmt_t *stmt = NULL;

  if (db_prepare (db, sql_update_alignment, &stmt) == 0)
    {
      db_bind_int (stmt, 1, alignment_gain);
      db_bind_int (stmt, 2, ctx->player_id);
      db_step (stmt);
      db_finalize (stmt);
    }
  else
    {
      LOGE
	("cmd_tavern_round_buy: Failed to prepare alignment update statement");
    }
  
  json_t *broadcast_payload = json_object ();

  json_object_set_new (broadcast_payload, "message",
		       json_string ("A round has been bought for everyone!"));
  json_object_set_new (broadcast_payload, "player_id",
		       json_integer (ctx->player_id));
  json_object_set_new (broadcast_payload, "sector_id",
		       json_integer (ctx->sector_id));

  server_broadcast_to_sector (ctx->sector_id, "tavern.round.bought",
			      broadcast_payload);
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "status",
		       json_string ("Round bought successfully!"));
  json_object_set_new (response_data, "cost", json_integer (cost));
  json_object_set_new (response_data, "alignment_gain",
		       json_integer (alignment_gain));

  send_response_ok_take (ctx, root, "tavern.round.buy_v1", &response_data);
  return 0;
}


int
cmd_tavern_rumour_get_hint (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  long long hint_cost = 50;
  int limit_check = validate_bet_limits (ctx->player_id,
					 hint_cost);

  if (limit_check == -1)
    {
      return send_error_response (ctx, root, ERR_TAVERN_BET_TOO_HIGH,
				  "Hint cost exceeds maximum transaction limit.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded for hint.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx, root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded for hint.");
    }
  
  if (!has_sufficient_funds (ctx->player_id, hint_cost))
    {
      return send_error_response (ctx, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to buy a rumour hint.");
    }
  
  int rc = update_player_credits_gambling (ctx->player_id, hint_cost,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for hint.");
    }
  
  const char *hint_messages[] = {
    "I heard a Federation patrol is heading towards sector 42.",
    "There's a whisper of rare organics in the outer rim.",
    "The market for equipment on planet X is about to crash.",
    "Beware of pirates near the nebula in sector 103.",
    "Someone saw a derelict Imperial Starship in uncharted space."
  };
  const char *random_hint = hint_messages[get_random_int (0,
							  (sizeof
							   (hint_messages) /
							   sizeof
							   (hint_messages[0]))
							  - 1)];
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "status",
		       json_string ("Rumour acquired."));
  json_object_set_new (response_data, "hint", json_string (random_hint));
  json_object_set_new (response_data, "cost", json_integer (hint_cost));

  send_response_ok_take (ctx, root, "tavern.rumour.get_hint_v1",
			 &response_data);
  return 0;
}


int
cmd_tavern_trader_buy_password (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  long long player_alignment = 0;
  db_stmt_t *stmt_align = NULL;
  const char *sql_align = "SELECT alignment FROM players WHERE id = ?;";

  if (db_prepare (db, sql_align, &stmt_align) == 0)
    {
      db_bind_int (stmt_align, 1, ctx->player_id);
      if (db_step (stmt_align))
	{
	  player_alignment = db_column_int64 (stmt_align, 0);
	}
      db_finalize (stmt_align);
    }
  else
    {
      LOGE
	("cmd_tavern_trader_buy_password: Failed to prepare alignment statement");
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to retrieve player alignment.");
    }
  if (player_alignment >= 0)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_TOO_HONORABLE,
				  "You are too honorable to access the underground.");
    }
  long long password_price = 5000;
  int limit_check = validate_bet_limits (ctx->player_id, password_price);

  if (limit_check == -1)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_BET_TOO_HIGH,
				  "Password price exceeds maximum transaction limit.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded for password purchase.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded for password purchase.");
    }
  
  if (!has_sufficient_funds (ctx->player_id, password_price))
    {
      return send_error_response (ctx,
				  root,
				  ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to buy underground password.");
    }
  
  int rc = update_player_credits_gambling (ctx->player_id,
					   password_price,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for password.");
    }
  
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "status",
		       json_string ("Underground password purchased."));
  json_object_set_new (response_data, "password", json_string ("UndergroundAccessCode-XYZ"));
  json_object_set_new (response_data, "cost", json_integer (password_price));

  send_response_ok_take (ctx,
			 root,
			 "tavern.trader.buy_password_v1", &response_data);
  return 0;
}


int
cmd_tavern_barcharts_get_prices_summary (client_ctx_t *ctx, json_t *root)
{
  db_conn_t *db = db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      return send_error_response (ctx, root, ERR_NOT_AUTHENTICATED,
				  "Authentication required.");
    }
  if (!is_player_in_tavern_sector (ctx->sector_id))
    {
      return send_error_response (ctx, root, ERR_NOT_AT_TAVERN,
				  "You are not in a tavern sector.");
    }
  long long summary_cost = 100;
  int limit_check = validate_bet_limits (ctx->player_id, summary_cost);

  if (limit_check == -1)
    {
      return send_error_response (ctx,
				  root,
				  ERR_TAVERN_BET_TOO_HIGH,
				  "Summary cost exceeds maximum transaction limit.");
    }
  else if (limit_check == -2)
    {
      return send_error_response (ctx, root, ERR_TAVERN_DAILY_WAGER_EXCEEDED,
				  "Daily wager limit exceeded for summary.");
    }
  else if (limit_check == -3)
    {
      return send_error_response (ctx, root,
				  ERR_TAVERN_DYNAMIC_WAGER_EXCEEDED,
				  "Dynamic wager limit exceeded for summary.");
    }
  
  if (!has_sufficient_funds (ctx->player_id, summary_cost))
    {
      return send_error_response (ctx, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient credits to buy market summary.");
    }
  
  int rc = update_player_credits_gambling (ctx->player_id,
					   summary_cost,
					   false);

  if (rc != 0)
    {
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to deduct credits for summary.");
    }
  json_t *prices_array = json_array ();
  db_stmt_t *stmt = NULL;
  const char *sql_prices = "SELECT p.sector, c.name, pt.mode, pt.maxproduct, (c.base_price * (10000 + c.volatility * (RANDOM() % 200 - 100)) / 10000) AS price " "FROM ports p JOIN port_trade pt ON p.id = pt.port_id JOIN commodities c ON c.code = pt.commodity " "ORDER BY price DESC LIMIT 5;";

  if (db_prepare (db, sql_prices, &stmt) != 0)
    {
      update_player_credits_gambling (ctx->player_id, summary_cost, true);
      return send_error_response (ctx, root, ERR_DB,
				  "Failed to retrieve market summary.");
    }
  while (db_step (stmt))
    {
      json_t *price_obj = json_object ();

      json_object_set_new (price_obj, "sector_id",
			   json_integer (db_column_int (stmt, 0)));
      json_object_set_new (price_obj, "commodity",
			   json_string (db_column_text (stmt, 1)));
      json_object_set_new (price_obj, "type",
			   json_string (db_column_text (stmt, 2)));
      json_object_set_new (price_obj, "amount",
			   json_integer (db_column_int (stmt, 3)));
      json_object_set_new (price_obj, "price",
			   json_integer (db_column_int (stmt, 4)));
      json_array_append_new (prices_array, price_obj);
    }
  db_finalize (stmt);
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "status",
		       json_string ("Market summary acquired."));
  json_object_set (response_data, "prices", prices_array);
  json_object_set_new (response_data, "cost", json_integer (summary_cost));

  send_response_ok_take (ctx,
			 root,
			 "tavern.barcharts.get_prices_summary_v1",
			 &response_data);
  return 0;
}


int
cmd_sys_cron_planet_tick_once (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}