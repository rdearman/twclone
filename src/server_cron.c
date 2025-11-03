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
// local includes
#include "server_cron.h"
#include "server_log.h"
#include "common.h"
#include "server_players.h"
#include "server_universe.h"

#define INITIAL_QUEUE_CAPACITY 64
#define FEDSPACE_SECTOR_START 1
#define FEDSPACE_SECTOR_END 10
  // 60 minutes in seconds for the timeout
#define LOGOUT_TIMEOUT_S (60 * 60)
#define CONFISCATION_SECTOR 0
// Define the range constants
#define MIN_UNPROTECTED_SECTOR 11
#define MAX_UNPROTECTED_SECTOR 999
#define RANGE_SIZE (MAX_UNPROTECTED_SECTOR - MIN_UNPROTECTED_SECTOR + 1)

// --- Configuration ---
// News articles will expire after 7 days (604800 seconds)
#define NEWS_EXPIRATION_SECONDS 604800L
#define MAX_ARTICLE_LEN 512

// New prototype for the event logging helper
int h_log_fedspace_event (sqlite3 *db, int64_t ts, const char *type, 
                          int actor_id, int target_id, int ship_id, 
                          int sector_id, const char *reason);


static inline uint64_t
monotonic_millis (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}


/* // Pre-define reasons for logging purposes */
/* enum */
/* { */
/*   REASON_EVIL_ALIGN = 1, */
/*   REASON_EXCESS_FIGHTERS = 2, */
/*   REASON_HIGH_EXP = 3, */
/*   REASON_NO_OWNER = 4, */
/*   REASON_OVERCROWDING = 5 */
/* }; */

enum
{ MAX_TOWS_PER_PASS = 50,
  MAX_SHIPS_PER_FED_SECTOR = 5,
  MAX_FED_FIGHTERS = 49
};


typedef struct
{
  const char *name;
  cron_handler_fn fn;
} entry_t;


/* ---- forward decls for handlers ---- */
/* static int h_reset_turns_for_player(sqlite3 *db, int64_t now_s); */
/* static int h_fedspace_cleanup(sqlite3 *db, int64_t now_s); */
/* static int h_autouncloak_sweeper(sqlite3 *db, int64_t now_s); */
/* static int h_terra_replenish(sqlite3 *db, int64_t now_s); */
/* static int h_port_reprice(sqlite3 *db, int64_t now_s); */
/* static int h_planet_growth(sqlite3 *db, int64_t now_s); */
/* static int h_broadcast_ttl_cleanup(sqlite3 *db, int64_t now_s); */
/* static int h_traps_process(sqlite3 *db, int64_t now_s); */
/* static int h_npc_step(sqlite3 *db, int64_t now_s); */


static entry_t REG[] = {
  {"daily_turn_reset", h_reset_turns_for_player},
  {"fedspace_cleanup", h_fedspace_cleanup},
  {"autouncloak_sweeper", h_autouncloak_sweeper},
  {"terra_replenish", h_terra_replenish},
  {"port_reprice", h_port_reprice},
  {"planet_growth", h_planet_growth},
  {"broadcast_ttl_cleanup", h_broadcast_ttl_cleanup},
  {"traps_process", h_traps_process},
  {"npc_step", h_npc_step},
  {"port_price_drift", h_port_price_drift},
  {"news_collator", h_news_collator},
};

static int g_reg_inited = 0;


/**
 * @brief Returns a random sector ID between 11 and 999 (inclusive).
 * * NOTE: The 'db' parameter is kept for compatibility with the function signature
 * but is not used for the random number calculation.
 */
int
get_random_sector (sqlite3 *db)
{
  // Generate a random number from 0 up to (RANGE_SIZE - 1)
  // The modulus operator (%) is used to clamp the result.
  int random_offset = rand () % RANGE_SIZE;

  // Shift the offset up to the minimum sector ID
  int random_sector = MIN_UNPROTECTED_SECTOR + random_offset;

  return random_sector;
}



