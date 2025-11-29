#include <sqlite3.h>
#include <stdio.h>
#include <stdint.h>
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>		/* for strtol */
#include <sqlite3.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
// local includes
#include "server_players.h"
#include "database.h"
#include "database_cmd.h"
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
#include "server_ships.h"
#include "server_loop.h"

static const char *DEFAULT_PLAYER_FIELDS[] = {
    "id",
    "username",
    "credits",
    "sector", // Assuming "sector" is a direct field in player info or easily derivable
    "faction",
    NULL // Sentinel value to mark the end of the array
};

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
  pthread_mutex_lock (&db_mutex);	// Acquire lock

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
      pthread_mutex_unlock (&db_mutex);	// Release lock
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
      pthread_mutex_unlock (&db_mutex);	// Release lock
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
	  pthread_mutex_unlock (&db_mutex);	// Release lock
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
	  pthread_mutex_unlock (&db_mutex);	// Release lock
	  return TURN_CONSUME_ERROR_NO_TURNS;
	}
      else if (rc == SQLITE_DONE)
	{
	  // Player does not exist in the turns table
	  sqlite3_finalize (stmt);
	  LOGE
	    ("Turn consumption failed for Player %d (%s): Player not found in turns table.\n",
	     player_id, reason_cmd);
	  pthread_mutex_unlock (&db_mutex);	// Release lock
	  return TURN_CONSUME_ERROR_PLAYER_NOT_FOUND;
	}
      else
	{
	  // Some other select error
	  LOGE ("DB Error (Execute Check %s): %s\n", reason_cmd,
		sqlite3_errmsg (db_conn));
	  sqlite3_finalize (stmt);
	  pthread_mutex_unlock (&db_mutex);	// Release lock
	  return TURN_CONSUME_ERROR_DB_FAIL;
	}
    }

  // 6. Success
  LOGD
    ("Player %d consumed 1 turn for command: %s. New turn count updated.\n",
     player_id, reason_cmd);
  pthread_mutex_unlock (&db_mutex);	// Release lock
  return TURN_CONSUME_SUCCESS;
}


/*
 * In server_players.c
 * This helper now *accepts* the db pointer.
 */
int
h_get_credits (sqlite3 *db, const char *owner_type, int owner_id,
	       long long *credits_out)
{
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;

  // Ensure a bank account exists for the owner (INSERT OR IGNORE)
  const char *SQL_ENSURE_ACCOUNT =
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) VALUES (?1, ?2, 'CRD', 0);";
  rc = sqlite3_prepare_v2 (db, SQL_ENSURE_ACCOUNT, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_get_credits: ENSURE_ACCOUNT prepare error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (st, 1, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, owner_id);
  sqlite3_step (st);		// Execute the insert or ignore
  sqlite3_finalize (st);
  st = NULL;			// Reset statement pointer

  const char *SQL_SEL =
    "SELECT COALESCE(balance,0) FROM bank_accounts WHERE owner_type=?1 AND owner_id=?2";
  rc = sqlite3_prepare_v2 (db, SQL_SEL, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_get_credits: SELECT prepare error: %s", sqlite3_errmsg (db));
      return rc;
    }

  sqlite3_bind_text (st, 1, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, owner_id);

  if (sqlite3_step (st) == SQLITE_ROW)
    {
      *credits_out = sqlite3_column_int64 (st, 0);
      rc = SQLITE_OK;
    }
  else
    {
      LOGE
	("h_get_credits: Owner type '%s', ID %d not found in bank_accounts.",
	 owner_type, owner_id);
      rc = SQLITE_NOTFOUND;
    }
  sqlite3_finalize (st);
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

  int total = 0;

  rc = sqlite3_prepare_v2 (db,
			   "SELECT (COALESCE(s.holds, 0) - "
			   "COALESCE(s.colonists + s.equipment + s.organics + s.ore + s.slaves + s.weapons + s.drugs, 0)) "
			   "FROM players p "
			   "JOIN ships s ON s.id = p.ship "
			   "WHERE p.id = ?1", -1, &st, NULL);

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
player_credits (client_ctx_t *ctx)
{
  if (!ctx || ctx->player_id <= 0)
    return 0;
  sqlite3 *db = db_get_handle ();
  long long c = 0;
  if (h_get_credits (db, "player", ctx->player_id, &c) != SQLITE_OK)
    return 0;
  return c;
}


int
cargo_space_free (client_ctx_t *ctx)
{
  int f = 0;
  sqlite3 *db = db_get_handle ();
  if (h_get_cargo_space_free (db, ctx->player_id, &f) != SQLITE_OK)
    return 0;
  return f;
}



#include <sqlite3.h>
#include <string.h>
#include <stdio.h>		// For fprintf, stderr

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
  if (strcasecmp (commodity, "ORE") == 0)
    col_name = "ore";
  else if (strcasecmp (commodity, "ORG") == 0)
    col_name = "organics";
  else if (strcasecmp (commodity, "EQU") == 0)
    col_name = "equipment";
  else if (strcasecmp (commodity, "colonists") == 0)
    col_name = "colonists";
  else if (strcasecmp (commodity, "SLV") == 0) // NEW
    col_name = "slaves";
  else if (strcasecmp (commodity, "WPN") == 0) // NEW
    col_name = "weapons";
  else if (strcasecmp (commodity, "DRG") == 0) // NEW
    col_name = "drugs";
  else
    {
      LOGE ("h_update_ship_cargo: Invalid commodity name '%s'\n", commodity);
      return SQLITE_MISUSE;	// Invalid commodity string
    }

  // 2. Get the player's active ship ID
  const char *SQL_GET_SHIP = "SELECT ship FROM players WHERE id = ?1";

  rc = sqlite3_prepare_v2 (db, SQL_GET_SHIP, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_update_ship_cargo: Failed to prepare get_ship: %s\n",
	    sqlite3_errmsg (db));
      return rc;
    }

  sqlite3_bind_int (stmt, 1, player_id);

  rc = sqlite3_step (stmt);
  if (rc == SQLITE_ROW)
    {
      ship_id = sqlite3_column_int (stmt, 0);
    }
  sqlite3_finalize (stmt);
  stmt = NULL;

  if (rc != SQLITE_ROW || ship_id <= 0)
    {
      // This means no player was found, or the player has no ship (ship_id is 0 or NULL)
      LOGE ("h_update_ship_cargo: Player %d has no active ship (id: %d).\n",
	    player_id, ship_id);
      return SQLITE_ERROR;	// No such player or no ship associated
    }

  // 3. Build and execute the dynamic UPDATE ... RETURNING
  // This query updates the value, clamps it at 0, and returns the new value.
  // The CHECK constraint on the 'ships' table will catch (col + ... > holds)
  LOGD ("h_update_ship_cargo: col_name='%s', delta=%d, ship_id=%d\n",
	col_name, delta, ship_id);
  sql_query =
    sqlite3_mprintf
    ("UPDATE ships SET %q = MAX(0, COALESCE(%q, 0) + ?1) WHERE id = ?2 "
     "RETURNING %q", col_name, col_name, col_name);
  LOGD ("h_update_ship_cargo: Generated SQL: %s\n", sql_query);

  if (!sql_query)
    {
      return SQLITE_NOMEM;
    }

  rc = sqlite3_prepare_v2 (db, sql_query, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_update_ship_cargo: Failed to prepare update: %s\n",
	    sqlite3_errmsg (db));
      sqlite3_free (sql_query);
      return rc;
    }

  sqlite3_bind_int (stmt, 1, delta);
  sqlite3_bind_int (stmt, 2, ship_id);

  rc = sqlite3_step (stmt);

  // If the update was successful, rc will be SQLITE_ROW (due to RETURNING)
  if (rc == SQLITE_ROW)
    {
      // (Optional) Get the new quantity if requested
      if (new_qty_out)
	{
	  *new_qty_out = sqlite3_column_int (stmt, 0);
	}
      rc = SQLITE_OK;		// Set the final return code to success
    }
  else if (rc == SQLITE_DONE)
    {
      // This would mean the UPDATE affected 0 rows, e.g., ship_id was not found
      // (which shouldn't happen if our SELECT above worked, but good to check)
      LOGE ("h_update_ship_cargo: Failed to find ship %d for update.\n",
	    ship_id);
      rc = SQLITE_ERROR;
    }
  // If rc is any other value (like SQLITE_CONSTRAINT), we will just return that
  // error code, which is what we want.

  // 4. Clean up
  sqlite3_finalize (stmt);
  sqlite3_free (sql_query);

  return rc;			// Will be SQLITE_OK or an error code (e.g., SQLITE_CONSTRAINT)
}

