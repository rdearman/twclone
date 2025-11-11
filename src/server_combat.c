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
#define SECTOR_MINE_CAP 50000
#define MINE_SECTOR_CAP_PER_TYPE       100
#define PLAYER_MINE_UNIVERSE_CAP_TOTAL 10000

/* Forward decls from your codebase */
json_t *db_get_stardock_sectors (void);
static int ship_consume_mines (sqlite3 *db, int ship_id, int asset_type, int amount);
static int insert_sector_mines (sqlite3 *db, int sector_id, int owner_player_id, json_t *corp_id_json, int asset_type, int offense_mode, int amount);
static int sum_sector_fighters (sqlite3 *db, int sector_id, int *total_out);
static int ship_consume_fighters (sqlite3 *db, int ship_id, int amount);
static int insert_sector_fighters (sqlite3 *db, int sector_id, int owner_player_id, json_t *corp_id_json, int offense_mode, int amount);

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



int
cmd_deploy_assets_list_internal (client_ctx_t *ctx,
                                   json_t *root,
                                   const char *list_type,
                                   const char *asset_key,
                                   const char *sql_query)
{
    (void)asset_key; // asset_key is unused

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
    sqlite3_bind_int (st, 1, self_player_id);
    sqlite3_bind_int (st, 2, self_player_id);

    // --- 4. Execute Query and Build Entries Array ---
    int total_count = 0;
    json_t *entries = json_array(); // Create the array for the entries

    while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
        total_count++;

        // Extract all columns from the row
        int asset_id     = sqlite3_column_int(st, 0); // NEW
        int sector_id    = sqlite3_column_int(st, 1);
        int count        = sqlite3_column_int(st, 2);
        int offense_mode = sqlite3_column_int(st, 3);
        int player_id    = sqlite3_column_int(st, 4);
        const char *player_name = (const char *)sqlite3_column_text(st, 5);
        int corp_id      = sqlite3_column_int(st, 6);
        const char *corp_tag = (const char *)sqlite3_column_text(st, 7);
        int asset_type   = sqlite3_column_int(st, 8);

        // Pack them into a JSON object
        json_t *entry = json_pack(
            "{s:i, s:i, s:i, s:i, s:i, s:s, s:i, s:s, s:i}", // Added one more s:i for asset_id
            "asset_id", asset_id, // NEW
            "sector_id", sector_id,
            "count", count,
            "offense_mode", offense_mode,
            "player_id", player_id,
            "player_name", player_name ? player_name : "Unknown",
            "corp_id", corp_id,
            "corp_tag", corp_tag ? corp_tag : "",
            "type", asset_type
        );

        if (entry) {
            json_array_append_new(entries, entry);
        }
    }

    // --- 5. Finalize Statement and Unlock Mutex ---
    sqlite3_finalize (st);
    pthread_mutex_unlock (&db_mutex);

    if (rc != SQLITE_DONE)
    {
        json_decref(entries); // Clean up on error
        send_enveloped_error (ctx->fd, root, 500, "Error processing asset list.");
        return 0;
    }

    // --- 6. Build Final Payload and Send Response ---
    json_t *jdata_payload = json_object ();
    json_object_set_new (jdata_payload, "total", json_integer (total_count));
    json_object_set_new (jdata_payload, "entries", entries); // Add the array

    send_enveloped_ok (ctx->fd, root, list_type, jdata_payload);

    return 0;
}




