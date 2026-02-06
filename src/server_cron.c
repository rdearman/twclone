#include <unistd.h>		// For usleep()
#include <jansson.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <math.h>		// For MAX, MIN, etc.

// local includes
#include "server_cron.h"
#include "server_log.h"
#include "common.h"
#include "server_players.h"
#include "server_universe.h"
#include "server_ports.h"
#include "server_planets.h"
#include "repo_citadel.h"
#include "game_db.h"
#include "server_config.h"
#include "repo_market.h"	// For market order helpers
#include "repo_cmd.h"		// For bank helpers
#include "repo_cron.h"		// For cron helpers
#include "server_stardock.h"	// For Tavern-related declarations
#include "server_corporation.h"	// For corporation cron jobs
#include "server_clusters.h"	// Cluster Economy & Law
#include "db/db_api.h"
#include "db/sql_driver.h"

int iss_init_once (void);
#define INITIAL_QUEUE_CAPACITY 64
#define FEDSPACE_SECTOR_START 1
#define FEDSPACE_SECTOR_END 10
#define LOGOUT_TIMEOUT_S (60 * 60)
#define CONFISCATION_SECTOR 0
#define MIN_UNPROTECTED_SECTOR 11
#define MAX_UNPROTECTED_SECTOR 999
#define RANGE_SIZE (MAX_UNPROTECTED_SECTOR - MIN_UNPROTECTED_SECTOR + 1)
#define NEWS_EXPIRATION_SECONDS 604800L
#define MAX_ARTICLE_LEN 512
#define MSL_TABLE_NAME "msl_sectors"


/* --- ADD TO TOP OF FILE (Declarations section) --- */
/* These helpers allow us to yield the C-level lock while keeping the DB handle open */
int h_daily_news_compiler (db_t * db, int64_t now_s);
int h_cleanup_old_news (db_t * db, int64_t now_s);
int h_citadel_construction_reap (db_t * db, int64_t now_s);


static inline uint64_t
monotonic_millis (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}


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
int cron_limpet_ttl_cleanup (db_t * db, int64_t now_s);	// Forward declaration
// static int g_reg_inited = 0;


int
get_random_sector (db_t *db)
{
  (void) db;			// no longer use the db use preloaded config data.
  int random_offset = rand () % g_cfg.default_nodes;
  int random_sector = 11 + random_offset;


  return random_sector;
}


int
tow_ship (db_t *db, int ship_id, int new_sector_id, int admin_id,
	  int reason_code)
{
  db_error_t err;
  db_error_clear (&err);

  int owner_id = 0;
  int old_sector_id = 0;


  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      return -1;
    }

  if (db_cron_get_ship_info (db, ship_id, &old_sector_id, &owner_id) != 0)
    {
      LOGE ("tow_ship: Query SELECT failed for ship %d", ship_id);
      db_tx_rollback (db, &err);
      return -1;
    }

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

  if (db_cron_tow_ship (db, ship_id, new_sector_id, owner_id) != 0)
    {
      LOGE ("tow_ship: Update failed for ship %d owner %d", ship_id,
	    owner_id);
      db_tx_rollback (db, &err);
      return -1;
    }

  if (owner_id > 0)
    {
      char message_buffer[256];


      snprintf (message_buffer,
		sizeof (message_buffer),
		"Your ship was found parked in FedSpace (Sector %d) without protection. It has been towed to Sector %d for violating FedLaw: %s. The ship is now exposed to danger.",
		old_sector_id, new_sector_id, reason_str);
      h_send_message_to_player (db, admin_id, owner_id, subject_str,
				message_buffer);
    }

  LOGI
    ("TOW: Ship %d (Owner %d) towed from sector %d to sector %d. Reason: %s (Code %d). Admin: %d.",
     ship_id,
     owner_id,
     old_sector_id, new_sector_id, reason_str, reason_code, admin_id);

  int64_t current_time_s = (int64_t) time (NULL);
  json_t *fedspace_payload = json_object ();


  if (fedspace_payload)
    {
      json_object_set_new (fedspace_payload, "reason",
			   json_string (reason_str));
      json_object_set_new (fedspace_payload, "owner_id",
			   json_integer (owner_id));
      json_object_set_new (fedspace_payload, "ship_id",
			   json_integer (ship_id));
      db_log_engine_event (current_time_s, "fedspace:tow", NULL, admin_id,
			   old_sector_id, fedspace_payload, NULL);
      json_decref (fedspace_payload);
    }
  else
    {
      LOGE ("Failed to create JSON payload for fedspace:tow event.");
    }

  if (!db_tx_commit (db, &err))
    {
      LOGE ("tow_ship: commit failed: %s", err.message);
      db_tx_rollback (db, &err);
      return -1;
    }

  return 0;
}



static int *
universe_pathfind_get_sectors (db_t *db, int start_sector, int end_sector,
			       const int *avoid_list)
{
  if (start_sector == end_sector)
    {
      int *path = malloc (2 * sizeof (int));
      if (!path)
	{
	  return NULL;
	}
      path[0] = start_sector;
      path[1] = 0;
      return path;
    }
  int max_sector_id = 0;
  if (db_cron_get_max_sector_id (db, &max_sector_id) != 0)
    {
      // non-fatal, fallback handled below
    }

  if (max_sector_id <= 0)
    {
      max_sector_id = 10000;
    }

  if (start_sector > max_sector_id)
    max_sector_id = start_sector;
  if (end_sector > max_sector_id)
    max_sector_id = end_sector;

  int *parent = calloc (max_sector_id + 1, sizeof (int));


  if (!parent)
    {
      return NULL;
    }
  int *queue = malloc (INITIAL_QUEUE_CAPACITY * sizeof (int));
  int queue_head = 0;
  int queue_tail = 0;
  int queue_capacity = INITIAL_QUEUE_CAPACITY;


  if (!queue)
    {
      free (parent);
      return NULL;
    }
  if (avoid_list)
    {
      for (const int *avoid = avoid_list; *avoid != 0; avoid++)
	{
	  if (*avoid > 0 && *avoid <= max_sector_id)
	    {
	      parent[*avoid] = -2;
	    }
	}
    }
  parent[start_sector] = -1;
  queue[queue_tail++] = start_sector;
  int path_found = 0;

  while (queue_head < queue_tail)
    {
      int current_sector = queue[queue_head++];


      if (current_sector == end_sector)
	{
	  path_found = 1;
	  break;
	}

      int *neighbors = NULL;
      int neighbor_count = 0;

      if (db_cron_get_sector_warps
	  (db, current_sector, &neighbors, &neighbor_count) == 0)
	{
	  for (int i = 0; i < neighbor_count; i++)
	    {
	      int neighbor = neighbors[i];

	      if (neighbor <= 0 || neighbor > max_sector_id
		  || parent[neighbor] != 0)
		{
		  continue;
		}
	      parent[neighbor] = current_sector;
	      if (queue_tail == queue_capacity)
		{
		  queue_capacity *= 2;
		  int *new_queue = realloc (queue,
					    queue_capacity * sizeof (int));


		  if (!new_queue)
		    {
		      path_found = 0;
		      queue_tail = 0;
		      break;
		    }
		  queue = new_queue;
		}
	      queue[queue_tail++] = neighbor;
	    }
	  free (neighbors);
	}
    }

  free (queue);
  if (!path_found)
    {
      free (parent);
      return NULL;
    }
  int path_length = 0;
  int temp_sector = end_sector;


  while (temp_sector != -1)
    {
      path_length++;
      if (path_length > max_sector_id + 1)
	{
	  free (parent);
	  return NULL;
	}
      temp_sector = parent[temp_sector];
    }
  int *result_path = malloc ((path_length + 1) * sizeof (int));


  if (!result_path)
    {
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
  result_path[path_length] = 0;
  free (parent);
  return result_path;
}


static void
_insert_path_sectors (db_t *db, int start_sector,
		      int end_sector, const int *avoid_list,
		      int *total_unique_sectors_added)
{
  int *s;
  int *current_path =
    universe_pathfind_get_sectors (db, start_sector, end_sector, avoid_list);
  if (!current_path)
    {
      LOGW ("Could not find path from %d to %d. Check universe connections.",
	    start_sector, end_sector);
      return;
    }

  for (s = current_path; *s != 0; s++)
    {
      if (db_cron_msl_insert (db, *s, total_unique_sectors_added) != 0)
	{
	  LOGW ("SQL warning inserting sector %d for path %d->%d", *s,
		start_sector, end_sector);
	}
    }
  free (current_path);
}


int
populate_msl_if_empty (db_t *db)
{
  const int *avoid_list = NULL;
  int total_sectors_in_table = 0;

  if (db_cron_msl_count (db, &total_sectors_in_table) != 0)
    {
      // Log error?
    }

  if (total_sectors_in_table > 0)
    {
      LOGD
	("[cron] %s table already populated with %d entries. Skipping MSL calculation.",
	 MSL_TABLE_NAME, total_sectors_in_table);
      return 0;
    }
  LOGD
    ("[cron] %s table is empty. Starting comprehensive MSL path calculation (FedSpace 1-10 <-> Stardocks)...",
     MSL_TABLE_NAME);

  if (db_cron_create_msl_table (db) != 0)
    {
      LOGE ("[cron] SQL error creating %s table", MSL_TABLE_NAME);
      return -1;
    }

  int *stardock_sectors = NULL;
  int stardock_count = 0;

  if (db_cron_get_stardock_locations (db, &stardock_sectors, &stardock_count)
      != 0)
    {
      LOGE ("[cron] SQL error preparing Stardock select");
      return -1;
    }

  if (stardock_count == 0)
    {
      LOGW
	("[cron] No stardock locations found in stardock_location table. Skipping MSL calculation.");
      free (stardock_sectors);
      return 0;
    }

  db_error_t err;
  db_error_clear (&err);
  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      LOGE ("[cron] SQL error starting master transaction: %s", err.message);
      free (stardock_sectors);
      return -1;
    }
  int total_unique_sectors_added = 0;


  for (int start_sector = FEDSPACE_SECTOR_START;
       start_sector <= FEDSPACE_SECTOR_END; start_sector++)
    {
      for (int i = 0; i < stardock_count; i++)
	{
	  int stardock_id = stardock_sectors[i];


	  LOGI ("Calculating path %d -> %d", start_sector, stardock_id);
	  _insert_path_sectors (db, start_sector, stardock_id,
				avoid_list, &total_unique_sectors_added);
	  if (start_sector != stardock_id)
	    {
	      LOGI ("[cron] Calculating path %d -> %d (Reverse)", stardock_id,
		    start_sector);
	      _insert_path_sectors (db, stardock_id, start_sector,
				    avoid_list, &total_unique_sectors_added);
	    }
	}
    }

  free (stardock_sectors);
  if (!db_tx_commit (db, &err))
    {
      LOGE ("[cron] SQL error committing master path transaction: %s", err.message);
      return -1;
    }
  LOGI ("[cron] Completed MSL setup. Populated %s with %d total unique sectors.",
	MSL_TABLE_NAME, total_unique_sectors_added);
  return 0;
}


