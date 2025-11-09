#include <sqlite3.h>
#include <stdio.h>
#include <stdint.h>
#include <jansson.h>
#include <string.h>
#include <stdlib.h>		/* for strtol */
#include <sqlite3.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
// local includes
#include "server_players.h"
#include "database.h"		// play_login, user_create, db_player_info_json, db_player_get_sector, db_session_*
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_rules.h"
#include "db_player_settings.h"
#include "server_envelope.h"
#include "server_auth.h"
#include "server_envelope.h"
#include "server_log.h"
#include "common.h"
#include "db_player_settings.h"

extern sqlite3 *db_get_handle (void);

// extern void send_enveloped_refused(int fd, json_t *root_cmd, int code, const char *message, json_t *meta);


/**
 * @brief Maps a TurnConsumeResult enum to a client-facing string error message.
 * * @param result The TurnConsumeResult enum value.
 * @return const char* The corresponding error string.
 */
static const char *
get_turn_error_message (TurnConsumeResult result)
{
  switch (result)
    {
    case TURN_CONSUME_SUCCESS:
      return "Turn consumed successfully.";	// Should not be called on success
    case TURN_CONSUME_ERROR_DB_FAIL:
      return "Database failure prevented turn consumption. Please try again.";
    case TURN_CONSUME_ERROR_PLAYER_NOT_FOUND:
      return "Player entity not found in turn registry.";
    case TURN_CONSUME_ERROR_NO_TURNS:
      return "You have run out of turns and cannot perform this action.";
    default:
      return "An unknown error occurred during turn consumption.";
    }
}

/**
 * @brief Handles packaging and sending a TURN_CONSUME error back to the client.
 * * @param ctx The player context structure (assuming it holds player_id, fd, and root).
 * @param consume_result The TurnConsumeResult error code (must not be SUCCESS).
 * @param cmd The command that failed (e.g., "move.warp").
 * @param root The root JSON object of the command being processed.
 * @param meta_data Optional existing JSON metadata to include in the response.
 */
int
handle_turn_consumption_error (client_ctx_t *ctx,
			       TurnConsumeResult consume_result,
			       const char *cmd, json_t *root,
			       json_t *meta_data)
{

  // Convert the TurnConsumeResult enum into a string reason for the client
  const char *reason_str = NULL;
  switch (consume_result)
    {
      // We only expect to handle errors here
    case TURN_CONSUME_ERROR_DB_FAIL:
      reason_str = "db_failure";
      break;
    case TURN_CONSUME_ERROR_PLAYER_NOT_FOUND:
      reason_str = "player_not_found";
      break;
    case TURN_CONSUME_ERROR_NO_TURNS:
      reason_str = "no_turns_remaining";
      break;
    default:
      reason_str = "unknown_error";
      break;
    }

  // Prepare the metadata package for the client
  // We add the error code and the command that failed.
  json_t *meta = NULL;
  if (meta_data)
    {
      // Start with existing metadata if provided
      meta = json_copy (meta_data);
    }
  else
    {
      meta = json_object ();
    }

  // Add turn specific error details
  if (meta)
    {
      json_object_set_new (meta, "reason", json_string (reason_str));
      json_object_set_new (meta, "command", json_string (cmd));

      // Get the descriptive message for the user
      const char *user_message = get_turn_error_message (consume_result);

      // Send the refusal packet
      //      send_enveloped_refused (int fd, json_t *req, int code, const char *msg,
      //              json_t *data_opt)

      send_enveloped_refused (ctx->fd, root, ERR_REF_NO_TURNS, user_message,
			      NULL);

      json_decref (meta);
    }
  return 0;
}


/**
 * @brief Consumes one turn from a player's remaining turns count.
 *
 * This is the common function called by all turn-consuming actions (warp, attack, trade, etc.).
 * It decrements the 'turns_remaining' field in the 'turns' table for the specified player.
 *
 * @param db_conn The initialized SQLite database connection.
 * @param player_id The ID of the player whose turn should be consumed.
 * @param reason_cmd A string describing the command that consumed the turn (e.g., "move.warp").
 * @return TurnConsumeResult status code.
 */
TurnConsumeResult
h_consume_player_turn (sqlite3 *db_conn, client_ctx_t *ctx,
		       const char *reason_cmd)
{
  pthread_mutex_lock (&db_mutex); // Acquire lock

  sqlite3_stmt *stmt = NULL;
  int player_id = ctx->player_id;
  const char *sql_update =
    "UPDATE turns "
    "SET turns_remaining = turns_remaining - 1, "
    "    last_update = strftime('%s', 'now') "
    "WHERE player = ? AND turns_remaining > 0;";

  int rc;
  int changes;

  // 1. Prepare the SQL statement
  rc = sqlite3_prepare_v2 (db_conn, sql_update, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("DB Error (Prepare): %s\n", sqlite3_errmsg (db_conn));
      pthread_mutex_unlock (&db_mutex); // Release lock
      return TURN_CONSUME_ERROR_DB_FAIL;
    }

  // 2. Bind the player ID
  sqlite3_bind_int (stmt, 1, player_id);

  // 3. Execute the statement
  rc = sqlite3_step (stmt);

  if (rc != SQLITE_DONE)
    {
      // If the statement executed but returned an error state
      LOGE ("DB Error (Execute %s): %s\n", reason_cmd,
	    sqlite3_errmsg (db_conn));
      sqlite3_finalize (stmt);
      pthread_mutex_unlock (&db_mutex); // Release lock
      return TURN_CONSUME_ERROR_DB_FAIL;
    }

  // 4. Check how many rows were affected
  changes = sqlite3_changes (db_conn);

  // 5. Finalize the statement (release resources)
  sqlite3_finalize (stmt);

  if (changes == 0)
    {
      // We need a secondary check to see if the player has 0 turns, 
      // or if the player simply doesn't exist in the 'turns' table.

      // Secondary query to find the player's turn count
      const char *sql_select =
	"SELECT turns_remaining FROM turns WHERE player = ?;";

      rc = sqlite3_prepare_v2 (db_conn, sql_select, -1, &stmt, NULL);
      if (rc != SQLITE_OK)
	{
	  LOGE ("DB Error (Prepare Check): %s\n", sqlite3_errmsg (db_conn));
          pthread_mutex_unlock (&db_mutex); // Release lock
	  return TURN_CONSUME_ERROR_DB_FAIL;
	}

      sqlite3_bind_int (stmt, 1, player_id);

      rc = sqlite3_step (stmt);

      if (rc == SQLITE_ROW)
	{
	  // Player exists, but turns_remaining was 0 (or less, though that shouldn't happen)
	  sqlite3_finalize (stmt);
	  LOGE
	    ("Turn consumption failed for Player %d (%s): Turns remaining is 0.\n",
	     player_id, reason_cmd);
          pthread_mutex_unlock (&db_mutex); // Release lock
	  return TURN_CONSUME_ERROR_NO_TURNS;
	}
      else if (rc == SQLITE_DONE)
	{
	  // Player does not exist in the turns table
	  sqlite3_finalize (stmt);
	  LOGE
	    ("Turn consumption failed for Player %d (%s): Player not found in turns table.\n",
	     player_id, reason_cmd);
          pthread_mutex_unlock (&db_mutex); // Release lock
	  return TURN_CONSUME_ERROR_PLAYER_NOT_FOUND;
	}
      else
	{
	  // Some other select error
	  LOGE ("DB Error (Execute Check %s): %s\n", reason_cmd,
		sqlite3_errmsg (db_conn));
	  sqlite3_finalize (stmt);
          pthread_mutex_unlock (&db_mutex); // Release lock
	  return TURN_CONSUME_ERROR_DB_FAIL;
	}
    }

  // 6. Success
  LOGD
    ("Player %d consumed 1 turn for command: %s. New turn count updated.\n",
     player_id, reason_cmd);
  pthread_mutex_unlock (&db_mutex); // Release lock
  return TURN_CONSUME_SUCCESS;
}


/*
 * In server_players.c
 * This helper now *accepts* the db pointer.
 */
