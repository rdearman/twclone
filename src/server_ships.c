#include "db/repo/repo_ships.h"
#include "db/repo/repo_cargo.h"
#include <strings.h>
#include <libpq-fe.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jansson.h>		/* -ljansson */
#include <stdbool.h>
#include <uuid/uuid.h>		// For UUID generation
/* local includes */
#include "schemas.h"
#include "server_cmds.h"
#include "server_loop.h"
#include "server_ships.h"
#include "game_db.h"
#include "server_config.h"
#include "repo_cmd.h"
#include "repo_combat.h"
#include "db/repo/repo_stardock.h"
#include "db/repo/repo_porttypes.h"
#include "errors.h"
#include "config.h"
#include "common.h"
#include "server_envelope.h"
#include "server_rules.h"
#include "server_players.h"
#include "db/repo/repo_players.h"
#include "server_log.h"
#include "server_ports.h"
#include "db/db_api.h"
#include "db/sql_driver.h"


#define UUID_STR_LEN 37		// 36 chars + null terminator


/* map enum → text for DB */


static const char *
kill_cause_to_text (ship_kill_cause_t cause)
{
  switch (cause)


    {
    case KILL_CAUSE_COMBAT:
      return "combat";


    case KILL_CAUSE_MINES:
      return "mines";


    case KILL_CAUSE_QUASAR:
      return "quasar";


    case KILL_CAUSE_NAVHAZ:
      return "navhaz";


    case KILL_CAUSE_SELF_DESTRUCT:
      return "self_destruct";


    default:
      return "other";
    }
}


/*


 * PostgreSQL-backed ship destruction handler
 * Replaces old SQLite implementation.
 *
 * Side-effects only:
 *   - ship destroyed
 *   - player ship detached / escape pod spawned
 *   - XP penalty applied
 *   - times_blown_up incremented
 *   - podded_status updated
 *   - engine_events row written
 */
int
handle_ship_destruction (db_t *db, ship_kill_context_t *ctx)
{
  if (!db || !ctx)
    {
      return -1;
    }

  const long long now_ts = (long long) time (NULL);


  LOGI ("handle_ship_destruction: victim_player=%d ship=%d cause=%d",
	ctx->victim_player_id, ctx->victim_ship_id, ctx->cause);

  const char *cause_text = kill_cause_to_text (ctx->cause);

  int rc = -1;
  int32_t result_code = -1;
  if (repo_ships_handle_destruction (db,
				     ctx->victim_player_id,
				     ctx->victim_ship_id,
				     ctx->killer_player_id,
				     cause_text,
				     ctx->sector_id,
				     g_cfg.death.xp_loss_flat,
				     g_cfg.death.xp_loss_percent,
				     g_cfg.death.max_per_day,
				     g_cfg.death.big_sleep_duration_seconds,
				     now_ts, &result_code) == 0)
    {
      rc = (int) result_code;
    }
  else
    {
      LOGE ("handle_ship_destruction: Repository error");
      return -1;
    }

  if (rc != 0)
    {
      LOGE ("handle_ship_destruction: DB returned error code %d", rc);
    }
  return rc;
}


