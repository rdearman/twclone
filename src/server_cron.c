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
};

static int g_reg_inited = 0;

////////////////////////////////////


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
static int *universe_pathfind_get_sectors(sqlite3 *db, int start_sector, int end_sector, const int *avoid_list)
{
    // Check for trivial case
    if (start_sector == end_sector) {
        int *path = malloc(2 * sizeof(int));
        if (path) {
            path[0] = start_sector;
            path[1] = 0; // Null terminator
        }
        return path;
    }

    // --- 1. Initialization and Sizing ---

    // Find the actual maximum sector ID for correct array sizing
    int max_sector_id = 0;
    sqlite3_stmt *max_st = NULL;
    if (sqlite3_prepare_v2(db, "SELECT MAX(sector_id) FROM sectors;", -1, &max_st, NULL) == SQLITE_OK) {
        if (sqlite3_step(max_st) == SQLITE_ROW) {
            max_sector_id = sqlite3_column_int(max_st, 0);
        }
        sqlite3_finalize(max_st);
    }
    
    // Safety fallback for max size (should be larger than the largest possible sector ID)
    if (max_sector_id < 100) { 
        max_sector_id = 2000;
    }

    // Allocate parent array: stores the sector ID that led to the current sector.
    // Index corresponds to sector ID. Value 0 means unvisited.
    int *parent = calloc(max_sector_id + 1, sizeof(int));
    if (!parent) {
        // LOGE("Pathfind: Failed to allocate parent array."); // Assuming LOGE is available
        return NULL;
    }

    // Initialize BFS Queue (dynamic resizing)
    int *queue = malloc(INITIAL_QUEUE_CAPACITY * sizeof(int));
    int queue_head = 0;
    int queue_tail = 0;
    int queue_capacity = INITIAL_QUEUE_CAPACITY;
    if (!queue) {
        // LOGE("Pathfind: Failed to allocate queue."); // Assuming LOGE is available
        free(parent);
        return NULL;
    }

    // Handle avoid list: mark avoided sectors as blocked (-2)
    if (avoid_list) {
        for (const int *avoid = avoid_list; *avoid != 0; avoid++) {
             if (*avoid > 0 && *avoid <= max_sector_id) {
                parent[*avoid] = -2; // Sentinel for blocked/avoided sector
             }
        }
    }

    // Mark start sector as visited and enqueue it. Parent of start is -1 (sentinel).
    parent[start_sector] = -1;
    queue[queue_tail++] = start_sector;
    int path_found = 0;
    
    // --- 2. Breadth-First Search (BFS) ---
    
    sqlite3_stmt *warp_st = NULL;
    const char *sql_warps = "SELECT to_sector FROM sector_warps WHERE from_sector = ?1;";
    if (sqlite3_prepare_v2(db, sql_warps, -1, &warp_st, NULL) != SQLITE_OK) {
        // LOGE("Pathfind: DB prepare error for warps: %s", sqlite3_errmsg(db)); // Assuming LOGE is available
        free(parent);
        free(queue);
        return NULL;
    }

    while (queue_head < queue_tail) {
        int current_sector = queue[queue_head++];

        // Check if we reached the target
        if (current_sector == end_sector) {
            path_found = 1;
            break;
        }

        sqlite3_bind_int(warp_st, 1, current_sector);
        
        while (sqlite3_step(warp_st) == SQLITE_ROW) {
            int neighbor = sqlite3_column_int(warp_st, 0);

            // Bounds check and already visited/avoided check (parent[neighbor] == 0 means unvisited/unblocked)
            if (neighbor <= 0 || neighbor > max_sector_id || parent[neighbor] != 0) {
                continue; 
            }
            
            // Mark as visited and record parent
            parent[neighbor] = current_sector;

            // Enqueue: Check for capacity and reallocate if necessary
            if (queue_tail == queue_capacity) {
                queue_capacity *= 2;
                int *new_queue = realloc(queue, queue_capacity * sizeof(int));
                if (!new_queue) {
                    // LOGE("Pathfind: Realloc failed for queue expansion."); // Assuming LOGE is available
                    path_found = 0; // Failure state
                    queue_tail = 0; // Stop loop
                    break;
                }
                queue = new_queue;
            }
            queue[queue_tail++] = neighbor;
        }

        sqlite3_reset(warp_st);
    }

    sqlite3_finalize(warp_st);
    free(queue);

    // --- 3. Path Reconstruction ---
    
    if (!path_found) {
        free(parent);
        return NULL;
    }

    // Count path length
    int path_length = 0;
    int temp_sector = end_sector;
    while (temp_sector != -1) {
        path_length++;
        // Use a safety break in case of cycle/corrupt parent pointers
        if (path_length > max_sector_id + 1) { 
            // LOGE("Pathfind: Cycle detected during reconstruction. Aborting."); // Assuming LOGE is available
            free(parent);
            return NULL;
        }
        temp_sector = parent[temp_sector];
    }

    // Allocate result and fill in reverse order
    int *result_path = malloc((path_length + 1) * sizeof(int)); // +1 for terminator
    if (!result_path) {
        // LOGE("Pathfind: Failed to allocate result path."); // Assuming LOGE is available
        free(parent);
        return NULL;
    }

    int i = path_length - 1;
    temp_sector = end_sector;
    while (temp_sector != -1) {
        result_path[i--] = temp_sector;
        temp_sector = parent[temp_sector];
    }
    result_path[path_length] = 0; // Null terminator

    free(parent);
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
static void _insert_path_sectors(sqlite3 *db, sqlite3_stmt *insert_st, 
                                 int start_sector, int end_sector, 
                                 const int *avoid_list, int *total_unique_sectors_added)
{
    int rc;
    int *s;
    int *current_path = universe_pathfind_get_sectors(db, start_sector, end_sector, avoid_list);
        
    if (!current_path) {
        LOGW("Could not find path from %d to %d. Check universe connections.", start_sector, end_sector);
        return;
    }

    // Insert Path Sectors into Table
    for (s = current_path; *s != 0; s++) {
        
        sqlite3_reset(insert_st);
        sqlite3_bind_int(insert_st, 1, *s);
        
        rc = sqlite3_step(insert_st);
        
        if (rc == SQLITE_DONE) {
            // If the sector was newly inserted (not ignored)
            if (sqlite3_changes(db) > 0) {
                (*total_unique_sectors_added)++;
            }
        } else {
            LOGW("SQL warning inserting sector %d for path %d->%d: %s", 
                 *s, start_sector, end_sector, sqlite3_errmsg(db));
        }
    }
    
    free(current_path);
}

/**
 * @brief Populates the MSL table (MSL_TABLE_NAME) with 
 * all sectors on paths between FedSpace sectors (1-10) and all Stardock sectors.
 *
 * @param db The SQLite database handle.
 * @return 0 on success, -1 on database error.
 */
int populate_msl_if_empty(sqlite3 *db)
{
    const int *avoid_list = NULL; // No sectors to avoid for this "safe" path list
    int rc;
    int total_sectors_in_table = 0;
    char sql_buffer[256];
    
    // --- 1. Check if the table is empty ---
    snprintf(sql_buffer, sizeof(sql_buffer), 
             "SELECT COUNT(sector_id) FROM %s;", MSL_TABLE_NAME);
    
    sqlite3_stmt *count_st = NULL;
    
    if (sqlite3_prepare_v2(db, sql_buffer, -1, &count_st, NULL) == SQLITE_OK &&
        sqlite3_step(count_st) == SQLITE_ROW) {
        total_sectors_in_table = sqlite3_column_int(count_st, 0);
    }
    sqlite3_finalize(count_st);

    if (total_sectors_in_table > 0) {
        // Logging the correct table name now
        LOGI("%s table already populated with %d entries. Skipping MSL calculation.", 
             MSL_TABLE_NAME, total_sectors_in_table);
        return 0;
    }
    
    LOGI("%s table is empty. Starting comprehensive MSL path calculation (FedSpace 1-10 <-> Stardocks)...", 
         MSL_TABLE_NAME);
    
    // --- 2. Create the table (if it doesn't exist) ---
    snprintf(sql_buffer, sizeof(sql_buffer),
        "CREATE TABLE IF NOT EXISTS %s ("
        "  sector_id INTEGER PRIMARY KEY"
        ");", MSL_TABLE_NAME);
    
    if (sqlite3_exec(db, sql_buffer, NULL, NULL, NULL) != SQLITE_OK) {
        LOGE("SQL error creating %s table: %s", MSL_TABLE_NAME, sqlite3_errmsg(db));
        return -1;
    }

    // --- 3. Prepare Select and Insert Statements and collect Stardocks ---
    
    // Select all Stardock sector IDs from the specified table.
    sqlite3_stmt *select_st = NULL;
    const char *sql_select_stardocks = "SELECT sector_id FROM stardock_location;";
    rc = sqlite3_prepare_v2(db, sql_select_stardocks, -1, &select_st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("SQL error preparing Stardock select: %s", sqlite3_errmsg(db));
        return -1;
    }

    // Collect stardock sectors into a dynamically sized array
    int *stardock_sectors = NULL;
    int stardock_count = 0;
    int stardock_capacity = 8;
    stardock_sectors = malloc(stardock_capacity * sizeof(int));
    if (!stardock_sectors) {
        LOGE("Failed to allocate stardock sector array.");
        sqlite3_finalize(select_st);
        return -1;
    }

    // Fetch all stardock IDs
    while (sqlite3_step(select_st) == SQLITE_ROW) {
        int id = sqlite3_column_int(select_st, 0);
        if (stardock_count == stardock_capacity) {
            stardock_capacity *= 2;
            int *new_arr = realloc(stardock_sectors, stardock_capacity * sizeof(int));
            if (!new_arr) {
                LOGE("Failed to reallocate stardock sector array.");
                free(stardock_sectors);
                sqlite3_finalize(select_st);
                return -1;
            }
            stardock_sectors = new_arr;
        }
        stardock_sectors[stardock_count++] = id;
    }
    sqlite3_finalize(select_st);

    if (stardock_count == 0) {
        LOGW("No stardock locations found in stardock_location table. Skipping MSL calculation.");
        free(stardock_sectors);
        return 0;
    }

    // Prepare Insert statement
    sqlite3_stmt *insert_st = NULL;
    snprintf(sql_buffer, sizeof(sql_buffer),
        "INSERT OR IGNORE INTO %s (sector_id) VALUES (?);", MSL_TABLE_NAME);
    rc = sqlite3_prepare_v2(db, sql_buffer, -1, &insert_st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("SQL error preparing insert statement for %s: %s", MSL_TABLE_NAME, sqlite3_errmsg(db));
        free(stardock_sectors);
        return -1;
    }

    // --- 4. Iterate and Calculate Paths (Two-Way) ---
    
    // Start with a master transaction for the entire population process
    if (sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK) {
        LOGE("SQL error starting master transaction: %s", sqlite3_errmsg(db));
        sqlite3_finalize(insert_st);
        free(stardock_sectors);
        return -1;
    }
    
    int total_unique_sectors_added = 0;
    
    // Loop through FedSpace starting sectors (1 through 10)
    for (int start_sector = FEDSPACE_SECTOR_START; start_sector <= FEDSPACE_SECTOR_END; start_sector++) {
        // Loop through all collected Stardock sectors
        for (int i = 0; i < stardock_count; i++) {
            int stardock_id = stardock_sectors[i];
            
            // A. Path FedSpace -> Stardock (e.g., 1 -> 160)
            LOGI("Calculating path %d -> %d", start_sector, stardock_id);
            _insert_path_sectors(db, insert_st, start_sector, stardock_id, avoid_list, &total_unique_sectors_added);
            
            // B. Path Stardock -> FedSpace (e.g., 160 -> 1)
            // Only calculate reverse if the start/end sectors are different
            if (start_sector != stardock_id) { 
                LOGI("Calculating path %d -> %d (Reverse)", stardock_id, start_sector);
                _insert_path_sectors(db, insert_st, stardock_id, start_sector, avoid_list, &total_unique_sectors_added);
            }
        }
    }
    
    // --- 5. Finalize and Commit ---
    
    sqlite3_finalize(insert_st);
    free(stardock_sectors);

    if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        LOGE("SQL error committing master path transaction: %s", sqlite3_errmsg(db));
        return -1;
    }
    
    LOGI("Completed MSL setup. Populated %s with %d total unique sectors.", 
         MSL_TABLE_NAME, total_unique_sectors_added);

    return 0;
}