int
h_reset_turns_for_player (db_t *db, int64_t now_s)
{
  int max_turns = 0;
  int updated_count = 0;

  /* 1. Get turnsperday config */
  max_turns = (int) h_get_config_int_unlocked (db, "turnsperday", 0);

  if (max_turns <= 0)
    {
      LOGE ("Turn reset failed: turnsperday is %d or missing in config.",
	    max_turns);
      return -1;
    }

  if (db_cron_reset_turns_for_all_players
      (db, max_turns, now_s, &updated_count) != 0)
    {
      LOGE ("Turn reset transaction failed");
      return -1;
    }

  LOGI ("Successfully reset turns for %d players to %d.", updated_count,
	max_turns);
  return 0;
}


int
try_lock (db_t *db, const char *name, int64_t now_s)
{
  return db_cron_try_lock (db, name, now_s);
}


int64_t
db_lock_status (db_t *db, const char *name)
{
  return db_cron_get_lock_until (db, name);
}


void
unlock (db_t *db, const char *name)
{
  db_cron_unlock (db, name);
}


int
cron_register_builtins (void)
{
  return 0;
}


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


int
uncloak_ships_in_fedspace (db_t *db)
{
  if (db_cron_uncloak_fedspace_ships (db) != 0)
    {
      LOGE ("SQL error in uncloak_ships_in_fedspace");
      return -1;
    }
  return 0;
}


/* REPLACEMENT for h_fedspace_cleanup in src/server_cron.c */
int
h_fedspace_cleanup (db_t *db, int64_t now_s)
{
  int fedadmin = 2;
  int cleared_assets = 0;
  int tows = 0;


  /* 1. Acquire Cron Lock */
  if (!try_lock (db, "fedspace_cleanup", now_s))
    {
      return 0;
    }
  LOGI ("[cron] Lock acquired, starting cleanup operations.");

  /* 2. Heavy Ops: Uncloak & MSL & Clusters (These are largely idempotent/fast or own their own tx) */
  int uncloak = uncloak_ships_in_fedspace (db);


  if (uncloak > 0)
    {
      LOGI ("Uncloaked %d ships in FedSpace.", uncloak);
    }

  // Populate MSL if needed (this handles its own transaction internally)
  if (populate_msl_if_empty (db) != 0)
    {
      LOGE ("[cron] MSL population failed.");
    }

  // Init Clusters (idempotent)
  if (clusters_init (db) != 0)
    {
      LOGE ("[cron] Cluster init failed.");
    }
  clusters_seed_illegal_goods (db);

  /* 3. Illegal Assets on MSL */
  json_t *illegal_assets = NULL;
  if (db_cron_get_illegal_assets_json (db, &illegal_assets) == 0)
    {
      char message[256];
      size_t index;
      json_t *value;

      json_array_foreach (illegal_assets, index, value)
      {
	int player_id =
	  json_integer_value (json_object_get (value, "player_id"));
	int asset_type =
	  json_integer_value (json_object_get (value, "asset_type"));
	int sector_id =
	  json_integer_value (json_object_get (value, "sector_id"));
	int quantity =
	  json_integer_value (json_object_get (value, "quantity"));

	// Send message
	snprintf (message,
		  sizeof (message),
		  "%d %s(s) deployed in Sector %d (Major Space Lane) were destroyed by Federal Authorities.",
		  quantity, get_asset_name (asset_type), sector_id);
	h_send_message_to_player (db, player_id,
				  fedadmin,
				  "WARNING: MSL Violation", message);

	// Delete asset
	if (db_cron_delete_sector_asset
	    (db, player_id, asset_type, sector_id, quantity) == 0)
	  {
	    cleared_assets++;
	  }
      }
      json_decref (illegal_assets);
    }
  usleep (10000);		// Yield

  /* 3b. MSL Cleanse: Remove fighters/mines/beacons from MSL sectors > 10 */
  json_t *msl_cleanse_assets = NULL;
  if (db_cron_cleanse_msl_assets (db, &msl_cleanse_assets) == 0)
    {
      char message[256];
      size_t index;
      json_t *value;
      int fighters_removed = 0, mines_removed = 0, beacons_removed = 0;

      json_array_foreach (msl_cleanse_assets, index, value)
      {
        int player_id =
          json_integer_value (json_object_get (value, "player_id"));
        int asset_type =
          json_integer_value (json_object_get (value, "asset_type"));
        int sector_id =
          json_integer_value (json_object_get (value, "sector_id"));
        int quantity =
          json_integer_value (json_object_get (value, "quantity"));

        const char *asset_name = NULL;
        if (asset_type == 2) {
          asset_name = "Fighter";
          fighters_removed += quantity;
        } else if (asset_type == 1) {
          asset_name = "Beacon";
          beacons_removed += quantity;
        } else if (asset_type == 4) {
          asset_name = "Mine";
          mines_removed += quantity;
        } else {
          asset_name = "Unknown Asset";
        }

        snprintf (message,
                  sizeof (message),
                  "%d %s(s) deployed in Sector %d (Major Space Lane) were destroyed by Federal Patrols.",
                  quantity, asset_name, sector_id);
        h_send_message_to_player (db, player_id,
                                  fedadmin,
                                  "MSL Sweep: Assets Destroyed", message);

        if (db_cron_delete_sector_asset
            (db, player_id, asset_type, sector_id, quantity) == 0)
          {
            cleared_assets++;
          }
      }
      if (fighters_removed + mines_removed + beacons_removed > 0) {
        LOGI ("MSL cleanse: Removed %d fighters, %d mines, %d beacons in MSL sectors > 10",
              fighters_removed, mines_removed, beacons_removed);
      }
      json_decref (msl_cleanse_assets);
    }
  usleep (10000);		// Yield

  /* 4. Logout Timeout (New Transaction) */

  // Compute cutoff epoch in C to avoid arithmetic in SQL
  int64_t logout_cutoff = now_s - LOGOUT_TIMEOUT_S;

  db_cron_logout_inactive_players (db, logout_cutoff);

  /* 5. Prepare Towing Table */

  // Compute stale cutoff in C to avoid arithmetic in SQL
  int64_t stale_cutoff = now_s - (12 * 60 * 60);

  db_cron_init_eligible_tows (db, FEDSPACE_SECTOR_START, FEDSPACE_SECTOR_END,
			      stale_cutoff);

  usleep (10000);		// Yield

  /* Get random sector for confiscated ships */
  int confiscation_sector = get_random_sector (db);
  if (confiscation_sector <= 0)
    {
      LOGE ("[cron] Fedspace Clean Up: Could not get random sector");
      unlock (db, "fedspace_cleanup");
      return -1;
    }


  /* Towing Logic */

  /* A. Evil Alignment Tows */
  if (tows < MAX_TOWS_PER_PASS)
    {
      json_t *list = NULL;
      if (db_cron_get_eligible_tows_json
	  (db, "evil", MAX_TOWS_PER_PASS - tows, &list) == 0)
	{
	  size_t index;
	  json_t *val;
	  json_array_foreach (list, index, val)
	  {
	    int ship_id = (int) json_integer_value (val);
	    tow_ship (db, ship_id, get_random_sector (db), fedadmin,
		      REASON_EVIL_ALIGN);
	    tows++;
	  }
	  json_decref (list);
	}
    }

  /* B. Excess Fighters Tows */
  if (tows < MAX_TOWS_PER_PASS)
    {
      json_t *list = NULL;
      if (db_cron_get_eligible_tows_json
	  (db, "fighters", MAX_TOWS_PER_PASS - tows, &list) == 0)
	{
	  size_t index;
	  json_t *val;
	  json_array_foreach (list, index, val)
	  {
	    int ship_id = (int) json_integer_value (val);
	    tow_ship (db, ship_id, get_random_sector (db), fedadmin,
		      REASON_EXCESS_FIGHTERS);
	    tows++;
	  }
	  json_decref (list);
	}
    }

  /* C. High Exp Tows */
  if (tows < MAX_TOWS_PER_PASS)
    {
      json_t *list = NULL;
      if (db_cron_get_eligible_tows_json
	  (db, "exp", MAX_TOWS_PER_PASS - tows, &list) == 0)
	{
	  size_t index;
	  json_t *val;
	  json_array_foreach (list, index, val)
	  {
	    int ship_id = (int) json_integer_value (val);
	    tow_ship (db, ship_id, get_random_sector (db), fedadmin,
		      REASON_HIGH_EXP);
	    tows++;
	  }
	  json_decref (list);
	}
    }

  /* D. No Owner */
  if (tows < MAX_TOWS_PER_PASS)
    {
      json_t *list = NULL;
      if (db_cron_get_eligible_tows_json
	  (db, "no_owner", MAX_TOWS_PER_PASS - tows, &list) == 0)
	{
	  size_t index;
	  json_t *val;
	  json_array_foreach (list, index, val)
	  {
	    int ship_id = (int) json_integer_value (val);
	    tow_ship (db, ship_id, confiscation_sector, fedadmin,
		      REASON_NO_OWNER);
	    tows++;
	  }
	  json_decref (list);
	}
    }

  /* Final Cleanup */
  db_cron_clear_eligible_tows (db);
  LOGI ("[cron] Fedspace Clean Up (towed=%d)", tows);
  unlock (db, "fedspace_cleanup");
  return 0;
}


