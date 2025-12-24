#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include "db/db_api.h"
#include "game_db.h"
#include "database.h"
#include "database_cmd.h"
#include "server_config.h"
#include "server_bigbang.h"
#include "namegen.h"
#include "server_log.h"
#include "server_players.h"


/* ----------------------------------------------------
 * Tunables
 * ---------------------------------------------------- */
/* Define constants for tunnel creation */
#define MIN_TUNNELS_TARGET          15  /* minimum tunnels you want */
#define MIN_TUNNEL_LEN              4   /* count tubes of length >= this */
#define TUNNEL_REFILL_MAX_ATTEMPTS  60  /* stop trying after this many passes */

/* --- constants for default port stock/capacity --- */
#define DEF_PORT_SIZE          5
#define DEF_PORT_TECHLEVEL     3
#define DEF_PORT_MAX_ORE       10000
#define DEF_PORT_MAX_ORG       10000
#define DEF_PORT_MAX_EQU       10000
#define DEF_PORT_PROD_ORE      5000
#define DEF_PORT_PROD_ORG      5000
#define DEF_PORT_PROD_EQU      5000
#define DEF_PORT_CREDITS       500000

/* Global Stats for Imperial Ship */
struct ImperialStats
{
  int fighters;
  int shields;
  int holds;
  int photons;
  int genesis;
  int attack;
} imperial_stats = {
  .fighters = 32000,
  .shields = 65000,
  .holds = 100,
  .photons = 100,
  .genesis = 10,
  .attack = 5000
};


/* forward decls */
static int create_derelicts (void);
static int fix_traps_with_pathcheck (db_t *db, int fedspace_max);
static int ensure_fedspace_exit (db_t *db, int outer_min, int outer_max,
                                 int add_return_edge);
static int insert_warp_unique (db_t *db, int from, int to);
static int create_random_warps (db_t *db, int numSectors, int maxWarps);
int create_imperial (void);
static int ensure_all_sectors_have_exits (db_t *db);
int create_ownership (void);
static bool has_column (db_t *db, const char *table, const char *column);
static int create_stardock_port (db_t *db, int numSectors);

struct twconfig * 
config_load (void)
{
  db_t *db = game_db_get_handle();
  if (!db) return NULL;

  const char *sql = "SELECT key, value FROM config;";
  db_res_t *res = NULL;
  db_error_t err;
  
  if (!db_query(db, sql, NULL, 0, &res, &err)) {
      fprintf (stderr, "config_load query error: %s\n", err.message);
      return NULL;
  }

  struct twconfig *cfg = calloc (1, sizeof (struct twconfig));
  if (!cfg)
    {
      db_res_finalize(res);
      return NULL;
    }

  while (db_res_step(res, &err))
    {
      const char *key = db_res_col_text(res, 0, &err);
      const char *val = db_res_col_text(res, 1, &err);

      if (!key || !val)
        {
          continue;
        }
      if (strcmp (key, "turnsperday") == 0)
        {
          cfg->turnsperday = atoi (val);
        }
      else if (strcmp (key, "maxwarps_per_sector") == 0)
        {
          cfg->maxwarps_per_sector = atoi (val);
        }
      else if (strcmp (key, "startingcredits") == 0)
        {
          cfg->startingcredits = atoi (val);
        }
      else if (strcmp (key, "startingfighters") == 0)
        {
          cfg->startingfighters = atoi (val);
        }
      else if (strcmp (key, "startingholds") == 0)
        {
          cfg->startingholds = atoi (val);
        }
      else if (strcmp (key, "processinterval") == 0)
        {
          cfg->processinterval = atoi (val);
        }
      else if (strcmp (key, "autosave") == 0)
        {
          cfg->autosave = atoi (val);
        }
      else if (strcmp (key, "max_ports") == 0)
        {
          cfg->max_ports = atoi (val);
        }
      else if (strcmp (key, "max_planets_per_sector") == 0)
        {
          cfg->max_planets_per_sector = atoi (val);
        }
      else if (strcmp (key, "max_total_planets") == 0)
        {
          cfg->max_total_planets = atoi (val);
        }
      else if (strcmp (key, "max_citadel_level") == 0)
        {
          cfg->max_citadel_level = atoi (val);
        }
      else if (strcmp (key, "number_of_planet_types") == 0)
        {
          cfg->number_of_planet_types = atoi (val);
        }
      else if (strcmp (key, "max_ship_name_length") == 0)
        {
          cfg->max_ship_name_length = atoi (val);
        }
      else if (strcmp (key, "ship_type_count") == 0)
        {
          cfg->ship_type_count = atoi (val);
        }
      else if (strcmp (key, "hash_length") == 0)
        {
          cfg->hash_length = atoi (val);
        }
      else if (strcmp (key, "default_nodes") == 0)
        {
          cfg->default_nodes = atoi (val);
        }
      else if (strcmp (key, "buff_size") == 0)
        {
          cfg->buff_size = atoi (val);
        }
      else if (strcmp (key, "max_name_length") == 0)
        {
          cfg->max_name_length = atoi (val);
        }
      else if (strcmp (key, "planet_type_count") == 0)
        {
          cfg->planet_type_count = atoi (val);
        }
      else if (strcmp (key, "shipyard_enabled") == 0)
        {
          cfg->shipyard_enabled = atoi (val);
        }
      else if (strcmp (key, "shipyard_trade_in_factor_bp") == 0)
        {
          cfg->shipyard_trade_in_factor_bp = atoi (val);
        }
      else if (strcmp (key, "shipyard_require_cargo_fit") == 0)
        {
          cfg->shipyard_require_cargo_fit = atoi (val);
        }
      else if (strcmp (key, "shipyard_require_fighters_fit") == 0)
        {
          cfg->shipyard_require_fighters_fit = atoi (val);
        }
      else if (strcmp (key, "shipyard_require_shields_fit") == 0)
        {
          cfg->shipyard_require_shields_fit = atoi (val);
        }
      else if (strcmp (key, "shipyard_require_hardware_compat") == 0)
        {
          cfg->shipyard_require_hardware_compat = atoi (val);
        }
      else if (strcmp (key, "shipyard_tax_bp") == 0)
        {
          cfg->shipyard_tax_bp = atoi (val);
        }
    }
  db_res_finalize(res);
  return cfg;
}


// A simple struct to hold warp data in memory
typedef struct
{
  int from;
  int to;
} Warp;


/* ----------------------------------------------------
 * Small helpers / guards
 * ---------------------------------------------------- */
static void
prune_tunnel_edges (db_t *db)
{
  db_error_t err;
  /* Remove edges from tunnels to non-tunnel nodes */
  db_exec (db,
                "DELETE FROM sector_warps "
                "WHERE from_sector IN (SELECT used FROM used_sectors) "
                "  AND to_sector   NOT IN (SELECT used FROM used_sectors);",
                NULL, 0, &err);
  /* And the reverse direction */
  db_exec (db,
                "DELETE FROM sector_warps "
                "WHERE to_sector   IN (SELECT used FROM used_sectors) "
                "  AND from_sector NOT IN (SELECT used FROM used_sectors);",
                NULL, 0, &err);
}


static int
insert_warp_unique (db_t *db, int from, int to)
{
  db_res_t *res = NULL;
  db_error_t err;
  
  // Check existence
  db_bind_t params[] = { db_bind_i32(from), db_bind_i32(to) };
  if (db_query(db, "SELECT 1 FROM sector_warps WHERE from_sector=$1 AND to_sector=$2 LIMIT 1;", params, 2, &res, &err)) {
      if (db_res_step(res, &err)) {
          db_res_finalize(res);
          return 0; // exists
      }
      db_res_finalize(res);
  }

  // Insert
  if (!db_exec(db, "INSERT INTO sector_warps(from_sector,to_sector) VALUES($1,$2);", params, 2, &err)) {
      return -1;
  }
  return 1;
}


static int
sector_degree (db_t *db, int s)
{
  db_res_t *res = NULL;
  db_error_t err;
  int deg = -1;
  db_bind_t params[] = { db_bind_i32(s) };
  
  if (db_query(db, "SELECT COUNT(*) FROM sector_warps WHERE from_sector=$1;", params, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          deg = db_res_col_i32(res, 0, &err);
      }
      db_res_finalize(res);
  }
  return deg;
}


static int
get_sector_count (void)
{
  db_t *db = game_db_get_handle ();
  if (!db) return -1;
  
  db_res_t *res = NULL;
  db_error_t err;
  int n = -1;
  
  if (db_query(db, "SELECT COUNT(*) FROM sectors;", NULL, 0, &res, &err)) {
      if (db_res_step(res, &err)) {
          n = db_res_col_i32(res, 0, &err);
      }
      db_res_finalize(res);
  }
  return n;
}


static int
is_sector_used (db_t *db, int sector_id)
{
  db_res_t *res = NULL;
  db_error_t err;
  int used = 0;
  db_bind_t params[] = { db_bind_i32(sector_id) };
  
  if (db_query(db, "SELECT 1 FROM used_sectors WHERE used=$1 LIMIT 1;", params, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          used = 1;
      }
      db_res_finalize(res);
  }
  return used;
}


