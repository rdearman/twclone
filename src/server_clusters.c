#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "server_clusters.h"
#include "server_log.h"
#include "database.h"


/* Internal Helpers */
static int
_get_sector_count (sqlite3 *db)
{
  sqlite3_stmt *stmt;
  int count = 0;
  if (sqlite3_prepare_v2 (db, "SELECT COUNT(*) FROM sectors", -1, &stmt,
			  NULL) == SQLITE_OK)
    {
      if (sqlite3_step (stmt) == SQLITE_ROW)
	{
	  count = sqlite3_column_int (stmt, 0);
	}
      sqlite3_finalize (stmt);
    }
  return count;
}


static int
_get_cluster_for_sector (sqlite3 *db, int sector_id)
{
  sqlite3_stmt *stmt;
  int cluster_id = 0;
  if (sqlite3_prepare_v2 (db,
			  "SELECT cluster_id FROM cluster_sectors WHERE sector_id = ?",
			  -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (stmt, 1, sector_id);
      if (sqlite3_step (stmt) == SQLITE_ROW)
	{
	  cluster_id = sqlite3_column_int (stmt, 0);
	}
      sqlite3_finalize (stmt);
    }
  return cluster_id;
}


static int
_create_cluster (sqlite3 *db,
		 const char *name,
		 const char *role,
		 const char *kind,
		 int center_sector, int alignment, int law_severity)
{
  sqlite3_stmt *stmt;
  int cluster_id = -1;
  const char *sql =
    "INSERT INTO clusters (name, role, kind, center_sector, alignment, law_severity) VALUES (?, ?, ?, ?, ?, ?)";
  if (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_text (stmt, 1, name, -1, SQLITE_STATIC);
      sqlite3_bind_text (stmt, 2, role, -1, SQLITE_STATIC);
      sqlite3_bind_text (stmt, 3, kind, -1, SQLITE_STATIC);
      sqlite3_bind_int (stmt, 4, center_sector);
      sqlite3_bind_int (stmt, 5, alignment);
      sqlite3_bind_int (stmt, 6, law_severity);
      if (sqlite3_step (stmt) == SQLITE_DONE)
	{
	  cluster_id = (int) sqlite3_last_insert_rowid (db);
	}
      else
	{
	  LOGE ("Failed to create cluster %s: %s", name, sqlite3_errmsg (db));
	}
      sqlite3_finalize (stmt);
    }
  return cluster_id;
}


static void
_add_sector_to_cluster (sqlite3 *db, int cluster_id, int sector_id)
{
  sqlite3_stmt *stmt;
  const char *sql =
    "INSERT OR IGNORE INTO cluster_sectors (cluster_id, sector_id) VALUES (?, ?)";
  if (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (stmt, 1, cluster_id);
      sqlite3_bind_int (stmt, 2, sector_id);
      sqlite3_step (stmt);
      sqlite3_finalize (stmt);
    }
}


static int
_bfs_expand_cluster (sqlite3 *db,
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
  sqlite3_stmt *warp_stmt;
  const char *sql_warps =
    "SELECT to_sector FROM sector_warps WHERE from_sector = ?";


  if (sqlite3_prepare_v2 (db, sql_warps, -1, &warp_stmt, NULL) != SQLITE_OK)
    {
      free (queue);
      return count;
    }
  // Check statement for existing assignment
  sqlite3_stmt *check_stmt;
  const char *sql_check = "SELECT 1 FROM cluster_sectors WHERE sector_id = ?";


  if (sqlite3_prepare_v2 (db, sql_check, -1, &check_stmt, NULL) != SQLITE_OK)
    {
      sqlite3_finalize (warp_stmt);
      free (queue);
      return count;
    }
  while (head < tail && count < target_size)
    {
      int current = queue[head++];


      sqlite3_reset (warp_stmt);
      sqlite3_bind_int (warp_stmt, 1, current);
      while (sqlite3_step (warp_stmt) == SQLITE_ROW && count < target_size)
	{
	  int neighbor = sqlite3_column_int (warp_stmt, 0);


	  // Check if already in ANY cluster
	  sqlite3_reset (check_stmt);
	  sqlite3_bind_int (check_stmt, 1, neighbor);
	  if (sqlite3_step (check_stmt) == SQLITE_ROW)
	    {
	      continue;		// Already taken
	    }
	  // Also avoid adding same sector twice in this pass (simple check: is it in queue?
	  // For simplicity, we just insert into DB and rely on 'INSERT OR IGNORE' logic there,
	  // but we need to know if we *actually* added it to increment count)
	  // Try adding
	  _add_sector_to_cluster (db, cluster_id, neighbor);
	  // Check if it stuck (this is slightly inefficient but robust)
	  // Actually, _add_sector_to_cluster is void. Let's assume it works for now
	  // and check DB count or just assume success if check_stmt failed.
	  // Proper way: check queue
	  int in_queue = 0;


	  for (int i = 0; i < tail; i++)
	    {
	      if (queue[i] == neighbor)
		{
		  in_queue = 1;
		}
	    }
	  if (!in_queue)
	    {
	      queue[tail++] = neighbor;
	      count++;
	    }
	}
    }
  sqlite3_finalize (warp_stmt);
  sqlite3_finalize (check_stmt);
  free (queue);
  return count;
}


/* Public API Implementation */
int
clusters_init (sqlite3 *db)
{
  sqlite3_stmt *stmt;
  int rc;
  // 1. Check if initialized
  rc = sqlite3_prepare_v2 (db, "SELECT 1 FROM clusters LIMIT 1", -1, &stmt,
			   NULL);
  if (rc != SQLITE_OK)
    {
      return -1;
    }
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      sqlite3_finalize (stmt);
      return 0;			// Already done
    }
  sqlite3_finalize (stmt);
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


  rc = sqlite3_prepare_v2 (db, "SELECT sector FROM planets WHERE num=2", -1, &stmt, NULL);	// Assuming num=2 is fixed ID from bigbang
  if (rc == SQLITE_OK && sqlite3_step (stmt) == SQLITE_ROW)
    {
      fer_sector = sqlite3_column_int (stmt, 0);
    }
  sqlite3_finalize (stmt);
  if (fer_sector > 0)
    {
      int c_id = _create_cluster (db,
				  "Ferrengi Territory",
				  "FERR",
				  "FACTION",
				  fer_sector,
				  -25,
				  2);


      _bfs_expand_cluster (db, c_id, fer_sector, 8);	// Expand
    }
  // 4. Orion
  int ori_sector = 0;


  rc = sqlite3_prepare_v2 (db,
			   "SELECT sector FROM planets WHERE num=3",
			   -1, &stmt, NULL);
  if (rc == SQLITE_OK && sqlite3_step (stmt) == SQLITE_ROW)
    {
      ori_sector = sqlite3_column_int (stmt, 0);
    }
  sqlite3_finalize (stmt);
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
	    ("clusters_init: Created Orion cluster (ID: %d, Sector: %d) with %d sectors.",
	     c_id, ori_sector, added_sectors);
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


  rc = sqlite3_prepare_v2 (db,
			   "SELECT COUNT(*) FROM cluster_sectors",
			   -1, &stmt, NULL);
  if (rc == SQLITE_OK && sqlite3_step (stmt) == SQLITE_ROW)
    {
      current_clustered = sqlite3_column_int (stmt, 0);
    }
  sqlite3_finalize (stmt);
  LOGD ("Cluster Init: Total %d, Target Clustered %d, Current %d",
	total_sectors, target_clustered_sectors, current_clustered);
  // Prepare for random generation
  sqlite3_stmt *pick_stmt;


  rc = sqlite3_prepare_v2 (db,
			   "SELECT id FROM sectors WHERE id > 10 AND id NOT IN (SELECT sector_id FROM cluster_sectors) ORDER BY RANDOM() LIMIT 1",
			   -1, &pick_stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("Failed to prepare sector picker");
      return -1;
    }
  int attempts = 0;


  while (current_clustered < target_clustered_sectors && attempts < 1000)
    {
      if (sqlite3_step (pick_stmt) == SQLITE_ROW)
	{
	  int seed = sqlite3_column_int (pick_stmt, 0);


	  sqlite3_reset (pick_stmt);	// Reset for next iteration
	  // Generate params
	  char name[64];


	  snprintf (name, sizeof (name), "Cluster %d", seed);
	  int alignment = (rand () % 101) - 50;	// -50 to 50
	  int c_id = _create_cluster (db,
				      name,
				      "RANDOM",
				      "RANDOM",
				      seed,
				      alignment,
				      1);
	  int size = 4 + (rand () % 7);	// 4 to 10
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
	  sqlite3_reset (pick_stmt);
	  break;
	}
    }
  sqlite3_finalize (pick_stmt);
  LOGD ("Cluster generation complete. Total clustered sectors: %d",
	current_clustered);
  return 0;
}