int
cmd_ship_transfer_cargo (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return 0;
    }
  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVICE_UNAVAILABLE,
                           "Database unavailable.");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  int commodity_id = 0, quantity = 0, target_ship_id = 0;
  if (!json_is_object (data)
      || !json_get_int_flexible (data, "commodity_id", &commodity_id)
      || !json_get_int_flexible (data, "quantity", &quantity)
      || !json_get_int_flexible (data, "target_ship_id", &target_ship_id)
      || commodity_id <= 0 || quantity <= 0 || target_ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "commodity_id, quantity and target_ship_id must be positive integers");
      return 0;
    }
  int source_ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (source_ship_id <= 0 || target_ship_id == source_ship_id)
    {
      send_response_refused_steal (ctx, root, REF_TARGET_SHIP_INVALID,
                                   "Target ship is invalid.", NULL);
      return 0;
    }
  int source_sector = db_get_ship_sector_id (db, source_ship_id);
  int target_sector = db_get_ship_sector_id (db, target_ship_id);
  if (source_sector <= 0 || target_sector != source_sector)
    {
      send_response_refused_steal (ctx, root, REF_TARGET_SHIP_INVALID,
                                   "Target ship is not in your sector.", NULL);
      return 0;
    }
  int owner_id = 0, owner_corp = 0;
  if (db_get_ship_owner_id (db, target_ship_id, &owner_id, &owner_corp) != 0
      || owner_id != ctx->player_id)
    {
      send_response_refused_steal (ctx, root, REF_SHIP_NOT_OWNED_OR_PILOTED,
                                   "You do not own the target ship.", NULL);
      return 0;
    }
  char commodity[16] = {0};
  db_res_t *commodity_res = NULL;
  db_error_t err = {0};
  const char *lookup = "SELECT code FROM commodities WHERE commodities_id = {1};";
  char lookup_sql[256];
  if (sql_build (db, lookup, lookup_sql, sizeof lookup_sql) != 0
      || !db_query (db, lookup_sql, (db_bind_t[]){db_bind_i64 (commodity_id)}, 1,
                    &commodity_res, &err)
      || !db_res_step (commodity_res, &err))
    {
      if (commodity_res) db_res_finalize (commodity_res);
      send_response_error (ctx, root, ERR_INVALID_ARG, "Unknown commodity_id");
      return 0;
    }
  const char *code = db_res_col_text (commodity_res, 0, &err);
  if (!code || strlen (code) >= sizeof commodity)
    {
      db_res_finalize (commodity_res);
      send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid commodity");
      return 0;
    }
  strlcpy (commodity, code, sizeof commodity);
  db_res_finalize (commodity_res);
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_DB, "Could not start cargo transfer");
      return 0;
    }
  int locked_source_ship_id = 0;
  /* Lock hulls in stable order before the player row. This matches cargo
   * trade settlement's ship -> account lock order. */
  int rc = repo_cargo_lock_ships_for_update (db, source_ship_id,
                                              target_ship_id);
  if (rc == 0)
    rc = repo_ships_get_active_id_for_update (db, ctx->player_id,
                                               &locked_source_ship_id);
  if (rc == 0 && locked_source_ship_id != source_ship_id)
    rc = ERR_TARGET_INVALID;
  if (rc == 0)
    {
      source_sector = db_get_ship_sector_id (db, source_ship_id);
      target_sector = db_get_ship_sector_id (db, target_ship_id);
      if (source_sector <= 0 || target_sector != source_sector)
        rc = ERR_TARGET_INVALID;
    }
  if (rc == 0)
    {
      owner_id = 0;
      owner_corp = 0;
      if (db_get_ship_owner_id (db, target_ship_id, &owner_id, &owner_corp) != 0
          || owner_id != ctx->player_id)
        rc = ERR_SHIP_NOT_FOUND;
    }
  int64_t source_after = 0, target_after = 0;
  if (rc == 0)
    rc = repo_cargo_add (db, source_ship_id, commodity, -(int64_t) quantity,
                         &source_after);
  if (rc == 0)
    rc = repo_cargo_add (db, target_ship_id, commodity, quantity, &target_after);
  if (rc == 0)
    rc = repo_cargo_sync_legacy_columns (db, source_ship_id);
  if (rc == 0)
    rc = repo_cargo_sync_legacy_columns (db, target_ship_id);
  if (rc != 0 || !db_tx_commit (db, &err))
    {
      db_tx_rollback (db, &err);
      if (rc == ERR_HOLD_FULL)
        send_response_refused_steal (ctx, root, ERR_HOLD_FULL,
                                     "Target ship does not have enough cargo capacity.", NULL);
      else if (rc == ERR_DB_MISUSE)
        send_response_refused_steal (ctx, root, ERR_INVALID_ARG,
                                     "Source ship does not have enough cargo.", NULL);
      else if (rc == ERR_TARGET_INVALID || rc == ERR_SHIP_NOT_FOUND)
        send_response_refused_steal (ctx, root, REF_TARGET_SHIP_INVALID,
                                     "Ship ownership or location changed during transfer.", NULL);
      else
        send_response_error (ctx, root, ERR_DB, "Cargo transfer failed");
      return 0;
    }
  json_t *response = json_object ();
  json_object_set_new (response, "source_ship_id", json_integer (source_ship_id));
  json_object_set_new (response, "target_ship_id", json_integer (target_ship_id));
  json_object_set_new (response, "commodity_id", json_integer (commodity_id));
  json_object_set_new (response, "commodity_code", json_string (commodity));
  json_object_set_new (response, "quantity", json_integer (quantity));
  json_object_set_new (response, "source_remaining", json_integer (source_after));
  json_object_set_new (response, "target_quantity", json_integer (target_after));
  send_response_ok_take (ctx, root, "ship.cargo_transferred_v1", &response);
  return 0;
}


int
cmd_ship_upgrade (client_ctx_t *ctx, json_t *root)
{
  send_response_error (ctx,
		       root,
		       ERR_NOT_IMPLEMENTED,
		       "Use hardware.buy for components or shipyard.upgrade for hull exchange.");
  return 0;
}


