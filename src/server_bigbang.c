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


/* ----------------------------------------------------
 * Tunables
 * ---------------------------------------------------- */
/* Define constants for tunnel creation */
#define MIN_TUNNELS_TARGET          15	/* minimum tunnels you want */
#define MIN_TUNNEL_LEN              4	/* count tubes of length >= this */
#define TUNNEL_REFILL_MAX_ATTEMPTS  60	/* stop trying after this many passes */

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


static int ensure_fedspace_exit (sqlite3 * db, int outer_min, int outer_max,
				 int add_return_edge);

static const char *SQL_INSERT_WARP =
  "INSERT OR IGNORE INTO sector_warps(from_sector, to_sector) VALUES(?,?)";

static const char *SQL_INSERT_USED_SECTOR =
  "INSERT INTO used_sectors(used) VALUES(?)";

static int get_out_degree (sqlite3 * db, int sector);
static int insert_warp_unique (sqlite3 * db, int from, int to);
static int create_random_warps (sqlite3 * db, int numSectors, int maxWarps);
int create_imperial (void);	// Declared here for the compiler
static int ensure_all_sectors_have_exits (sqlite3 * db);
int create_ownership (void);
static bool has_column (sqlite3 * db, const char *table, const char *column);

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
    "       planet_type_count " "FROM config WHERE id=1;";



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
			       -1, &chk, NULL);
  if (rc != SQLITE_OK)
    return -1;
  sqlite3_bind_int (chk, 1, from);
  sqlite3_bind_int (chk, 2, to);
  rc = sqlite3_step (chk);
  sqlite3_finalize (chk);
  if (rc == SQLITE_ROW)
    return 0;			/* exists */

  rc = sqlite3_prepare_v2 (db,
			   "INSERT INTO sector_warps(from_sector,to_sector) VALUES(?,?);",
			   -1, &ins, NULL);
  if (rc != SQLITE_OK)
    return -1;
  sqlite3_bind_int (ins, 1, from);
  sqlite3_bind_int (ins, 2, to);
  rc = sqlite3_step (ins);
  sqlite3_finalize (ins);
  if (rc != SQLITE_DONE)
    return -1;
  return 1;
}

static int
sector_degree (sqlite3 *db, int s)
{
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db,
			       "SELECT COUNT(*) FROM sector_warps WHERE from_sector=?;",
			       -1, &st, NULL);
  if (rc != SQLITE_OK)
    return -1;
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
    return -1;
  sqlite3_stmt *st = NULL;
  int rc =
    sqlite3_prepare_v2 (db, "SELECT COUNT(*) FROM sectors;", -1, &st, NULL);
  if (rc != SQLITE_OK)
    return -1;
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
	used = 1;
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
	sqlite3_free (errmsg);
      return -1;
    }

  for (int s = 11; s <= numSectors; s++)
    {
      if (is_sector_used (db, s))
	continue;		/* don't touch tunnels */

      if ((rand () % 100) < DEFAULT_PERCENT_DEADEND)
	continue;		/* skip dead-ends */

      int targetWarps = 1 + (rand () % maxWarps);
      int deg = sector_degree (db, s);
      if (deg < 0)
	goto fail;

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
	    goto fail;

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
	sqlite3_free (errmsg);
      return -1;
    }
  if (errmsg)
    sqlite3_free (errmsg);
  return 0;

fail:
  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
  if (errmsg)
    sqlite3_free (errmsg);
  return -1;
}

/* ----------------------------------------------------
 * Ensure every non-tunnel sector has at least one exit
 * ---------------------------------------------------- */