int
h_get_credits (sqlite3 *db, const char *owner_type, int owner_id, int *credits_out)
{
    sqlite3_stmt *st = NULL;
    int rc = SQLITE_ERROR;

    // Ensure a bank account exists for the owner (INSERT OR IGNORE)
    const char *SQL_ENSURE_ACCOUNT =
      "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) VALUES (?1, ?2, 'CRD', 0);";
    rc = sqlite3_prepare_v2(db, SQL_ENSURE_ACCOUNT, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("h_get_credits: ENSURE_ACCOUNT prepare error: %s", sqlite3_errmsg(db));
        return rc;
    }
    sqlite3_bind_text(st, 1, owner_type, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, owner_id);
    sqlite3_step(st); // Execute the insert or ignore
    sqlite3_finalize(st);
    st = NULL; // Reset statement pointer

    const char *SQL_SEL = "SELECT COALESCE(balance,0) FROM bank_accounts WHERE owner_type=?1 AND owner_id=?2";
    rc = sqlite3_prepare_v2(db, SQL_SEL, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("h_get_credits: SELECT prepare error: %s", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_text(st, 1, owner_type, -1, SQLITE_STATIC);
    sqlite3_bind_int (st, 2, owner_id);

    if (sqlite3_step(st) == SQLITE_ROW) {
        *credits_out = sqlite3_column_int(st, 0);
        rc = SQLITE_OK;
    } else {
        LOGE("h_get_credits: Owner type '%s', ID %d not found in bank_accounts.", owner_type, owner_id);
        rc = SQLITE_NOTFOUND;
    }
    sqlite3_finalize(st);
    return rc;
}



/* Low-level: compute free cargo = players.holds - SUM(ship_goods.quantity) */
int
h_get_cargo_space_free (sqlite3 *db, int player_id, int *free_out)
{

  if (!free_out)
    return SQLITE_MISUSE;

  int rc;
  sqlite3_stmt *st = NULL;

  int holds = 0;
  /* players.holds */
  rc =
    sqlite3_prepare_v2 (db,
			"SELECT COALESCE(holds,0) FROM players WHERE id=?1",
			-1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    holds = sqlite3_column_int (st, 0);
  else
    {
      holds = holds; // Explicitly use holds to satisfy the compiler
      sqlite3_finalize (st);
      return SQLITE_NOTFOUND;
    }
  sqlite3_finalize (st);

  /* total cargo qon ship (assumes ship_goods exists as designed earlier) */
  int total = 0;

  rc = sqlite3_prepare_v2 (db,
                           "SELECT (COALESCE(s.holds, 0) - "
                           "COALESCE(s.colonists + s.equipment + s.organics + s.ore, 0)) "
                           "FROM players p "
                           "JOIN ships s ON s.id = p.ship "
                           "WHERE p.id = ?1",
                           -1, &st, NULL);
  
  if (rc != SQLITE_OK)
    return rc;
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    total = sqlite3_column_int (st, 0);
  else
    {
      sqlite3_finalize (st);
      return SQLITE_ERROR;
    }
  sqlite3_finalize (st);

  int free_space = total;
  if (free_space < 0)
    free_space = 0;		/* guard – shouldn’t happen if updates enforce caps */

  *free_out = free_space;
  return SQLITE_OK;
}

/* server_players.c */
int
player_credits( client_ctx_t *ctx)
{
  if (!ctx || ctx->player_id <= 0) return 0;
  sqlite3 *db = db_get_handle();
  int c = 0;
  if (h_get_credits(db, "player", ctx->player_id, &c) != SQLITE_OK)
    return 0;
  return c;
}


int
cargo_space_free (client_ctx_t *ctx)
{
  int f = 0;
  sqlite3 *db = db_get_handle();
  if (h_get_cargo_space_free (db, ctx->player_id, &f) != SQLITE_OK) 
    return 0;
  return f;
}



#include <sqlite3.h>
#include <string.h>
#include <stdio.h> // For fprintf, stderr

/**
 * @brief Updates a player's ship cargo for one commodity by a delta (+/-).
 *
 * (Refactored to operate safely within an existing transaction)
 *
 * This function is refactored to match the schema:
 * 1. Finds the player's active ship ID (from players.ship).
 * 2. Dynamically builds an UPDATE ... RETURNING query for the correct column
 * (e.g., "UPDATE ships SET ore = MAX(0, COALESCE(ore, 0) + ?1) ... RETURNING ore").
 * 3. Relies on the 'check_current_cargo_limit' CHECK constraint on the 'ships'
 * table to automatically reject transactions that exceed total holds.
 *
 * @param db             Valid sqlite3 database handle (must be in a transaction).
 * @param player_id      The ID of the player.
 * @param commodity      The name of the commodity ("ore", "organics", "equipment", "colonists").
 * @param delta          The amount to add (positive) or remove (negative).
 * @param new_qty_out    (Optional) A pointer to an int to store the new total for this commodity.
 * @return               SQLITE_OK on success, SQLITE_CONSTRAINT if holds are exceeded,
 * or another SQLite error code on failure.
 */
int
h_update_ship_cargo (sqlite3 *db, int player_id,
                       const char *commodity, int delta, int *new_qty_out)
{
    if (!db || !commodity || *commodity == '\0')
        return SQLITE_MISUSE;

    int rc;
    sqlite3_stmt *stmt = NULL;
    char *sql_query = NULL;
    int ship_id = 0;

    // 1. Identify the column name from the commodity string
    const char *col_name = NULL;
    if (strcmp(commodity, "ore") == 0) col_name = "ore";
    else if (strcmp(commodity, "organics") == 0) col_name = "organics";
    else if (strcmp(commodity, "equipment") == 0) col_name = "equipment";
    else if (strcmp(commodity, "colonists") == 0) col_name = "colonists";
    else {
        fprintf(stderr, "h_update_ship_cargo: Invalid commodity name '%s'\n", commodity);
        return SQLITE_MISUSE; // Invalid commodity string
    }

    // 2. Get the player's active ship ID
    const char *SQL_GET_SHIP = "SELECT ship FROM players WHERE id = ?1";
    
    rc = sqlite3_prepare_v2(db, SQL_GET_SHIP, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "h_update_ship_cargo: Failed to prepare get_ship: %s\n", sqlite3_errmsg(db));
        return rc;
    }
    
    sqlite3_bind_int(stmt, 1, player_id);
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        ship_id = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    stmt = NULL;
    
    if (rc != SQLITE_ROW || ship_id <= 0) {
        // This means no player was found, or the player has no ship (ship_id is 0 or NULL)
        fprintf(stderr, "h_update_ship_cargo: Player %d has no active ship (id: %d).\n", player_id, ship_id);
        return SQLITE_ERROR; // No such player or no ship associated
    }

    // 3. Build and execute the dynamic UPDATE ... RETURNING
    // This query updates the value, clamps it at 0, and returns the new value.
    // The CHECK constraint on the 'ships' table will catch (col + ... > holds)
    sql_query = sqlite3_mprintf(
        "UPDATE ships SET %q = MAX(0, COALESCE(%q, 0) + ?1) WHERE id = ?2 "
        "RETURNING %q",
        col_name, col_name, col_name
    );

    if (!sql_query) {
        return SQLITE_NOMEM;
    }

    rc = sqlite3_prepare_v2(db, sql_query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "h_update_ship_cargo: Failed to prepare update: %s\n", sqlite3_errmsg(db));
        sqlite3_free(sql_query);
        return rc;
    }
    
    sqlite3_bind_int(stmt, 1, delta);
    sqlite3_bind_int(stmt, 2, ship_id);
    
    rc = sqlite3_step(stmt);
    
    // If the update was successful, rc will be SQLITE_ROW (due to RETURNING)
    if (rc == SQLITE_ROW) {
        // (Optional) Get the new quantity if requested
        if (new_qty_out) {
            *new_qty_out = sqlite3_column_int(stmt, 0);
        }
        rc = SQLITE_OK; // Set the final return code to success
    } 
    else if (rc == SQLITE_DONE) {
        // This would mean the UPDATE affected 0 rows, e.g., ship_id was not found
        // (which shouldn't happen if our SELECT above worked, but good to check)
        fprintf(stderr, "h_update_ship_cargo: Failed to find ship %d for update.\n", ship_id);
        rc = SQLITE_ERROR;
    }
    // If rc is any other value (like SQLITE_CONSTRAINT), we will just return that
    // error code, which is what we want.

    // 4. Clean up
    sqlite3_finalize(stmt);
    sqlite3_free(sql_query);
    
    return rc; // Will be SQLITE_OK or an error code (e.g., SQLITE_CONSTRAINT)
}

int
h_deduct_ship_credits (sqlite3 *db, int player_id, int amount,
		     int *new_balance)
{
  long long new_balance_ll = 0;
  int rc = h_deduct_credits (db, "player", player_id, amount, &new_balance_ll);
  if (rc == SQLITE_OK && new_balance) {
    *new_balance = (int)new_balance_ll;
  }
  return rc;
}

int
h_deduct_credits (sqlite3 *db, const char *owner_type, int owner_id, int amount,
		  long long *new_balance_out)
{
  char *errmsg = NULL;
  int rc = SQLITE_ERROR;
  long long current_balance = 0;

  // Create mutable copies of owner_type and owner_id for potential redirection
  const char *actual_owner_type = owner_type;
  int actual_owner_id = owner_id;

  if (!db || !owner_type || !*owner_type || owner_id <= 0 || amount <= 0)
    {
      LOGE ("h_deduct_credits: Invalid arguments. db=%p, owner_type=%s, owner_id=%d, amount=%d", db, owner_type, owner_id, amount);
      return SQLITE_MISUSE;
    }

  // --- Redirection Logic for Orion/Ferringhi Traders ---
  if (strcmp(owner_type, "player") == 0) {
      sqlite3_stmt *player_info_stmt = NULL;
      const char *sql_get_player_info = "SELECT type, name FROM players WHERE id = ?;";
      if (sqlite3_prepare_v2(db, sql_get_player_info, -1, &player_info_stmt, NULL) == SQLITE_OK) {
          sqlite3_bind_int(player_info_stmt, 1, owner_id);
          if (sqlite3_step(player_info_stmt) == SQLITE_ROW) {
              int player_type = sqlite3_column_int(player_info_stmt, 0);
              const char *player_name = (const char *)sqlite3_column_text(player_info_stmt, 1);

              // Check if it's an NPC player (type 1) and if name contains "Orion" or "Ferrengi"
              if (player_type == 1) { // Assuming type 1 is for NPC players
                  if (strstr(player_name, "Orion") != NULL) {
                      actual_owner_type = "npc_planet";
                      sqlite3_stmt *planet_id_stmt = NULL;
                      const char *sql_get_planet_id = "SELECT id FROM planets WHERE name='Orion Hideout';";
                      if (sqlite3_prepare_v2(db, sql_get_planet_id, -1, &planet_id_stmt, NULL) == SQLITE_OK) {
                          if (sqlite3_step(planet_id_stmt) == SQLITE_ROW) {
                              actual_owner_id = sqlite3_column_int(planet_id_stmt, 0);
                          }
                          sqlite3_finalize(planet_id_stmt);
                      }
                      LOGD("h_deduct_credits: Redirecting Orion player %d (%s) to npc_planet %d.", owner_id, player_name, actual_owner_id);
                  } else if (strstr(player_name, "Ferrengi") != NULL) {
                      actual_owner_type = "npc_planet";
                      sqlite3_stmt *planet_id_stmt = NULL;
                      const char *sql_get_planet_id = "SELECT id FROM planets WHERE name='Ferringhi Homeworld';";
                      if (sqlite3_prepare_v2(db, sql_get_planet_id, -1, &planet_id_stmt, NULL) == SQLITE_OK) {
                          if (sqlite3_step(planet_id_stmt) == SQLITE_ROW) {
                              actual_owner_id = sqlite3_column_int(planet_id_stmt, 0);
                          }
                          sqlite3_finalize(planet_id_stmt);
                      }
                      LOGD("h_deduct_credits: Redirecting Ferringhi player %d (%s) to npc_planet %d.", owner_id, player_name, actual_owner_id);
                  }
              }
          }
          sqlite3_finalize(player_info_stmt);
      }
  }
  // --- End Redirection Logic ---

  // Start transaction
  rc = sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_deduct_credits: Failed to start transaction: %s", errmsg ? errmsg : "unknown error");
      sqlite3_free (errmsg);
      return rc;
    }

  // 1. Get current balance
  sqlite3_stmt *select_stmt = NULL;
  const char *sql_select_balance =
    "SELECT balance FROM bank_accounts WHERE owner_type = ?1 AND owner_id = ?2;";
  rc = sqlite3_prepare_v2 (db, sql_select_balance, -1, &select_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_deduct_credits: SELECT prepare error: %s", sqlite3_errmsg (db));
      goto rollback_and_cleanup;
    }
  sqlite3_bind_text (select_stmt, 1, actual_owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (select_stmt, 2, actual_owner_id);

  if (sqlite3_step (select_stmt) == SQLITE_ROW)
    {
      current_balance = sqlite3_column_int64 (select_stmt, 0);
    }
  else
    {
      LOGE ("h_deduct_credits: Owner type '%s', ID %d not found in bank_accounts.", actual_owner_type, actual_owner_id);
      rc = SQLITE_NOTFOUND;
      goto rollback_and_cleanup;
    }
  sqlite3_finalize (select_stmt);
  select_stmt = NULL;

  // 2. Check for sufficient funds
  if (current_balance < amount)
    {
      LOGW ("h_deduct_credits: Insufficient funds for owner type '%s', ID %d. Current: %lld, Attempted deduct: %d", actual_owner_type, actual_owner_id, current_balance, amount);
      rc = SQLITE_CONSTRAINT;	// Or a custom error code for insufficient funds
      goto rollback_and_cleanup;
    }

  // 3. Update balance
  long long new_balance = current_balance - amount;
  sqlite3_stmt *update_stmt = NULL;
  const char *sql_update_balance =
    "UPDATE bank_accounts SET balance = ?1 WHERE owner_type = ?2 AND owner_id = ?3;";
  rc = sqlite3_prepare_v2 (db, sql_update_balance, -1, &update_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_deduct_credits: UPDATE prepare error: %s", sqlite3_errmsg (db));
      goto rollback_and_cleanup;
    }
  sqlite3_bind_int64 (update_stmt, 1, new_balance);
  sqlite3_bind_text (update_stmt, 2, actual_owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (update_stmt, 3, actual_owner_id);

  if (sqlite3_step (update_stmt) != SQLITE_DONE)
    {
      LOGE ("h_deduct_credits: UPDATE step error: %s", sqlite3_errmsg (db));
      goto rollback_and_cleanup;
    }
  sqlite3_finalize (update_stmt);
  update_stmt = NULL;

  // 4. Record transaction
  sqlite3_stmt *insert_tx_stmt = NULL;
  const char *sql_insert_tx =
    "INSERT INTO bank_tx (owner_type, owner_id, kind, amount, balance_after, currency, memo) "
    "VALUES (?1, ?2, 'withdraw', ?3, ?4, 'CRD', 'Withdrawal via h_deduct_credits');";
  rc = sqlite3_prepare_v2(db, sql_insert_tx, -1, &insert_tx_stmt, NULL);
  if (rc != SQLITE_OK) {
      LOGE("h_deduct_credits: INSERT_TX prepare error: %s", sqlite3_errmsg(db));
      goto rollback_and_cleanup;
  }
  sqlite3_bind_text(insert_tx_stmt, 1, actual_owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int(insert_tx_stmt, 2, actual_owner_id);
  sqlite3_bind_int(insert_tx_stmt, 3, amount);
  sqlite3_bind_int64(insert_tx_stmt, 4, new_balance);

  if (sqlite3_step(insert_tx_stmt) != SQLITE_DONE) {
      LOGE("h_deduct_credits: INSERT_TX step error: %s", sqlite3_errmsg(db));
      goto rollback_and_cleanup;
  }
  sqlite3_finalize(insert_tx_stmt);
  insert_tx_stmt = NULL;

  // Commit transaction
  rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_deduct_credits: Failed to commit transaction: %s", errmsg ? errmsg : "unknown error");
      sqlite3_free (errmsg);
      return rc;
    }

  if (new_balance_out)
    *new_balance_out = new_balance;

  return SQLITE_OK;

rollback_and_cleanup:
  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
  if (select_stmt)
    sqlite3_finalize (select_stmt);
  if (update_stmt)
    sqlite3_finalize (update_stmt);
  if (insert_tx_stmt)
    sqlite3_finalize (insert_tx_stmt);
  if (errmsg)
    sqlite3_free (errmsg);
  return rc;
}


int
h_add_credits (sqlite3 *db, const char *owner_type, int owner_id, int amount, long long *new_balance_out)
{
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;
  char *errmsg = NULL;
  long long current_balance = 0;

  // Create mutable copies of owner_type and owner_id for potential redirection
  const char *actual_owner_type = owner_type;
  int actual_owner_id = owner_id;

  if (!db || !owner_type || owner_id <= 0 || amount <= 0) {
    LOGE("h_add_credits: Invalid arguments. db=%p, owner_type=%s, owner_id=%d, amount=%d", db, owner_type, owner_id, amount);
    return SQLITE_MISUSE;
  }

  // --- Redirection Logic for Orion/Ferringhi Traders --- 
  if (strcmp(owner_type, "player") == 0) {
      sqlite3_stmt *player_info_stmt = NULL;
      const char *sql_get_player_info = "SELECT type, name FROM players WHERE id = ?;";
      if (sqlite3_prepare_v2(db, sql_get_player_info, -1, &player_info_stmt, NULL) == SQLITE_OK) {
          sqlite3_bind_int(player_info_stmt, 1, owner_id);
          if (sqlite3_step(player_info_stmt) == SQLITE_ROW) {
              int player_type = sqlite3_column_int(player_info_stmt, 0);
              const char *player_name = (const char *)sqlite3_column_text(player_info_stmt, 1);

              // Check if it's an NPC player (type 1) and if name contains "Orion" or "Ferrengi"
              if (player_type == 1) { // Assuming type 1 is for NPC players
                  if (strstr(player_name, "Orion") != NULL) {
                      actual_owner_type = "npc_planet";
                      sqlite3_stmt *planet_id_stmt = NULL;
                      const char *sql_get_planet_id = "SELECT id FROM planets WHERE name='Orion Hideout';";
                      if (sqlite3_prepare_v2(db, sql_get_planet_id, -1, &planet_id_stmt, NULL) == SQLITE_OK) {
                          if (sqlite3_step(planet_id_stmt) == SQLITE_ROW) {
                              actual_owner_id = sqlite3_column_int(planet_id_stmt, 0);
                          }
                          sqlite3_finalize(planet_id_stmt);
                      }
                      LOGD("h_add_credits: Redirecting Orion player %d (%s) to npc_planet %d.", owner_id, player_name, actual_owner_id);
                  } else if (strstr(player_name, "Ferrengi") != NULL) {
                      actual_owner_type = "npc_planet";
                      sqlite3_stmt *planet_id_stmt = NULL;
                      const char *sql_get_planet_id = "SELECT id FROM planets WHERE name='Ferringhi Homeworld';";
                      if (sqlite3_prepare_v2(db, sql_get_planet_id, -1, &planet_id_stmt, NULL) == SQLITE_OK) {
                          if (sqlite3_step(planet_id_stmt) == SQLITE_ROW) {
                              actual_owner_id = sqlite3_column_int(planet_id_stmt, 0);
                          }
                          sqlite3_finalize(planet_id_stmt);
                      }
                      LOGD("h_add_credits: Redirecting Ferringhi player %d (%s) to npc_planet %d.", owner_id, player_name, actual_owner_id);
                  }
              }
          }
          sqlite3_finalize(player_info_stmt);
      }
  }
  // --- End Redirection Logic ---

  // Start transaction
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK) {
    LOGE("h_add_credits: Failed to start transaction: %s", errmsg ? errmsg : "unknown error");
    if (errmsg) sqlite3_free (errmsg);
    return SQLITE_ERROR;
  }

  // Ensure a bank account exists for the owner (INSERT OR IGNORE)
  const char *SQL_ENSURE_ACCOUNT =
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) VALUES (?1, ?2, 'CRD', 0);";
  rc = sqlite3_prepare_v2(db, SQL_ENSURE_ACCOUNT, -1, &st, NULL);
  if (rc != SQLITE_OK) {
    LOGE("h_add_credits: ENSURE_ACCOUNT prepare error: %s", sqlite3_errmsg(db));
    goto rollback_and_exit;
  }
  sqlite3_bind_text(st, 1, actual_owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int(st, 2, actual_owner_id);
  sqlite3_step(st); // Execute the insert or ignore
  sqlite3_finalize(st);
  st = NULL;

  // Update bank_accounts.balance and get new balance
  const char *SQL_UPDATE_BANK =
    "UPDATE bank_accounts SET balance = balance + ?3 WHERE owner_type = ?1 AND owner_id = ?2 RETURNING balance;";
  rc = sqlite3_prepare_v2 (db, SQL_UPDATE_BANK, -1, &st, NULL);
  if (rc != SQLITE_OK) {
    LOGE("h_add_credits: UPDATE_BANK prepare error: %s", sqlite3_errmsg(db));
    goto rollback_and_exit;
  }
  sqlite3_bind_text (st, 1, actual_owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, actual_owner_id);
  sqlite3_bind_int64 (st, 3, amount);

  if (sqlite3_step (st) == SQLITE_ROW) {
    current_balance = sqlite3_column_int64 (st, 0);
    rc = SQLITE_OK;
  } else {
    LOGE("h_add_credits: UPDATE_BANK step error: %s", sqlite3_errmsg(db));
    rc = SQLITE_ERROR; // Owner not found or update failed
    goto rollback_and_exit;
  }
  sqlite3_finalize (st);
  st = NULL;

  // Log bank_tx entry
  const char *SQL_INSERT_TX =
    "INSERT INTO bank_tx (owner_type, owner_id, kind, amount, balance_after, currency, memo) "
    "VALUES (?1, ?2, 'deposit', ?3, ?4, 'CRD', 'Deposit via h_add_credits');";
  rc = sqlite3_prepare_v2(db, SQL_INSERT_TX, -1, &st, NULL);
  if (rc != SQLITE_OK) {
    LOGE("h_add_credits: INSERT_TX prepare error: %s", sqlite3_errmsg(db));
    goto rollback_and_exit;
  }
  sqlite3_bind_text(st, 1, actual_owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int(st, 2, actual_owner_id);
  sqlite3_bind_int64(st, 3, amount);
  sqlite3_bind_int64(st, 4, current_balance);
  sqlite3_step(st);
  sqlite3_finalize(st);
  st = NULL;

  // Commit transaction
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK) {
    LOGE("h_add_credits: Failed to commit transaction: %s", errmsg ? errmsg : "unknown error");
    if (errmsg) sqlite3_free (errmsg);
    return SQLITE_ERROR;
  }

  if (new_balance_out) {
    *new_balance_out = current_balance;
  }
  return SQLITE_OK;

rollback_and_exit:
  sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
  if (st) sqlite3_finalize(st);
  return rc;
}


/*
 * Returns the current sector for the given player.
 * - On success: sector id (>=1) or 0 if sector is NULL/0 (i.e., “in_ship”).
 * - On not found or any error: 0.
 *
 * Schema fields used: players(id, sector)
 * See also: player_locations view maps NULL/0 sector → "in_ship".
 */
int
h_get_player_sector (int player_id)
{
  sqlite3 *db = db_get_handle ();
  static const char *SQL =
    "SELECT COALESCE(sector, 0) FROM players WHERE id = ?1";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2 (db, SQL, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      /* optional: fprintf(stderr, "prepare failed: %s\n", sqlite3_errmsg(db)); */
      return 0;
    }

  rc = sqlite3_bind_int (stmt, 1, player_id);
  if (rc != SQLITE_OK)
    {
      /* optional: fprintf(stderr, "bind failed: %s\n", sqlite3_errmsg(db)); */
      sqlite3_finalize (stmt);
      return 0;
    }

  rc = sqlite3_step (stmt);
  int sector = 0;

  if (rc == SQLITE_ROW)
    {
      /* COALESCE guarantees non-NULL; still guard defensively */
      sector = sqlite3_column_int (stmt, 0);
      if (sector < 0)
	sector = 0;
    }
  else
    {
      /* optional: fprintf(stderr, "no row for player_id=%d\n", player_id); */
      sector = 0;
    }

  sqlite3_finalize (stmt);
  return sector;
}




/** * Sends a complex mail message to a specific player, including a sender ID and subject.
 * The 'mail' table is assumed to now include sender_id, subject, and a 'read' status.
 *
 * @param player_id The ID of the player to receive the message (the recipient). 
 * @param sender_id The ID of the message sender (use 0 or a specific system ID for system messages).
 * @param subject The subject line of the mail message.
 * @param message The text content of the message body. 
 * @return 0 on success, or non-zero on error. 
 */
int
h_send_message_to_player (int player_id, int sender_id, const char *subject,
			  const char *message)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  int rc;

  // Use current Unix timestamp for the message time 
//   int timestamp = (int) time (NULL);

  // Updated SQL statement to insert recipient_id (player_id), sender_id, timestamp, 
  // subject, message, and set 'read' status to 0 (unread).
  const char *sql =
    "INSERT INTO mail (sender_id, recipient_id, subject, body) "
    "VALUES (?, ?, ?, ?);";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);

  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error preparing complex message insert: %s\n",
	    sqlite3_errmsg (db));
      return 1;
    }

  // Bind parameters
  // 1: recipient_id (player_id)
  sqlite3_bind_int (st, 1, player_id);
  // 2: sender_id
  sqlite3_bind_int (st, 2, sender_id);
  // 3: timestamp
  sqlite3_bind_text (st, 3, subject, -1, SQLITE_TRANSIENT);
  // 5: message (using SQLITE_TRANSIENT)
  sqlite3_bind_text (st, 4, message, -1, SQLITE_TRANSIENT);

  // Execute the statement 
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE ("SQL error executing complex message insert for player %d: %s\n",
	    player_id, sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return 1;
    }

  sqlite3_finalize (st);

  LOGD ("Complex message sent to player %d from sender %d. Subject: '%s'\n",
	player_id, sender_id, subject);

  return 0;
}