int
cmd_deploy_fighters_list (client_ctx_t *ctx, json_t *root)
{
const char *sql_query_fighters=
    "SELECT "
    "  sa.id AS asset_id, "                   /* NEW: Index 0 */
    "  sa.sector AS sector_id, "               /* Index 1 */
    "  sa.quantity AS count, "                /* Index 2 */
    "  sa.offensive_setting AS offense_mode, " /* Index 3 */
    "  sa.player AS player_id, "              /* Index 4 */
    "  p.name AS player_name, "               /* Index 5 */
    "  c.id AS corp_id, "                     /* Index 6 */
    "  c.tag AS corp_tag, "                   /* Index 7 */
    "  sa.asset_type AS type "                /* Index 8 */
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


int
cmd_deploy_mines_list (client_ctx_t *ctx, json_t *root)
{
const char *sql_query_mines =
    "SELECT "
    "  sa.id AS asset_id, "                   /* NEW: Index 0 */
    "  sa.sector AS sector_id, "               /* Index 1 */
    "  sa.quantity AS count, "                /* Index 2 */
    "  sa.offensive_setting AS offense_mode, " /* Index 3 */
    "  sa.player AS player_id, "              /* Index 4 */
    "  p.name AS player_name, "               /* Index 5 */
    "  c.id AS corp_id, "                     /* Index 6 */
    "  c.tag AS corp_tag, "                   /* Index 7 */
    "  sa.asset_type AS type "                /* Index 8 */
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
      return 0;
  
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
  	(db, "SELECT sector FROM ships WHERE id=?1;", -1, &st,
  	 NULL) != SQLITE_OK)
        {
  	char error_buffer[256]; 
  	snprintf(error_buffer, sizeof(error_buffer), 
  		 "Unable to resolve current sector - SELECT sector FROM ships WHERE id=%d;", 
  		 ship_id);
  	char *shperror = error_buffer;	
  	send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
  			    shperror);
        return 0;
      }
      sqlite3_bind_int (st, 1, ship_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
  	sector_id = sqlite3_column_int (st, 0);
        }
      sqlite3_finalize (st);
      if (sector_id <= 0) {
  	char error_buffer[256]; 
  	snprintf(error_buffer, sizeof(error_buffer), 
  		 "Unable to resolve current sector - SELECT sector_id FROM ships WHERE id=%d;", 
  		 ship_id);
  	char *scterror = error_buffer;	
        send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
  			    scterror);
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
    
    int asset_id = (int) sqlite3_last_insert_rowid(db); // Capture the newly created asset_id

    if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
      {
        if (errmsg)
  	sqlite3_free (errmsg);
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
      json_object_set_new (evt, "asset_id", json_integer (asset_id)); // Add asset_id to event
  
            (void) db_log_engine_event ((long long)time(NULL), "combat.fighters.deployed", NULL, ctx->player_id,
                                      sector_id, evt, NULL);    }
                                 
    /* Recompute total for response convenience */
    (void) sum_sector_fighters (db, sector_id, &sector_total);
  
    LOGI("DEBUG: cmd_combat_deploy_fighters - sector_id: %d, player_id: %d, amount: %d, offense: %d, sector_total: %d, asset_id: %d",
         sector_id, ctx->player_id, amount, offense, sector_total, asset_id);

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
    json_object_set_new (out, "asset_id", json_integer (asset_id)); // Add asset_id to response
  
    /* Envelope: echo id/meta from `root`, set type string for this result */
    send_enveloped_ok (ctx->fd, root, "combat.fighters.deployed", out);
    json_decref (out);
    return 0;
  }

/* ---------- combat.deploy_mines ---------- */

static const char *SQL_SECTOR_MINE_SUM =
  "SELECT COALESCE(SUM(quantity),0) "
  "FROM sector_assets " "WHERE sector=?1 AND asset_type IN (1, 4);"; // 1 for Armid, 4 for Limpet

static const char *SQL_SHIP_GET_MINE =
  "SELECT mines FROM ships WHERE id=?1;"; // Assuming 'mines' column for total mines

static const char *SQL_SHIP_DEC_MINE =
  "UPDATE ships SET mines=mines-?1 WHERE id=?2;";

static const char *SQL_ASSET_INSERT_MINES =
  "INSERT INTO sector_assets(sector, player, corporation, "
  "                          asset_type, quantity, offensive_setting, deployed_at) "
  "VALUES (?1, ?2, ?3, ?4, ?5, ?6, strftime('%s','now'));"; // ?4 for asset_type (1 or 4)

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