int
tow_ship (sqlite3 *db, int ship_id, int new_sector_id, int admin_id,
	  int reason_code)
{
  int rc;
  int owner_id = 0;
  int old_sector_id = 0;
  sqlite3_stmt *stmt = NULL;


  // --- 1. SELECT: Get current location and owner ---
  // CORRECTION: Join 'ships' (T1) directly to 'players' (T2) using T1.id = T2.ship
  const char *sql_select_ship_info = "SELECT T1.location, T2.id " "FROM ships T1 " "LEFT JOIN players T2 ON T1.id = T2.ship "	// CORRECTED JOIN
    "WHERE T1.id = ?;";

  if (sqlite3_prepare_v2 (db, sql_select_ship_info, -1, &stmt, NULL) !=
      SQLITE_OK)
    {
      LOGE ("tow_ship: Prepare SELECT failed for ship %d: %s", ship_id,
	    sqlite3_errmsg (db));
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (stmt, 1, ship_id);

  if (sqlite3_step (stmt) != SQLITE_ROW)
    {
      LOGE ("tow_ship: Ship ID %d not found or step failed. Error: %s",
	    ship_id, sqlite3_errmsg (db));
      sqlite3_finalize (stmt);
      return SQLITE_NOTFOUND;
    }

  old_sector_id = sqlite3_column_int (stmt, 0);
  // This will now correctly pull the player ID (4, 5, 6, etc.) instead of 0
  owner_id = sqlite3_column_int (stmt, 1);
  sqlite3_finalize (stmt);


  // --- 2. Reason Code Mapping (Unchanged) ---
  const char *reason_str;
  const char *subject_str;
  switch (reason_code)
    {
    case REASON_EVIL_ALIGN:
      reason_str = "Evil Alignment";
      subject_str = "Federation Tow Notice: Evil Alignment";
      break;
    case REASON_EXCESS_FIGHTERS:
      reason_str = "Excess Fighters (>50)";
      subject_str = "Federation Tow Notice: Illegal Fighters";
      break;
    case REASON_HIGH_EXP:
      reason_str = "High Experience (>=1000 EP)";
      subject_str = "Federation Tow Notice: Protection Expired";
      break;
    case REASON_NO_OWNER:
      reason_str = "Unowned/Derelict Ship (Confiscated)";
      subject_str = "Ship Confiscation Notice";
      break;
    case REASON_OVERCROWDING:
      reason_str = "Overcrowding in FedSpace";
      subject_str = "Federation Tow Notice: FedSpace Overcrowding";
      break;
    default:
      reason_str = "Unknown Reason";
      subject_str = "Federation Tow Notice: Ship Moved";
      break;
    }


  // --- 3. UPDATE: Ship Location (Unchanged) ---
  const char *sql_update_ship = "UPDATE ships SET location = ? WHERE id = ?;";

  if (sqlite3_prepare_v2 (db, sql_update_ship, -1, &stmt, NULL) != SQLITE_OK)
    {
      LOGE ("tow_ship: Prepare UPDATE ship failed: %s", sqlite3_errmsg (db));
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (stmt, 1, new_sector_id);
  sqlite3_bind_int (stmt, 2, ship_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);

  if (rc != SQLITE_DONE)
    {
      LOGE ("tow_ship: Ship %d UPDATE failed: %d", ship_id, rc);
      return rc;
    }


  // --- 4. UPDATE: Player Location and Notification ---
  if (owner_id > 0)		// This check should now pass for the bot players
    {
      const char *sql_update_player = "UPDATE players SET sector = ? WHERE id = ?;";	// Corrected to use 'id'

      if (sqlite3_prepare_v2 (db, sql_update_player, -1, &stmt, NULL) !=
	  SQLITE_OK)
	{
	  LOGE ("tow_ship: Prepare UPDATE player failed: %s",
		sqlite3_errmsg (db));
	}
      else
	{
	  // Player ID is now correctly bound
	  sqlite3_bind_int (stmt, 1, new_sector_id);
	  sqlite3_bind_int (stmt, 2, owner_id);
	  rc = sqlite3_step (stmt);
	  sqlite3_finalize (stmt);

	  if (rc != SQLITE_DONE)
	    {
	      LOGE ("tow_ship: Player %d UPDATE failed: %d", owner_id, rc);
	    }
	}

      // Send Notification Message
      char message_buffer[256];
      snprintf (message_buffer, sizeof (message_buffer),
		"Your ship was found parked in FedSpace (Sector %d) without protection. "
		"It has been towed to Sector %d for violating FedLaw: %s. "
		"The ship is now exposed to danger.",
		old_sector_id, new_sector_id, reason_str);

      // This function call should now execute
      //      h_send_message_to_player (owner_id, admin_id, subject_str,
      //                        message_buffer);
      h_send_message_to_player (admin_id, owner_id, subject_str,
				message_buffer);

    }

  // --- 5. Log the Action (Unchanged) ---
  LOGI
    ("TOW: Ship %d (Owner %d) towed from sector %d to sector %d. Reason: %s (Code %d). Admin: %d.",
     ship_id, owner_id, old_sector_id, new_sector_id, reason_str, reason_code,
     admin_id);
  int64_t current_time_s = (int64_t)time(NULL);
  h_log_fedspace_event(db, current_time_s, "fedspace:tow", admin_id, owner_id, 
		       ship_id, old_sector_id, reason_str); 

  return SQLITE_OK;
}

#define MSL_TABLE_NAME "msl_sectors"

/**
 * @brief Finds the shortest path between two sectors using Breadth-First Search (BFS).
 *
 * This function is the internal core logic, similar to the one used by cmd_move_pathfind,
 * but tailored to return the raw path data.
 *
 * @param db The SQLite database handle.
 * @param start_sector The starting sector ID.
 * @param end_sector The destination sector ID.
 * @param avoid_list A null-terminated array of sector IDs to avoid (can be NULL).
 * @return A dynamically allocated, null-terminated (0-terminated) array of sector IDs 
 * representing the path, or NULL if no path is found or on allocation error.
 * The caller is responsible for freeing the returned pointer.
 */
static int *
universe_pathfind_get_sectors (sqlite3 *db, int start_sector, int end_sector,
			       const int *avoid_list)
{
  // Check for trivial case
  if (start_sector == end_sector)
    {
      int *path = malloc (2 * sizeof (int));
      if (path)
	{
	  path[0] = start_sector;
	  path[1] = 0;		// Null terminator
	}
      return path;
    }

  // --- 1. Initialization and Sizing ---

  // Find the actual maximum sector ID for correct array sizing
  int max_sector_id = 0;
  sqlite3_stmt *max_st = NULL;
  if (sqlite3_prepare_v2
      (db, "SELECT MAX(sector_id) FROM sectors;", -1, &max_st,
       NULL) == SQLITE_OK)
    {
      if (sqlite3_step (max_st) == SQLITE_ROW)
	{
	  max_sector_id = sqlite3_column_int (max_st, 0);
	}
      sqlite3_finalize (max_st);
    }

  // Safety fallback for max size (should be larger than the largest possible sector ID)
  if (max_sector_id < 100)
    {
      max_sector_id = 2000;
    }

  // Allocate parent array: stores the sector ID that led to the current sector.
  // Index corresponds to sector ID. Value 0 means unvisited.
  int *parent = calloc (max_sector_id + 1, sizeof (int));
  if (!parent)
    {
      // LOGE("Pathfind: Failed to allocate parent array."); // Assuming LOGE is available
      return NULL;
    }

  // Initialize BFS Queue (dynamic resizing)
  int *queue = malloc (INITIAL_QUEUE_CAPACITY * sizeof (int));
  int queue_head = 0;
  int queue_tail = 0;
  int queue_capacity = INITIAL_QUEUE_CAPACITY;
  if (!queue)
    {
      // LOGE("Pathfind: Failed to allocate queue."); // Assuming LOGE is available
      free (parent);
      return NULL;
    }

  // Handle avoid list: mark avoided sectors as blocked (-2)
  if (avoid_list)
    {
      for (const int *avoid = avoid_list; *avoid != 0; avoid++)
	{
	  if (*avoid > 0 && *avoid <= max_sector_id)
	    {
	      parent[*avoid] = -2;	// Sentinel for blocked/avoided sector
	    }
	}
    }

  // Mark start sector as visited and enqueue it. Parent of start is -1 (sentinel).
  parent[start_sector] = -1;
  queue[queue_tail++] = start_sector;
  int path_found = 0;

  // --- 2. Breadth-First Search (BFS) ---

  sqlite3_stmt *warp_st = NULL;
  const char *sql_warps =
    "SELECT to_sector FROM sector_warps WHERE from_sector = ?1;";
  if (sqlite3_prepare_v2 (db, sql_warps, -1, &warp_st, NULL) != SQLITE_OK)
    {
      // LOGE("Pathfind: DB prepare error for warps: %s", sqlite3_errmsg(db)); // Assuming LOGE is available
      free (parent);
      free (queue);
      return NULL;
    }

  while (queue_head < queue_tail)
    {
      int current_sector = queue[queue_head++];

      // Check if we reached the target
      if (current_sector == end_sector)
	{
	  path_found = 1;
	  break;
	}

      sqlite3_bind_int (warp_st, 1, current_sector);

      while (sqlite3_step (warp_st) == SQLITE_ROW)
	{
	  int neighbor = sqlite3_column_int (warp_st, 0);

	  // Bounds check and already visited/avoided check (parent[neighbor] == 0 means unvisited/unblocked)
	  if (neighbor <= 0 || neighbor > max_sector_id
	      || parent[neighbor] != 0)
	    {
	      continue;
	    }

	  // Mark as visited and record parent
	  parent[neighbor] = current_sector;

	  // Enqueue: Check for capacity and reallocate if necessary
	  if (queue_tail == queue_capacity)
	    {
	      queue_capacity *= 2;
	      int *new_queue = realloc (queue, queue_capacity * sizeof (int));
	      if (!new_queue)
		{
		  // LOGE("Pathfind: Realloc failed for queue expansion."); // Assuming LOGE is available
		  path_found = 0;	// Failure state
		  queue_tail = 0;	// Stop loop
		  break;
		}
	      queue = new_queue;
	    }
	  queue[queue_tail++] = neighbor;
	}

      sqlite3_reset (warp_st);
    }

  sqlite3_finalize (warp_st);
  free (queue);

  // --- 3. Path Reconstruction ---

  if (!path_found)
    {
      free (parent);
      return NULL;
    }

  // Count path length
  int path_length = 0;
  int temp_sector = end_sector;
  while (temp_sector != -1)
    {
      path_length++;
      // Use a safety break in case of cycle/corrupt parent pointers
      if (path_length > max_sector_id + 1)
	{
	  // LOGE("Pathfind: Cycle detected during reconstruction. Aborting."); // Assuming LOGE is available
	  free (parent);
	  return NULL;
	}
      temp_sector = parent[temp_sector];
    }

  // Allocate result and fill in reverse order
  int *result_path = malloc ((path_length + 1) * sizeof (int));	// +1 for terminator
  if (!result_path)
    {
      // LOGE("Pathfind: Failed to allocate result path."); // Assuming LOGE is available
      free (parent);
      return NULL;
    }

  int i = path_length - 1;
  temp_sector = end_sector;
  while (temp_sector != -1)
    {
      result_path[i--] = temp_sector;
      temp_sector = parent[temp_sector];
    }
  result_path[path_length] = 0;	// Null terminator

  free (parent);
  return result_path;
}

/**
 * @brief Helper function to perform pathfinding and insert the path sectors.
 *
 * @param db The SQLite database handle.
 * @param insert_st The prepared INSERT statement for the MSL table.
 * @param start_sector Starting sector ID.
 * @param end_sector Destination sector ID.
 * @param avoid_list Sectors to avoid.
 * @param total_unique_sectors_added Pointer to counter for logging.
 */
static void
_insert_path_sectors (sqlite3 *db, sqlite3_stmt *insert_st,
		      int start_sector, int end_sector,
		      const int *avoid_list, int *total_unique_sectors_added)
{
  int rc;
  int *s;
  int *current_path =
    universe_pathfind_get_sectors (db, start_sector, end_sector, avoid_list);

  if (!current_path)
    {
      LOGW ("Could not find path from %d to %d. Check universe connections.",
	    start_sector, end_sector);
      return;
    }

  // Insert Path Sectors into Table
  for (s = current_path; *s != 0; s++)
    {

      sqlite3_reset (insert_st);
      sqlite3_bind_int (insert_st, 1, *s);

      rc = sqlite3_step (insert_st);

      if (rc == SQLITE_DONE)
	{
	  // If the sector was newly inserted (not ignored)
	  if (sqlite3_changes (db) > 0)
	    {
	      (*total_unique_sectors_added)++;
	    }
	}
      else
	{
	  LOGW ("SQL warning inserting sector %d for path %d->%d: %s",
		*s, start_sector, end_sector, sqlite3_errmsg (db));
	}
    }

  free (current_path);
}

/**
 * @brief Populates the MSL table (MSL_TABLE_NAME) with 
 * all sectors on paths between FedSpace sectors (1-10) and all Stardock sectors.
 *
 * @param db The SQLite database handle.
 * @return 0 on success, -1 on database error.
 */
int
populate_msl_if_empty (sqlite3 *db)
{
  const int *avoid_list = NULL;	// No sectors to avoid for this "safe" path list
  int rc;
  int total_sectors_in_table = 0;
  char sql_buffer[256];

  // --- 1. Check if the table is empty ---
  snprintf (sql_buffer, sizeof (sql_buffer),
	    "SELECT COUNT(sector_id) FROM %s;", MSL_TABLE_NAME);

  sqlite3_stmt *count_st = NULL;

  if (sqlite3_prepare_v2 (db, sql_buffer, -1, &count_st, NULL) == SQLITE_OK &&
      sqlite3_step (count_st) == SQLITE_ROW)
    {
      total_sectors_in_table = sqlite3_column_int (count_st, 0);
    }
  sqlite3_finalize (count_st);

  if (total_sectors_in_table > 0)
    {
      // Logging the correct table name now
      LOGI
	("%s table already populated with %d entries. Skipping MSL calculation.",
	 MSL_TABLE_NAME, total_sectors_in_table);
      return 0;
    }

  LOGI
    ("%s table is empty. Starting comprehensive MSL path calculation (FedSpace 1-10 <-> Stardocks)...",
     MSL_TABLE_NAME);

  // --- 2. Create the table (if it doesn't exist) ---
  snprintf (sql_buffer, sizeof (sql_buffer),
	    "CREATE TABLE IF NOT EXISTS %s ("
	    "  sector_id INTEGER PRIMARY KEY" ");", MSL_TABLE_NAME);

  if (sqlite3_exec (db, sql_buffer, NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error creating %s table: %s", MSL_TABLE_NAME,
	    sqlite3_errmsg (db));
      return -1;
    }

  // --- 3. Prepare Select and Insert Statements and collect Stardocks ---

  // Select all Stardock sector IDs from the specified table.
  sqlite3_stmt *select_st = NULL;
  const char *sql_select_stardocks =
    "SELECT sector_id FROM stardock_location;";
  rc = sqlite3_prepare_v2 (db, sql_select_stardocks, -1, &select_st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error preparing Stardock select: %s", sqlite3_errmsg (db));
      return -1;
    }

  // Collect stardock sectors into a dynamically sized array
  int *stardock_sectors = NULL;
  int stardock_count = 0;
  int stardock_capacity = 8;
  stardock_sectors = malloc (stardock_capacity * sizeof (int));
  if (!stardock_sectors)
    {
      LOGE ("Failed to allocate stardock sector array.");
      sqlite3_finalize (select_st);
      return -1;
    }

  // Fetch all stardock IDs
  while (sqlite3_step (select_st) == SQLITE_ROW)
    {
      int id = sqlite3_column_int (select_st, 0);
      if (stardock_count == stardock_capacity)
	{
	  stardock_capacity *= 2;
	  int *new_arr =
	    realloc (stardock_sectors, stardock_capacity * sizeof (int));
	  if (!new_arr)
	    {
	      LOGE ("Failed to reallocate stardock sector array.");
	      free (stardock_sectors);
	      sqlite3_finalize (select_st);
	      return -1;
	    }
	  stardock_sectors = new_arr;
	}
      stardock_sectors[stardock_count++] = id;
    }
  sqlite3_finalize (select_st);

  if (stardock_count == 0)
    {
      LOGW
	("No stardock locations found in stardock_location table. Skipping MSL calculation.");
      free (stardock_sectors);
      return 0;
    }

  // Prepare Insert statement
  sqlite3_stmt *insert_st = NULL;
  snprintf (sql_buffer, sizeof (sql_buffer),
	    "INSERT OR IGNORE INTO %s (sector_id) VALUES (?);",
	    MSL_TABLE_NAME);
  rc = sqlite3_prepare_v2 (db, sql_buffer, -1, &insert_st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error preparing insert statement for %s: %s", MSL_TABLE_NAME,
	    sqlite3_errmsg (db));
      free (stardock_sectors);
      return -1;
    }

  // --- 4. Iterate and Calculate Paths (Two-Way) ---

  // Start with a master transaction for the entire population process
  if (sqlite3_exec (db, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error starting master transaction: %s", sqlite3_errmsg (db));
      sqlite3_finalize (insert_st);
      free (stardock_sectors);
      return -1;
    }

  int total_unique_sectors_added = 0;

  // Loop through FedSpace starting sectors (1 through 10)
  for (int start_sector = FEDSPACE_SECTOR_START;
       start_sector <= FEDSPACE_SECTOR_END; start_sector++)
    {
      // Loop through all collected Stardock sectors
      for (int i = 0; i < stardock_count; i++)
	{
	  int stardock_id = stardock_sectors[i];

	  // A. Path FedSpace -> Stardock (e.g., 1 -> 160)
	  LOGI ("Calculating path %d -> %d", start_sector, stardock_id);
	  _insert_path_sectors (db, insert_st, start_sector, stardock_id,
				avoid_list, &total_unique_sectors_added);

	  // B. Path Stardock -> FedSpace (e.g., 160 -> 1)
	  // Only calculate reverse if the start/end sectors are different
	  if (start_sector != stardock_id)
	    {
	      LOGI ("Calculating path %d -> %d (Reverse)", stardock_id,
		    start_sector);
	      _insert_path_sectors (db, insert_st, stardock_id, start_sector,
				    avoid_list, &total_unique_sectors_added);
	    }
	}
    }

  // --- 5. Finalize and Commit ---

  sqlite3_finalize (insert_st);
  free (stardock_sectors);

  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error committing master path transaction: %s",
	    sqlite3_errmsg (db));
      return -1;
    }

  LOGI ("Completed MSL setup. Populated %s with %d total unique sectors.",
	MSL_TABLE_NAME, total_unique_sectors_added);

  return 0;
}



