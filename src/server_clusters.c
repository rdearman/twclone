#include "db/repo/repo_clusters.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "server_clusters.h"
#include "server_log.h"
#include "db/repo/repo_database.h"
#include "game_db.h"
#include "db/sql_driver.h"
#include "db/db_api.h"
#include "db/sql_driver.h"


/* Internal Helpers */
static int
_get_sector_count (db_t *db)
{
  int count = 0;
  if (repo_clusters_get_sector_count(db, &count) == 0)
    {
      return count;
    }
  return 0;
}


static int
_get_cluster_for_sector (db_t *db, int sector_id)
{
  int cluster_id = 0;
  if (repo_clusters_get_cluster_for_sector(db, sector_id, &cluster_id) == 0)
    {
      return cluster_id;
    }
  return 0;
}


static int
_create_cluster (db_t *db,
                 const char *name,
                 const char *role,
                 const char *kind,
                 int center_sector, int alignment, int law_severity)
{
  int cluster_id = -1;
  if (repo_clusters_create(db, name, role, kind, center_sector, alignment, law_severity, &cluster_id) != 0)
    {
      LOGE ("Failed to create cluster %s", name);
    }
  return cluster_id;
}


static void
_add_sector_to_cluster (db_t *db, int cluster_id, int sector_id)
{
  repo_clusters_add_sector(db, cluster_id, sector_id);
}


static int
_bfs_expand_cluster (db_t *db,
                     int cluster_id, int start_sector, int target_size)
{
  // Simple BFS to find connected sectors not yet in a cluster
  // This is a simplified in-memory BFS for small clusters (target_size ~10)
  int *queue = malloc (100 * sizeof (int));
  int head = 0, tail = 0;
  int count = 0;
  if (!queue)
    {
      return 0;
    }
  _add_sector_to_cluster (db, cluster_id, start_sector);
  queue[tail++] = start_sector;
  count++;


  while (head < tail && count < target_size)
    {
      int current = queue[head++];

      db_res_t *res_warps = NULL;
      db_error_t err;

      if ((res_warps = repo_clusters_get_warps(db, current, &err)) != NULL)
        {
          while (db_res_step (res_warps, &err) && count < target_size)
            {
              int neighbor = db_res_col_i32 (res_warps, 0, &err);

              // Check if already in ANY cluster
              int exists = 0;
              if (repo_clusters_check_sector_in_any_cluster(db, neighbor, &exists) != 0)
                {
                  exists = 0;
                }

              if (exists)
                {
                  continue;         // Already taken
                }
              // Add to cluster
              _add_sector_to_cluster (db, cluster_id, neighbor);

              // Add to queue if unique in queue (simple scan)
              int in_queue = 0;


              for (int i = 0; i < tail; i++)
                {
                  if (queue[i] == neighbor)
                    {
                      in_queue = 1;
                      break;
                    }
                }
              if (!in_queue)
                {
                  queue[tail++] = neighbor;
                  count++;
                }
            }
          db_res_finalize (res_warps);
        }
    }
  free (queue);
  return count;
}