/* Debit ship fighters safely (returns SQLITE_TOOBIG if insufficient). */
static int
ship_consume_fighters (sqlite3 *db, int ship_id, int amount)
{
  sqlite3_stmt *st = NULL;

  int rc = sqlite3_prepare_v2 (db, SQL_SHIP_GET_FTR, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGI("ship_consume_fighters - SQL_SHIP_GET_FTR");
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
        LOGI("DEBUGGING:\nhave=%d ship_id=%d amount=%d \n%s", have, ship_id, amount,SQL_SHIP_GET_FTR);
        rc = SQLITE_ERROR;
      }
  
    sqlite3_finalize (st);
    if (rc != SQLITE_OK)
      return rc;
  
    if (have < amount)
      {
        LOGI("DEBUGGING SQLITE_TOOBIG have < amount");
        return SQLITE_TOOBIG;
      }
  
    rc = sqlite3_prepare_v2 (db, SQL_SHIP_DEC_FTR, -1, &st, NULL);
    if (rc != SQLITE_OK)
      {
        LOGI("%s",SQL_SHIP_DEC_FTR);
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

/* Debit ship mines safely (returns SQLITE_TOOBIG if insufficient). */
static int
ship_consume_mines (sqlite3 *db, int ship_id, int asset_type, int amount)
{
  sqlite3_stmt *st = NULL;

  const char *sql_get_mines = "SELECT mines FROM ships WHERE id=?1;";
  const char *sql_dec_mines = "UPDATE ships SET mines=mines-?1 WHERE id=?2;";

  int rc = sqlite3_prepare_v2 (db, sql_get_mines, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGI("ship_consume_mines - SQL_SHIP_GET_MINE prepare failed");
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
      LOGI("ship_consume_mines - SQL_SHIP_GET_MINE step failed or no row");
      rc = SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  if (rc != SQLITE_OK)
    return rc;

  if (have < amount)
    {
      LOGI("ship_consume_mines - Insufficient mines: have %d, requested %d", have, amount);
      return SQLITE_TOOBIG;
    }

  rc = sqlite3_prepare_v2 (db, sql_dec_mines, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGI("ship_consume_mines - SQL_SHIP_DEC_MINE prepare failed");
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
                     json_t *corp_id_json /* nullable */ ,
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
      LOGE("insert_sector_mines prepare failed: %s", sqlite3_errmsg(db));
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
      LOGE("insert_sector_mines step failed: %s (rc=%d)", sqlite3_errmsg(db), rc);
    }

  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
}
  
    
  

/* Insert a sector_assets row for fighters. */

/* Insert a sector_assets row for fighters. */
static int
insert_sector_fighters (sqlite3 *db,
                        int sector_id, int owner_player_id,
                        json_t *corp_id_json /* nullable */ ,
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
    LOGE("insert_sector_fighters prepare failed: %s", sqlite3_errmsg(db));
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
    LOGE("insert_sector_fighters step failed: %s (rc=%d)", sqlite3_errmsg(db), rc); 
  }

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

int
cmd_fighters_recall (client_ctx_t *ctx, json_t *root)
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
      send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
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
    "LEFT JOIN corp_members cm ON cm.player_id = ?1 "
    "WHERE s.id = ?2;";

  if (sqlite3_prepare_v2 (db, sql_player_ship, -1, &stmt_player_ship, NULL) != SQLITE_OK)
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
      player_corp_id = sqlite3_column_int (stmt_player_ship, 3); // 0 if NULL
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
      send_enveloped_refused (ctx->fd, root, REF_NOT_IN_SECTOR, "Not in sector",
			      json_pack ("{s:s}", "reason", "not_in_sector"));
      return 0;
    }

  /* 4. Fetch asset and validate existence */
  sqlite3_stmt *stmt_asset = NULL;
  const char *sql_asset =
    "SELECT quantity, player, corporation, offensive_setting "
    "FROM sector_assets "
    "WHERE id = ?1 AND sector = ?2 AND asset_type = 2;"; // asset_type = 2 for fighters

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
      send_enveloped_error (ctx->fd, root, ERR_SHIP_NOT_FOUND, "Asset not found");
      return 0;
    }

  /* 5. Validate ownership */
  bool is_owner = false;
  if (asset_owner_player_id == ctx->player_id)
    {
      is_owner = true; // Personal fighters
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

  take = (available_to_recall < capacity_left) ? available_to_recall : capacity_left;

  if (take <= 0) // Now this check makes sense
    {
      send_enveloped_refused (ctx->fd, root, ERR_OUT_OF_RANGE, "No fighters to recall or no capacity",
			      json_pack ("{s:s}", "reason", "no_fighters_or_capacity"));
      return 0;
    }

  /* 7. Apply changes (transaction) */
  char *errmsg = NULL;
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
	sqlite3_free (errmsg);
      send_enveloped_error (ctx->fd, root, ERR_DB,
			    "Could not start transaction");
      return 0;
    }

  /* Increment ship fighters */
  sqlite3_stmt *stmt_update_ship = NULL;
  const char *sql_update_ship =
    "UPDATE ships SET fighters = fighters + ?1 WHERE id = ?2;";
  if (sqlite3_prepare_v2 (db, sql_update_ship, -1, &stmt_update_ship, NULL) != SQLITE_OK)
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
      const char *sql_delete_asset = "DELETE FROM sector_assets WHERE id = ?1;";
      if (sqlite3_prepare_v2 (db, sql_delete_asset, -1, &stmt_delete_asset, NULL) != SQLITE_OK)
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
      if (sqlite3_prepare_v2 (db, sql_update_asset, -1, &stmt_update_asset, NULL) != SQLITE_OK)
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
	sqlite3_free (errmsg);
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
  json_object_set_new (evt, "remaining_in_sector", json_integer (asset_qty - take));

  const char *mode_str = "unknown";
  if (asset_offensive_setting == 1) mode_str = "offensive";
  else if (asset_offensive_setting == 2) mode_str = "defensive";
  else if (asset_offensive_setting == 3) mode_str = "toll";
  json_object_set_new (evt, "mode", json_string (mode_str));

    (void) db_log_engine_event ((long long)time(NULL), "fighters.recalled", NULL, ctx->player_id,
                                requested_sector_id, evt, NULL);
  /* 9. Send enveloped_ok response */
  json_t *out = json_object ();
  json_object_set_new (out, "sector_id", json_integer (requested_sector_id));
  json_object_set_new (out, "asset_id", json_integer (asset_id));
  json_object_set_new (out, "recalled", json_integer (take));
  json_object_set_new (out, "remaining_in_sector", json_integer (asset_qty - take));
  json_object_set_new (out, "mode", json_string (mode_str));

    send_enveloped_ok (ctx->fd, root, "combat.fighters.deployed", out);
    json_decref (out);
    return 0;
  }


