#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <time.h>
#include "server_config.h"
#include "server_bigbang.h"
#include "database.h"		// Needed for db_get_handle()

// Forward declarations of internal helper functions
static int get_high_degree_sector (sqlite3 * db);
static int get_tunnel_end (sqlite3 * db, int start_sector);

/**
 * @brief Finds a random sector with a high out-degree (many warps).
 * This is used to create "safe" exit points for one-way and dead-end
 * warps, ensuring the player isn't trapped.
 * @param db The SQLite database handle.
 * @return The ID of a sector with a high degree, or -1 on failure.
 */
static int
get_high_degree_sector (sqlite3 *db)
{
  const char *q = "SELECT from_sector, COUNT(to_sector) AS out_degree "
    "FROM sector_warps "
    "GROUP BY from_sector "
    "HAVING out_degree > 3 " "ORDER BY RANDOM() LIMIT 1;";
  sqlite3_stmt *st = NULL;
  int sector_id = -1;

  if (sqlite3_prepare_v2 (db, q, -1, &st, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "get_high_degree_sector prepare failed: %s\n",
	       sqlite3_errmsg (db));
      return -1;
    }

  if (sqlite3_step (st) == SQLITE_ROW)
    {
      sector_id = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  return sector_id;
}


/**
 * @brief Finds the destination of a one-way warp from a given sector.
 * @param db The SQLite database handle.
 * @param start_sector The sector ID to find the destination for.
 * @return The ID of the destination sector, or -1 if not found.
 */
static int
get_tunnel_end (sqlite3 *db, int start_sector)
{
  const char *q = "SELECT to_sector FROM sector_warps WHERE from_sector = ?;";
  sqlite3_stmt *st = NULL;
  int end_sector = -1;

  if (sqlite3_prepare_v2 (db, q, -1, &st, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "get_tunnel_end prepare failed: %s\n",
	       sqlite3_errmsg (db));
      return -1;
    }
  sqlite3_bind_int (st, 1, start_sector);

  if (sqlite3_step (st) == SQLITE_ROW)
    {
      end_sector = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  return end_sector;
}

/**
 * @brief Creates complex warps in the universe, including one-way warps and dead-end warps.
 * This is a post-processing step after the initial random warp generation.
 * @param db The SQLite database handle.
 * @param numSectors The total number of sectors in the universe.
 * @return 0 on success, -1 on failure.
 */
int
create_complex_warps (sqlite3 *db, int numSectors)
{
  char *errmsg = 0;
  int rc;

  fprintf (stderr, "BIGBANG: Creating complex warps...\n");

  // Step 1: Create longer tunnels by splicing two existing tunnels
  const char *q_find_tunnels =
    "SELECT from_sector FROM (SELECT from_sector, COUNT(to_sector) AS out_degree FROM sector_warps GROUP BY from_sector) WHERE out_degree = 1 ORDER BY RANDOM() LIMIT 2;";
  sqlite3_stmt *st = NULL;
  int start_sector1 = -1, start_sector2 = -1;

  if (sqlite3_prepare_v2 (db, q_find_tunnels, -1, &st, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "find_tunnels prepare failed: %s\n",
	       sqlite3_errmsg (db));
      return -1;
    }

  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      start_sector1 = sqlite3_column_int (st, 0);
    }
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      start_sector2 = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);

  if (start_sector1 > 0 && start_sector2 > 0
      && start_sector1 != start_sector2)
    {
      int end_sector1 = get_tunnel_end (db, start_sector1);
      int end_sector2 = get_tunnel_end (db, start_sector2);

      if (end_sector1 != -1 && end_sector2 != -1
	  && end_sector1 != end_sector2)
	{
	  char sql_delete_warp[128];
	  snprintf (sql_delete_warp, sizeof (sql_delete_warp),
		    "DELETE FROM sector_warps WHERE from_sector = %d AND to_sector = %d;",
		    end_sector1, start_sector1);
	  sqlite3_exec (db, sql_delete_warp, NULL, NULL, &errmsg);
	  if (errmsg)
	    {
	      fprintf (stderr, "SQL error: %s\n", errmsg);
	      sqlite3_free (errmsg);
	      return -1;
	    }

	  char sql_insert_warp[128];
	  snprintf (sql_insert_warp, sizeof (sql_insert_warp),
		    "INSERT INTO sector_warps (from_sector, to_sector) VALUES (%d, %d);",
		    end_sector1, start_sector2);
	  sqlite3_exec (db, sql_insert_warp, NULL, NULL, &errmsg);
	  if (errmsg)
	    {
	      fprintf (stderr, "SQL error: %s\n", errmsg);
	      sqlite3_free (errmsg);
	      // Re-insert the original warp to restore state
	      char sql_reinsert_original[128];
	      snprintf (sql_reinsert_original, sizeof (sql_reinsert_original),
			"INSERT INTO sector_warps (from_sector, to_sector) VALUES (%d, %d);",
			end_sector1, start_sector1);
	      sqlite3_exec (db, sql_reinsert_original, NULL, NULL, &errmsg); // errmsg is reused, but should be handled if this also fails
	      if (errmsg) {
		  fprintf (stderr, "CRITICAL SQL error during rollback: %s\n", errmsg);
		  sqlite3_free (errmsg);
	      }
	      return -1;
	    }
	}
    }

  // Step 2: Create one-way warps by converting bidirectional warps
  const int num_one_way_warps =
    (int) (numSectors * (DEFAULT_PERCENT_ONEWAY / 100.0));
  fprintf (stderr, "BIGBANG: Creating %d one-way warps...\n",
	   num_one_way_warps);

  for (int i = 0; i < num_one_way_warps; i++)
    {
      const char *q_find_bidirectional =
	"SELECT a, b FROM v_bidirectional_warps ORDER BY RANDOM() LIMIT 1;";
      int one_way_from = -1, one_way_to = -1;
      st = NULL;
      if (sqlite3_prepare_v2 (db, q_find_bidirectional, -1, &st, NULL) !=
	  SQLITE_OK)
	{
	  fprintf (stderr, "find_bidirectional prepare failed: %s\n",
		   sqlite3_errmsg (db));
	  continue;
	}
      if (sqlite3_step (st) == SQLITE_ROW)
	{
	  one_way_from = sqlite3_column_int (st, 0);
	  one_way_to = sqlite3_column_int (st, 1);
	}
      sqlite3_finalize (st);

      if (one_way_from > 0 && one_way_to > 0)
	{
	  char sql_delete_return[128];
	  snprintf (sql_delete_return, sizeof (sql_delete_return),
		    "DELETE FROM sector_warps WHERE from_sector = %d AND to_sector = %d;",
		    one_way_to, one_way_from);
	  sqlite3_exec (db, sql_delete_return, NULL, NULL, &errmsg);
	  if (errmsg)
	    {
	      fprintf (stderr, "SQL error: %s\n", errmsg);
	      sqlite3_free (errmsg);
	    }
	}
    }

  // Step 3: Create dead-end warps by converting low-degree sectors
  const int num_dead_end_warps =
    (int) (numSectors * (DEFAULT_PERCENT_DEADEND / 100.0));
  fprintf (stderr, "BIGBANG: Creating %d dead-end warps...\n",
	   num_dead_end_warps);

  for (int i = 0; i < num_dead_end_warps; i++)
    {
      const char *q_find_low_degree =
	"SELECT from_sector FROM (SELECT from_sector, COUNT(to_sector) AS out_degree "
	"FROM sector_warps GROUP BY from_sector) "
	"WHERE out_degree = 1 ORDER BY RANDOM() LIMIT 1;";

      int dead_end_sector = -1;
      st = NULL;
      if (sqlite3_prepare_v2 (db, q_find_low_degree, -1, &st, NULL) !=
	  SQLITE_OK)
	{
	  fprintf (stderr, "find_low_degree prepare failed: %s\n",
		   sqlite3_errmsg (db));
	  continue;
	}
      if (sqlite3_step (st) == SQLITE_ROW)
	{
	  dead_end_sector = sqlite3_column_int (st, 0);
	}
      sqlite3_finalize (st);

      if (dead_end_sector > 0)
	{
	  int exit_sector = get_high_degree_sector (db);
	  if (exit_sector > 0 && dead_end_sector != exit_sector)
	    {
	      char sql_delete[128];
	      snprintf (sql_delete, sizeof (sql_delete),
			"DELETE FROM sector_warps WHERE from_sector = %d;",
			dead_end_sector);
	      sqlite3_exec (db, sql_delete, NULL, NULL, &errmsg);
	      if (errmsg)
		{
		  fprintf (stderr, "SQL error: %s\n", errmsg);
		  sqlite3_free (errmsg);
		}

	      char sql_insert[128];
	      snprintf (sql_insert, sizeof (sql_insert),
			"INSERT INTO sector_warps (from_sector, to_sector) VALUES (%d, %d);",
			dead_end_sector, exit_sector);
	      sqlite3_exec (db, sql_insert, NULL, NULL, &errmsg);
	      if (errmsg)
		{
		  fprintf (stderr, "SQL error: %s\n", errmsg);
		  sqlite3_free (errmsg);
		}
	    }
	}
    }

  return 0;
}