////////////////////////////////////




/**
 * @brief Resets the daily turns remaining for all players to the maximum configured amount.
 * * This function iterates over all players in the 'turns' table and sets their 
 * 'turns_remaining' back to the 'turnsperday' value found in the 'config' table.
 * It is designed to be run as a daily cron job, hence its signature (sqlite3 *db, int64_t now_s).
 * * @param db The SQLite database handle.
 * @param now_s The current time in seconds (passed by the cron runner).
 * @return 0 on success, -1 on database error or if configuration is missing.
 */
int h_reset_turns_for_player(sqlite3 *db, int64_t now_s)
{
    sqlite3_stmt *select_st = NULL;
    sqlite3_stmt *update_st = NULL;
    int max_turns = 0;
    int rc;
    int updated_count = 0;
    
    // --- 1. Get Maximum Turns Per Day from Config ---
    const char *sql_config = "SELECT turnsperday FROM config WHERE id = 1;";
    rc = sqlite3_prepare_v2(db, sql_config, -1, &select_st, NULL);
    if (rc == SQLITE_OK && sqlite3_step(select_st) == SQLITE_ROW) {
        max_turns = sqlite3_column_int(select_st, 0);
    }
    sqlite3_finalize(select_st);
    select_st = NULL; // Reset to NULL for safety

    if (max_turns <= 0) {
        // Use LOGE for a critical failure that prevents the job from running
        LOGE("Turn reset failed: turnsperday is %d or missing in config.", max_turns);
        return -1;
    }

    // --- 2. Prepare Statements and Start Transaction ---
    
    // Select all player IDs that have turn records
    const char *sql_select_players = "SELECT player FROM turns;";
    rc = sqlite3_prepare_v2(db, sql_select_players, -1, &select_st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("SQL error preparing player select: %s", sqlite3_errmsg(db));
        return -1;
    }
    
    // Update player's turns
    const char *sql_update = 
        "UPDATE turns SET turns_remaining = ?, last_update = ? WHERE player = ?;";
    rc = sqlite3_prepare_v2(db, sql_update, -1, &update_st, NULL);
    if (rc != SQLITE_OK) {
        LOGE("SQL error preparing turns update: %s", sqlite3_errmsg(db));
        sqlite3_finalize(select_st);
        return -1;
    }
    
    // Start Transaction for performance and atomicity
    if (sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL) != SQLITE_OK) {
        LOGE("SQL error starting transaction: %s", sqlite3_errmsg(db));
        sqlite3_finalize(select_st);
        sqlite3_finalize(update_st);
        return -1;
    }

    // --- 3. Iterate and Update ---

    while (sqlite3_step(select_st) == SQLITE_ROW) {
        int player_id = sqlite3_column_int(select_st, 0);

        // Bind parameters to the prepared UPDATE statement
        sqlite3_reset(update_st);
        sqlite3_bind_int(update_st, 1, max_turns);
        sqlite3_bind_int64(update_st, 2, now_s); // Use cron-provided time
        sqlite3_bind_int(update_st, 3, player_id);

        rc = sqlite3_step(update_st);
        
        if (rc == SQLITE_DONE) {
             updated_count++;
        } else {
            // Use LOGE for a failure within the transaction that needs attention
            LOGE("SQL error executing turns update for player %d: %s",
                     player_id, sqlite3_errmsg(db));
            // Note: The transaction continues, but this failure is severe.
        }
    }

    // --- 4. Finalize and Commit ---
    
    sqlite3_finalize(select_st);
    sqlite3_finalize(update_st);

    if (sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK) {
        LOGE("SQL error committing transaction: %s", sqlite3_errmsg(db));
        return -1;
    }
    
    // Use LOGI for successful job completion
    LOGI("Successfully reset turns for %d players to %d.", updated_count, max_turns);
    
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
const char* get_asset_name(int type) {
    switch(type) {
        case 3: return "Mine";
        case 2: return "Fighter";
        case 1: return "Beacon";
        default: return "Unknown Asset";
    }
}