/* ---------- combat.deploy_mines ---------- */
  int
  cmd_combat_deploy_mines (client_ctx_t *ctx, json_t *root)
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
    json_t *j_offense = json_object_get (data, "offense");	/* required: 1..3 */
    json_t *j_corp_id = json_object_get (data, "corporation_id");	/* optional, nullable */
    json_t *j_mine_type = json_object_get (data, "mine_type"); /* optional, 1 for Armid, 4 for Limpet */

    if (!j_amount || !json_is_integer (j_amount) ||
        !j_offense || !json_is_integer (j_offense))
      {
        send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
  			    "Missing required field or invalid type: amount/offense");
        return 0;
      }
  
    int amount = (int) json_integer_value (j_amount);
    int offense = (int) json_integer_value (j_offense);
    int mine_type = 1; // Default to Armid Mine

    if (j_mine_type && json_is_integer(j_mine_type)) {
        mine_type = (int) json_integer_value(j_mine_type);
        if (mine_type != 1 && mine_type != 4) { // Validate mine type
            send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
                                "Invalid mine_type. Must be 1 (Armid) or 4 (Limpet).");
            return 0;
        }
    }
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
  	(db, "SELECT sector FROM ships WHERE id=?1;", -1, &st,
  	 NULL) != SQLITE_OK)
        {
  	char error_buffer[256]; 
  	snprintf(error_buffer, sizeof(error_buffer), 
  		 "Unable to resolve current sector - SELECT sector FROM ships WHERE id=%d;", 
  		 ship_id);
  	char *shperror = error_buffer;	
  	send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
  			    shperror);
        return 0;
      }
      sqlite3_bind_int (st, 1, ship_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
  	sector_id = sqlite3_column_int (st, 0);
        }
      sqlite3_finalize (st);
      if (sector_id <= 0) {
  	char error_buffer[256]; 
  	snprintf(error_buffer, sizeof(error_buffer), 
  		 "Unable to resolve current sector - SELECT sector_id FROM ships WHERE id=%d;", 
  		 ship_id);
  	char *scterror = error_buffer;	
        send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
  			    scterror);
        return 0;
      }
    }
  
    /* Sector cap */
    int sector_total = 0;
    if (sum_sector_mines (db, sector_id, &sector_total) != SQLITE_OK) {
      send_enveloped_error (ctx->fd, root, REF_NOT_IN_SECTOR,
  			  "Failed to read sector mines");
      return 0;
    }
    if (sector_total + amount > SECTOR_MINE_CAP) { // Assuming SECTOR_MINE_CAP is defined
      send_enveloped_error (ctx->fd, root, ERR_SECTOR_OVERCROWDED,
  			  "Sector mine limit exceeded (50,000)");
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
      insert_sector_mines (db, sector_id, ctx->player_id, j_corp_id, mine_type, offense,
  			    amount);
    if (rc != SQLITE_OK)
      {
        sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
        send_enveloped_error (ctx->fd, root, SECTOR_ERR,
  			    "Failed to create sector assets record");
        return 0;
      }
    
    int asset_id = (int) sqlite3_last_insert_rowid(db); // Capture the newly created asset_id

    if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
      {
        if (errmsg)
  	sqlite3_free (errmsg);
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
  
        (void) h_send_message_to_player (ctx->player_id, 0,
  				       "Federation Warning",
  				       "Mine deployment in protected space has triggered ISS response.");
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
      json_object_set_new (evt, "mine_type", json_integer (mine_type));
      json_object_set_new (evt, "event_ts",
  			 json_integer ((json_int_t) time (NULL)));
      json_object_set_new (evt, "asset_id", json_integer (asset_id)); // Add asset_id to event
  
            (void) db_log_engine_event ((long long)time(NULL), "combat.mines.deployed", NULL, ctx->player_id,
                                      sector_id, evt, NULL);    }
                                 
    /* Recompute total for response convenience */
    (void) sum_sector_mines (db, sector_id, &sector_total);
  
    LOGI("DEBUG: cmd_combat_deploy_mines - sector_id: %d, player_id: %d, amount: %d, offense: %d, mine_type: %d, sector_total: %d, asset_id: %d",
         sector_id, ctx->player_id, amount, offense, mine_type, sector_total, asset_id);

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
    json_object_set_new (out, "mine_type", json_integer (mine_type));
    json_object_set_new (out, "sector_total_after",
  		       json_integer (sector_total));
    json_object_set_new (out, "asset_id", json_integer (asset_id)); // Add asset_id to response
  
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
      return 0;
  
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
    json_t *j_offense = json_object_get (data, "offense");	/* required: 1..3 */
    json_t *j_corp_id = json_object_get (data, "corporation_id");	/* optional, nullable */
    json_t *j_mine_type = json_object_get (data, "mine_type"); /* optional, 1 for Armid, 4 for Limpet */

    if (!j_amount || !json_is_integer (j_amount) ||
        !j_offense || !json_is_integer (j_offense))
      {
        send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
  			    "Missing required field or invalid type: amount/offense");
        return 0;
      }
  
    int amount = (int) json_integer_value (j_amount);
    int offense = (int) json_integer_value (j_offense);
    int mine_type = 1; // Default to Armid Mine

    if (j_mine_type && json_is_integer(j_mine_type)) {
        mine_type = (int) json_integer_value(j_mine_type);
        if (mine_type != 1 && mine_type != 4) { // Validate mine type
            send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
                                "Invalid mine_type. Must be 1 (Armid) or 4 (Limpet).");
            return 0;
        }
    }
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
  	(db, "SELECT sector FROM ships WHERE id=?1;", -1, &st,
  	 NULL) != SQLITE_OK)
        {
  	char error_buffer[256]; 
  	snprintf(error_buffer, sizeof(error_buffer), 
  		 "Unable to resolve current sector - SELECT sector FROM ships WHERE id=%d;", 
  		 ship_id);
  	char *shperror = error_buffer;	
  	send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
  			    shperror);
        return 0;
      }
      sqlite3_bind_int (st, 1, ship_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
  	sector_id = sqlite3_column_int (st, 0);
        }
      sqlite3_finalize (st);
      if (sector_id <= 0) {
  	char error_buffer[256]; 
  	snprintf(error_buffer, sizeof(error_buffer), 
  		 "Unable to resolve current sector - SELECT sector_id FROM ships WHERE id=%d;", 
  		 ship_id);
  	char *scterror = error_buffer;	
        send_enveloped_error (ctx->fd, root, ERR_SECTOR_NOT_FOUND,
  			    scterror);
        return 0;
      }
    }
  
    /* Sector cap */
    int sector_total = 0;
    if (sum_sector_mines (db, sector_id, &sector_total) != SQLITE_OK) {
      send_enveloped_error (ctx->fd, root, REF_NOT_IN_SECTOR,
  			  "Failed to read sector mines");
      return 0;
    }
    if (sector_total + amount > SECTOR_MINE_CAP) { // Assuming SECTOR_MINE_CAP is defined
      send_enveloped_error (ctx->fd, root, ERR_SECTOR_OVERCROWDED,
  			  "Sector mine limit exceeded (50,000)");
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
      insert_sector_mines (db, sector_id, ctx->player_id, j_corp_id, mine_type, offense,
  			    amount);
    if (rc != SQLITE_OK)
      {
        sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
        send_enveloped_error (ctx->fd, root, SECTOR_ERR,
  			    "Failed to create sector assets record");
        return 0;
      }
    
    int asset_id = (int) sqlite3_last_insert_rowid(db); // Capture the newly created asset_id

    if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
      {
        if (errmsg)
  	sqlite3_free (errmsg);
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
  
        (void) h_send_message_to_player (ctx->player_id, 0,
  				       "Federation Warning",
  				       "Mine deployment in protected space has triggered ISS response.");
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
      json_object_set_new (evt, "mine_type", json_integer (mine_type));
      json_object_set_new (evt, "event_ts",
  			 json_integer ((json_int_t) time (NULL)));
      json_object_set_new (evt, "asset_id", json_integer (asset_id)); // Add asset_id to event
  
            (void) db_log_engine_event ((long long)time(NULL), "combat.mines.deployed", NULL, ctx->player_id,
                                      sector_id, evt, NULL);    }
                                 
    /* Recompute total for response convenience */
    (void) sum_sector_mines (db, sector_id, &sector_total);
  
    LOGI("DEBUG: cmd_combat_deploy_mines - sector_id: %d, player_id: %d, amount: %d, offense: %d, mine_type: %d, sector_total: %d, asset_id: %d",
         sector_id, ctx->player_id, amount, offense, mine_type, sector_total, asset_id);

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
    json_object_set_new (out, "mine_type", json_integer (mine_type));
    json_object_set_new (out, "sector_total_after",
  		       json_integer (sector_total));
    json_object_set_new (out, "asset_id", json_integer (asset_id)); // Add asset_id to response
  
    /* Envelope: echo id/meta from `root`, set type string for this result */
    send_enveloped_ok (ctx->fd, root, "combat.mines.deployed", out);
    json_decref (out);
    return 0;
  }