// Helper function to convert Unix epoch seconds to UTC epoch day (YYYYMMDD)
int64_t
get_utc_epoch_day (int64_t unix_timestamp)
{
  time_t rawtime = unix_timestamp;
  struct tm *info;
  // Use gmtime for UTC
  info = gmtime (&rawtime);
  if (info == NULL)
    {
      return 0;			// Error
    }
  // Return YYYYMMDD
  return (info->tm_year + 1900) * 10000 + (info->tm_mon + 1) * 100 +
    info->tm_mday;
}


int
h_robbery_daily_cleanup (db_t *db, int64_t now_s)
{
  // 1. Suspicion Decay (10% daily)
  if (db_cron_robbery_decay_suspicion (db) != 0)
    {
      LOGE ("h_robbery_daily_cleanup: Suspicion decay failed");
    }

  // 2. Clear Busts
  if (db_cron_robbery_clear_busts (db, now_s) != 0)
    {
      LOGE ("h_robbery_daily_cleanup: Bust clear failed");
    }
  return 0;
}


int
h_daily_turn_reset (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "daily_turn_reset", now_s))
    {
      return 0;
    }
  LOGI ("daily_turn_reset: starting daily turn reset.");

  int turns = (int) h_get_config_int_unlocked (db, "turnsperday", 0);
  if (db_cron_reset_daily_turns (db, turns) != 0)
    {
      LOGE ("daily_turn_reset: player turn update failed");
      unlock (db, "daily_turn_reset");
      return -1;
    }

  // Call Robbery Cleanup as part of daily maintenance
  h_robbery_daily_cleanup (db, now_s);

  unlock (db, "daily_turn_reset");
  return 0;
}


int
h_autouncloak_sweeper (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "autouncloak_sweeper", now_s))
    {
      return 0;
    }

  int max_hours =
    (int) h_get_config_int_unlocked (db, "max_cloak_duration", 0);

  if (max_hours <= 0)
    {
      LOGI
	("autouncloak_sweeper: max_cloak_duration is zero/invalid. Skipping sweep.");
      unlock (db, "autouncloak_sweeper");
      return 0;
    }
  const int SECONDS_IN_HOUR = 3600;
  int64_t max_duration_seconds = (int64_t) max_hours * SECONDS_IN_HOUR;
  int64_t uncloak_threshold_s = now_s - max_duration_seconds;

  if (db_cron_autouncloak_ships (db, uncloak_threshold_s) != 0)
    {
      LOGE ("Can't prepare ships UPDATE");
    }

  unlock (db, "autouncloak_sweeper");
  return 0;
}


int
h_terra_replenish (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "terra_replenish", now_s))
    {
      return 0;
    }
  (void) now_s;

  if (db_cron_terra_replenish (db) != 0)
    {
      LOGE ("terra_replenish failed");
      unlock (db, "terra_replenish");
      return -1;
    }
  unlock (db, "terra_replenish");
  return 0;
}


int
h_planet_population_tick (db_t *db, int64_t now_s)
{
  (void) now_s;
  const double GROWTH_RATE = 0.05;

  if (db_cron_planet_pop_growth_tick (db, GROWTH_RATE) != 0)
    {
      LOGE ("h_planet_population_tick: update failed");
      return -1;
    }
  return 0;
}


int
h_planet_treasury_interest_tick (db_t *db, int64_t now_s)
{
  LOGI ("[cron] Planet Treasury Interest cron disabled for v1.0.");
  (void) db;			// Suppress unused parameter warning
  (void) now_s;			// Suppress unused parameter warning
  return 0;			// Do nothing, cleanly exit

  (void) now_s;

  int rate_bps =
    (int) h_get_config_int_unlocked (db, "planet_treasury_interest_rate_bps",
				     0);

  if (rate_bps <= 0)
    {
      rate_bps = 100;		// Default: 1.00%
    }

  if (db_cron_citadel_treasury_tick (db, rate_bps) != 0)
    {
      LOGE ("h_planet_treasury_interest_tick: failed");
      return -1;
    }
  return 0;
}


int
h_planet_growth (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "planet_growth", now_s))
    {
      return 0;
    }

  // 1. Population Growth
  if (h_planet_population_tick (db, now_s) != 0)
    {
      LOGE ("h_planet_growth: Population tick failed.");
    }

  // 2. Treasury Interest (T2)
  if (h_planet_treasury_interest_tick (db, now_s) != 0)
    {
      LOGE ("h_planet_growth: Treasury tick failed.");
    }

    // --- NEW: Update commodity quantities in entity_stock based on planet_production ---

    if (db_cron_planet_update_production_stock (db, now_s) != 0)

      {

        LOGE ("planet_growth (commodities exec) failed");

        // Continue or return? Original returned rc.

        unlock (db, "planet_growth");

        return -1;

      }

    // --- END NEW COMMODITY UPDATE ---

  

    // Now call the new market tick for planets

    h_planet_market_tick (db, now_s);

  

    unlock (db, "planet_growth");

    return 0;

  }