int
cmd_ship_repair (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();

  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_NOT_AUTHENTICATED,
				   "Auth required", NULL);
      return 0;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);


  if (ship_id <= 0)
    {
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "No active ship.");
      return 0;
    }

  int port_id = 0, port_type_id = 0;
  int rc = repo_stardock_get_port_by_sector (db, ctx->sector_id,
                                             &port_id, &port_type_id);
  if (rc != 0 && rc != 1)
    {
      send_response_error (ctx, root, ERR_DB, "DB error checking location.");
      return 0;
    }
  if (rc == 1)
    {
      send_response_refused_steal (ctx, root, ERR_NOT_AT_SHIPYARD,
				   "Must be at Stardock or Class 0 port.",
				   NULL);
      return 0;
    }
  porttype_t port_type = { 0 };
  if (!repo_porttypes_get_by_id (db, port_type_id, &port_type))
    {
      send_response_error (ctx, root, ERR_DB, "DB error checking port type.");
      return 0;
    }
  if (!port_type.is_stardock && strcmp (port_type.code, "CLASS0") != 0)
    {
      send_response_refused_steal (ctx, root, ERR_NOT_AT_SHIPYARD,
                                   "Must be at Stardock or Class 0 port.",
                                   NULL);
      return 0;
    }

  repo_combat_ship_full_t ship_state = { 0 };
  if (db_combat_load_ship_full_unlocked (db, ship_id, &ship_state) != 0)
    {
      send_response_error (ctx, root, ERR_DB, "DB error reading hull.");
      return 0;
    }
  int current_hull = ship_state.hull;

  if (current_hull >= 100)
    {
      json_t *res = json_object ();


      json_object_set_new (res, "repaired", json_boolean (0));
      json_object_set_new (res, "cost", json_integer (0));
      json_object_set_new (res, "hull", json_integer (100));
      send_response_ok_take (ctx, root, "ship.repair", &res);
      return 0;
    }

  int to_repair = 100 - current_hull;
  int cost = to_repair * 10;

  long long new_player_credits = 0;
  db_error_t err;

  bool ok = db_ship_repair_atomic (db,
				   ctx->player_id,
				   ship_id,
				   cost,
				   (int64_t *) & new_player_credits,
				   &err);


  if (!ok)
    {
      if (err.code == ERR_INSUFFICIENT_FUNDS)
	{
	  send_response_refused_steal (ctx, root, ERR_INSUFFICIENT_FUNDS,
				       "Insufficient credits.", NULL);
	  return 0;
	}
      if (err.code == ERR_SHIP_NOT_FOUND)
	{
	  send_response_error (ctx, root, ERR_SHIP_NOT_FOUND,
			       "Ship not found or update failed.");
	  return 0;
	}
      send_response_error (ctx, root, ERR_DB, "Repair failed.");
      return 0;
    }

  json_t *res = json_object ();


  json_object_set_new (res, "repaired", json_boolean (1));
  json_object_set_new (res, "cost", json_integer (cost));
  json_object_set_new (res, "hull", json_integer (100));
  send_response_ok_take (ctx, root, "ship.repair", &res);
  return 0;
}


// Helper to get active ship ID for a player
int
h_get_active_ship_id (db_t *db, int player_id)
{
  int ship_id = 0;
  if (repo_ships_get_active_id (db, player_id, &ship_id) != 0)
    {
      LOGE ("Failed to get active ship id for player %d", player_id);
    }
  return ship_id;
}


/*
 * cmd_ship_self_destruct: Initiate player ship self-destruct sequence.
 */
int
cmd_ship_self_destruct (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  int confirmation = 0;
  json_t *data = json_object_get (root, "data");
  if (!data || json_unpack (data, "{s:i}", "confirmation", &confirmation) != 0
      || confirmation == 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_CONFIRMATION_REQUIRED,
				   "Self-destruct requires explicit non-zero integer confirmation.",
				   NULL);
      return -1;
    }
  /* 2. Refusal check: Cannot self-destruct in protected zones (FedSpace) */
  if (db_is_sector_fedspace (db, ctx->sector_id))
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_FORBIDDEN_IN_SECTOR,
				   "Cannot self-destruct in protected FedSpace.",
				   NULL);
      return -1;
    }
  /* 3. Invoke handle_ship_destruction for self-destruct logic */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  int ship_sector = 0;


  ship_sector = db_get_ship_sector_id (db, ship_id);

  ship_kill_context_t kill_ctx = {
    .victim_player_id = ctx->player_id,
    .victim_ship_id = ship_id,
    .killer_player_id = ctx->player_id,	// Self-destruct, so killer is also the victim
    .cause = KILL_CAUSE_SELF_DESTRUCT,
    .sector_id = ship_sector > 0 ? ship_sector : 1	// Fallback to sector 1 if current sector is invalid
  };
  int destroy_rc = handle_ship_destruction (db, &kill_ctx);


  if (destroy_rc != 0)
    {
      LOGE
	("cmd_ship_self_destruct: handle_ship_destruction failed for player %d, ship %d",
	 ctx->player_id, ship_id);
      send_response_error (ctx,
			   root,
			   ERR_PLANET_NOT_FOUND,
			   "Failed to process self-destruction.");
      return 0;
    }
  /* 4. Response: command acknowledged and processed */
  send_response_ok_take (ctx, root, "ship.self_destruct.confirmed", NULL);
  return 0;
}


int
cmd_ship_inspect (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_SECTOR_NOT_FOUND,
				   "Not authenticated", NULL);
      return 0;
    }


  json_t *data = json_object_get (root, "data");
  json_t *requested = json_is_object (data) ? json_object_get (data, "ship_id") : NULL;
  if (!json_is_object (data) || json_object_size (data) != 1 ||
      !json_is_integer (requested) || json_integer_value (requested) <= 0 || ctx->sector_id <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Valid ship_id and current sector required");
      return 0;
    }

  json_t *ships = NULL;
  int rc = db_ships_inspectable_at_sector_json (db, ctx->player_id,
                                                ctx->sector_id, &ships);
  if (rc != 0 || !ships)
    {
      send_response_error (ctx, root, ERR_DB, "Database error");
      return 0;
    }

  size_t i;
  json_t *ship, *selected = NULL;
  json_array_foreach (ships, i, ship)
    {
      json_t *id = json_object_get (ship, "ship_id");
      if (json_is_integer (id) && json_integer_value (id) == json_integer_value (requested))
        {
          selected = ship;
          break;
        }
    }
  if (!selected)
    {
      json_decref (ships);
      send_response_error (ctx, root, ERR_SHIP_NOT_FOUND, "Ship not found or not visible");
      return 0;
    }
  json_t *payload = json_object ();
  json_object_set (payload, "ship", selected);
  json_decref (ships);
  send_response_ok_take (ctx, root, "ship.inspect", &payload);
  return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////