int
cmd_mines_recall (client_ctx_t *ctx, json_t *root)
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
      send_enveloped_error (ctx->fd, root, ERR_CURSOR_INVALID,
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
    "JOIN shiptypes st ON s.type_id = st.id "
    "WHERE s.id = ?1;";

  if (sqlite3_prepare_v2 (db, sql_player_ship, -1, &stmt_player_ship, NULL) != SQLITE_OK)
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
    "WHERE id = ?1 AND player = ?2 AND sector = ?3 AND asset_type IN (1, 4);"; // Mines only

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
      send_enveloped_error (ctx->fd, root, ERR_NOT_FOUND,
			    "Mine asset not found or does not belong to you in this sector.");
      return 0;
    }

  /* 4. Check if ship has capacity for recalled mines */
  if (ship_mines_current + asset_quantity > ship_mines_max)
    {
      send_enveloped_refused (ctx->fd, root, REF_INSUFFICIENT_CAPACITY,
			      "Insufficient ship capacity to recall all mines.",
			      json_pack ("{s:s}", "reason", "insufficient_mine_capacity"));
      return 0;
    }

  /* 5. Transaction: delete asset, credit ship */
  char *errmsg = NULL;
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      if (errmsg)
	sqlite3_free (errmsg);
      send_enveloped_error (ctx->fd, root, ERR_DB,
			    "Could not start transaction");
      return 0;
    }

  // Delete the asset from sector_assets
  sqlite3_stmt *stmt_delete = NULL;
  const char *sql_delete = "DELETE FROM sector_assets WHERE id = ?1;";
  if (sqlite3_prepare_v2 (db, sql_delete, -1, &stmt_delete, NULL) != SQLITE_OK)
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
  const char *sql_credit = "UPDATE ships SET mines = mines + ?1 WHERE id = ?2;";
  if (sqlite3_prepare_v2 (db, sql_credit, -1, &stmt_credit, NULL) != SQLITE_OK)
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
	sqlite3_free (errmsg);
      send_enveloped_error (ctx->fd, root, ERR_DB, "Commit failed");
      return 0;
    }

  /* 6. Emit engine_event via h_log_engine_event */
  {
    json_t *evt = json_object ();
    json_object_set_new (evt, "sector_id", json_integer (requested_sector_id));
    json_object_set_new (evt, "player_id", json_integer (ctx->player_id));
    json_object_set_new (evt, "asset_id", json_integer (asset_id));
    json_object_set_new (evt, "amount", json_integer (asset_quantity));
    json_object_set_new (evt, "asset_type", json_integer (asset_type));
    json_object_set_new (evt, "event_ts",
			 json_integer ((json_int_t) time (NULL)));

    (void) db_log_engine_event ((long long)time(NULL), "mines.recalled", NULL, ctx->player_id,
                                requested_sector_id, evt, NULL);
  }

  /* 7. Send response */
  json_t *out = json_pack ("{\"s\":i, \"s\":i, \"s\":i, \"s\":i, \"s\":i}",
			   "sector_id", requested_sector_id,
			   "player_id", ctx->player_id,
			   "asset_id", asset_id,
			   "amount", asset_quantity,
			   "asset_type", asset_type);

  send_enveloped_ok (ctx->fd, root, "combat.mines.recalled", out);
  json_decref (out);
  return 0;
}