/* FedSpace cleanup:
   - Remove traps in FedSpace
   - Clear wanted flags for ships in FedSpace
   - Tow excess ships if > MAX_SHIPS_PER_FED_SECTOR in any Fed sector (keep the lowest IDs in place)
   - Tow ships carrying >= MAX_FED_FIGHTERS in FedSpace
   - At most 50 total tows per pass
*/
int
h_fedspace_cleanup (sqlite3 *db, int64_t now_s)
{
  // Use milliseconds for the lock check (now_s is seconds, convert to ms)
  int64_t now_ms = now_s * 1000;
  sqlite3_stmt *select_stmt = NULL;
  
  if (!try_lock (db, "fedspace_cleanup", now_ms))
    {
      // try_lock failed. Check the status to see if the lock is stale.
      int64_t until_ms = db_lock_status (db, "fedspace_cleanup");
      int64_t time_left_s = (until_ms - now_ms) / 1000;

      if (until_ms > now_ms)
	{
	  // The lock exists and is NOT stale. This is an ACTIVE conflict.
	  LOGW
	    ("fedspace_cleanup: FAILED to acquire lock. Still held for %lld more seconds.",
	     (long long) time_left_s);
	}
      else
	{
	  // The lock exists but should have been cleaned up by try_lock (Stale/Expired).
	  // This indicates a problem in the try_lock's cleanup step, but logs the state.
	  LOGW
	    ("fedspace_cleanup: FAILED to acquire lock. Lock is stale (Expires at %lld).",
	     (long long) until_ms);
	}
      return 0;
    }
  else
    {
      // try_lock succeeded.
      LOGI ("fedspace_cleanup: Lock acquired, starting cleanup operations.");
    }

  /////////////////////
      // --- STEP 1: Ensure MSL table is populated ---
    // If the table is empty, this runs the one-time population logic.
    if (populate_msl_if_empty(db) != 0) {
        LOGE("fedspace_cleanup: MSL population failed. Aborting cleanup.");
    }

    // --- STEP 2: Identify assets and owners in MSL sectors for AUDIT/MESSAGING ---
    
    // Query: Select all assets in sectors marked as MSL (excluding System-owned assets, Player 0)
    const char *select_assets_sql = 
        "SELECT player, asset_type, sector, quantity "
        "FROM sector_assets "
        "WHERE sector IN (SELECT sector_id FROM msl_sectors) AND player != 0;";
        
    int rc = sqlite3_prepare_v2(db, select_assets_sql, -1, &select_stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGW("fedspace_cleanup: Failed to prepare SELECT assets query: %s", sqlite3_errmsg(db));
    }
    
    // --- STEP 3: Send messages to owners and delete assets ---
    int cleared_assets = 0;
    char message[256];

    // Prepare DELETE statement outside the loop for efficiency
    sqlite3_stmt *delete_stmt = NULL;
    const char *delete_sql = 
        "DELETE FROM sector_assets "
        "WHERE player = ?1 AND asset_type = ?2 AND sector = ?3 AND quantity = ?4;";
    
    rc = sqlite3_prepare_v2(db, delete_sql, -1, &delete_stmt, NULL);
    if (rc != SQLITE_OK) {
        LOGE("fedspace_cleanup: Failed to prepare DELETE assets query: %s", sqlite3_errmsg(db));
        // We can't proceed with deletion if the prepare failed, but let's continue to messages for now
        // A full implementation might exit or handle this error more strictly.
    }

    while ((rc = sqlite3_step(select_stmt)) == SQLITE_ROW) {
        int player_id = sqlite3_column_int(select_stmt, 0);
        int asset_type = sqlite3_column_int(select_stmt, 1);
        int sector_id = sqlite3_column_int(select_stmt, 2);
        int quantity = sqlite3_column_int(select_stmt, 3);

        if (player_id == 0) continue;


        // Construct audit message
        snprintf(message, sizeof(message),
                 "%d %s(s) deployed in Sector %d (Major Space Lane) were destroyed by Federal Authorities. Deployments in MSL are strictly prohibited.",
                 quantity, get_asset_name(asset_type), sector_id); // Completed snprintf line
        
        // --- 1. SEND MAIL ---
        // h_send_message_to_player(int player_id, int sender_id, const char *subject, const char *message) ;
        h_send_message_to_player(player_id, 1, "WARNING: MSL Violation", message);

        LOGI("ASSET_RETURNED: %d", asset_type);
        
        // --- 2. DELETE ASSET ---
        if (delete_stmt) {
            // Re-bind parameters for the current row's data
            sqlite3_reset(delete_stmt);
            sqlite3_bind_int(delete_stmt, 1, player_id);
            sqlite3_bind_int(delete_stmt, 2, asset_type);
            sqlite3_bind_int(delete_stmt, 3, sector_id);
            sqlite3_bind_int(delete_stmt, 4, quantity);
            
            int delete_rc = sqlite3_step(delete_stmt);
            if (delete_rc != SQLITE_DONE) {
                LOGE("fedspace_cleanup: Failed to execute DELETE for asset %d, player %d, sector %d: %s", 
                    asset_type, player_id, sector_id, sqlite3_errmsg(db));
            } else {
                cleared_assets++;
                LOGI("fedspace_cleanup: Cleared asset %d for player %d in sector %d. Quantity: %d", 
                    asset_type, player_id, sector_id, quantity);
            }
        }
        
    }
    
    sqlite3_finalize(select_stmt);
    if (delete_stmt) {
        sqlite3_finalize(delete_stmt);
    }
    
    if (cleared_assets > 0) {
        rc = commit(db);
        LOGI("fedspace_cleanup: Completed transaction with %d assets cleared.", cleared_assets);
    }


  //////////////////////


  
  enum
  { MAX_TOWS_PER_PASS = 50, MAX_SHIPS_PER_FED_SECTOR = 5, MAX_FED_FIGHTERS =
      98
  };
  rc = begin (db);
  LOGI ("Starting Fedspace Cleanup");
  if (rc)
    {
      LOGW ("fedspace_cleanup: begin rc=%d", rc);
      return rc;
    }

  LOGI ("Looking for mines in fedspace");
  /* 1) Remove sector traps/mines in FedSpace */
  rc = sqlite3_exec (db,
		     "DELETE FROM traps WHERE sector IN (SELECT id FROM sectors WHERE is_fed=1);",
		     NULL, NULL, NULL);
  if (rc)
    {
      rollback (db);
      LOGE ("fedspace_cleanup traps rc=%d", rc);
      unlock (db, "fedspace_cleanup");
      return rc;
    }

  /* 2) Clear wanted flags in FedSpace (engine policy) */
  rc = sqlite3_exec (db,
		     "UPDATE ships SET wanted=0 "
		     "WHERE wanted=1 AND sector IN (SELECT id FROM sectors WHERE is_fed=1);",
		     NULL, NULL, NULL);
  if (rc)
    {
      rollback (db);
      LOGE ("fedspace_cleanup ships rc=%d", rc);
      unlock (db, "fedspace_cleanup");
      return rc;
    }

  /* Helper: find a non-fed tow target for a given Fed sector (first non-fed neighbour; fallback = 1) */
  sqlite3_stmt *q_tow = NULL;
  rc = sqlite3_prepare_v2 (db,
			   "WITH nf AS ( "
			   "  SELECT w.to_sector AS cand "
			   "  FROM warps w JOIN sectors s ON s.id=w.to_sector "
			   "  WHERE w.from_sector=?1 AND s.is_fed=0 "
			   "  ORDER BY cand "
			   ") SELECT COALESCE((SELECT cand FROM nf LIMIT 1), 1);",
			   -1, &q_tow, NULL);
  if (rc != SQLITE_OK)
    {
      rollback (db);
      LOGE ("fedspace_cleanup prep tow-target rc=%d", rc);
      unlock (db, "fedspace_cleanup");
      return rc;
    }
  else
    LOGI ("Tow Target: %s", (char *) &q_tow);

  // OK, need to figure out what to do with player who has multiple ships? If the ship is towed and they are in it, they move too. 
  LOGI ("Set ships sectors for twoed ships");
  /* Update ship sector */
  sqlite3_stmt *u_ship = NULL;
  rc = sqlite3_prepare_v2 (db,
			   "UPDATE ships SET sector=?1 WHERE id=?2;", -1,
			   &u_ship, NULL);
  if (rc != SQLITE_OK)
    {
      sqlite3_finalize (q_tow);
      rollback (db);
      LOGE ("fedspace_cleanup prep upd-ship rc=%d", rc);
      unlock (db, "fedspace_cleanup");
      return rc;
    }


  /* Notice to owner (adjust to your notice table/columns) */
  sqlite3_stmt *i_notice = NULL;
  rc = sqlite3_prepare_v2 (db,
			   "INSERT INTO system_notice(scope,scope_id,message,created_at,expires_at) "
			   "VALUES('player',?1,?2,?3,NULL);",
			   -1, &i_notice, NULL);
  if (rc != SQLITE_OK)
    {
      sqlite3_finalize (u_ship);
      sqlite3_finalize (q_tow);
      rollback (db);
      LOGE ("fedspace_cleanup prep notice rc=%d", rc);
      unlock (db, "fedspace_cleanup");
      return rc;
    }
  else
    LOGI ("%s", (char *) &i_notice);

  int tows = 0;

  /* 3) Overcrowding: tow ships beyond MAX_SHIPS_PER_FED_SECTOR in each Fed sector (keep the lowest IDs) */
  sqlite3_stmt *q_over = NULL;
  rc = sqlite3_prepare_v2 (db,
			   "WITH fed AS IN (1,2,3,4,5,6,7,8,9,10), "
			   "crowded AS ( "
			   "  SELECT s.id AS ship_id, s.sector, s.owner_player_id, "
			   "         ROW_NUMBER() OVER (PARTITION BY s.sector ORDER BY s.id) AS rn "
			   "  FROM ships s WHERE s.sector IN (SELECT id FROM fed) "
			   "), offenders AS ( "
			   "  SELECT ship_id, sector, owner_player_id "
			   "  FROM crowded WHERE rn > ?1 "
			   "  LIMIT ?2 "
			   ") "
			   "SELECT ship_id, sector, owner_player_id FROM offenders;",
			   -1, &q_over, NULL);
  if (rc != SQLITE_OK)
    {
      sqlite3_finalize (i_notice);
      sqlite3_finalize (u_ship);
      sqlite3_finalize (q_tow);
      rollback (db);
      LOGE ("fedspace_cleanup prep overcrowded rc=%d", rc);
      unlock (db, "fedspace_cleanup");
      return rc;
    }
  else
    LOGI ("Cleared ship from fed from fedspace to %s", (char *) &q_over);

  sqlite3_bind_int (q_over, 1, MAX_SHIPS_PER_FED_SECTOR);
  sqlite3_bind_int (q_over, 2, MAX_TOWS_PER_PASS);

  while (tows < MAX_TOWS_PER_PASS && sqlite3_step (q_over) == SQLITE_ROW)
    {
      int ship_id = sqlite3_column_int (q_over, 0);
      int sec = sqlite3_column_int (q_over, 1);
      int owner = sqlite3_column_int (q_over, 2);

      /* compute tow target */
      int tow_to = 1;		// this should probably just be a random number between 11-500
      LOGI ("Q_TOW=%s", (char *) q_tow);
      sqlite3_reset (q_tow);
      sqlite3_clear_bindings (q_tow);
      sqlite3_bind_int (q_tow, 1, sec);
      if (sqlite3_step (q_tow) == SQLITE_ROW)
	{
	  tow_to = sqlite3_column_int (q_tow, 0);
	}
      sqlite3_reset (q_tow);

      /* move ship */
      sqlite3_reset (u_ship);
      sqlite3_clear_bindings (u_ship);
      sqlite3_bind_int (u_ship, 1, tow_to);
      sqlite3_bind_int (u_ship, 2, ship_id);
      if (sqlite3_step (u_ship) != SQLITE_DONE)
	{
	  rc = SQLITE_ERROR;
	  break;
	}

      /* owner notice */
      sqlite3_reset (i_notice);
      sqlite3_clear_bindings (i_notice);
      sqlite3_bind_int (i_notice, 1, owner);
      sqlite3_bind_text (i_notice, 2,
			 "Federation tow: sector overcrowded; your ship was relocated to a nearby non-Fed sector.",
			 -1, SQLITE_STATIC);
      sqlite3_bind_int64 (i_notice, 3, now_s);
      if (sqlite3_step (i_notice) != SQLITE_DONE)
	{
	  rc = SQLITE_ERROR;
	  break;
	}

      tows++;
    }

  sqlite3_finalize (q_over);

  if (rc == SQLITE_OK && tows < MAX_TOWS_PER_PASS)
    {
      /* 4) Fighter cap: tow ships carrying >= 99 fighters while in FedSpace (bounded by remaining budget) */
      int remaining = MAX_TOWS_PER_PASS - tows;
      sqlite3_stmt *q_fcap = NULL;
      rc = sqlite3_prepare_v2 (db,
			       "SELECT s.id AS ship_id, s.sector, s.owner_player_id "
			       "FROM ships s "
			       "WHERE s.sector IN (SELECT id FROM sectors WHERE in (1,2,3,4,5,6,7,8,9,10)) "
			       "  AND s.fighters >= ?1 "
			       "LIMIT ?2;", -1, &q_fcap, NULL);
      if (rc == SQLITE_OK)
	{
	  sqlite3_bind_int (q_fcap, 1, MAX_FED_FIGHTERS + 1);	/* >= 99 */
	  sqlite3_bind_int (q_fcap, 2, remaining);

	  while (tows < MAX_TOWS_PER_PASS
		 && sqlite3_step (q_fcap) == SQLITE_ROW)
	    {
	      int ship_id = sqlite3_column_int (q_fcap, 0);
	      int sec = sqlite3_column_int (q_fcap, 1);
	      int owner = sqlite3_column_int (q_fcap, 2);

	      int tow_to = 1;
	      sqlite3_reset (q_tow);
	      sqlite3_clear_bindings (q_tow);
	      sqlite3_bind_int (q_tow, 1, sec);
	      if (sqlite3_step (q_tow) == SQLITE_ROW)
		{
		  tow_to = sqlite3_column_int (q_tow, 0);
		}
	      sqlite3_reset (q_tow);

	      sqlite3_reset (u_ship);
	      sqlite3_clear_bindings (u_ship);
	      sqlite3_bind_int (u_ship, 1, tow_to);
	      sqlite3_bind_int (u_ship, 2, ship_id);
	      if (sqlite3_step (u_ship) != SQLITE_DONE)
		{
		  rc = SQLITE_ERROR;
		  break;
		}

	      sqlite3_reset (i_notice);
	      sqlite3_clear_bindings (i_notice);
	      sqlite3_bind_int (i_notice, 1, owner);
	      sqlite3_bind_text (i_notice, 2,
				 "Federation tow: carrying too many fighters in FedSpace; your ship was relocated.",
				 -1, SQLITE_STATIC);
	      sqlite3_bind_int64 (i_notice, 3, now_s);
	      if (sqlite3_step (i_notice) != SQLITE_DONE)
		{
		  rc = SQLITE_ERROR;
		  break;
		}

	      tows++;
	    }
	  sqlite3_finalize (q_fcap);
	}
    }

  sqlite3_finalize (i_notice);
  sqlite3_finalize (u_ship);
  sqlite3_finalize (q_tow);

  if (rc != SQLITE_OK)
    {
      rollback (db);
      LOGE ("fedspace_cleanup tow rc=%d (towed=%d)", rc, tows);
      unlock (db, "fedspace_cleanup");
      return rc;
    }

  commit (db);
  LOGI ("fedspace_cleanup: ok (towed=%d)", tows);
  unlock (db, "fedspace_cleanup");
  return 0;
}


