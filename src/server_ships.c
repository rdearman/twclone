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
#include <sqlite3.h>
/* local includes */
#include "schemas.h"
#include "server_cmds.h"
#include "server_loop.h"
#include "server_ships.h"
#include "database.h"
#include "errors.h"
#include "config.h"
#include "common.h"
#include "server_envelope.h"
#include "server_rules.h"
#include "server_players.h"
#include "server_log.h"




void handle_move_pathfind (client_ctx_t * ctx, json_t * root);

void send_enveloped_ok (int fd, json_t * root, const char *type,
			json_t * data);
void send_enveloped_error (int fd, json_t * root, int code, const char *msg);
void send_enveloped_refused (int fd, json_t * root, int code, const char *msg,
			     json_t * data_opt);


/**
 * @brief Handles the 'combat.attack' command.
 * * @param ctx The player context (struct context *).
 * @param root The root JSON object containing the command payload.
 * @return int Returns 0 on successful processing (or error handling).
 */
int handle_combat_attack(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db = db_get_handle();
    
    
    // All combat actions reveal the ship
    h_decloak_ship(db, h_get_active_ship_id (db, ctx->player_id));

  TurnConsumeResult tc = h_consume_player_turn(db, ctx, "combat.attack");
  if (tc != TURN_CONSUME_SUCCESS) {
      return handle_turn_consumption_error(ctx, tc, "combat.attack", root, NULL);
  }    
    
    // --- COMBAT ATTACK LOGIC GOES HERE ---
    
    // 2. Determine target (player ship, planet, port, etc.)
    // 3. Perform attack calculations (fighters, cannons, torpedoes)
    // 4. Update the DB with results (losses, sector change if target destroyed)
    // 5. Send successful ACK/status to client
    
    return 0; // Success
}

/**
 * @brief Handles the 'combat.flee' command.
 * * @param ctx The player context (struct context *).
 * @param root The root JSON object containing the command payload.
 * @return int Returns 0 on successful processing (or error handling).
 */
int handle_combat_flee(client_ctx_t *ctx, json_t *root) {
    sqlite3 *db_handle = db_get_handle();
    
    // Attempting to flee reveals the ship
    h_decloak_ship(db_handle, h_get_active_ship_id(db_handle, (ctx->player_id)));

    TurnConsumeResult tc = h_consume_player_turn(db_handle, ctx, "combat.flee");
    if (tc != TURN_CONSUME_SUCCESS) {
      return handle_turn_consumption_error(ctx, tc, "combat.flee", root, NULL);
    }    	
    
    // --- COMBAT FLEE LOGIC GOES HERE ---
    
    // 2. Determine success chance based on ship speed, opponent status, etc.
    // 3. If successful, potentially move the ship one warp or clear the combat status flag.
    // 4. If unsuccessful, opponent might get a free attack or the ship remains in combat.
    // 5. Send successful ACK/status to client
    
    return 0; // Success
}

int
cmd_ship_transfer_cargo (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db_handle = db_get_handle ();


  h_decloak_ship (db_handle,
		  h_get_active_ship_id (db_handle, ctx->player_id));
  STUB_NIY (ctx, root, "ship.transfer_cargo");
}

int
cmd_ship_jettison (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db_handle = db_get_handle ();
  // Actions reveal the ship
  h_decloak_ship(db_handle, h_get_active_ship_id(db_handle, ctx->player_id));

  TurnConsumeResult tc = h_consume_player_turn(db_handle, ctx, "trade.jettison");
    if (tc != TURN_CONSUME_SUCCESS) {
      return handle_turn_consumption_error(ctx, tc, "trade.jettison", root, NULL);
    }    

  STUB_NIY (ctx, root, "ship.jettison");
}

int
cmd_ship_upgrade (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "ship.upgrade");
}

int
cmd_ship_repair (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "ship.repair");
}


static void
mark_deprecated (json_t *res)
{
  json_t *meta = json_object_get (res, "meta");
  if (!meta)
    {
      meta = json_object ();
      json_object_set_new (res, "meta", meta);
    }
  json_object_set_new (meta, "deprecated", json_true ());
}


/* ship.inspect */