int
h_deduct_ship_credits (sqlite3 *db, int player_id, int amount,
		       int *new_balance)
{
  long long new_balance_ll = 0;
  const char *transaction_type = "WITHDRAWAL";
  const char *transaction_group_id = NULL;
  int rc =
    h_deduct_credits (db, "player", player_id, amount, transaction_type,
		      transaction_group_id, &new_balance_ll);
  if (rc == SQLITE_OK && new_balance)
    {
      *new_balance = (int) new_balance_ll;
    }
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



static json_t *
get_online_players_json_array (int offset, int limit, const json_t * fields_array)
{
  json_t *response_obj = NULL;
  json_t *players_list = json_array ();
  int *online_player_ids = NULL;
  int total_online = 0;
  int current_count = 0; // Number of players actually added to players_list
  json_t *effective_fields = (json_t *)fields_array; // Non-const pointer for modification
  bool using_default_fields = false;


  pthread_mutex_lock (&g_clients_mu);

  // First pass: collect all online player IDs and count total
  client_node_t *n = g_clients;
  while (n)
    {
      if (n->ctx && n->ctx->player_id > 0)
        {
          total_online++;
        }
      n = n->next;
    }

  // Allocate memory for player IDs
  if (total_online > 0)
    {
      online_player_ids = (int *) malloc (sizeof (int) * total_online);
      if (!online_player_ids)
        {
          LOGE ("Failed to allocate memory for online player IDs.\n");
          pthread_mutex_unlock (&g_clients_mu);
          json_decref (players_list);
          return NULL;
        }

      int i = 0;
      n = g_clients;
      while (n && i < total_online)
        {
          if (n->ctx && n->ctx->player_id > 0)
            {
              online_player_ids[i++] = n->ctx->player_id;
            }
          n = n->next;
        }
    }

  pthread_mutex_unlock (&g_clients_mu);


  // Apply offset and limit
  int start_idx = offset;
  if (start_idx < 0)
    start_idx = 0;
  if (start_idx >= total_online)
    start_idx = total_online;

  int end_idx = start_idx + limit;
  if (end_idx > total_online)
    end_idx = total_online;


  // If no fields requested, use default fields
  if (!effective_fields || json_array_size (effective_fields) == 0)
    {
      effective_fields = json_array();
      for (int f = 0; DEFAULT_PLAYER_FIELDS[f] != NULL; f++) {
          json_array_append_new(effective_fields, json_string(DEFAULT_PLAYER_FIELDS[f]));
      }
      using_default_fields = true;
    }


  // Second pass: fetch player details from DB and apply field selection
  for (int i = start_idx; i < end_idx; i++)
    {
      int player_id = online_player_ids[i];
      json_t *full_player_info = NULL; // This will become the filtered info

      // Use the new function that selects only the requested fields
      int db_rc = db_player_info_selected_fields(player_id, effective_fields, &full_player_info);

      if (db_rc != SQLITE_OK || !full_player_info)
        {
          LOGW ("Failed to retrieve player info (selected fields) for online player ID %d. Skipping. DB_RC: %d\n", player_id, db_rc);
          continue;
        }

      // full_player_info now already contains only the selected fields.
      // We just need to append it.
      if (json_object_size(full_player_info) > 0) { // Only add if it has fields
          json_array_append_new (players_list, full_player_info); // Transfer ownership
          current_count++;
      } else {
          json_decref(full_player_info); // No fields were added, free it
      }
    }

  // Free allocated player IDs array
  if (online_player_ids)
    {
      free (online_player_ids);
    }
  
  // Decref effective_fields if it was created from defaults
  if (using_default_fields) {
      json_decref(effective_fields);
  }

  // Construct final response object
  response_obj = json_object ();
  json_object_set_new (response_obj, "total_online", json_integer (total_online));
  json_object_set_new (response_obj, "returned_count", json_integer (current_count));
  json_object_set_new (response_obj, "offset", json_integer (offset));
  json_object_set_new (response_obj, "limit", json_integer (limit));
  json_object_set_new (response_obj, "players", players_list); // Ownership transferred


  return response_obj;
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

  // Add player credits
  long long credits_val = 0;
  if (h_get_player_petty_cash (db, ctx->player_id, &credits_val) == SQLITE_OK)
    {
      char credits_str[32];
      snprintf (credits_str, sizeof (credits_str), "%lld.00", credits_val);	// Format as string with decimals
      json_object_set_new (player, "credits", json_string (credits_str));
    }

  // Add title info from the new helper
  json_t *title_info = NULL;
  int title_rc = h_player_build_title_payload(db, ctx->player_id, &title_info);
  if (title_rc == SQLITE_OK && title_info) {
      json_object_set_new(player, "title_info", title_info); // Embed title_info into the player object
  } else {
      LOGW("cmd_player_my_info: Failed to build title payload for player %d. RC: %d", ctx->player_id, title_rc);
      // Decide how to handle this error: send without title_info, or return error.
      // For now, it will just not include title_info on failure.
  }

  send_enveloped_ok (ctx->fd, root, "player.info", pinfo);
  json_decref (pinfo);
  return 0;
}

int
cmd_player_list_online (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_NOT_AUTHENTICATED, "Not authenticated", NULL);
      return 0;
    }

  json_t *data_payload = json_object_get (root, "data");
  if (!json_is_object (data_payload))
    {
      data_payload = json_object (); // Default to empty object if data is missing or not an object
    }

  int offset = 0;
  json_t *offset_json = json_object_get (data_payload, "offset");
  if (json_is_integer (offset_json))
    {
      offset = (int) json_integer_value (offset_json);
      if (offset < 0) offset = 0;
    }

  int limit = 100; // Default limit
  json_t *limit_json = json_object_get (data_payload, "limit");
  if (json_is_integer (limit_json))
    {
      limit = (int) json_integer_value (limit_json);
      if (limit < 1) limit = 1;
      if (limit > 1000) limit = 1000; // Enforce maximum limit
    }

  json_t *fields_array = json_object_get (data_payload, "fields");
  if (!json_is_array (fields_array))
    {
      fields_array = NULL; // Ensure it's NULL if not a valid array
    }

  json_t *response_data = get_online_players_json_array (offset, limit, fields_array);

  if (!response_data)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "Failed to retrieve online players list.");
      return 0;
    }

  send_enveloped_ok (ctx->fd, root, "player.list_online.result", response_data);
  json_decref (response_data);
  return 0;
}