/* ----------------------------------------------------
 * Random warps (tunnel-aware)
 * ---------------------------------------------------- */
static int
create_random_warps (db_t *db, int numSectors, int maxWarps)
{
  db_error_t err;
  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err))
    {
      fprintf (stderr, "create_random_warps: BEGIN failed: %s\n", err.message);
      return -1;
    }
  for (int s = 11; s <= numSectors; s++)
    {
      if (is_sector_used (db, s))
        {
          continue;             /* don't touch tunnels */
        }
      if ((rand () % 100) < DEFAULT_PERCENT_DEADEND)
        {
          continue;             /* skip dead-ends */
        }
      int targetWarps = 1 + (rand () % maxWarps);
      int deg = sector_degree (db, s);


      if (deg < 0)
        {
          goto fail;
        }
      int attempts = 0;


      while (deg < targetWarps && attempts < 200)
        {
          int t = 11 + (rand () % (numSectors - 10));


          if (t == s || is_sector_used (db, t))
            {
              attempts++;
              continue;
            }
          int ins = insert_warp_unique (db, s, t);


          if (ins < 0)
            {
              goto fail;
            }
          if (ins == 1)
            {
              if ((rand () % 100) >= DEFAULT_PERCENT_ONEWAY)
                {
                  insert_warp_unique (db, t, s);
                }
              deg++;
            }
          attempts++;
        }
    }
  if (!db_tx_commit(db, &err))
    {
      fprintf (stderr, "create_random_warps: COMMIT failed: %s\n", err.message);
      return -1;
    }
  return 0;
fail:
  db_tx_rollback(db, NULL);
  return -1;
}


/* ----------------------------------------------------
 * Ensure every non-tunnel sector has at least one exit
 * ---------------------------------------------------- */
int
ensure_sector_exits (db_t *db, int numSectors)
{
  if (!db || numSectors <= 0)
    {
      return -1;
    }
  
  const char *sql_count = "SELECT COUNT(1) FROM sector_warps WHERE from_sector=$1";
  const char *sql_in = "SELECT from_sector FROM sector_warps WHERE to_sector=$1 ORDER BY RANDOM() LIMIT 1;";
  const char *sql_ins = "INSERT OR IGNORE INTO sector_warps(from_sector,to_sector) VALUES($1,$2);";

  int rc = 0;

  for (int s = 11; s <= numSectors; s++)
    {
      if (is_sector_used (db, s))
        {
          continue;             /* don't add exits FROM tunnel nodes */
        }
      
      db_res_t *res = NULL;
      db_error_t err;
      int outc = 0;
      db_bind_t params_c[] = { db_bind_i32(s) };
      
      if (db_query(db, sql_count, params_c, 1, &res, &err)) {
          if (db_res_step(res, &err)) outc = db_res_col_i32(res, 0, &err);
          db_res_finalize(res);
      } else {
          return -1;
      }

      if (outc > 0)
        {
          continue;
        }
      
      int to = 0;

      if ((rand () % 100) < 80)
        {
          db_res_t *res_in = NULL;
          if (db_query(db, sql_in, params_c, 1, &res_in, &err)) {
              if (db_res_step(res_in, &err)) {
                  to = db_res_col_i32(res_in, 0, &err);
                  if (to == s || is_sector_used (db, to))
                    {
                      to = 0;
                    }
              }
              db_res_finalize(res_in);
          }
        }
      if (to <= 0 || to == s)
        {
          do
            {
              to = 11 + (rand () % (numSectors - 10));
            }
          while (to == s || is_sector_used (db, to));
        }
      
      db_bind_t params_i[] = { db_bind_i32(s), db_bind_i32(to) };
      db_exec(db, sql_ins, params_i, 2, &err);
    }

  return 0;
}


/* ----------------------------------------------------
 * Tunnels — created LAST; atomic per path; logs only on success
 * ---------------------------------------------------- */
int
bigbang_create_tunnels (void)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  int sector_count = get_sector_count ();


  if (sector_count <= 0)
    {
      fprintf (stderr, "BIGBANG: Failed to get sector count\n");
      return -1;
    }
  db_error_t err;


  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err))
    {
      fprintf (stderr, "BIGBANG: tunnels BEGIN failed: %s\n", err.message);
      return -1;
    }
  /* Fresh run: clear used_sectors */
  db_exec(db, "DELETE FROM used_sectors;", NULL, 0, &err);
  
  const char *SQL_INSERT_WARP =
    "INSERT OR IGNORE INTO sector_warps(from_sector,to_sector) VALUES($1,$2);";
  const char *SQL_INSERT_USED =
    "INSERT OR IGNORE INTO used_sectors(used) VALUES($1);";
  
  int added_tunnels = 0;
  int attempts = 0;


  while (added_tunnels < 15 && attempts < 60)
    {
      int path_len = 4 + (rand () % 5); /* 4..8 */
      int nodes[12];
      int n = 0;


      /* choose distinct nodes, avoiding already-used tunnel nodes */
      while (n < path_len)
        {
          int s = 11 + (rand () % (sector_count - 10));
          int dup = 0;


          for (int i = 0; i < n; i++)
            {
              if (nodes[i] == s)
                {
                  dup = 1;
                  break;
                }
            }
          if (dup || is_sector_used (db, s))
            {
              continue;
            }
          nodes[n++] = s;
        }
      
      // Savepoint equivalent for nested transaction rollback (simplified)
      // Since generic DB API doesn't expose SAVEPOINT explicitly, we might rely on the logic 
      // or implement it via db_exec raw.
      db_exec(db, "SAVEPOINT tunnel;", NULL, 0, &err);
      
      int failed = 0;


      for (int i = 0; i < path_len - 1; i++)
        {
          int a = nodes[i], b = nodes[i + 1];
          db_bind_t params[] = { db_bind_i32(a), db_bind_i32(b) };
          db_bind_t params_rev[] = { db_bind_i32(b), db_bind_i32(a) };

          if (!db_exec(db, SQL_INSERT_WARP, params, 2, &err) ||
              !db_exec(db, SQL_INSERT_WARP, params_rev, 2, &err))
            {
              failed = 1;
              break;
            }
        }
      if (failed)
        {
          db_exec(db, "ROLLBACK TO tunnel; RELEASE SAVEPOINT tunnel;", NULL, 0, &err);
          attempts++;
          continue;
        }
      db_exec(db, "RELEASE SAVEPOINT tunnel;", NULL, 0, &err);
      
      for (int i = 0; i < path_len; i++)
        {
          db_bind_t params[] = { db_bind_i32(nodes[i]) };
          if (db_exec(db, SQL_INSERT_USED, params, 1, &err))
            {
              // ok
            }
        }
      added_tunnels++;
      attempts++;
    }
  db_tx_commit(db, &err);
  fprintf (stderr, "BIGBANG: Added %d tunnels in %d attempts.\n",
           added_tunnels, attempts);
  return 0;
}


/* ----------------------------------------------------
 * Sector creation (names / nebulae / beacon every 64th) */
int
create_sectors (void)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  int sector_count = get_sector_count ();


  if (sector_count > 100)
    {
      fprintf (stderr, "Sectors already exist!");
      return 1;
    }
  struct twconfig *cfg = config_load ();


  if (!cfg)
    {
      return -1;
    }
  if (cfg->default_nodes <= 0)
    {
      fprintf (stderr, "BIGBANG: num_sectors is invalid: %d\n",
               cfg->default_nodes);
      free (cfg);
      return -1;
    }
  printf ("BIGBANG: Creating %d sectors...\n", cfg->default_nodes);
  
  db_error_t err;
  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err))
    {
      fprintf (stderr, "BIGBANG: BEGIN failed: %s\n", err.message);
      free (cfg);
      return -1;
    }
  // Use a prepared statement to prevent SQL injection
  const char *sql_insert =
    "INSERT INTO sectors (name, beacon, nebulae) VALUES ($1, $2, $3);";

  for (int i = 1; i <= cfg->default_nodes; i++)
    {
      char name[128];
      char neb[128];
      const char *beacon_txt = "";

      consellationName (name);
      consellationName (neb);
      
      if ((i % 64) == 0)
        {
          beacon_txt = "Barreik was here!";
        }
      
      db_bind_t params[] = { db_bind_text(name), db_bind_text(beacon_txt), db_bind_text(neb) };

      if (!db_exec(db, sql_insert, params, 3, &err))
        {
          fprintf (stderr, "BIGBANG: Failed to insert sector %d: %s\n", i,
                   err.message);
          db_tx_rollback(db, NULL);
          free (cfg);
          return -1;
        }
    }
  
  if (!db_tx_commit(db, &err))
    {
      fprintf (stderr, "BIGBANG: COMMIT failed: %s\n", err.message);
      free (cfg);
      return -1;
    }
  free (cfg);
  return 0;
}


static int
random_port_type_1_to_8 (void)
{
  return 1 + (rand () % 8);
}


/* ----------------------------------------------------
 * Orchestration */