// New function to handle market-related planet ticks (order generation)
int
h_planet_market_tick (db_t *db, int64_t now_s)
{
  (void) now_s;
  json_t *planets = NULL;
  if (db_cron_planet_get_market_data_json (db, &planets) != 0)
    {
      LOGE ("h_planet_market_tick: Failed to get planet data");
      return -1;
    }

  size_t index;
  json_t *p_data;
  json_array_foreach (planets, index, p_data)
  {
    int planet_id =
      json_integer_value (json_object_get (p_data, "planet_id"));
    int maxore = json_integer_value (json_object_get (p_data, "maxore"));
    int maxorganics =
      json_integer_value (json_object_get (p_data, "maxorganics"));
    int maxequipment =
      json_integer_value (json_object_get (p_data, "maxequipment"));
    const char *commodity_code =
      json_string_value (json_object_get (p_data, "commodity_code"));
    int commodity_id =
      json_integer_value (json_object_get (p_data, "commodity_id"));
    int current_quantity =
      json_integer_value (json_object_get (p_data, "current_quantity"));
    int base_price =
      json_integer_value (json_object_get (p_data, "base_price"));
    int owner_id = json_integer_value (json_object_get (p_data, "owner_id"));
    const char *owner_type =
      json_string_value (json_object_get (p_data, "owner_type"));

    // --- PE1: NPC Check ---
    bool is_npc = false;

    if (owner_id == 0)
      {
	is_npc = true;
      }
    else
      {
	if (owner_type)
	  {
	    if (strcasecmp (owner_type, "player") == 0 ||
		strcasecmp (owner_type, "corp") == 0 ||
		strcasecmp (owner_type, "corporation") == 0)
	      {
		is_npc = false;
	      }
	    else
	      {
		is_npc = true;	// Other types assumed NPC
	      }
	  }
	else
	  {
	    is_npc = true;	// No type, owned? Assume NPC/System
	  }
      }

    if (!is_npc)
      continue;
    // ----------------------

    if (!commodity_code)
      continue;

    int max_capacity = 0;

    if (strcasecmp (commodity_code, "ORE") == 0)
      max_capacity = maxore;
    else if (strcasecmp (commodity_code, "ORG") == 0)
      max_capacity = maxorganics;
    else if (strcasecmp (commodity_code, "EQU") == 0)
      max_capacity = maxequipment;
    else
      max_capacity = 999999;

    int desired_stock = max_capacity / 2;
    int shortage = 0;
    int surplus = 0;

    if (desired_stock > current_quantity)
      shortage = desired_stock - current_quantity;
    else if (current_quantity > desired_stock)
      surplus = current_quantity - desired_stock;

    int order_qty = 0;
    const char *side = NULL;
    const double planet_order_fraction = 0.1;

    if (shortage > 0)
      {
	order_qty = (int) (shortage * planet_order_fraction);
	side = "buy";
      }
    else if (surplus > 0)
      {
	order_qty = (int) (surplus * planet_order_fraction);
	side = "sell";
      }

    if ((shortage > 0 || surplus > 0) && order_qty == 0)
      order_qty = 1;

    if (order_qty > 0 && side != NULL)
      {
	int price = base_price;
	commodity_order_t existing_order;
	int find_rc =
	  db_get_open_order (db, "planet", planet_id, commodity_id, side,
			     &existing_order);

	if (find_rc == 0)
	  {
	    int new_total = existing_order.filled_quantity + order_qty;
	    db_update_commodity_order (db, existing_order.id, new_total,
				       existing_order.filled_quantity,
				       "open");
	  }
	else
	  {
	    db_insert_commodity_order (db, "planet", planet_id, "planet",
				       planet_id, commodity_id, side,
				       order_qty, price, 0);
	  }
      }
    else
      {
	db_cancel_commodity_orders_for_actor_and_commodity (db, "planet",
							    planet_id,
							    commodity_id,
							    "buy");
	db_cancel_commodity_orders_for_actor_and_commodity (db, "planet",
							    planet_id,
							    commodity_id,
							    "sell");
      }
  }

  json_decref (planets);
  return 0;
}


int
h_broadcast_ttl_cleanup (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "broadcast_ttl_cleanup", now_s))
    {
      return 0;
    }

  if (db_cron_broadcast_cleanup (db, now_s) != 0)
    {
      // Log if needed
    }

  unlock (db, "broadcast_ttl_cleanup");
  return 0;
}


int
h_traps_process (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "traps_process", now_s))
    {
      return 0;
    }

  if (db_cron_traps_process (db, now_s) != 0)
    {
      LOGE ("h_traps_process: failed");
      unlock (db, "traps_process");
      return -1;
    }

  unlock (db, "traps_process");
  return 0;
}


int
h_npc_step (db_t *db, int64_t now_s)
{
  (void) now_s;
  int64_t now_ms = (int64_t) monotonic_millis ();


  if (iss_init_once () == 1)
    {
      iss_tick (db, now_ms);
    }

  if (fer_init_once (db) == 1)
    {
      fer_attach_db (db);
      fer_tick (db, now_ms);
    }

  if (ori_init_once (db) == 1)
    {
      ori_attach_db (db);
      ori_tick (db, now_ms);
    }

  return 0;
}


int
cmd_sys_cron_planet_tick_once (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0 ||
      auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_PERMISSION_DENIED,
				   "Permission denied", NULL);
      return 0;
    }

  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR,
			   "Database unavailable");
      return 0;
    }

  int64_t now_s = time (NULL);


  // Call the main planet growth handler, which also orchestrates the market tick
  if (h_planet_growth (db, now_s) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR, "Planet cron tick failed.");
      return 0;
    }

  send_response_ok_take (ctx, root, "sys.cron.planet_tick_once.success",
			 NULL);
  return 0;
}

int
cmd_sys_cron_citadel_reap_once (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0 ||
      auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_PERMISSION_DENIED,
				   "Permission denied", NULL);
      return 0;
    }

  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR,
			   "Database unavailable");
      return 0;
    }

  int64_t now_s = time (NULL);

  if (h_citadel_construction_reap (db, now_s) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR, "Citadel reap cron failed.");
      return 0;
    }

  send_response_ok_take (ctx, root, "sys.cron.citadel_reap_once.success",
			 NULL);
  return 0;
}


int
h_port_economy_tick (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "port_economy_tick", now_s))
    {
      return 0;
    }
  LOGI ("port_economy_tick: Starting port economy update.");

  json_t *ports_data = NULL;
  if (db_cron_port_get_economy_data_json (db, &ports_data) != 0)
    {
      LOGE ("port_economy_tick: Failed to get data");
      unlock (db, "port_economy_tick");
      return -1;
    }

  int orders_processed = 0;
  size_t index;
  json_t *item;

  json_array_foreach (ports_data, index, item)
  {
    int port_id = json_integer_value (json_object_get (item, "port_id"));
    int port_type = json_integer_value (json_object_get (item, "port_type"));
    int port_size = json_integer_value (json_object_get (item, "port_size"));
    const char *commodity_code =
      json_string_value (json_object_get (item, "commodity_code"));
    int current_quantity =
      json_integer_value (json_object_get (item, "current_quantity"));
    double base_restock_rate =
      json_real_value (json_object_get (item, "base_restock_rate"));
    int commodity_id =
      json_integer_value (json_object_get (item, "commodity_id"));

    int max_capacity = port_size * 1000;
    double desired_level_ratio =
      (port_type == PORT_TYPE_STARDOCK) ? 0.9 : 0.5;
    int desired_stock = (int) (max_capacity * desired_level_ratio);

    int shortage =
      (desired_stock >
       current_quantity) ? (desired_stock - current_quantity) : 0;
    int surplus =
      (current_quantity >
       desired_stock) ? (current_quantity - desired_stock) : 0;

    int order_qty = 0;
    const char *side = NULL;

    if (shortage > 0)
      {
	order_qty = (int) (shortage * base_restock_rate);
	side = "buy";
      }
    else if (surplus > 0)
      {
	order_qty = (int) (surplus * base_restock_rate);
	side = "sell";
      }

    if ((shortage > 0 || surplus > 0) && base_restock_rate > 0
	&& order_qty == 0)
      {
	order_qty = 1;
      }

    if (order_qty > 0 && side != NULL)
      {
	int price = (strcmp (side, "buy") == 0) ?
	  h_calculate_port_buy_price (db, port_id, commodity_code) :
	  h_calculate_port_sell_price (db, port_id, commodity_code);

	commodity_order_t existing_order;
	int find_rc =
	  db_get_open_order_for_port (db, port_id, commodity_id, side,
				      &existing_order);

	if (find_rc == 0)
	  {
	    int new_total = existing_order.filled_quantity + order_qty;
	    db_update_commodity_order (db, existing_order.id, new_total,
				       existing_order.filled_quantity,
				       "open");
	  }
	else
	  {
	    db_insert_commodity_order (db, "port", port_id, "port", port_id,
				       commodity_id, side, order_qty, price,
				       0);
	  }
	orders_processed++;
      }
    else
      {
	db_cancel_commodity_orders_for_port_and_commodity (db, port_id,
							   commodity_id,
							   "buy");
	db_cancel_commodity_orders_for_port_and_commodity (db, port_id,
							   commodity_id,
							   "sell");
      }
  }

  json_decref (ports_data);
  unlock (db, "port_economy_tick");
  return 0;
}