int
cmd_player_rankings (client_ctx_t *ctx, json_t *root)
{
    if (ctx->player_id <= 0) {
        send_enveloped_refused(ctx->fd, root, ERR_NOT_AUTHENTICATED, "Authentication required.", NULL);
        return 0;
    }

    sqlite3 *db = db_get_handle();
    if (!db) {
        send_enveloped_error(ctx->fd, root, ERR_DB, "Database error.");
        return 0;
    }

    json_t *data_payload = json_object_get(root, "data");
    const char *order_by = "experience"; // Default ranking criterion
    int limit = 10; // Default limit
    int offset = 0; // Default offset

    if (json_is_object(data_payload)) {
        json_t *by_json = json_object_get(data_payload, "by");
        if (json_is_string(by_json)) {
            const char *requested_by = json_string_value(by_json);
            // Only allow specific ranking criteria for now
            if (strcmp(requested_by, "experience") == 0 || strcmp(requested_by, "net_worth") == 0) {
                order_by = requested_by;
            } else {
                send_enveloped_refused(ctx->fd, root, ERR_INVALID_ARG, "Invalid ranking criterion. Supported: 'experience', 'net_worth'.", NULL);
                return 0;
            }
        }

        json_t *limit_json = json_object_get(data_payload, "limit");
        if (json_is_integer(limit_json)) {
            limit = (int)json_integer_value(limit_json);
            if (limit < 1 || limit > 100) limit = 10; // Clamp limit
        }
        
        json_t *offset_json = json_object_get(data_payload, "offset");
        if (json_is_integer(offset_json)) {
            offset = (int)json_integer_value(offset_json);
            if (offset < 0) offset = 0;
        }
    }

    json_t *rankings_array = json_array();
    sqlite3_stmt *st = NULL;
    char sql[512]; // Increased size for longer queries

    // Base query for rankings
    if (strcmp(order_by, "experience") == 0) {
        snprintf(sql, sizeof(sql),
            "SELECT id, name, alignment, experience FROM players "
            "ORDER BY experience DESC LIMIT %d OFFSET %d;", limit, offset);
    } else if (strcmp(order_by, "net_worth") == 0) {
        // This assumes v_player_networth is a view or similar.
        // For now, a simplified query. You might have a more complex net_worth calculation.
        snprintf(sql, sizeof(sql),
            "SELECT p.id, p.name, p.alignment, p.experience, COALESCE(ba.balance, 0) as net_worth "
            "FROM players p LEFT JOIN bank_accounts ba ON p.id = ba.owner_id AND ba.owner_type = 'player' "
            "ORDER BY net_worth DESC LIMIT %d OFFSET %d;", limit, offset);
    } else {
        // Should not happen due to prior validation, but defensive programming
        send_enveloped_error(ctx->fd, root, ERROR_INTERNAL, "Internal ranking error.");
        return 0;
    }
    
    pthread_mutex_lock(&db_mutex); // Acquire lock for thread safety

    int rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("cmd_player_rankings: Prepare error: %s", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        send_enveloped_error(ctx->fd, root, ERR_DB, "Database error retrieving rankings.");
        return 0;
    }

    int rank_num = offset + 1;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        int player_id = sqlite3_column_int(st, 0);
        const char *player_name = (const char*)sqlite3_column_text(st, 1);
        int alignment = sqlite3_column_int(st, 2);
        long long experience = sqlite3_column_int64(st, 3);
        long long net_worth = 0;

        json_t *player_rank_obj = json_object();
        json_object_set_new(player_rank_obj, "rank", json_integer(rank_num++));
        json_object_set_new(player_rank_obj, "id", json_integer(player_id));
        json_object_set_new(player_rank_obj, "username", json_string(player_name));
        json_object_set_new(player_rank_obj, "alignment", json_integer(alignment));
        json_object_set_new(player_rank_obj, "experience", json_integer(experience));

        if (strcmp(order_by, "net_worth") == 0) {
             net_worth = sqlite3_column_int64(st, 4);
             char net_worth_str[32];
             snprintf(net_worth_str, sizeof(net_worth_str), "%lld.00", net_worth);
             json_object_set_new(player_rank_obj, "net_worth", json_string(net_worth_str));
        }


        // Add title info
        json_t *title_info = NULL;
        // db_mutex is already locked, so h_player_build_title_payload needs to be adjusted not to lock itself, or we temporarily unlock here.
        // For now, assuming h_player_build_title_payload handles its own locking safely or expects outer lock.
        // Given db_player_update_commission temporarily unlocks and relocks, this should be fine.
        pthread_mutex_unlock(&db_mutex); // Temporarily unlock before calling another function that locks db_mutex
        int title_rc = h_player_build_title_payload(db, player_id, &title_info);
        pthread_mutex_lock(&db_mutex); // Re-acquire lock

        if (title_rc == SQLITE_OK && title_info) {
            json_object_set_new(player_rank_obj, "title_info", title_info);
        } else {
            LOGW("cmd_player_rankings: Failed to build title payload for player %d. RC: %d", player_id, title_rc);
            // Continue without title_info if it fails
        }
        json_array_append_new(rankings_array, player_rank_obj);
    }
    sqlite3_finalize(st);
    pthread_mutex_unlock(&db_mutex); // Release lock

    if (rc != SQLITE_DONE) {
        LOGE("cmd_player_rankings: Execute error: %s", sqlite3_errmsg(db));
        json_decref(rankings_array);
        send_enveloped_error(ctx->fd, root, ERR_DB, "Database error retrieving rankings.");
        return 0;
    }

    json_t *response_data = json_object();
    json_object_set_new(response_data, "rankings", rankings_array);
    send_enveloped_ok(ctx->fd, root, "player.rankings", response_data);
    json_decref(response_data);

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
      else if (strcasecmp (key, "news.fetch_mode") == 0)
	{
	  if (!json_is_integer (val))
	    {
	      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
				    "news.fetch_mode must be an integer");
	      return -1;
	    }
	  int mode = json_integer_value (val);
	  if (mode < 0 || mode > 7)
	    {
	      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
				    "news.fetch_mode must be between 0 and 7");
	      return -1;
	    }
	}
      else if (strcasecmp (key, "news.category_filter") == 0)
	{
	  if (!json_is_string (val))
	    {
	      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
				    "news.category_filter must be a string");
	      return -1;
	    }
	  const char *filter = json_string_value (val);
	  if (strlen (filter) > 128)
	    {
	      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
				    "news.category_filter is too long");
	      return -1;
	    }
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