/* ship.rename + ship.reregister */
int
cmd_ship_rename (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_SECTOR_NOT_FOUND,
				   "Not authenticated", NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_NOT_IN_SECTOR, "Bad request", NULL);
      return 0;
    }
  json_t *j_name = json_object_get (data, "name");


  if (json_object_size (data) != 1 || !json_is_string (j_name) || json_string_length (j_name) == 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_NOT_IN_SECTOR,
				   "Missing ship name", NULL);
      return 0;
    }
  const char *new_name = json_string_value (j_name);
  size_t name_bytes = json_string_length (j_name);
  size_t name_chars = 0;
  for (size_t i = 0; i < name_bytes; i++)
    if (((unsigned char) new_name[i] & 0xc0) != 0x80)
      name_chars++;
  if (memchr (new_name, '\0', name_bytes) || name_chars > 100)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Ship name must be 1 to 100 characters");
      return 0;
    }
  db_t *db = game_db_get_handle ();
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  int64_t rows = 0;
  int rc = ship_id > 0 ? repo_ships_rename_active (db, ctx->player_id, ship_id, new_name, &rows) : 0;


  if (rc == 0 && rows != 1)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TURN_COST_EXCEEDS,
				   "Permission denied", NULL);
      return 0;
    }
  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_DB, "Database error");
      return 0;
    }
  json_t *payload = json_object ();


  json_object_set_new (payload, "ship_id", json_integer (ship_id));
  json_object_set_new (payload, "name", json_string (new_name));


  send_response_ok_take (ctx, root, "ship.renamed", &payload);
  return 0;
}


/* ship.claim */
int
cmd_ship_claim (client_ctx_t *ctx, json_t *root)
{
  db_t *db_handle = game_db_get_handle ();
  h_decloak_ship (db_handle,
		  h_get_active_ship_id (db_handle, ctx->player_id));
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_SECTOR_NOT_FOUND,
				   "Not authenticated", NULL);
      return 0;
    }
  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 1;
  json_t *data = json_object_get (root, "data");


  if (json_is_object (data))
    {
      json_t *jsec = json_object_get (data, "sector_id");


      if (json_is_integer (jsec))
	{
	  int s = (int) json_integer_value (jsec);


	  if (s > 0)
	    {
	      sector_id = s;
	    }
	}
    }
  int ship_id = -1;


  if (json_is_object (data))
    {
      json_t *j_ship = json_object_get (data, "ship_id");


      if (json_is_integer (j_ship))
	{
	  ship_id = (int) json_integer_value (j_ship);
	}
    }
  if (ship_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_NOT_IN_SECTOR,
				   "Missing ship_id", NULL);
      return 0;
    }
  json_t *ship = NULL;
  int rc =
    db_ship_claim (db_handle, ctx->player_id, sector_id, ship_id, &ship);


  if (rc != 0 || !ship)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_SAFE_ZONE_ONLY,
				   "Ship not claimable", NULL);
      return 0;
    }
  json_t *payload = json_object ();


  json_object_set_new (payload, "ship", ship);


  send_response_ok_take (ctx, root, "ship.claimed", &payload);
  return 0;
}


/* ship.status (canonical) */
int
cmd_ship_status (client_ctx_t *ctx, json_t *root)
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
  json_t *player_info = NULL;


  if (db_player_info_json (db, ctx->player_id, &player_info) != 0
      || !player_info)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "Database error");
      return 0;
    }
  // Extract ship-only view from player_info (adjust keys to match your schema)
  json_t *ship = json_object_get (player_info, "ship");


  if (!json_is_object (ship))
    {
      json_decref (player_info);
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "No ship info");
      return 0;
    }

  /* Hull is authoritative ship state used by public maintenance decisions.
   * Use the repository read and fail closed if it cannot load the ship; never
   * report a default value for a damaged ship. */
  int ship_id = (int) json_integer_value (json_object_get (ship, "id"));
  repo_combat_ship_full_t ship_state = { 0 };
  if (ship_id <= 0
      || db_combat_load_ship_full_unlocked (db, ship_id, &ship_state) != 0)
    {
      json_decref (player_info);
      send_response_error (ctx, root, ERR_DB, "Database error reading ship hull.");
      return 0;
    }

  /* Do not mutate the player-info object: it is also used by other response
   * paths. Build this command's ship snapshot with the additional field. */
  json_t *ship_status = json_deep_copy (ship);
  if (!ship_status || json_object_set_new (ship_status, "hull",
                                           json_integer (ship_state.hull)) != 0)
    {
      json_decref (ship_status);
      json_decref (player_info);
      send_response_error (ctx, root, ERR_DB, "Could not build ship status.");
      return 0;
    }
  db_res_t *scanner_res = NULL;
  db_error_t scanner_err;
  if (repo_stardock_get_ship_state (db, ship_id, &scanner_res) != 0
      || !scanner_res || !db_res_step (scanner_res, &scanner_err))
    {
      if (scanner_res)
	db_res_finalize (scanner_res);
      json_decref (ship_status);
      json_decref (player_info);
      send_response_error (ctx, root, ERR_DB,
				   "Database error reading scanner equipment.");
      return 0;
    }
  int has_long_range_scanner =
    (int) db_res_col_i64 (scanner_res, 9, &scanner_err);
  db_res_finalize (scanner_res);
  if (json_object_set_new (ship_status, "has_long_range_scanner",
				   json_boolean (has_long_range_scanner != 0)) != 0)
    {
      json_decref (ship_status);
      json_decref (player_info);
      send_response_error (ctx, root, ERR_DB, "Could not build ship status.");
      return 0;
    }
  json_t *payload = json_object ();
  if (!payload)
    {
      json_decref (ship_status);
      json_decref (player_info);
      send_response_error (ctx, root, ERR_DB, "Could not build ship status.");
      return 0;
    }
  if (json_object_set_new (payload, "ship", ship_status) != 0)
    {
      json_decref (payload);
      json_decref (player_info);
      send_response_error (ctx, root, ERR_DB, "Could not build ship status.");
      return 0;
    }
  send_response_ok_take (ctx, root, "ship.status", &payload);
  json_decref (player_info);
  return 0;
}