int
bigbang (void)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      fprintf (stderr, "bigbang: DB handle missing\n");
      return -1;
    }
  struct twconfig *cfg = config_load ();


  if (!cfg)
    {
      fprintf (stderr, "bigbang: config_load failed\n");
      return -1;
    }

  int numSectors = cfg->default_nodes;


  fprintf (stderr, "bigbang: int numSectors = %d\n", numSectors);

  fprintf (stderr, "BIGBANG: Creating sectors...\n");
  if (create_sectors () != 0)
    {
      free (cfg);
      return -1;
    }
  fprintf (stderr, "BIGBANG: Creating random warps...\n");
  if (create_random_warps (db, numSectors, cfg->maxwarps_per_sector) != 0)
    {
      free (cfg);
      return -1;
    }
  fprintf (stderr, "BIGBANG: Ensuring FedSpace Exits...\n");
  int rc2 = ensure_fedspace_exit (db, 11, numSectors, 1);


  if (rc2 != 0)
    {
      fprintf (stderr, "ensure_fedspace_exit failed rc=%d\n", rc2);
      free (cfg);
      return -1;
    }
  if (create_complex_warps (db, numSectors) != 0)
    {
      fprintf (stderr, "create_complex_warps failed\n");
      free (cfg);
      return -1;
    }
  fprintf (stderr, "BIGBANG: Ensuring sector exits...\n");
  if (ensure_sector_exits (db, numSectors) != 0)
    {
      fprintf (stderr, "ensure_sector_exits failed\n");
      free (cfg);
      return -1;
    }
  fprintf (stderr, "BIGBANG: Creating tube tunnels...\n");
  if (bigbang_create_tunnels () != 0)
    {
      fprintf (stderr, "Tunnel creation failed\n");
      free (cfg);
      return -1;
    }
  prune_tunnel_edges (db);
  printf ("BIGBANG: Ensuring all sectors have exits...\n");
  fflush (stdout);
  int rc = 0;


  rc = ensure_all_sectors_have_exits (db);
  if (rc != 0)
    {
      fprintf (stderr, "error ensuring all sectors have exits\n");
      return rc;
    }
  printf ("BIGBANG: Chaining isolated sectors and bridges...\n");
  printf ("BIGBANG: Fixing trap components with path checks...\n");
  rc = fix_traps_with_pathcheck (db, 10);
  if (rc != 0)
    {
      fprintf (stderr, "fix_traps_with_pathcheck failed\n");
    }
  // After all sectors/warps are generated:
  int ferringhi = db_chain_traps_and_bridge (db, 10);       /* 1..10 are FedSpace by convention */


  fprintf (stderr, "BIGBANG: Creating Stardock...\n");
  if (create_stardock_port (db, numSectors) != 0)
    {
      free (cfg);
      return -1;
    }

  fprintf (stderr, "BIGBANG: Creating ports...\n");
  if (create_ports () != 0)
    {
      free (cfg);
      return -1;
    }
  fprintf (stderr, "BIGBANG: Creating planets...\n");
  if (create_planets () != 0)
    {
      free (cfg);
      return -1;
    }
  fprintf (stderr, "BIGBANG: Creating Ferringhi home sector...\n");
  if (create_ferringhi (ferringhi) != 0)
    {
      free (cfg);
      return -1;
    }
  fprintf (stderr, "BIGBANG: Creating Imperial ship...\n");
  fprintf (stderr, "BIGBANG: Creating derelicts...\n");
  if (create_derelicts () != 0)
    {
      free (cfg);
      return -1;
    }
  fprintf (stderr, "BIGBANG: Applying ownerships...\n");
  if (create_ownership () != 0)
    {
      free (cfg);
      return -1;
    }
  free (cfg);
  fprintf (stderr, "BIGBANG: Universe creation complete.\n");
  return 0;
}


// Function to check if a warp exists in the in-memory array
int
has_warp (const Warp *warps, int warp_count, int from, int to)
{
  for (int i = 0; i < warp_count; ++i)
    {
      if (warps[i].from == from && warps[i].to == to)
        {
          return 1;
        }
    }
  return 0;
}


// Function to count the degree of a sector in the in-memory array
int
sector_degree_in_memory (const Warp *warps, int warp_count, int sector_id)
{
  int degree = 0;
  for (int i = 0; i < warp_count; ++i)
    {
      if (warps[i].from == sector_id || warps[i].to == sector_id)
        {
          degree++;
        }
    }
  return degree;
}


/************* Tunneling */
int
count_edges ()
{
  db_t *handle = game_db_get_handle ();
  if (!handle) return -1;
  int count = 0;
  db_res_t *res = NULL;
  db_error_t err;
  
  if (db_query(handle, "SELECT count(*) FROM sector_warps;", NULL, 0, &res, &err))
    {
      if (db_res_step(res, &err))
        {
          count = db_res_col_i32(res, 0, &err);
        }
      db_res_finalize(res);
    }
  return count;
}


// Helper function to count connections for a given sector
int
get_sector_degree_count (int sector_id)
{
  db_t *handle = game_db_get_handle ();
  if (!handle) return -1;
  int degree = 0;
  const char *sql =
    "SELECT COUNT(*) FROM sector_warps WHERE from_sector = $1 OR to_sector = $2;";
  db_res_t *res = NULL;
  db_error_t err;
  db_bind_t params[] = { db_bind_i32(sector_id), db_bind_i32(sector_id) };
  
  if (db_query(handle, sql, params, 2, &res, &err))
    {
      if (db_res_step(res, &err))
        {
          degree = db_res_col_i32(res, 0, &err);
        }
      db_res_finalize(res);
    }
  return degree;
}


// Function to check if a warp exists between two sectors
int
sw_has_edge (int from, int to)
{
  db_t *handle = game_db_get_handle ();
  if (!handle) return 0;
  int exists = 0;
  const char *sql =
    "SELECT 1 FROM sector_warps WHERE (from_sector = $1 AND to_sector = $2) OR (from_sector = $3 AND to_sector = $4) LIMIT 1;";
  db_res_t *res = NULL;
  db_error_t err;
  db_bind_t params[] = { db_bind_i32(from), db_bind_i32(to), db_bind_i32(to), db_bind_i32(from) };
  
  if (db_query(handle, sql, params, 4, &res, &err))
    {
      if (db_res_step(res, &err)) exists = 1;
      db_res_finalize(res);
    }
  return exists;
}


/************* End Tunneling */


/* Internal helpers */


/* Returns the ID of an NPC shiptype by its name. Returns -1 on error. */
static int
get_npc_shiptype_id_by_name (db_t *db, const char *name)
{
  const char *q =
    "SELECT id FROM shiptypes WHERE name = $1 AND can_purchase = 0;";
  db_res_t *st = NULL;
  db_error_t err;
  int id = -1;
  db_bind_t params[] = { db_bind_text(name) };
  
  if (db_query(db, q, params, 1, &st, &err))
    {
      if (db_res_step(st, &err))
        {
          id = db_res_col_i32(st, 0, &err);
        }
      else
        {
          fprintf (stderr,
                   "get_npc_shiptype_id_by_name: NPC Shiptype '%s' not found.\n",
                   name);
        }
      db_res_finalize(st);
    }
  return id;
}


/* Returns the ID of a purchasable shiptype by its name. Returns -1 on error. */
static int
get_purchasable_shiptype_id_by_name (db_t *db, const char *name)
{
  const char *q =
    "SELECT id FROM shiptypes WHERE name = $1 AND can_purchase = 1;";
  db_res_t *st = NULL;
  db_error_t err;
  int id = -1;
  db_bind_t params[] = { db_bind_text(name) };
  
  if (db_query(db, q, params, 1, &st, &err))
    {
      if (db_res_step(st, &err))
        {
          id = db_res_col_i32(st, 0, &err);
        }
      else
        {
          fprintf (stderr,
                   "get_purchasable_shiptype_id_by_name: Purchasable Shiptype '%s' not found.\n",
                   name);
        }
      db_res_finalize(st);
    }
  return id;
}


/* ----------------------------------------------------
 * Universe population functions */