int
h_daily_market_settlement (db_t *db, int64_t now_s)
{
  // 1. Acquire Lock
  if (!try_lock (db, "daily_market_settlement", now_s))
    {
      return 0;
    }

  // 3. Get list of commodities to iterate over
  json_t *commodities = NULL;
  if (db_cron_get_all_commodities_json (db, &commodities) != 0)
    {
      LOGE ("h_daily_market_settlement: Failed to get commodities");
      unlock (db, "daily_market_settlement");
      return -1;
    }

  size_t idx;
  json_t *comm_obj;
  json_array_foreach (commodities, idx, comm_obj)
  {
    int commodity_id = json_integer_value (json_object_get (comm_obj, "id"));
    const char *commodity_code_ptr =
      json_string_value (json_object_get (comm_obj, "code"));
    if (!commodity_code_ptr)
      continue;
    char *commodity_code = strdup (commodity_code_ptr);

    // 4. Load Orders
    int buy_count = 0;
    commodity_order_t *buy_orders = db_load_open_orders_for_commodity (db,
								       commodity_id,
								       "buy",
								       &buy_count);

    int sell_count = 0;
    commodity_order_t *sell_orders = db_load_open_orders_for_commodity (db,
									commodity_id,
									"sell",
									&sell_count);

    // 5. Matching Loop
    int b_idx = 0;
    int s_idx = 0;


    while (b_idx < buy_count && s_idx < sell_count)
      {
	commodity_order_t *buy = &buy_orders[b_idx];
	commodity_order_t *sell = &sell_orders[s_idx];


	if (buy->price >= sell->price)
	  {
	    // Match possible
	    int qty_buy_rem = buy->quantity - buy->filled_quantity;
	    int qty_sell_rem = sell->quantity - sell->filled_quantity;

	    int seller_stock = 0;


	    h_get_port_commodity_quantity (db,
					   sell->actor_id,
					   commodity_code, &seller_stock);

	    int trade_qty = qty_buy_rem;


	    if (qty_sell_rem < trade_qty)
	      {
		trade_qty = qty_sell_rem;
	      }
	    if (seller_stock < trade_qty)
	      {
		trade_qty = seller_stock;
	      }
	    /* Note: max_affordable check removed; balance check is atomic in DB */

	    if (trade_qty > 0)
	      {
		db_error_t err;
		db_error_clear (&err);
		if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
		  {
		    LOGE
		      ("h_daily_market_settlement: Failed to start transaction: %s",
		       err.message);
		    break;
		  }

		bool trade_ok = true;
		int trade_price = sell->price;
		long long total_cost = (long long) trade_qty * trade_price;

		// Execute Trade
		int buyer_acct = 0;


		if (h_get_account_id_unlocked (db,
					       buy->actor_type,
					       buy->actor_id,
					       &buyer_acct) != 0)
		  trade_ok = false;

		int seller_acct = 0;


		if (trade_ok && h_get_account_id_unlocked (db,
							   sell->actor_type,
							   sell->actor_id,
							   &seller_acct) != 0)
		  trade_ok = false;

		long long new_bal;


		if (trade_ok && h_deduct_credits_unlocked (db,
							   buyer_acct,
							   total_cost,
							   "TRADE_BUY",
							   "MARKET_SETTLEMENT",
							   &new_bal) != 0)
		  trade_ok = false;

		if (trade_ok && h_add_credits_unlocked (db,
						       seller_acct,
						       total_cost,
						       "TRADE_SELL",
						       "MARKET_SETTLEMENT",
						       &new_bal) != 0)
		  trade_ok = false;

		if (trade_ok && strcmp (sell->actor_type, "port") == 0)
		  {
		    if (h_market_move_port_stock (db,
						  sell->actor_id,
						  commodity_code,
						  -trade_qty) != 0)
		      trade_ok = false;
		  }
		else if (trade_ok && strcmp (sell->actor_type, "planet") == 0)
		  {
		    if (h_market_move_planet_stock (db,
						    sell->actor_id,
						    commodity_code,
						    -trade_qty) != 0)
		      trade_ok = false;
		  }

		if (trade_ok && strcmp (buy->actor_type, "port") == 0)
		  {
		    if (h_market_move_port_stock (db,
						  buy->actor_id,
						  commodity_code,
						  trade_qty) != 0)
		      trade_ok = false;
		  }
		else if (trade_ok && strcmp (buy->actor_type, "planet") == 0)
		  {
		    if (h_market_move_planet_stock (db,
						    buy->actor_id,
						    commodity_code,
						    trade_qty) != 0)
		      trade_ok = false;
		  }

		if (trade_ok && db_insert_commodity_trade (db,
							   commodity_id,
							   buy->id,
							   sell->id,
							   trade_qty,
							   trade_price,
							   buy->actor_type,
							   buy->actor_id,
							   sell->actor_type,
							   sell->actor_id,
							   0, 0) != 0)
		  trade_ok = false;

		if (trade_ok)
		  {
		    /* Checked arithmetic to prevent overflow */
		    if (__builtin_add_overflow
			(buy->filled_quantity, trade_qty,
			 &buy->filled_quantity))
		      {
			LOGE ("Integer overflow in buy order filled_quantity");
			trade_ok = false;
		      }
		    if (trade_ok && __builtin_add_overflow
			(sell->filled_quantity, trade_qty,
			 &sell->filled_quantity))
		      {
			LOGE
			  ("Integer overflow in sell order filled_quantity");
			trade_ok = false;
		      }
		  }

		if (trade_ok)
		  {
		    const char *b_status = (buy->filled_quantity >=
					    buy->quantity) ? "filled" :
		      "partial";


		    if (db_update_commodity_order (db,
						   buy->id,
						   buy->quantity,
						   buy->filled_quantity,
						   b_status) != 0)
		      trade_ok = false;

		    const char *s_status = (sell->filled_quantity >=
					    sell->quantity) ? "filled" :
		      "partial";


		    if (trade_ok && db_update_commodity_order (db,
							       sell->id,
							       sell->quantity,
							       sell->filled_quantity,
							       s_status) != 0)
		      trade_ok = false;
		  }

		if (trade_ok)
		  {
		    db_tx_commit (db, &err);
		  }
		else
		  {
		    db_tx_rollback (db, &err);
		    /* Instead of breaking, just move to next buyer if insufficient funds */
		    if (b_idx + 1 < buy_count)
		      {
			b_idx++;
		      }
		    else
		      {
			break;  /* No more buyers to try */
		      }
		    continue;
		  }

		if (buy->filled_quantity >= buy->quantity)
		  {
		    b_idx++;
		  }
		if (sell->filled_quantity >= sell->quantity ||
		    seller_stock <= 0)
		  {
		    s_idx++;
		  }
	      }
	    else
	      {
		if (seller_stock <= 0)
		  {
		    s_idx++;
		  }
		else
		  {
		    /* Can't match; move to next seller */
		    s_idx++;
		  }
	      }
	  }
	else
	  {
	    break;
	  }
      }

    free (buy_orders);
    free (sell_orders);
    free (commodity_code);
  }

  json_decref (commodities);

  // 6. Handle Expiry
  if (db_cron_expire_market_orders (db, now_s) != 0)
    {
      LOGE ("h_daily_market_settlement: Failed to expire orders");
    }

  unlock (db, "daily_market_settlement");
  return 0;
}


