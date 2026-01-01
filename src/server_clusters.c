#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "server_clusters.h"
#include "server_log.h"
#include "database.h"
#include "game_db.h"
#include "db/db_api.h"


/* Internal Helpers */
static int
_get_sector_count (db_t *db)
{
  db_res_t *res = NULL;
  db_error_t err;
  int count = 0;
  if (db_query (db, "SELECT COUNT(*) FROM sectors", NULL, 0, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          count = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }
  return count;
}


static int
_get_cluster_for_sector (db_t *db, int sector_id)
{
  db_res_t *res = NULL;
  db_error_t err;
  int cluster_id = 0;
  db_bind_t params[] = { db_bind_i32 (sector_id) };

  if (db_query (db,
                "SELECT cluster_id FROM cluster_sectors WHERE sector_id = $1",
                params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          cluster_id = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }
  return cluster_id;
}


static int
_create_cluster (db_t *db,
                 const char *name,
                 const char *role,
                 const char *kind,
                 int center_sector, int alignment, int law_severity)
{
  int cluster_id = -1;
  const char *sql =
    "INSERT INTO clusters (name, role, kind, center_sector, alignment, law_severity) VALUES ($1, $2, $3, $4, $5, $6)";

  db_bind_t params[] = {
    db_bind_text (name),
    db_bind_text (role),
    db_bind_text (kind),
    db_bind_i32 (center_sector),
    db_bind_i32 (alignment),
    db_bind_i32 (law_severity)
  };

  db_error_t err;
  int64_t new_id = 0;

  if (db_exec_insert_id (db, sql, params, 6, &new_id, &err))
    {
      cluster_id = (int) new_id;
    }
  else
    {
      LOGE ("Failed to create cluster %s: %s", name, err.message);
    }
  return cluster_id;
}


static void
_add_sector_to_cluster (db_t *db, int cluster_id, int sector_id)
{
  const char *sql =
    "INSERT OR IGNORE INTO cluster_sectors (cluster_id, sector_id) VALUES ($1, $2)";
  db_bind_t params[] = { db_bind_i32 (cluster_id), db_bind_i32 (sector_id) };
  db_error_t err;
  db_exec (db, sql, params, 2, &err);
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

  const char *sql_warps =
    "SELECT to_sector FROM sector_warps WHERE from_sector = $1";

  const char *sql_check = "SELECT 1 FROM cluster_sectors WHERE sector_id = $1";


  while (head < tail && count < target_size)
    {
      int current = queue[head++];

      db_res_t *res_warps = NULL;
      db_error_t err;
      db_bind_t params_w[] = { db_bind_i32 (current) };


      if (db_query (db, sql_warps, params_w, 1, &res_warps, &err))
        {
          while (db_res_step (res_warps, &err) && count < target_size)
            {
              int neighbor = db_res_col_i32 (res_warps, 0, &err);

              // Check if already in ANY cluster
              db_res_t *res_check = NULL;
              db_bind_t params_c[] = { db_bind_i32 (neighbor) };
              int exists = 0;


              if (db_query (db, sql_check, params_c, 1, &res_check, &err))
                {
                  if (db_res_step (res_check, &err))
                    {
                      exists = 1;
                    }
                  db_res_finalize (res_check);
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
  db_res_t *res = NULL;
  db_error_t err;
  // 1. Check if initialized
  if (db_query (db, "SELECT 1 FROM clusters LIMIT 1", NULL, 0, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          db_res_finalize (res);
          return 0;                 // Already done
        }
      db_res_finalize (res);
    }
  else
    {
      return -1;
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


  if (db_query (db,
                "SELECT sector FROM planets WHERE num=2",
                NULL,
                0,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          fer_sector = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
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


  if (db_query (db,
                "SELECT sector FROM planets WHERE num=3",
                NULL,
                0,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          ori_sector = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
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


  if (db_query (db, "SELECT COUNT(*) FROM cluster_sectors", NULL, 0, &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          current_clustered = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }

  LOGD ("Cluster Init: Total %d, Target Clustered %d, Current %d",
        total_sectors, target_clustered_sectors, current_clustered);

  // Prepare for random generation - we'll query one by one in loop for simplicity
  const char *sql_pick =
    (db_backend (db) == DB_BACKEND_POSTGRES)
      ?
    "SELECT id FROM sectors WHERE id > 10 AND id NOT IN (SELECT sector_id FROM cluster_sectors) ORDER BY RANDOM() LIMIT 1"
      :
    "SELECT id FROM sectors WHERE id > 10 AND id NOT IN (SELECT sector_id FROM cluster_sectors) ORDER BY RANDOM() LIMIT 1";

  int attempts = 0;


  while (current_clustered < target_clustered_sectors && attempts < 1000)
    {
      int seed = 0;


      if (db_query (db, sql_pick, NULL, 0, &res, &err))
        {
          if (db_res_step (res, &err))
            {
              seed = db_res_col_i32 (res, 0, &err);
            }
          db_res_finalize (res);
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


  if (db_query (db,
                "SELECT id, name FROM clusters",
                NULL,
                0,
                &res_clusters,
                &err))
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
              db_res_t *res_avg = NULL;
              const char *sql_avg_real =
                "SELECT AVG(price) FROM port_trade pt "
                "JOIN ports p ON p.id = pt.port_id "
                "JOIN cluster_sectors cs ON cs.sector_id_id = p.sector_id "
                "WHERE cs.cluster_id = $1 AND pt.commodity = $2";

              db_bind_t params_avg[] = { db_bind_i32 (cluster_id),
                                         db_bind_text (comm) };


              if (db_query (db, sql_avg_real, params_avg, 2, &res_avg, &err))
                {
                  if (db_res_step (res_avg, &err))
                    {
                      avg_price = db_res_col_double (res_avg, 0, &err);
                    }
                  db_res_finalize (res_avg);
                }
              if (avg_price < 1.0)
                {
                  continue;         // No trades?
                }
              int mid_price = (int) avg_price;
              // Update Index
              const char *sql_idx =
                "INSERT INTO cluster_commodity_index (cluster_id, commodity_code, mid_price, last_updated) VALUES ($1, $2, $3, CURRENT_TIMESTAMP) "
                "ON CONFLICT(cluster_id, commodity_code) DO UPDATE SET mid_price=excluded.mid_price, last_updated=CURRENT_TIMESTAMP";

              db_bind_t params_idx[] = { db_bind_i32 (cluster_id),
                                         db_bind_text (comm),
                                         db_bind_i32 (mid_price) };


              db_exec (db, sql_idx, params_idx, 3, &err);

              // Drift Ports
              // new_price = price + 0.1 * (mid - price)
              const char *sql_drift =
                "UPDATE port_trade "
                "SET price = CAST(price + 0.1 * ($1 - price) AS INTEGER) "
                "WHERE commodity = $2 AND port_id IN ("
                "  SELECT p.id FROM ports p "
                "  JOIN cluster_sectors cs ON cs.sector_id_id = p.sector_id "
                "  WHERE cs.cluster_id = $3" ")";

              db_bind_t params_drift[] = { db_bind_i32 (mid_price),
                                           db_bind_text (comm),
                                           db_bind_i32 (cluster_id) };


              db_exec (db, sql_drift, params_drift, 3, &err);
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
  db_res_t *res = NULL;
  db_error_t err;
  int banned = 0;
  db_bind_t params[] = { db_bind_i32 (cluster_id), db_bind_i32 (player_id) };


  if (db_query (db,
                "SELECT banned FROM cluster_player_status WHERE cluster_id = $1 AND player_id = $2",
                params,
                2,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          banned = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
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
  db_res_t *res = NULL;
  db_error_t err;
  db_bind_t params[] = { db_bind_i32 (cluster_id), db_bind_i32 (player_id) };


  if (db_query (db,
                "SELECT suspicion, wanted_level FROM cluster_player_status WHERE cluster_id = $1 AND player_id = $2",
                params,
                2,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          suspicion = db_res_col_i32 (res, 0, &err);
          wanted = db_res_col_i32 (res, 1, &err);
        }
      db_res_finalize (res);
    }
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

  const char *sql_upsert =
    "INSERT INTO cluster_player_status (cluster_id, player_id, suspicion, bust_count, last_bust_at) "
    "VALUES ($1, $2, $3, $4, CASE WHEN $5=1 THEN CURRENT_TIMESTAMP ELSE NULL END) "
    "ON CONFLICT(cluster_id, player_id) DO UPDATE SET "
    "suspicion = suspicion + $6, "
    "bust_count = bust_count + $7, "
    "last_bust_at = CASE WHEN $8=1 THEN CURRENT_TIMESTAMP ELSE last_bust_at END;";

  db_bind_t params[] = {
    db_bind_i32 (cluster_id),
    db_bind_i32 (player_id),
    db_bind_i32 (susp_inc),
    db_bind_i32 (busted ? 1 : 0),
    db_bind_i32 (busted),
    db_bind_i32 (susp_inc),
    db_bind_i32 (busted ? 1 : 0),
    db_bind_i32 (busted)
  };

  db_error_t err;


  db_exec (db, sql_upsert, params, 8, &err);
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
  if (db_query (db, "SELECT id, sector FROM ports", NULL, 0, &res_ports, &err))
    {
      while (db_res_step (res_ports, &err))
        {
          int port_id = db_res_col_i32 (res_ports, 0, &err);
          int sector_id = db_res_col_i32 (res_ports, 1, &err);
          int cluster_alignment = 0;        // Default to neutral

          // Get cluster alignment for the port's sector
          db_res_t *res_align = NULL;
          const char *sql_cluster_align =
            "SELECT c.alignment FROM clusters c JOIN cluster_sectors cs ON cs.cluster_id = c.id WHERE cs.sector_id_id = $1 LIMIT 1";
          db_bind_t params_a[] = { db_bind_i32 (sector_id) };


          if (db_query (db, sql_cluster_align, params_a, 1, &res_align, &err))
            {
              if (db_res_step (res_align, &err))
                {
                  cluster_alignment = db_res_col_i32 (res_align, 0, &err);
                }
              db_res_finalize (res_align);
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

              const char *sql_update_stock =
                "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "
                "VALUES ('port', $1, $2, $3, 0, $4) "
                "ON CONFLICT(entity_type, entity_id, commodity_code) DO UPDATE SET quantity = excluded.quantity, last_updated_ts = excluded.last_updated_ts;";

              int64_t now_s = (int64_t)time (NULL);

              // Update SLV
              db_bind_t p_slv[] = { db_bind_i32 (port_id), db_bind_text ("SLV"),
                                    db_bind_i32 (slaves_qty),
                                    db_bind_i64 (now_s) };


              db_exec (db, sql_update_stock, p_slv, 4, &err);

              // Update WPN
              db_bind_t p_wpn[] = { db_bind_i32 (port_id), db_bind_text ("WPN"),
                                    db_bind_i32 (weapons_qty),
                                    db_bind_i64 (now_s) };


              db_exec (db, sql_update_stock, p_wpn, 4, &err);

              // Update DRG
              db_bind_t p_drg[] = { db_bind_i32 (port_id), db_bind_text ("DRG"),
                                    db_bind_i32 (drugs_qty),
                                    db_bind_i64 (now_s) };


              db_exec (db, sql_update_stock, p_drg, 4, &err);

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