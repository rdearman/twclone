#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sqlite3.h>
#include <string.h>
#include <stdbool.h>
#include "database.h"
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
/* forward decls */
static int create_derelicts (void);
static int fix_traps_with_pathcheck (sqlite3 *db, int fedspace_max);
static int ensure_fedspace_exit (sqlite3 *db, int outer_min, int outer_max,
                                 int add_return_edge);
/*
   static const char *SQL_INSERT_WARP =
   "INSERT OR IGNORE INTO sector_warps(from_sector, to_sector) VALUES(?,?)";
 */
/*
   static const char *SQL_INSERT_USED_SECTOR =
   "INSERT INTO used_sectors(used) VALUES(?)";
 */
// static int get_out_degree (sqlite3 * db, int sector);
static int insert_warp_unique (sqlite3 *db, int from, int to);
static int create_random_warps (sqlite3 *db, int numSectors, int maxWarps);
int create_imperial (void);     // Declared here for the compiler
static int ensure_all_sectors_have_exits (sqlite3 *db);
int create_ownership (void);
static bool has_column (sqlite3 *db, const char *table, const char *column);
struct twconfig *


config_load (void)
{
  const char *sql =
    "SELECT turnsperday, "
    "       maxwarps_per_sector, "
    "       startingcredits, "
    "       startingfighters, "
    "       startingholds, "
    "       processinterval, "
    "       autosave, "
    "       max_ports, "
    "       max_planets_per_sector, "
    "       max_total_planets, "
    "       max_citadel_level, "
    "       number_of_planet_types, "
    "       max_ship_name_length, "
    "       ship_type_count, "
    "       hash_length, "
    "       default_nodes, "
    "       buff_size, "
    "       max_name_length, "
    "       planet_type_count, "
    "       shipyard_enabled, "
    "       shipyard_trade_in_factor_bp, "
    "       shipyard_require_cargo_fit, "
    "       shipyard_require_fighters_fit, "
    "       shipyard_require_shields_fit, "
    "       shipyard_require_hardware_compat, "
    "       shipyard_tax_bp " "FROM config WHERE id=1;";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2 (db_get_handle (), sql, -1, &stmt, NULL) !=
      SQLITE_OK)
    {
      fprintf (stderr, "config_load prepare error: %s\n",
               sqlite3_errmsg (db_get_handle ()));
      return NULL;
    }
  struct twconfig *cfg = malloc (sizeof (struct twconfig));
  if (!cfg)
    {
      sqlite3_finalize (stmt);
      return NULL;
    }
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      cfg->turnsperday = sqlite3_column_int (stmt, 0);
      cfg->maxwarps_per_sector = sqlite3_column_int (stmt, 1);
      cfg->startingcredits = sqlite3_column_int (stmt, 2);
      cfg->startingfighters = sqlite3_column_int (stmt, 3);
      cfg->startingholds = sqlite3_column_int (stmt, 4);
      cfg->processinterval = sqlite3_column_int (stmt, 5);
      cfg->autosave = sqlite3_column_int (stmt, 6);
      cfg->max_ports = sqlite3_column_int (stmt, 7);
      cfg->max_planets_per_sector = sqlite3_column_int (stmt, 8);
      cfg->max_total_planets = sqlite3_column_int (stmt, 9);
      cfg->max_citadel_level = sqlite3_column_int (stmt, 10);
      cfg->number_of_planet_types = sqlite3_column_int (stmt, 11);
      cfg->max_ship_name_length = sqlite3_column_int (stmt, 12);
      cfg->ship_type_count = sqlite3_column_int (stmt, 13);
      cfg->hash_length = sqlite3_column_int (stmt, 14);
      cfg->default_nodes = sqlite3_column_int (stmt, 15);
      cfg->buff_size = sqlite3_column_int (stmt, 16);
      cfg->max_name_length = sqlite3_column_int (stmt, 17);
      cfg->planet_type_count = sqlite3_column_int (stmt, 18);
      cfg->shipyard_enabled = sqlite3_column_int (stmt, 19);
      cfg->shipyard_trade_in_factor_bp = sqlite3_column_int (stmt, 20);
      cfg->shipyard_require_cargo_fit = sqlite3_column_int (stmt, 21);
      cfg->shipyard_require_fighters_fit = sqlite3_column_int (stmt, 22);
      cfg->shipyard_require_shields_fit = sqlite3_column_int (stmt, 23);
      cfg->shipyard_require_hardware_compat = sqlite3_column_int (stmt, 24);
      cfg->shipyard_tax_bp = sqlite3_column_int (stmt, 25);
      // fprintf(stderr, "DEBUG:
      //        cfg->maxwarps_per_sector);
    }
  else
    {
      free (cfg);
      cfg = NULL;
    }
  sqlite3_finalize (stmt);
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
prune_tunnel_edges (sqlite3 *db)
{
  /* Remove edges from tunnels to non-tunnel nodes */
  sqlite3_exec (db,
                "DELETE FROM sector_warps "
                "WHERE from_sector IN (SELECT used FROM used_sectors) "
                "  AND to_sector   NOT IN (SELECT used FROM used_sectors);",
                NULL, NULL, NULL);
  /* And the reverse direction */
  sqlite3_exec (db,
                "DELETE FROM sector_warps "
                "WHERE to_sector   IN (SELECT used FROM used_sectors) "
                "  AND from_sector NOT IN (SELECT used FROM used_sectors);",
                NULL, NULL, NULL);
}


static int
insert_warp_unique (sqlite3 *db, int from, int to)
{
  sqlite3_stmt *chk = NULL, *ins = NULL;
  int rc = sqlite3_prepare_v2 (db,
                               "SELECT 1 FROM sector_warps WHERE from_sector=? AND to_sector=? LIMIT 1;",
                               -1,
                               &chk,
                               NULL);
  if (rc != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int (chk, 1, from);
  sqlite3_bind_int (chk, 2, to);
  rc = sqlite3_step (chk);
  sqlite3_finalize (chk);
  if (rc == SQLITE_ROW)
    {
      return 0;                 /* exists */
    }
  rc = sqlite3_prepare_v2 (db,
                           "INSERT INTO sector_warps(from_sector,to_sector) VALUES(?,?);",
                           -1,
                           &ins,
                           NULL);
  if (rc != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int (ins, 1, from);
  sqlite3_bind_int (ins, 2, to);
  rc = sqlite3_step (ins);
  sqlite3_finalize (ins);
  if (rc != SQLITE_DONE)
    {
      return -1;
    }
  return 1;
}


static int
sector_degree (sqlite3 *db, int s)
{
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db,
                               "SELECT COUNT(*) FROM sector_warps WHERE from_sector=?;",
                               -1,
                               &st,
                               NULL);
  if (rc != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int (st, 1, s);
  rc = sqlite3_step (st);
  int deg = (rc == SQLITE_ROW) ? sqlite3_column_int (st, 0) : -1;
  sqlite3_finalize (st);
  return deg;
}


static int
get_sector_count (void)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return -1;
    }
  sqlite3_stmt *st = NULL;
  int rc =
    sqlite3_prepare_v2 (db, "SELECT COUNT(*) FROM sectors;", -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return -1;
    }
  rc = sqlite3_step (st);
  int n = (rc == SQLITE_ROW) ? sqlite3_column_int (st, 0) : -1;
  sqlite3_finalize (st);
  return n;
}


static int
is_sector_used (sqlite3 *db, int sector_id)
{
  sqlite3_stmt *st = NULL;
  int used = 0;
  if (sqlite3_prepare_v2
        (db, "SELECT 1 FROM used_sectors WHERE used=? LIMIT 1;", -1, &st,
        NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, sector_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          used = 1;
        }
      sqlite3_finalize (st);
    }
  return used;
}


/* ----------------------------------------------------
 * Random warps (tunnel-aware)
 * ---------------------------------------------------- */
static int
create_random_warps (sqlite3 *db, int numSectors, int maxWarps)
{
  char *errmsg = NULL;
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      fprintf (stderr, "create_random_warps: BEGIN failed: %s\n", errmsg);
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
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
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      fprintf (stderr, "create_random_warps: COMMIT failed: %s\n", errmsg);
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      return -1;
    }
  if (errmsg)
    {
      sqlite3_free (errmsg);
    }
  return 0;
fail:
  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
  if (errmsg)
    {
      sqlite3_free (errmsg);
    }
  return -1;
}


/* ----------------------------------------------------
 * Ensure every non-tunnel sector has at least one exit
 * ---------------------------------------------------- */