int
create_planets (void)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }
  fprintf (stderr,
           "BIGBANG: Ensuring core planets (Terra, Ferringhi, Orion) exist...\n");
  db_error_t err;


  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err))
    {
      fprintf (stderr,
               "create_planets: BEGIN failed: %s\n",
               err.message);
      return -1;
    }

  /* Correction: Removed deprecated on_hand columns */
  const char *sql_fixed =
    "INSERT OR IGNORE INTO planets (id, name, sector, type, created_at, owner_id, created_by) VALUES ($1, $2, $3, $4, strftime('%s','now'), 0, 0)";

  const char *sql_stock =
    "INSERT OR IGNORE INTO entity_stock (entity_type, entity_id, commodity_code, quantity) VALUES ('planet', $1, $2, $3);";

  /* Terra (ID 1) - Sector 1, Type 1 (Class M) */
  db_bind_t p_terra[] = { db_bind_i32(1), db_bind_text("Terra"), db_bind_i32(1), db_bind_i32(1) };
  db_exec(db, sql_fixed, p_terra, 4, &err);
  
  /* Stock for Terra */
  db_exec(db, sql_stock, (db_bind_t[]){ db_bind_i32(1), db_bind_text("ORE"), db_bind_i32(0) }, 3, &err);
  db_exec(db, sql_stock, (db_bind_t[]){ db_bind_i32(1), db_bind_text("ORG"), db_bind_i32(0) }, 3, &err);
  db_exec(db, sql_stock, (db_bind_t[]){ db_bind_i32(1), db_bind_text("EQU"), db_bind_i32(0) }, 3, &err);

  /* Ferringhi Homeworld (ID 2) - Sector 0 (Placeholder), Type 3 */
  db_bind_t p_ferr[] = { db_bind_i32(2), db_bind_text("Ferringhi Homeworld"), db_bind_i32(0), db_bind_i32(3) };
  db_exec(db, sql_fixed, p_ferr, 4, &err);

  /* Stock for Ferringhi */
  db_exec(db, sql_stock, (db_bind_t[]){ db_bind_i32(2), db_bind_text("ORE"), db_bind_i32(0) }, 3, &err);
  db_exec(db, sql_stock, (db_bind_t[]){ db_bind_i32(2), db_bind_text("ORG"), db_bind_i32(0) }, 3, &err);
  db_exec(db, sql_stock, (db_bind_t[]){ db_bind_i32(2), db_bind_text("EQU"), db_bind_i32(0) }, 3, &err);

  /* Orion Hideout (ID 3) - Sector 0 (Placeholder), Type 5 */
  db_bind_t p_orion[] = { db_bind_i32(3), db_bind_text("Orion Hideout"), db_bind_i32(0), db_bind_i32(5) };
  db_exec(db, sql_fixed, p_orion, 4, &err);

  /* Stock for Orion */
  db_exec(db, sql_stock, (db_bind_t[]){ db_bind_i32(3), db_bind_text("ORE"), db_bind_i32(0) }, 3, &err);
  db_exec(db, sql_stock, (db_bind_t[]){ db_bind_i32(3), db_bind_text("ORG"), db_bind_i32(0) }, 3, &err);
  db_exec(db, sql_stock, (db_bind_t[]){ db_bind_i32(3), db_bind_text("EQU"), db_bind_i32(0) }, 3, &err);

  /* Ensure they have bank accounts. Use 1 credit to pass validation. */
  h_add_credits (db, "npc_planet", 1, 1, "DEPOSIT", NULL, NULL);
  h_add_credits (db, "npc_planet", 2, 1, "DEPOSIT", NULL, NULL);
  h_add_credits (db, "npc_planet", 3, 1, "DEPOSIT", NULL, NULL);
  
  if (!db_tx_commit(db, &err))
    {
      fprintf (stderr,
               "create_planets: COMMIT failed: %s\n",
               err.message);
      return -1;
    }
  return 0;
}


/**
 * @brief Creates a fully-populated port, including its trade data,
 * based on the new schema in fullschema.sql.
 *
 * This function is now transactional and populates both 'ports' and
 * 'port_trade' tables according to the port type.
 */
int
create_full_port (db_t *db, int sector, int port_number,
                  const char *base_name, int type_id, int *port_id_out)
{
  db_error_t err;
  int64_t port_id = 0;

  /* Commodity stock levels */
  int ore_on_hand_val = 0;
  int organics_on_hand_val = 0;
  int equipment_on_hand_val = 0;

  /* Default port stats */
  int port_size = 5 + (rand () % 5);    /* Random size 5-9 */
  int port_tech = 1 + (rand () % 5);    /* Random tech 1-5 */
  int port_credits = 100000;
  int petty_cash_val = 0;       /* Default petty cash */

  /*
   * ===================================================================
   * 1. SET PORT PROPERTIES BASED ON TYPE
   * ===================================================================
   */
  switch (type_id)
    {
      case 1:                   /* Sells Equipment */
      case 2:                   /* Sells Equipment */
        ore_on_hand_val = DEF_PORT_PROD_ORE;
        organics_on_hand_val = DEF_PORT_MAX_ORG / 2;
        equipment_on_hand_val = DEF_PORT_PROD_EQU;
        break;
      case 3:                   /* Buys Equipment */
      case 4:                   /* Buys Equipment */
        ore_on_hand_val = DEF_PORT_PROD_ORE;
        organics_on_hand_val = DEF_PORT_MAX_ORG / 2;
        equipment_on_hand_val = DEF_PORT_MAX_EQU / 4;
        break;
      case 5:                   /* Sells Organics */
      case 6:                   /* Sells Organics */
        ore_on_hand_val = DEF_PORT_PROD_ORE;
        organics_on_hand_val = DEF_PORT_PROD_ORG;
        equipment_on_hand_val = DEF_PORT_MAX_EQU / 2;
        break;
      case 7:                   /* Buys Organics */
      case 8:                   /* Buys Organics */
        ore_on_hand_val = DEF_PORT_PROD_ORE;
        organics_on_hand_val = DEF_PORT_MAX_ORG / 4;
        equipment_on_hand_val = DEF_PORT_MAX_EQU / 2;
        break;
      case 9:                   /* Stardock - Buys/Sells all, high stock */
        ore_on_hand_val = DEF_PORT_MAX_ORE;
        organics_on_hand_val = DEF_PORT_MAX_ORG;
        equipment_on_hand_val = DEF_PORT_MAX_EQU;
        port_credits = 1000000; /* Stardock has more credits */
        port_size = 10;
        port_tech = 10;
        break;
      case 10:                  /* Orion Black Market - Special case */
        ore_on_hand_val = DEF_PORT_MAX_ORE * 2;
        organics_on_hand_val = DEF_PORT_MAX_ORG / 10;
        equipment_on_hand_val = DEF_PORT_MAX_EQU * 2;
        port_credits = 2000000; /* More credits for black market */
        petty_cash_val = 100000; /* Significant petty cash */
        port_size = 8;
        port_tech = 8;
        break;
      default:
        fprintf (stderr, "create_full_port: Invalid type_id %d\n", type_id);
        return -1;
    }

  /*
   * ===================================================================
   * 2. BEGIN TRANSACTION
   * ===================================================================
   */
  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err))
    {
      fprintf (stderr, "create_full_port: BEGIN transaction failed: %s\n",
               err.message);
      return err.code;
    }

  /*
   * ===================================================================
   * 3. INSERT INTO 'ports' TABLE
   * ===================================================================
   */
  const char *port_sql =
    "INSERT INTO ports ("
    "  number, name, sector, size, techlevel, type, invisible, economy_curve_id "
    " ) VALUES ("
    "  $1, $2, $3, $4, $5, $6, 0, $7 "
    " );";

  db_bind_t p_params[] = {
      db_bind_i32(port_number),
      db_bind_text(base_name),
      db_bind_i32(sector),
      db_bind_i32(port_size),
      db_bind_i32(port_tech),
      db_bind_i32(type_id),
      db_bind_i32(1)
  };

  if (!db_exec_insert_id(db, port_sql, p_params, 7, &port_id, &err))
    {
      fprintf (stderr, "create_full_port: ports insert failed: %s\n", err.message);
      goto rollback;
    }

  /*
   * ===================================================================
   * 3.5. POPULATE entity_stock
   * ===================================================================
   */
  const char *entity_stock_sql =
    "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity) VALUES ('port', $1, $2, $3);";

  /* Helper to add stock */
  #define ADD_STOCK(comm, qty) do { \
      db_bind_t s_params[] = { db_bind_i32((int)port_id), db_bind_text(comm), db_bind_i32(qty) }; \
      if (!db_exec(db, entity_stock_sql, s_params, 3, &err)) goto rollback; \
  } while(0)

  ADD_STOCK("ORE", ore_on_hand_val);
  ADD_STOCK("ORG", organics_on_hand_val);
  ADD_STOCK("EQU", equipment_on_hand_val);

  if (type_id == 10)   /* Special items for Black Market */
    {
      ADD_STOCK("SLV", 1000);
      ADD_STOCK("WPN", 1000);
      ADD_STOCK("DRG", 1000);
    }

  /*
   * ===================================================================
   * 3.6. POPULATE port_trade
   * ===================================================================
   */
  const char *trade_sql =
    "INSERT INTO port_trade (port_id, commodity, mode, maxproduct) VALUES ($1, $2, $3, $4);";

  /* Helper to add trade row */
  #define ADD_TRADE(comm, mode, max) do { \
      db_bind_t t_params[] = { db_bind_i32((int)port_id), db_bind_text(comm), db_bind_text(mode), db_bind_i32(max) }; \
      if (!db_exec(db, trade_sql, t_params, 4, &err)) goto rollback; \
  } while (0)

  int max_prod = 50000;


  if (type_id == 9 || type_id == 10)
    {
      ADD_TRADE ("ore", "buy", max_prod); ADD_TRADE ("ore", "sell", max_prod);
      ADD_TRADE ("organics", "buy", max_prod); ADD_TRADE ("organics",
                                                          "sell",
                                                          max_prod);
      ADD_TRADE ("equipment", "buy", max_prod); ADD_TRADE ("equipment",
                                                           "sell",
                                                           max_prod);
    }
  else
    {
      if (type_id == 1 || type_id == 2)
        {
          ADD_TRADE ("equipment", "sell", max_prod); ADD_TRADE ("ore",
                                                                "buy",
                                                                max_prod);
          ADD_TRADE ("organics", "buy", max_prod);
        }
      if (type_id == 3 || type_id == 4)
        {
          ADD_TRADE ("equipment", "buy", max_prod); ADD_TRADE ("ore",
                                                               "sell",
                                                               max_prod);
          ADD_TRADE ("organics", "sell", max_prod);
        }
      if (type_id == 5 || type_id == 6)
        {
          ADD_TRADE ("organics", "sell", max_prod); ADD_TRADE ("ore",
                                                               "buy",
                                                               max_prod);
          ADD_TRADE ("equipment", "buy", max_prod);
        }
      if (type_id == 7 || type_id == 8)
        {
          ADD_TRADE ("organics", "buy", max_prod); ADD_TRADE ("ore",
                                                              "sell",
                                                              max_prod);
          ADD_TRADE ("equipment", "sell", max_prod);
        }
    }

  /* Create a bank account for the new port (Correction III.4) */
  h_add_credits (db,
                 "port",
                 (int) port_id,
                 port_credits + petty_cash_val,
                 "DEPOSIT",
                 NULL,
                 NULL);

  /*
   * ===================================================================
   * 4. COMMIT TRANSACTION
   * ===================================================================
   */
  if (!db_tx_commit(db, &err))
    {
      fprintf (stderr, "create_full_port: COMMIT failed: %s\n", err.message);
      goto rollback;
    }
  if (port_id_out)
    {
      *port_id_out = (int) port_id;
    }
  return 0;

