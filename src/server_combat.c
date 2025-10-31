#include <jansson.h>
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
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

#define SECTOR_FIGHTER_CAP 50000

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
    return 1;
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


/**
 * @brief Handles the 'combat.attack' command.
 * * @param ctx The player context (struct context *).
 * @param root The root JSON object containing the command payload.
 * @return int Returns 0 on successful processing (or error handling).
 */
int
handle_combat_attack (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();


  // All combat actions reveal the ship
  h_decloak_ship (db, h_get_active_ship_id (db, ctx->player_id));

  TurnConsumeResult tc = h_consume_player_turn (db, ctx, "combat.attack");
  if (tc != TURN_CONSUME_SUCCESS)
    {
      return handle_turn_consumption_error (ctx, tc, "combat.attack", root,
					    NULL);
    }

  // --- COMBAT ATTACK LOGIC GOES HERE ---

  // 2. Determine target (player ship, planet, port, etc.)
  // 3. Perform attack calculations (fighters, cannons, torpedoes)
  // 4. Update the DB with results (losses, sector change if target destroyed)
  // 5. Send successful ACK/status to client

  return 0;			// Success
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

  return 0;			// Success
}


/* ---------- combat.attack ---------- */
int
cmd_combat_attack (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse target, resolve sector, dmg calc, state updates, events
  return niy (ctx, root, "combat.attack");
}

/* ---------- combat.deploy_fighters ---------- */
/* ---------- combat.deploy_fighters (fixed-SQL) ---------- */

/* typedef enum { */
/*     ASSET_MINE = 1, */
/*     ASSET_FIGHTER = 2, */
/*     ASSET_BEACON = 3, */
/*     ASSET_LIMPET_MINE = 4     */
/* } asset_type_t; */


static const char *SQL_SECTOR_FTR_SUM =
  "SELECT COALESCE(SUM(quantity),0) "
  "FROM sector_assets " "WHERE sector_id=?1 AND asset_type='2';";

static const char *SQL_SHIP_GET_FTR =
  "SELECT fighters FROM ships WHERE id=?1;";

static const char *SQL_SHIP_DEC_FTR =
  "UPDATE ships SET fighters=fighters-?1 WHERE id=?2;";

static const char *SQL_ASSET_INSERT =
  "INSERT INTO sector_assets(sector, player, corporation, "
  "                          asset_type, quantity, offensive_setting, deployed_at) "
  "VALUES (?1, ?2, ?3, 'fighters', ?4, ?5, strftime('%s','now'));";