int
ensure_sector_exits (sqlite3 *db, int numSectors)
{
  if (!db || numSectors <= 0)
    {
      return -1;
    }
  sqlite3_stmt *q_count = NULL, *q_in = NULL, *ins = NULL;
  int rc = sqlite3_prepare_v2 (db,
                               "SELECT COUNT(1) FROM sector_warps WHERE from_sector=?",
                               -1,
                               &q_count,
                               NULL);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
  rc = sqlite3_prepare_v2 (db,
                           "SELECT from_sector FROM sector_warps WHERE to_sector=? ORDER BY RANDOM() LIMIT 1;",
                           -1,
                           &q_in,
                           NULL);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
  rc = sqlite3_prepare_v2 (db,
                           "INSERT OR IGNORE INTO sector_warps(from_sector,to_sector) VALUES(?,?);",
                           -1,
                           &ins,
                           NULL);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
  for (int s = 11; s <= numSectors; s++)
    {
      if (is_sector_used (db, s))
        {
          continue;             /* don't add exits FROM tunnel nodes */
        }
      sqlite3_reset (q_count);
      sqlite3_clear_bindings (q_count);
      sqlite3_bind_int (q_count, 1, s);
      rc = sqlite3_step (q_count);
      if (rc != SQLITE_ROW)
        {
          rc = SQLITE_ERROR;
          break;
        }
      int outc = sqlite3_column_int (q_count, 0);
      if (outc > 0)
        {
          continue;
        }
      int to = 0;
      if ((rand () % 100) < 80)
        {
          sqlite3_reset (q_in);
          sqlite3_clear_bindings (q_in);
          sqlite3_bind_int (q_in, 1, s);
          rc = sqlite3_step (q_in);
          if (rc == SQLITE_ROW)
            {
              to = sqlite3_column_int (q_in, 0);
              if (to == s || is_sector_used (db, to))
                {
                  to = 0;
                }
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
      sqlite3_reset (ins);
      sqlite3_clear_bindings (ins);
      sqlite3_bind_int (ins, 1, s);
      sqlite3_bind_int (ins, 2, to);
      sqlite3_step (ins);
    }
done:
  if (q_count)
    {
      sqlite3_finalize (q_count);
    }
  if (q_in)
    {
      sqlite3_finalize (q_in);
    }
  if (ins)
    {
      sqlite3_finalize (ins);
    }
  return (rc == SQLITE_OK || rc == SQLITE_ROW) ? 0 : -1;
}


/* ----------------------------------------------------
 * Tunnels — created LAST; atomic per path; logs only on success
 * ---------------------------------------------------- */
int
bigbang_create_tunnels (void)
{
  sqlite3 *db = db_get_handle ();
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
  char *errmsg = NULL;
  if (sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      fprintf (stderr, "BIGBANG: tunnels BEGIN failed: %s\n", errmsg);
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      return -1;
    }
  /* Fresh run: clear used_sectors */
  sqlite3_exec (db, "DELETE FROM used_sectors;", NULL, NULL, NULL);
  const char *SQL_INSERT_WARP =
    "INSERT OR IGNORE INTO sector_warps(from_sector,to_sector) VALUES(?,?);";
  const char *SQL_INSERT_USED =
    "INSERT OR IGNORE INTO used_sectors(used) VALUES(?);";
  sqlite3_stmt *st_warp = NULL, *st_used = NULL;
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
      //fprintf (stderr, "BIGBANG: Proposed tunnel: %d", nodes[0]);
      /* for (int i = 1; i < path_len; i++) */
      /*        fprintf (stderr, "->%d", nodes[i]); */
      /* fprintf (stderr, "\n"); */
      sqlite3_exec (db, "SAVEPOINT tunnel;", NULL, NULL, NULL);
      int failed = 0;
      for (int i = 0; i < path_len - 1; i++)
        {
          int a = nodes[i], b = nodes[i + 1];
          if (sqlite3_prepare_v2 (db, SQL_INSERT_WARP, -1, &st_warp, NULL) !=
              SQLITE_OK)
            {
              failed = 1;
              break;
            }
          sqlite3_bind_int (st_warp, 1, a);
          sqlite3_bind_int (st_warp, 2, b);
          if (sqlite3_step (st_warp) != SQLITE_DONE)
            {
              failed = 1;
              sqlite3_finalize (st_warp);
              break;
            }
          sqlite3_finalize (st_warp);
          st_warp = NULL;
          if (sqlite3_prepare_v2 (db, SQL_INSERT_WARP, -1, &st_warp, NULL) !=
              SQLITE_OK)
            {
              failed = 1;
              break;
            }
          sqlite3_bind_int (st_warp, 1, b);
          sqlite3_bind_int (st_warp, 2, a);
          if (sqlite3_step (st_warp) != SQLITE_DONE)
            {
              failed = 1;
              sqlite3_finalize (st_warp);
              break;
            }
          sqlite3_finalize (st_warp);
          st_warp = NULL;
        }
      if (failed)
        {
          sqlite3_exec (db, "ROLLBACK TO tunnel; RELEASE SAVEPOINT tunnel;",
                        NULL, NULL, NULL);
          attempts++;
          continue;
        }
      sqlite3_exec (db, "RELEASE SAVEPOINT tunnel;", NULL, NULL, NULL);
      //fprintf (stderr, "BIGBANG: Created tunnel: %d", nodes[0]);
      /* for (int i = 1; i < path_len; i++) */
      /*        fprintf (stderr, "->%d", nodes[i]); */
      /* fprintf (stderr, "\n"); */
      for (int i = 0; i < path_len; i++)
        {
          if (sqlite3_prepare_v2 (db, SQL_INSERT_USED, -1, &st_used, NULL) ==
              SQLITE_OK)
            {
              sqlite3_bind_int (st_used, 1, nodes[i]);
              sqlite3_step (st_used);
              sqlite3_finalize (st_used);
              st_used = NULL;
            }
        }
      added_tunnels++;
      attempts++;
    }
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
  fprintf (stderr, "BIGBANG: Added %d tunnels in %d attempts.\n",
           added_tunnels, attempts);
  return 0;
}


/* ----------------------------------------------------
 * Sector creation (names / nebulae / beacon every 64th)
 * ---------------------------------------------------- */


int
create_sectors (void)
{
  sqlite3 *db = db_get_handle ();
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
  char *errmsg = NULL;
  // Begin transaction for performance
  int rc = sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "BIGBANG: BEGIN failed: %s\n", errmsg);
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      free (cfg);
      return -1;
    }
  // Use a prepared statement to prevent SQL injection
  const char *sql_insert =
    "INSERT INTO sectors (name, beacon, nebulae) VALUES (?, ?, ?);";
  sqlite3_stmt *stmt = NULL;
  rc = sqlite3_prepare_v2 (db, sql_insert, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "BIGBANG: Failed to prepare statement: %s\n",
               sqlite3_errmsg (db));
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      free (cfg);
      return -1;
    }
  for (int i = 1; i <= cfg->default_nodes; i++)
    {
      char name[128];
      char neb[128];
      consellationName (name);
      consellationName (neb);
      sqlite3_bind_text (stmt, 1, name, -1, SQLITE_STATIC);
      if ((i % 64) == 0)
        {
          sqlite3_bind_text (stmt, 2, "Barreik was here!", -1, SQLITE_STATIC);
        }
      else
        {
          sqlite3_bind_text (stmt, 2, "", -1, SQLITE_STATIC);
        }
      sqlite3_bind_text (stmt, 3, neb, -1, SQLITE_STATIC);
      rc = sqlite3_step (stmt);
      if (rc != SQLITE_DONE)
        {
          fprintf (stderr, "BIGBANG: Failed to insert sector %d: %s\n", i,
                   sqlite3_errmsg (db));
          sqlite3_finalize (stmt);
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          free (cfg);
          return -1;
        }
      sqlite3_reset (stmt);
    }
  sqlite3_finalize (stmt);
  rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "BIGBANG: COMMIT failed: %s\n", errmsg);
      if (errmsg)
        {
          sqlite3_free (errmsg);
        }
      free (cfg);
      return -1;
    }
  free (cfg);
  return 0;
}


/* ----------------------------------------------------
 * Port creation (placeholder — keep your original behaviour)
 * ---------------------------------------------------- */


/*
   static const char *
   trade_code_for_type (int t)
   {
   switch (t)
    {
    case 1:
      return "BBS";
    case 2:
      return "BSB";
    case 3:
      return "BSS";
    case 4:
      return "SBB";
    case 5:
      return "SBS";
    case 6:
      return "SSB";
    case 7:
      return "SSS";
    case 8:
      return "BBB";
    default:
      return "???";
    }
   }
 */


static int
random_port_type_1_to_8 (void)
{
  return 1 + (rand () % 8);
}


/*
   static int
   seed_port_trade_rows (sqlite3 *db, int port_id, int type)
   {
   (void) db;
   (void) port_id;
   (void) type;
   return 0;
   }
 */


/* ----------------------------------------------------
 * Orchestration
 * ---------------------------------------------------- */
int
bigbang (void)
{
  sqlite3 *db = db_get_handle ();
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
  int rc2 = ensure_fedspace_exit (db, 11, 500, 1);
  if (rc2 != SQLITE_OK)
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
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "error ensuring all sectors have exits: %s\n",
               sqlite3_errmsg (db));
      return rc;
    }
  printf ("BIGBANG: Chaining isolated sectors and bridges...\n");
  printf ("BIGBANG: Fixing trap components with path checks...\n");
  rc = fix_traps_with_pathcheck (db, 10);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "fix_traps_with_pathcheck failed: %s\n",
               sqlite3_errmsg (db));
    }
  // After all sectors/warps are generated:
  int ferringhi = db_chain_traps_and_bridge (10);       // 1..10 are FedSpace by convention
  // It returns the first isolated sector it found. So we pass that to the
  // create_ferringhi function which will use it if it isn't equal to zero and
  // the create_ferringhi function can't find a long tunnel.
  // might want to change that later.
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


/* ------- tunnel helpers ------- */


/*
   static int
   sw_add_edge (sqlite3 *db, int a, int b)
   {
   static sqlite3_stmt *ins = NULL;
   if (!ins)
    {
      sqlite3_prepare_v2 (db,
                          "INSERT OR IGNORE INTO sector_warps(from_sector,to_sector) VALUES(?,?)",
                          -1, &ins, NULL);
    }
   sqlite3_reset (ins);
   sqlite3_clear_bindings (ins);
   sqlite3_bind_int (ins, 1, a);
   sqlite3_bind_int (ins, 2, b);
   int rc = sqlite3_step (ins);
   return (rc == SQLITE_DONE || rc == SQLITE_OK) ? SQLITE_OK : rc;
   }
 */


/* Pick a random sector in [lo,hi] that isn't equal to 'avoid' and exists in sectors */


/*
   static int
   pick_sector_in_range (sqlite3 *db, int lo, int hi, int avoid,
                      int max_attempts)
   {
   static sqlite3_stmt *st = NULL;
   if (!st)
    {
      sqlite3_prepare_v2 (db, "SELECT 1 FROM sectors WHERE id=? LIMIT 1", -1,
                          &st, NULL);
    }
   for (int i = 0; i < max_attempts; i++)
    {
      int s = lo + (rand () % (hi - lo + 1));
      if (s == avoid)
        continue;
      sqlite3_reset (st);
      sqlite3_clear_bindings (st);
      sqlite3_bind_int (st, 1, s);
      int rc = sqlite3_step (st);
      if (rc == SQLITE_ROW)
        return s;
    }
   return 0;
   }
 */


/************* Tunneling *******************/


int
count_edges ()
{
  sqlite3 *handle = db_get_handle ();
  if (!handle)
    {
      return -1;
    }
  int count = 0;
  sqlite3_stmt *stmt;
  int rc =
    sqlite3_prepare_v2 (handle, "SELECT count(*) FROM sector_warps;", -1,
                        &stmt, NULL);
  if (rc == SQLITE_OK)
    {
      if (sqlite3_step (stmt) == SQLITE_ROW)
        {
          count = sqlite3_column_int (stmt, 0);
        }
    }
  sqlite3_finalize (stmt);
  return count;
}