// In ../src/server_cron.c (or a dedicated cron job file)

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
		     "UPDATE players SET turns = (SELECT turnsperday FROM config WHERE id=1);",
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

  LOGI ("daily_turn_reset: ok");
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
  LOGI ("autouncloak_sweeper: ok");
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

  rc = sqlite3_exec (db,
		     "UPDATE ports SET credits=MAX(credits, 500000000), fuel_stock=1000000, organics_stock=1000000 "
		     "WHERE is_terra=1;", NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("terra_replenish rc=%d", rc);
      return rc;
    }
  commit (db);
  LOGI ("terra_replenish: ok");
  unlock (db, "terra_replenish");
  return 0;
}

int
h_port_reprice (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "port_reprice", now_s))
    return 0;
  (void) now_s;
  int rc = begin (db);
  if (rc)
    return rc;

  /* Placeholder: gentle mean reversion toward baseline prices. */
  rc = sqlite3_exec (db,
		     "UPDATE ports SET "
		     " fuel_price = (fuel_price*9 + baseline_fuel_price)/10, "
		     " org_price  = (org_price*9  + baseline_org_price)/10, "
		     " equip_price= (equip_price*9+ baseline_equip_price)/10;",
		     NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("port_reprice rc=%d", rc);
      return rc;
    }
  commit (db);
  LOGI ("port_reprice: ok");
  unlock (db, "port_reprice");
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

  rc = sqlite3_exec (db, "UPDATE planets SET " " population = population + CAST(population*0.001 AS INT), "	/* +0.1% */
		     " ore_stock  = MIN(ore_cap,  ore_stock  + 50), "
		     " org_stock  = MIN(org_cap,  org_stock  + 50), "
		     " equip_stock= MIN(equip_cap,equip_stock+ 50);",
		     NULL, NULL, NULL);

  if (rc)
    {
      rollback (db);
      LOGE ("planet_growth rc=%d", rc);
      return rc;
    }
  commit (db);
  LOGI ("planet_growth: ok");
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
  LOGI ("broadcast_ttl_cleanup: ok");
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
  LOGI ("traps_process: ok");
  unlock (db, "traps_process");
  return 0;
}

int
h_npc_step (sqlite3 *db, int64_t now_s)
{
  (void) db;
  (void) now_s;
  /* No-op for now; handled elsewhere. Consider disabling cron_tasks.enabled for npc_step. */
  // LOGD("npc_step noop");
  return 0;
}
