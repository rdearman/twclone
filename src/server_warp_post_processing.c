#include "db/repo/repo_warp.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "server_config.h"
//#include "server_bigbang.h"
#include "db/repo/repo_database.h"	// Needed for db_get_handle()
#include "server_log.h"
#include "db/db_api.h"
#include "db/sql_driver.h"
#include "game_db.h"

// Forward declarations of internal helper functions
static int get_high_degree_sector (db_t * db);
static int get_tunnel_end (db_t * db, int start_sector);

#define DEFAULT_PERCENT_ONEWAY 20
#define DEFAULT_PERCENT_DEADEND 20


/**
 * @brief Finds a random sector with a high out-degree (many warps).
 * This is used to create "safe" exit points for one-way and dead-end
 * warps, ensuring the player isn't trapped.
 * @param db The generic database handle.
 * @return The ID of a sector with a high degree, or -1 on failure.
 */
static int
get_high_degree_sector (db_t *db)
{
  int sector_id = -1;
  if (repo_warp_get_high_degree_sector (db, &sector_id) != 0)
    {
      LOGE ("get_high_degree_sector query failed\n");
    }
  return sector_id;
}


/**
 * @brief Finds the destination of a one-way warp from a given sector.
 * @param db The generic database handle.
 * @param start_sector The sector ID to find the destination for.
 * @return The ID of the destination sector, or -1 if not found.
 */
static int
get_tunnel_end (db_t *db, int start_sector)
{
  int end_sector = -1;
  if (repo_warp_get_tunnel_end (db, start_sector, &end_sector) != 0)
    {
      LOGE ("get_tunnel_end query failed\n");
    }
  return end_sector;
}


/**
 * @brief Creates complex warps in the universe, including one-way warps and dead-end warps.
 * This is a post-processing step after the initial random warp generation.
 * @param db The generic database handle.
 * @param numSectors The total number of sectors in the universe.
 * @return 0 on success, -1 on failure.
 */
int
create_complex_warps (db_t *db, int numSectors)
{
  LOGE ("BIGBANG: Creating complex warps...\n");

  // Step 1: Create longer tunnels by splicing two existing tunnels
  int start_sector1 = -1, start_sector2 = -1;

  if (repo_warp_get_tunnels (db, &start_sector1, &start_sector2) != 0)
    {
      LOGE ("find_tunnels query failed\n");
      return -1;
    }

  if (start_sector1 > 0 && start_sector2 > 0
      && start_sector1 != start_sector2)
    {
      int end_sector1 = get_tunnel_end (db, start_sector1);
      int end_sector2 = get_tunnel_end (db, start_sector2);


      if (end_sector1 != -1 && end_sector2 != -1
	  && end_sector1 != end_sector2)
	{
	  if (repo_warp_delete_warp (db, end_sector1, start_sector1) != 0)
	    {
	      LOGE ("SQL error delete\n");
	      return -1;
	    }

	  if (repo_warp_insert_warp (db, end_sector1, start_sector2) != 0)
	    {
	      LOGE ("SQL error insert\n");

	      // Re-insert the original warp to restore state
	      if (repo_warp_insert_warp (db, end_sector1, start_sector1) != 0)
		{
		  LOGE ("CRITICAL SQL error during rollback\n");
		}
	      return -1;
	    }
	}
    }

  // Step 2: Create one-way warps by converting bidirectional warps
  const int num_one_way_warps =
    (int) (numSectors * (DEFAULT_PERCENT_ONEWAY / 100.0));


  LOGE ("BIGBANG: Creating %d one-way warps...\n", num_one_way_warps);


  for (int i = 0; i < num_one_way_warps; i++)
    {
      int one_way_from = -1, one_way_to = -1;

      if (repo_warp_get_bidirectional_warp (db, &one_way_from, &one_way_to) !=
	  0)
	{
	  LOGE ("find_bidirectional query failed\n");
	  continue;
	}

      if (one_way_from > 0 && one_way_to > 0)
	{
	  if (repo_warp_delete_warp (db, one_way_to, one_way_from) != 0)
	    {
	      LOGE ("SQL error delete return\n");
	    }
	}
    }

  // Step 3: Create dead-end warps by converting low-degree sectors
  const int num_dead_end_warps =
    (int) (numSectors * (DEFAULT_PERCENT_DEADEND / 100.0));


  LOGE ("BIGBANG: Creating %d dead-end warps...\n", num_dead_end_warps);


  for (int i = 0; i < num_dead_end_warps; i++)
    {
      int dead_end_sector = -1;

      if (repo_warp_get_low_degree_sector (db, &dead_end_sector) != 0)
	{
	  LOGE ("find_low_degree query failed\n");
	  continue;
	}

      if (dead_end_sector > 0)
	{
	  int exit_sector = get_high_degree_sector (db);


	  if (exit_sector > 0 && dead_end_sector != exit_sector)
	    {
	      if (repo_warp_delete_warps_from_sector (db, dead_end_sector) !=
		  0)
		{
		  LOGE ("SQL error delete dead end\n");
		}

	      if (repo_warp_insert_warp (db, dead_end_sector, exit_sector) !=
		  0)
		{
		  LOGE ("SQL error insert dead end\n");
		}
	    }
	}
    }
  return 0;
}