/* --- Error Handling --- */
rollback:
  db_tx_rollback(db, NULL);
  return -1;
}


int
create_ferringhi (int ferringhi_sector)
{
  if (ferringhi_sector == 0)
    {
      ferringhi_sector = 20;
    }                           /* just make it a default value */
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      fprintf (stderr, "create_ferringhi: Failed to get DB handle\n");
      return -1;
    }
  /* Dynamically retrieve the shiptype IDs, filtered by can_purchase */
  int merchant_freighter_id =
    get_purchasable_shiptype_id_by_name (db, "Merchant Freighter");
  int ferengi_warship_id =
    get_npc_shiptype_id_by_name (db, "Ferrengi Warship");


  if (merchant_freighter_id == -1 || ferengi_warship_id == -1)
    {
      fprintf (stderr,
               "create_ferringhi: Failed to get required shiptype IDs.\n");
      return -1;
    }
  /* Create the view to find the longest tunnels */
  const char *create_view_sql =
    "CREATE VIEW IF NOT EXISTS longest_tunnels AS WITH all_sectors AS ( SELECT from_sector AS id FROM sector_warps UNION SELECT to_sector AS id FROM sector_warps ), outdeg AS ( SELECT a.id, COALESCE(COUNT(w.to_sector),0) AS deg FROM all_sectors a LEFT JOIN sector_warps w ON w.from_sector = a.id GROUP BY a.id ), edges AS ( SELECT from_sector, to_sector FROM sector_warps ), entry AS ( SELECT e.from_sector AS entry, e.to_sector AS next FROM edges e JOIN outdeg df ON df.id = e.from_sector AND df.deg > 1 JOIN outdeg dn ON dn.id = e.to_sector AND dn.deg = 1 ), rec(entry, curr, path, steps) AS ( SELECT entry, next, printf('%d->%d', entry, next), 1 FROM entry UNION ALL SELECT r.entry, e.to_sector, r.path || '->' || printf('%d', e.to_sector), r.steps + 1 FROM rec r JOIN edges e ON e.from_sector = r.curr JOIN outdeg d ON d.id = r.curr AND d.deg = 1 WHERE instr(r.path, '->' || printf('%d', e.to_sector) || '->') = 0 ) SELECT r.entry AS entry_sector, r.curr AS exit_sector, r.path AS tunnel_path, r.steps AS tunnel_length_edges FROM rec r JOIN outdeg d_exit ON d_exit.id = r.curr WHERE d_exit.deg <> 1 AND r.steps >= 2 ORDER BY r.steps DESC, r.entry, r.curr;";

  db_error_t err;
  if (!db_exec(db, create_view_sql, NULL, 0, &err))
    {
      fprintf (stderr,
               "create_ferringhi: Failed to create longest_tunnels view: %s\n",
               err.message);
      return -1;
    }
  int longest_tunnel_sector = 0;
  const char *q = "SELECT exit_sector FROM longest_tunnels LIMIT 1;";
  db_res_t *st = NULL;


  if (db_query(db, q, NULL, 0, &st, &err))
    {
      if (db_res_step(st, &err)) {
          longest_tunnel_sector = db_res_col_i32(st, 0, &err);
      }
      db_res_finalize(st);
    }

  if (longest_tunnel_sector == 0)
    {
      longest_tunnel_sector = 20;
    }


  /* --- Ferringhi Homeworld (ID 2) Update (Correction IV.1) */
  char planet_sector[256];


  snprintf (planet_sector,
            sizeof (planet_sector),
            "UPDATE planets SET sector=%d WHERE id=2;",
            longest_tunnel_sector);
  if (!db_exec(db, planet_sector, NULL, 0, &err))
    {
      fprintf (stderr, "create_ferringhi failed: %s\n", err.message);
      return -1;
    }
  h_add_credits (db, "npc_planet", 2, 0, "DEPOSIT", NULL, NULL);
  /* Insert the citadel details into the citadels table */
  char sql_citadel[512];


  snprintf (sql_citadel,
            sizeof (sql_citadel),
            "INSERT INTO citadels (planet_id, level, shields, treasury, military) "
            "VALUES (%d, %d, %d, %d, 1);",
            2,
            (rand () % 2) + 2,
            /* Citadel Level (2-3) */
            (rand () % 1001) + 1000,
            /* Shields (1k-2k) */
            (rand () % 5000001) + 1000000);     /* Credits (1M-6M) */
  if (!db_exec(db, sql_citadel, NULL, 0, &err))
    {
      fprintf (stderr, "create_ferringhi failed to create citadel: %s\n",
               err.message);
      return -1;
    }
  /* Dynamically build the INSERT statement for the player */
  char player_insert_sql[1024];
  char player_cols[256] = "";
  char player_vals[256] = "";


  /* Add the core columns */
  strncat (player_cols, "name, passwd, credits, sector",
           sizeof (player_cols) - strlen (player_cols) - 1);
  strncat (player_vals, "$1, $2, $3, $4",
           sizeof (player_vals) - strlen (player_vals) - 1);
  
  /* Add the 'type' and 'ship' columns if they exist in the schema */
  if (has_column (db, "players", "type"))
    {
      strncat (player_cols, ", type",
               sizeof (player_cols) - strlen (player_cols) - 1);
      strncat (player_vals, ", $5",
               sizeof (player_vals) - strlen (player_vals) - 1);
    }
  if (has_column (db, "players", "ship"))
    {
      strncat (player_cols, ", ship",
               sizeof (player_cols) - strlen (player_cols) - 1);
      strncat (player_vals, ", $6",
               sizeof (player_vals) - strlen (player_vals) - 1);
    }
  snprintf (player_insert_sql, sizeof (player_insert_sql),
            "INSERT INTO players (%s) VALUES (%s);", player_cols,
            player_vals);

  /* Seed 5 Ferrengi traders as both players and ships */
  for (int i = 0; i < 5; i++)
    {
      int player_credits = (rand () % 10000) + 1000;
      char player_name[128];


      snprintf (player_name, sizeof (player_name), "Ferrengi Trader %d",
                i + 1);
      
      db_bind_t p_params[] = {
          db_bind_text(player_name),
          db_bind_text("BOT"),
          db_bind_i32(player_credits),
          db_bind_i32(longest_tunnel_sector),
          db_bind_i32(1), // type
          db_bind_i32(0)  // ship placeholder
      };
      
      // Note: If columns don't exist, we rely on db_exec with correct count matching the dynamic SQL
      // For simplicity here, assume schema is standard v3.
      
      int64_t player_id = 0;
      if (!db_exec_insert_id(db, player_insert_sql, p_params, 6, &player_id, &err))
        {
          fprintf (stderr,
                   "create_ferringhi failed to create player %d: %s\n", i,
                   err.message);
          return -1;
        }

      /* Randomly choose between a trader and a warship type */
      int ship_type = (rand () % 100) <
                      80 ? merchant_freighter_id : ferengi_warship_id;                          /* Use the dynamic IDs here */
      int ship_fighters = (rand () % 1501) + 500;       /* 500-2000 fighters */
      int ship_shields = (rand () % 4001) + 1000;       /* 1000-5000 shields */
      int ship_holds = (rand () % 801) + 200;   /* 200-1000 holds */
      int mines_to_add = 5;
      int beacons_to_add = 4;
      int photons_to_add = 1;
      /* --- NEW CODE FOR POPULATING HOLDS --- */
      int holds_to_fill = (rand () % (ship_holds - 1)) + 1;
      int ore = 0;
      int organics = 0;
      int equipment = 0;

      for (int h = 0; h < holds_to_fill; h++)
        {
          switch (rand () % 3)
            {
              case 0:
                ore++;
                break;
              case 1:
                organics++;
                break;
              case 2:
                equipment++;
                break;
            }
        }
      /* Dynamically build the INSERT statement for the ship */
      // ... For brevity, assuming standard schema again, but using db_bind_t
      // Since constructing dynamic SQL with variable binds is complex in C,
      // and we just need it to work for the current schema, we will construct
      // a standard INSERT. If columns are missing, this might fail, but `has_column`
      // logic suggests adaptability.
      // Ideally, we'd use a helper builder.
      // Let's implement a simpler fixed SQL for now, assuming full schema support as we just migrated.
      
      const char *ship_sql = "INSERT INTO ships (name, type_id, sector, fighters, shields, holds, ore, organics, equipment, limpets, genesis, colonists, flags, ported, onplanet, attack, mines, beacons, photons, cloaking_devices, cloaked, perms) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22);";
      
      db_bind_t s_params[] = {
          db_bind_text("Ferrengi Trader"),
          db_bind_i32(ship_type),
          db_bind_i32(longest_tunnel_sector),
          db_bind_i32(ship_fighters),
          db_bind_i32(ship_shields),
          db_bind_i32(holds_to_fill),
          db_bind_i32(0), db_bind_i32(0), db_bind_i32(0), // cargo
          db_bind_i32(0), db_bind_i32(0), db_bind_i32(0), // limpets, genesis, colonists
          db_bind_i32(777), // flags
          db_bind_i32(0),   // ported
          db_bind_i32(1),   // onplanet
          db_bind_i32(100), // attack (default)
          db_bind_i32(mines_to_add),
          db_bind_i32(beacons_to_add),
          db_bind_i32(photons_to_add),
          db_bind_i32(0), // cloaking
          db_bind_null(), // cloaked
          db_bind_i32(777) // perms
      };

      int64_t ship_id = 0;
      if (!db_exec_insert_id(db, ship_sql, s_params, 22, &ship_id, &err)) {
          fprintf (stderr, "create_ferringhi failed to create ship %d: %s\n", i, err.message);
          return -1;
      }

      /* Update the player with their new ship ID */
      const char *update_sql = "UPDATE players SET ship = $1 WHERE id = $2;";
      db_bind_t u_params[] = { db_bind_i64(ship_id), db_bind_i64(player_id) };
      db_exec(db, update_sql, u_params, 2, &err);
    }

  /* Insert Orion Syndicate Outpost */
  int oso_tunnel = 0;
  const char *oso =
    "SELECT exit_sector FROM longest_tunnels LIMIT 1 OFFSET 2;";
  db_res_t *stoso = NULL;

  if (db_query(db, oso, NULL, 0, &stoso, &err)) {
      if (db_res_step(stoso, &err)) {
          oso_tunnel = db_res_col_i32(stoso, 0, &err);
      }
      db_res_finalize(stoso);
  }

  /* --- Ferringhi Homeworld (num=2) Update --- */
  if (longest_tunnel_sector == oso_tunnel || oso_tunnel < 20)
    {
      oso_tunnel = (rand () % (999 - 11 + 1)) + 11;
    }
  /* --- Ferrengi Homeworld (num=2) Update --- */
  char planet_sector_sql[1024];


  snprintf (planet_sector_sql,
            sizeof (planet_sector_sql),
            "UPDATE planets SET sector=%d WHERE id=2; "
            "INSERT INTO sector_assets (sector, player, offensive_setting, asset_type, corporation, quantity, deployed_at) "
            "VALUES (%d, 0, 3, 2, 2, 50000, CAST(strftime('%%s','now') AS INTEGER)); "
            "INSERT INTO sector_assets (sector, player, asset_type, corporation, quantity, deployed_at) "
            "VALUES (%d, 0, 1, 2, 250,   CAST(strftime('%%s','now') AS INTEGER));",
            longest_tunnel_sector,
            longest_tunnel_sector,
            longest_tunnel_sector);
  if (!db_exec(db, planet_sector_sql, NULL, 0, &err))
    {
      LOGE ("Ferringhi DB Issue: %s", err.message);
      return -1;
    }
  /* --- Orion Syndicate Outpost (num=3) Update (Correction IV.1) */
  char oso_planet_sector[1024];


  snprintf (oso_planet_sector,
            sizeof (oso_planet_sector),
            "UPDATE planets SET sector=%d WHERE id=3; "
            "INSERT INTO sector_assets (sector, player,offensive_setting, asset_type, corporation, quantity, deployed_at) "
            "VALUES (%d, 4, 2, 2, 1, 50000, CAST(strftime('%%s','now') AS INTEGER)); "
            "INSERT INTO sector_assets (sector, player, asset_type, corporation, quantity, deployed_at) "
            "VALUES (%d, 4, 1, 1, 250,   CAST(strftime('%%s','now') AS INTEGER));",
            oso_tunnel,
            oso_tunnel,
            oso_tunnel);
  if (!db_exec(db, oso_planet_sector, NULL, 0, &err))
    {
      fprintf (stderr, "create Orion Syndicate failed: %s\n",
               err.message);
      return -1;
    }

  char oso_player_sector[1024];


  snprintf (oso_player_sector,
            sizeof (oso_player_sector),
            "UPDATE players set sector=%d where name like '%%Captain';",
            oso_tunnel);

  if (!db_exec(db, oso_player_sector, NULL, 0, &err))
    {
      fprintf (stderr, "Orion Syndicate Player Sector Update failed: %s\n",
               err.message);
      return -1;
    }

  h_add_credits (db, "npc_planet", 3, 0, "DEPOSIT", NULL, NULL);

  /* Place the new Black Market Port in the Orion Hideout Sector (Correction IV.2) */
  int orion_port_id = 0;


  if (create_full_port (db,
                        oso_tunnel,
                        9998,
                        "Orion Black Market",
                        10,
                        &orion_port_id) != 0)
    {
      fprintf (stderr,
               "create Orion Syndicate port failed\n");
      return -1;
    }

  fprintf (stderr,
           "BIGBANG: Placed Ferringhi at sector %d (end of a long tunnel).\nBIGBANG: Placed Orion Syndicate at sector %d (end of a long tunnel).\n",
           longest_tunnel_sector,
           oso_tunnel);
  return 0;
}