/**
 * @brief Resets the daily turns remaining for all players to the maximum configured amount.
 * * This function iterates over all players in the 'turns' table and sets their 
 * 'turns_remaining' back to the 'turnsperday' value found in the 'config' table.
 * It is designed to be run as a daily cron job, hence its signature (sqlite3 *db, int64_t now_s).
 * * @param db The SQLite database handle.
 * @param now_s The current time in seconds (passed by the cron runner).
 * @return 0 on success, -1 on database error or if configuration is missing.
 */
int
h_reset_turns_for_player (sqlite3 *db, int64_t now_s)
{
  sqlite3_stmt *select_st = NULL;
  sqlite3_stmt *update_st = NULL;
  int max_turns = 0;
  int rc;
  int updated_count = 0;

  // --- 1. Get Maximum Turns Per Day from Config ---
  const char *sql_config = "SELECT turnsperday FROM config WHERE id = 1;";
  rc = sqlite3_prepare_v2 (db, sql_config, -1, &select_st, NULL);
  if (rc == SQLITE_OK && sqlite3_step (select_st) == SQLITE_ROW)
    {
      max_turns = sqlite3_column_int (select_st, 0);
    }
  sqlite3_finalize (select_st);
  select_st = NULL;		// Reset to NULL for safety

  if (max_turns <= 0)
    {
      // Use LOGE for a critical failure that prevents the job from running
      LOGE ("Turn reset failed: turnsperday is %d or missing in config.",
	    max_turns);
      return -1;
    }

  // --- 2. Prepare Statements and Start Transaction ---

  // Select all player IDs that have turn records
  const char *sql_select_players = "SELECT player FROM turns;";
  rc = sqlite3_prepare_v2 (db, sql_select_players, -1, &select_st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error preparing player select: %s", sqlite3_errmsg (db));
      return -1;
    }

  // Update player's turns
  const char *sql_update =
    "UPDATE turns SET turns_remaining = ?, last_update = ? WHERE player = ?;";
  rc = sqlite3_prepare_v2 (db, sql_update, -1, &update_st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("SQL error preparing turns update: %s", sqlite3_errmsg (db));
      sqlite3_finalize (select_st);
      return -1;
    }

  // Start Transaction for performance and atomicity
  if (sqlite3_exec (db, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error starting transaction: %s", sqlite3_errmsg (db));
      sqlite3_finalize (select_st);
      sqlite3_finalize (update_st);
      return -1;
    }

  // --- 3. Iterate and Update ---

  while (sqlite3_step (select_st) == SQLITE_ROW)
    {
      int player_id = sqlite3_column_int (select_st, 0);

      // Bind parameters to the prepared UPDATE statement
      sqlite3_reset (update_st);
      sqlite3_bind_int (update_st, 1, max_turns);
      sqlite3_bind_int64 (update_st, 2, now_s);	// Use cron-provided time
      sqlite3_bind_int (update_st, 3, player_id);

      rc = sqlite3_step (update_st);

      if (rc == SQLITE_DONE)
	{
	  updated_count++;
	}
      else
	{
	  // Use LOGE for a failure within the transaction that needs attention
	  LOGE ("SQL error executing turns update for player %d: %s",
		player_id, sqlite3_errmsg (db));
	  // Note: The transaction continues, but this failure is severe.
	}
    }

  // --- 4. Finalize and Commit ---

  sqlite3_finalize (select_st);
  sqlite3_finalize (update_st);

  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("SQL error committing transaction: %s", sqlite3_errmsg (db));
      return -1;
    }

  // Use LOGI for successful job completion
  LOGI ("Successfully reset turns for %d players to %d.", updated_count,
	max_turns);

  return 0;
}


static int
try_lock (sqlite3 *db, const char *name, int64_t now_s)
{
  sqlite3_stmt *st = NULL;
  int rc;

  /* Lock duration: 60 seconds (in seconds, matching now_s) */
  const int LOCK_DURATION_S = 60;
  int64_t until_s = now_s + LOCK_DURATION_S;

  // 1. --- STALE LOCK CLEANUP ---
  // Delete any lock that has ALREADY EXPIRED (until_ms <= now_s).
  // Note: Since your schema uses 'until_ms' (INTEGER) but you pass 'now_s' (seconds),
  // we will assume the INTEGER column stores SECONDS for now, and adjust the column names.

  rc = sqlite3_prepare_v2 (db,
			   "DELETE FROM locks WHERE lock_name=?1 AND until_ms < ?2;",
			   -1, &st, NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
      sqlite3_bind_int64 (st, 2, now_s);	// Use current time as the limit
      sqlite3_step (st);	// Execute the deletion of the stale lock
    }
  sqlite3_finalize (st);
  if (rc != SQLITE_OK)
    return 0;			// Abort on SQL error during cleanup

  // 2. --- LOCK ACQUISITION ---
  rc = sqlite3_prepare_v2 (db,
			   "INSERT INTO locks(lock_name, owner, until_ms) VALUES(?1, 'server', ?2) "
			   "ON CONFLICT(lock_name) DO NOTHING;", -1, &st,
			   NULL);
  if (rc != SQLITE_OK)
    return 0;

  sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
  // Bind the calculated expiration time (now_s + 60s)
  sqlite3_bind_int64 (st, 2, until_s);

  sqlite3_step (st);
  sqlite3_finalize (st);

  /* 3. Check we own it now (The original logic for checking ownership is still valid) */
  rc = sqlite3_prepare_v2 (db,
			   "SELECT owner FROM locks WHERE lock_name=?1;", -1,
			   &st, NULL);
  if (rc != SQLITE_OK)
    return 0;

  sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
  int ok = 0;
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      const unsigned char *o = sqlite3_column_text (st, 0);
      ok = (o && strcmp ((const char *) o, "server") == 0);
    }
  sqlite3_finalize (st);
  return ok;
}