/* ship.info legacy alias → call status then mark deprecated (in envelope meta) */
int
cmd_ship_info_compat (client_ctx_t *ctx, json_t *root)
{
  // Call canonical
  int rc = cmd_ship_status (ctx, root);
  // If your envelope code supports meta injection per request, you can set it there.
  // Otherwise leave as-is; the handler name + docs indicate deprecation.
  return rc;
}


int
cmd_ship_tow_status (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_NOT_AUTHENTICATED,
				   "Not authenticated", NULL);
      return 0;
    }
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }

  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_NO_ACTIVE_SHIP,
				   "You do not have an active ship.", NULL);
      return 0;
    }

  int towing_ship_id = 0;
  int towed_by_ship_id = 0;
  if (repo_ships_get_towing_id (db, ship_id, &towing_ship_id) != 0
      || repo_ships_get_is_being_towed_by (db, ship_id,
						   &towed_by_ship_id) != 0)
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Database error");
      return 0;
    }

  json_t *payload = json_object ();
  json_object_set_new (payload, "ship_id", json_integer (ship_id));
  json_object_set_new (payload, "towing_ship_id",
		       towing_ship_id > 0 ? json_integer (towing_ship_id) : json_null ());
  json_object_set_new (payload, "towed_by_ship_id",
		       towed_by_ship_id > 0 ? json_integer (towed_by_ship_id) : json_null ());
  send_response_ok_take (ctx, root, "ship.tow.status", &payload);
  return 0;
}

int
cmd_ship_tow (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }

  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NOT_AUTHENTICATED,
				   "Not authenticated", NULL);
      return 0;
    }


  int player_ship_id = h_get_active_ship_id (db,
					     ctx->player_id);


  if (player_ship_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NO_ACTIVE_SHIP,
				   "You do not have an active ship.", NULL);
      return 0;
    }

  /* 1. Get Player's Current Tow Status */
  int current_towing_ship_id = 0;
  if (repo_ships_get_towing_id (db, player_ship_id, &current_towing_ship_id)
      != 0)
    {
      LOGE ("cmd_ship_tow: DB error checking status for ship %d",
	    player_ship_id);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Database error");
      return 0;
    }

  /* 2. Parse Input */
  json_t *data = json_object_get (root, "data");
  int target_ship_id = 0;


  if (json_is_object (data))
    {
      json_t *j_target = json_object_get (data, "target_ship_id");


      if (json_is_integer (j_target))
	{
	  target_ship_id = (int) json_integer_value (j_target);
	}
    }

  /* 3. DISENGAGE LOGIC */
  if (current_towing_ship_id != 0)
    {
      if (target_ship_id == 0 || target_ship_id == current_towing_ship_id)
	{
	  db_error_t err;
	  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
	    {
	      goto rollback;
	    }

	  // 3a. Clear player's towing field
	  if (repo_ships_clear_towing_id (db, player_ship_id) != 0)
	    {
	      goto rollback;
	    }

	  // 3b. Clear target's "being towed" field
	  if (repo_ships_clear_is_being_towed_by (db, current_towing_ship_id)
	      != 0)
	    {
	      goto rollback;
	    }

	  if (!db_tx_commit (db, &err))
	    {
	      goto rollback;
	    }

	  {
	    json_t *tmp = json_object ();


	    json_object_set_new (tmp, "status",
				 json_string ("Towing beam disengaged"));
	    json_object_set_new (tmp, "towee_ship_id",
				 json_integer (current_towing_ship_id));


	    send_response_ok_take (ctx, root, "ship.tow.disengaged", &tmp);
	  }
	  return 0;
	}
      else
	{
	  send_response_refused_steal (ctx,
				       root,
				       REF_ALREADY_TOWING,
				       "You are already towing another ship. Release it first.",
				       NULL);
	  return 0;
	}
    }

  /* 4. ENGAGE LOGIC - Prerequisites */
  if (target_ship_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_MISSING_FIELD,
				   "Invalid target ship ID.", NULL);
      return 0;
    }
  if (target_ship_id == player_ship_id)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TARGET_SHIP_INVALID,
				   "You cannot tow yourself.", NULL);
      return 0;
    }

  int target_sector = 0;


  target_sector = db_get_ship_sector_id (db, target_ship_id);
  int player_sector = 0;


  player_sector = db_get_ship_sector_id (db, player_ship_id);


  if (target_sector <= 0 || target_sector != player_sector)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_TARGET_SHIP_INVALID,
				   "Target ship not in your sector.", NULL);
      return 0;
    }

  // Ownership Check
  int owner_id = 0, corp_id = 0;


  db_get_ship_owner_id (db, target_ship_id, &owner_id, &corp_id);

  bool is_mine = (owner_id == ctx->player_id);
  bool is_corp = (ctx->corp_id > 0 && corp_id == ctx->corp_id);


  if (!is_mine && !is_corp)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_SHIP_NOT_OWNED_OR_PILOTED,
				   "You do not own that ship.", NULL);
      return 0;
    }

  // Unmanned Check
  if (db_is_ship_piloted (db, target_ship_id))
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_SHIP_NOT_OWNED_OR_PILOTED,
				   "Target ship is currently piloted.", NULL);
      return 0;
    }

  // Already Towed Check
  int is_being_towed = 0;
  if (repo_ships_get_is_being_towed_by (db, target_ship_id, &is_being_towed)
      != 0)
    {
      LOGE ("cmd_ship_tow: DB error checking towed status for ship %d",
	    target_ship_id);
    }

  if (is_being_towed != 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   REF_ALREADY_BEING_TOWED,
				   "Target is already under tow.", NULL);
      return 0;
    }

  /* 5. ENGAGE LOGIC - Execution */
  db_error_t err;
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      goto rollback;
    }

  // 5a. Set player's towing_ship_id -> target
  if (repo_ships_set_towing_id (db, player_ship_id, target_ship_id) != 0)
    {
      goto rollback;
    }

  // 5b. Set target's is_being_towed_by -> player
  if (repo_ships_set_is_being_towed_by (db, target_ship_id, player_ship_id) !=
      0)
    {
      goto rollback;
    }

  if (!db_tx_commit (db, &err))
    {
      goto rollback;
    }

  {
    json_t *tmp = json_object ();


    json_object_set_new (tmp, "status", json_string ("Towing beam engaged"));
    json_object_set_new (tmp, "towee_ship_id", json_integer (target_ship_id));


    send_response_ok_take (ctx, root, "ship.tow.engaged", &tmp);
  }
  return 0;