int
h_deduct_player_petty_cash (sqlite3 *db, int player_id, long long amount,
			    long long *new_balance_out)
{
  char *errmsg = NULL;
  int rc = SQLITE_ERROR;
  long long current_balance = 0;

  if (!db || player_id <= 0 || amount <= 0)
    {
      LOGE
	("h_deduct_player_petty_cash: Invalid arguments. db=%p, player_id=%d, amount=%lld",
	 db, player_id, amount);
      return SQLITE_MISUSE;
    }

  // 1. Get current petty cash balance
  sqlite3_stmt *select_stmt = NULL;
  const char *sql_select_balance =
    "SELECT credits FROM players WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_select_balance, -1, &select_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_deduct_player_petty_cash: SELECT prepare error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (select_stmt, 1, player_id);

  if (sqlite3_step (select_stmt) == SQLITE_ROW)
    {
      current_balance = sqlite3_column_int64 (select_stmt, 0);
    }
  else
    {
      LOGE
	("h_deduct_player_petty_cash: Player ID %d not found in players table.",
	 player_id);
      rc = SQLITE_NOTFOUND;
      goto cleanup;
    }
  sqlite3_finalize (select_stmt);
  select_stmt = NULL;

  // 2. Check for sufficient funds
  if (current_balance < amount)
    {
      LOGW
	("h_deduct_player_petty_cash: Insufficient petty cash for player %d. Current: %lld, Attempted deduct: %lld",
	 player_id, current_balance, amount);
      rc = SQLITE_CONSTRAINT;	// Or a custom error code for insufficient funds
      goto cleanup;
    }

  // 3. Update petty cash balance
  long long new_balance = current_balance - amount;
  sqlite3_stmt *update_stmt = NULL;
  const char *sql_update_balance =
    "UPDATE players SET credits = ? WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_update_balance, -1, &update_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_deduct_player_petty_cash: UPDATE prepare error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int64 (update_stmt, 1, new_balance);
  sqlite3_bind_int (update_stmt, 2, player_id);

  if (sqlite3_step (update_stmt) != SQLITE_DONE)
    {
      LOGE ("h_deduct_player_petty_cash: UPDATE step error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_finalize (update_stmt);
  update_stmt = NULL;

  if (new_balance_out)
    *new_balance_out = new_balance;

  return SQLITE_OK;

cleanup:
  if (select_stmt)
    sqlite3_finalize (select_stmt);
  if (update_stmt)
    sqlite3_finalize (update_stmt);
  if (errmsg)
    sqlite3_free (errmsg);
  return rc;
}

int
h_add_player_petty_cash (sqlite3 *db, int player_id, long long amount,
			 long long *new_balance_out)
{
  char *errmsg = NULL;
  int rc = SQLITE_ERROR;
  long long current_balance = 0;

  if (!db || player_id <= 0 || amount <= 0)
    {
      LOGE
	("h_add_player_petty_cash: Invalid arguments. db=%p, player_id=%d, amount=%lld",
	 db, player_id, amount);
      return SQLITE_MISUSE;
    }

  // 1. Get current petty cash balance
  pthread_mutex_lock (&db_mutex);	// Acquire lock
  sqlite3_stmt *select_stmt = NULL;
  const char *sql_select_balance =
    "SELECT credits FROM players WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_select_balance, -1, &select_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_add_player_petty_cash: SELECT prepare error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (select_stmt, 1, player_id);

  if (sqlite3_step (select_stmt) == SQLITE_ROW)
    {
      current_balance = sqlite3_column_int64 (select_stmt, 0);
    }
  else
    {
      LOGE
	("h_add_player_petty_cash: Player ID %d not found in players table.",
	 player_id);
      rc = SQLITE_NOTFOUND;
      goto cleanup;
    }
  sqlite3_finalize (select_stmt);
  select_stmt = NULL;

  // 2. Update petty cash balance
  long long new_balance = current_balance + amount;
  sqlite3_stmt *update_stmt = NULL;
  const char *sql_update_balance =
    "UPDATE players SET credits = ? WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_update_balance, -1, &update_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_add_player_petty_cash: UPDATE prepare error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int64 (update_stmt, 1, new_balance);
  sqlite3_bind_int (update_stmt, 2, player_id);

  if (sqlite3_step (update_stmt) != SQLITE_DONE)
    {
      LOGE ("h_add_player_petty_cash: UPDATE step error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_finalize (update_stmt);
  update_stmt = NULL;

  if (new_balance_out)
    *new_balance_out = new_balance;

  rc = SQLITE_OK;

cleanup:
  if (select_stmt)
    sqlite3_finalize (select_stmt);
  if (update_stmt)
    sqlite3_finalize (update_stmt);
  if (errmsg)
    sqlite3_free (errmsg);
  pthread_mutex_unlock (&db_mutex);	// Release lock
  return rc;
}

// Unlocked version: Assumes db_mutex is already held by the caller
int
h_add_player_petty_cash_unlocked (sqlite3 *db, int player_id, long long amount,
                                  long long *new_balance_out)
{
  char *errmsg = NULL;
  int rc = SQLITE_ERROR;
  long long current_balance = 0;

  if (!db || player_id <= 0 || amount <= 0)
    {
      LOGE
	("h_add_player_petty_cash_unlocked: Invalid arguments. db=%p, player_id=%d, amount=%lld",
	 db, player_id, amount);
      return SQLITE_MISUSE;
    }

  // 1. Get current petty cash balance (unlocked context)
  sqlite3_stmt *select_stmt = NULL;
  const char *sql_select_balance =
    "SELECT credits FROM players WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_select_balance, -1, &select_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_add_player_petty_cash_unlocked: SELECT prepare error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (select_stmt, 1, player_id);

  if (sqlite3_step (select_stmt) == SQLITE_ROW)
    {
      current_balance = sqlite3_column_int64 (select_stmt, 0);
    }
  else
    {
      LOGE
	("h_add_player_petty_cash_unlocked: Player ID %d not found in players table.",
	 player_id);
      rc = SQLITE_NOTFOUND;
      goto cleanup;
    }
  sqlite3_finalize (select_stmt);
  select_stmt = NULL;

  // 2. Update petty cash balance (unlocked context)
  long long new_balance = current_balance + amount;
  sqlite3_stmt *update_stmt = NULL;
  const char *sql_update_balance =
    "UPDATE players SET credits = ? WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_update_balance, -1, &update_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_add_player_petty_cash_unlocked: UPDATE prepare error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int64 (update_stmt, 1, new_balance);
  sqlite3_bind_int (update_stmt, 2, player_id);

  if (sqlite3_step (update_stmt) != SQLITE_DONE)
    {
      LOGE ("h_add_player_petty_cash_unlocked: UPDATE step error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_finalize (update_stmt);
  update_stmt = NULL;

  if (new_balance_out)
    *new_balance_out = new_balance;

  rc = SQLITE_OK;

cleanup:
  if (select_stmt)
    sqlite3_finalize (select_stmt);
  if (update_stmt)
    sqlite3_finalize (update_stmt);
  if (errmsg)
    sqlite3_free (errmsg);
  return rc;
}

// Unlocked version: Assumes db_mutex is already held by the caller
int
h_deduct_player_petty_cash_unlocked (sqlite3 *db, int player_id,
                                     long long amount,
                                     long long *new_balance_out)
{
  char *errmsg = NULL;
  int rc = SQLITE_ERROR;
  long long current_balance = 0;

  if (!db || player_id <= 0 || amount <= 0)
    {
      LOGE
	("h_deduct_player_petty_cash_unlocked: Invalid arguments. db=%p, player_id=%d, amount=%lld",
	 db, player_id, amount);
      return SQLITE_MISUSE;
    }

  // 1. Get current petty cash balance (unlocked context)
  sqlite3_stmt *select_stmt = NULL;
  const char *sql_select_balance =
    "SELECT credits FROM players WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_select_balance, -1, &select_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_deduct_player_petty_cash_unlocked: SELECT prepare error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (select_stmt, 1, player_id);

  if (sqlite3_step (select_stmt) == SQLITE_ROW)
    {
      current_balance = sqlite3_column_int64 (select_stmt, 0);
    }
  else
    {
      LOGE
	("h_deduct_player_petty_cash_unlocked: Player ID %d not found in players table.",
	 player_id);
      rc = SQLITE_NOTFOUND;
      goto cleanup;
    }
  sqlite3_finalize (select_stmt);
  select_stmt = NULL;

  // 2. Check for sufficient funds
  if (current_balance < amount)
    {
      LOGW
	("h_deduct_player_petty_cash_unlocked: Insufficient petty cash for player %d. Current: %lld, Attempted deduct: %lld",
	 player_id, current_balance, amount);
      rc = SQLITE_CONSTRAINT;	// Or a custom error code for insufficient funds
      goto cleanup;
    }

  // 3. Update petty cash balance (unlocked context)
  long long new_balance = current_balance - amount;
  sqlite3_stmt *update_stmt = NULL;
  const char *sql_update_balance =
    "UPDATE players SET credits = ? WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_update_balance, -1, &update_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_deduct_player_petty_cash_unlocked: UPDATE prepare error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int64 (update_stmt, 1, new_balance);
  sqlite3_bind_int (update_stmt, 2, player_id);

  if (sqlite3_step (update_stmt) != SQLITE_DONE)
    {
      LOGE ("h_deduct_player_petty_cash_unlocked: UPDATE step error: %s",
	    sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_finalize (update_stmt);
  update_stmt = NULL;

  if (new_balance_out)
    *new_balance_out = new_balance;

  return SQLITE_OK;

cleanup:
  if (select_stmt)
    sqlite3_finalize (select_stmt);
  if (update_stmt)
    sqlite3_finalize (update_stmt);
  if (errmsg)
    sqlite3_free (errmsg);
  return rc;
}

int
auth_player_get_type (int player_id)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *stmt = NULL;
  int player_type = 0; // Default to 0 or an 'unknown' type

  if (player_id <= 0)
    return 0; // Invalid player_id

  const char *sql = "SELECT type FROM players WHERE id = ?;";
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    LOGE("auth_player_get_type: Failed to prepare statement: %s", sqlite3_errmsg(db));
    return 0;
  }

  sqlite3_bind_int(stmt, 1, player_id);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    player_type = sqlite3_column_int(stmt, 0);
  } else {
    // Player not found or no type defined
    LOGW("auth_player_get_type: Player ID %d not found or type not set.", player_id);
  }

  sqlite3_finalize(stmt);
  return player_type;
}

int
h_get_player_petty_cash (sqlite3 *db, int player_id, long long *credits_out)
{
  sqlite3_stmt *select_stmt = NULL;
  const char *sql_select_balance =
    "SELECT credits FROM players WHERE id = ?;";
  int rc = SQLITE_ERROR;

  if (!db || player_id <= 0 || !credits_out)
    {
      LOGE
	("h_get_player_petty_cash: Invalid arguments. db=%p, player_id=%d, credits_out=%p",
	 db, player_id, credits_out);
      return SQLITE_MISUSE;
    }

  rc = sqlite3_prepare_v2 (db, sql_select_balance, -1, &select_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_get_player_petty_cash: SELECT prepare error: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (select_stmt, 1, player_id);

  if (sqlite3_step (select_stmt) == SQLITE_ROW)
    {
      *credits_out = sqlite3_column_int64 (select_stmt, 0);
      rc = SQLITE_OK;
    }
  else
    {
      LOGE
	("h_get_player_petty_cash: Player ID %d not found in players table.",
	 player_id);
      rc = SQLITE_NOTFOUND;
    }
  sqlite3_finalize (select_stmt);
  return rc;
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


/////////
int cmd_bank_history (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_NOT_AUTHENTICATED, "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA, "data must be object");
      return 0;
    }

  // --- Input Parameters ---
  int limit = 20; // Default limit
  json_t *j_limit = json_object_get(data, "limit");
  if (json_is_integer(j_limit)) {
      limit = (int)json_integer_value(j_limit);
      if (limit <= 0 || limit > 50) limit = 20; // Clamp limit
  }

  const char *tx_type_filter = NULL;
  json_t *j_tx_type = json_object_get(data, "tx_type");
  if (json_is_string(j_tx_type)) {
      tx_type_filter = json_string_value(j_tx_type);
  }

  long long start_date = 0;
  json_t *j_start_date = json_object_get(data, "start_date");
  if (json_is_integer(j_start_date)) {
      start_date = json_integer_value(j_start_date);
  }

  long long end_date = 0;
  json_t *j_end_date = json_object_get(data, "end_date");
  if (json_is_integer(j_end_date)) {
      end_date = json_integer_value(j_end_date);
  }

  long long min_amount = 0;
  json_t *j_min_amount = json_object_get(data, "min_amount");
  if (json_is_integer(j_min_amount)) {
      min_amount = json_integer_value(j_min_amount);
  }

  long long max_amount = 0;
  json_t *j_max_amount = json_object_get(data, "max_amount");
  if (json_is_integer(j_max_amount)) {
      max_amount = json_integer_value(j_max_amount);
  }

  json_t *transactions_array = NULL;
  int rc = db_bank_get_transactions("player", ctx->player_id, limit, tx_type_filter,
                                  start_date, end_date, min_amount, max_amount, &transactions_array);

  if (rc != SQLITE_OK) {
      send_enveloped_error(ctx->fd, root, ERR_DB_QUERY_FAILED, "Failed to retrieve bank history.");
      if (transactions_array) json_decref(transactions_array);
      return 0;
  }

  // --- Send Response ---
  // The has_next_page logic is simplified here. A more robust implementation
  // might involve fetching limit + 1 records to see if there are more.
  json_t *payload = json_pack("{s:o, s:b}", "history", transactions_array, "has_next_page", json_false());
  send_enveloped_ok(ctx->fd, root, "bank.history.response", payload);
  json_decref(payload); // this will also decref transactions_array

  return 0;
}


////////
int
cmd_bank_leaderboard (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;

  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			      "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return 0;
    }

  // --- Input Parameters ---
  int limit = 20;		// Default limit
  json_t *j_limit = json_object_get (data, "limit");
  if (json_is_integer (j_limit))
    {
      limit = (int) json_integer_value (j_limit);
      if (limit <= 0 || limit > 100)
	limit = 20;		// Clamp limit between 1 and 100
    }

  // --- Build SQL Query ---
  // We need player name from 'players' table and balance from 'bank_accounts' table
  const char *sql_query =
    "SELECT P.name, BA.balance "
    "FROM bank_accounts BA "
    "JOIN players P ON P.id = BA.owner_id "
    "WHERE BA.owner_type = 'player' " "ORDER BY BA.balance DESC " "LIMIT ?1;";

  rc = sqlite3_prepare_v2 (db, sql_query, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("cmd_bank_leaderboard: Failed to prepare statement: %s",
	    sqlite3_errmsg (db));
      send_enveloped_error (ctx->fd, root, 500, "Database error.");
      return 0;
    }
  sqlite3_bind_int (stmt, 1, limit);

  // --- Execute Query and Build Response ---
  json_t *leaderboard_array = json_array ();

  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      json_t *entry = json_object ();
      json_object_set_new (entry, "player_name",
			   json_string ((const char *)
					sqlite3_column_text (stmt, 0)));
      json_object_set_new (entry, "balance",
			   json_integer (sqlite3_column_int64 (stmt, 1)));
      json_array_append_new (leaderboard_array, entry);
    }
  sqlite3_finalize (stmt);

  // --- Send Response ---
  json_t *payload = json_pack ("{s:o}", "leaderboard", leaderboard_array);
  send_enveloped_ok (ctx->fd, root, "bank.leaderboard.response", payload);
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

  long long player_petty_cash = 0;
  int rc = h_get_player_petty_cash (db, ctx->player_id, &player_petty_cash);
  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500,
			    "Failed to retrieve petty cash balance.");
      return 0;
    }

  if (player_petty_cash < amount)
    {
      send_enveloped_refused (ctx->fd, root, ERR_INSUFFICIENT_FUNDS,
			      "Insufficient petty cash to deposit.", NULL);
      return 0;
    }

  // Start transaction
  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);

  // Deduct from player's petty cash
  rc = h_deduct_player_petty_cash (db, ctx->player_id, amount, NULL);	// new_balance_out can be NULL
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      if (rc == SQLITE_CONSTRAINT)
	{
	  send_enveloped_refused (ctx->fd, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient petty cash to deposit.",
				  NULL);
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, 500,
				"Failed to deduct from petty cash.");
	}
      return 0;
    }

  // Add to player's bank account
  long long new_bank_balance = 0;
  // rc = h_add_credits (db, "player", ctx->player_id, amount, &new_bank_balance);  
  const char *transaction_type = "DEPOSIT";
  const char *transaction_group_id = NULL;	// Or an appropriate ID if part of a grouped transaction
  rc = h_add_credits (db, "player", ctx->player_id, amount,
		      transaction_type, transaction_group_id,
		      &new_bank_balance);


  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, 500,
			    "Failed to deposit credits to bank.");
      return 0;
    }

  // Commit transaction
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);

  json_t *payload =
    json_pack ("{s:i, s:I}", "player_id", ctx->player_id, "new_balance",
	       new_bank_balance);
  send_enveloped_ok (ctx->fd, root, "bank.deposit.confirmed", payload);
  json_decref (payload);

  return 0;
}