// Helper function to count connections for a given sector
int
get_sector_degree_count (int sector_id)
{
  sqlite3 *handle = db_get_handle ();
  sqlite3_stmt *stmt;
  int degree = 0;
  const char *sql =
    "SELECT COUNT(*) FROM sector_warps WHERE from_sector = ? OR to_sector = ?;";
  if (sqlite3_prepare_v2 (handle, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (stmt, 1, sector_id);
      sqlite3_bind_int (stmt, 2, sector_id);
      if (sqlite3_step (stmt) == SQLITE_ROW)
        {
          degree = sqlite3_column_int (stmt, 0);
        }
      sqlite3_finalize (stmt);
    }
  return degree;
}


// Function to check if a warp exists between two sectors
int
sw_has_edge (int from, int to)
{
  sqlite3 *handle = db_get_handle ();
  sqlite3_stmt *stmt;
  int exists = 0;
  const char *sql =
    "SELECT 1 FROM sector_warps WHERE (from_sector = ? AND to_sector = ?) OR (from_sector = ? AND to_sector = ?);";
  if (sqlite3_prepare_v2 (handle, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (stmt, 1, from);
      sqlite3_bind_int (stmt, 2, to);
      sqlite3_bind_int (stmt, 3, to);
      sqlite3_bind_int (stmt, 4, from);
      exists = (sqlite3_step (stmt) == SQLITE_ROW);
      sqlite3_finalize (stmt);
    }
  return exists;
}


/************* End Tunneling *******************/


/* Internal helpers */


/* Returns the ID of an NPC shiptype by its name. Returns -1 on error. */
static int
get_npc_shiptype_id_by_name (sqlite3 *db, const char *name)
{
  const char *q =
    "SELECT id FROM shiptypes WHERE name = ? AND can_purchase = 0;";
  sqlite3_stmt *st = NULL;
  int id = -1;
  if (sqlite3_prepare_v2 (db, q, -1, &st, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "get_npc_shiptype_id_by_name prepare failed: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      id = sqlite3_column_int (st, 0);
    }
  else
    {
      fprintf (stderr,
               "get_npc_shiptype_id_by_name: NPC Shiptype '%s' not found.\n",
               name);
    }
  sqlite3_finalize (st);
  return id;
}


/* Returns the ID of a purchasable shiptype by its name. Returns -1 on error. */
static int
get_purchasable_shiptype_id_by_name (sqlite3 *db, const char *name)
{
  const char *q =
    "SELECT id FROM shiptypes WHERE name = ? AND can_purchase = 1;";
  sqlite3_stmt *st = NULL;
  int id = -1;
  if (sqlite3_prepare_v2 (db, q, -1, &st, NULL) != SQLITE_OK)
    {
      fprintf (stderr,
               "get_purchasable_shiptype_id_by_name prepare failed: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      id = sqlite3_column_int (st, 0);
    }
  else
    {
      fprintf (stderr,
               "get_purchasable_shiptype_id_by_name: Purchasable Shiptype '%s' not found.\n",
               name);
    }
  sqlite3_finalize (st);
  return id;
}


/* ----------------------------------------------------
 * Universe population functions
 * ---------------------------------------------------- */


int
create_planets (void)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return -1;
    }
  fprintf (stderr,
           "BIGBANG: Ensuring core planets (Terra, Ferringhi, Orion) exist...\n");
  char *errmsg = NULL;
  if (sqlite3_exec (db, "BEGIN TRANSACTION;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      fprintf (stderr,
               "create_planets: BEGIN failed: %s\n",
               errmsg ? errmsg : sqlite3_errmsg (db));
      sqlite3_free (errmsg);
      return -1;
    }
  sqlite3_stmt *ins_fixed = NULL;
  /* Included owner_id and created_by which are NOT NULL */
  const char *sql_fixed =
    "INSERT OR IGNORE INTO planets (id, name, sector, type, created_at, ore_on_hand, organics_on_hand, equipment_on_hand, owner_id, created_by) VALUES (?, ?, ?, ?, strftime('%s','now'), 0, 0, 0, 0, 0)";
  if (sqlite3_prepare_v2 (db, sql_fixed, -1, &ins_fixed, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_planets prepare fixed failed: %s\n",
               sqlite3_errmsg (db));
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      return -1;
    }
  /* Terra (ID 1) - Sector 1, Type 1 (Class M) */
  sqlite3_bind_int (ins_fixed, 1, 1);
  sqlite3_bind_text (ins_fixed, 2, "Terra", -1, SQLITE_STATIC);
  sqlite3_bind_int (ins_fixed, 3, 1);
  sqlite3_bind_int (ins_fixed, 4, 1);
  if (sqlite3_step (ins_fixed) != SQLITE_DONE)
    {
      fprintf (stderr,
               "create_planets: Failed to insert Terra: %s\n",
               sqlite3_errmsg (db));
    }
  sqlite3_reset (ins_fixed);
  /* Ferringhi Homeworld (ID 2) - Sector 0 (Placeholder), Type 3 */
  sqlite3_bind_int (ins_fixed, 1, 2);
  sqlite3_bind_text (ins_fixed, 2, "Ferringhi Homeworld", -1, SQLITE_STATIC);
  sqlite3_bind_int (ins_fixed, 3, 0);
  sqlite3_bind_int (ins_fixed, 4, 3);
  if (sqlite3_step (ins_fixed) != SQLITE_DONE)
    {
      fprintf (stderr,
               "create_planets: Failed to insert Ferringhi: %s\n",
               sqlite3_errmsg (db));
    }
  sqlite3_reset (ins_fixed);
  /* Orion Hideout (ID 3) - Sector 0 (Placeholder), Type 5 */
  sqlite3_bind_int (ins_fixed, 1, 3);
  sqlite3_bind_text (ins_fixed, 2, "Orion Hideout", -1, SQLITE_STATIC);
  sqlite3_bind_int (ins_fixed, 3, 0);
  sqlite3_bind_int (ins_fixed, 4, 5);
  if (sqlite3_step (ins_fixed) != SQLITE_DONE)
    {
      fprintf (stderr,
               "create_planets: Failed to insert Orion: %s\n",
               sqlite3_errmsg (db));
    }
  sqlite3_finalize (ins_fixed);
  /* Ensure they have bank accounts. Use 1 credit to pass validation. */
  h_add_credits (db, "npc_planet", 1, 1, "DEPOSIT", NULL, NULL);
  h_add_credits (db, "npc_planet", 2, 1, "DEPOSIT", NULL, NULL);
  h_add_credits (db, "npc_planet", 3, 1, "DEPOSIT", NULL, NULL);
  if (sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg) != SQLITE_OK)
    {
      fprintf (stderr,
               "create_planets: COMMIT failed: %s\n",
               errmsg ? errmsg : sqlite3_errmsg (db));
      sqlite3_free (errmsg);
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
create_full_port (sqlite3 *db, int sector, int port_number,
                  const char *base_name, int type_id, int *port_id_out)
{
  sqlite3_stmt *port_stmt = NULL;
  int rc = SQLITE_OK;
  sqlite3_int64 port_id = 0;
  /*
   * These variables will be set by the switch statement
   * based on the port type_id.
   */
  /* Commodity stock levels */
  int ore_on_hand_val = 0;
  int organics_on_hand_val = 0;
  int equipment_on_hand_val = 0;
  /* Default port stats */
  int port_size = 5 + (rand () % 5);    /* Random size 5-9 */
  int port_tech = 1 + (rand () % 5);    /* Random tech 1-5 */
  int port_credits = 100000;
  int petty_cash_val = 0;       // Default petty cash
  /*
   * ===================================================================
   * 1. SET PORT PROPERTIES BASED ON TYPE
   * ===================================================================
   * This logic determines what each port type buys/sells and its stock.
   * This is based on standard TradeWars port classifications.
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
        port_credits = 1000000; // Stardock has more credits
        port_size = 10;
        port_tech = 10;
        break;
      case 10:                  /* Orion Black Market - Special case, high ore, low organics, high equipment */
        ore_on_hand_val = DEF_PORT_MAX_ORE * 2;
        organics_on_hand_val = DEF_PORT_MAX_ORG / 10;
        equipment_on_hand_val = DEF_PORT_MAX_EQU * 2;
        port_credits = 2000000; // More credits for black market
        petty_cash_val = 100000; // Significant petty cash
        port_size = 8;
        port_tech = 8;
        break;
      default:
        fprintf (stderr, "create_full_port: Invalid type_id %d\n", type_id);
        return SQLITE_MISUSE;
    }
  /*
   * ===================================================================
   * 2. BEGIN TRANSACTION
   * ===================================================================
   */
  rc = sqlite3_exec (db, "BEGIN IMMEDIATE", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "create_full_port: BEGIN transaction failed: %s\n",
               sqlite3_errmsg (db));
      return rc;
    }
  /*
   * ===================================================================
   * 3. INSERT INTO 'ports' TABLE
   * ===================================================================
   */
  const char *port_sql =
    "INSERT INTO ports ("
    "  number, name, sector, size, techlevel, type, invisible, "
    "  ore_on_hand, organics_on_hand, equipment_on_hand, petty_cash "
    ") VALUES (" "  ?1, ?2, ?3, ?4, ?5, ?6, 0, "                                                                                                                                                                                /* Params 1-6 */
    "  ?7, ?8, ?9, ?10 "        /* New goods_on_hand and petty_cash params */
    ");";
  if (sqlite3_prepare_v2 (db, port_sql, -1, &port_stmt, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_full_port: ports prepare failed: %s\n",
               sqlite3_errmsg (db));
      rc = sqlite3_errcode (db);
      goto rollback;
    }
  /* Bind all 10 parameters */
  sqlite3_bind_int (port_stmt, 1, port_number);
  sqlite3_bind_text (port_stmt, 2, base_name, -1, SQLITE_STATIC);
  sqlite3_bind_int (port_stmt, 3, sector);
  sqlite3_bind_int (port_stmt, 4, port_size);
  sqlite3_bind_int (port_stmt, 5, port_tech);
  sqlite3_bind_int (port_stmt, 6, type_id);
  /* Goods on Hand and Petty Cash */
  sqlite3_bind_int (port_stmt, 7, ore_on_hand_val);
  sqlite3_bind_int (port_stmt, 8, organics_on_hand_val);
  sqlite3_bind_int (port_stmt, 9, equipment_on_hand_val);
  sqlite3_bind_int (port_stmt, 10, petty_cash_val);
  if (sqlite3_step (port_stmt) != SQLITE_DONE)
    {
      fprintf (stderr, "create_full_port: ports insert failed: %s\n",
               sqlite3_errmsg (db));
      rc = sqlite3_errcode (db);
      goto rollback;
    }
  port_id = sqlite3_last_insert_rowid (db);
  sqlite3_finalize (port_stmt);
  port_stmt = NULL;
  // Create a bank account for the new port
  h_add_credits (db, "port", (int) port_id, port_credits, "DEPOSIT",
                 NULL, NULL);
  /*
   * ===================================================================
   * 4. COMMIT TRANSACTION
   * ===================================================================
   */
  rc = sqlite3_exec (db, "COMMIT", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "create_full_port: COMMIT failed: %s\n",
               sqlite3_errmsg (db));
      goto rollback;            /* Technically already rolled back, but good practice */
    }
  if (port_id_out)
    {
      *port_id_out = (int) port_id;
    }
  return SQLITE_OK;
/* --- Error Handling --- */
rollback:
  if (port_stmt)
    {
      sqlite3_finalize (port_stmt);
    }
  sqlite3_exec (db, "ROLLBACK", NULL, NULL, NULL);
  return rc;
}