/**
 * @brief Retrieves the ID of the active ship for a given player.
 * * Queries the 'players' table using the player_id to get the value
 * from the 'ship' column.
 * * @param db The SQLite database handle.
 * @param player_id The ID of the player whose ship is being sought.
 * @return The ship ID (int) on success, or 0 if player is not found 
 * or the ship column is NULL/0.
 */
int
h_get_active_ship_id (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int ship_id = 0;
  int rc;

  // SQL: Select the 'ship' column from 'players' where the 'id' matches the player_id.
  // The player's active ship ID is stored in the 'ship' column.
  const char *sql = "SELECT ship FROM players WHERE id = ?;";

  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);

  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "SQL error preparing ship lookup: %s\n",
	       sqlite3_errmsg (db));
      return 0;			// Return 0 on error
    }

  sqlite3_bind_int (st, 1, player_id);

  // Execute the query
  rc = sqlite3_step (st);

  if (rc == SQLITE_ROW)
    {
      // The 'ship' column is at index 0. If it's NULL or 0, ship_id remains 0.
      ship_id = sqlite3_column_int (st, 0);
    }
  else if (rc != SQLITE_DONE)
    {
      // Handle error if step was not successful and not just finished.
      fprintf (stderr, "SQL error executing ship lookup for player %d: %s\n",
	       player_id, sqlite3_errmsg (db));
    }

  sqlite3_finalize (st);

  // ship_id will be > 0 if a valid ship was found, otherwise 0.
  return ship_id;
}