int
cmd_bank_transfer (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			      "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return 0;
    }

  json_t *j_to_player_id = json_object_get (data, "to_player_id");
  json_t *j_amount = json_object_get (data, "amount");

  if (!json_is_integer (j_to_player_id)
      || json_integer_value (j_to_player_id) <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_INVALID_ARG,
			      "Invalid recipient player ID", NULL);
      return 0;
    }

  if (!json_is_integer (j_amount) || json_integer_value (j_amount) <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_INVALID_ARG,
			      "Invalid transfer amount", NULL);
      return 0;
    }

  int to_player_id = json_integer_value (j_to_player_id);
  long long amount = json_integer_value (j_amount);

  if (ctx->player_id == to_player_id)
    {
      send_enveloped_refused (ctx->fd, root, ERR_INVALID_ARG,
			      "Cannot transfer to self", NULL);
      return 0;
    }

  sqlite3 *db = db_get_handle ();
  long long from_balance = 0;
  long long to_balance = 0;
  int rc;

  // Start transaction
  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);

  // Check sender's balance
  rc = h_get_credits (db, "player", ctx->player_id, &from_balance);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN,
			    "Failed to get sender balance");
      return 0;
    }
  if (from_balance < amount)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_refused (ctx->fd, root, ERR_INSUFFICIENT_FUNDS,
			      "Insufficient funds", NULL);
      return 0;
    }

  // Deduct from sender
  const char *transaction_type = "TRANSFER";
  char tx_group_id_buffer[UUID_STR_LEN];
  h_generate_hex_uuid (tx_group_id_buffer, sizeof (tx_group_id_buffer));
  const char *tx_group_id_for_transfer = tx_group_id_buffer;


  rc = h_deduct_credits (db, "player", ctx->player_id, amount,
			 transaction_type, tx_group_id_for_transfer,
			 &from_balance);

  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN,
			    "Failed to deduct funds");
      return 0;
    }
  // Add to recipient
  // rc = h_add_credits (db, "player", to_player_id, amount, &to_balance);

  // Use the same transaction_type and tx_group_id_for_transfer generated earlier for the sender's deduction.
  const char *add_tx_type = "TRANSFER";	// Consistent with the deduction for the sender

  rc = h_add_credits (db, "player", to_player_id, amount, add_tx_type, tx_group_id_for_transfer,	// Pass the same generated group ID
		      &to_balance);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN,
			    "Failed to add funds to recipient");
      return 0;
    }
  // Commit transaction
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);

  json_t *payload = json_pack ("{s:i, s:i, s:I, s:I}",
			       "from_player_id", ctx->player_id,
			       "to_player_id", to_player_id,
			       "from_balance", from_balance,
			       "to_balance", to_balance);
  send_enveloped_ok (ctx->fd, root, "bank.transfer.confirmed", payload);
  json_decref (payload);
  return 0;
}