rollback:
  db_tx_rollback (db, NULL);
  LOGE ("cmd_ship_tow: DB Transaction failed: %s", err.message);
  send_response_error (ctx,
		       root,
		       ERR_DB_QUERY_FAILED, "Database transaction error");
  return 0;
}


int
cmd_ship_list (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  if (data && !json_is_object (data))
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA,
                           "data must be an object");
      return 0;
    }
  db_t *db = game_db_get_handle ();
  db_res_t *res = NULL;
  db_error_t err;
  const char *sql_template =
    "SELECT s.ship_id, s.name, s.type_id, s.sector_id, s.holds, "
    "s.fighters, s.shields, (p.ship_id = s.ship_id) AS active "
    "FROM ships s JOIN ship_ownership o ON o.ship_id = s.ship_id "
    "LEFT JOIN players p ON p.player_id = o.player_id "
    "WHERE o.player_id = {1} AND o.is_primary = {2} "
    "ORDER BY s.ship_id ASC;";
  char sql[1024];
  if (sql_build (db, sql_template, sql, sizeof sql) != 0
      || !db_query (db, sql,
                    (db_bind_t[]){db_bind_i64 (ctx->player_id),
                                  db_bind_bool (true)}, 2, &res, &err))
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Unable to list ships");
      return 0;
    }
  json_t *payload = json_object ();
  json_t *ships = json_array ();
  while (db_res_step (res, &err))
    {
      json_t *ship = json_object ();
      const char *name = db_res_col_text (res, 1, &err);
      json_object_set_new (ship, "ship_id",
                           json_integer (db_res_col_i32 (res, 0, &err)));
      json_object_set_new (ship, "name", json_string (name ? name : ""));
      json_object_set_new (ship, "type_id",
                           json_integer (db_res_col_i32 (res, 2, &err)));
      json_object_set_new (ship, "sector_id",
                           json_integer (db_res_col_i32 (res, 3, &err)));
      json_object_set_new (ship, "holds",
                           json_integer (db_res_col_i32 (res, 4, &err)));
      json_object_set_new (ship, "fighters",
                           json_integer (db_res_col_i32 (res, 5, &err)));
      json_object_set_new (ship, "shields",
                           json_integer (db_res_col_i32 (res, 6, &err)));
      json_object_set_new (ship, "active",
                           json_boolean (db_res_col_int (res, 7, &err)));
      json_array_append_new (ships, ship);
    }
  db_res_finalize (res);
  json_object_set_new (payload, "ships", ships);
  send_response_ok_take (ctx, root, "ship.list.response", &payload);
  return 0;
}