int
h_decloak_ship (sqlite3 *db, int ship_id)
{
  sqlite3_stmt *st = NULL;
  sqlite3_stmt *st_select = NULL;	// Keep this distinct from 'st'
  int rc = 0;
  int player_id = 0;

  // --- Step 1: Get the Player ID for the Ship (Primary Owner) ---
  const char *sql_select_owner =
    "SELECT player_id FROM ship_ownership WHERE ship_id = ? AND is_primary = 1;";

  rc = sqlite3_prepare_v2 (db, sql_select_owner, -1, &st_select, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_int (st_select, 1, ship_id);	// Use the function argument 'ship_id'

  if (sqlite3_step (st_select) == SQLITE_ROW)
    {
      player_id = sqlite3_column_int (st_select, 0);
    }
  // No need to check for SQLITE_DONE/ERROR explicitly here; we continue even if no owner is found.
  sqlite3_finalize (st_select);
  st_select = NULL;


  // --- Step 2: Update the Ships table (De-cloak) ---
  const char *sql_update_cloak =
    "UPDATE ships "
    "SET cloaked = NULL " "WHERE id = ? AND cloaked IS NOT NULL;";

  rc = sqlite3_prepare_v2 (db, sql_update_cloak, -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto cleanup;

  sqlite3_bind_int (st, 1, ship_id);

  // Execute the UPDATE statement. It returns SQLITE_DONE on success.
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      // If the UPDATE failed (e.g., integrity constraint, I/O error), jump to cleanup
      rc = sqlite3_step (st);	// Store the error code
      goto cleanup;
    }

  // Check if any row was affected (i.e., the ship was successfully de-cloaked)
  if (sqlite3_changes (db) > 0)
    {
      // 3. Send the notification to the player (if an owner was found)
      if (player_id > 0)
	{
	  // Use the identified message function
	  h_send_message_to_player (player_id, 1, "Uncloaking",
				    "Your ship's cloaking device has been deactivated due to action.");
	}
    }

  rc = SQLITE_OK;		// Set success status if we reached here

cleanup:
  if (st_select)
    sqlite3_finalize (st_select);
  if (st)
    sqlite3_finalize (st);

  // Returning 0 on success is standard for C functions where an explicit error code is not necessary.
  // We return 0 on success (SQLITE_OK) or the SQLite error code.
  return (rc == SQLITE_OK) ? 0 : rc;
}



enum
{ MAX_BOOKMARKS = 64, MAX_BM_NAME = 64 };
enum
{ MAX_AVOIDS = 64 };


/* ---------- forward decls for settings section builders (stubs for now) ---------- */
// static json_t *players_build_settings (client_ctx_t * ctx, json_t * req);
// static json_t *players_get_prefs (client_ctx_t * ctx);
static json_t *players_get_subscriptions (client_ctx_t * ctx);
static json_t *players_list_bookmarks (client_ctx_t * ctx);
static json_t *players_list_avoid (client_ctx_t * ctx);
static json_t *players_list_notes (client_ctx_t * ctx, json_t * req);


static int
is_ascii_printable (const char *s)
{
  if (!s)
    return 0;
  for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    if (*p < 0x20 || *p > 0x7E)
      return 0;
  return 1;
}

static int
len_leq (const char *s, size_t m)
{
  return s && strlen (s) <= m;
}


static int
is_valid_key (const char *s, size_t max)
{
  if (!s)
    return 0;
  size_t n = strlen (s);
  if (n == 0 || n > max)
    return 0;
  for (size_t i = 0; i < n; ++i)
    {
      char c = s[i];
      if (!
	  ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.'
	   || c == '_' || c == '-'))
	return 0;
    }
  return 1;
}