/// Returns 0 if lock is free, or the 'until_ms' timestamp if locked.
int64_t
db_lock_status (sqlite3 *db, const char *name)
{
  const char *SQL = "SELECT until_ms FROM locks WHERE lock_name = ?1;";
  sqlite3_stmt *st = NULL;
  int64_t until_ms = 0;

  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
      if (sqlite3_step (st) == SQLITE_ROW)
	{
	  // Note: assuming until_ms is stored in milliseconds (int64)
	  until_ms = sqlite3_column_int64 (st, 0);
	}
      sqlite3_finalize (st);
    }
  return until_ms;
}

static void
unlock (sqlite3 *db, const char *name)
{
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db, "DELETE FROM locks WHERE name=?1 AND owner='server';", -1, &st,
       NULL) == SQLITE_OK)
    {
      sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }
}


void
cron_register_builtins (void)
{
  g_reg_inited = 1;
}

/* cron_handler_fn cron_find(const char *name) { */
/*   if (!g_reg_inited || !name) return NULL; */
/*   for (unsigned i = 0; i < sizeof(REG)/sizeof(REG[0]); ++i) { */
/*     if (strcmp(REG[i].name, name) == 0) return REG[i].fn; */
/*   } */
/*   return NULL; */
/* } */

/* Helpers */
static int
begin (sqlite3 *db)
{
  return sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
}

static int
commit (sqlite3 *db)
{
  return sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
}

static int
rollback (sqlite3 *db)
{
  return sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
}

// --- Helper function to get readable asset name ---
const char *
get_asset_name (int type)
{
  switch (type)
    {
    case 3:
      return "Mine";
    case 2:
      return "Fighter";
    case 1:
      return "Beacon";
    default:
      return "Unknown Asset";
    }
}


/*
 * Helper to log a FedSpace action (tow or asset clear) to engine_events.
 * * NOTE: Assumes db_mutex is already held by the caller (h_fedspace_cleanup).
 */
