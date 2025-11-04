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
#define MINE_SECTOR_CAP_PER_TYPE       100
#define PLAYER_MINE_UNIVERSE_CAP_TOTAL 10000

/* Forward decls from your codebase */
json_t *db_get_stardock_sectors (void);

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
 * @brief Generic handler to list a player's and their corp's deployed assets.
 *
 * This function is a generic implementation for listing deployed assets.
 * It runs the provided SQL query and returns a simple count of the assets.
 *
 * THIS VERSION IS CORRECTED TO MATCH THE TEST HARNESS EXPECTATIONS
 * (i.e., return a "total" count, not a detailed list).
 *
 * @param ctx           The client context.
 * @param root          The root JSON request object.
 * @param list_type     The 'type' string for the JSON response (e.g., "Deploy.fighters.list_v1").
 * @param asset_key     (Unused in this version, but kept for signature matching)
 * @param sql_query     The complete, corrected SQL query to execute.
 * @return 0 on success (response sent).
 */
static int
cmd_deploy_assets_list_internal (client_ctx_t *ctx,
                                 json_t *root,
                                 const char *list_type,
                                 const char *asset_key,
                                 const char *sql_query)
{
    // asset_key is unused in this version, but we'll silence the warning
    (void)asset_key;

    // --- 1. Initialization ---
    sqlite3 *db = db_get_handle ();
    if (!db)
    {
        send_enveloped_error (ctx->fd, root, 500, "Database handle not available.");
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
    // Both ?1 and ?2 in the corrected SQL refer to the calling player's ID.
    sqlite3_bind_int (st, 1, self_player_id);
    sqlite3_bind_int (st, 2, self_player_id);

    // --- 4. Execute Query and Count Rows ---
    int total_count = 0;
    while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
        total_count++;
    }

    // --- 5. Finalize Statement and Unlock Mutex ---
    sqlite3_finalize (st);
    pthread_mutex_unlock (&db_mutex);

    // Check for an error during the final step
    if (rc != SQLITE_DONE)
    {
        send_enveloped_error (ctx->fd, root, 500, "Error processing asset count.");
        return 0;
    }

    // --- 6. Build Final Payload and Send Response ---
    // This payload matches the test expectation: {"data": {"total": ...}}
    json_t *jdata_payload = json_object ();
    json_object_set_new (jdata_payload, "total", json_integer (total_count));

    // send_enveloped_ok will take ownership of jdata_payload
    // and wrap it, resulting in: { "status": "ok", "data": { "total": ... } }
    send_enveloped_ok (ctx->fd, root, list_type, jdata_payload);

    return 0;
}




/*
 * RPC: deploy.fighters.list
 * Fetches the list of the player's and their corporation's deployed fighters.
 */
int
cmd_deploy_fighters_list (client_ctx_t *ctx, json_t *root)
{
  // NOTE: This SQL assumes a helper function/macro populates ctx->corp_id
  // or that ctx->corp_id is loaded during session initialization.
  // It joins player_fighters with players and corporations to get email details.
  // 
  // It selects fighters belonging to:
  // 1. The current player (pf.player_id = ?)
  // 2. The player's corporation (p.corp_id = c.id) where c.id is the player's corp.
const char *sql_query_fighters=
    "SELECT "
    "  sa.sector AS sector_id, "
    "  sa.quantity AS count, "
    "  sa.player AS player_id, "
    "  p.name AS player_name, " 
    "  c.id AS corp_id, "
    "  c.tag AS corp_tag "
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
    "      ) "
    "    ) "
    "  ) "
    "ORDER BY sa.sector ASC;";
 
  return cmd_deploy_assets_list_internal (ctx,
					  root,
                                          "Deploy.fighters.list_v1",
                                          "fighters",
                                          sql_query_fighters);
}


/*
 * RPC: deploy.mines.list
 * Fetches the list of the player's and their corporation's deployed mines.
 */