/* Sum fighters already in the sector. */
static int
sum_sector_fighters (sqlite3 *db, int sector_id, int *total_out)
{
  *total_out = 0;
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL_SECTOR_FTR_SUM, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;
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
static int
ship_consume_fighters (sqlite3 *db, int ship_id, int amount)
{
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL_SHIP_GET_FTR, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  int have = 0;
  if ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      have = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  if (rc != SQLITE_OK)
    return rc;

  if (have < amount)
    return SQLITE_TOOBIG;

  rc = sqlite3_prepare_v2 (db, SQL_SHIP_DEC_FTR, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;
  sqlite3_bind_int (st, 1, amount);
  sqlite3_bind_int (st, 2, ship_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

/* Insert a sector_assets row for fighters. */
static int
insert_sector_fighters (sqlite3 *db,
			int sector_id, int owner_player_id,
			json_t *corp_id_json /* nullable */ ,
			int offense_mode, int amount)
{
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL_ASSET_INSERT, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st, 1, sector_id);
  sqlite3_bind_int (st, 2, owner_player_id);

  if (corp_id_json && json_is_integer (corp_id_json))
    {
      sqlite3_bind_int (st, 3, (int) json_integer_value (corp_id_json));
    }
  else
    {
      sqlite3_bind_null (st, 3);
    }

  sqlite3_bind_int (st, 4, amount);
  sqlite3_bind_int (st, 5, offense_mode);

  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
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
	  json_decref (j_sector);	// Clean up the orphaned reference
	  // You may choose to stop here or continue
	}
    }

  // 3. Handle step errors if the loop didn't finish normally (SQLITE_DONE)
  if (rc != SQLITE_DONE)
    {
      fprintf (stderr, "DB Error: Failed to step stardock query: %s\n",
	       sqlite3_errmsg (db));
      json_decref (sector_list);	// Cleanup the partially built list
      sqlite3_finalize (stmt);
      return NULL;
    }

  // 4. Clean up and return
  sqlite3_finalize (stmt);

  return sector_list;
}


int
cmd_combat_deploy_fighters (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;

  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, ERR_SERVICE_UNAVAILABLE, "Database unavailable");
      return 0;
    }

  /* Parse input */
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
     send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD, "Missing required field: data");
     return 0;
    }	

  json_t *j_amount = json_object_get (data, "amount");
  json_t *j_offense = json_object_get (data, "offense");	/* required: 1..3 */
  json_t *j_corp_id = json_object_get (data, "corporation_id");	/* optional, nullable */

  if (!j_amount || !json_is_integer (j_amount) ||
      !j_offense || !json_is_integer (j_offense))
    {
      send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID, "Missing required field or invalid type: amount/offense");
    }

  int amount = (int) json_integer_value (j_amount);
  int offense = (int) json_integer_value (j_offense);
  if (amount <= 0)
    send_enveloped_error (ctx->fd, root,ERR_NOT_FOUND, "amount must be > 0");
  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK)
          send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,"offense must be one of {1=TOLL,2=DEFEND,3=ATTACK}");
  /* Resolve active ship + sector */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    send_enveloped_error (ctx->fd, root, ERR_SHIP_NOT_FOUND, "No active ship");

  /* Decloak: visible hostile/defensive action */
  (void) h_decloak_ship (db, ship_id);

  int sector_id = -1;
  {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2
	(db, "SELECT sector_id FROM ships WHERE id=?1;", -1, &st,
	 NULL) != SQLITE_OK)
      send_enveloped_error (ctx->fd, root,ERR_SECTOR_NOT_FOUND,"Unable to resolve current sector");
    sqlite3_bind_int (st, 1, ship_id);
    if (sqlite3_step (st) == SQLITE_ROW)
      {
	sector_id = sqlite3_column_int (st, 0);
      }
    sqlite3_finalize (st);
    if (sector_id <= 0)
      send_enveloped_error (ctx->fd, root,ERR_SECTOR_NOT_FOUND, "Unable to resolve current sector");
  }

  /* Sector cap */
  int sector_total = 0;
  if (sum_sector_fighters (db, sector_id, &sector_total) != SQLITE_OK)
    send_enveloped_error (ctx->fd, root, REF_NOT_IN_SECTOR , "Failed to read sector fighters");
  if (sector_total + amount > SECTOR_FIGHTER_CAP)
    send_enveloped_error (ctx->fd, root,ERR_SECTOR_OVERCROWDED,  "Sector fighter limit exceeded (50,000)");

  /* Transaction: debit ship, credit sector */
  char *errmsg = NULL;
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
	sqlite3_free (errmsg);
      send_enveloped_error (ctx->fd, root, ERR_DB  , "Could not start transaction");
    }

  int rc = ship_consume_fighters (db, ship_id, amount);
  if (rc == SQLITE_TOOBIG)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED, "Insufficient fighters on ship");
    }
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED, "Failed to update ship fighters");
    }

  rc =
    insert_sector_fighters (db, sector_id, ctx->player_id, j_corp_id, offense,
			    amount);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, SECTOR_ERR, "Failed to create sector assets record");
    }

  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
	sqlite3_free (errmsg);
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
    }

  /* Fedspace/Stardock → summon ISS + warn player */
  json_t *stardock_sectors = db_get_stardock_sectors ();
  {
    bool in_fed = false, in_sdock = false;
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

    if (in_fed)
      {
	iss_summon (sector_id, ctx->player_id);
	// h_send_message_to_player (int player_id, int sender_id, const char *subject,
	// const char *message)

	(void) h_send_message_to_player (ctx->player_id, 0, "Federation Warning", "Fighter deployment in protected space has triggered ISS response.");
      }

  /* Emit engine_event via h_log_engine_event */
  {
    json_t *evt = json_object();
    json_object_set_new(evt, "sector_id",        json_integer(sector_id));
    json_object_set_new(evt, "player_id",        json_integer(ctx->player_id));
    if (j_corp_id && json_is_integer(j_corp_id))
      json_object_set_new(evt, "corporation_id", json_integer(json_integer_value(j_corp_id)));
    else
      json_object_set_new(evt, "corporation_id", json_null());
    json_object_set_new(evt, "amount",           json_integer(amount));
    json_object_set_new(evt, "offense",          json_integer(offense));
    json_object_set_new(evt, "event_ts",         json_integer((json_int_t)time(NULL)));

    char *payload = json_dumps(evt, JSON_COMPACT);
    json_decref(evt);
    if (payload) {
	(void) h_log_engine_event("fighters.deployed", ctx->player_id, sector_id,  payload, NULL);
      free(payload);
    }
    /* If your h_log_engine_event signature takes json_t* instead:
       // (void)h_log_engine_event(db, "fighters.deployed", evt);
    */
  }

    /* Emit engine_event via h_log_engine_event */
    {
      json_t *evt = json_object ();
      json_object_set_new (evt, "sector_id", json_integer (sector_id));
      json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
      if (j_corp_id && json_is_integer (j_corp_id))
	json_object_set_new (evt, "corporation_id",
			     json_integer (json_integer_value (j_corp_id)));
      else
	json_object_set_new (evt, "corporation_id", json_null ());
      json_object_set_new (evt, "amount", json_integer (amount));
      json_object_set_new (evt, "offense", json_integer (offense));
      json_object_set_new (evt, "event_ts",
			   json_integer ((json_int_t) time (NULL)));

      char *payload = json_dumps (evt, JSON_COMPACT);
      json_decref (evt);
      if (payload)
	{
	  // int h_log_engine_event (const char *type, int actor_player_id, int sector_id,
	  //	json_t * payload, const char *idem_key);
	  (void) h_log_engine_event ("fighters.deployed",ctx->player_id, sector_id, payload, "");
	  free (payload);
	}
      /* If your h_log_engine_event signature takes json_t* instead:
         // (void)h_log_engine_event(db, "fighters.deployed", evt);
       */
    }

    /* Recompute total for response convenience */
    (void) sum_sector_fighters (db, sector_id, &sector_total);

    /* ---- Build data payload (no outer wrapper here) ---- */
    json_t *out = json_object();
    json_object_set_new(out, "sector_id",          json_integer(sector_id));
    json_object_set_new(out, "owner_player_id",    json_integer(ctx->player_id));
    if (j_corp_id && json_is_integer(j_corp_id))
      json_object_set_new(out, "owner_corp_id",    json_integer(json_integer_value(j_corp_id)));
    else
      json_object_set_new(out, "owner_corp_id",    json_null());
    json_object_set_new(out, "amount",             json_integer(amount));
    json_object_set_new(out, "offense",            json_integer(offense));
    json_object_set_new(out, "sector_total_after", json_integer(sector_total));

    /* Envelope: echo id/meta from `root`, set type string for this result */
    send_enveloped_ok(ctx->fd, root, "combat.fighters.deployed", out);
    json_decref(out);
    return 0;
 
/* Planet integration (future):
  // If sector has a player-owned planet, you may redirect/attach fighters to planet storage.
*/
  }
}



/* ---------- combat.lay_mines ---------- */
  int cmd_combat_lay_mines (client_ctx_t * ctx, json_t * root)
  {
    if (!require_auth (ctx, root))
      return 0;
    // TODO: parse amount, lay mines in sector grid
    return niy (ctx, root, "combat.lay_mines");
  }

/* ---------- combat.sweep_mines ---------- */
  int cmd_combat_sweep_mines (client_ctx_t * ctx, json_t * root)
  {
    if (!require_auth (ctx, root))
      return 0;
    // TODO: parse sweep strength, clear mines, apply risk to ship
    return niy (ctx, root, "combat.sweep_mines");
  }

/* ---------- combat.status ---------- */
  int cmd_combat_status (client_ctx_t * ctx, json_t * root)
  {
    if (!require_auth (ctx, root))
      return 0;
    // TODO: return sector combat snapshot (entities, mines, fighters, cooldowns)
    return niy (ctx, root, "combat.status");
  }