int
h_log_fedspace_event (sqlite3 *db, int64_t ts, const char *type, 
                      int actor_id, int target_id, int ship_id, 
                      int sector_id, const char *reason)
{
    int rc = SQLITE_ERROR;
    json_t *payload = NULL;
    char *payload_str = NULL;
    sqlite3_stmt *stmt = NULL;

    // --- 1. Build JSON Payload ---
    payload = json_object ();

    if (!payload) {
        LOGE ("h_log_fedspace_event: Failed to create JSON object.");
        return rc;
    }

    // Add reason/context to the payload
    json_object_set_new (payload, "reason", json_string(reason));
    
    // Dump the JSON to a string
    payload_str = json_dumps (payload, JSON_COMPACT);
    if (!payload_str) {
        LOGE ("h_log_fedspace_event: Failed to dump JSON to string.");
        goto cleanup;
    }

    // --- 2. Prepare SQL statement ---
    const char *sql_insert = 
        "INSERT INTO engine_events (ts, type, payload, actor_player_id, sector_id) "
        "VALUES (?1, ?2, ?3, ?4, ?5);";

    rc = sqlite3_prepare_v2 (db, sql_insert, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGE ("h_log_fedspace_event SQL prepare failed: %s", sqlite3_errmsg(db));
        goto cleanup;
    }

    // --- 3. Bind values ---
    sqlite3_bind_int64 (stmt, 1, ts);
    sqlite3_bind_text  (stmt, 2, type, -1, SQLITE_STATIC);
    sqlite3_bind_text  (stmt, 3, payload_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (stmt, 4, actor_id);
    sqlite3_bind_int   (stmt, 5, sector_id);


    // --- 4. Execute and Finalize ---
    if (sqlite3_step (stmt) != SQLITE_DONE) {
        LOGE ("h_log_fedspace_event SQL execute failed: %s", sqlite3_errmsg(db));
        rc = SQLITE_ERROR; // Ensure error is returned
    } else {
        rc = SQLITE_OK;
    }

cleanup:

    sqlite3_finalize (stmt);
    json_decref (payload);
    free (payload_str); // Free the string created by json_dumps

    return rc;
}



#include <stdio.h>
#include <sqlite3.h>
#include <string.h>

// --- Callback function to process each row ---
static int ship_callback(void *count_ptr, int argc, char **argv, char **azColName) {
    int *count = (int *)count_ptr;
    
    // Check if the 'cloaked' value exists and print it
    if (argv[0]) {
        printf("Found cloaked ship (Timestamp: %s)\n", argv[0]);
        (*count)++;
    }
    
    return 0; // Return 0 to continue execution
}


// --- Function to execute the query ---
int uncloak_ships_in_fedspace(sqlite3 *db) {
    int rc;
    char *err_msg = 0;
    int cloaked_ship_count = 0;

    // The corrected SQL query
    const char *sql = 
        "UPDATE ships SET cloaked=NULL "
        "WHERE cloaked IS NOT NULL "
        "  AND ( "
        "    location IN (SELECT sector_id FROM stardock_location) "
        "    OR location IN (1, 2, 3, 4, 5, 6, 7, 8, 9, 10) "
        "  );";

    //printf("Executing SQL query...\n");

    rc = sqlite3_exec(db, sql, ship_callback, &cloaked_ship_count, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return -1;
    } 

    // printf("Query finished. Total cloaked ships found: %d\n", cloaked_ship_count);
    return cloaked_ship_count;
}


  // 1. Check anyone who has been in fedspace for more than an hour.
  // 2. Check for Evil alignment who are in that list and tow them
  // 3. Check for ships which have more than 50 fighters in that list and tow them
  // 4. Check for players with more than 1000 EP in that list and tow them
  // 5. Check for ships with no player inside (or no owner) and confiscate. (These can be offered for sale at Stardock later.) 
  // 6. Overcrowding, more than 5 ships parked in fedspace, tow them to another fedspace sector if possible, otherwise tow to random.

////////////////////////////////////////////////////////

int
h_fedspace_cleanup (sqlite3 *db, int64_t now_s)
{

  int fedadmin = 2;
  int64_t now_ms = now_s * 1000;
  sqlite3_stmt *select_stmt = NULL;
  sqlite3_stmt *delete_stmt = NULL;
  sqlite3_stmt *sector_stmt = NULL;
  int cleared_assets = 0;
  int tows = 0;
  int rc;
  char *err_msg = NULL;

  if (!try_lock (db, "fedspace_cleanup", now_ms))
    {
      int64_t until_ms = db_lock_status (db, "fedspace_cleanup");
      int64_t time_left_s = (until_ms - now_ms) / 1000;
      if (until_ms > now_ms)
	{
	  LOGW
	    ("fedspace_cleanup: FAILED to acquire lock. Still held for %lld more seconds.",
	     (long long) time_left_s);
	}
      else
	{
	  LOGW
	    ("fedspace_cleanup: FAILED to acquire lock. Lock is stale (Expires at %lld).",
	     (long long) until_ms);
	}
      return 0;
    }
  else
    {
      LOGI ("fedspace_cleanup: Lock acquired, starting cleanup operations.");
    }

  // Uncloak people in fedspace before towing. Also it will waste one cloaking device
  // for anyone stupid enough to cloak in fedspace. lol
  int uncloak = uncloak_ships_in_fedspace(db);

  // --- STEP 1: Ensure MSL table is populated ---
  if (populate_msl_if_empty (db) != 0)
    {
      LOGE ("fedspace_cleanup: MSL population failed. Aborting cleanup.");
    }

  // --- MSL Asset Clearing Logic (Using select_stmt, delete_stmt) ---
  const char *select_assets_sql =
    "SELECT player, asset_type, sector, quantity "
    "FROM sector_assets "
    "WHERE sector IN (SELECT sector_id FROM msl_sectors) AND player != 0;";

  rc = sqlite3_prepare_v2 (db, select_assets_sql, -1, &select_stmt, NULL);
  if (rc == SQLITE_OK)
    {
      char message[256];
      const char *delete_sql =
	"DELETE FROM sector_assets WHERE player = ?1 AND asset_type = ?2 AND sector = ?3 AND quantity = ?4;";

      rc = sqlite3_prepare_v2 (db, delete_sql, -1, &delete_stmt, NULL);

      if (rc == SQLITE_OK)
	{
	  while ((rc = sqlite3_step (select_stmt)) == SQLITE_ROW)
	    {
	      int player_id = sqlite3_column_int (select_stmt, 0);
	      int asset_type = sqlite3_column_int (select_stmt, 1);
	      int sector_id = sqlite3_column_int (select_stmt, 2);
	      int quantity = sqlite3_column_int (select_stmt, 3);

	      snprintf (message, sizeof (message),
			"%d %s(s) deployed in Sector %d (Major Space Lane) were destroyed by Federal Authorities. Deployments in MSL are strictly prohibited.",
			quantity, get_asset_name (asset_type), sector_id);
	      h_send_message_to_player (player_id, fedadmin,
					"WARNING: MSL Violation", message);

	      h_log_fedspace_event(db, now_s, "fedspace:asset_cleared", fedadmin, player_id, 
                     0, sector_id, "MSL_VIOLATION"); 
	      
	      sqlite3_reset (delete_stmt);
	      sqlite3_bind_int (delete_stmt, 1, player_id);
	      sqlite3_bind_int (delete_stmt, 2, asset_type);
	      sqlite3_bind_int (delete_stmt, 3, sector_id);
	      sqlite3_bind_int (delete_stmt, 4, quantity);
	      if (sqlite3_step (delete_stmt) == SQLITE_DONE)
		cleared_assets++;
	    }
	  sqlite3_finalize (delete_stmt);
	  delete_stmt = NULL;
	}
      else
	{
	  LOGE ("h_fedspace_cleanup: MSL DELETE prepare failed: %s",
		sqlite3_errmsg (db));
	}
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }
  else
    {
      LOGE ("h_fedspace_cleanup: MSL SELECT prepare failed: %s",
	    sqlite3_errmsg (db));
    }

  if (cleared_assets > 0)
    {
      LOGI
	("fedspace_cleanup: Completed asset clearing with %d assets cleared.",
	 cleared_assets);
    }

  // --- START TOWING TRANSACTION ---
  rc = begin (db);
  if (rc != 0)
    {
      unlock (db, "fedspace_cleanup");
      return rc;
    }

  //////////////////////////////////////////////////////////////////
  // I. Update LOGGED-IN Status (60-minute timeout)             //
  //////////////////////////////////////////////////////////////////

  // FIX: Using 'last_update' and 'loggedin'.
  const char *sql_timeout_logout =
    "UPDATE players SET loggedin = 0 "
    "WHERE loggedin = 1 AND ?1 - last_update > ?2;";

  if (sqlite3_prepare_v2 (db, sql_timeout_logout, -1, &select_stmt, NULL) ==
      SQLITE_OK)
    {
      sqlite3_bind_int64 (select_stmt, 1, now_s);
      sqlite3_bind_int (select_stmt, 2, LOGOUT_TIMEOUT_S);
      rc = sqlite3_step (select_stmt);
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;

      if (rc != SQLITE_DONE)
	{
	  LOGE ("h_fedspace_cleanup: LOGOUT failed: %d", rc);
	}
    }
  else
    {
      LOGE ("h_fedspace_cleanup: LOGOUT prepare failed: %s",
	    sqlite3_errmsg (db));
    }

//////////////////////////////////////////////////////////////////
  // II. Setup Permanent Eligible Ships Table                     //
  //////////////////////////////////////////////////////////////////

  // 1. Ensure the permanent table exists (run once)
  const char *sql_create_eligible_table =
    "CREATE TABLE IF NOT EXISTS eligible_tows (ship_id INTEGER PRIMARY KEY, sector_id INTEGER, owner_id INTEGER, fighters INTEGER, alignment INTEGER, experience INTEGER);";

  rc = sqlite3_exec (db, sql_create_eligible_table, NULL, NULL, &err_msg);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_fedspace_cleanup: PERM TABLE creation failed: %s", err_msg);
      sqlite3_free (err_msg);
      rollback (db);
      unlock (db, "fedspace_cleanup");
      return -1;
    }

  // 2. CRITICAL: Empty the table for the current run
  // This replaces the old temporary table logic and should not be locked.
  rc = sqlite3_exec (db, "DELETE FROM eligible_tows", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_fedspace_cleanup: DELETE TABLE failed: %s", err_msg);
      sqlite3_free (err_msg);
      rollback (db);
      unlock (db, "fedspace_cleanup");
      return -1;
    }

  // 3. Populate the table (Now an INSERT INTO SELECT)
  const char *sql_insert_eligible = "INSERT INTO eligible_tows (ship_id, sector_id, owner_id, fighters, alignment, experience) " "SELECT T1.id, T1.location, T2.id, T1.fighters, " "       COALESCE(T2.alignment, 0), "	// Use 0 if alignment is NULL
    "       COALESCE(T2.experience, 0) "	// Use 0 if experience is NULL
    "FROM ships T1 " "LEFT JOIN players T2 ON T1.id = T2.ship " "WHERE T1.location BETWEEN ?1 AND ?2 " "  AND (T2.loggedin = 0 OR T2.id IS NULL) "	// Filter by loggedin status or no owner
    "ORDER BY T1.id ASC;";
  if (sqlite3_prepare_v2 (db, sql_insert_eligible, -1, &select_stmt, NULL) ==
      SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, FEDSPACE_SECTOR_START);
      sqlite3_bind_int (select_stmt, 2, FEDSPACE_SECTOR_END);
      sqlite3_step (select_stmt);
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }

  

  //////////////////////////////////////////////////////////////////
  // III. Towing Rules (2-5)                                      //
  //////////////////////////////////////////////////////////////////

  // Prepare DELETE statement once for all rules
  const char *sql_delete_eligible =
    "DELETE FROM eligible_tows WHERE ship_id = ?;";
  if (sqlite3_prepare_v2 (db, sql_delete_eligible, -1, &delete_stmt, NULL) !=
      SQLITE_OK)
    {
      LOGE ("h_fedspace_cleanup: DELETE prepare failed: %s",
	    sqlite3_errmsg (db));
      delete_stmt = NULL;
    }

  // --- Rule 2: Evil alignment ---
  const char *sql_evil_alignment =
    "SELECT ship_id FROM eligible_tows WHERE owner_id IS NOT NULL AND alignment < 0 LIMIT ?1;";
  if (sqlite3_prepare_v2 (db, sql_evil_alignment, -1, &select_stmt, NULL) ==
      SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, MAX_TOWS_PER_PASS - tows);
      while (sqlite3_step (select_stmt) == SQLITE_ROW
	     && tows < MAX_TOWS_PER_PASS)
	{
	  int ship_id = sqlite3_column_int (select_stmt, 0);
	  tow_ship (db, ship_id, get_random_sector (db), fedadmin,
		    REASON_EVIL_ALIGN);
	  
	  if (delete_stmt)
	    {
	      sqlite3_bind_int (delete_stmt, 1, ship_id);
	      sqlite3_step (delete_stmt);
	      sqlite3_reset (delete_stmt);
	    }
	  tows++;
	}
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }


  // --- Rule 3: Excess Fighters ---
  const char *sql_excess_fighters =
    "SELECT ship_id FROM eligible_tows WHERE fighters > ?1 LIMIT ?2;";
  if (sqlite3_prepare_v2 (db, sql_excess_fighters, -1, &select_stmt, NULL) ==
      SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, 49);
      sqlite3_bind_int (select_stmt, 2, MAX_TOWS_PER_PASS - tows);
      while (sqlite3_step (select_stmt) == SQLITE_ROW
	     && tows < MAX_TOWS_PER_PASS)
	{
	  int ship_id = sqlite3_column_int (select_stmt, 0);
	  tow_ship (db, ship_id, get_random_sector (db), fedadmin,
		    REASON_EXCESS_FIGHTERS);

	  if (delete_stmt)
	    {
	      sqlite3_bind_int (delete_stmt, 1, ship_id);
	      sqlite3_step (delete_stmt);
	      sqlite3_reset (delete_stmt);
	    }
	  tows++;
	}
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }

  // --- Rule 4: High Experience ---
  const char *sql_high_exp =
    "SELECT ship_id FROM eligible_tows WHERE owner_id IS NOT NULL AND experience >= 1000 LIMIT ?1;";
  if (sqlite3_prepare_v2 (db, sql_high_exp, -1, &select_stmt, NULL) ==
      SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, MAX_TOWS_PER_PASS - tows);
      while (sqlite3_step (select_stmt) == SQLITE_ROW
	     && tows < MAX_TOWS_PER_PASS)
	{
	  int ship_id = sqlite3_column_int (select_stmt, 0);
	  tow_ship (db, ship_id, get_random_sector (db), fedadmin,
		    REASON_HIGH_EXP);

	  if (delete_stmt)
	    {
	      sqlite3_bind_int (delete_stmt, 1, ship_id);
	      sqlite3_step (delete_stmt);
	      sqlite3_reset (delete_stmt);
	    }
	  tows++;
	}
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }

  // --- Rule 5: No Owner (Confiscation) ---
  // The query should only target truly unowned ships.
  const char *sql_no_owner =
    "SELECT ship_id FROM eligible_tows WHERE owner_id IS NULL LIMIT ?1;";
  // This looks correct and will only catch ships where the LEFT JOIN returned NULL for T2.id.
  if (sqlite3_prepare_v2 (db, sql_no_owner, -1, &select_stmt, NULL) ==
      SQLITE_OK)
    {
      sqlite3_bind_int (select_stmt, 1, MAX_TOWS_PER_PASS - tows);
      while (sqlite3_step (select_stmt) == SQLITE_ROW
	     && tows < MAX_TOWS_PER_PASS)
	{
	  int ship_id = sqlite3_column_int (select_stmt, 0);
	  tow_ship (db, ship_id, CONFISCATION_SECTOR, fedadmin,
		    REASON_NO_OWNER);

	  if (delete_stmt)
	    {
	      sqlite3_bind_int (delete_stmt, 1, ship_id);
	      sqlite3_step (delete_stmt);
	      sqlite3_reset (delete_stmt);
	    }
	  tows++;
	}
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }

  //////////////////////////////////////////////////////////////////
  // IV. Overcrowding Check (Rule 6)                              //
  //////////////////////////////////////////////////////////////////

  // 1. Select Overcrowded Sectors (in FedSpace, 1-10)
  const char *sql_overcrowded_sectors =
    "SELECT T1.sector_id, COUNT(T1.ship_id) AS ship_count "
    "FROM eligible_tows T1 "
    "WHERE T1.sector_id BETWEEN ?1 AND ?2 "
    "GROUP BY T1.sector_id "
    "HAVING COUNT(T1.ship_id) > ?3 " "ORDER BY T1.sector_id ASC;";

  if (tows < MAX_TOWS_PER_PASS
      && sqlite3_prepare_v2 (db, sql_overcrowded_sectors, -1, &sector_stmt,
			     NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (sector_stmt, 1, FEDSPACE_SECTOR_START);
      sqlite3_bind_int (sector_stmt, 2, FEDSPACE_SECTOR_END);
      sqlite3_bind_int (sector_stmt, 3, MAX_SHIPS_PER_FED_SECTOR);

      // Loop through overcrowded sectors
      while (sqlite3_step (sector_stmt) == SQLITE_ROW
	     && tows < MAX_TOWS_PER_PASS)
	{
	  int sector_id = sqlite3_column_int (sector_stmt, 0);
	  int ship_count = sqlite3_column_int (sector_stmt, 1);

	  int excess_ships = ship_count - MAX_SHIPS_PER_FED_SECTOR;
	  int to_tow =
	    (MAX_TOWS_PER_PASS - tows <
	     excess_ships) ? (MAX_TOWS_PER_PASS - tows) : excess_ships;

	  // 2. Select the excess ships (tow the newest ones first)
	  const char *sql_overcrowded_ships =
	    "SELECT ship_id FROM eligible_tows "
	    "WHERE sector_id = ?1 " "ORDER BY ship_id DESC " "LIMIT ?2;";

	  if (sqlite3_prepare_v2
	      (db, sql_overcrowded_ships, -1, &select_stmt,
	       NULL) == SQLITE_OK)
	    {
	      sqlite3_bind_int (select_stmt, 1, sector_id);
	      sqlite3_bind_int (select_stmt, 2, to_tow);

	      // Loop through the excess ships and tow them
	      while (sqlite3_step (select_stmt) == SQLITE_ROW)
		{
		  int ship_id = sqlite3_column_int (select_stmt, 0);
		  int new_sector = get_random_sector (db);
		  tow_ship (db, ship_id, new_sector, fedadmin,
			    REASON_OVERCROWDING);

		  if (delete_stmt)
		    {
		      sqlite3_bind_int (delete_stmt, 1, ship_id);
		      sqlite3_step (delete_stmt);
		      sqlite3_reset (delete_stmt);
		    }
		  tows++;
		}
	      sqlite3_finalize (select_stmt);
	      select_stmt = NULL;	// Crucial: Reset pointer after finalization
	    }
	}
      sqlite3_finalize (sector_stmt);
      sector_stmt = NULL;	// Crucial: Reset pointer after finalization
    }

  // select cloaked from ships where cloaked != NULL and location = fedspace;
  
  
// --- CRITICAL FINAL CLEANUP ---
  // Finalize any lingering prepared statement pointers to release any possible lock
  if (delete_stmt)
    {
      sqlite3_finalize (delete_stmt);
      delete_stmt = NULL;
    }
  if (select_stmt)
    {
      sqlite3_finalize (select_stmt);
      select_stmt = NULL;
    }
  if (sector_stmt)
    {
      sqlite3_finalize (sector_stmt);
      sector_stmt = NULL;
    }


  // The final cleanup is now a DELETE (already done in II.2, but harmless repetition)
  // If the original TEMP table existed, this command is now harmlessly deleting rows 
  // from the permanent table. NO DROP TABLE COMMAND HERE.
  rc = sqlite3_exec (db, "DELETE FROM eligible_tows", NULL, NULL, &err_msg);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_fedspace_cleanup: Final DELETE failed: %s", err_msg);
      sqlite3_free (err_msg);
    }


  

  // Commit the main transaction
  commit (db);
  LOGI ("fedspace_cleanup: ok (towed=%d)", tows);
  unlock (db, "fedspace_cleanup");
  return 0;
}