int
cmd_player_my_info (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  json_t *pinfo = NULL;
  int prc = db_player_info_json (ctx->player_id, &pinfo);
  if (prc != SQLITE_OK || !pinfo)
    {
      send_enveloped_error (ctx->fd, root, 1503, "Database error");
      return 0;
    }
  // Ensure data.player.turns_remaining is present for tests & clients
  int tr = 0;
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db, "SELECT turns_remaining FROM turns WHERE player=?;", -1, &st,
       NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, ctx->player_id);
      if (sqlite3_step (st) == SQLITE_ROW)
	tr = sqlite3_column_int (st, 0);
      sqlite3_finalize (st);
    }
  json_t *player = json_object_get (pinfo, "player");
  if (!player || !json_is_object (player))
    {
      player = json_object ();	// be defensive
      json_object_set_new (pinfo, "player", player);
    }
  json_object_set_new (player, "turns_remaining", json_integer (tr));


  send_enveloped_ok (ctx->fd, root, "player.info", pinfo);
  json_decref (pinfo);
  return 0;
}

int
cmd_player_list_online (client_ctx_t *ctx, json_t *root)
{
  /* Until a global connection registry exists, return current player only. */
  json_t *arr = json_array ();
  if (ctx->player_id > 0)
    {
      json_array_append_new (arr,
			     json_pack ("{s:i}", "player_id",
					ctx->player_id));
    }
  json_t *data = json_pack ("{s:o}", "players", arr);
  send_enveloped_ok (ctx->fd, root, "player.list_online", data);
  json_decref (data);
  return 0;
}