int
cmd_bank_withdraw (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			      "Not authenticated", NULL);
      return 0;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return 0;
    }
  json_t *j_amount = json_object_get (data, "amount");

  if (!json_is_integer (j_amount) || json_integer_value (j_amount) <= 0)
    {
      send_enveloped_refused (ctx->fd, root, ERR_INVALID_ARG,
			      "Invalid withdrawal amount", NULL);
      return 0;
    }

  long long amount = json_integer_value (j_amount);
  sqlite3 *db = db_get_handle ();
  long long new_balance = 0;
  int rc;

  // Start transaction
  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);

  // Deduct from player's bank account
  //rc = h_deduct_credits (db, "player", ctx->player_id, amount, &new_balance);
  const char *transaction_type = "WITHDRAWAL";
  const char *transaction_group_id = NULL;	// Assuming this is a standalone withdrawal

  rc = h_deduct_credits (db, "player", ctx->player_id, amount,
			 transaction_type, transaction_group_id,
			 &new_balance);
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      if (rc == SQLITE_CONSTRAINT)
	{			// Assuming h_deduct_credits returns SQLITE_CONSTRAINT for insufficient funds
	  send_enveloped_refused (ctx->fd, root, ERR_INSUFFICIENT_FUNDS,
				  "Insufficient funds", NULL);
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, ERR_UNKNOWN,
				"Failed to withdraw funds");
	}
      return 0;
    }
  // Add to player's petty cash (players.credits)
  rc = h_add_player_petty_cash (db, ctx->player_id, amount, NULL);	// new_balance_out can be NULL if not needed
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN,
			    "Failed to add to petty cash");
      return 0;
    }
  // Commit transaction
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);

  json_t *payload = json_pack ("{s:I}", "new_balance", new_balance);
  json_decref (payload);
  return 0;
}

int
cmd_bank_balance (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      send_enveloped_error (ctx->fd, root, 500, "No database handle");
      return 0;
    }

  long long balance = 0;
  if (db_get_player_bank_balance (ctx->player_id, &balance) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500,
			    "Could not retrieve bank balance.");
      return 0;
    }

  json_t *payload = json_object ();
  json_object_set_new (payload, "balance", json_integer (balance));

  send_enveloped_ok (ctx->fd, root, "bank.balance", payload);

  return 0;
}