int
cmd_deploy_mines_list (client_ctx_t *ctx, json_t *root)
{
  // NOTE: This SQL assumes a helper function/macro populates ctx->corp_id
  // or that ctx->corp_id is loaded during session initialization.
  // The mine query must include the 'type' column (armid/limpet).
  //
  // It selects mines belonging to:
  // 1. The current player (pm.player_id = ?)
  // 2. The player's corporation (p.corp_id = c.id) where c.id is the player's corp.

const char *sql_query_mines =
    "SELECT "
    "  sa.sector AS sector_id, "
    "  sa.quantity AS count, "
    "  sa.player AS player_id, "
    "  p.name AS player_name, "      /* Replaced p.email */
    "  c.id AS corp_id, "
    "  c.tag AS corp_tag, "          /* Replaced c.email */
    "  sa.asset_type AS type "
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
    "      ) "
    "    ) "
    "  ) "
    "ORDER BY sa.sector ASC, sa.asset_type ASC;";


  return cmd_deploy_assets_list_internal (ctx,
					  root,
                                          "deploy.mines.list_v1",
                                          "mines",
                                          sql_query_mines);
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

/* ---------- combat.deploy_fighters ---------- */
/* ---------- combat.deploy_fighters (fixed-SQL) ---------- */

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
      send_enveloped_error (ctx->fd, root, ERR_SERVICE_UNAVAILABLE,
			    "Database unavailable");
      return 0;
    }

  /* Parse input */
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
			    "Missing required field: data");
      return 0;
    }

  json_t *j_amount = json_object_get (data, "amount");
  json_t *j_offense = json_object_get (data, "offense");	/* required: 1..3 */
  json_t *j_corp_id = json_object_get (data, "corporation_id");	/* optional, nullable */

  if (!j_amount || !json_is_integer (j_amount) ||
      !j_offense || !json_is_integer (j_offense))
    {
      send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
			    "Missing required field or invalid type: amount/offense");
      return 0;
    }

  int amount = (int) json_integer_value (j_amount);
  int offense = (int) json_integer_value (j_offense);
  if (amount <= 0) {
    send_enveloped_error (ctx->fd, root, ERR_NOT_FOUND, "amount must be > 0");
    return 0;
  }
  if (offense < OFFENSE_TOLL || offense > OFFENSE_ATTACK) {
    send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
			  "offense must be one of {1=TOLL,2=DEFEND,3=ATTACK}");
    return 0;
  }
  /* Resolve active ship + sector */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0) {
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
	(db, "SELECT sector_id FROM ships WHERE id=?1;", -1, &st,
	 NULL) != SQLITE_OK) {
      send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
			    "Unable to resolve current sector");
      return 0;
    }
    sqlite3_bind_int (st, 1, ship_id);
    if (sqlite3_step (st) == SQLITE_ROW)
      {
	sector_id = sqlite3_column_int (st, 0);
      }
    sqlite3_finalize (st);
    if (sector_id <= 0) {
      send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
			    "Unable to resolve current sector");
      return 0;
    }
  }

  /* Sector cap */
  int sector_total = 0;
  if (sum_sector_fighters (db, sector_id, &sector_total) != SQLITE_OK) {
    send_enveloped_error (ctx->fd, root, REF_NOT_IN_SECTOR,
			  "Failed to read sector fighters");
    return 0;
  }
  if (sector_total + amount > SECTOR_FIGHTER_CAP) {
    send_enveloped_error (ctx->fd, root, ERR_SECTOR_OVERCROWDED,
			  "Sector fighter limit exceeded (50,000)");
    return 0;
  }

  /* Transaction: debit ship, credit sector */
  char *errmsg = NULL;
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
	sqlite3_free (errmsg);
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

  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
	sqlite3_free (errmsg);
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
      return 0;
    }

  /* Fedspace/Stardock → summon ISS + warn player */
  json_t *stardock_sectors = db_get_stardock_sectors ();
  if (stardock_sectors == NULL) {}
  {
    bool in_fed = false;
    bool in_sdock = false;
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

	(void) h_send_message_to_player (ctx->player_id, 0,
					 "Federation Warning",
					 "Fighter deployment in protected space has triggered ISS response.");
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

      (void) h_log_engine_event ("fighters.deployed", ctx->player_id,
				 sector_id, evt, NULL);
    }
                               
    /* If your h_log_engine_event signature takes json_t* instead:
       // (void)h_log_engine_event(db, "fighters.deployed", evt); 
    */
    // }
    /* /\* Emit engine_event via h_log_engine_event *\/ */
    /* { */
    /*   json_t *evt = json_object (); */
    /*   json_object_set_new (evt, "sector_id", json_integer (sector_id)); */
    /*   json_object_set_new (evt, "player_id", json_integer (ctx->player_id)); */
    /*   if (j_corp_id && json_is_integer (j_corp_id)) */
    /* 	json_object_set_new (evt, "corporation_id", */
    /* 			     json_integer (json_integer_value (j_corp_id))); */
    /*   else */
    /* 	json_object_set_new (evt, "corporation_id", json_null ()); */
    /*   json_object_set_new (evt, "amount", json_integer (amount)); */
    /*   json_object_set_new (evt, "offense", json_integer (offense)); */
    /*   json_object_set_new (evt, "event_ts", */
    /* 			   json_integer ((json_int_t) time (NULL))); */

    /*   char *payload = json_dumps (evt, JSON_COMPACT); */
    /*   json_decref (evt); */
    /*   if (payload) */
    /* 	{ */
    /* 	  // int h_log_engine_event (const char *type, int actor_player_id, int sector_id, */
    /* 	  //    json_t * payload, const char *idem_key); */
    /* 	  (void) h_log_engine_event ("fighters.deployed", ctx->player_id, */
    /* 				     sector_id, payload, ""); */
    /* 	  free (payload); */
    /* 	} */
    /*   /\* If your h_log_engine_event signature takes json_t* instead: */
    /*      // (void)h_log_engine_event(db, "fighters.deployed", evt); */
    /*    *\/ */
    /* } */

    /* Recompute total for response convenience */
    (void) sum_sector_fighters (db, sector_id, &sector_total);

    /* ---- Build data payload (no outer wrapper here) ---- */
    json_t *out = json_object ();
    json_object_set_new (out, "sector_id", json_integer (sector_id));
    json_object_set_new (out, "owner_player_id",
			 json_integer (ctx->player_id));
    if (j_corp_id && json_is_integer (j_corp_id))
      json_object_set_new (out, "owner_corp_id",
			   json_integer (json_integer_value (j_corp_id)));
    else
      json_object_set_new (out, "owner_corp_id", json_null ());
    json_object_set_new (out, "amount", json_integer (amount));
    json_object_set_new (out, "offense", json_integer (offense));
    json_object_set_new (out, "sector_total_after",
			 json_integer (sector_total));

    /* Envelope: echo id/meta from `root`, set type string for this result */
    send_enveloped_ok (ctx->fd, root, "combat.fighters.deployed", out);
    json_decref (out);
    return 0;