int
ensure_sector_exits (sqlite3 *db, int numSectors)
{
  if (!db || numSectors <= 0)
    return -1;

  sqlite3_stmt *q_count = NULL, *q_in = NULL, *ins = NULL;
  int rc = sqlite3_prepare_v2 (db,
			       "SELECT COUNT(1) FROM sector_warps WHERE from_sector=?",
			       -1, &q_count, NULL);
  if (rc != SQLITE_OK)
    goto done;

  rc = sqlite3_prepare_v2 (db,
			   "SELECT from_sector FROM sector_warps WHERE to_sector=? ORDER BY RANDOM() LIMIT 1;",
			   -1, &q_in, NULL);
  if (rc != SQLITE_OK)
    goto done;

  rc = sqlite3_prepare_v2 (db,
			   "INSERT OR IGNORE INTO sector_warps(from_sector,to_sector) VALUES(?,?);",
			   -1, &ins, NULL);
  if (rc != SQLITE_OK)
    goto done;

  for (int s = 11; s <= numSectors; s++)
    {
      if (is_sector_used (db, s))
	continue;		/* don't add exits FROM tunnel nodes */

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
	continue;

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
		to = 0;
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
    sqlite3_finalize (q_count);
  if (q_in)
    sqlite3_finalize (q_in);
  if (ins)
    sqlite3_finalize (ins);
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
    return -1;

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
	sqlite3_free (errmsg);
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
      int path_len = 4 + (rand () % 5);	/* 4..8 */
      int nodes[12];
      int n = 0;

      /* choose distinct nodes, avoiding already-used tunnel nodes */
      while (n < path_len)
	{
	  int s = 11 + (rand () % (sector_count - 10));
	  int dup = 0;
	  for (int i = 0; i < n; i++)
	    if (nodes[i] == s)
	      {
		dup = 1;
		break;
	      }
	  if (dup || is_sector_used (db, s))
	    continue;
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
    return -1;

  int sector_count = get_sector_count ();

  struct twconfig *cfg = config_load ();

  if (!cfg)
    return -1;

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
	sqlite3_free (errmsg);
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
	sqlite3_free (errmsg);
      free (cfg);
      return -1;
    }

  free (cfg);
  return 0;
}

/* ----------------------------------------------------
 * Port creation (placeholder — keep your original behaviour)
 * ---------------------------------------------------- */
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

static int
random_port_type_1_to_8 (void)
{
  return 1 + (rand () % 8);
}

static int
seed_port_trade_rows (sqlite3 *db, int port_id, int type)
{
  (void) db;
  (void) port_id;
  (void) type;
  return 0;
}

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

  srand ((unsigned) time (NULL));

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
  // After all sectors/warps are generated:
  int ferringhi = db_chain_traps_and_bridge (10);	// 1..10 are FedSpace by convention
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

  fprintf (stderr, "BIGBANG: Creating Ferringhi home sector...\n");
  if (create_ferringhi (ferringhi) != 0)
    {
      free (cfg);
      return -1;
    }

  fprintf (stderr, "BIGBANG: Creating Imperial ship...\n");
  if (create_imperial () != 0)
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


/* Pick a random sector in [lo,hi] that isn't equal to 'avoid' and exists in sectors */
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

/************* Tunneling *******************/



int
count_edges ()
{
  sqlite3 *handle = db_get_handle ();
  if (!handle)
    return -1;

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

/* Reusable helper: insert A->B and B->A using the same prepared stmt. */
static int
insert_bidirectional (sqlite3 *db, sqlite3_stmt *ins, int a, int b)
{
  int rc;

  if (a == b)
    return SQLITE_OK;		/* ignore self-edge safely */

  /* A -> B */
  sqlite3_bind_int (ins, 1, a);
  sqlite3_bind_int (ins, 2, b);
  rc = sqlite3_step (ins);
  if (rc != SQLITE_DONE)
    {
      fprintf (stderr, "BIGBANG: INSERT %d->%d failed: %s\n", a, b,
	       sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_reset (ins);
  sqlite3_clear_bindings (ins);

  /* B -> A */
  sqlite3_bind_int (ins, 1, b);
  sqlite3_bind_int (ins, 2, a);
  rc = sqlite3_step (ins);
  if (rc != SQLITE_DONE)
    {
      fprintf (stderr, "BIGBANG: INSERT %d->%d failed: %s\n", b, a,
	       sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_reset (ins);
  sqlite3_clear_bindings (ins);

  return SQLITE_OK;
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




/* Insert a fully-populated port (non-Stardock), append trade code to name,
   and seed its port_trade rows. Returns SQLITE_OK on success and sets *out_port_id. */
static int
create_full_port (sqlite3 *dbh, int sector_id, int port_number,	/* visible number (can be sequential) */
		  const char *base_name,	/* e.g., "Port Greotua" */
		  int type_1_to_8, int *out_port_id)
{
  if (out_port_id)
    *out_port_id = 0;
  if (type_1_to_8 < 1 || type_1_to_8 > 8)
    type_1_to_8 = random_port_type_1_to_8 ();

  /* Build "Name (CODE)" into a small stack buffer */
  const char *code = trade_code_for_type (type_1_to_8);
  char name_buf[256];
  if (base_name && *base_name)
    {
      snprintf (name_buf, sizeof (name_buf), "%s (%s)", base_name, code);
    }
  else
    {
      snprintf (name_buf, sizeof (name_buf), "Port %d (%s)", port_number,
		code);
    }

  /* Insert port with defaults populated */
  const char *ins =
    "INSERT INTO ports "
    "(number, name, location, type, size, techlevel, "
    " max_ore, max_organics, max_equipment, "
    " product_ore, product_organics, product_equipment, "
    " credits, invisible) " "VALUES (?,?,?,?,?,?, ?,?,?, ?,?,?, ?,0)";

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (dbh, ins, -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_int (st, 1, port_number);
  sqlite3_bind_text (st, 2, name_buf, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 3, sector_id);
  sqlite3_bind_int (st, 4, type_1_to_8);
  sqlite3_bind_int (st, 5, DEF_PORT_SIZE);
  sqlite3_bind_int (st, 6, DEF_PORT_TECHLEVEL);
  sqlite3_bind_int (st, 7, DEF_PORT_MAX_ORE);
  sqlite3_bind_int (st, 8, DEF_PORT_MAX_ORG);
  sqlite3_bind_int (st, 9, DEF_PORT_MAX_EQU);
  sqlite3_bind_int (st, 10, DEF_PORT_PROD_ORE);
  sqlite3_bind_int (st, 11, DEF_PORT_PROD_ORG);
  sqlite3_bind_int (st, 12, DEF_PORT_PROD_EQU);
  sqlite3_bind_int (st, 13, DEF_PORT_CREDITS);

  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    return rc;

  int port_id = (int) sqlite3_last_insert_rowid (dbh);
  if (out_port_id)
    *out_port_id = port_id;

  /* Seed trade rows to match type */
  rc = seed_port_trade_rows (dbh, port_id, type_1_to_8);
  return rc;
}


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

/* Returns the number of existing warps from a sector. Returns -1 on error. */
static int
get_out_degree (sqlite3 *db, int sector)
{
  const char *q = "SELECT COUNT(*) FROM sector_warps WHERE from_sector=?;";
  sqlite3_stmt *st = NULL;

  if (sqlite3_prepare_v2 (db, q, -1, &st, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "get_out_degree prepare failed: %s\n",
	       sqlite3_errmsg (db));
      return -1;
    }
  sqlite3_bind_int (st, 1, sector);
  int deg = -1;
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      deg = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  return deg;
}




/* ----------------------------------------------------
 * Universe population functions
 * ---------------------------------------------------- */


int
create_planets (void)
{
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



  // Prepare statements for efficiency
  const char *port_sql =
    "INSERT INTO ports (number, name, location, size, techlevel, credits, type, invisible) VALUES (?, 'Port ' || ?, ?, ?, ?, ?, ?, 0);";
  sqlite3_stmt *port_stmt;
  if (sqlite3_prepare_v2 (db, port_sql, -1, &port_stmt, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_ports prepare failed: %s\n",
	       sqlite3_errmsg (db));
      return -1;
    }

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

  // --- Create and populate Stardock ---
  // Bind values for the Stardock port
  sqlite3_bind_int (port_stmt, 1, 1);
  sqlite3_bind_text (port_stmt, 2, "Stardock", -1, SQLITE_STATIC);
  sqlite3_bind_int (port_stmt, 3, stardock_sector);
  sqlite3_bind_int (port_stmt, 4, 10);
  sqlite3_bind_int (port_stmt, 5, 5);
  sqlite3_bind_int (port_stmt, 6, 1000000);
  sqlite3_bind_int (port_stmt, 7, 9);	// Type ID for Stardock

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

  // Populate trade data for Stardock (type 9: Buy all)
  int maxproduct_amount = 5000;
  sqlite3_bind_int (trade_stmt, 1, stardock_id);
  sqlite3_bind_text (trade_stmt, 2, "ore", -1, SQLITE_STATIC);
  sqlite3_bind_text (trade_stmt, 3, "buy", -1, SQLITE_STATIC);
  sqlite3_bind_int (trade_stmt, 4, maxproduct_amount);
  sqlite3_step (trade_stmt);
  sqlite3_reset (trade_stmt);

  sqlite3_bind_int (trade_stmt, 1, stardock_id);
  sqlite3_bind_text (trade_stmt, 2, "organics", -1, SQLITE_STATIC);
  sqlite3_bind_text (trade_stmt, 3, "buy", -1, SQLITE_STATIC);
  sqlite3_bind_int (trade_stmt, 4, maxproduct_amount);
  sqlite3_step (trade_stmt);
  sqlite3_reset (trade_stmt);

  sqlite3_bind_int (trade_stmt, 1, stardock_id);
  sqlite3_bind_text (trade_stmt, 2, "equipment", -1, SQLITE_STATIC);
  sqlite3_bind_text (trade_stmt, 3, "buy", -1, SQLITE_STATIC);
  sqlite3_bind_int (trade_stmt, 4, maxproduct_amount);
  sqlite3_step (trade_stmt);
  sqlite3_reset (trade_stmt);

  // --- Create and populate the rest of the ports ---
// --- Create and populate the rest of the ports ---
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

	/* create the fully-populated port row (also seeds port_trade rows) */
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
	    const char *sql =
	      "UPDATE ports SET size=?, techlevel=? WHERE id=?";
	    if (sqlite3_prepare_v2 (dbh, sql, -1, &adj, NULL) == SQLITE_OK)
	      {
		sqlite3_bind_int (adj, 1, 10);	/* size */
		sqlite3_bind_int (adj, 2, 5);	/* techlevel */
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


int
create_ferringhi (int ferringhi_sector)
{
  if (ferringhi_sector == 0)
    {
      ferringhi_sector = 20;
    }				// just make it a default value

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
  snprintf (planet_sector, sizeof (planet_sector),
	    "UPDATE planets SET sector=%d where id=2;",
	    longest_tunnel_sector);

  // Execute the variable that actually holds the SQL string: 'planet_sector'
  if (sqlite3_exec (db, planet_sector, NULL, NULL, NULL) != SQLITE_OK) 
    {
      fprintf (stderr, "create_ferringhi failed: %s\n", sqlite3_errmsg (db));
      return -1;
    }
  
  // Insert the citadel details into the citadels table
  char sql_citadel[512];
  snprintf (sql_citadel, sizeof (sql_citadel), "INSERT INTO citadels (planet_id, level, shields, treasury, military) " "VALUES (%d, %d, %d, %d, 1);", 2, (rand () % 2) + 2,	// Citadel Level (2-3)
	    (rand () % 1001) + 1000,	// Shields (1k-2k)
	    (rand () % 5000001) + 1000000);	// Credits (1M-6M)

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
	  sqlite3_bind_int (player_ins, b_player++, 0);	// Placeholder
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
      int ship_type = (rand () % 100) < 80 ? merchant_freighter_id : ferengi_warship_id;	// Use the dynamic IDs here
      int ship_fighters = (rand () % 1501) + 500;	// 500-2000 fighters
      int ship_shields = (rand () % 4001) + 1000;	// 1000-5000 shields
      int ship_holds = (rand () % 801) + 200;	// 200-1000 holds
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

      // Add the core columns that are always present
      strncat (cols,
	       "name, type, location, fighters, shields, holds, holds_used, ore, organics, equipment",
	       sizeof (cols) - strlen (cols) - 1);
      strncat (vals, "?, ?, ?, ?, ?, ?, ?, ?, ?, ?",
	       sizeof (vals) - strlen (vals) - 1);

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
      sqlite3_bind_text (ins, b++, "Ferrengi Trader", -1, SQLITE_STATIC);
      sqlite3_bind_int (ins, b++, ship_type);
      sqlite3_bind_int (ins, b++, longest_tunnel_sector);
      sqlite3_bind_int (ins, b++, ship_fighters);
      sqlite3_bind_int (ins, b++, ship_shields);
      sqlite3_bind_int (ins, b++, ship_holds);
      sqlite3_bind_int (ins, b++, holds_to_fill);
      sqlite3_bind_int (ins, b++, ore);
      sqlite3_bind_int (ins, b++, organics);
      sqlite3_bind_int (ins, b++, equipment);

      if (has_column (db, "ships", "attack"))
	sqlite3_bind_int (ins, b++, ship_fighters);
      if (has_column (db, "ships", "mines"))
	sqlite3_bind_int (ins, b++, mines_to_add);
      if (has_column (db, "ships", "beacons"))
	sqlite3_bind_int (ins, b++, beacons_to_add);
      if (has_column (db, "ships", "photons"))
	sqlite3_bind_int (ins, b++, photons_to_add);


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
		       player_id, ship_id, sqlite3_errmsg (db));
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
  const char *oso = "SELECT exit_sector FROM longest_tunnels LIMIT 1 OFFSET 2;";
  sqlite3_stmt *stoso = NULL;
  int rc = 0; // Variable to capture return code

  if (sqlite3_prepare_v2 (db, oso, -1, &stoso, NULL) != SQLITE_OK) // <-- FIXED: Changed 'q' to 'oso'
    {
      fprintf (stderr, " Orion Syndicate prepare failed: %s\n",
	       sqlite3_errmsg (db));
      // Do not return -1 here; let it fall through and use the default sector 22.
    }
  else
    {
      rc = sqlite3_step (stoso); // Only call step once

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
      oso_tunnel = (rand() % (999 - 11 + 1)) + 11;
    }


  // --- Ferrengi Homeworld (num=2) Update ---
  char planet_sector_sql[1024];
  snprintf(planet_sector_sql, sizeof(planet_sector_sql),
	   "UPDATE planets SET sector=%d WHERE id=2; "
	   "INSERT INTO sector_assets (sector, player, offensive_setting, asset_type, corporation, quantity, deployed_at) "
	   "VALUES (%d, 0, 3, 2, 2, 50000, CAST(strftime('%%s','now') AS INTEGER)); "
	   "INSERT INTO sector_assets (sector, player, asset_type, corporation, quantity, deployed_at) "
	   "VALUES (%d, 0, 1, 2, 250,   CAST(strftime('%%s','now') AS INTEGER));",
	   longest_tunnel_sector, longest_tunnel_sector, longest_tunnel_sector);

  if (sqlite3_exec(db, planet_sector_sql, NULL, NULL, NULL) != SQLITE_OK) {
    LOGE("Ferringhi DB Issue: %s", sqlite3_errmsg(db));
    fprintf(stderr, "create_ferringhi failed: %s\n", sqlite3_errmsg(db));
    return -1;
  }

  
  // --- Orion Syndicate Outpost (num=3) Update ---
  char oso_planet_sector[1024];
  snprintf(oso_planet_sector, sizeof(oso_planet_sector),
	   "UPDATE planets SET sector=%d WHERE id=3; "
	   "INSERT INTO sector_assets (sector, player,offensive_setting, asset_type, corporation, quantity, deployed_at) "
	   "VALUES (%d, 4, 2, 2, 1, 50000, CAST(strftime('%%s','now') AS INTEGER)); "
	   "INSERT INTO sector_assets (sector, player, asset_type, corporation, quantity, deployed_at) "
	   "VALUES (%d, 4, 1, 1, 250,   CAST(strftime('%%s','now') AS INTEGER));",
	   oso_tunnel, oso_tunnel, oso_tunnel);

  if (sqlite3_exec(db, oso_planet_sector, NULL, NULL, NULL) != SQLITE_OK) {
    fprintf(stderr, "create Orion Syndicate failed: %s\n", sqlite3_errmsg(db));
    return -1;
  }

  
 
 /* Place the new Black Market Port in the Orion Hideout Sector (Planet num=3) */                                              char oso_port_sector[256];
 snprintf (oso_port_sector, sizeof (oso_port_sector),
	   "DELETE FROM ports where location=%d; INSERT INTO ports (location, type, name) values (%d, 10, 'Orion Black Market');",
	   oso_tunnel,oso_tunnel);

 if (sqlite3_exec (db, oso_port_sector, NULL, NULL, NULL) != SQLITE_OK)
   {
     fprintf (stderr, "create Orion Syndicate portfailed: %s\n", sqlite3_errmsg (db));
     return -1;
   }
  
 
 
  fprintf (stderr,
	   "BIGBANG: Placed Ferringhi at sector %d (end of a long tunnel).\nBIGBANG: Placed Orion Syndicate at sector %d (end of a long tunnel).\n",
	   longest_tunnel_sector, oso_tunnel);

  return 0;
}



int
create_imperial (void)
{
  pthread_mutex_lock (&db_mutex);
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      fprintf (stderr, "create_imperial: Failed to get DB handle\n");
      return -1;
    }

  struct twconfig *cfg = config_load ();
  if (!cfg)
    {
      fprintf (stderr, "create_imperial: could not load config\n");
      return -1;
    }

  // Dynamically retrieve the NPC shiptype ID, filtered by can_purchase = 0
  int imperial_starship_id =
    get_npc_shiptype_id_by_name (db, "Imperial Starship (NPC)");

  if (imperial_starship_id == -1)
    {
      fprintf (stderr,
	       "create_imperial: Failed to get required NPC shiptype ID.\n");
      return -1;
    }
  // Randomly place the Imperial Starship in a sector
  int imperial_sector = 11 + (rand () % (cfg->default_nodes - 10));
  free (cfg);

  // Create the "Imperial" player (NPC) entry
  char sql_player[512];
  snprintf (sql_player, sizeof (sql_player), "INSERT INTO players (name, passwd, credits, type) VALUES ('Imperial Starship', 'BOT', 10000000, 1);");	// Large credits and high alignment
  if (sqlite3_exec (db, sql_player, NULL, NULL, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_imperial failed to create player: %s\n",
	       sqlite3_errmsg (db));
      return -1;
    }

  sqlite3_int64 imperial_player_id = sqlite3_last_insert_rowid (db);

  // Create the "Imperial Starship" ship entry linked to the player
  char sql_ship[512];
  snprintf (sql_ship, sizeof (sql_ship),
	    "INSERT INTO ships (name, type, location, fighters, shields, holds, photons, genesis) "
	    "VALUES ('Imperial Starship', %d, %d, 32000, 65000, 100, 100, 10);",
	    imperial_starship_id, imperial_sector);
  if (sqlite3_exec (db, sql_ship, NULL, NULL, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_imperial failed to create ship: %s\n",
	       sqlite3_errmsg (db));
      return -1;
    }
  sqlite3_int64 imperial_ship_id = sqlite3_last_insert_rowid (db);

  // Create the "Imperial Starship" ship entry linked to the player
  char sql_update[512];
  // Build the UPDATE into the right buffer with the right size
  snprintf (sql_update, sizeof (sql_update),
	    "UPDATE players SET ship=%lld WHERE id=%lld;",
	    (long long) imperial_ship_id, (long long) imperial_player_id);

  // Execute the UPDATE, not the previous INSERT
  if (sqlite3_exec (db, sql_update, NULL, NULL, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "create_imperial failed to update player ship: %s\n",
	       sqlite3_errmsg (db));
      return -1;
    }


  fprintf (stderr,
	   "BIGBANG: Imperial Starship placed at sector %d.\n",
	   imperial_sector);
  pthread_mutex_unlock (&db_mutex);

  return 0;
}

/* /\* ---------- small helpers ---------- *\/ */

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

/* Check if a sector exists */
static int
sector_exists (sqlite3 *db, int sector_id)
{
  static sqlite3_stmt *st = NULL;
  int rc;

  if (!st)
    {
      rc =
	sqlite3_prepare_v2 (db,
			    "SELECT 1 FROM sectors WHERE id = ?1 LIMIT 1;",
			    -1, &st, NULL);
      if (rc != SQLITE_OK)
	return 0;
    }
  sqlite3_reset (st);
  sqlite3_clear_bindings (st);
  sqlite3_bind_int (st, 1, sector_id);

  rc = sqlite3_step (st);
  return (rc == SQLITE_ROW);
}

/* random int in [lo, hi] inclusive */
static int
rand_incl (int lo, int hi)
{
  return lo + (int) (rand () % (hi - lo + 1));
}



/* Ensure there are at least N exits from Fedspace (2..10) to [outer_min..outer_max].
   If fewer exist, create more (and optionally the return edge). */
static int
ensure_fedspace_exit (sqlite3 *db, int outer_min, int outer_max,
		      int add_return_edge)
{
  if (!db)
    return SQLITE_ERROR;

  const int required_exits = 3;	/* <- change this if you want a different minimum */
  const int max_attempts = 100;	/* avoid infinite loops on tiny maps */

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
    goto done;

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
			   -1, &st_ins, NULL);
  if (rc != SQLITE_OK)
    goto done;

  /* Try to add exits until we reach required_exits or hit attempt cap */
  int attempts = 0;
  while (have < required_exits && attempts < max_attempts)
    {
      ++attempts;

      /* Pick random FedSpace source 2..10 and random outer destination [outer_min..outer_max] */
      int from = 2 + (rand () % 9);	/* 2..10 inclusive */
      int span = (outer_max >= outer_min) ? (outer_max - outer_min + 1) : 1;
      int to = outer_min + (rand () % span);

      if (to == from)
	to = (to < outer_max) ? (to + 1) : outer_min;	/* avoid self-edge */

      /* Insert forward edge */
      sqlite3_clear_bindings (st_ins);
      sqlite3_bind_int (st_ins, 1, from);
      sqlite3_bind_int (st_ins, 2, to);
      int rc1 = sqlite3_step (st_ins);
      sqlite3_reset (st_ins);

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
    sqlite3_finalize (st_ins);
  if (st_count)
    sqlite3_finalize (st_count);
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

  /* Dynamically build the INSERT statement based on existing columns in your schema */
  char insert_sql[1024];
  char cols[512] = "";
  char vals[512] = "";

  // Add the core columns that are always present
  strncat (cols, "name, type", sizeof (cols) - strlen (cols) - 1);
  strncat (vals, "?, ?", sizeof (vals) - strlen (vals) - 1);

  // DYNAMIC FIX: Check for the 'attack' column and add it if present
  if (has_column (db, "ships", "attack"))
    {
      strncat (cols, ", attack", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }

  // Add the remaining core columns
  strncat (cols, ", location, holds, fighters, shields",
	   sizeof (cols) - strlen (cols) - 1);
  strncat (vals, ", ?, ?, ?, ?", sizeof (vals) - strlen (vals) - 1);

  // Dynamic cargo and equipment columns based on your schema
  if (has_column (db, "ships", "holds_used"))
    {
      strncat (cols, ", holds_used", sizeof (cols) - strlen (cols) - 1);
      strncat (vals, ", ?", sizeof (vals) - strlen (vals) - 1);
    }
  if (has_column (db, "ships", "fighters_used"))
    {
      strncat (cols, ", fighters_used", sizeof (cols) - strlen (cols) - 1);
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


  // Add columns with a fixed default value
  strncat (cols, ", flags, ported, onplanet",
	   sizeof (cols) - strlen (cols) - 1);
  strncat (vals, ", ?, ?, ?", sizeof (vals) - strlen (vals) - 1);

  snprintf (insert_sql, sizeof (insert_sql),
	    "INSERT INTO ships (%s) VALUES (%s);", cols, vals);

  sqlite3_stmt *ins = NULL;
  rc = sqlite3_prepare_v2 (db, insert_sql, -1, &ins, NULL);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "[create_derelicts] prepare insert failed: %s\n",
	       sqlite3_errmsg (db));
      return 1;
    }

  /* Select statement based on your shiptypes schema */
  sqlite3_stmt *sel = NULL;
  const char *select_candidates[] = {
    "SELECT id, name, maxholds, maxfighters, maxshields FROM shiptypes;",
    NULL
  };
  rc = prepare_first_ok (db, &sel, select_candidates);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr,
	       "[create_derelicts] could not prepare shiptypes SELECT.\n");
      sqlite3_finalize (ins);
      return 1;
    }

  /*
   * BEGIN IMMEDIATE TRANSACTION:
   * This is a single transaction for atomicity and speed.
   * The duplicate BEGIN call was removed to fix the error.
   */
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
      // (const char *) (stype_name_uc ? stype_name_uc : "");
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

      /* Random fill percentage for “used” fields */
#define MIN_FILL_PCT 25
#define MAX_FILL_PCT 75
      int fill_pct =
	MIN_FILL_PCT + (rand () % (MAX_FILL_PCT - MIN_FILL_PCT + 1));
      int holds_used = (int) (max_holds * (fill_pct / 100.0));
      int fighters_used = (int) (max_fighters * (fill_pct / 100.0));
      int mines_to_add = 1 + (rand () % 10);
      int photons_to_add = 1;
      int beacons_to_add = 4;

      /* Bind in EXACTLY the same order as your INSERT column list */
      int b = 1;
      sqlite3_bind_text (ins, b++, dname, -1, SQLITE_TRANSIENT);	/* name */
      sqlite3_bind_int (ins, b++, st_id);	/* type */

      // CHANGE: Now binds the attack value to the ship's maximum fighter capacity
      if (has_column (db, "ships", "attack"))
	{
	  sqlite3_bind_int (ins, b++, max_fighters);
	}

      sqlite3_bind_int (ins, b++, sector);	/* location */
      sqlite3_bind_int (ins, b++, max_holds);	/* holds */
      sqlite3_bind_int (ins, b++, max_fighters);	/* fighters */
      sqlite3_bind_int (ins, b++, max_shields);	/* shields */

      if (has_column (db, "ships", "holds_used"))
	sqlite3_bind_int (ins, b++, holds_used);
      if (has_column (db, "ships", "fighters_used"))
	sqlite3_bind_int (ins, b++, fighters_used);
      if (has_column (db, "ships", "organics"))
	sqlite3_bind_int (ins, b++, 0);
      if (has_column (db, "ships", "equipment"))
	sqlite3_bind_int (ins, b++, 0);
      if (has_column (db, "ships", "ore"))
	sqlite3_bind_int (ins, b++, 0);
      if (has_column (db, "ships", "colonists"))
	sqlite3_bind_int (ins, b++, 0);
      if (has_column (db, "ships", "mines"))
	sqlite3_bind_int (ins, b++, mines_to_add);
      if (has_column (db, "ships", "genesis"))
	sqlite3_bind_int (ins, b++, 0);
      if (has_column (db, "ships", "photons"))
	sqlite3_bind_int (ins, b++, photons_to_add);
      if (has_column (db, "ships", "beacons"))
	sqlite3_bind_int (ins, b++, beacons_to_add);

      sqlite3_bind_int (ins, b++, 0);	/* flags */
      sqlite3_bind_int (ins, b++, 0);	/* ported */
      sqlite3_bind_int (ins, b++, 0);	/* onplanet */

      int irc = sqlite3_step (ins);
      if (irc != SQLITE_DONE)
	{
	  fprintf (stderr,
		   "[create_derelicts] insert failed for shiptype %d (%s) into sector %d: %s\n",
		   st_id, dname, sector, sqlite3_errmsg (db));
	  sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
	  sqlite3_finalize (ins);
	  sqlite3_finalize (sel);
	  return 1;
	}
      sqlite3_reset (ins);
    }

  /* Normalise loop exit */
  if (rc == SQLITE_DONE)
    rc = SQLITE_OK;
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
    int urc =
      sqlite3_prepare_v2 (db, "UPDATE ships SET name=? WHERE type=?;", -1,
			  &st, NULL);
    if (urc == SQLITE_OK)
      {
	sqlite3_bind_text (st, 1, "Mary Celeste", -1, SQLITE_STATIC);
	sqlite3_bind_int (st, 2, 18);
	urc = sqlite3_step (st);
      }
    sqlite3_finalize (st);
    if (urc != SQLITE_DONE)
      {
	fprintf (stderr, "[create_derelicts] rename type 18 failed: %s\n",
		 sqlite3_errmsg (db));
	sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
	sqlite3_finalize (ins);
	sqlite3_finalize (sel);
	return 1;
      }
  }

  /* Delete all ships with class 17 (NPC Imperial Warship) */
  rc =
    sqlite3_exec (db, "DELETE FROM ships WHERE type=17;", NULL, NULL,
		  &errmsg);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "[create_derelicts] DELETE type 17 failed: %s\n",
	       errmsg ? errmsg : "(unknown)");
      sqlite3_free (errmsg);
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      sqlite3_finalize (ins);
      sqlite3_finalize (sel);
      return 1;
    }

  /* Commit the transaction */
  rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      fprintf (stderr, "[create_derelicts] commit failed: %s\n",
	       errmsg ? errmsg : "(unknown)");
      sqlite3_free (errmsg);
      /* fall through to cleanup as error */
    }

  /* unified cleanup & error handling */
  if (rc != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      sqlite3_finalize (ins);
      sqlite3_finalize (sel);
      return 1;
    }

  /* success path */
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
  const char *sql_select_incoming = "SELECT from_sector FROM sector_warps WHERE to_sector=?;";	// This line was the problem
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
  sqlite3 *handle = db_get_handle ();	// Assuming db_get_handle() is available
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
		   sector_id, sqlite3_errmsg (handle));
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