/* ship.inspect */
int
cmd_ship_inspect (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 1;
  json_t *jdata = json_object_get (root, "data");
  if (json_is_object (jdata))
    {
      json_t *jsec = json_object_get (jdata, "sector_id");
      if (json_is_integer (jsec))
	{
	  int s = (int) json_integer_value (jsec);
	  if (s > 0)
	    sector_id = s;
	}
    }

  json_t *ships = NULL;
  int rc =
    db_ships_inspectable_at_sector_json (ctx->player_id, sector_id, &ships);
  if (rc != SQLITE_OK || !ships)
    {
      send_enveloped_error (ctx->fd, root, 1500, "Database error");
      return 0;
    }

  json_t *payload =
    json_pack ("{s:i s:o}", "sector", sector_id, "ships", ships);
  send_enveloped_ok (ctx->fd, root, "ship.inspect", payload);
  json_decref (payload);
  return 0;
}

/* ship.rename + ship.reregister */
int
cmd_ship_rename (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_refused (ctx->fd, root, 1400, "Bad request", NULL);
      return 0;
    }
  json_t *j_ship = json_object_get (data, "ship_id");
  json_t *j_name = json_object_get (data, "new_name");
  if (!json_is_integer (j_ship) || !json_is_string (j_name))
    {
      send_enveloped_refused (ctx->fd, root, 1400, "Missing ship_id/new_name",
			      NULL);
      return 0;
    }
  int ship_id = (int) json_integer_value (j_ship);
  const char *new_name = json_string_value (j_name);

  int rc = db_ship_rename_if_owner (ctx->player_id, ship_id, new_name);
  if (rc == SQLITE_CONSTRAINT)
    {
      send_enveloped_refused (ctx->fd, root, 1403, "Permission denied", NULL);
      return 0;
    }
  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 1500, "Database error");
      return 0;
    }

  json_t *payload =
    json_pack ("{s:i s:s}", "ship_id", ship_id, "name", new_name);
  send_enveloped_ok (ctx->fd, root, "ship.renamed", payload);
  json_decref (payload);
  return 0;
}

/* ship.claim */
int
cmd_ship_claim (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db_handle = db_get_handle ();
  h_decloak_ship (db_handle,
		  h_get_active_ship_id (db_handle, ctx->player_id));

  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
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
	    sector_id = s;
	}
    }

  int ship_id = -1;
  if (json_is_object (data))
    {
      json_t *j_ship = json_object_get (data, "ship_id");
      if (json_is_integer (j_ship))
	ship_id = (int) json_integer_value (j_ship);
    }
  if (ship_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1400, "Missing ship_id", NULL);
      return 0;
    }

  json_t *ship = NULL;
  int rc = db_ship_claim (ctx->player_id, sector_id, ship_id, &ship);
  if (rc != SQLITE_OK || !ship)
    {
      send_enveloped_refused (ctx->fd, root, 1406, "Ship not claimable",
			      NULL);
      return 0;
    }

  json_t *payload = json_pack ("{s:o}", "ship", ship);
  send_enveloped_ok (ctx->fd, root, "ship.claimed", payload);
  json_decref (payload);
  return 0;
}

/* ship.status (canonical) */
int
cmd_ship_status (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  json_t *player_info = NULL;
  if (db_player_info_json (ctx->player_id, &player_info) != SQLITE_OK
      || !player_info)
    {
      send_enveloped_error (ctx->fd, root, 1500, "Database error");
      return 0;
    }

  // Extract ship-only view from player_info (adjust keys to match your schema)
  json_t *ship = json_object_get (player_info, "ship");
  if (!json_is_object (ship))
    {
      json_decref (player_info);
      send_enveloped_error (ctx->fd, root, 1500, "No ship info");
      return 0;
    }
  // Build payload (clone or pack fields as needed)
  json_t *payload = json_pack ("{s:O}", "ship", ship);
  send_enveloped_ok (ctx->fd, root, "ship.status", payload);
  json_decref (payload);
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


void process_ship_destruction(int attacker_id, int victim_id, const char *victim_ship_name, int sector)
{
    // 1. Build the JSON payload using the helper json_pack()
    json_t *payload = json_pack("{s:s, s:i, s:i}", 
                                "ship_name", victim_ship_name, 
                                "victim_player_id", victim_id, 
                                "attacker_player_id", attacker_id);
    
    // 2. Log the event
    int rc = db_log_engine_event(
        (long long)time(NULL), 
        "combat.ship_destroyed", 
        attacker_id, 
        sector, 
        payload
    );
    
    // 3. Cleanup the JSON object
    json_decref(payload);
    
    if (rc != SQLITE_OK) {
        // Log event logging failure
    }
}