int
create_ferringhi (int ferringhi_sector)
{
  if (ferringhi_sector == 0)
    {
      ferringhi_sector = 20;
    }                           // just make it a default value
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      fprintf (stderr, "create_ferringhi: Failed to get DB handle\n");
      return -1;
    }
  // Dynamically retrieve the shiptype IDs, filtered by can_purchase
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
  // Create the view to find the longest tunnels
  const char *create_view_sql =
    "CREATE VIEW IF NOT EXISTS longest_tunnels AS WITH all_sectors AS ( SELECT from_sector AS id FROM sector_warps UNION SELECT to_sector AS id FROM to_sector_warps ), outdeg AS ( SELECT a.id, COALESCE(COUNT(w.to_sector),0) AS deg FROM all_sectors a LEFT JOIN sector_warps w ON w.from_sector = a.id GROUP BY a.id ), edges AS ( SELECT from_sector, to_sector FROM sector_warps ), entry AS ( SELECT e.from_sector AS entry, e.to_sector AS next FROM edges e JOIN outdeg df ON df.id = e.from_sector AND df.deg > 1 JOIN outdeg dn ON dn.id = e.to_sector AND dn.deg = 1 ), rec(entry, curr, path, steps) AS ( SELECT entry, next, printf('%d->%d', entry, next), 1 FROM entry UNION ALL SELECT r.entry, e.to_sector, r.path || '->' || printf('%d', e.to_sector), r.steps + 1 FROM rec r JOIN edges e ON e.from_sector = r.curr JOIN outdeg d ON d.id = r.curr AND d.deg = 1 WHERE instr(r.path, '->' || printf('%d', e.to_sector) || '->') = 0 ) SELECT r.entry AS entry_sector, r.curr AS exit_sector, r.path AS tunnel_path, r.steps AS tunnel_length_edges FROM rec r JOIN outdeg d_exit ON d_exit.id = r.curr WHERE d_exit.deg <> 1 AND r.steps >= 2 ORDER BY r.steps DESC, r.entry, r.curr;";
  if (sqlite3_exec (db, create_view_sql, NULL, NULL, NULL) != SQLITE_OK)
    {
      fprintf (stderr,
               "create_ferringhi: Failed to create longest_tunnels view: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  int longest_tunnel_sector = 0;
  const char *q = "SELECT exit_sector FROM longest_tunnels LIMIT 1;";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, q, -1, &st, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_ferringhi prepare failed: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      longest_tunnel_sector = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  // --- Ferringhi Homeworld (num=2) Update ---
  char planet_sector[256];
  snprintf (planet_sector,
            sizeof (planet_sector),
            "UPDATE planets SET sector=%d, ore_on_hand=0, organics_on_hand=0, equipment_on_hand=0 where id=2;",
            longest_tunnel_sector);
  // Execute the variable that actually holds the SQL string: 'planet_sector'
  if (sqlite3_exec (db, planet_sector, NULL, NULL, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_ferringhi failed: %s\n", sqlite3_errmsg (db));
      return -1;
    }
  h_add_credits (db, "npc_planet", 2, 0, "DEPOSIT", NULL, NULL);
  // Insert the citadel details into the citadels table
  char sql_citadel[512];
  snprintf (sql_citadel,
            sizeof (sql_citadel),
            "INSERT INTO citadels (planet_id, level, shields, treasury, military) "
            "VALUES (%d, %d, %d, %d, 1);",
            2,
            (rand () % 2) + 2,
            // Citadel Level (2-3)
            (rand () % 1001) + 1000,
            // Shields (1k-2k)
            (rand () % 5000001) + 1000000);     // Credits (1M-6M)
  if (sqlite3_exec (db, sql_citadel, NULL, NULL, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_ferringhi failed to create citadel: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  // Dynamically build the INSERT statement for the player
  char player_insert_sql[512];
  char player_cols[256] = "";
  char player_vals[256] = "";
  // Add the core columns
  strncat (player_cols, "name, passwd, credits, sector",
           sizeof (player_cols) - strlen (player_cols) - 1);
  strncat (player_vals, "?, ?, ?, ?",
           sizeof (player_vals) - strlen (player_vals) - 1);
  // Add the 'type' and 'ship' columns if they exist in the schema
  if (has_column (db, "players", "type"))
    {
      strncat (player_cols, ", type",
               sizeof (player_cols) - strlen (player_cols) - 1);
      strncat (player_vals, ", ?",
               sizeof (player_vals) - strlen (player_vals) - 1);
    }
  if (has_column (db, "players", "ship"))
    {
      strncat (player_cols, ", ship",
               sizeof (player_cols) - strlen (player_cols) - 1);
      strncat (player_vals, ", ?",
               sizeof (player_vals) - strlen (player_vals) - 1);
    }
  snprintf (player_insert_sql, sizeof (player_insert_sql),
            "INSERT INTO players (%s) VALUES (%s);", player_cols,
            player_vals);
  sqlite3_stmt *player_ins = NULL;
  int rc_player_prepare =
    sqlite3_prepare_v2 (db, player_insert_sql, -1, &player_ins, NULL);
  if (rc_player_prepare != SQLITE_OK)
    {
      fprintf (stderr, "create_ferringhi prepare player insert failed: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  // Prepare the player update statement outside the loop for efficiency
  sqlite3_stmt *player_update = NULL;
  if (has_column (db, "players", "ship"))
    {
      const char *update_sql = "UPDATE players SET ship = ? WHERE id = ?;";
      int rc_update_prepare =
        sqlite3_prepare_v2 (db, update_sql, -1, &player_update, NULL);
      if (rc_update_prepare != SQLITE_OK)
        {
          fprintf (stderr,
                   "create_ferringhi prepare player update failed: %s\n",
                   sqlite3_errmsg (db));
          sqlite3_finalize (player_ins);
          return -1;
        }
    }
  // Seed 5 Ferrengi traders as both players and ships
  for (int i = 0; i < 5; i++)
    {
      int player_credits = (rand () % 10000) + 1000;
      char player_name[128];
      snprintf (player_name, sizeof (player_name), "Ferrengi Trader %d",
                i + 1);
      int b_player = 1;
      sqlite3_bind_text (player_ins, b_player++, player_name, -1,
                         SQLITE_STATIC);
      sqlite3_bind_text (player_ins, b_player++, "BOT", -1, SQLITE_STATIC);
      sqlite3_bind_int (player_ins, b_player++, player_credits);
      sqlite3_bind_int (player_ins, b_player++, longest_tunnel_sector);
      // Bind the type if the column exists
      if (has_column (db, "players", "type"))
        {
          sqlite3_bind_int (player_ins, b_player++, 1);
        }
      // Bind the ship ID, initially as 0, to be updated later
      if (has_column (db, "players", "ship"))
        {
          sqlite3_bind_int (player_ins, b_player++, 0); // Placeholder
        }
      if (sqlite3_step (player_ins) != SQLITE_DONE)
        {
          fprintf (stderr,
                   "create_ferringhi failed to create player %d: %s\n", i,
                   sqlite3_errmsg (db));
          sqlite3_finalize (player_ins);
          sqlite3_finalize (player_update);
          return -1;
        }
      sqlite3_reset (player_ins);
      // Get the ID of the new player
      sqlite3_int64 player_id = sqlite3_last_insert_rowid (db);
      // Randomly choose between a trader and a warship type
      int ship_type = (rand () % 100) <
                      80 ? merchant_freighter_id : ferengi_warship_id;                          // Use the dynamic IDs here
      int ship_fighters = (rand () % 1501) + 500;       // 500-2000 fighters
      int ship_shields = (rand () % 4001) + 1000;       // 1000-5000 shields
      int ship_holds = (rand () % 801) + 200;   // 200-1000 holds
      int mines_to_add = 5;
      int beacons_to_add = 4;
      int photons_to_add = 1;
      // --- NEW CODE FOR POPULATING HOLDS ---
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
      // Dynamically build the INSERT statement for the ship
      char insert_sql[1024];
      char cols[512] = "";
      char vals[512] = "";
      int ship_attack = 100;
      // The 'ships' table schema requires all these columns for a complete insert.
      // We are removing the redundant 'holds' column and renaming 'type' to 'type_id'.
      // We are also adding 'limpets', 'genesis', 'colonists', 'flags', 'ported', 'onplanet',
      // and the cloaking columns, providing default values where necessary.
      strncat (cols,
               "name, type_id, sector, fighters, shields, holds, ore, organics, equipment, limpets, genesis, colonists, flags, ported, onplanet",
               sizeof (cols) - strlen (cols) - 1);
      strncat (vals, "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?",
               sizeof (vals) - strlen (vals) - 1);
      // NOTE: We now explicitly check and bind the `attack`, `mines`, `beacons`, and `photons`
      // as they are optional / conditional.
      if (has_column (db, "ships", "attack"))
        {
          strncat (cols, ", attack", sizeof (cols) - strlen (cols) - 1);
          strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
        }
      if (has_column (db, "ships", "mines"))
        {
          strncat (cols, ", mines", sizeof (cols) - strlen (cols) - 1);
          strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
        }
      if (has_column (db, "ships", "beacons"))
        {
          strncat (cols, ", beacons", sizeof (cols) - strlen (cols) - 1);
          strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
        }
      if (has_column (db, "ships", "photons"))
        {
          strncat (cols, ", photons", sizeof (cols) - strlen (cols) - 1);
          strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
        }
      // Add the cloaking fields if they exist, to ensure proper schema upgrade compatibility.
      // If you are certain these columns exist, you can add them to the core list above.
      if (has_column (db, "ships", "cloaking_devices"))
        {
          strncat (cols, ", cloaking_devices",
                   sizeof (cols) - strlen (cols) - 1);
          strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
        }
      if (has_column (db, "ships", "cloaked"))
        {
          strncat (cols, ", cloaked", sizeof (cols) - strlen (cols) - 1);
          strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
        }
      snprintf (insert_sql, sizeof (insert_sql),
                "INSERT INTO ships (%s) VALUES (%s);", cols, vals);
      sqlite3_stmt *ins = NULL;
      int rc = sqlite3_prepare_v2 (db, insert_sql, -1, &ins, NULL);
      if (rc != SQLITE_OK)
        {
          fprintf (stderr, "create_ferringhi prepare insert failed: %s\n",
                   sqlite3_errmsg (db));
          sqlite3_finalize (player_ins);
          sqlite3_finalize (player_update);
          return -1;
        }
      int b = 1;
      // 1. Core Columns (15 parameters)
      sqlite3_bind_text (ins, b++, "Ferrengi Trader", -1, SQLITE_STATIC);
      sqlite3_bind_int (ins, b++, ship_type);   // Renamed 'type' to 'type_id'
      sqlite3_bind_int (ins, b++, longest_tunnel_sector);
      sqlite3_bind_int (ins, b++, ship_fighters);
      sqlite3_bind_int (ins, b++, ship_shields);
      // We are binding the max holds to the current holds to start
      // The ship should begin with 0 current cargo, but needs max holds for player to see capacity.
      // Since Ferengi Trader will likely start with 0 cargo, we should bind 0 to holds.
      // Assuming 'holds_to_fill' is the number of cargo holds the ship starts with (0 in most cases).
      sqlite3_bind_int (ins, b++, holds_to_fill);       // This should be 0 for a new ship (holds)
      sqlite3_bind_int (ins, b++, 0);   // Cargo begins here
      sqlite3_bind_int (ins, b++, 0);
      sqlite3_bind_int (ins, b++, 0);   // Cargo ends here
      sqlite3_bind_int (ins, b++, 0);   // limpets (New, default 0)
      sqlite3_bind_int (ins, b++, 0);   // genesis (New, default 0)
      sqlite3_bind_int (ins, b++, 0);   // colonists (New, default 0)
      sqlite3_bind_int (ins, b++, 777); // flags (New, default 777 for claimable derelicts)
      sqlite3_bind_int (ins, b++, 0);   // ported (New, default 0)
      sqlite3_bind_int (ins, b++, 1);   // onplanet (Always 1 for new ship on starting planet)
      // 2. Conditional Columns
      if (has_column (db, "ships", "attack"))
        {
          sqlite3_bind_int (ins, b++, ship_attack);     // Changed to bind ship_attack
        }
      if (has_column (db, "ships", "mines"))
        {
          sqlite3_bind_int (ins, b++, mines_to_add);
        }
      if (has_column (db, "ships", "beacons"))
        {
          sqlite3_bind_int (ins, b++, beacons_to_add);
        }
      if (has_column (db, "ships", "photons"))
        {
          sqlite3_bind_int (ins, b++, photons_to_add);
        }
      // 3. Cloaking Columns
      if (has_column (db, "ships", "cloaking_devices"))
        {
          sqlite3_bind_int (ins, b++, 0); // New, default 0
        }
      if (has_column (db, "ships", "cloaked"))
        {
          sqlite3_bind_null (ins, b++); // New, default NULL
        }
      if (sqlite3_step (ins) != SQLITE_DONE)
        {
          fprintf (stderr, "create_ferringhi failed to create ship %d: %s\n",
                   i, sqlite3_errmsg (db));
          sqlite3_finalize (ins);
          sqlite3_finalize (player_ins);
          sqlite3_finalize (player_update);
          return -1;
        }
      sqlite3_finalize (ins);
      // Get the ID of the newly created ship
      sqlite3_int64 ship_id = sqlite3_last_insert_rowid (db);
      // Update the player with their new ship ID
      if (player_update)
        {
          sqlite3_bind_int (player_update, 1, ship_id);
          sqlite3_bind_int (player_update, 2, player_id);
          if (sqlite3_step (player_update) != SQLITE_DONE)
            {
              fprintf (stderr,
                       "create_ferringhi failed to link player %lld with ship %lld: %s\n",
                       player_id,
                       ship_id,
                       sqlite3_errmsg (db));
              sqlite3_finalize (player_ins);
              sqlite3_finalize (player_update);
              return -1;
            }
          sqlite3_reset (player_update);
        }
    }
  sqlite3_finalize (player_ins);
  sqlite3_finalize (player_update);
  /* Insert Orion Syndicate Outpost */
  int oso_tunnel = 0;
  const char *oso =
    "SELECT exit_sector FROM longest_tunnels LIMIT 1 OFFSET 2;";
  sqlite3_stmt *stoso = NULL;
  int rc = 0;                   // Variable to capture return code
  if (sqlite3_prepare_v2 (db, oso, -1, &stoso, NULL) != SQLITE_OK)      // <-- FIXED: Changed 'q' to 'oso'
    {
      fprintf (stderr, " Orion Syndicate prepare failed: %s\n",
               sqlite3_errmsg (db));
      // Do not return -1 here; let it fall through and use the default sector 22.
    }
  else
    {
      rc = sqlite3_step (stoso);        // Only call step once
      if (rc == SQLITE_ROW)
        {
          oso_tunnel = sqlite3_column_int (stoso, 0);
        }
      else if (rc != SQLITE_DONE)
        {
          fprintf (stderr, " Orion Syndicate step failed: %s\n",
                   sqlite3_errmsg (db));
        }
      sqlite3_finalize (stoso);
    }
  // --- Ferringhi Homeworld (num=2) Update ---
  if (longest_tunnel_sector == oso_tunnel || oso_tunnel < 20)
    {
      oso_tunnel = (rand () % (999 - 11 + 1)) + 11;
    }
  // --- Ferrengi Homeworld (num=2) Update ---
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
  if (sqlite3_exec (db, planet_sector_sql, NULL, NULL, NULL) != SQLITE_OK)
    {
      LOGE ("Ferringhi DB Issue: %s", sqlite3_errmsg (db));
      fprintf (stderr, "create_ferringhi failed: %s\n", sqlite3_errmsg (db));
      return -1;
    }
  // --- Orion Syndicate Outpost (num=3) Update ---
  char oso_planet_sector[1024];
  snprintf (oso_planet_sector,
            sizeof (oso_planet_sector),
            "UPDATE planets SET sector=%d, ore_on_hand=0, organics_on_hand=0, equipment_on_hand=0 WHERE id=3; "
            "INSERT INTO sector_assets (sector, player,offensive_setting, asset_type, corporation, quantity, deployed_at) "
            "VALUES (%d, 4, 2, 2, 1, 50000, CAST(strftime('%%s','now') AS INTEGER)); "
            "INSERT INTO sector_assets (sector, player, asset_type, corporation, quantity, deployed_at) "
            "VALUES (%d, 4, 1, 1, 250,   CAST(strftime('%%s','now') AS INTEGER));",
            oso_tunnel,
            oso_tunnel,
            oso_tunnel);
  if (sqlite3_exec (db, oso_planet_sector, NULL, NULL, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create Orion Syndicate failed: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  h_add_credits (db, "npc_planet", 3, 0, "DEPOSIT", NULL, NULL);
  /* Place the new Black Market Port in the Orion Hideout Sector (Planet num=3) */
  char oso_port_sector[256];
  snprintf (oso_port_sector,
            sizeof (oso_port_sector),
            "DELETE FROM ports where sector=%d; INSERT INTO ports (sector, type, name) values (%d, 10, 'Orion Black Market');",
            oso_tunnel,
            oso_tunnel);
  if (sqlite3_exec (db, oso_port_sector, NULL, NULL, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create Orion Syndicate portfailed: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  // --- FIX: Create a bank account for the newly created Orion port ---
  int orion_port_id = (int) sqlite3_last_insert_rowid (db);
  if (orion_port_id > 0)
    {
      h_add_credits (db, "port", orion_port_id, 2000000, "DEPOSIT", NULL,
                     NULL);
      LOGD ("Created bank account for Orion Black Market port with ID: %d",
            orion_port_id);
    }
  // --- END FIX ---
  fprintf (stderr,
           "BIGBANG: Placed Ferringhi at sector %d (end of a long tunnel).\nBIGBANG: Placed Orion Syndicate at sector %d (end of a long tunnel).\n",
           longest_tunnel_sector,
           oso_tunnel);
  return 0;
}


// In a fully relational design, these should ideally be fetched from the shiptypes table.
// We are adding 'attack' here with a high default value.
struct ImperialStats
{
  int fighters;
  int shields;
  int holds;
  int photons;
  int genesis;
  int attack;                   // <- The new required column
} imperial_stats = {
  .fighters = 32000,
  .shields = 65000,
  .holds = 100,
  .photons = 100,
  .genesis = 10,
  .attack = 5000                // High attack rating for Imperial ship
};


int
create_imperial (void)
{
  int rc;
  pthread_mutex_lock (&db_mutex);
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      fprintf (stderr, "create_imperial: Failed to get DB handle\n");
      pthread_mutex_unlock (&db_mutex);
      return -1;
    }
  struct twconfig *cfg = config_load ();
  if (!cfg)
    {
      fprintf (stderr, "create_imperial: could not load config\n");
      pthread_mutex_unlock (&db_mutex);
      return -1;
    }
  // Dynamically retrieve the NPC shiptype ID, filtered by can_purchase = 0
  int imperial_starship_id =
    get_npc_shiptype_id_by_name (db, "Imperial Starship (NPC)");
  if (imperial_starship_id == -1)
    {
      fprintf (stderr,
               "create_imperial: Failed to get required NPC shiptype ID.\n");
      free (cfg);
      pthread_mutex_unlock (&db_mutex);
      return -1;
    }
  // Randomly place the Imperial Starship in a sector
  int imperial_sector = 11 + (rand () % (cfg->default_nodes - 10));
  free (cfg);
  // --- 1. Create the "Imperial" player (NPC) entry ---
  char sql_player[512];
  snprintf (sql_player,
            sizeof (sql_player),
            "INSERT INTO players (name, passwd, credits, type) VALUES ('Imperial Starship', 'BOT', \
10000000, 1);");
  // Large credits and high alignment
  if (sqlite3_exec (db, sql_player, NULL, NULL, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_imperial failed to create player: %s\n",
               sqlite3_errmsg (db));
      pthread_mutex_unlock (&db_mutex);
      return -1;
    }
  sqlite3_int64 imperial_player_id = sqlite3_last_insert_rowid (db);
  // --- 2. Create the "Imperial Starship" ship entry linked to the player ---
  // Using a prepared statement for safety and to correctly bind all new and existing columns.
  sqlite3_stmt *ins = NULL;
  // Fixed SQL: Removed binding to non-existent owner column, mapped correct columns
  const char *sql_ship_insert =
    "INSERT INTO ships (name, type_id, sector, fighters, shields, holds, photons, genesis, attack, hull, perms, flags) "
    "VALUES ('Imperial Starship', ?, ?, ?, ?, ?, ?, ?, ?, 10000, 731, 777);";
  rc = sqlite3_prepare_v2 (db, sql_ship_insert, -1, &ins, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "create_imperial: Failed to prepare ship INSERT: %s\n",
               sqlite3_errmsg (db));
      pthread_mutex_unlock (&db_mutex);
      return -1;
    }
  // Bind the ship properties
  int b = 1;
  // (1) type (FK to shiptypes)
  sqlite3_bind_int (ins, b++, imperial_starship_id);
  // (2) location
  sqlite3_bind_int (ins, b++, imperial_sector);
  // (3) fighters (initial value from struct)
  sqlite3_bind_int (ins, b++, imperial_stats.fighters);
  // (4) shields (initial value from struct)
  sqlite3_bind_int (ins, b++, imperial_stats.shields);
  // (5) holds (initial value from struct)
  sqlite3_bind_int (ins, b++, imperial_stats.holds);
  // (6) photons (initial value from struct)
  sqlite3_bind_int (ins, b++, imperial_stats.photons);
  // (7) genesis (initial value from struct)
  sqlite3_bind_int (ins, b++, imperial_stats.genesis);
  // (8) attack (initial value from struct)
  sqlite3_bind_int (ins, b++, imperial_stats.attack);
  // Execute the ship insert
  rc = sqlite3_step (ins);
  if (rc != SQLITE_DONE)
    {
      fprintf (stderr, "create_imperial failed to create ship: %s\n",
               sqlite3_errmsg (db));
      sqlite3_finalize (ins);
      pthread_mutex_unlock (&db_mutex);
      return -1;
    }
  sqlite3_int64 imperial_ship_id = sqlite3_last_insert_rowid (db);
  sqlite3_finalize (ins);       // Clean up the prepared statement
  // --- 3. Link the ship ID back to the player entry AND create ownership ---
  char sql_update[512];
  snprintf (sql_update, sizeof (sql_update),
            "UPDATE players SET ship=%lld WHERE id=%lld;",
            (long long) imperial_ship_id, (long long) imperial_player_id);
  if (sqlite3_exec (db, sql_update, NULL, NULL, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_imperial failed to update player ship: %s\n",
               sqlite3_errmsg (db));
      pthread_mutex_unlock (&db_mutex);
      return -1;
    }
  // Create ownership record
  char sql_own[512];
  snprintf (sql_own,
            sizeof (sql_own),
            "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES (%lld, %lld, 1, 1);",
            (long long) imperial_ship_id,
            (long long) imperial_player_id);
  if (sqlite3_exec (db, sql_own, NULL, NULL, NULL) != SQLITE_OK)
    {
      fprintf (stderr,
               "create_imperial failed to insert ownership: %s\n",
               sqlite3_errmsg (db));
      // non-fatal, but bad
    }
  fprintf (stderr,
           "BIGBANG: Imperial Starship placed at sector %d.\n",
           imperial_sector);
  pthread_mutex_unlock (&db_mutex);
  return 0;
}


int
create_taverns (void)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return -1;
    }
  // 1. Get a random tavern name ID
  int tavern_name_id = 0;
  sqlite3_stmt *st_name = NULL;
  const char *sql_get_name =
    "SELECT id FROM tavern_names ORDER BY RANDOM() LIMIT 1;";
  if (sqlite3_prepare_v2 (db, sql_get_name, -1, &st_name, NULL) != SQLITE_OK)
    {
      fprintf (stderr,
               "create_taverns: Failed to prepare name statement: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  if (sqlite3_step (st_name) == SQLITE_ROW)
    {
      tavern_name_id = sqlite3_column_int (st_name, 0);
    }
  else
    {
      fprintf (stderr,
               "create_taverns: No tavern names found in database.\n");
      sqlite3_finalize (st_name);
      return -1;
    }
  sqlite3_finalize (st_name);
  if (tavern_name_id == 0)
    {
      return -1;                // Should not happen if data is seeded
    }
  // 2. Find Stardock sector_id
  int stardock_sector_id = 0;
  sqlite3_stmt *st_stardock = NULL;
  const char *sql_get_stardock =
    "SELECT sector FROM ports WHERE type = 9 LIMIT 1;";
  if (sqlite3_prepare_v2 (db, sql_get_stardock, -1, &st_stardock, NULL) !=
      SQLITE_OK)
    {
      fprintf (stderr,
               "create_taverns: Failed to prepare stardock statement: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  if (sqlite3_step (st_stardock) == SQLITE_ROW)
    {
      stardock_sector_id = sqlite3_column_int (st_stardock, 0);
    }
  sqlite3_finalize (st_stardock);
  if (stardock_sector_id == 0)
    {
      fprintf (stderr, "create_taverns: Stardock not found.\n");
      return -1;
    }
  // 3. Find Orion Black Market sector_id (port type 10)
  int orion_sector_id = 0;
  sqlite3_stmt *st_orion = NULL;
  const char *sql_get_orion =
    "SELECT sector FROM ports WHERE type = 10 LIMIT 1;";
  if (sqlite3_prepare_v2 (db, sql_get_orion, -1, &st_orion, NULL) !=
      SQLITE_OK)
    {
      fprintf (stderr,
               "create_taverns: Failed to prepare orion statement: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  if (sqlite3_step (st_orion) == SQLITE_ROW)
    {
      orion_sector_id = sqlite3_column_int (st_orion, 0);
    }
  sqlite3_finalize (st_orion);
  if (orion_sector_id == 0)
    {
      fprintf (stderr,
               "create_taverns: Orion Black Market port not found.\n");
    }
  // 4. Insert taverns into the taverns table
  const char *sql_insert_tavern =
    "INSERT OR IGNORE INTO taverns (sector_id, name_id, enabled) VALUES (?, ?, 1);";
  sqlite3_stmt *st_insert = NULL;
  int rc = 0;
  // Insert Stardock tavern
  if (sqlite3_prepare_v2 (db, sql_insert_tavern, -1, &st_insert, NULL) !=
      SQLITE_OK)
    {
      fprintf (stderr,
               "create_taverns: Failed to prepare insert statement: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  sqlite3_bind_int (st_insert, 1, stardock_sector_id);
  sqlite3_bind_int (st_insert, 2, tavern_name_id);
  rc = sqlite3_step (st_insert);
  if (rc != SQLITE_DONE)
    {
      fprintf (stderr,
               "create_taverns: Failed to insert Stardock tavern: %s\n",
               sqlite3_errmsg (db));
      sqlite3_finalize (st_insert);
      return -1;
    }
  sqlite3_reset (st_insert);
  // Insert Orion Black Market tavern (if found)
  if (orion_sector_id != 0)
    {
      sqlite3_bind_int (st_insert, 1, orion_sector_id);
      sqlite3_bind_int (st_insert, 2, tavern_name_id);  // Use the same name for simplicity
      rc = sqlite3_step (st_insert);
      if (rc != SQLITE_DONE)
        {
          fprintf (stderr,
                   "create_taverns: Failed to insert Orion tavern: %s\n",
                   sqlite3_errmsg (db));
          sqlite3_finalize (st_insert);
          return -1;
        }
    }
  sqlite3_finalize (st_insert);
  fprintf (stderr, "BIGBANG: Created taverns in sectors %d and %d.\n",
           stardock_sector_id, orion_sector_id);
  return 0;
}


/* /\* ---------- small helpers ---------- *\/ */


/*
   static int
   prepare_first_ok (sqlite3 *db, sqlite3_stmt **stmt,
                  const char *const *candidates)
   {
   for (int i = 0; candidates[i]; ++i)
    {
      if (sqlite3_prepare_v2 (db, candidates[i], -1, stmt, NULL) == SQLITE_OK)
        return SQLITE_OK;
    }
   return SQLITE_ERROR;
   }
 */


/* /\* Check if a sector exists *\/ */


/* static int */


/* sector_exists (sqlite3 *db, int sector_id) */


/* { */


/*   static sqlite3_stmt *st = NULL; */


/*   int rc; */


/*   if (!st) */


/*     { */


/*       rc = */


/*      sqlite3_prepare_v2 (db, */


/*                          "SELECT 1 FROM sectors WHERE id = ?1 LIMIT 1;", */


/*                          -1, &st, NULL); */


/*       if (rc != SQLITE_OK) */


/*      return 0; */


/*     } */


/*   sqlite3_reset (st); */


/*   sqlite3_clear_bindings (st); */


/*   sqlite3_bind_int (st, 1, sector_id); */


/*   rc = sqlite3_step (st); */


/*   return (rc == SQLITE_ROW); */


/* } */


/* /\* random int in [lo, hi] inclusive *\/ */


/* static int */


/* rand_incl (int lo, int hi) */


/* { */


/*   return lo + (int) (rand () % (hi - lo + 1)); */


/* } */


/* Ensure there are at least N exits from Fedspace (2..10) to [outer_min..outer_max].
   If fewer exist, create more (and optionally the return edge). */
static int
ensure_fedspace_exit (sqlite3 *db, int outer_min, int outer_max,
                      int add_return_edge)
{
  if (!db)
    {
      return SQLITE_ERROR;
    }
  const int required_exits = 3; /* <- change this if you want a different minimum */
  const int max_attempts = 100; /* avoid infinite loops on tiny maps */
  int rc = SQLITE_OK;
  sqlite3_stmt *st_count = NULL;
  sqlite3_stmt *st_ins = NULL;
  /* Prepare COUNT(*) of existing exits 2..10 -> [outer_min..outer_max] */
  rc = sqlite3_prepare_v2 (db,
                           "SELECT COUNT(*) "
                           "FROM sector_warps "
                           "WHERE from_sector BETWEEN 2 AND 10 "
                           "  AND to_sector   BETWEEN ?1 AND ?2;",
                           -1, &st_count, NULL);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
  sqlite3_bind_int (st_count, 1, outer_min);
  sqlite3_bind_int (st_count, 2, outer_max);
  rc = sqlite3_step (st_count);
  int have = (rc == SQLITE_ROW) ? sqlite3_column_int (st_count, 0) : 0;
  sqlite3_reset (st_count);
  if (have >= required_exits)
    {
      rc = SQLITE_OK;
      goto done;
    }
  /* Prepare INSERT (idempotent) */
  rc = sqlite3_prepare_v2 (db,
                           "INSERT OR IGNORE INTO sector_warps(from_sector,to_sector) VALUES(?,?);",
                           -1,
                           &st_ins,
                           NULL);
  if (rc != SQLITE_OK)
    {
      goto done;
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
      sqlite3_clear_bindings (st_ins);
      sqlite3_bind_int (st_ins, 1, from);
      sqlite3_bind_int (st_ins, 2, to);
      int rc1 = sqlite3_step (st_ins);
      sqlite3_reset (st_ins);
      if (rc1)
        {
        }                       //stop the compiler complaining
      /* Optional return edge (keeps it safe from one-way pruning) */
      if (add_return_edge)
        {
          sqlite3_clear_bindings (st_ins);
          sqlite3_bind_int (st_ins, 1, to);
          sqlite3_bind_int (st_ins, 2, from);
          (void) sqlite3_step (st_ins);
          sqlite3_reset (st_ins);
        }
      /* Re-count to see if we actually increased the number of exits */
      sqlite3_reset (st_count);
      rc = sqlite3_step (st_count);
      have = (rc == SQLITE_ROW) ? sqlite3_column_int (st_count, 0) : have;
      sqlite3_reset (st_count);
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
  rc = SQLITE_OK;
done:
  if (st_ins)
    {
      sqlite3_finalize (st_ins);
    }
  if (st_count)
    {
      sqlite3_finalize (st_count);
    }
  return rc;
}


/* A simple utility to check if a column exists. */
static bool
has_column (sqlite3 *db, const char *table, const char *column)
{
  char sql[256];
  snprintf (sql, sizeof (sql), "PRAGMA table_info(%s);", table);
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      return false;
    }
  bool found = false;
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      if (strcmp ((const char *) sqlite3_column_text (st, 1), column) == 0)
        {
          found = true;
          break;
        }
    }
  sqlite3_finalize (st);
  return found;
}


//////////////////////////////////////////


int
create_derelicts (void)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      fprintf (stderr, "[create_derelicts] no DB handle\n");
      return 1;
    }
  int rc = 0;
  char *errmsg = NULL;
  /* Seed RNG once per process. */
  static int seeded = 0;
  if (!seeded)
    {
      srand ((unsigned) time (NULL));
      seeded = 1;
    }
  /* Get all shiptypes (UPDATED to include maxattack, maxlimpets, maxgenesis) */
  const char *sql_select =
    "SELECT id, name, maxholds, maxfighters, maxshields, maxmines, maxphotons, maxbeacons, maxattack, maxlimpets, maxgenesis "
    "FROM shiptypes WHERE can_purchase=1 AND id NOT IN (18);";
  sqlite3_stmt *sel = NULL;
  rc = sqlite3_prepare_v2 (db, sql_select, -1, &sel, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr,
               "[create_derelicts] shiptypes SELECT prepare failed: %s\n",
               sqlite3_errmsg (db));
      return 1;
    }
  /* Dynamically build the INSERT statement based on existing columns in your schema */
  char insert_sql[1024];
  char cols[512] = "";
  char vals[512] = "";
  // Add the core columns that are always present
  // FIX: 'type' is now 'type_id'
  strncat (cols, "name, type_id", sizeof (cols) - strlen (cols) - 1);
  strncat (vals, "?, ?", sizeof (vals) - strlen (vals) - 1);
  // DYNAMIC FIX: Check for the 'attack' column and add it if present
  if (has_column (db, "ships", "attack"))
    {
      strncat (cols, ", attack", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  // sector, holds, shields are essential and should be added
  if (has_column (db, "ships", "sector"))
    {
      strncat (cols, ", sector", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "holds"))
    {
      strncat (cols, ", holds", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "shields"))
    {
      strncat (cols, ", shields", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  // Equipment and Cargo
  if (has_column (db, "ships", "fighters"))
    {
      strncat (cols, ", fighters", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "organics"))
    {
      strncat (cols, ", organics", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "equipment"))
    {
      strncat (cols, ", equipment", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "ore"))
    {
      strncat (cols, ", ore", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "colonists"))
    {
      strncat (cols, ", colonists", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  // Weapons and Special Items
  // NEW: limpets must be handled
  if (has_column (db, "ships", "limpets"))
    {
      strncat (cols, ", limpets", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "mines"))
    {
      strncat (cols, ", mines", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "genesis"))
    {
      strncat (cols, ", genesis", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "photons"))
    {
      strncat (cols, ", photons", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "beacons"))
    {
      strncat (cols, ", beacons", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  // Add columns with a fixed default value (flags, ported, onplanet are present in new schema)
  strncat (cols, ", flags, ported, onplanet",
           sizeof (cols) - strlen (cols) - 1);
  strncat (vals, ", ?, ?, ?", sizeof (vals) - strlen (vals) - 1);
  // cloaking_devices and cloaked are also new in the schema
  if (has_column (db, "ships", "cloaking_devices"))
    {
      strncat (cols, ", cloaking_devices", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "cloaked"))
    {
      strncat (cols, ", cloaked", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "perms"))
    {
      strncat (cols, ", perms", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  snprintf (insert_sql, sizeof (insert_sql),
            "INSERT INTO ships (%s) VALUES (%s);", cols, vals);
  sqlite3_stmt *ins = NULL;
  rc = sqlite3_prepare_v2 (db, insert_sql, -1, &ins, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "[create_derelicts] insert prepare failed: %s\n",
               sqlite3_errmsg (db));
      sqlite3_finalize (sel);
      return 1;
    }
  rc = sqlite3_exec (db, "BEGIN IMMEDIATE TRANSACTION;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "[create_derelicts] begin transaction failed: %s\n",
               errmsg ? errmsg : "(unknown)");
      sqlite3_free (errmsg);
      sqlite3_finalize (ins);
      sqlite3_finalize (sel);
      return 1;
    }
  /* Insert one derelict for each shiptype */
  while ((rc = sqlite3_step (sel)) == SQLITE_ROW)
    {
      int st_id = sqlite3_column_int (sel, 0);
      const unsigned char *stype_name_uc = sqlite3_column_text (sel, 1);
      const char *stype_name =
        (const char *) (stype_name_uc ? (const char *) stype_name_uc : "");
      /* Build display name */
      char dname[128];
      char rnd[128];
      if (db_rand_npc_shipname (rnd, sizeof rnd) == SQLITE_OK && rnd[0])
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
      int max_holds = sqlite3_column_int (sel, 2);
      int max_fighters = sqlite3_column_int (sel, 3);
      int max_shields = sqlite3_column_int (sel, 4);
      int max_mines = sqlite3_column_int (sel, 5);
      int max_photons = sqlite3_column_int (sel, 6);
      int max_beacons = sqlite3_column_int (sel, 7);
      int max_attack = sqlite3_column_int (sel, 8);     // NEW: maxattack
      int max_limpets = sqlite3_column_int (sel, 9);    // NEW: maxlimpets
      int max_genesis = sqlite3_column_int (sel, 10);   // NEW: maxgenesis
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
      // NEW: Calculations for new item types
      int attack_to_add = (int) (max_attack * ((float) fill_pct / 100.0));
      int limpets_to_add = (int) (max_limpets * ((float) fill_pct / 100.0));
      int genesis_to_add = (int) (max_genesis * ((float) fill_pct / 100.0));
      /* Bind parameters in the correct order */
      int b = 1;
      // 1. Core columns
      sqlite3_bind_text (ins, b++, dname, -1, SQLITE_TRANSIENT);
      sqlite3_bind_int (ins, b++, st_id);       // FIX: Bind to type_id
      // 2. Dynamic bindings that were added to cols/vals
      if (has_column (db, "ships", "attack"))
        {
          sqlite3_bind_int (ins, b++, attack_to_add);   // NEW: Use calculated attack
        }
      if (has_column (db, "ships", "sector"))
        {
          sqlite3_bind_int (ins, b++, sector);
        }
      if (has_column (db, "ships", "holds"))
        {
          sqlite3_bind_int (ins, b++, holds);
        }
      if (has_column (db, "ships", "shields"))
        {
          sqlite3_bind_int (ins, b++, shields_to_add);
        }
      if (has_column (db, "ships", "fighters"))
        {
          sqlite3_bind_int (ins, b++, fighters_to_add);
        }
      if (has_column (db, "ships", "organics"))
        {
          sqlite3_bind_int (ins, b++, 0); // No organics in derelict
        }
      if (has_column (db, "ships", "equipment"))
        {
          sqlite3_bind_int (ins, b++, 0); // No equipment in derelict
        }
      if (has_column (db, "ships", "ore"))
        {
          sqlite3_bind_int (ins, b++, 0); // No ore in derelict
        }
      if (has_column (db, "ships", "colonists"))
        {
          sqlite3_bind_int (ins, b++, 0); // No colonists in derelict
        }
      if (has_column (db, "ships", "limpets"))
        {
          sqlite3_bind_int (ins, b++, limpets_to_add);  // NEW: Bind limpets
        }
      if (has_column (db, "ships", "mines"))
        {
          sqlite3_bind_int (ins, b++, mines_to_add);
        }
      if (has_column (db, "ships", "genesis"))
        {
          sqlite3_bind_int (ins, b++, genesis_to_add);  // FIX: Use calculated genesis
        }
      if (has_column (db, "ships", "photons"))
        {
          sqlite3_bind_int (ins, b++, photons_to_add);
        }
      if (has_column (db, "ships", "beacons"))
        {
          sqlite3_bind_int (ins, b++, beacons_to_add);
        }
      if (has_column (db, "ships", "perms"))
        {
          sqlite3_bind_int (ins, b++, 777);
        }
      // 3. Fixed value columns
      sqlite3_bind_int (ins, b++, 0);   /* flags (NPC derelict) */
      sqlite3_bind_int (ins, b++, 0);   /* ported (not ported) */
      sqlite3_bind_int (ins, b++, 0);   /* onplanet (not on planet) */
      // 4. Cloaking columns
      if (has_column (db, "ships", "cloaking_devices"))
        {
          sqlite3_bind_int (ins, b++, 0); // No cloaking devices on derelict
        }
      if (has_column (db, "ships", "cloaked"))
        {
          sqlite3_bind_int (ins, b++, 0); // Not cloaked
        }
      int irc = sqlite3_step (ins);
      if (irc != SQLITE_DONE)
        {
          fprintf (stderr,
                   "[create_derelicts] insert failed for shiptype %d (%s) into sector %d: %s\n",
                   st_id,
                   dname,
                   sector,
                   sqlite3_errmsg (db));
          sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
          sqlite3_finalize (ins);
          sqlite3_finalize (sel);
          return 1;
        }
      sqlite3_reset (ins);
    }
  /* Normalise loop exit */
  if (rc == SQLITE_DONE)
    {
      rc = SQLITE_OK;
    }
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "[create_derelicts] shiptypes SELECT failed: %s\n",
               sqlite3_errmsg (db));
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      sqlite3_finalize (ins);
      sqlite3_finalize (sel);
      return 1;
    }
  /* Rename all ships of type 18 to "Mary Celeste" */
  {
    sqlite3_stmt *st = NULL;
    int urc = sqlite3_prepare_v2 (db,
                                  "UPDATE ships SET name=? WHERE type_id=?;",
                                  -1,
                                  &st,
                                  NULL);                                                                // FIX: Changed type to type_id
    if (urc == SQLITE_OK)
      {
        sqlite3_bind_text (st, 1, "Mary Celeste", -1, SQLITE_STATIC);
        sqlite3_bind_int (st, 2, 18);
        (void) sqlite3_step (st);
      }
    sqlite3_finalize (st);
  }
  /* success path */
  rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "[create_derelicts] COMMIT failed: %s\n",
               errmsg ? errmsg : "(unknown)");
      sqlite3_free (errmsg);
      sqlite3_finalize (ins);
      sqlite3_finalize (sel);
      return 1;
    }
  sqlite3_finalize (ins);
  sqlite3_finalize (sel);
  return 0;
}


static int
ensure_all_sectors_have_exits (sqlite3 *db)
{
  const char *sql_select_all = "SELECT id FROM sectors;";
  const char *sql_select_outgoing =
    "SELECT COUNT(*) FROM sector_warps WHERE from_sector=?;";
  const char *sql_select_incoming =
    "SELECT from_sector FROM sector_warps WHERE to_sector=?;";                                  // This line was the problem
  const char *sql_insert_warp =
    "INSERT INTO sector_warps (from_sector, to_sector) VALUES (?, ?);";
  sqlite3_stmt *stmt_all = NULL;
  sqlite3_stmt *stmt_outgoing = NULL;
  sqlite3_stmt *stmt_incoming = NULL;
  sqlite3_stmt *stmt_insert = NULL;
  int rc;
  int sectors_fixed = 0;
  sqlite3_exec (db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
  rc = sqlite3_prepare_v2 (db, sql_select_all, -1, &stmt_all, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  rc = sqlite3_prepare_v2 (db, sql_select_outgoing, -1, &stmt_outgoing, NULL);
  if (rc != SQLITE_OK)
    {
      sqlite3_finalize (stmt_all);
      return rc;
    }
  rc = sqlite3_prepare_v2 (db, sql_select_incoming, -1, &stmt_incoming, NULL);
  if (rc != SQLITE_OK)
    {
      sqlite3_finalize (stmt_all);
      sqlite3_finalize (stmt_outgoing);
      return rc;
    }
  rc = sqlite3_prepare_v2 (db, sql_insert_warp, -1, &stmt_insert, NULL);
  if (rc != SQLITE_OK)
    {
      sqlite3_finalize (stmt_all);
      sqlite3_finalize (stmt_outgoing);
      sqlite3_finalize (stmt_incoming);
      return rc;
    }
  while ((rc = sqlite3_step (stmt_all)) == SQLITE_ROW)
    {
      int sector_id = sqlite3_column_int (stmt_all, 0);
      // Check if the sector has any outgoing warps
      sqlite3_bind_int (stmt_outgoing, 1, sector_id);
      sqlite3_step (stmt_outgoing);
      int outgoing_count = sqlite3_column_int (stmt_outgoing, 0);
      sqlite3_reset (stmt_outgoing);
      if (outgoing_count == 0)
        {
          // This is a one-way trap. Find a sector that warps here.
          int from_sector_id = -1;
          sqlite3_bind_int (stmt_incoming, 1, sector_id);
          if (sqlite3_step (stmt_incoming) == SQLITE_ROW)
            {
              from_sector_id = sqlite3_column_int (stmt_incoming, 0);
            }
          sqlite3_reset (stmt_incoming);
          if (from_sector_id != -1)
            {
              // Create a return warp back to the originating sector
              //  printf("Sector %d is a one-way trap. Adding a return warp to sector %d.\n", sector_id, from_sector_id);
              sqlite3_bind_int (stmt_insert, 1, sector_id);
              sqlite3_bind_int (stmt_insert, 2, from_sector_id);
              sqlite3_step (stmt_insert);
              sqlite3_reset (stmt_insert);
              sectors_fixed++;
            }
        }
    }
  sqlite3_finalize (stmt_all);
  sqlite3_finalize (stmt_outgoing);
  sqlite3_finalize (stmt_incoming);
  sqlite3_finalize (stmt_insert);
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
  printf ("BIGBANG: Fixed %d one-way sectors.\n", sectors_fixed);
  return SQLITE_OK;
}


#include <sqlite3.h>
#include <stdio.h>
#include <time.h>
#include <string.h>


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
  sqlite3 *handle = db_get_handle ();   // Assuming db_get_handle() is available
  sqlite3_stmt *select_stmt = NULL;
  sqlite3_stmt *insert_stmt = NULL;
  int rc;
  // --- 1. Prepare SELECT statement: Get sectors with existing beacon text.
  // We only need the sector ID to create the ownership record.
  const char *select_sql =
    "SELECT id FROM sectors WHERE beacon IS NOT NULL AND beacon != '';";
  if (sqlite3_prepare_v2 (handle, select_sql, -1, &select_stmt, NULL) !=
      SQLITE_OK)
    {
      fprintf (stderr,
               "BIGBANG ERROR: Failed to prepare SELECT beacon query: %s\n",
               sqlite3_errmsg (handle));
      return -1;
    }
  // --- 2. Prepare INSERT statement: Insert into sector_assets (OWNERSHIP ONLY)
  // Using your schema: no 'content' column.
  // player=0 (System), asset_type=1 (Beacon), quantity=1, ttl=NULL.
  const char *insert_sql =
    "INSERT INTO sector_assets (sector, player, asset_type, quantity, ttl, deployed_at) VALUES (?, 0, 1, 1, NULL, strftime('%s', 'now'));";
  if (sqlite3_prepare_v2 (handle, insert_sql, -1, &insert_stmt, NULL) !=
      SQLITE_OK)
    {
      fprintf (stderr,
               "BIGBANG ERROR: Failed to prepare INSERT asset query: %s\n",
               sqlite3_errmsg (handle));
      sqlite3_finalize (select_stmt);
      return -1;
    }
  // --- 3. Loop through sectors and insert assets
  int total_beacons = 0;
  while ((rc = sqlite3_step (select_stmt)) == SQLITE_ROW)
    {
      int sector_id = sqlite3_column_int (select_stmt, 0);
      // Reset the INSERT statement for re-use and bind sector ID
      sqlite3_reset (insert_stmt);
      sqlite3_bind_int (insert_stmt, 1, sector_id);
      // No need to bind beacon text since we are not storing it in sector_assets
      // Execute the INSERT
      if (sqlite3_step (insert_stmt) != SQLITE_DONE)
        {
          fprintf (stderr,
                   "BIGBANG ERROR: Failed to insert asset ownership for sector %d: %s\n",
                   sector_id,
                   sqlite3_errmsg (handle));
          sqlite3_finalize (select_stmt);
          sqlite3_finalize (insert_stmt);
          return -1;
        }
      total_beacons++;
    }
  // Check for error in sqlite3_step (not an end of row condition)
  if (rc != SQLITE_DONE)
    {
      fprintf (stderr, "BIGBANG ERROR: SELECT processing failed: %s\n",
               sqlite3_errmsg (handle));
      sqlite3_finalize (select_stmt);
      sqlite3_finalize (insert_stmt);
      return -1;
    }
  // --- 4. Finalize statements.
  sqlite3_finalize (select_stmt);
  sqlite3_finalize (insert_stmt);
  // NOTE: We DO NOT clear the 'sectors.beacon' column here, as existing functions
  // depend on the text remaining there. We only inserted the ownership metadata.
  fprintf (stderr,
           "BIGBANG: Migrated ownership for %d system beacons successfully.\n",
           total_beacons);
  return 0;
}


int
create_ports (void)
{
  sqlite3 *db = db_get_handle ();
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
  /* Stardock (Port 1) */
  int stardock_sector = 0;
  if (numSectors > 11)
    {
      stardock_sector = (rand () % (numSectors - 10)) + 11;
    }
  else
    {
      stardock_sector = 11;
    }
  /*
   * ===================================================================
   * REFACTORED SQL: This query now includes all columns from fullschema.sql
   * ===================================================================
   */
  const char *port_sql =
    "INSERT INTO ports ("
    "  number, name, sector, size, techlevel, type, invisible, "
    "  ore_on_hand, organics_on_hand, equipment_on_hand, petty_cash "
    ") VALUES (" "  ?1, ?2, ?3, ?4, ?5, ?6, 0, "                                                                                                                                                                                /* Params 1-6 */
    "  ?7, ?8, ?9, ?10 "        /* New goods_on_hand and petty_cash params */
    ");";
  sqlite3_stmt *port_stmt;
  if (sqlite3_prepare_v2 (db, port_sql, -1, &port_stmt, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_ports prepare failed: %s\n",
               sqlite3_errmsg (db));
      return -1;
    }
  /*
   * REFACTORED SQL: This query is now correct.
   * The 'maxproduct' column does exist in your schema.
   */
  const char *trade_sql =
    "INSERT INTO port_trade (port_id, commodity, mode, maxproduct) VALUES (?, ?, ?, ?);";
  sqlite3_stmt *trade_stmt;
  if (sqlite3_prepare_v2 (db, trade_sql, -1, &trade_stmt, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_ports prepare failed for trade data: %s\n",
               sqlite3_errmsg (db));
      sqlite3_finalize (port_stmt);
      return -1;
    }
  /* --- Create and populate Stardock --- */
  /* Bind base port values */
  sqlite3_bind_int (port_stmt, 1, 1);
  sqlite3_bind_text (port_stmt, 2, "Stardock", -1, SQLITE_STATIC);
  sqlite3_bind_int (port_stmt, 3, stardock_sector);
  sqlite3_bind_int (port_stmt, 4, 10);
  sqlite3_bind_int (port_stmt, 5, 5);
  sqlite3_bind_int (port_stmt, 6, 9);   /* Type ID for Stardock */
  /* Bind new commodity values (Type 9: Buy/Sell all) */
  int start_stock = 25000;      // Use start_stock for initial on_hand values
  int petty_cash_val = 0;       // Assuming petty_cash starts at 0 for Stardock
  sqlite3_bind_int (port_stmt, 7, start_stock); // ore_on_hand
  sqlite3_bind_int (port_stmt, 8, start_stock); // organics_on_hand
  sqlite3_bind_int (port_stmt, 9, start_stock); // equipment_on_hand
  sqlite3_bind_int (port_stmt, 10, petty_cash_val);     // petty_cash
  if (sqlite3_step (port_stmt) != SQLITE_DONE)
    {
      fprintf (stderr, "create_ports failed (Stardock): %s\n",
               sqlite3_errmsg (db));
      sqlite3_finalize (port_stmt);
      sqlite3_finalize (trade_stmt);
      return -1;
    }
  sqlite3_reset (port_stmt);
  sqlite3_int64 stardock_id = sqlite3_last_insert_rowid (db);
  /*
   * ===================================================================
   * REFACTORED LOGIC: Stardock (Type 9) BUYS *AND* SELLS.
   * This is the fix for your "Port is not selling" test failure.
   * ===================================================================
   */
  int maxproduct_amount = 50000;
  /* --- Ore (Buy & Sell) --- */
  sqlite3_bind_int (trade_stmt, 1, stardock_id);
  sqlite3_bind_text (trade_stmt, 2, "ore", -1, SQLITE_STATIC);
  sqlite3_bind_text (trade_stmt, 3, "buy", -1, SQLITE_STATIC);
  sqlite3_bind_int (trade_stmt, 4, maxproduct_amount);
  sqlite3_step (trade_stmt);
  sqlite3_reset (trade_stmt);
  sqlite3_bind_int (trade_stmt, 1, stardock_id);
  sqlite3_bind_text (trade_stmt, 2, "ore", -1, SQLITE_STATIC);
  sqlite3_bind_text (trade_stmt, 3, "sell", -1, SQLITE_STATIC);
  sqlite3_bind_int (trade_stmt, 4, maxproduct_amount);
  sqlite3_step (trade_stmt);
  sqlite3_reset (trade_stmt);
  /* --- Organics (Buy & Sell) --- */
  sqlite3_bind_int (trade_stmt, 1, stardock_id);
  sqlite3_bind_text (trade_stmt, 2, "organics", -1, SQLITE_STATIC);
  sqlite3_bind_text (trade_stmt, 3, "buy", -1, SQLITE_STATIC);
  sqlite3_bind_int (trade_stmt, 4, maxproduct_amount);
  sqlite3_step (trade_stmt);
  sqlite3_reset (trade_stmt);
  sqlite3_bind_int (trade_stmt, 1, stardock_id);
  sqlite3_bind_text (trade_stmt, 2, "organics", -1, SQLITE_STATIC);
  sqlite3_bind_text (trade_stmt, 3, "sell", -1, SQLITE_STATIC);
  sqlite3_bind_int (trade_stmt, 4, maxproduct_amount);
  sqlite3_step (trade_stmt);
  sqlite3_reset (trade_stmt);
  /* --- Equipment (Buy & Sell) --- */
  sqlite3_bind_int (trade_stmt, 1, stardock_id);
  sqlite3_bind_text (trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
  sqlite3_bind_text (trade_stmt, 3, "buy", -1, SQLITE_STATIC);
  sqlite3_bind_int (trade_stmt, 4, maxproduct_amount);
  sqlite3_step (trade_stmt);
  sqlite3_reset (trade_stmt);
  sqlite3_bind_int (trade_stmt, 1, stardock_id);
  sqlite3_bind_text (trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
  sqlite3_bind_text (trade_stmt, 3, "sell", -1, SQLITE_STATIC);
  sqlite3_bind_int (trade_stmt, 4, maxproduct_amount);
  sqlite3_step (trade_stmt);
  sqlite3_reset (trade_stmt);
  /*
   * ===================================================================
   * --- Create and populate the rest of the ports ---
   * WARNING: The function 'create_full_port' MUST also be updated
   * to use the new 'ports' table schema, just as we did for Stardock.
   * ===================================================================
   */
  {
    sqlite3 *dbh = db_get_handle ();
    if (!dbh)
      {
        fprintf (stderr, "create_ports: no DB handle\n");
        return -1;
      }
    for (int i = 2; i <= maxPorts; i++)
      {
        /* pick a sector that is not the Stardock sector and not in the first 10 */
        int sector;
        do
          {
            sector = 11 + (rand () % (numSectors - 10));
          }
        while (sector == stardock_sector);
        /* pick random non-Stardock type 1..8 */
        int type_id = random_port_type_1_to_8 ();
        /* name: use your generator, special-case Ferrengi home */
        randomname (name_buffer);
        if (i == 7)
          {
            snprintf (name_buffer, sizeof (name_buffer), "Ferrengi Home");
          }
        /*
         * This function MUST be refactored to populate all new columns in 'ports'
         * (product_ore, max_ore, etc.) and 'port_trade' (buy/sell modes).
         */
        int port_id = 0;
        int rc = create_full_port (dbh, sector, /*port number */ i,
                                   /*base name */ name_buffer,
                                   /*type */ type_id,
                                   &port_id);
        if (rc != SQLITE_OK)
          {
            fprintf (stderr,
                     "create_ports failed at %d (create_full_port rc=%d)\n",
                     i, rc);
            return -1;
          }
        /* optional: tweak Ferrengi Home stats after insert to match your old special-case */
        if (i == 7)
          {
            sqlite3_stmt *adj = NULL;
            /* This UPDATE is now incomplete. It should also set prices/stock. */
            const char *sql =
              "UPDATE ports SET size=?, techlevel=? WHERE id=?";
            if (sqlite3_prepare_v2 (dbh, sql, -1, &adj, NULL) == SQLITE_OK)
              {
                sqlite3_bind_int (adj, 1, 10);  /* size */
                sqlite3_bind_int (adj, 2, 5);   /* techlevel */
                sqlite3_bind_int (adj, 3, port_id);
                (void) sqlite3_step (adj);
              }
            sqlite3_finalize (adj);
          }
      }
  }
  sqlite3_finalize (port_stmt);
  sqlite3_finalize (trade_stmt);
  fprintf (stderr,
           "BIGBANG: Stardock at sector %d, plus %d normal ports\n",
           stardock_sector, maxPorts - 1);
  return 0;
}


static int
fix_traps_with_pathcheck (sqlite3 *db, int fedspace_max)
{
  int rc;
  int max_id = 0;
  sqlite3_stmt *st = NULL;
  rc = sqlite3_prepare_v2 (db, "SELECT MAX(id) FROM sectors", -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      max_id = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  if (max_id <= fedspace_max)
    {
      return SQLITE_OK;
    }
  /* Pre-prepare insert for escape warps */
  rc = sqlite3_prepare_v2 (db,
                           "INSERT OR IGNORE INTO sector_warps(from_sector,to_sector) VALUES(?1,?2)",
                           -1,
                           &st,
                           NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  for (int s = fedspace_max + 1; s <= max_id; ++s)
    {
      /* Is there a path from s back to sector 1? */
      int has_path = db_path_exists (db, s, 1);
      if (has_path < 0)
        {
          sqlite3_finalize (st);
          return SQLITE_ERROR;
        }
      if (!has_path)
        {
          /* No path home: add an escape warp back into FedSpace */
          int escape = 1 + (rand () % fedspace_max);
          sqlite3_reset (st);
          sqlite3_clear_bindings (st);
          sqlite3_bind_int (st, 1, s);
          sqlite3_bind_int (st, 2, escape);
          sqlite3_step (st);
        }
    }
  sqlite3_finalize (st);
  return SQLITE_OK;
}