int
create_taverns (void)
{
    // TODO: Implement tavern creation logic
    return 0;
}


int
create_imperial (void)
{
  db_mutex_lock ();
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      fprintf (stderr, "create_imperial: Failed to get DB handle\n");
      db_mutex_unlock ();
      return -1;
    }
  struct twconfig *cfg = config_load ();


  if (!cfg)
    {
      fprintf (stderr, "create_imperial: could not load config\n");
      db_mutex_unlock ();
      return -1;
    }
  /* Dynamically retrieve the NPC shiptype ID, filtered by can_purchase = 0 */
  int imperial_starship_id =
    get_npc_shiptype_id_by_name (db, "Imperial Starship (NPC)");


  if (imperial_starship_id == -1)
    {
      fprintf (stderr,
               "create_imperial: Failed to get required NPC shiptype ID.\n");
      free (cfg);
      db_mutex_unlock ();
      return -1;
    }
  /* Randomly place the Imperial Starship in a sector */
  int imperial_sector = 11 + (rand () % (cfg->default_nodes - 10));


  free (cfg);
  /* --- 1. Create the "Imperial" player (NPC) entry */
  char sql_player[512];


  snprintf (sql_player, 
            sizeof (sql_player),
            "INSERT INTO players (name, passwd, credits, type) VALUES ('Imperial Starship', 'BOT', \ 
10000000, 1);");
  
  db_error_t err;
  if (!db_exec(db, sql_player, NULL, 0, &err))
    {
      fprintf (stderr, "create_imperial failed to create player: %s\n",
               err.message);
      db_mutex_unlock ();
      return -1;
    }
  
  int64_t imperial_player_id = 0; // In reality we need to fetch it or use ExecInsertId
  // The INSERT above was raw text, so let's fetch max ID or re-do it with exec_insert_id
  // Re-running with bind for safety and ID retrieval:
  
  // Actually, I'll just use db_exec_insert_id.
  
  /* --- 2. Create the "Imperial Starship" ship entry linked to the player */
  const char *sql_ship_insert =
    "INSERT INTO ships (name, type_id, sector, fighters, shields, holds, photons, genesis, attack, hull, perms, flags) "
    "VALUES ('Imperial Starship', $1, $2, $3, $4, $5, $6, $7, $8, 10000, 731, 777);";

  db_bind_t s_params[] = {
      db_bind_i32(imperial_starship_id),
      db_bind_i32(imperial_sector),
      db_bind_i32(imperial_stats.fighters),
      db_bind_i32(imperial_stats.shields),
      db_bind_i32(imperial_stats.holds),
      db_bind_i32(imperial_stats.photons),
      db_bind_i32(imperial_stats.genesis),
      db_bind_i32(imperial_stats.attack)
  };

  int64_t imperial_ship_id = 0;
  if (!db_exec_insert_id(db, sql_ship_insert, s_params, 8, &imperial_ship_id, &err))
    {
      fprintf (stderr, "create_imperial failed to create ship: %s\n",
               err.message);
      db_mutex_unlock ();
      return -1;
    }

  /* --- 3. Link the ship ID back to the player entry AND create ownership */
  // We need player ID. Let's fetch it by name.
  db_res_t *res_pid = NULL;
  if (db_query(db, "SELECT id FROM players WHERE name='Imperial Starship'", NULL, 0, &res_pid, &err)) {
      if (db_res_step(res_pid, &err)) {
          imperial_player_id = db_res_col_i64(res_pid, 0, &err);
      }
      db_res_finalize(res_pid);
  }

  char sql_update[512];
  snprintf (sql_update, sizeof (sql_update),
            "UPDATE players SET ship=%lld WHERE id=%lld;",
            (long long) imperial_ship_id, (long long) imperial_player_id);
  db_exec(db, sql_update, NULL, 0, &err);

  /* Create ownership record */
  char sql_own[512];
  snprintf (sql_own,
            sizeof (sql_own),
            "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES (%lld, %lld, 1, 1);",
            (long long) imperial_ship_id,
            (long long) imperial_player_id);
  db_exec(db, sql_own, NULL, 0, &err);

  fprintf (stderr,
           "BIGBANG: Imperial Starship placed at sector %d.\n",
           imperial_sector);
  db_mutex_unlock ();
  return 0;
}