int
spawn_starter_ship (sqlite3 *db, int player_id, int sector_id)
{
  int rc = SQLITE_ERROR;
  sqlite3_stmt *st = NULL;
  int ship_type_id = 0;
  int new_ship_id = 0;
  const char *starter_ship_type_name = "Scout Marauder";

  LOGI
    ("spawn_starter_ship: Player %d respawning in starter ship at sector %d.",
     player_id, sector_id);

  // 1. Get the shiptype ID for the starter ship (Scout Marauder)
  const char *sql_get_shiptype =
    "SELECT id, initialholds, maxfighters, maxshields FROM shiptypes WHERE name = ?;";
  rc = sqlite3_prepare_v2 (db, sql_get_shiptype, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("spawn_starter_ship: Failed to prepare shiptype select: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (st, 1, starter_ship_type_name, -1, SQLITE_STATIC);

  int initial_holds = 0;
  int initial_fighters = 0;
  int initial_shields = 0;

  if (sqlite3_step (st) == SQLITE_ROW)
    {
      ship_type_id = sqlite3_column_int (st, 0);
      initial_holds = sqlite3_column_int (st, 1);
      initial_fighters = sqlite3_column_int (st, 2);
      initial_shields = sqlite3_column_int (st, 3);
    }
  sqlite3_finalize (st);
  st = NULL;

  if (ship_type_id == 0)
    {
      LOGE ("spawn_starter_ship: Starter ship type '%s' not found.",
	    starter_ship_type_name);
      return SQLITE_NOTFOUND;
    }

  // 2. Insert a new ship for the player
  // We are no longer using owner_player_id in ships directly, rely on ship_ownership
  const char *sql_insert_ship = "INSERT INTO ships (name, type_id, holds, fighters, shields, sector, ported, onplanet) " "VALUES (?1, ?2, ?3, ?4, ?5, ?6, 0, 0);";	// Assume not ported or on planet initially
  rc = sqlite3_prepare_v2 (db, sql_insert_ship, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("spawn_starter_ship: Failed to prepare ship insert: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (st, 1, "Starter Ship", -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, ship_type_id);
  sqlite3_bind_int (st, 3, initial_holds);
  sqlite3_bind_int (st, 4, initial_fighters);
  sqlite3_bind_int (st, 5, initial_shields);
  sqlite3_bind_int (st, 6, sector_id);

  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE ("spawn_starter_ship: Failed to insert new starter ship: %s",
	    sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return rc;
    }
  new_ship_id = sqlite3_last_insert_rowid (db);
  sqlite3_finalize (st);
  st = NULL;

  // 3. Assign ship ownership via ship_ownership table
  const char *sql_insert_ship_ownership = "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES (?1, ?2, 1, 1);";	// role_id 1 = owner, is_primary 1
  rc = sqlite3_prepare_v2 (db, sql_insert_ship_ownership, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("spawn_starter_ship: Failed to prepare ship_ownership insert: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, new_ship_id);
  sqlite3_bind_int (st, 2, player_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE ("spawn_starter_ship: Failed to insert ship_ownership: %s",
	    sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return rc;
    }
  sqlite3_finalize (st);
  st = NULL;

  // 4. Update player's active ship and sector in the players table
  const char *sql_update_player =
    "UPDATE players SET ship = ?, sector = ? WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_update_player, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("spawn_starter_ship: Failed to prepare player update: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, new_ship_id);
  sqlite3_bind_int (st, 2, sector_id);
  sqlite3_bind_int (st, 3, player_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE ("spawn_starter_ship: Failed to update player's ship/sector: %s",
	    sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return rc;
    }
  sqlite3_finalize (st);
  st = NULL;

  // 5. Update podded_status table
  const char *sql_update_podded_status =
    "UPDATE podded_status SET status = 'alive', podded_count_today = 0, big_sleep_until = 0 WHERE player_id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_update_podded_status, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("spawn_starter_ship: Failed to prepare podded_status update: %s",
	    sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, player_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      LOGE
	("spawn_starter_ship: Failed to update podded_status for player %d: %s",
	 player_id, sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return rc;
    }
  sqlite3_finalize (st);
  st = NULL;

  LOGI
    ("spawn_starter_ship: Player %d successfully respawned in ship %d ('%s') at sector %d.",
     player_id, new_ship_id, starter_ship_type_name, sector_id);
  return SQLITE_OK;
}

// Function to destroy a ship and handle its side effects
int
destroy_ship_and_handle_side_effects (client_ctx_t * /*ctx */ , int sector_id,
				      int player_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      LOGE
	("destroy_ship_and_handle_side_effects: Database handle not available.");
      return -1;
    }

  // Get the ship_id for the player
  int ship_id = h_get_active_ship_id (db, player_id);
  if (ship_id <= 0)
    {
      LOGW
	("destroy_ship_and_handle_side_effects: No active ship found for player %d to destroy.",
	 player_id);
      return 0;			// Nothing to destroy
    }

  char *errmsg = NULL;
  int rc;

  // Start a transaction
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      LOGE
	("destroy_ship_and_handle_side_effects: Failed to start transaction: %s",
	 errmsg);
      if (errmsg)
	sqlite3_free (errmsg);
      return -1;
    }

  // 1. Delete the ship from the 'ships' table
  sqlite3_stmt *delete_ship_stmt = NULL;
  const char *sql_delete_ship = "DELETE FROM ships WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_delete_ship, -1, &delete_ship_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
	("destroy_ship_and_handle_side_effects: Failed to prepare delete ship statement: %s",
	 sqlite3_errmsg (db));
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      return -1;
    }
  sqlite3_bind_int (delete_ship_stmt, 1, ship_id);
  rc = sqlite3_step (delete_ship_stmt);
  sqlite3_finalize (delete_ship_stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE
	("destroy_ship_and_handle_side_effects: Failed to delete ship %d: %s",
	 ship_id, sqlite3_errmsg (db));
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      return -1;
    }

  // 2. Update the player's active_ship to NULL or 0
  sqlite3_stmt *update_player_stmt = NULL;
  const char *sql_update_player_ship =
    "UPDATE players SET ship = NULL WHERE id = ?;";
  rc =
    sqlite3_prepare_v2 (db, sql_update_player_ship, -1, &update_player_stmt,
			NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
	("destroy_ship_and_handle_side_effects: Failed to prepare update player ship statement: %s",
	 sqlite3_errmsg (db));
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      return -1;
    }
  sqlite3_bind_int (update_player_stmt, 1, player_id);
  rc = sqlite3_step (update_player_stmt);
  sqlite3_finalize (update_player_stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE
	("destroy_ship_and_handle_side_effects: Failed to update player %d active ship: %s",
	 player_id, sqlite3_errmsg (db));
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      return -1;
    }

  // Commit the transaction
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      LOGE
	("destroy_ship_and_handle_side_effects: Failed to commit transaction: %s",
	 errmsg);
      if (errmsg)
	sqlite3_free (errmsg);
      return -1;
    }

  // 3. Log an engine event for ship destruction
  json_t *evt = json_object ();
  if (evt)
    {
      json_object_set_new (evt, "player_id", json_integer (player_id));
      json_object_set_new (evt, "ship_id", json_integer (ship_id));
      json_object_set_new (evt, "sector_id", json_integer (sector_id));
      (void) db_log_engine_event ((long long) time (NULL), "ship.destroyed",
				  NULL, player_id, sector_id, evt, NULL);
    }
  else
    {
      LOGE
	("destroy_ship_and_handle_side_effects: Failed to create JSON event for ship.destroyed.");
    }

  // 4. Send a message to the player about their ship being destroyed
  // Assuming ctx->player_id is the player whose ship was destroyed.
  // The brief implies row.player is the mine owner, so the message should go to ctx->player_id.
  (void) h_send_message_to_player (player_id, 0, "Ship Destroyed!",
				   "Your ship has been destroyed! You will need to acquire a new one.");

  LOGI ("Ship %d belonging to player %d destroyed in sector %d.", ship_id,
	player_id, sector_id);

  return 0;
}


/*
 * Helper for sending mail messages.
 * This function should be located in src/server_players.c
 */
int
h_send_message_to_player (int player_id, int sender_id, const char *subject,
			  const char *message)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  int rc;

  // Updated SQL statement to insert recipient_id (player_id), sender_id, subject, message.
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
  sqlite3_bind_int (st, 1, sender_id);
  sqlite3_bind_int (st, 2, player_id);
  sqlite3_bind_text (st, 3, subject, -1, SQLITE_TRANSIENT);
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

/*
 * Helper for applying player progress (XP, alignment) and updating commission.
 * This function should be located in src/server_players.c
 */
int
h_player_apply_progress(sqlite3 *db,
                        int player_id,
                        long long delta_xp,
                        int delta_align,
                        const char *reason) // optional for audit
{
    if (!db || player_id <= 0) {
        LOGE("h_player_apply_progress: Invalid arguments.");
        return SQLITE_MISUSE;
    }

    sqlite3_stmt *st = NULL;
    int rc = SQLITE_ERROR;
    int current_alignment = 0;
    long long current_experience = 0;

    // 1. Load current xp/alignment
    const char *sql_select_player = "SELECT alignment, experience FROM players WHERE id = ?1;";
    pthread_mutex_lock(&db_mutex); // Acquire lock for this transaction

    rc = sqlite3_prepare_v2(db, sql_select_player, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("h_player_apply_progress: Prepare select player error: %s", sqlite3_errmsg(db));
        goto cleanup;
    }
    sqlite3_bind_int(st, 1, player_id);

    if (sqlite3_step(st) == SQLITE_ROW) {
        current_alignment = sqlite3_column_int(st, 0);
        current_experience = sqlite3_column_int64(st, 1);
    } else if (rc == SQLITE_DONE) {
        LOGW("h_player_apply_progress: Player with ID %d not found.", player_id);
        rc = SQLITE_NOTFOUND;
        goto cleanup;
    } else {
        LOGE("h_player_apply_progress: Execute select player error: %s", sqlite3_errmsg(db));
        goto cleanup;
    }
    sqlite3_finalize(st);
    st = NULL;

    // 2. Apply deltas + clamp (e.g. alignment [-2000, 2000], xp >= 0)
    long long new_experience = current_experience + delta_xp;
    if (new_experience < 0) new_experience = 0; // XP cannot be negative

    int new_alignment = current_alignment + delta_align;
    if (new_alignment > 2000) new_alignment = 2000; // Max alignment
    if (new_alignment < -2000) new_alignment = -2000; // Min alignment

    // 3. Write back to players table
    const char *sql_update_player = "UPDATE players SET experience = ?1, alignment = ?2 WHERE id = ?3;";
    rc = sqlite3_prepare_v2(db, sql_update_player, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("h_player_apply_progress: Prepare update player error: %s", sqlite3_errmsg(db));
        goto cleanup;
    }
    sqlite3_bind_int64(st, 1, new_experience);
    sqlite3_bind_int(st, 2, new_alignment);
    sqlite3_bind_int(st, 3, player_id);

    if (sqlite3_step(st) != SQLITE_DONE) {
        LOGE("h_player_apply_progress: Execute update player error: %s", sqlite3_errmsg(db));
        goto cleanup;
    }
    sqlite3_finalize(st);
    st = NULL;

    // 4. Call db_player_update_commission to update cached commission ID
    // db_player_update_commission manages its own locking, so this is safe for now.
    // If future changes require a single transaction for progress and commission update,
    // this call might need to be re-evaluated for nested locks/transactions.
    pthread_mutex_unlock(&db_mutex); // Release lock before calling another function that locks

    rc = db_player_update_commission(db, player_id);

    pthread_mutex_lock(&db_mutex); // Re-acquire lock

    if (rc != SQLITE_OK) {
        LOGE("h_player_apply_progress: Failed to update player commission for player %d. RC: %d", player_id, rc);
        return rc;
    }

    // 5. Optionally log promotion/demotion events (future enhancement)
    if (reason) {
        LOGD("Player %d progress updated. XP: %lld (+%lld), Alignment: %d (%+d). Reason: %s",
             player_id, new_experience, delta_xp, new_alignment, delta_align, reason);
    } else {
        LOGD("Player %d progress updated. XP: %lld (%+lld), Alignment: %d (%+d).",
             player_id, new_experience, delta_xp, new_alignment, delta_align);
    }

    rc = SQLITE_OK;

cleanup:
    if (st) sqlite3_finalize(st);
    pthread_mutex_unlock(&db_mutex); // Ensure unlock on all paths
    return rc;
}

/*
 * Helper for building JSON payload with player's title and alignment band info.
 * This function should be located in src/server_players.c
 */
int
h_player_build_title_payload(sqlite3 *db,
                             int player_id,
                             json_t **out_json)
{
    if (!db || player_id <= 0 || !out_json) {
        LOGE("h_player_build_title_payload: Invalid arguments.");
        return SQLITE_MISUSE;
    }

    sqlite3_stmt *st = NULL;
    int rc = SQLITE_ERROR;
    int player_alignment = 0;
    long long player_experience = 0;
    int player_commission_id = 0;

    char *band_code = NULL;
    char *band_name = NULL;
    int band_is_good = 0;
    int band_is_evil = 0;
    int band_can_buy_iss = 0;
    int band_can_rob_ports = 0;

    char *commission_title = NULL;
    int commission_is_evil_flag = 0;
    int determined_commission_id = 0;

    json_t *obj = NULL;
    json_t *band_obj = NULL;

    // Acquire lock for this function's operations
    pthread_mutex_lock(&db_mutex);

    // 1. Get player's current alignment, experience, and cached commission
    const char *sql_get_player_info = "SELECT alignment, experience, commission FROM players WHERE id = ?1;";
    rc = sqlite3_prepare_v2(db, sql_get_player_info, -1, &st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("h_player_build_title_payload: Prepare get player info error: %s", sqlite3_errmsg(db));
        goto cleanup;
    }
    sqlite3_bind_int(st, 1, player_id);

    if (sqlite3_step(st) == SQLITE_ROW) {
        player_alignment = sqlite3_column_int(st, 0);
        player_experience = sqlite3_column_int64(st, 1);
        player_commission_id = sqlite3_column_int(st, 2);
    } else if (rc == SQLITE_DONE) {
        LOGW("h_player_build_title_payload: Player with ID %d not found.", player_id);
        rc = SQLITE_NOTFOUND;
        goto cleanup;
    } else {
        LOGE("h_player_build_title_payload: Execute get player info error: %s", sqlite3_errmsg(db));
        goto cleanup;
    }
    sqlite3_finalize(st);
    st = NULL;

    // 2. Get Alignment Band info
    pthread_mutex_unlock(&db_mutex); // Temporarily unlock as db_alignment_band_for_value locks internally
    rc = db_alignment_band_for_value(db, player_alignment, NULL, &band_code, &band_name,
                                     &band_is_good, &band_is_evil,
                                     &band_can_buy_iss, &band_can_rob_ports);
    pthread_mutex_lock(&db_mutex); // Re-acquire lock
    if (rc != SQLITE_OK) {
        LOGE("h_player_build_title_payload: Failed to get alignment band for player %d. RC: %d", player_id, rc);
        goto cleanup;
    }

    // 3. Get Commission (rank) info - use the is_evil from the band to pick the track
    pthread_mutex_unlock(&db_mutex); // Temporarily unlock as db_commission_for_player locks internally
    rc = db_commission_for_player(db, band_is_evil, player_experience, &determined_commission_id, &commission_title, &commission_is_evil_flag);
    pthread_mutex_lock(&db_mutex); // Re-acquire lock
    if (rc != SQLITE_OK) {
        LOGE("h_player_build_title_payload: Failed to get commission for player %d. RC: %d", player_id, rc);
        goto cleanup;
    }

    // Defensive check: if cached player_commission_id is different from determined, update it.
    if (player_commission_id != determined_commission_id) {
        LOGW("h_player_build_title_payload: Player %d cached commission (%d) outdated. Recalculating and updating to %d.",
             player_id, player_commission_id, determined_commission_id);
        pthread_mutex_unlock(&db_mutex); // Temporarily unlock as db_player_update_commission locks internally
        rc = db_player_update_commission(db, player_id);
        pthread_mutex_lock(&db_mutex); // Re-acquire lock
        if (rc != SQLITE_OK) {
            LOGE("h_player_build_title_payload: Failed to update outdated commission for player %d. RC: %d", player_id, rc);
            goto cleanup;
        }
        player_commission_id = determined_commission_id; // Use the newly determined ID
    }


    // 4. Build JSON object
    obj = json_object();
    if (!obj) { rc = SQLITE_NOMEM; goto cleanup; }

    json_object_set_new(obj, "title", json_string(commission_title ? commission_title : "Unknown"));
    json_object_set_new(obj, "commission", json_integer(player_commission_id));
    json_object_set_new(obj, "alignment", json_integer(player_alignment));
    json_object_set_new(obj, "experience", json_integer(player_experience));

    band_obj = json_object();
    if (!band_obj) { rc = SQLITE_NOMEM; goto cleanup; }
    json_object_set_new(band_obj, "code", json_string(band_code ? band_code : "UNKNOWN"));
    json_object_set_new(band_obj, "name", json_string(band_name ? band_name : "Unknown"));
    json_object_set_new(band_obj, "is_good", json_boolean(band_is_good));
    json_object_set_new(band_obj, "is_evil", json_boolean(band_is_evil));
    json_object_set_new(band_obj, "can_buy_iss", json_boolean(band_can_buy_iss));
    json_object_set_new(band_obj, "can_rob_ports", json_boolean(band_can_rob_ports));
    json_object_set_new(obj, "alignment_band", band_obj);
    band_obj = NULL; // Ownership transferred

    *out_json = obj;
    rc = SQLITE_OK;

cleanup:
    if (band_code) free(band_code);
    if (band_name) free(band_name);
    if (commission_title) free(commission_title);
    if (obj && rc != SQLITE_OK) json_decref(obj);
    if (band_obj) json_decref(band_obj);
    pthread_mutex_unlock(&db_mutex); // Ensure unlock on all paths
    return rc;
}