/* ==================================================================== */
/*                         SETTINGS AGGREGATE                            */
/* ==================================================================== */

int
cmd_player_set_settings (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_settings");
}



/* ==================================================================== */
/*                             PREFS                                     */
/* ==================================================================== */


int
cmd_player_get_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  sqlite3_stmt *it = NULL;
  if (db_prefs_get_all (ctx->player_id, &it) != 0)
    {
      send_enveloped_error (ctx->fd, root, 1503, "Database error");
      return 0;
    }

  json_t *prefs = json_array ();
  while (sqlite3_step (it) == SQLITE_ROW)
    {
      const char *k = (const char *) sqlite3_column_text (it, 0);
      const char *t = (const char *) sqlite3_column_text (it, 1);
      const char *v = (const char *) sqlite3_column_text (it, 2);
      json_t *o = json_object ();
      json_object_set_new (o, "key", json_string (k ? k : ""));
      json_object_set_new (o, "type", json_string (t ? t : "string"));
      json_object_set_new (o, "value", json_string (v ? v : ""));
      json_array_append_new (prefs, o);
    }
  sqlite3_finalize (it);

  json_t *data = json_object ();
  json_object_set_new (data, "prefs", prefs);
  send_enveloped_ok (ctx->fd, root, "player.prefs_v1", data);
  return 0;
}



/* ==================================================================== */
/*                           SUBSCRIPTIONS                               */
/* ==================================================================== */

int
cmd_player_set_topics (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_topics");
}