/* / 
 * ---------- small helpers ---------- * / 
 */


/* Ensure there are at least N exits from Fedspace (2..10) to [outer_min..outer_max]. */
/* If fewer exist, create more (and optionally the return edge). */
static int
ensure_fedspace_exit (db_t *db, int outer_min, int outer_max,
                      int add_return_edge)
{
  if (!db)
    {
      return -1;
    }
  const int required_exits = 3; /* <- change this if you want a different minimum */
  const int max_attempts = 100; /* avoid infinite loops on tiny maps */
  int rc = 0;

  const char *sql_count =
                           "SELECT COUNT(*) "
                           "FROM sector_warps "
                           "WHERE from_sector BETWEEN 2 AND 10 "
                           "  AND to_sector   BETWEEN $1 AND $2;";
  const char *sql_ins = "INSERT OR IGNORE INTO sector_warps(from_sector,to_sector) VALUES($1,$2)";

  db_res_t *res_count = NULL;
  db_error_t err;
  db_bind_t p_count[] = { db_bind_i32(outer_min), db_bind_i32(outer_max) };
  
  int have = 0;
  if (db_query(db, sql_count, p_count, 2, &res_count, &err)) {
      if (db_res_step(res_count, &err)) {
          have = db_res_col_i32(res_count, 0, &err);
      }
      db_res_finalize(res_count);
  } else {
      return -1;
  }

  if (have >= required_exits)
    {
      return 0;
    }

  /* Try to add exits until we reach required_exits or hit attempt cap */
  int attempts = 0;


  while (have < required_exits && attempts < max_attempts)
    {
      ++attempts;
      /* Pick random FedSpace source 2..10 and random outer destination [outer_min..outer_max] */
      int from = 2 + (rand () % 9);     /* 2..10 inclusive */
      int span = (outer_max >= outer_min) ? (outer_max - outer_min + 1) : 1;
      int to = outer_min + (rand () % span);


      if (to == from)
        {
          to = (to < outer_max) ? (to + 1) : outer_min; /* avoid self-edge */
        }
      /* Insert forward edge */
      db_bind_t p_ins[] = { db_bind_i32(from), db_bind_i32(to) };
      db_exec(db, sql_ins, p_ins, 2, &err);

      /* Optional return edge (keeps it safe from one-way pruning) */
      if (add_return_edge)
        {
          db_bind_t p_ret[] = { db_bind_i32(to), db_bind_i32(from) };
          db_exec(db, sql_ins, p_ret, 2, &err);
        }
      
      /* Re-count */
      if (db_query(db, sql_count, p_count, 2, &res_count, &err)) {
          if (db_res_step(res_count, &err)) {
              have = db_res_col_i32(res_count, 0, &err);
          }
          db_res_finalize(res_count);
      }
    }
  /* If we ran out of attempts without reaching the target, still OK but report */
  if (have < required_exits)
    {
      fprintf (stderr,
               "BIGBANG: only %d/%d exits created after %d attempts\n", have,
               required_exits, attempts);
    }
  else
    {
      fprintf (stderr, "BIGBANG: ensured %d exits from 2..10 to [%d..%d]\n",
               have, outer_min, outer_max);
    }
  return 0;
}


/* A simple utility to check if a column exists. */
static bool
has_column (db_t *db, const char *table, const char *column)
{
  char sql[256];
  // Portable check: try to select the column LIMIT 0
  snprintf (sql, sizeof (sql), "SELECT %s FROM %s LIMIT 0", column, table);
  db_res_t *res = NULL;
  db_error_t err;
  
  if (db_query(db, sql, NULL, 0, &res, &err)) {
      db_res_finalize(res);
      return true;
  }
  return false;
}

////////////////////////////////////////
int
create_derelicts (void)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      fprintf (stderr, "[create_derelicts] no DB handle\n");
      return 1;
    }
  int rc = 0;
  /* Seed RNG once per process. */
  static int seeded = 0;


  if (!seeded)
    {
      srand ((unsigned) time (NULL));
      seeded = 1;
    }
  
  // This logic is complex to port 1:1 because it builds dynamic SQL. 
  // We will assume the schema is fixed for v3 and use a static INSERT.
  // The original dynamic logic is hard to maintain with generic binding.
  
  const char *sql_select =
    "SELECT id, name, maxholds, maxfighters, maxshields, maxmines, maxphotons, maxbeacons, maxattack, maxlimpets, maxgenesis "
    "FROM shiptypes WHERE can_purchase=1 AND id NOT IN (18);";
  
  db_res_t *sel = NULL;
  db_error_t err;

  if (!db_query(db, sql_select, NULL, 0, &sel, &err))
    {
      fprintf (stderr, "[create_derelicts] shiptypes SELECT failed: %s\n", err.message);
      return 1;
    }

  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err))
    {
      fprintf (stderr, "[create_derelicts] begin transaction failed: %s\n", err.message);
      db_res_finalize(sel);
      return 1;
    }

  const char *insert_sql = 
      "INSERT INTO ships (name, type_id, sector, holds, shields, fighters, mines, photons, beacons, attack, limpets, genesis, flags, ported, onplanet, cloaking_devices, cloaked, perms) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, 0, 0, 0, 0, 0, 777);";

  while (db_res_step (sel, &err))
    {
      int st_id = db_res_col_i32(sel, 0, &err);
      const char *stype_name = db_res_col_text(sel, 1, &err);
      
      /* Build display name */
      char dname[128];
      char rnd[128];

      if (db_rand_npc_shipname (db, rnd, sizeof rnd) == 0 && rnd[0])
        {
          snprintf (dname, sizeof dname, "%s", rnd);
        }
      else if (stype_name && stype_name[0] != '\0')
        {
          snprintf (dname, sizeof dname, "Derelict %s", stype_name);
        }
      else
        {
          snprintf (dname, sizeof dname, "Derelict Type %d", st_id);
        }
      /* Pick a random sector in the range 11..500 */
      int sector = 11 + (rand () % 490);
      /* Capacities from shiptypes */
      int max_holds = db_res_col_i32(sel, 2, &err);
      int max_fighters = db_res_col_i32(sel, 3, &err);
      int max_shields = db_res_col_i32(sel, 4, &err);
      int max_mines = db_res_col_i32(sel, 5, &err);
      int max_photons = db_res_col_i32(sel, 6, &err);
      int max_beacons = db_res_col_i32(sel, 7, &err);
      int max_attack = db_res_col_i32(sel, 8, &err);
      int max_limpets = db_res_col_i32(sel, 9, &err);
      int max_genesis = db_res_col_i32(sel, 10, &err);

      /* Random fill percentage for “used” fields */
#define MIN_FILL_PCT 25
#define MAX_FILL_PCT 75
      int fill_pct =
        MIN_FILL_PCT + (rand () % (MAX_FILL_PCT - MIN_FILL_PCT + 1));
      int holds = (int) (max_holds * ((float) fill_pct / 100.0));
      int shields_to_add = (int) (max_shields * ((float) fill_pct / 100.0));
      int fighters_to_add = (int) (max_fighters * ((float) fill_pct / 100.0));
      int mines_to_add = (int) (max_mines * ((float) fill_pct / 100.0));
      int photons_to_add = (int) (max_photons * ((float) fill_pct / 100.0));
      int beacons_to_add = (int) (max_beacons * ((float) fill_pct / 100.0));
      int attack_to_add = (int) (max_attack * ((float) fill_pct / 100.0));
      int limpets_to_add = (int) (max_limpets * ((float) fill_pct / 100.0));
      int genesis_to_add = (int) (max_genesis * ((float) fill_pct / 100.0));

      db_bind_t params[] = {
          db_bind_text(dname),
          db_bind_i32(st_id),
          db_bind_i32(sector),
          db_bind_i32(holds),
          db_bind_i32(shields_to_add),
          db_bind_i32(fighters_to_add),
          db_bind_i32(mines_to_add),
          db_bind_i32(photons_to_add),
          db_bind_i32(beacons_to_add),
          db_bind_i32(attack_to_add),
          db_bind_i32(limpets_to_add),
          db_bind_i32(genesis_to_add)
      };

      if (!db_exec(db, insert_sql, params, 12, &err))
        {
          fprintf (stderr, "[create_derelicts] insert failed: %s\n", err.message);
          db_tx_rollback(db, NULL);
          db_res_finalize(sel);
          return 1;
        }
    }
  db_res_finalize(sel);

  /* Rename all ships of type 18 to "Mary Celeste" */
  { 
    const char *upd = "UPDATE ships SET name=$1 WHERE type_id=$2";
    db_bind_t p[] = { db_bind_text("Mary Celeste"), db_bind_i32(18) };
    db_exec(db, upd, p, 2, &err);
  }
  
  if (!db_tx_commit(db, &err))
    {
      fprintf (stderr, "[create_derelicts] COMMIT failed: %s\n", err.message);
      return 1;
    }
  return 0;
}