////////////////////////////////////////////////////////

int
h_daily_turn_reset (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "daily_turn_reset", now_s))
    return 0;

  LOGI ("daily_turn_reset: starting daily turn reset.");

  int rc = begin (db);
  if (rc)
    {
      unlock (db, "daily_turn_reset");
      return rc;
    }

  // 1. Reset all player's available turns to the daily maximum.
  rc = sqlite3_exec (db,
		     "UPDATE turns SET turns_remaining = (SELECT turnsperday FROM config WHERE id=1);",
		     NULL, NULL, NULL);

  if (rc != SQLITE_OK)
    {
      LOGE ("daily_turn_reset: player turn update failed: %s",
	    sqlite3_errmsg (db));
      rollback (db);
      unlock (db, "daily_turn_reset");
      return rc;
    }

  // 2. Perform any other daily cleanup/reset logic here (e.g., daily events, stats reset).

  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("daily_turn_reset: commit failed: %s", sqlite3_errmsg (db));
    }

  //LOGI ("daily_turn_reset: ok");
  unlock (db, "daily_turn_reset");
  return rc;
}


int
h_autouncloak_sweeper (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "autouncloak_sweeper", now_s))
    return 0;

  sqlite3_stmt *st = NULL;	// Statement for UPDATE
  sqlite3_stmt *q_ccap = NULL;	// Statement for SELECT config
  int rc;
  int max_hours = 0;

  // 1. Get the max cloaking duration from the config table.
  rc = sqlite3_prepare_v2 (db,
			   "SELECT max_cloak_duration FROM config;",
			   -1, &q_ccap, NULL);

  if (rc != SQLITE_OK)
    {
      LOGE ("Can't prepare config SELECT: %s", sqlite3_errmsg (db));
      unlock (db, "autouncloak_sweeper");
      return 0;			// Exit early on error
    }

  if (sqlite3_step (q_ccap) == SQLITE_ROW)
    {
      max_hours = sqlite3_column_int (q_ccap, 0);	// Get the 25 hours
    }

  sqlite3_finalize (q_ccap);	// Cleanup SELECT statement

  // If max_hours is zero or unset, skip the sweep (optional safety check)
  if (max_hours <= 0)
    {
      LOGI
	("autouncloak_sweeper: max_cloak_duration is zero/invalid. Skipping sweep.");
      unlock (db, "autouncloak_sweeper");
      return 0;
    }

  // 2. Calculate the uncloak threshold in seconds
  const int SECONDS_IN_HOUR = 3600;
  int64_t max_duration_seconds = (int64_t) max_hours * SECONDS_IN_HOUR;
  int64_t uncloak_threshold_s = now_s - max_duration_seconds;

  // 3. Prepare and execute the UPDATE statement
  rc = sqlite3_prepare_v2 (db,
			   "UPDATE ships "
			   "SET cloaked = NULL "
			   "WHERE cloaked IS NOT NULL AND cloaked < ?;",
			   -1, &st, NULL);

  if (rc == SQLITE_OK)
    {
      // Bind the calculated threshold time
      sqlite3_bind_int64 (st, 1, uncloak_threshold_s);

      // Execute the statement
      sqlite3_step (st);
    }
  else
    {
      LOGE ("Can't prepare ships UPDATE: %s", sqlite3_errmsg (db));
    }

  sqlite3_finalize (st);	// Cleanup UPDATE statement

  // 4. Finalize and Unlock
  rc = commit (db);
  //LOGI ("autouncloak_sweeper: ok");
  unlock (db, "autouncloak_sweeper");

  return 0;
}


int
h_terra_replenish (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "terra_replenish", now_s))
    return 0;
  (void) now_s;
  int rc = begin (db);
  if (rc)
    return rc;

  /* 1. Max out all storable goods on the Planet Terra (Earth) */
  /* This assumes 'Terra' is the canonical name for Earth/FedSpace planet. */
  rc = sqlite3_exec (db,
		     "UPDATE planet_goods SET quantity = max_capacity "
		     "WHERE planet_id = 1;", NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("terra_replenish (Terra resources max) rc=%d", rc);
      return rc;
    }

  /* 2. Reset Terraforming Turns for all player-owned planets */
  /* This allows players to terraform one more time that day. */
  /* Assuming 'terraform_turns_left' is a column on the planets table, and 1 is the daily limit. */
  rc = sqlite3_exec (db, "UPDATE planets SET terraform_turns_left = 1 "	// Reset to one turn per day
		     "WHERE owner > 0;",	// Targets only player-owned planets (owner_id > 0)
		     NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("terra_replenish (turns reset) rc=%d", rc);
      return rc;
    }

  commit (db);
  //LOGI ("terra_replenish: ok");
  unlock (db, "terra_replenish");
  return 0;
}




int
h_planet_growth (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "planet_growth", now_s))
    return 0;
  (void) now_s;
  int rc = begin (db);
  if (rc)
    return rc;

  /* 1. Population Growth (applies to the planets table) 
   * RESTRICTED TO PLAYER-OWNED PLANETS (owner > 0) 
   */
  rc = sqlite3_exec (db, "UPDATE planets SET " " population = population + CAST(population*0.001 AS INT) "	/* +0.1% */
		     "WHERE population > 0 AND owner > 0;",	// ADDED: owner > 0
		     NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("planet_growth (pop) rc=%d", rc);
      unlock (db, "planet_growth");
      return rc;
    }

  /* 2. Resource Growth (applies to the planet_goods table) 
   * Note: This applies to all goods with production, which includes ports/planets.
   */
  rc = sqlite3_exec (db,
		     "UPDATE planet_goods SET "
		     " quantity = MIN(max_capacity, quantity + production_rate) "
		     "WHERE production_rate > 0;", NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("planet_growth (res) rc=%d", rc);
      unlock (db, "planet_growth");
      return rc;
    }

  commit (db);
  //LOGI ("planet_growth: ok");
  unlock (db, "planet_growth");
  return 0;
}