//////////////////////// NEWS BLOCK ////////////////////////
int
h_daily_news_compiler (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "daily_news_compiler", now_s))
    {
      return 0;
    }
  LOGI ("h_daily_news_compiler: Starting daily news compilation.");

  int64_t yesterday_s = now_s - 86400;	// 24 hours ago

  json_t *events = NULL;
  if (db_cron_news_get_events_json (db, yesterday_s, now_s, &events) != 0)
    {
      LOGE ("h_daily_news_compiler: Failed to get events");
      unlock (db, "daily_news_compiler");
      return 0;			// Not fatal to crash, just no news
    }

  size_t idx;
  json_t *event_obj;
  json_array_foreach (events, idx, event_obj)
  {
    int64_t event_ts = json_integer_value (json_object_get (event_obj, "ts"));
    const char *event_type =
      json_string_value (json_object_get (event_obj, "type"));
    int actor_player_id =
      json_integer_value (json_object_get (event_obj, "actor_player_id"));
    int sector_id =
      json_integer_value (json_object_get (event_obj, "sector_id"));
    const char *payload_str =
      json_string_value (json_object_get (event_obj, "payload"));

    json_error_t jerr;
    json_t *payload_obj = json_loads (payload_str, 0, &jerr);


    if (!payload_obj)
      {
	LOGW
	  ("h_daily_news_compiler: Failed to parse JSON payload for event type '%s': %s",
	   event_type, jerr.text);
	continue;
      }
    const char *headline = NULL;
    const char *body = NULL;
    const char *category = "report";	// Default category
    const char *scope = "global";	// Default scope
    json_t *context_data = json_object ();
    char *headline_str = NULL;	// For asprintf
    char *body_str = NULL;	// For asprintf
    char *scope_str = NULL;	// For asprintf


    // Add common context data
    json_object_set_new (context_data, "event_type",
			 json_string (event_type));
    if (actor_player_id > 0)
      {
	json_object_set_new (context_data, "actor_player_id",
			     json_integer (actor_player_id));
      }
    if (sector_id > 0)
      {
	json_object_set_new (context_data, "sector_id",
			     json_integer (sector_id));
	if (asprintf (&scope_str, "sector:%d", sector_id) == -1)
	  {
	    LOGE ("h_daily_news_compiler: Failed to allocate scope_str.");
	    json_decref (payload_obj);
	    json_decref (context_data);
	    continue;
	  }
	scope = scope_str;	// Dynamic scope
      }
    if (strcasecmp (event_type, "commodity.boom") == 0
	|| strcasecmp (event_type, "commodity.bust") == 0)
      {
	const char *commodity =
	  json_string_value (json_object_get (payload_obj, "commodity"));
	const char *location =
	  json_string_value (json_object_get (payload_obj, "location"));
	double price =
	  json_real_value (json_object_get (payload_obj, "price"));


	if (commodity && location)
	  {
	    category = "economic";
	    if (strcasecmp (event_type, "commodity.boom") == 0)
	      {
		if (asprintf
		    (&headline_str, "Economic Boom! %s Prices Soar in %s!",
		     commodity, location) == -1)
		  {
		    LOGE
		      ("h_daily_news_compiler: Failed to allocate headline_str for commodity.boom.");
		    goto next_event_cleanup;
		  }
		headline = headline_str;
		if (asprintf
		    (&body_str,
		     "The price of %s has surged to %.2f credits in %s, indicating strong market demand.",
		     commodity, price, location) == -1)
		  {
		    LOGE
		      ("h_daily_news_compiler: Failed to allocate body_str for commodity.boom.");
		    goto next_event_cleanup;
		  }
		body = body_str;
	      }
	    else
	      {			// commodity.bust
		if (asprintf
		    (&headline_str,
		     "Market Crash! %s Prices Plummet in %s!", commodity,
		     location) == -1)
		  {
		    LOGE
		      ("h_daily_news_compiler: Failed to allocate headline_str for commodity.bust.");
		    goto next_event_cleanup;
		  }
		headline = headline_str;
		if (asprintf
		    (&body_str,
		     "A sudden drop has seen %s prices fall to %.2f credits in %s, causing market instability.",
		     commodity, price, location) == -1)
		  {
		    LOGE
		      ("h_daily_news_compiler: Failed to allocate body_str for commodity.bust.");
		    goto next_event_cleanup;
		  }
		body = body_str;
	      }
	    json_object_set_new (context_data, "commodity",
				 json_string (commodity));
	    json_object_set_new (context_data, "location",
				 json_string (location));
	    json_object_set_new (context_data, "price", json_real (price));
	  }
      }
    else if (strcasecmp (event_type, "ship.destroyed") == 0)
      {
	int destroyed_player_id =
	  json_integer_value (json_object_get (payload_obj, "player_id"));
	int destroyed_ship_id =
	  json_integer_value (json_object_get (payload_obj, "ship_id"));
	const char *ship_name =
	  json_string_value (json_object_get (payload_obj, "ship_name"));


	if (ship_name)
	  {
	    category = "military";
	    if (asprintf
		(&headline_str, "Ship %s Destroyed in Sector %d!",
		 ship_name, sector_id) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate headline_str for ship.destroyed.");
		goto next_event_cleanup;
	      }
	    headline = headline_str;
	    if (asprintf
		(&body_str,
		 "Reports indicate the ship '%s' (ID: %d), associated with Player ID %d, was destroyed in Sector %d. The cause is currently under investigation.",
		 ship_name,
		 destroyed_ship_id, destroyed_player_id, sector_id) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate body_str for ship.destroyed.");
		goto next_event_cleanup;
	      }
	    body = body_str;
	    json_object_set_new (context_data, "destroyed_player_id",
				 json_integer (destroyed_player_id));
	    json_object_set_new (context_data, "destroyed_ship_id",
				 json_integer (destroyed_ship_id));
	    json_object_set_new (context_data, "ship_name",
				 json_string (ship_name));
	  }
      }
    else if (strcasecmp (event_type, "fedspace:tow") == 0)
      {
	const char *reason =
	  json_string_value (json_object_get (payload_obj, "reason"));


	if (reason)
	  {
	    category = "military";
	    if (asprintf
		(&headline_str,
		 "Federal Authorities Tow Ship from Sector %d!",
		 sector_id) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate headline_str for fedspace:tow.");
		goto next_event_cleanup;
	      }
	    headline = headline_str;
	    if (asprintf
		(&body_str,
		 "A ship was forcibly towed from FedSpace Sector %d due to a violation of Federal Law: %s. Owners are advised to review regulations.",
		 sector_id, reason) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate body_str for fedspace:tow.");
		goto next_event_cleanup;
	      }
	    body = body_str;
	  }
      }
    else if (strcasecmp (event_type, "corp.tax.paid") == 0)
      {
	int corp_id =
	  json_integer_value (json_object_get (payload_obj, "corp_id"));
	long long amount =
	  json_integer_value (json_object_get (payload_obj, "amount"));
	long long total_assets =
	  json_integer_value (json_object_get (payload_obj, "total_assets"));
	// Fetch corp name and tag
	json_t *cdata = NULL;
	db_cron_get_corp_details_json (db, corp_id, &cdata);

	char corp_name_buf[64] = { 0 };
	char corp_tag_buf[16] = { 0 };

	if (cdata)
	  {
	    const char *name_val =
	      json_string_value (json_object_get (cdata, "name"));
	    const char *tag_val =
	      json_string_value (json_object_get (cdata, "tag"));
	    if (name_val)
	      strncpy (corp_name_buf, name_val, sizeof (corp_name_buf) - 1);
	    if (tag_val)
	      strncpy (corp_tag_buf, tag_val, sizeof (corp_tag_buf) - 1);
	    json_decref (cdata);
	  }

	if (corp_id > 0)
	  {
	    category = "economic";
	    if (asprintf
		(&headline_str, "Corporation Tax Paid by %s [%s]!",
		 corp_name_buf, corp_tag_buf) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate headline_str for corp.tax.paid.");
		goto next_event_cleanup;
	      }
	    headline = headline_str;
	    if (asprintf
		(&body_str,
		 "%s [%s] has successfully paid %lld credits in daily corporate taxes on reported assets of %lld credits, maintaining good standing with the Federation.",
		 corp_name_buf, corp_tag_buf, amount, total_assets) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate body_str for corp.tax.paid.");
		goto next_event_cleanup;
	      }
	    body = body_str;
	    json_object_set_new (context_data, "corp_id",
				 json_integer (corp_id));
	    json_object_set_new (context_data, "amount",
				 json_integer (amount));
	    json_object_set_new (context_data, "total_assets",
				 json_integer (total_assets));
	  }
      }
    else if (strcasecmp (event_type, "corp.tax.failed") == 0)
      {
	int corp_id =
	  json_integer_value (json_object_get (payload_obj, "corp_id"));
	long long amount =
	  json_integer_value (json_object_get (payload_obj, "amount"));
	long long total_assets =
	  json_integer_value (json_object_get (payload_obj, "total_assets"));
	// Fetch corp name and tag
	json_t *cdata = NULL;
	db_cron_get_corp_details_json (db, corp_id, &cdata);

	char corp_name_buf[64] = { 0 };
	char corp_tag_buf[16] = { 0 };
	long long tax_arrears = 0;
	int credit_rating = 0;

	if (cdata)
	  {
	    const char *name_val =
	      json_string_value (json_object_get (cdata, "name"));
	    const char *tag_val =
	      json_string_value (json_object_get (cdata, "tag"));
	    if (name_val)
	      strncpy (corp_name_buf, name_val, sizeof (corp_name_buf) - 1);
	    if (tag_val)
	      strncpy (corp_tag_buf, tag_val, sizeof (corp_tag_buf) - 1);
	    tax_arrears =
	      json_integer_value (json_object_get (cdata, "tax_arrears"));
	    credit_rating =
	      json_integer_value (json_object_get (cdata, "credit_rating"));
	    json_decref (cdata);
	  }

	if (corp_id > 0)
	  {
	    category = "economic";
	    if (asprintf
		(&headline_str, "Corporation Tax Default by %s [%s]!",
		 corp_name_buf, corp_tag_buf) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate headline_str for corp.tax.failed.");
		goto next_event_cleanup;
	      }
	    headline = headline_str;
	    if (asprintf
		(&body_str,
		 "%s [%s] has failed to pay %lld credits in daily corporate taxes on reported assets of %lld credits. Total arrears now stand at %lld credits, and their credit rating has fallen to %d. Federation authorities are monitoring the situation.",
		 corp_name_buf,
		 corp_tag_buf,
		 amount, total_assets, tax_arrears, credit_rating) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate body_str for corp.tax.failed.");
		goto next_event_cleanup;
	      }
	    body = body_str;
	    json_object_set_new (context_data, "corp_id",
				 json_integer (corp_id));
	    json_object_set_new (context_data, "amount",
				 json_integer (amount));
	    json_object_set_new (context_data, "total_assets",
				 json_integer (total_assets));
	    json_object_set_new (context_data, "tax_arrears",
				 json_integer (tax_arrears));
	    json_object_set_new (context_data, "credit_rating",
				 json_integer (credit_rating));
	  }
      }
    else if (strcasecmp (event_type, "stock.ipo.registered") == 0)
      {
	int corp_id =
	  json_integer_value (json_object_get (payload_obj, "corp_id"));
	const char *ticker =
	  json_string_value (json_object_get (payload_obj, "ticker"));
	// Fetch corp name
	json_t *cdata = NULL;
	db_cron_get_corp_details_json (db, corp_id, &cdata);

	char corp_name_buf[64] = { 0 };

	if (cdata)
	  {
	    const char *name_val =
	      json_string_value (json_object_get (cdata, "name"));
	    if (name_val)
	      strncpy (corp_name_buf, name_val, sizeof (corp_name_buf) - 1);
	    json_decref (cdata);
	  }

	if (corp_id > 0 && ticker)
	  {
	    category = "economic";
	    if (asprintf
		(&headline_str, "%s [%s] Goes Public!", corp_name_buf,
		 ticker) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate headline_str for stock.ipo.registered.");
		goto next_event_cleanup;
	      }
	    headline = headline_str;
	    if (asprintf
		(&body_str,
		 "The corporation %s has successfully launched its Initial Public Offering under the ticker symbol [%s], opening new investment opportunities.",
		 corp_name_buf, ticker) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate body_str for stock.ipo.registered.");
		goto next_event_cleanup;
	      }
	    body = body_str;
	    json_object_set_new (context_data, "corp_id",
				 json_integer (corp_id));
	    json_object_set_new (context_data, "ticker",
				 json_string (ticker));
	  }
      }
    else if (strcasecmp (event_type, "stock.dividend.declared") == 0)
      {
	int corp_id =
	  json_integer_value (json_object_get (payload_obj, "corp_id"));
	int stock_id =
	  json_integer_value (json_object_get (payload_obj, "stock_id"));
	int amount_per_share =
	  json_integer_value (json_object_get (payload_obj,
					       "amount_per_share"));
	long long total_payout =
	  json_integer_value (json_object_get (payload_obj, "total_payout"));
	// Fetch corp name and ticker
	json_t *sdata = NULL;
	db_cron_get_stock_details_json (db, corp_id, stock_id, &sdata);

	char corp_name_buf[64] = { 0 };
	char ticker_buf[16] = { 0 };

	if (sdata)
	  {
	    const char *name_val =
	      json_string_value (json_object_get (sdata, "name"));
	    const char *ticker_val =
	      json_string_value (json_object_get (sdata, "ticker"));
	    if (name_val)
	      strncpy (corp_name_buf, name_val, sizeof (corp_name_buf) - 1);
	    if (ticker_val)
	      strncpy (ticker_buf, ticker_val, sizeof (ticker_buf) - 1);
	    json_decref (sdata);
	  }

	if (corp_id > 0 && stock_id > 0)
	  {
	    category = "economic";
	    if (asprintf
		(&headline_str, "Dividend Declared by %s [%s]!",
		 corp_name_buf, ticker_buf) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate headline_str for stock.dividend.declared.");
		goto next_event_cleanup;
	      }
	    headline = headline_str;
	    if (asprintf
		(&body_str,
		 "%s [%s] has declared a dividend of %d credits per share, with a total payout of %lld credits, signaling strong financial performance.",
		 corp_name_buf,
		 ticker_buf, amount_per_share, total_payout) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate body_str for stock.dividend.declared.");
		goto next_event_cleanup;
	      }
	    body = body_str;
	    json_object_set_new (context_data, "corp_id",
				 json_integer (corp_id));
	    json_object_set_new (context_data, "stock_id",
				 json_integer (stock_id));
	    json_object_set_new (context_data, "amount_per_share",
				 json_integer (amount_per_share));
	    json_object_set_new (context_data, "total_payout",
				 json_integer (total_payout));
	  }
      }
    else if (strcasecmp (event_type, "stock.dividend.payout_failed") == 0)
      {
	int corp_id =
	  json_integer_value (json_object_get (payload_obj, "corp_id"));
	int stock_id =
	  json_integer_value (json_object_get (payload_obj, "stock_id"));
	long long required_payout =
	  json_integer_value (json_object_get (payload_obj, "required"));
	long long available_funds =
	  json_integer_value (json_object_get (payload_obj, "available"));
	// Fetch corp name and ticker
	json_t *sdata = NULL;
	db_cron_get_stock_details_json (db, corp_id, stock_id, &sdata);

	char corp_name_buf[64] = { 0 };
	char ticker_buf[16] = { 0 };

	if (sdata)
	  {
	    const char *name_val =
	      json_string_value (json_object_get (sdata, "name"));
	    const char *ticker_val =
	      json_string_value (json_object_get (sdata, "ticker"));
	    if (name_val)
	      strncpy (corp_name_buf, name_val, sizeof (corp_name_buf) - 1);
	    if (ticker_val)
	      strncpy (ticker_buf, ticker_val, sizeof (ticker_buf) - 1);
	    json_decref (sdata);
	  }

	if (corp_id > 0 && stock_id > 0)
	  {
	    category = "economic";
	    if (asprintf
		(&headline_str, "Dividend Payout Failed for %s [%s]!",
		 corp_name_buf, ticker_buf) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate headline_str for stock.dividend.payout_failed.");
		goto next_event_cleanup;
	      }
	    headline = headline_str;
	    if (asprintf
		(&body_str,
		 "Due to insufficient corporate funds, %s [%s] has failed to pay a declared dividend. %lld credits were required, but only %lld credits were available.",
		 corp_name_buf,
		 ticker_buf, required_payout, available_funds) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate body_str for stock.dividend.payout_failed.");
		goto next_event_cleanup;
	      }
	    body = body_str;
	    json_object_set_new (context_data, "corp_id",
				 json_integer (corp_id));
	    json_object_set_new (context_data, "stock_id",
				 json_integer (stock_id));
	    json_object_set_new (context_data, "required_payout",
				 json_integer (required_payout));
	    json_object_set_new (context_data, "available_funds",
				 json_integer (available_funds));
	  }
      }
    else if (strcasecmp (event_type, "stock.dividend.payout_completed") == 0)
      {
	int corp_id =
	  json_integer_value (json_object_get (payload_obj, "corp_id"));
	int stock_id =
	  json_integer_value (json_object_get (payload_obj, "stock_id"));
	long long actual_payout =
	  json_integer_value (json_object_get (payload_obj,
					       "actual_payout"));
	// Fetch corp name and ticker
	json_t *sdata = NULL;
	db_cron_get_stock_details_json (db, corp_id, stock_id, &sdata);

	char corp_name_buf[64] = { 0 };
	char ticker_buf[16] = { 0 };

	if (sdata)
	  {
	    const char *name_val =
	      json_string_value (json_object_get (sdata, "name"));
	    const char *ticker_val =
	      json_string_value (json_object_get (sdata, "ticker"));
	    if (name_val)
	      strncpy (corp_name_buf, name_val, sizeof (corp_name_buf) - 1);
	    if (ticker_val)
	      strncpy (ticker_buf, ticker_val, sizeof (ticker_buf) - 1);
	    json_decref (sdata);
	  }

	if (corp_id > 0 && stock_id > 0)
	  {
	    category = "economic";
	    if (asprintf
		(&headline_str, "Dividend Payout Completed by %s [%s]!",
		 corp_name_buf, ticker_buf) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate headline_str for stock.dividend.payout_completed.");
		goto next_event_cleanup;
	      }
	    headline = headline_str;
	    if (asprintf
		(&body_str,
		 "%s [%s] has successfully completed the payout of %lld credits in dividends to its shareholders.",
		 corp_name_buf, ticker_buf, actual_payout) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate body_str for stock.dividend.payout_completed.");
		goto next_event_cleanup;
	      }
	    body = body_str;
	    json_object_set_new (context_data, "corp_id",
				 json_integer (corp_id));
	    json_object_set_new (context_data, "stock_id",
				 json_integer (stock_id));
	    json_object_set_new (context_data, "actual_payout",
				 json_integer (actual_payout));
	  }
      }
    else if (strcasecmp (event_type, "combat.ship_destroyed") == 0)
      {
	const char *ship_name =
	  json_string_value (json_object_get (payload_obj, "ship_name"));
	int victim_player_id =
	  json_integer_value (json_object_get
			      (payload_obj, "victim_player_id"));
	int attacker_player_id =
	  json_integer_value (json_object_get
			      (payload_obj, "attacker_player_id"));


	if (ship_name)
	  {
	    category = "military";
	    if (asprintf
		(&headline_str, "Ship '%s' Destroyed in Combat!",
		 ship_name) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate headline_str for combat.ship_destroyed.");
		goto next_event_cleanup;
	      }
	    headline = headline_str;
	    if (asprintf
		(&body_str,
		 "The ship '%s' (owned by Player ID %d) was destroyed in Sector %d, reportedly by Player ID %d.",
		 ship_name,
		 victim_player_id, sector_id, attacker_player_id) == -1)
	      {
		LOGE
		  ("h_daily_news_compiler: Failed to allocate body_str for combat.ship_destroyed.");
		goto next_event_cleanup;
	      }
	    body = body_str;
	    json_object_set_new (context_data, "ship_name",
				 json_string (ship_name));
	    json_object_set_new (context_data, "victim_player_id",
				 json_integer (victim_player_id));
	    json_object_set_new (context_data, "attacker_player_id",
				 json_integer (attacker_player_id));
	  }
      }
    // Add more event types here as needed for news generation
    if (headline && body)
      {
	db_news_insert_feed_item (event_ts, category, scope, headline, body,
				  context_data);
      }
    else
      {
	LOGD
	  ("h_daily_news_compiler: No news generated for event type '%s'.",
	   event_type);
      }
  next_event_cleanup:
    if (headline_str)
      {
	free (headline_str);
	headline_str = NULL;
      }
    if (body_str)
      {
	free (body_str);
	body_str = NULL;
      }
    if (scope_str)
      {
	free (scope_str);
	scope_str = NULL;
      }
    json_decref (context_data);
    json_decref (payload_obj);
  }
  json_decref (events);
  unlock (db, "daily_news_compiler");
  return 0;
}