int
cmd_ship_sell (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  int ship_id = 0, replacement_ship_id = 0;
  if (!json_is_object (data)
      || !json_get_int_flexible (data, "ship_id", &ship_id)
      || !json_get_int_flexible (data, "replacement_ship_id",
                                 &replacement_ship_id)
      || ship_id <= 0 || replacement_ship_id <= 0
      || ship_id == replacement_ship_id)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "ship_id and a different replacement_ship_id are required");
      return 0;
    }
  if (!g_cfg.shipyard_enabled)
    {
      send_response_error (ctx, root, ERR_SERVICE_UNAVAILABLE,
                           "Shipyard services are disabled");
      return 0;
    }
  int sale_factor_bp = g_cfg.shipyard_sale_factor_bp;
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVICE_UNAVAILABLE,
                           "Database unavailable");
      return 0;
    }
  repo_ship_sale_result_t result = {0};
  int rc = repo_stardock_sell_ship (db, ctx->player_id, ship_id,
                                    replacement_ship_id,
                                    sale_factor_bp, &result);
  if (rc != 0)
    {
      if (rc == ERR_NOT_AT_SHIPYARD)
        send_response_error (ctx, root, ERR_NOT_AT_SHIPYARD,
                             "Your active ship must be docked at an authorized shipyard");
      else if (rc == ERR_BANNED_FROM_JURISDICTION)
        send_response_error (ctx, root, rc,
                             "Local authorities prohibit this shipyard transaction");
      else if (rc == ERR_PERMISSION_DENIED)
        send_response_error (ctx, root, rc,
                             "Ship ownership, replacement eligibility, or last-ship rule prevents sale");
      else if (rc == ERR_TARGET_INVALID)
        send_response_error (ctx, root, rc,
                             "The selected replacement ship is not eligible");
      else if (rc == ERR_SHIP_NOT_FOUND)
        send_response_error (ctx, root, rc, "Ship not found");
      else if (rc == ERR_DB_BUSY)
        send_response_error (ctx, root, rc,
                             "Ship state changed concurrently; retry the sale");
      else
        send_response_error (ctx, root, ERR_DB,
                             "Ship sale transaction failed");
      return 0;
    }
  if (result.sold_active_ship)
    ctx->ship_id = replacement_ship_id;
  json_t *payload = json_pack
    ("{s:I,s:I,s:i,s:I,s:I,s:i,s:I,s:I,s:b,s:b,s:I,s:I}",
     "ship_id", (json_int_t) ship_id,
     "replacement_ship_id", (json_int_t) replacement_ship_id,
     "shipyard_port_id", result.port_id,
     "hull_valuation", (json_int_t) result.hull_valuation,
     "installed_equipment_valuation", (json_int_t) result.equipment_valuation,
     "sale_factor_bp", sale_factor_bp,
     "sale_proceeds", (json_int_t) result.proceeds,
     "updated_credits", (json_int_t) result.updated_credits,
     "cargo_forfeited", true,
     "insurance_terminated", true,
     "sale_id", (json_int_t) result.sale_id,
     "used_inventory_id", (json_int_t) result.used_inventory_id);
  if (!payload)
    {
      /* Settlement is committed; the durable sale record is authoritative. */
      send_response_error (ctx, root, ERR_SERVER_ERROR,
                           "Sale completed, but response construction failed; query shipyard records");
      return 0;
    }
  send_response_ok_take (ctx, root, "ship.sold_v1", &payload);
  return 0;
}


int
cmd_ship_transfer (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  int ship_id = 0, target_player_id = 0;
  if (!json_is_object (data)
      || !json_get_int_flexible (data, "ship_id", &ship_id)
      || !json_get_int_flexible (data, "target_player_id", &target_player_id)
      || ship_id <= 0 || target_player_id <= 0
      || target_player_id == ctx->player_id)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "ship_id and a different target_player_id are required");
      return 0;
    }

  db_t *db = game_db_get_handle ();
  /* Use the player repository for an early, side-effect-free refusal.  The
   * ship repository repeats the existence check while locking both players;
   * that in-transaction check remains authoritative when a recipient changes
   * between this read and the ownership mutation. */
  int target_exists = 0;
  int target_rc = repo_players_check_exists (db, target_player_id,
                                            &target_exists);
  if (target_rc != 0)
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Could not validate transfer recipient");
      return 0;
    }
  if (!target_exists)
    {
      send_response_refused_steal (ctx, root, ERR_TARGET_INVALID,
                                   "Transfer recipient does not exist", NULL);
      return 0;
    }

  /* Both-player locking and the ownership mutation belong to the ship
   * repository.  Keeping that validation inside the transaction closes the
   * check-then-transfer race and makes the repository the only ownership
   * mutation boundary. */
  db_error_t err = {0};
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Could not start ship transfer");
      return 0;
    }

  int64_t rows = 0;
  int rc = repo_ships_transfer_primary (db, ship_id, ctx->player_id,
                                        target_player_id, &rows);
  if (rc != 0 || rows != 1)
    {
      db_tx_rollback (db, &err);
      /* repo_ships_transfer_primary owns the guarded ownership predicate.
       * A failed predicate is ordinary gameplay refusal, not a persistence
       * outage; preserve the no-write transaction outcome on the wire. */
      /* A zero-row guarded update is a gameplay refusal only when the
       * repository completed successfully.  Do not let the initial zero
       * value of rows hide a query/transaction failure from the client or
       * from rollback evidence. */
      if (rc == 0 && rows == 0)
        send_response_refused_steal (ctx, root, ERR_TARGET_INVALID,
                                     "Ship is not transferable or target is invalid",
                                     NULL);
      else if (rc == ERR_TARGET_INVALID || rc == ERR_INVALID_ARG)
        send_response_refused_steal (ctx, root, ERR_TARGET_INVALID,
                                     "Ship is not transferable or target is invalid",
                                     NULL);
      else if (rc != 0)
        send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                             "Ship transfer failed");
      return 0;
    }
  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, &err);
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED,
                           "Ship transfer could not be committed");
      return 0;
    }

  json_t *payload = json_object ();
  json_object_set_new (payload, "ship_id", json_integer (ship_id));
  json_object_set_new (payload, "from_player_id",
                       json_integer (ctx->player_id));
  json_object_set_new (payload, "target_player_id",
                       json_integer (target_player_id));
  send_response_ok_take (ctx, root, "ship.transfer.response", &payload);
  return 0;
}