int
h_broadcast_ttl_cleanup (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "broadcast_ttl_cleanup", now_s))
    return 0;
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db,
			       "DELETE FROM broadcasts WHERE ttl_expires_at IS NOT NULL AND ttl_expires_at <= ?1;",
			       -1, &st, NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_int64 (st, 1, now_s);
      sqlite3_step (st);
    }
  sqlite3_finalize (st);
  rc = commit (db);
  //LOGI ("broadcast_ttl_cleanup: ok");
  unlock (db, "broadcast_ttl_cleanup");
  return 0;
}

int
h_traps_process (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "traps_process", now_s))
    return 0;
  int rc = begin (db);
  if (rc)
    return rc;

  /* Example: move due traps to a jobs table for the engine/worker to consume. */
  rc = sqlite3_exec (db,
		     "INSERT INTO jobs(type, payload, created_at) "
		     "SELECT 'trap.trigger', json_object('trap_id',id), ?1 "
		     "FROM traps WHERE armed=1 AND trigger_at IS NOT NULL AND trigger_at <= ?1;",
		     NULL, NULL, NULL);
  if (rc)
    {
      rollback (db);
      LOGE ("traps_process insert rc=%d", rc);
      return rc;
    }

  rc = sqlite3_exec (db,
		     "DELETE FROM traps WHERE armed=1 AND trigger_at IS NOT NULL AND trigger_at <= ?1;",
		     NULL, NULL, NULL);
  if (rc)
    {
      rollback (db);
      LOGE ("traps_process delete rc=%d", rc);
      return rc;
    }

  commit (db);
  //LOGI ("traps_process: ok");
  unlock (db, "traps_process");
  return 0;
}


/* Handler for the 'npc_step' cron task */
int
h_npc_step (sqlite3 *db, int64_t now_s)
{
  int64_t now_ms = (int64_t) monotonic_millis ();
  // 1. Initialize and run ISS tick
  if (iss_init_once () == 1)
    {
      iss_tick (now_ms);
    }
  // 2. Initialize and run Ferringhi tick
  if (fer_init_once () == 1)
    {
      fer_attach_db (db);	// Ensure DB handle is attached if needed
      fer_tick (now_ms);
    }
  // 3. Initialize and run Orion tick <--- ADD THIS BLOCK
  if (ori_init_once () == 1)
    {
      ori_attach_db (db);	// Ensure DB handle is attached if needed
      ori_tick (now_ms);
    }
  // LOGI ("npc_step: ok");
  return 0;			// Return 0 (SQLITE_OK) for success
}


/* Handler for the 'port_price_drift' cron task (FIXED to update Price Indices) */
int
h_port_price_drift (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "port_price_drift", now_s))
    return 0;
  (void) now_s;
  int rc = begin (db);
  if (rc)
    return rc;

  /* Apply a small, random drift (-0.01, 0.0, or +0.01) to all price indices.
     The price index is clamped between 0.5 and 1.5. */
  rc = sqlite3_exec (db,
             "UPDATE ports SET "
             // Ore: Target the price_index_ore column
             " price_index_ore = MIN(1.5, MAX(0.5, price_index_ore + ((ABS(RANDOM()) % 3 - 1) * 0.01))), "
             // Organics: Target the price_index_organics column
             " price_index_organics = MIN(1.5, MAX(0.5, price_index_organics + ((ABS(RANDOM()) % 3 - 1) * 0.01))), "
             // Equipment: Target the price_index_equipment column
             " price_index_equipment = MIN(1.5, MAX(0.5, price_index_equipment + ((ABS(RANDOM()) % 3 - 1) * 0.01))), "
             // Fuel: Target the price_index_fuel column
             " price_index_fuel = MIN(1.5, MAX(0.5, price_index_fuel + ((ABS(RANDOM()) % 3 - 1) * 0.01))) "
             "WHERE id > 0;", NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("port_price_drift rc=%d", rc);
      unlock (db, "port_price_drift");
      return rc;
    }

  commit (db);
  //LOGI ("port_price_drift: ok");
  unlock (db, "port_price_drift");
  return 0;
}


/* Handler for the 'port_reprice' cron task (FIXED to update Price Indices based on trade flow) */
int
h_port_reprice (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "port_reprice", now_s))
    return 0;
  (void) now_s;
  int rc = begin (db);
  if (rc)
    return rc;

  /* Adjust price indices based on trade flow.
     Net flow (T.X_net_flow) is POSITIVE when players SELL (increasing port supply).
     Therefore, we SUBTRACT net_flow from the price index to make price FALL when supply is high. 
     The price index is also clamped between 0.5 (cheap) and 1.5 (expensive). */
  rc = sqlite3_exec (db,
             "UPDATE ports SET "
             // Use subtraction for price adjustment and clamp the index
             " price_index_ore = MIN(1.5, MAX(0.5, price_index_ore - COALESCE(T.ore_net_flow / 50.0, 0))),"
             " price_index_organics = MIN(1.5, MAX(0.5, price_index_organics - COALESCE(T.organics_net_flow / 50.0, 0))),"
             " price_index_equipment = MIN(1.5, MAX(0.5, price_index_equipment - COALESCE(T.equipment_net_flow / 50.0, 0))),"
             // Add Fuel price index update
             " price_index_fuel = MIN(1.5, MAX(0.5, price_index_fuel - COALESCE(T.fuel_net_flow / 50.0, 0)))"
             "FROM ( "
             "  SELECT "
             "    port_id, "
             "    SUM(CASE WHEN commodity = 'ore' AND action = 'sell' THEN units "
             "             WHEN commodity = 'ore' AND action = 'buy' THEN -units ELSE 0 END) AS ore_net_flow, "
             "    SUM(CASE WHEN commodity = 'organics' AND action = 'sell' THEN units "
             "             WHEN commodity = 'organics' AND action = 'buy' THEN -units ELSE 0 END) AS organics_net_flow, "
             "    SUM(CASE WHEN commodity = 'equipment' AND action = 'sell' THEN units "
             "             WHEN commodity = 'equipment' AND action = 'buy' THEN -units ELSE 0 END) AS equipment_net_flow, "
             // Add Fuel to the net flow calculation
             "    SUM(CASE WHEN commodity = 'fuel' AND action = 'sell' THEN units "
             "             WHEN commodity = 'fuel' AND action = 'buy' THEN -units ELSE 0 END) AS fuel_net_flow "
             "  FROM trade_log "
             "  WHERE timestamp > (strftime('%s', 'now') - 86400) "
             "  GROUP BY port_id "
             ") AS T "
             "WHERE ports.id = T.port_id;", NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("port_reprice rc=%d", rc);
      unlock (db, "port_reprice");
      return rc;
    }

  commit (db);
  //LOGI ("port_reprice: ok");
  unlock (db, "port_reprice");
  return 0;
}



//////////////////////// NEWS BLOCK ////////////////////////

// --- Helper Functions for Event Translation ---

/**
 * @brief Formats a combat.ship_destroyed event into a news article.
 * @param payload The parsed JSON payload.
 * @param sector_id The sector ID where the event occurred.
 * @param out_category Output buffer for news category.
 * @param out_article Output buffer for the final article text.
 */
static void
format_ship_destroyed_news (json_t *payload, int sector_id,
			    char *out_category, char *out_article)
{
  const char *ship_name =
    json_string_value (json_object_get (payload, "ship_name"));
  const char *destroyed_by =
    json_string_value (json_object_get (payload, "destroyed_by"));

  // Default values if JSON is malformed
  if (!ship_name)
    ship_name = "An Unknown Vessel";
  if (!destroyed_by)
    destroyed_by = "Mysterious Forces";

  // Set Category
  snprintf (out_category, MAX_ARTICLE_LEN, "Combat");

  // Format the news text
  snprintf (out_article, MAX_ARTICLE_LEN,
	    "A major engagement was recorded in Sector %d: %s was destroyed by %s.",
	    sector_id, ship_name, destroyed_by);
}

/**
 * @brief Formats a trade.large_sale event into a news article.
 * @param payload The parsed JSON payload.
 * @param actor_player_id The player ID initiating the event.
 * @param out_category Output buffer for news category.
 * @param out_article Output buffer for the final article text.
 */
static void
format_large_sale_news (json_t *payload, int actor_player_id,
			char *out_category, char *out_article)
{
  const char *commodity =
    json_string_value (json_object_get (payload, "commodity"));
  int units = (int) json_integer_value (json_object_get (payload, "units"));

  // In a full system, you would look up the player's name here:
  // char *player_name = db_get_player_name(actor_player_id);
  const char *player_name = "A Prominent Trader";
  if (actor_player_id > 0)
    {
      // Simple mock for a real player name
      // player_name = db_get_player_name(actor_player_id); 
    }

  if (!commodity)
    commodity = "Unknown Goods";

  snprintf (out_category, MAX_ARTICLE_LEN, "Trade");

  snprintf (out_article, MAX_ARTICLE_LEN,
	    "%s executed a massive sale of %d units of %s, causing major market turbulence.",
	    player_name, units, commodity);
}


/**
 * @brief Dispatches event to the appropriate formatter function.
 * @return 1 if successfully collated and published, 0 otherwise.
 */