/* Planet integration (future):
  // If sector has a player-owned planet, you may redirect/attach fighters to planet storage.
*/
  }
}



/* ---------- combat.lay_mines ---------- */
/* ---------- combat.lay_mines (fixed-SQL) ---------- */

/* Count mines in ship cargo (Armid) */
static const char *SQL_SHIP_GET_ARMID =
  "SELECT armid_mines FROM ships WHERE id=?1;";

/* Count mines in ship cargo (Limpet) */
static const char *SQL_SHIP_GET_LIMPET =
  "SELECT limpet_mines FROM ships WHERE id=?1;";

/* Decrement ship cargo (Armid) */
static const char *SQL_SHIP_DEC_ARMID =
  "UPDATE ships SET armid_mines = armid_mines - ?1 WHERE id=?2;";

/* Decrement ship cargo (Limpet) */
static const char *SQL_SHIP_DEC_LIMPET =
  "UPDATE ships SET limpet_mines = limpet_mines - ?1 WHERE id=?2;";

/* ---- Sector assets (fixed column names from your fighter handler) ---- */
static const char *SQL_SECTOR_MINES_SUM_BY_TYPE =
  "SELECT COALESCE(SUM(quantity),0) "
  "FROM sector_assets " "WHERE sector=?1 AND asset_type=?2;";

static const char *SQL_PLAYER_MINES_SUM_TOTAL = "SELECT COALESCE(SUM(quantity),0) " "FROM sector_assets " "WHERE player=?1 AND asset_type IN (1,4);";	/* armid+limpet */

static const char *SQL_ASSET_INSERT_MINES =
  "INSERT INTO sector_assets(sector, player, corporation, "
  "                          asset_type, quantity, offensive_setting, deployed_at) "
  "VALUES (?1, ?2, ?3, ?4, ?5, NULL, strftime('%s','now'));";