int
cluster_economy_step (sqlite3 *db, int64_t now_s)
{
  (void) now_s;
  // 1. Ensure clusters exist (safety)
  clusters_init (db);
  LOGD ("Running Cluster Economy Step...");
  // Iterate all clusters
  sqlite3_stmt *cluster_stmt;


  if (sqlite3_prepare_v2 (db,
			  "SELECT id, name FROM clusters",
			  -1, &cluster_stmt, NULL) != SQLITE_OK)
    {
      return -1;
    }
  while (sqlite3_step (cluster_stmt) == SQLITE_ROW)
    {
      int cluster_id = sqlite3_column_int (cluster_stmt, 0);
      const char *commodities[] = { "ore", "organics", "equipment" };


      for (int i = 0; i < 3; i++)
	{
	  const char *comm = commodities[i];
	  // Calculate Avg Price
	  double avg_price = 0;
	  sqlite3_stmt *avg_stmt;
	  const char *sql_avg_real =
	    "SELECT AVG(price) FROM port_trade pt "
	    "JOIN ports p ON p.id = pt.port_id "
	    "JOIN cluster_sectors cs ON cs.sector_id = p.sector "
	    "WHERE cs.cluster_id = ? AND pt.commodity = ?";


	  if (sqlite3_prepare_v2 (db, sql_avg_real, -1, &avg_stmt,
				  NULL) == SQLITE_OK)
	    {
	      sqlite3_bind_int (avg_stmt, 1, cluster_id);
	      sqlite3_bind_text (avg_stmt, 2, comm, -1, SQLITE_STATIC);
	      if (sqlite3_step (avg_stmt) == SQLITE_ROW)
		{
		  avg_price = sqlite3_column_double (avg_stmt, 0);
		}
	      sqlite3_finalize (avg_stmt);
	    }
	  if (avg_price < 1.0)
	    {
	      continue;		// No trades?
	    }
	  int mid_price = (int) avg_price;
	  // Update Index
	  sqlite3_stmt *idx_stmt;
	  const char *sql_idx =
	    "INSERT INTO cluster_commodity_index (cluster_id, commodity_code, mid_price, last_updated) VALUES (?, ?, ?, CURRENT_TIMESTAMP) "
	    "ON CONFLICT(cluster_id, commodity_code) DO UPDATE SET mid_price=excluded.mid_price, last_updated=CURRENT_TIMESTAMP";


	  if (sqlite3_prepare_v2 (db, sql_idx, -1, &idx_stmt,
				  NULL) == SQLITE_OK)
	    {
	      sqlite3_bind_int (idx_stmt, 1, cluster_id);
	      sqlite3_bind_text (idx_stmt, 2, comm, -1, SQLITE_STATIC);
	      sqlite3_bind_int (idx_stmt, 3, mid_price);
	      sqlite3_step (idx_stmt);
	      sqlite3_finalize (idx_stmt);
	    }
	  // Drift Ports
	  // We can do this in SQL mostly
	  // new_price = price + 0.1 * (mid - price)
	  sqlite3_stmt *drift_stmt;
	  const char *sql_drift =
	    "UPDATE port_trade "
	    "SET price = CAST(price + 0.1 * (? - price) AS INTEGER) "
	    "WHERE commodity = ? AND port_id IN ("
	    "  SELECT p.id FROM ports p "
	    "  JOIN cluster_sectors cs ON cs.sector_id = p.sector "
	    "  WHERE cs.cluster_id = ?" ")";


	  if (sqlite3_prepare_v2 (db, sql_drift, -1, &drift_stmt,
				  NULL) == SQLITE_OK)
	    {
	      sqlite3_bind_int (drift_stmt, 1, mid_price);
	      sqlite3_bind_text (drift_stmt, 2, comm, -1, SQLITE_STATIC);
	      sqlite3_bind_int (drift_stmt, 3, cluster_id);
	      sqlite3_step (drift_stmt);
	      sqlite3_finalize (drift_stmt);
	    }
	}
    }
  sqlite3_finalize (cluster_stmt);
  return 0;
}