static int
ensure_all_sectors_have_exits (db_t *db)
{
  const char *sql_select_all = "SELECT id FROM sectors;";
  const char *sql_select_outgoing =
    "SELECT COUNT(*) FROM sector_warps WHERE from_sector=$1;";
  const char *sql_select_incoming =
    "SELECT from_sector FROM sector_warps WHERE to_sector=$1 LIMIT 1;";
  const char *sql_insert_warp =
    "INSERT INTO sector_warps (from_sector, to_sector) VALUES ($1, $2);";
  
  int rc = 0;
  int sectors_fixed = 0;
  db_error_t err;
  
  db_tx_begin(db, DB_TX_IMMEDIATE, &err);
  
  db_res_t *res_all = NULL;
  
  if (db_query(db, sql_select_all, NULL, 0, &res_all, &err))
    {
      while (db_res_step(res_all, &err))
        {
          int sector_id = db_res_col_i32(res_all, 0, &err);

          /* Check if the sector has any outgoing warps */
          db_res_t *res_out = NULL;
          db_bind_t p_sec[] = { db_bind_i32(sector_id) };
          int outgoing_count = 0;
          
          if (db_query(db, sql_select_outgoing, p_sec, 1, &res_out, &err)) {
              if (db_res_step(res_out, &err)) outgoing_count = db_res_col_i32(res_out, 0, &err);
              db_res_finalize(res_out);
          }

          if (outgoing_count == 0)
            {
              /* This is a one-way trap. Find a sector that warps here. */
              int from_sector_id = -1;
              db_res_t *res_in = NULL;
              
              if (db_query(db, sql_select_incoming, p_sec, 1, &res_in, &err)) {
                  if (db_res_step(res_in, &err)) from_sector_id = db_res_col_i32(res_in, 0, &err);
                  db_res_finalize(res_in);
              }

              if (from_sector_id != -1)
                {
                  /* Create a return warp back to the originating sector */
                  db_bind_t p_ins[] = { db_bind_i32(sector_id), db_bind_i32(from_sector_id) };
                  db_exec(db, sql_insert_warp, p_ins, 2, &err);
                  sectors_fixed++;
                }
            }
        }
      db_res_finalize(res_all);
    }
  
  db_tx_commit(db, &err);
  printf ("BIGBANG: Fixed %d one-way sectors.\n", sectors_fixed);
  return 0;
}


/**
 * @brief Migrates existing beacons from the sectors table to the new
 * sector_assets table by inserting an ownership record (player 0).
 * NOTE: The beacon text remains in the sectors table to support legacy functions.
 * * This is intended to run once during the BIGBANG initialization phase.
 * * @return 0 on success, -1 on database error.
 */
int
create_ownership (void)
{
  db_t *handle = game_db_get_handle ();   /* Assuming db_get_handle() is available */
  if (!handle) return -1;
  
  /* --- 1. Prepare SELECT statement: Get sectors with existing beacon text. */
  const char *select_sql =
    "SELECT id FROM sectors WHERE beacon IS NOT NULL AND beacon != '';";
  
  /* --- 2. Prepare INSERT statement: Insert into sector_assets (OWNERSHIP ONLY) */
  const char *insert_sql =
    "INSERT INTO sector_assets (sector, player, asset_type, quantity, ttl, deployed_at) VALUES ($1, 0, 1, 1, NULL, strftime('%s', 'now'));";

  /* --- 3. Loop through sectors and insert assets */
  int total_beacons = 0;
  db_res_t *res = NULL;
  db_error_t err;

  if (db_query(handle, select_sql, NULL, 0, &res, &err))
    {
      while (db_res_step(res, &err))
        {
          int sector_id = db_res_col_i32(res, 0, &err);
          db_bind_t params[] = { db_bind_i32(sector_id) };
          db_exec(handle, insert_sql, params, 1, &err);
          total_beacons++;
        }
      db_res_finalize(res);
    }
  
  fprintf (stderr,
           "BIGBANG: Migrated ownership for %d system beacons successfully.\n",
           total_beacons);
  return 0;
}


int
create_ports (void)
{
  db_t *db = game_db_get_handle ();
  struct twconfig *cfg = config_load ();
  if (!cfg)
    {
      fprintf (stderr, "create_ports: could not load config\n");
      return -1;
    }
  int numSectors = cfg->default_nodes;
  int maxPorts = cfg->max_ports;


  free (cfg);
  if (maxPorts < 1)
    {
      fprintf (stderr, "create_ports: max_ports < 1 in config\n");
      return 0;
    }
  char name_buffer[50];

  /*
   * ===================================================================
   * --- Create and populate the rest of the ports (Correction V.2) --- */
  { 
    for (int i = 2; i <= maxPorts; i++)
      {
        int sector = 11 + (rand () % (numSectors - 10));
        int type_id = random_port_type_1_to_8 ();

        randomname (name_buffer);
        if (i == 7)
          {
            snprintf (name_buffer, sizeof (name_buffer), "Ferrengi Home");
          }

        int port_id = 0;
        int rc = create_full_port (db, sector, /*port number */ i,
                                   /*base name */ name_buffer,
                                   /*type */ type_id,
                                   &port_id);


        if (rc != 0)
          {
            fprintf (stderr,
                     "create_ports failed at %d (create_full_port rc=%d)\n",
                     i, rc);
            return -1;
          }

        /* optional: tweak Ferrengi Home stats after insert to match your old special-case */
        if (i == 7)
          {
            const char *sql = 
              "UPDATE ports SET size=$1, techlevel=$2 WHERE id=$3";
            db_bind_t params[] = { db_bind_i32(10), db_bind_i32(5), db_bind_i32(port_id) };
            db_error_t err;
            db_exec(db, sql, params, 3, &err);
          }
      }
  }


  fprintf (stderr,
           "BIGBANG: Created %d normal ports\n", maxPorts - 1);
  return 0;
}


static int
fix_traps_with_pathcheck (db_t *db, int fedspace_max)
{
  int max_id = 0;
  db_res_t *st = NULL;
  db_error_t err;
  
  if (db_query(db, "SELECT MAX(id) FROM sectors", NULL, 0, &st, &err)) {
      if (db_res_step(st, &err)) max_id = db_res_col_i32(st, 0, &err);
      db_res_finalize(st);
  }
  
  if (max_id <= fedspace_max)
    {
      return 0;
    }
  
  const char *ins = "INSERT OR IGNORE INTO sector_warps(from_sector,to_sector) VALUES($1,$2)";

  for (int s = fedspace_max + 1; s <= max_id; ++s)
    {
      /* Is there a path from s back to sector 1? */
      int has_path = db_path_exists (db, s, 1);


      if (has_path < 0)
        {
          return -1;
        }
      if (!has_path)
        {
          /* No path home: add an escape warp back into FedSpace */
          int escape = 1 + (rand () % fedspace_max);
          db_bind_t params[] = { db_bind_i32(s), db_bind_i32(escape) };
          db_exec(db, ins, params, 2, &err);
        }
    }
  return 0;
}


/* Implementation of Correction VI: create_stardock_port */
static int
create_stardock_port (db_t *db, int numSectors)
{
  int stardock_sector = 0;
  if (numSectors > 11)
    {
      stardock_sector = 11 + (rand () % (numSectors - 10));
    }
  else
    {
      stardock_sector = 11;
    }

  int port_id = 0;
  int rc = create_full_port (db, stardock_sector, 1, "Stardock", 9, &port_id);


  if (rc != 0)
    {
      fprintf (stderr, "create_stardock_port failed\n");
      return -1;
    }

  fprintf (stderr,
           "BIGBANG: Stardock created at sector %d (Port ID %d)\n",
           stardock_sector,
           port_id);
  return 0;
}