int
h_cleanup_old_news (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "cleanup_old_news", now_s))
    {
      return 0;
    }
  LOGI ("cleanup_old_news: Starting cleanup of old news articles.");

  int64_t cutoff = now_s - 604800;
  if (db_cron_cleanup_old_news (db, cutoff) != 0)
    {
      LOGE ("cleanup_old_news: Failed to execute delete");
    }
  else
    {
      LOGI ("cleanup_old_news: Deleted old news articles.");
    }

  unlock (db, "cleanup_old_news");
  return 0;
}

int
h_daily_lottery_draw (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "daily_lottery_draw", now_s))
    {
      return 0;
    }
  LOGI ("daily_lottery_draw: Starting daily lottery draw.");

  char draw_date_str[32];
  struct tm *tm_info = localtime (&now_s);
  strftime (draw_date_str, sizeof (draw_date_str), "%Y-%m-%d", tm_info);

  if (db_cron_lottery_check_processed (db, draw_date_str))
    {
      LOGI ("daily_lottery_draw: Lottery for %s already processed. Skipping.",
	    draw_date_str);
      goto commit_and_unlock;
    }

  long long yesterday_carried_over = 0;
  char yesterday_date_str[32];
  time_t yesterday_s = (time_t) now_s - (24 * 60 * 60);
  struct tm *tm_yest = localtime (&yesterday_s);
  strftime (yesterday_date_str, sizeof (yesterday_date_str), "%Y-%m-%d",
	    tm_yest);

  db_cron_lottery_get_yesterday (db, yesterday_date_str,
				 &yesterday_carried_over);

  long long total_pot_from_tickets = 0;
  if (db_cron_lottery_get_stats (db, draw_date_str, &total_pot_from_tickets)
      != 0)
    {
      LOGE ("daily_lottery_draw: Failed to get stats");
      goto rollback_and_unlock;
    }

  long long current_jackpot =
    yesterday_carried_over + (total_pot_from_tickets / 2);
  int winning_number = get_random_int (1, 999);
  bool winner_found = false;

  json_t *winners_array = NULL;
  db_cron_lottery_get_winners_json (db, draw_date_str, winning_number,
				    &winners_array);

  if (json_array_size (winners_array) > 0)
    {
      winner_found = true;
      long long payout_per_winner =
	current_jackpot / json_array_size (winners_array);
      size_t i;
      json_t *w;
      json_array_foreach (winners_array, i, w)
      {
	int pid = json_integer_value (json_object_get (w, "player_id"));
	h_add_credits (db, "player", pid, payout_per_winner, "LOTTERY_WIN",
		       NULL, NULL);
	json_object_set_new (w, "winnings", json_integer (payout_per_winner));
      }
      current_jackpot = 0;
      yesterday_carried_over = 0;
    }
  else
    {
      yesterday_carried_over = total_pot_from_tickets / 2;
      current_jackpot = 0;
      LOGI ("daily_lottery_draw: No winner found for %s. Jackpot rolls over.",
	    draw_date_str);
    }

  db_cron_lottery_update_state (db, draw_date_str,
				winner_found ? winning_number : 0,
				current_jackpot, yesterday_carried_over);

  LOGI
    ("daily_lottery_draw: Draw for %s completed. Winning number: %d. Jackpot: %lld. Winners: %s",
     draw_date_str, winning_number, current_jackpot,
     winner_found ? json_dumps (winners_array, 0) : "None");
  json_decref (winners_array);