int
cmd_player_get_topics (client_ctx_t *ctx, json_t *root)
{
  json_t *topics = players_get_subscriptions (ctx);
  if (!topics)
    send_enveloped_error (ctx->fd, root, 500, "subs_load_failed");

  json_t *out = json_object ();
  json_object_set_new (out, "topics", topics);
  send_enveloped_ok (ctx->fd, root, "player.subscriptions", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                           BOOKMARKS                                   */
/* ==================================================================== */

int
cmd_player_set_bookmarks (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_bookmarks");
}

int
cmd_player_get_bookmarks (client_ctx_t *ctx, json_t *root)
{
  json_t *bookmarks = players_list_bookmarks (ctx);
  if (!bookmarks)
    send_enveloped_error (ctx->fd, root, 500, "bookmarks_load_failed");
  json_t *out = json_object ();
  json_object_set_new (out, "bookmarks", bookmarks);
  send_enveloped_ok (ctx->fd, root, "player.bookmarks", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                              AVOIDS                                   */
/* ==================================================================== */

int
cmd_player_set_avoids (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_avoids");
}

int
cmd_player_get_avoids (client_ctx_t *ctx, json_t *root)
{
  json_t *avoid = players_list_avoid (ctx);
  if (!avoid)
    send_enveloped_error (ctx->fd, root, 500, "avoid_load_failed");

  json_t *out = json_object ();
  json_object_set_new (out, "avoid", avoid);
  send_enveloped_ok (ctx->fd, root, "avoids", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                               NOTES                                   */
/* ==================================================================== */

int
cmd_player_get_notes (client_ctx_t *ctx, json_t *root)
{
  json_t *notes = players_list_notes (ctx, root);	/* supports {"scope":...,"key":...} */
  if (!notes)
    send_enveloped_error (ctx->fd, root, 500, "notes_load_failed");

  json_t *out = json_object ();
  json_object_set_new (out, "notes", notes);
  send_enveloped_ok (ctx->fd, root, "player.notes", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                       SECTION BUILDERS (STUBS)                        */
/* ==================================================================== */

/*
static int
_include_wanted (json_t *data, const char *key)
{
  json_t *inc = data ? json_object_get (data, "include") : NULL;
  if (!inc || !json_is_array (inc))
    return 1;			// no filter → include all
  size_t i, n = json_array_size (inc);
  for (i = 0; i < n; i++)
    {
      const char *s = json_string_value (json_array_get (inc, i));
      if (s && 0 == strcmp (s, key))
	return 1;
    }
  return 0;
}
*/

/*
static json_t *
players_build_settings (client_ctx_t *ctx, json_t *req)
{
  json_t *out = json_object ();

  if (_include_wanted (req, "prefs"))
    {
      json_t *prefs = players_get_prefs (ctx);
      if (!prefs)
	prefs = json_object ();
      json_object_set_new (out, "prefs", prefs);
    }
  if (_include_wanted (req, "subscriptions"))
    {
      json_t *subs = players_get_subscriptions (ctx);
      if (!subs)
	subs = json_array ();
      json_object_set_new (out, "subscriptions", subs);
    }
  if (_include_wanted (req, "bookmarks"))
    {
      json_t *bm = players_list_bookmarks (ctx);
      if (!bm)
	bm = json_array ();
      json_object_set_new (out, "bookmarks", bm);
    }
  if (_include_wanted (req, "avoid"))
    {
      json_t *av = players_list_avoid (ctx);
      if (!av)
	av = json_array ();
      json_object_set_new (out, "avoid", av);
    }
  if (_include_wanted (req, "notes"))
    {
      json_t *nt = players_list_notes (ctx, req);
      if (!nt)
	nt = json_array ();
      json_object_set_new (out, "notes", nt);
    }
  return out;
}
*/

/* Replace these stubs with DB-backed implementations as you land #189+ */
/*
static json_t *
players_get_prefs (client_ctx_t *ctx)
{
  (void) ctx;
  json_t *prefs = json_object ();
  json_object_set_new (prefs, "ui.ansi", json_true ());
  json_object_set_new (prefs, "ui.clock_24h", json_true ());
  json_object_set_new (prefs, "ui.locale", json_string ("en-GB"));
  json_object_set_new (prefs, "ui.page_length", json_integer (20));
  json_object_set_new (prefs, "privacy.dm_allowed", json_true ());
  return prefs;
}
*/

static json_t *
players_get_subscriptions (client_ctx_t *ctx)
{
  (void) ctx;
  json_t *arr = json_array ();
  json_t *a = json_object ();
  json_object_set_new (a, "topic", json_string ("system.notice"));
  json_object_set_new (a, "locked", json_true ());
  json_array_append_new (arr, a);
  json_t *b = json_object ();
  json_object_set_new (b, "topic", json_string ("sector.*"));
  json_object_set_new (b, "locked", json_false ());
  json_array_append_new (arr, b);
  return arr;
}

static json_t *
players_list_bookmarks (client_ctx_t *ctx)
{
  (void) ctx;
  return json_array ();
}

static json_t *
players_list_avoid (client_ctx_t *ctx)
{
  (void) ctx;
  return json_array ();
}

static json_t *
players_list_notes (client_ctx_t *ctx, json_t *req)
{
  (void) ctx;
  (void) req;
  return json_array ();
}

/* --- local helpers for type mapping/validation --- */
/*
static int
map_pt (const char *s)
{
  if (!s)
    return PT_STRING;
  if (strcmp (s, "bool") == 0)
    return PT_BOOL;
  if (strcmp (s, "int") == 0)
    return PT_INT;
  if (strcmp (s, "json") == 0)
    return PT_JSON;
  return PT_STRING;
}
*/

/*
static int
validate_value (int pt, const char *v)
{
  if (!v)
    return 0;
  char *end = NULL;
  switch (pt)
    {
    case PT_BOOL:
      return (!strcmp (v, "true") || !strcmp (v, "false") || !strcmp (v, "0")
	      || !strcmp (v, "1"));
    case PT_INT:
      strtol (v, &end, 10);
      return (end && *end == '\0');
    case PT_JSON:
      return v[0] == '{' || v[0] == '[';	// lightweight check
    case PT_STRING:
    default:
      return 1;
    }
}
*/


/* ------ Set Pref ------- */

/* --- allow ui.locale (simple BCP47-ish) --- */
static int
is_valid_locale (const char *s)
{
  if (!s)
    return 0;
  size_t n = strlen (s);
  if (n < 2 || n > 16)
    return 0;
  for (size_t i = 0; i < n; i++)
    {
      char c = s[i];
      if (!
	  ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
	   || (c >= '0' && c <= '9') || c == '-'))
	return 0;
    }
  return 1;
}




int
cmd_player_set_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return -1;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return -1;
    }

  /* Support both: {data:{patch:{...}}} OR {data:{...}} */
  json_t *patch = json_object_get (data, "patch");
  json_t *prefs = json_is_object (patch) ? patch : data;

  void *it = json_object_iter (prefs);
  while (it)
    {
      const char *key = json_object_iter_key (it);
      json_t *val = json_object_iter_value (it);

      if (!is_valid_key (key, 64))
	{
	  send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
				"invalid key");
	  return -1;
	}

      // pref_type t; // Declare without initialization
      char buf[512] = { 0 };

      if (json_is_string (val))
	{
	  const char *s = json_string_value (val);
	  if (!is_ascii_printable (s) || strlen (s) > 256)
	    {
	      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
				    "string too long/not printable");
	      return -1;
	    }
	  // t = PT_STRING; // Assign here
	  snprintf (buf, sizeof (buf), "%s", s);
	}
      else if (json_is_integer (val))
	{
	  // t = PT_INT; // Assign here
	  snprintf (buf, sizeof (buf), "%lld",
		    (long long) json_integer_value (val));
	}
      else if (json_is_boolean (val))
	{
	  // t = PT_BOOL; // Assign here
	  snprintf (buf, sizeof (buf), "%s", json_is_true (val) ? "1" : "0");
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
				"unsupported value type");
	  return -1;
	}

      // if (db_prefs_set_one (ctx->player_id, key, t, buf) != 0)
      if (db_prefs_set_one
	  (ctx->player_id, "ui.locale", PT_STRING,
	   json_string_value (val)) != 0)
	{
	  send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
	  return -1;
	}

      if (strcmp (key, "ui.locale") == 0)
	{
	  if (!json_is_string (val)
	      || !is_valid_locale (json_string_value (val)))
	    {
	      send_enveloped_error (ctx->fd, root, ERR_BAD_REQUEST,
				    "invalid ui.locale");
	      return -1;
	    }
	  /* falls through to db_prefs_set_one with type='string' */
	}


      it = json_object_iter_next (prefs, it);
    }

  json_t *resp = json_pack ("{s:b}", "ok", 1);
  send_enveloped_ok (ctx->fd, root, "player.prefs.updated", resp);
  json_decref (resp);
  return 0;
}



/* ---------- nav.bookmark.* ---------- */



void
cmd_nav_bookmark_add (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return;
    }

  json_t *v = json_object_get (data, "name");
  if (!json_is_string (v))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
			    "missing field: name");
      return;
    }
  const char *name = json_string_value (v);
  if (!is_ascii_printable (name) || !len_leq (name, MAX_BM_NAME))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG, "invalid name");
      return;
    }

  v = json_object_get (data, "sector_id");
  if (!json_is_integer (v) || json_integer_value (v) <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
			    "invalid sector_id");
      return;
    }
  int64_t sector_id = json_integer_value (v);

  /* Cap check */
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db, "SELECT COUNT(*) FROM player_bookmarks WHERE player_id=?1;", -1,
       &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);
  int have = 0;
  if (sqlite3_step (st) == SQLITE_ROW)
    have = sqlite3_column_int (st, 0);
  sqlite3_finalize (st);
  if (have >= MAX_BOOKMARKS)
    {
      json_t *meta =
	json_pack ("{s:i,s:i}", "max", MAX_BOOKMARKS, "have", have);
      send_enveloped_refused (ctx->fd, root, ERR_LIMIT_EXCEEDED,
			      "too many bookmarks", meta);
      return;
    }

  int rc = db_bookmark_upsert (ctx->player_id, name, sector_id);
  if (rc != 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }

  json_t *resp =
    json_pack ("{s:s,s:i}", "name", name, "sector_id", (int) sector_id);
  send_enveloped_ok (ctx->fd, root, "nav.bookmark.added", resp);
  json_decref (resp);
}

void
cmd_nav_bookmark_remove (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return;
    }

  json_t *v = json_object_get (data, "name");
  if (!json_is_string (v))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
			    "missing field: name");
      return;
    }
  const char *name = json_string_value (v);
  if (!is_ascii_printable (name) || !len_leq (name, MAX_BM_NAME))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG, "invalid name");
      return;
    }

  int rc = db_bookmark_remove (ctx->player_id, name);
  if (rc != 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_FOUND,
			    "bookmark not found");
      return;
    }

  json_t *resp = json_pack ("{s:s}", "name", name);
  send_enveloped_ok (ctx->fd, root, "nav.bookmark.removed", resp);
  json_decref (resp);
}

void
cmd_nav_bookmark_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db,
       "SELECT name, sector_id FROM player_bookmarks WHERE player_id=?1 ORDER BY updated_at DESC,name;",
       -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);

  json_t *items = json_array ();
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      const char *name = (const char *) sqlite3_column_text (st, 0);
      int sector_id = sqlite3_column_int (st, 1);
      json_array_append_new (items,
			     json_pack ("{s:s,s:i}", "name", name ? name : "",
					"sector_id", sector_id));
    }
  sqlite3_finalize (st);

  json_t *resp = json_pack ("{s:O}", "items", items);
  send_enveloped_ok (ctx->fd, root, "nav.bookmark.list", resp);
  json_decref (resp);
}





/* ---------- nav.avoid.* ---------- */


void
cmd_nav_avoid_add (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return;
    }

  json_t *v = json_object_get (data, "sector_id");
  if (!json_is_integer (v) || json_integer_value (v) <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
			    "invalid sector_id");
      return;
    }
  int sector_id = (int) json_integer_value (v);

  /* Cap */
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db, "SELECT COUNT(*) FROM player_avoid WHERE player_id=?1;", -1, &st,
       NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);
  int have = 0;
  if (sqlite3_step (st) == SQLITE_ROW)
    have = sqlite3_column_int (st, 0);
  sqlite3_finalize (st);
  if (have >= MAX_AVOIDS)
    {
      json_t *meta = json_pack ("{s:i,s:i}", "max", MAX_AVOIDS, "have", have);
      send_enveloped_refused (ctx->fd, root, ERR_LIMIT_EXCEEDED,
			      "too many avoids", meta);
      return;
    }

  if (sqlite3_prepare_v2
      (db,
       "INSERT INTO player_avoid(player_id,sector_id,updated_at) VALUES(?1,?2,strftime('%s','now'));",
       -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);
  sqlite3_bind_int (st, 2, sector_id);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      if (rc == SQLITE_CONSTRAINT)
	{
	  send_enveloped_error (ctx->fd, root, ERR_DUPLICATE_REQUEST,
				"already in avoid list");
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
	}
      return;
    }

  json_t *resp = json_pack ("{s:i}", "sector_id", sector_id);
  send_enveloped_ok (ctx->fd, root, "nav.avoid.added", resp);
  json_decref (resp);
}