int
h_update_ship_cargo (db_t *db,
		     int ship_id,
		     const char *commodity_code,
		     int delta, int *new_quantity_out)
{
  LOGI ("h_update_ship_cargo: entered for ship_id=%d, commodity=%s, delta=%d",
	ship_id, commodity_code, delta);

  if (!db || ship_id <= 0 || !commodity_code)
    {
      LOGE
	("h_update_ship_cargo: invalid params: db=%p, ship_id=%d, commodity_code=%s",
	 (void *) db, ship_id, commodity_code ? commodity_code : "NULL");
      if (new_quantity_out)
	{
	  *new_quantity_out = 0;
	}
      return ERR_DB_MISUSE;
    }

  /* Phase 1: Route through cargo layer (ship_cargo is source of truth) */
  int64_t new_qty = 0;
  db_error_t txerr = {0};
  if (!db_tx_begin (db, DB_TX_DEFAULT, &txerr))
    return txerr.code ? txerr.code : ERR_DB_TX;
  int rc = repo_cargo_add (db, ship_id, commodity_code, (int64_t) delta,
                           &new_qty);

  /* The cargo table is authoritative, but the legacy ships.* columns are
   * still read by older gameplay handlers and by the public ship status
   * path.  Keep the compatibility columns in sync after every successful
   * mutation so a subsequent request cannot observe stale cargo. */
  if (rc == 0)
    rc = repo_cargo_sync_legacy_columns (db, ship_id);

  if (rc != 0)
    {
      db_tx_rollback (db, NULL);
      return rc;
    }
  if (!db_tx_commit (db, &txerr))
    {
      db_tx_rollback (db, NULL);
      return txerr.code ? txerr.code : ERR_DB_TX;
    }

  if (new_quantity_out)
    {
      *new_quantity_out = (int) new_qty;
    }

  return rc;
}


int
h_get_ship_cargo_and_holds (db_t *db,
			    int ship_id,
			    int *ore,
			    int *organics,
			    int *equipment,
			    int *colonists,
			    int *slaves, int *weapons, int *drugs, int *holds)
{
  if (!db || ship_id <= 0)
    {
      return ERR_DB_MISUSE;
    }

  /* Keep legacy callers working for ships that predate ship_cargo, but use
   * the authoritative cargo rows whenever they exist.  Several handlers
   * mutate ship_cargo directly; reading only ships.* here makes the next
   * request see stale quantities. */
  int legacy_ore = 0, legacy_org = 0, legacy_eq = 0;
  int legacy_col = 0, legacy_slv = 0, legacy_wpn = 0, legacy_drg = 0;
  int rc = repo_ships_get_cargo_and_holds (db, ship_id,
                                           &legacy_ore, &legacy_org,
                                           &legacy_eq, &legacy_col,
                                           &legacy_slv, &legacy_wpn,
                                           &legacy_drg, holds);
  if (rc != 0)
    return rc;

  int64_t cargo_total = 0;
  rc = repo_cargo_get_total (db, ship_id, &cargo_total);
  if (rc != 0)
    return rc;
  if (cargo_total == 0)
    {
      if (ore) *ore = legacy_ore;
      if (organics) *organics = legacy_org;
      if (equipment) *equipment = legacy_eq;
      if (colonists) *colonists = legacy_col;
      if (slaves) *slaves = legacy_slv;
      if (weapons) *weapons = legacy_wpn;
      if (drugs) *drugs = legacy_drg;
      return 0;
    }

  int64_t value = 0;
  if ((rc = repo_cargo_get (db, ship_id, "ORE", &value)) != 0) return rc;
  if (ore) *ore = (int) value;
  if ((rc = repo_cargo_get (db, ship_id, "ORG", &value)) != 0) return rc;
  if (organics) *organics = (int) value;
  if ((rc = repo_cargo_get (db, ship_id, "EQU", &value)) != 0) return rc;
  if (equipment) *equipment = (int) value;
  if ((rc = repo_cargo_get (db, ship_id, "COL", &value)) != 0) return rc;
  if (colonists) *colonists = (int) value;
  if ((rc = repo_cargo_get (db, ship_id, "SLV", &value)) != 0) return rc;
  if (slaves) *slaves = (int) value;
  if ((rc = repo_cargo_get (db, ship_id, "WPN", &value)) != 0) return rc;
  if (weapons) *weapons = (int) value;
  if ((rc = repo_cargo_get (db, ship_id, "DRG", &value)) != 0) return rc;
  if (drugs) *drugs = (int) value;
  return 0;
}