commit_and_unlock:
  unlock (db, "daily_lottery_draw");
  return 0;

rollback_and_unlock:
  unlock (db, "daily_lottery_draw");
  return -1;
}

int
h_deadpool_resolution_cron (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "deadpool_resolution_cron", now_s))
    {
      return 0;
    }
  LOGI ("deadpool_resolution_cron: Starting Dead Pool bet resolution.");

  db_cron_deadpool_expire_bets (db, now_s);

  json_t *events = NULL;
  db_cron_deadpool_get_events_json (db, &events);

  size_t i;
  json_t *ev;
  json_array_foreach (events, i, ev)
  {
    int destroyed_pid =
      json_integer_value (json_object_get (ev, "player_id"));
    if (destroyed_pid <= 0)
      continue;

    json_t *bets = NULL;
    db_cron_deadpool_get_bets_json (db, destroyed_pid, &bets);

    size_t j;
    json_t *bet;
    json_array_foreach (bets, j, bet)
    {
      int bet_id = json_integer_value (json_object_get (bet, "id"));
      int bettor_id = json_integer_value (json_object_get (bet, "bettor_id"));
      long long amount = json_integer_value (json_object_get (bet, "amount"));
      int odds = json_integer_value (json_object_get (bet, "odds_bp"));
      long long payout = (amount * odds) / 10000;

      h_add_credits (db, "player", bettor_id, payout, "DEADPOOL_WIN", NULL,
		     NULL);
      db_cron_deadpool_update_bet (db, bet_id, "won", now_s);
    }
    json_decref (bets);

    db_cron_deadpool_update_lost_bets (db, destroyed_pid, now_s);
  }
  json_decref (events);
  LOGI ("deadpool_resolution_cron: Dead Pool bet resolution completed.");
  unlock (db, "deadpool_resolution_cron");
  return 0;
}

int
h_tavern_notice_expiry_cron (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "tavern_notice_expiry_cron", now_s))
    {
      return 0;
    }
  LOGI
    ("tavern_notice_expiry_cron: Starting Tavern notice and corp recruiting expiry cleanup.");
  db_cron_tavern_cleanup (db, now_s);
  LOGI
    ("tavern_notice_expiry_cron: Removed expired Tavern notices and corp recruiting entries.");
  unlock (db, "tavern_notice_expiry_cron");
  return 0;
}

int
h_loan_shark_interest_cron (db_t *db, int64_t now_s)
{
  LOGI ("BANK0: Loan Shark Interest cron disabled for v1.0.");
  (void) db;
  (void) now_s;
  return 0;
}

int
h_daily_stock_price_recalculation (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "daily_stock_price_recalculation", now_s))
    {
      return 0;
    }
  LOGI
    ("h_daily_stock_price_recalculation: Starting daily stock price recalculation.");

  json_t *stocks = NULL;
  db_cron_get_stocks_json (db, &stocks);

  size_t i;
  json_t *s;
  json_array_foreach (stocks, i, s)
  {
    int id = json_integer_value (json_object_get (s, "id"));
    int corp_id = json_integer_value (json_object_get (s, "corp_id"));
    long long shares =
      json_integer_value (json_object_get (s, "total_shares"));
    long long net_value = 0;
    long long bank = 0;

    db_get_corp_bank_balance (db, corp_id, &bank);
    net_value += bank;

    long long assets = 0;
    db_cron_get_corp_planet_assets (db, corp_id, &assets);
    net_value += assets;

    long long price = 0;
    if (shares > 0)
      price = net_value / shares;
    if (price < 1)
      price = 1;

    db_cron_update_stock_price (db, id, price);
  }
  json_decref (stocks);

  LOGI
    ("h_daily_stock_price_recalculation: Successfully recalculated stock prices.");
  unlock (db, "daily_stock_price_recalculation");
  return 0;
}

int
h_shield_regen_tick (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "shield_regen", now_s))
    {
      return 0;
    }
  int regen = 5;
  if (db_cron_shield_regen (db, regen) != 0)
    {
      LOGE ("h_shield_regen_tick: SQL Error");
    }
  unlock (db, "shield_regen");
  return 0;
}

int
h_citadel_construction_reap (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "citadel_construction_reap", now_s))
    {
      return 0;
    }

  LOGD ("h_citadel_construction_reap: Scanning for completed upgrades...");

  /* Reap up to 50 citadels per pass */
  int rc = repo_citadel_reap_upgrades (db, now_s, 50);

  if (rc != 0)
    {
      LOGE ("h_citadel_construction_reap: Reap failed with code %d", rc);
    }

  unlock (db, "citadel_construction_reap");
  return 0;
}