/* Law Enforcement */
int
cluster_can_trade (sqlite3 *db, int sector_id, int player_id)
{
  int cluster_id = _get_cluster_for_sector (db, sector_id);
  if (cluster_id == 0)
    {
      return 1;			// Not in a cluster, standard rules apply
    }
  sqlite3_stmt *stmt;
  int banned = 0;


  if (sqlite3_prepare_v2 (db,
			  "SELECT banned FROM cluster_player_status WHERE cluster_id = ? AND player_id = ?",
			  -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (stmt, 1, cluster_id);
      sqlite3_bind_int (stmt, 2, player_id);
      if (sqlite3_step (stmt) == SQLITE_ROW)
	{
	  banned = sqlite3_column_int (stmt, 0);
	}
      sqlite3_finalize (stmt);
    }
  return (banned == 1) ? 0 : 1;
}


double
cluster_get_bust_modifier (sqlite3 *db, int sector_id, int player_id)
{
  int cluster_id = _get_cluster_for_sector (db, sector_id);
  if (cluster_id == 0)
    {
      return 0.0;
    }
  int suspicion = 0;
  int wanted = 0;
  sqlite3_stmt *stmt;


  if (sqlite3_prepare_v2 (db,
			  "SELECT suspicion, wanted_level FROM cluster_player_status WHERE cluster_id = ? AND player_id = ?",
			  -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (stmt, 1, cluster_id);
      sqlite3_bind_int (stmt, 2, player_id);
      if (sqlite3_step (stmt) == SQLITE_ROW)
	{
	  suspicion = sqlite3_column_int (stmt, 0);
	  wanted = sqlite3_column_int (stmt, 1);
	}
      sqlite3_finalize (stmt);
    }
  // Formula: Base modifier
  // e.g. Wanted level 1 = +10%, Level 2 = +25%, Level 3 = +50%
  // Suspicion adds tiny bits.
  double mod = (wanted * 0.15) + (suspicion * 0.01);


  return mod;
}


void
cluster_on_crime (sqlite3 *db,
		  int player_id, int success, int sector_id, int busted)
{
  int cluster_id = _get_cluster_for_sector (db, sector_id);
  if (cluster_id == 0)
    {
      return;
    }
  // Upsert row
  sqlite3_stmt *stmt = NULL;
  int susp_inc = success ? 2 : 0;


  if (busted)
    {
      susp_inc += 10;
    }
  char *sql_update = sqlite3_mprintf ("UPDATE cluster_player_status SET "
				      "suspicion = suspicion + %d, "
				      "bust_count = bust_count + %d, "
				      "last_bust_at = CASE WHEN %d=1 THEN CURRENT_TIMESTAMP ELSE last_bust_at END "
				      "WHERE cluster_id = %d AND player_id = %d",
				      susp_inc,
				      busted ? 1 : 0,
				      busted,
				      cluster_id,
				      player_id);


  sqlite3_exec (db, sql_update, NULL, NULL, NULL);
  sqlite3_free (sql_update);
  sqlite3_bind_int (stmt, 1, cluster_id);
  sqlite3_bind_int (stmt, 2, player_id);
  sqlite3_step (stmt);
  sqlite3_finalize (stmt);
}


/* New: Illegal Goods Seeding using entity_stock */
int
clusters_seed_illegal_goods (sqlite3 *db)
{
  if (!db)
    {
      LOGE ("clusters_seed_illegal_goods: Database handle is NULL.");
      return -1;
    }
  LOGD ("Seeding illegal goods in ports...");
  sqlite3_stmt *port_stmt = NULL;
  sqlite3_stmt *cluster_align_stmt = NULL;


  // Prepare statement to select all ports
  if (sqlite3_prepare_v2 (db,
			  "SELECT id, sector FROM ports",
			  -1, &port_stmt, NULL) != SQLITE_OK)
    {
      LOGE ("Failed to prepare port select statement: %s",
	    sqlite3_errmsg (db));
      return -1;
    }
  // Prepare statement to get cluster alignment for a sector
  const char *sql_cluster_align =
    "SELECT c.alignment FROM clusters c JOIN cluster_sectors cs ON cs.cluster_id = c.id WHERE cs.sector_id = ? LIMIT 1";


  if (sqlite3_prepare_v2 (db, sql_cluster_align, -1, &cluster_align_stmt,
			  NULL) != SQLITE_OK)
    {
      LOGE ("Failed to prepare cluster alignment statement: %s",
	    sqlite3_errmsg (db));
      sqlite3_finalize (port_stmt);
      return -1;
    }
  // Prepared statement for updating entity_stock
  sqlite3_stmt *update_stock_stmt = NULL;
  const char *sql_update_stock =
    "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "
    "VALUES ('port', ?, ?, ?, 0, strftime('%s','now')) "
    "ON CONFLICT(entity_type, entity_id, commodity_code) DO UPDATE SET quantity = excluded.quantity, last_updated_ts = excluded.last_updated_ts;";


  if (sqlite3_prepare_v2 (db, sql_update_stock, -1, &update_stock_stmt,
			  NULL) != SQLITE_OK)
    {
      LOGE ("Failed to prepare entity_stock update statement: %s",
	    sqlite3_errmsg (db));
      sqlite3_finalize (port_stmt);
      sqlite3_finalize (cluster_align_stmt);
      return -1;
    }
  // Iterate through all ports
  while (sqlite3_step (port_stmt) == SQLITE_ROW)
    {
      int port_id = sqlite3_column_int (port_stmt, 0);
      int sector_id = sqlite3_column_int (port_stmt, 1);
      int cluster_alignment = 0;	// Default to neutral


      // LOGD ("Processing port: %d (sector: %d)", port_id, sector_id); // Reduced logging
      // Get cluster alignment for the port's sector
      sqlite3_reset (cluster_align_stmt);
      sqlite3_bind_int (cluster_align_stmt, 1, sector_id);
      if (sqlite3_step (cluster_align_stmt) == SQLITE_ROW)
	{
	  cluster_alignment = sqlite3_column_int (cluster_align_stmt, 0);
	}
      // Check if cluster is evil
      if (cluster_alignment <= CLUSTER_EVIL_MAX_ALIGN)
	{
	  int severity_factor = abs (cluster_alignment) / 25;	// e.g., -100/25=4, -25/25=1


	  if (severity_factor < 1)
	    {
	      severity_factor = 1;
	    }
	  int slaves_qty = (rand () % 10 + 1) * severity_factor;
	  int weapons_qty = (rand () % 15 + 1) * severity_factor;
	  int drugs_qty = (rand () % 20 + 1) * severity_factor;


	  // Update SLV
	  sqlite3_reset (update_stock_stmt);
	  sqlite3_bind_int (update_stock_stmt, 1, port_id);
	  sqlite3_bind_text (update_stock_stmt, 2, "SLV", -1, SQLITE_STATIC);
	  sqlite3_bind_int (update_stock_stmt, 3, slaves_qty);
	  sqlite3_step (update_stock_stmt);
	  // Update WPN
	  sqlite3_reset (update_stock_stmt);
	  sqlite3_bind_int (update_stock_stmt, 1, port_id);
	  sqlite3_bind_text (update_stock_stmt, 2, "WPN", -1, SQLITE_STATIC);
	  sqlite3_bind_int (update_stock_stmt, 3, weapons_qty);
	  sqlite3_step (update_stock_stmt);
	  // Update DRG
	  sqlite3_reset (update_stock_stmt);
	  sqlite3_bind_int (update_stock_stmt, 1, port_id);
	  sqlite3_bind_text (update_stock_stmt, 2, "DRG", -1, SQLITE_STATIC);
	  sqlite3_bind_int (update_stock_stmt, 3, drugs_qty);
	  sqlite3_step (update_stock_stmt);
	  LOGD
	    ("Seeding illegal goods for port %d (sector %d, alignment %d): SLV=%d, WPN=%d, DRG=%d",
	     port_id, sector_id, cluster_alignment, slaves_qty, weapons_qty,
	     drugs_qty);
	}
    }
  sqlite3_finalize (port_stmt);
  sqlite3_finalize (cluster_align_stmt);
  sqlite3_finalize (update_stock_stmt);
  return 0;
}


int
cluster_black_market_step (sqlite3 *db, int64_t now_s)
{
  (void) db;
  (void) now_s;
  // Placeholder logic until you implement the black market
  // return cluster_economy_step(db, now_s); // Or just return 0
  return 0;
}
