#include "db_legacy.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "server_config.h"
//#include "server_bigbang.h"
#include "db/repo/repo_database.h"           // Needed for db_get_handle()
#include "server_log.h"
#include "db/db_api.h"
#include "db/sql_driver.h"
#include "game_db.h"

// Forward declarations of internal helper functions
static int get_high_degree_sector (db_t *db);
static int get_tunnel_end (db_t *db, int start_sector);

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
  const char *q = "SELECT from_sector, COUNT(to_sector) AS out_degree "
                  "FROM sector_warps "
                  "GROUP BY from_sector "
                  "HAVING COUNT(to_sector) > 3 " "ORDER BY RANDOM() LIMIT 1;";
  db_res_t *st = NULL;
  db_error_t err;
  int sector_id = -1;

  if (db_query (db, q, NULL, 0, &st, &err))
    {
      if (db_res_step (st, &err))
        {
          sector_id = db_res_col_i32 (st, 0, &err);
        }
      db_res_finalize (st);
    }
  else
    {
      LOGE ("get_high_degree_sector query failed: %s\n", err.message);
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
  const char *q = "SELECT to_sector FROM sector_warps WHERE from_sector = {1};";
  db_res_t *st = NULL;
  db_error_t err;
  int end_sector = -1;
  db_bind_t params[] = { db_bind_i32 (start_sector) };
  char sql_converted[256];
  sql_build(db, q, sql_converted, sizeof(sql_converted));

  if (db_query (db, sql_converted, params, 1, &st, &err))
    {
      if (db_res_step (st, &err))
        {
          end_sector = db_res_col_i32 (st, 0, &err);
        }
      db_res_finalize (st);
    }
  else
    {
      LOGE ("get_tunnel_end query failed: %s\n", err.message);
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
  db_error_t err;
  LOGE ("BIGBANG: Creating complex warps...\n");

  // Step 1: Create longer tunnels by splicing two existing tunnels
  // Note: HAVING usage with alias 'out_degree' might not be standard SQL in all backends,
  // safer to use COUNT(to_sector) in HAVING or subquery.
  // SQLite allows HAVING on alias, PG allows it too usually, but let's stick to standard if possible.
  // The original used nested SELECT to group first.
  const char *q_find_tunnels =
    "SELECT from_sector FROM (SELECT from_sector, COUNT(to_sector) AS out_degree FROM sector_warps GROUP BY from_sector) AS t WHERE out_degree = 1 ORDER BY RANDOM() LIMIT 2;";

  db_res_t *st = NULL;
  int start_sector1 = -1, start_sector2 = -1;


  if (db_query (db, q_find_tunnels, NULL, 0, &st, &err))
    {
      if (db_res_step (st, &err))
        {
          start_sector1 = db_res_col_i32 (st, 0, &err);
        }
      if (db_res_step (st, &err))
        {
          start_sector2 = db_res_col_i32 (st, 0, &err);
        }
      db_res_finalize (st);
    }
  else
    {
      LOGE ("find_tunnels query failed: %s\n", err.message);
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
          const char *sql_delete_warp =
            "DELETE FROM sector_warps WHERE from_sector = {1} AND to_sector = {2};";

          db_bind_t p_del[] = { db_bind_i32 (end_sector1),
                                db_bind_i32 (start_sector1) };

          char sql_delete_warp_converted[256];
          sql_build(db, sql_delete_warp, sql_delete_warp_converted, sizeof(sql_delete_warp_converted));

          if (!db_exec (db, sql_delete_warp_converted, p_del, 2, &err))
            {
              LOGE ("SQL error delete: %s\n", err.message);
              return -1;
            }

          const char *sql_insert_warp =
            "INSERT INTO sector_warps (from_sector, to_sector) VALUES ({1}, {2});";

          db_bind_t p_ins[] = { db_bind_i32 (end_sector1),
                                db_bind_i32 (start_sector2) };

          char sql_insert_warp_converted[256];
          sql_build(db, sql_insert_warp, sql_insert_warp_converted, sizeof(sql_insert_warp_converted));

          if (!db_exec (db, sql_insert_warp_converted, p_ins, 2, &err))
            {
              LOGE ("SQL error insert: %s\n", err.message);

              // Re-insert the original warp to restore state
              db_bind_t p_restore[] = { db_bind_i32 (end_sector1),
                                        db_bind_i32 (start_sector1) };

              char sql_insert_warp_restore_converted[256];
              sql_build(db, sql_insert_warp, sql_insert_warp_restore_converted, sizeof(sql_insert_warp_restore_converted));

              if (!db_exec (db, sql_insert_warp_restore_converted, p_restore, 2, &err))
                {
                  LOGE ("CRITICAL SQL error during rollback: %s\n",
                        err.message);
                }
              return -1;
            }
        }
    }

  // Step 2: Create one-way warps by converting bidirectional warps
  const int num_one_way_warps =
    (int) (numSectors * (DEFAULT_PERCENT_ONEWAY / 100.0));


  LOGE ("BIGBANG: Creating %d one-way warps...\n", num_one_way_warps);

  const char *q_find_bidirectional =
    "SELECT a, b FROM v_bidirectional_warps ORDER BY RANDOM() LIMIT 1;";
  const char *sql_delete_return =
    "DELETE FROM sector_warps WHERE from_sector = {1} AND to_sector = {2};";


  for (int i = 0; i < num_one_way_warps; i++)
    {
      int one_way_from = -1, one_way_to = -1;


      st = NULL;

      if (db_query (db, q_find_bidirectional, NULL, 0, &st, &err))
        {
          if (db_res_step (st, &err))
            {
              one_way_from = db_res_col_i32 (st, 0, &err);
              one_way_to = db_res_col_i32 (st, 1, &err);
            }
          db_res_finalize (st);
        }
      else
        {
          LOGE ("find_bidirectional query failed: %s\n", err.message);
          continue;
        }

      if (one_way_from > 0 && one_way_to > 0)
        {
          db_bind_t p_del[] = { db_bind_i32 (one_way_to),
                                db_bind_i32 (one_way_from) };

          char sql_delete_return_converted[256];
          sql_build(db, sql_delete_return, sql_delete_return_converted, sizeof(sql_delete_return_converted));

          if (!db_exec (db, sql_delete_return_converted, p_del, 2, &err))
            {
              LOGE ("SQL error delete return: %s\n", err.message);
            }
        }
    }

  // Step 3: Create dead-end warps by converting low-degree sectors
  const int num_dead_end_warps =
    (int) (numSectors * (DEFAULT_PERCENT_DEADEND / 100.0));


  LOGE ("BIGBANG: Creating %d dead-end warps...\n", num_dead_end_warps);

  const char *q_find_low_degree =
    "SELECT from_sector FROM (SELECT from_sector, COUNT(to_sector) AS out_degree "
    "FROM sector_warps GROUP BY from_sector) AS t "
    "WHERE out_degree = 1 ORDER BY RANDOM() LIMIT 1;";

  const char *sql_delete = "DELETE FROM sector_warps WHERE from_sector = {1};";
  const char *sql_insert =
    "INSERT INTO sector_warps (from_sector, to_sector) VALUES ({1}, {2});";


  for (int i = 0; i < num_dead_end_warps; i++)
    {
      int dead_end_sector = -1;


      st = NULL;

      if (db_query (db, q_find_low_degree, NULL, 0, &st, &err))
        {
          if (db_res_step (st, &err))
            {
              dead_end_sector = db_res_col_i32 (st, 0, &err);
            }
          db_res_finalize (st);
        }
      else
        {
          LOGE ("find_low_degree query failed: %s\n", err.message);
          continue;
        }

      if (dead_end_sector > 0)
        {
          int exit_sector = get_high_degree_sector (db);


          if (exit_sector > 0 && dead_end_sector != exit_sector)
            {
              db_bind_t p_del[] = { db_bind_i32 (dead_end_sector) };

              char sql_delete_converted[256];
              sql_build(db, sql_delete, sql_delete_converted, sizeof(sql_delete_converted));

              if (!db_exec (db, sql_delete_converted, p_del, 1, &err))
                {
                  LOGE ("SQL error delete dead end: %s\n", err.message);
                }

              db_bind_t p_ins[] = { db_bind_i32 (dead_end_sector),
                                    db_bind_i32 (exit_sector) };

              char sql_insert_converted[256];
              sql_build(db, sql_insert, sql_insert_converted, sizeof(sql_insert_converted));

              if (!db_exec (db, sql_insert_converted, p_ins, 2, &err))
                {
                  LOGE ("SQL error insert dead end: %s\n", err.message);
                }
            }
        }
    }
  return 0;
}