void
cmd_nav_avoid_remove (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return;
    }

  json_t *v = json_object_get (data, "sector_id");
  if (!json_is_integer (v) || json_integer_value (v) <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
			    "invalid sector_id");
      return;
    }
  int sector_id = (int) json_integer_value (v);

  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db, "DELETE FROM player_avoid WHERE player_id=?1 AND sector_id=?2;",
       -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);
  sqlite3_bind_int (st, 2, sector_id);
  int rc = sqlite3_step (st);
  int rows = sqlite3_changes (db);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE || rows == 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_FOUND, "avoid not found");
      return;
    }

  json_t *resp = json_pack ("{s:i}", "sector_id", sector_id);
  send_enveloped_ok (ctx->fd, root, "nav.avoid.removed", resp);
  json_decref (resp);
}

void
cmd_nav_avoid_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db,
       "SELECT sector_id FROM player_avoid WHERE player_id=?1 ORDER BY updated_at DESC, sector_id;",
       -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);

  json_t *items = json_array ();
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int sid = sqlite3_column_int (st, 0);
      json_array_append_new (items, json_integer (sid));
    }
  sqlite3_finalize (st);

  json_t *resp = json_pack ("{s:O}", "items", items);
  send_enveloped_ok (ctx->fd, root, "nav.avoid.list", resp);
  json_decref (resp);
}


static json_t *
prefs_as_array (int64_t pid)
{
  sqlite3_stmt *it = NULL;
  if (db_prefs_get_all (pid, &it) != 0)
    return json_array ();

  json_t *arr = json_array ();
  while (sqlite3_step (it) == SQLITE_ROW)
    {
      const char *k = (const char *) sqlite3_column_text (it, 0);
      const char *t = (const char *) sqlite3_column_text (it, 1);
      const char *v = (const char *) sqlite3_column_text (it, 2);
      json_t *one = json_pack ("{s:s, s:s, s:s}", "key", k ? k : "", "type",
			       t ? t : "string", "value", v ? v : "");
      json_array_append_new (arr, one);
    }
  sqlite3_finalize (it);
  return arr;
}

static json_t *
bookmarks_as_array (int64_t pid)
{
  static const char *SQL =
    "SELECT name, sector_id FROM player_bookmarks WHERE player_id=?1 ORDER BY name;";
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
    return json_array ();
  sqlite3_bind_int64 (st, 1, pid);

  json_t *arr = json_array ();
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      const char *name = (const char *) sqlite3_column_text (st, 0);
      int64_t sector_id = sqlite3_column_int64 (st, 1);
      json_array_append_new (arr,
			     json_pack ("{s:s, s:i}", "name",
					name ? name : "", "sector_id",
					(int) sector_id));
    }
  sqlite3_finalize (st);
  return arr;
}

static json_t *
avoid_as_array (int64_t pid)
{
  static const char *SQL =
    "SELECT sector_id FROM player_avoid WHERE player_id=?1 ORDER BY sector_id;";
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
    return json_array ();
  sqlite3_bind_int64 (st, 1, pid);

  json_t *arr = json_array ();
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int64_t sector_id = sqlite3_column_int64 (st, 0);
      json_array_append_new (arr, json_integer ((json_int_t) sector_id));
    }
  sqlite3_finalize (st);
  return arr;
}

static json_t *
subscriptions_as_array (int64_t pid)
{
  sqlite3_stmt *it = NULL;
  if (db_subscribe_list (pid, &it) != 0)
    return json_array ();

  json_t *arr = json_array ();
  while (sqlite3_step (it) == SQLITE_ROW)
    {
      const char *topic = (const char *) sqlite3_column_text (it, 0);
      int locked = sqlite3_column_int (it, 1);
      int enabled = sqlite3_column_int (it, 2);
      const char *delivery = (const char *) sqlite3_column_text (it, 3);
      const char *filter = (const char *) sqlite3_column_text (it, 4);
      json_t *one = json_pack ("{s:s, s:b, s:b, s:s}",
			       "topic", topic ? topic : "",
			       "locked", locked ? 1 : 0,
			       "enabled", enabled ? 1 : 0,
			       "delivery", delivery ? delivery : "push");
      if (filter)
	json_object_set_new (one, "filter", json_string (filter));
      json_array_append_new (arr, one);
    }
  sqlite3_finalize (it);
  return arr;
}

/* player.get_settings → player.settings_v1 */
int
cmd_player_get_settings (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_enveloped_error (ctx ? ctx->fd : -1, root, ERR_NOT_AUTHENTICATED,
			    "Authentication required");
      return 0;
    }

  json_t *prefs = prefs_as_array (ctx->player_id);
  json_t *bm = bookmarks_as_array (ctx->player_id);
  json_t *avoid = avoid_as_array (ctx->player_id);
  json_t *subs = subscriptions_as_array (ctx->player_id);

  json_t *data = json_pack ("{s:o, s:o, s:o, s:o}",
			    "prefs", prefs,
			    "bookmarks", bm,
			    "avoid", avoid,
			    "subscriptions", subs);

  send_enveloped_ok (ctx->fd, root, "player.settings_v1", data);
  return 0;
}


#include <sqlite3.h>
#include <jansson.h>
#include <time.h>
// ... include your other headers ...

int
cmd_get_news (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;

  // 1. Basic check
  if (ctx->player_id <= 0)
    {
      // send_enveloped_refused is assumed to be available
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  // 2. Query to retrieve news (ordered newest first, non-expired)
  const char *SQL = "SELECT published_ts, news_category, article_text " "FROM news_feed " "WHERE expiration_ts > ? " "ORDER BY published_ts DESC, news_id DESC " "LIMIT 50;";	// Limit results to a reasonable number (e.g., 50)

  json_t *news_array = json_array ();
  long long current_time = (long long) time (NULL);

  // 3. Prepare and Bind
  rc = sqlite3_prepare_v2 (db, SQL, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      // Handle error...
      json_decref (news_array);
      send_enveloped_error (ctx->fd, root, 1500, "Database error");
      return 0;
    }
  sqlite3_bind_int64 (stmt, 1, current_time);	// Bind current time for expiry check

  // 4. Loop and Build JSON Array
  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      long long pub_ts = sqlite3_column_int64 (stmt, 0);
      const char *category = (const char *) sqlite3_column_text (stmt, 1);
      const char *article = (const char *) sqlite3_column_text (stmt, 2);

      json_array_append_new (news_array,
			     json_pack ("{s:i, s:s, s:s}",
					"ts", pub_ts,
					"category", category,
					"article", article));
    }

  sqlite3_finalize (stmt);

  // 5. Build and Send Response
  json_t *payload = json_pack ("{s:O}", "news", news_array);
  send_enveloped_ok (ctx->fd, root, "news.list", payload);

  json_decref (payload);
  return 0;
}

int
cmd_bank_deposit (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 400, "Missing data object.");
      return 0;
    }

  json_t *j_amount = json_object_get (data, "amount");
  if (!json_is_integer (j_amount) || json_integer_value (j_amount) <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400,
                            "Deposit amount must be a positive integer.");
      return 0;
    }

  long long amount = json_integer_value (j_amount);
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, 500, "No database handle.");
      return 0;
    }

  long long new_balance = 0;
  int rc = h_add_credits (db, "player", ctx->player_id, amount, &new_balance);

  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, "Failed to deposit credits.");
      return 0;
    }

  json_t *payload = json_pack ("{s:i, s:I}", "player_id", ctx->player_id, "new_balance", new_balance);
  send_enveloped_ok (ctx->fd, root, "bank.deposit.confirmed", payload);
  json_decref (payload);

  return 0;
}