/* Public API Implementation */
int
clusters_init (db_t *db)
{
  int inited = 0;
  if (repo_clusters_is_initialized(db, &inited) == 0 && inited)
    {
      return 0;
    }

  LOGI ("Initializing Clusters...");
  // 2. Create Federation Cluster (Sectors 1-10)
  int fed_id = _create_cluster (db,
                                "Federation Core",
                                "FED",
                                "FACTION",
                                1,
                                100,
                                3);


  for (int i = 1; i <= 10; i++)
    {
      _add_sector_to_cluster (db, fed_id, i);
    }
  // 3. Ferrengi
  int fer_sector = 0;
  if (repo_clusters_get_planet_sector(db, 2, &fer_sector) != 0)
    {
      fer_sector = 0;
    }

  if (fer_sector > 0)
    {
      int c_id = _create_cluster (db,
                                  "Ferrengi Territory",
                                  "FERR",
                                  "FACTION",
                                  fer_sector,
                                  -25,
                                  2);


      _bfs_expand_cluster (db, c_id, fer_sector, 8);    // Expand
    }
  // 4. Orion
  int ori_sector = 0;
  if (repo_clusters_get_planet_sector(db, 3, &ori_sector) != 0)
    {
      ori_sector = 0;
    }

  if (ori_sector > 0)
    {
      int c_id = _create_cluster (db,
                                  "Orion Syndicate Space",
                                  "ORION",
                                  "FACTION",
                                  ori_sector,
                                  -100,
                                  1);


      if (c_id != -1)
        {
          int added_sectors = _bfs_expand_cluster (db, c_id, ori_sector, 8);


          LOGD
          (
            "clusters_init: Created Orion cluster (ID: %d, Sector: %d) with %d sectors.",
            c_id,
            ori_sector,
            added_sectors);
        }
      else
        {
          LOGE ("clusters_init: Failed to create Orion cluster.");
        }
    }
  // 5. Random Clusters
  int total_sectors = _get_sector_count (db);
  int target_clustered_sectors = (int) (total_sectors * 0.15);
  // Count currently clustered
  int current_clustered = 0;
  if (repo_clusters_get_clustered_count(db, &current_clustered) != 0)
    {
      current_clustered = 0;
    }

  LOGD ("Cluster Init: Total %d, Target Clustered %d, Current %d",
        total_sectors, target_clustered_sectors, current_clustered);

  int attempts = 0;


  while (current_clustered < target_clustered_sectors && attempts < 1000)
    {
      int seed = 0;
      if (repo_clusters_pick_random_unclustered_sector(db, &seed) != 0)
        {
          seed = 0;
        }

      if (seed > 0)
        {
          // Generate params
          char name[64];


          snprintf (name, sizeof (name), "Cluster %d", seed);
          int alignment = (rand () % 101) - 50; // -50 to 50
          int c_id = _create_cluster (db,
                                      name,
                                      "RANDOM",
                                      "RANDOM",
                                      seed,
                                      alignment,
                                      1);
          int size = 4 + (rand () % 7); // 4 to 10
          int added = _bfs_expand_cluster (db, c_id, seed, size);


          current_clustered += added;
          // If we added nothing (trapped?), don't loop forever
          if (added == 0)
            {
              attempts++;
            }
        }
      else
        {
          // No more available sectors?
          break;
        }
    }
  LOGD ("Cluster generation complete. Total clustered sectors: %d",
        current_clustered);
  return 0;
}


int
cluster_economy_step (db_t *db, int64_t now_s)
{
  (void) now_s;
  // 1. Ensure clusters exist (safety)
  clusters_init (db);
  LOGD ("Running Cluster Economy Step...");
  // Iterate all clusters
  db_res_t *res_clusters = NULL;
  db_error_t err;


  if ((res_clusters = repo_clusters_get_all(db, &err)) != NULL)
    {
      while (db_res_step (res_clusters, &err))
        {
          int cluster_id = db_res_col_i32 (res_clusters, 0, &err);
          const char *commodities[] = { "ore", "organics", "equipment" };


          for (int i = 0; i < 3; i++)
            {
              const char *comm = commodities[i];
              // Calculate Avg Price
              double avg_price = 0;
              if (repo_clusters_get_avg_price(db, cluster_id, comm, &avg_price) != 0)
                {
                  continue;
                }

              if (avg_price < 1.0)
                {
                  continue;         // No trades?
                }
              int mid_price = (int) avg_price;
              // Update Index
              repo_clusters_update_commodity_index(db, cluster_id, comm, mid_price);

              // Drift Ports
              repo_clusters_drift_port_prices(db, mid_price, comm, cluster_id);
            }
        }
      db_res_finalize (res_clusters);
    }
  return 0;
}