static int
collate_single_event (sqlite3_stmt *insert_stmt, sqlite3_int64 event_id,
		      sqlite3_int64 ts, const char *type, int actor_player_id,
		      int sector_id, const char *payload_str)
{
  json_t *payload = NULL;
  char category[MAX_ARTICLE_LEN] = { 0 };
  char article[MAX_ARTICLE_LEN] = { 0 };
  sqlite3_int64 published_ts = (sqlite3_int64) time (NULL);
  sqlite3_int64 expiration_ts = published_ts + NEWS_EXPIRATION_SECONDS;

  int processed = 0;		// Flag if we generated an article

  // 1. Parse JSON Payload
  payload = json_loads (payload_str, 0, NULL);
  if (!payload)
    {
      // fprintf(stderr, "Error parsing JSON payload for event %lld.\n", event_id);
      return 0;
    }

  // 2. Dispatch to the Correct Formatter based on 'type'
  if (strcmp (type, "combat.ship_destroyed") == 0)
    {
      format_ship_destroyed_news (payload, sector_id, category, article);
      processed = 1;
    }
  else if (strcmp (type, "trade.large_sale") == 0)
    {
      format_large_sale_news (payload, actor_player_id, category, article);
      processed = 1;
    }
  else if (strcmp (type, "lottery.winner") == 0)
    {
      snprintf (category, MAX_ARTICLE_LEN, "System");
      snprintf (article, MAX_ARTICLE_LEN,
		"The Sector Lottery jackpot has been claimed by a lucky player! Check the system logs for details.");
      processed = 1;
    }
  // Add more types here: bounty.updated, ports.destroyed, etc.

  // 3. Insert into news_feed if an article was generated
  if (processed)
    {
      // Reset and bind parameters to the INSERT INTO news_feed statement
      sqlite3_reset (insert_stmt);
      sqlite3_bind_int64 (insert_stmt, 1, published_ts);
      sqlite3_bind_int64 (insert_stmt, 2, expiration_ts);
      sqlite3_bind_text (insert_stmt, 3, category, -1, SQLITE_STATIC);
      sqlite3_bind_text (insert_stmt, 4, article, -1, SQLITE_STATIC);

      // The source_ids field is just the single event ID for now (JSON array "[ID]")
      char source_id_json[64];
      snprintf (source_id_json, sizeof (source_id_json), "[%lld]", event_id);
      sqlite3_bind_text (insert_stmt, 5, source_id_json, -1,
			 SQLITE_TRANSIENT);

      // Execute insert
      if (sqlite3_step (insert_stmt) != SQLITE_DONE)
	{
	  // fprintf(stderr, "DB Error (news_feed insert): %s\n", sqlite3_errmsg(db_get_handle()));
	  processed = 0;	// Insertion failed
	}
    }

  json_decref (payload);
  return processed;
}


// --- Main Cron Handler ---

int
h_news_collator (void)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *select_stmt = NULL;
  sqlite3_stmt *insert_stmt = NULL;
  int rc = SQLITE_OK;
  sqlite3_int64 current_time = (sqlite3_int64) time (NULL);

  // --- 0. SQL Definitions ---
  // 1. SELECT: Get all unprocessed events
  const char *SELECT_UNPROCESSED_SQL =
    "SELECT id, ts, type, actor_player_id, sector_id, payload "
    "FROM engine_events WHERE processed_at IS NULL ORDER BY ts ASC, id ASC;";

  // 2. INSERT: Insert into the news_feed table (fields: published_ts, expiration_ts, news_category, article_text, source_ids)
  const char *INSERT_NEWS_SQL =
    "INSERT INTO news_feed (published_ts, expiration_ts, news_category, article_text, source_ids) "
    "VALUES (?, ?, ?, ?, ?);";

  // 3. DELETE: Clean up expired news
  const char *DELETE_EXPIRED_SQL =
    "DELETE FROM news_feed WHERE expiration_ts < ?;";

  // 4. UPDATE: Mark processed events (Done in a batch after the loop)
  char update_sql_buffer[4096] = { 0 };	// Large enough for a batch update

  // --- 1. Start Transaction ---
  if (begin (db) != SQLITE_OK)
    {
      // fprintf(stderr, "Failed to start transaction.\n");
      return SQLITE_ERROR;
    }

  // --- 2. Clean Up Expired News ---
  rc = sqlite3_prepare_v2 (db, DELETE_EXPIRED_SQL, -1, &select_stmt, NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_int64 (select_stmt, 1, current_time);
      sqlite3_step (select_stmt);
    }
  sqlite3_finalize (select_stmt);
  select_stmt = NULL;		// Reset for next use

  // --- 3. Prepare SELECT and INSERT Statements ---
  rc =
    sqlite3_prepare_v2 (db, SELECT_UNPROCESSED_SQL, -1, &select_stmt, NULL);
  if (rc != SQLITE_OK)
    goto error_cleanup;

  rc = sqlite3_prepare_v2 (db, INSERT_NEWS_SQL, -1, &insert_stmt, NULL);
  if (rc != SQLITE_OK)
    goto error_cleanup;

  // --- 4. Process Events and Build Batch Update List ---

  // Buffer to hold IDs of successfully processed events for batch update
  char batch_ids[2048] = { 0 };
  size_t batch_len = 0;
  int event_count = 0;

  while (sqlite3_step (select_stmt) == SQLITE_ROW)
    {
      sqlite3_int64 id = sqlite3_column_int64 (select_stmt, 0);
      sqlite3_int64 ts = sqlite3_column_int64 (select_stmt, 1);
      const char *type = (const char *) sqlite3_column_text (select_stmt, 2);
      int actor_id = sqlite3_column_int (select_stmt, 3);
      int sector_id = sqlite3_column_int (select_stmt, 4);
      const char *payload_str =
	(const char *) sqlite3_column_text (select_stmt, 5);

      // Try to process and publish the event
      if (collate_single_event
	  (insert_stmt, id, ts, type, actor_id, sector_id, payload_str))
	{
	  // Article created successfully. Add ID to batch list.
	  if (batch_len > 0)
	    {
	      batch_len +=
		snprintf (batch_ids + batch_len,
			  sizeof (batch_ids) - batch_len, ",%lld", id);
	    }
	  else
	    {
	      batch_len +=
		snprintf (batch_ids + batch_len,
			  sizeof (batch_ids) - batch_len, "%lld", id);
	    }
	  event_count++;
	}
    }

  // --- 5. Mark Events as Processed (Batch Update) ---
  if (event_count > 0)
    {
      snprintf (update_sql_buffer, sizeof (update_sql_buffer),
		"UPDATE engine_events SET processed_at = %lld WHERE id IN (%s);",
		current_time, batch_ids);

      if (sqlite3_exec (db, update_sql_buffer, NULL, NULL, NULL) != SQLITE_OK)
	{
	  // fprintf(stderr, "DB Error (batch update): %s\n", sqlite3_errmsg(db));
	  rc = SQLITE_ERROR;
	  goto error_cleanup;
	}
    }

  // --- 6. Finalize Statements and Commit ---
  sqlite3_finalize (select_stmt);
  sqlite3_finalize (insert_stmt);

  if (commit (db) != SQLITE_OK)
    {
      // fprintf(stderr, "Failed to commit transaction.\n");
      rc = SQLITE_ERROR;
      return rc;
    }

  return SQLITE_OK;

error_cleanup:
  // fprintf(stderr, "News collation failed, rolling back.\n");
  sqlite3_finalize (select_stmt);
  sqlite3_finalize (insert_stmt);
  rollback (db);
  return rc;
}


// --- Helper for logging (INSERT into engine_events) ---

int
h_log_engine_event (const char *type,
		    int actor_player_id,
		    int sector_id, json_t *payload, const char *idem_key)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *stmt = NULL;
  int rc;
  int current_ts = (int) time (NULL);
  char *payload_str = NULL;

  if (payload == NULL)
    {
      payload = json_object (); // Create a new empty object: {}
      if (payload == NULL)
        {
          fprintf (stderr,
                   "ERROR: h_log_engine_event Failed to create empty JSON payload (OOM).\n");
          return SQLITE_NOMEM; // Use a more appropriate OOM error code
        }
    }
  
  // 1. Serialize the JSON payload
  // JSON_COMPACT is used to save space in the database TEXT field.
  payload_str = json_dumps (payload, JSON_COMPACT | JSON_ENSURE_ASCII);
  json_decref (payload);	// Consume the reference as per function contract

  if (!payload_str)
  {
    fprintf (stderr,
             "ERROR: h_log_engine_event Failed to serialize JSON payload for type: %s\n", type); // ADDED
    return SQLITE_ERROR;
  }

  // 2. Prepare the SQL statement
  const char *sql =
    "INSERT INTO engine_events (ts, type, actor_player_id, sector_id, payload, idem_key) "
    "VALUES (?, ?, ?, ?, ?, ?);";

  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "ERROR: h_log_engine_event prepare failed: %s\n",
	       sqlite3_errmsg (db));
      free (payload_str);
      return rc;
    }

  // 3. Bind the parameters
  sqlite3_bind_int (stmt, 1, current_ts);
  sqlite3_bind_text (stmt, 2, type, -1, SQLITE_STATIC);
  sqlite3_bind_int (stmt, 3, actor_player_id);
  sqlite3_bind_int (stmt, 4, sector_id);
  // Use SQLITE_TRANSIENT to make a copy of payload_str
  sqlite3_bind_text (stmt, 5, payload_str, -1, SQLITE_TRANSIENT);

  if (idem_key && idem_key[0] != '\0')
    {
      sqlite3_bind_text (stmt, 6, idem_key, -1, SQLITE_STATIC);
    }
  else
    {
      sqlite3_bind_null (stmt, 6);
    }

  // 4. Execute the statement
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      fprintf (stderr, "ERROR: h_log_engine_event execution failed: %s\n",
	       sqlite3_errmsg (db));
    }
  else
    {
      rc = SQLITE_OK;		// Success
    }

  // 5. Cleanup
  sqlite3_finalize (stmt);
  free (payload_str);

  return rc;
}