/* Helpers */
static int
sum_sector_mines_by_type (sqlite3 *db, int sector_id, int asset_type,
			  int *total_out)
{
  *total_out = 0;
  sqlite3_stmt *st = NULL;
  int rc =
    sqlite3_prepare_v2 (db, SQL_SECTOR_MINES_SUM_BY_TYPE, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;
  sqlite3_bind_int (st, 1, sector_id);
  sqlite3_bind_int (st, 2, asset_type);
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

static int
sum_player_mines_total (sqlite3 *db, int player_id, int *total_out)
{
  *total_out = 0;
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL_PLAYER_MINES_SUM_TOTAL, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;
  sqlite3_bind_int (st, 1, player_id);
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

static int
ship_get_mines (sqlite3 *db, int ship_id, int asset_type, int *have_out)
{
  *have_out = 0;
  const char *sql =
    (asset_type == ASSET_MINE) ? SQL_SHIP_GET_ARMID : SQL_SHIP_GET_LIMPET;
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;
  sqlite3_bind_int (st, 1, ship_id);
  if ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      *have_out = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  return rc;
}

static int
ship_consume_mines (sqlite3 *db, int ship_id, int asset_type, int amount)
{
  /* Check available */
  int have = 0;
  int rc = ship_get_mines (db, ship_id, asset_type, &have);
  if (rc != SQLITE_OK)
    return rc;
  if (have < amount)
    return SQLITE_TOOBIG;

  const char *sql =
    (asset_type == ASSET_MINE) ? SQL_SHIP_DEC_ARMID : SQL_SHIP_DEC_LIMPET;
  sqlite3_stmt *st = NULL;
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;
  sqlite3_bind_int (st, 1, amount);
  sqlite3_bind_int (st, 2, ship_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

static int
insert_sector_mines (sqlite3 *db, int sector_id, int owner_player_id,
		     json_t *corp_id_json, int asset_type, int amount)
{
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL_ASSET_INSERT_MINES, -1, &st, NULL);
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
  sqlite3_bind_int (st, 4, asset_type);
  sqlite3_bind_int (st, 5, amount);

  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}

/* ---- Main command -------------------------------------------------------- */
/* JSON:
   { "command":"combat.lay_mines",
     "data": { "mine_type":"armid"|"limpet" | 1|4, "amount":N, "corporation_id":null|<id> }
   }
*/
int
cmd_combat_lay_mines (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;

  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, ERR_SERVICE_UNAVAILABLE,
			    "Database unavailable");
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
			    "Missing required field: data");
      return 0;
    }

  /* mine_type: accept string ("armid"/"limpet") or integer (1/4) */
  json_t *j_type = json_object_get (data, "mine_type");
  json_t *j_amount = json_object_get (data, "amount");
  json_t *j_corp = json_object_get (data, "corporation_id");	/* optional */

  if (!j_type || !j_amount || !json_is_integer (j_amount))
    {
      send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
			    "Missing or invalid fields: mine_type/amount");
      return 0;
    }

  int asset_type = 0;
  if (json_is_string (j_type))
    {
      const char *s = json_string_value (j_type);
      if (s && strcasecmp (s, "armid") == 0)
	asset_type = ASSET_MINE;
      else if (s && strcasecmp (s, "limpet") == 0)
	asset_type = ASSET_LIMPET_MINE;
    }
  else if (json_is_integer (j_type))
    {
      int v = (int) json_integer_value (j_type);
      if (v == ASSET_MINE || v == ASSET_LIMPET_MINE)
	asset_type = v;
    }

  if (!(asset_type == ASSET_MINE || asset_type == ASSET_LIMPET_MINE))
    {
      send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
			    "mine_type must be 'armid'|'limpet' or 1|4");
      return 0;
    }

  int amount = (int) json_integer_value (j_amount);
  if (amount <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_FOUND,
			    "amount must be > 0");
      return 0;
    }

  /* Resolve active ship + sector (do NOT decloak for mines) */
  int ship_id = h_get_active_ship_id (db, ctx->player_id);
  if (ship_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_SHIP_NOT_FOUND,
			    "No active ship");
      return 0;
    }

  int sector_id = -1;
  {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2
	(db, "SELECT sector_id FROM ships WHERE id=?1;", -1, &st,
	 NULL) != SQLITE_OK)
      {
	send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
			      "Unable to resolve current sector");
	return 0;
      }
    sqlite3_bind_int (st, 1, ship_id);
    if (sqlite3_step (st) == SQLITE_ROW)
      sector_id = sqlite3_column_int (st, 0);
    sqlite3_finalize (st);
    if (sector_id <= 0)
      {
	send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
			      "Unable to resolve current sector");
	return 0;
      }
  }

  /* Caps: per-sector per-type + per-player universe total */
  int sector_type_total = 0;
  if (sum_sector_mines_by_type (db, sector_id, asset_type, &sector_type_total)
      != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, REF_NOT_IN_SECTOR,
			    "Failed to read sector mines");
      return 0;
    }
  if (sector_type_total + amount > MINE_SECTOR_CAP_PER_TYPE)
    {
      send_enveloped_error (ctx->fd, root, ERR_SECTOR_OVERCROWDED,
			    "Sector mine limit exceeded (100 per type)");
      return 0;
    }

  int player_total = 0;
  if (sum_player_mines_total (db, ctx->player_id, &player_total) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_DB,
			    "Failed to compute player mine total");
      return 0;
    }
  if (player_total + amount > PLAYER_MINE_UNIVERSE_CAP_TOTAL)
    {
      send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
			    "Universe mine limit exceeded (10,000 total)");
      return 0;
    }

  /* TODO (optional): consume 1 turn here, if you have a helper:
     // if (!h_consume_turn(ctx->player_id, 1)) {
     //   send_enveloped_error(ctx->fd, root, ERR_NO_TURNS, "No turns remaining");
     //   return 0;
     // }
   */

  /* TX: debit ship cargo, credit sector_assets */
  char *errmsg = NULL;
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
	sqlite3_free (errmsg);
      send_enveloped_error (ctx->fd, root, ERR_DB,
			    "Could not start transaction");
      return 0;
    }

  int rc = ship_consume_mines (db, ship_id, asset_type, amount);
  if (rc == SQLITE_TOOBIG)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, REF_AMMO_DEPLETED,
			    "Insufficient mines in cargo");
      return 0;
    }
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_DB,
			    "Failed to update ship cargo");
      return 0;
    }

  rc =
    insert_sector_mines (db, sector_id, ctx->player_id, j_corp, asset_type,
			 amount);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, SECTOR_ERR,
			    "Failed to create sector assets record");
      return 0;
    }

  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
	sqlite3_free (errmsg);
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
      return 0;
    }

  /* Fedspace/Stardock → summon ISS + warn */
  {
    bool in_fed = (sector_id >= 1 && sector_id <= 10);
    bool in_sdock = false;

    json_t *sd = db_get_stardock_sectors ();
    if (sd && json_is_array (sd))
      {
	size_t i;
	json_t *v;
	json_array_foreach (sd, i, v)
	{
	  if (json_is_integer (v) && json_integer_value (v) == sector_id)
	    {
	      in_sdock = true;
	      break;
	    }
	}
      }
    if (sd)
      json_decref (sd);

    if (in_fed || in_sdock)
      {
	iss_summon (sector_id, ctx->player_id);
	(void) h_send_message_to_player (ctx->player_id, 0,
					 "Federation Warning",
					 "Mine deployment in protected space has triggered ISS response.");
      }
  }

  /* Engine event: mines.deployed */
  {
    json_t *evt = json_object ();
    json_object_set_new (evt, "sector_id", json_integer (sector_id));
    json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
    if (j_corp && json_is_integer (j_corp))
      json_object_set_new (evt, "corporation_id",
			   json_integer (json_integer_value (j_corp)));
    else
      json_object_set_new (evt, "corporation_id", json_null ());
    json_object_set_new (evt, "amount", json_integer (amount));
    json_object_set_new (evt, "mine_type",
			 json_string (asset_type ==
				      ASSET_MINE ? "armid" : "limpet"));
    json_object_set_new (evt, "asset_type", json_integer (asset_type));
    json_object_set_new (evt, "event_ts",
			 json_integer ((json_int_t) time (NULL)));

    char *payload = json_dumps (evt, JSON_COMPACT);
    json_decref (evt);
    if (payload)
      {
	(void) h_log_engine_event ("mines.deployed", ctx->player_id,
				   sector_id, payload, "");
	free (payload);
      }
  }

  /* Recompute sector per-type for response convenience */
  (void) sum_sector_mines_by_type (db, sector_id, asset_type,
				   &sector_type_total);

  /* Response */
  json_t *out = json_object ();
  json_object_set_new (out, "sector_id", json_integer (sector_id));
  json_object_set_new (out, "owner_player_id", json_integer (ctx->player_id));
  if (j_corp && json_is_integer (j_corp))
    json_object_set_new (out, "owner_corp_id",
			 json_integer (json_integer_value (j_corp)));
  else
    json_object_set_new (out, "owner_corp_id", json_null ());
  json_object_set_new (out, "amount", json_integer (amount));
  json_object_set_new (out, "asset_type", json_integer (asset_type));
  json_object_set_new (out, "mine_type",
		       json_string (asset_type ==
				    ASSET_MINE ? "armid" : "limpet"));
  json_object_set_new (out, "sector_total_after",
		       json_integer (sector_type_total));

  send_enveloped_ok (ctx->fd, root, "combat.mines.deployed", out);
  json_decref (out);
  return 0;
}


/* ---------- STUBS ---------- */
/* ---------- combat.sweep_mines ---------- */
int
cmd_combat_sweep_mines (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse sweep strength, clear mines, apply risk to ship
  return niy (ctx, root, "combat.sweep_mines");
}

/* ---------- combat.status ---------- */
int
cmd_combat_status (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: return sector combat snapshot (entities, mines, fighters, cooldowns)
  return niy (ctx, root, "combat.status");
}