/* Law Enforcement */
int
cluster_can_trade (db_t *db, int sector_id, int player_id)
{
  int cluster_id = _get_cluster_for_sector (db, sector_id);
  if (cluster_id == 0)
    {
      return 1;                 // Not in a cluster, standard rules apply
    }
  int banned = 0;
  if (repo_clusters_get_player_banned(db, cluster_id, player_id, &banned) != 0)
    {
      banned = 0;
    }
  return (banned == 1) ? 0 : 1;
}


double
cluster_get_bust_modifier (db_t *db, int sector_id, int player_id)
{
  int cluster_id = _get_cluster_for_sector (db, sector_id);
  if (cluster_id == 0)
    {
      return 0.0;
    }
  int suspicion = 0;
  int wanted = 0;
  repo_clusters_get_player_suspicion_wanted(db, cluster_id, player_id, &suspicion, &wanted);
  // Formula: Base modifier
  // e.g. Wanted level 1 = +10%, Level 2 = +25%, Level 3 = +50%
  // Suspicion adds tiny bits.
  double mod = (wanted * 0.15) + (suspicion * 0.01);


  return mod;
}


void
cluster_on_crime (db_t *db,
                  int player_id, int success, int sector_id, int busted)
{
  int cluster_id = _get_cluster_for_sector (db, sector_id);
  if (cluster_id == 0)
    {
      return;
    }
  // Upsert row
  int susp_inc = success ? 2 : 0;


  if (busted)
    {
      susp_inc += 10;
    }

  repo_clusters_upsert_player_status(db, cluster_id, player_id, susp_inc, busted);
}


/* New: Illegal Goods Seeding using entity_stock */
int
clusters_seed_illegal_goods (db_t *db)
{
  if (!db)
    {
      LOGE ("clusters_seed_illegal_goods: Database handle is NULL.");
      return -1;
    }
  LOGD ("Seeding illegal goods in ports...");
  db_res_t *res_ports = NULL;
  db_error_t err;


  // Prepare statement to select all ports
  if ((res_ports = repo_clusters_get_all_ports(db, &err)) != NULL)
    {
      while (db_res_step (res_ports, &err))
        {
          int port_id = db_res_col_i32 (res_ports, 0, &err);
          int sector_id = db_res_col_i32 (res_ports, 1, &err);
          int cluster_alignment = 0;        // Default to neutral

          if (repo_clusters_get_alignment(db, sector_id, &cluster_alignment) != 0)
            {
              cluster_alignment = 0;
            }

          // Check if cluster is evil
          if (cluster_alignment <= CLUSTER_EVIL_MAX_ALIGN)
            {
              int severity_factor = abs (cluster_alignment) / 25;   // e.g., -100/25=4, -25/25=1


              if (severity_factor < 1)
                {
                  severity_factor = 1;
                }
              int slaves_qty = (rand () % 10 + 1) * severity_factor;
              int weapons_qty = (rand () % 15 + 1) * severity_factor;
              int drugs_qty = (rand () % 20 + 1) * severity_factor;

              int64_t now_s = (int64_t)time (NULL);

              // Update SLV
              repo_clusters_upsert_port_stock(db, port_id, "SLV", slaves_qty, now_s);

              // Update WPN
              repo_clusters_upsert_port_stock(db, port_id, "WPN", weapons_qty, now_s);

              // Update DRG
              repo_clusters_upsert_port_stock(db, port_id, "DRG", drugs_qty, now_s);

              LOGD
              (
                "Seeding illegal goods for port %d (sector %d, alignment %d): SLV=%d, WPN=%d, DRG=%d",
                port_id,
                sector_id,
                cluster_alignment,
                slaves_qty,
                weapons_qty,
                drugs_qty);
            }
        }
      db_res_finalize (res_ports);
    }
  return 0;
}


int
cluster_black_market_step (db_t *db, int64_t now_s)
{
  (void) db;
  (void) now_s;
  // Placeholder logic until you implement the black market
  // return cluster_economy_step(db, now_s); // Or just return 0
  return 0;
}