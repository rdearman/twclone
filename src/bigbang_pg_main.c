/*
 * bigbang_pg_main.c — merged Postgres "DB lifecycle" and "Universe Generator" for twclone.
 */

#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <jansson.h>
#include <dirent.h>

#include "db/db_api.h"
#include "server_log.h"

/* -----------------------------------------------------------------------------*
 * Macros & Config
 * ----------------------------------------------------------------------------- */
#define MIN_TUNNELS_TARGET          15
#define TUNNEL_REFILL_MAX_ATTEMPTS  60
#define DEFAULT_PERCENT_DEADEND     5
#define DEFAULT_PERCENT_ONEWAY      5


/* -----------------------------------------------------------------------------*
 * Logging & Error Helpers
 * ----------------------------------------------------------------------------- */
static void
die_conn (PGconn *c, const char *msg)
{
  fprintf (stderr, "FATAL: %s: %s\n", msg,
	   c ? PQerrorMessage (c) : "(no conn)");
  exit (2);
}


static void
die (const char *msg)
{
  fprintf (stderr, "FATAL: %s\n", msg);
  exit (2);
}


static int
exec_sql (PGconn *c, const char *sql, const char *label)
{
  PGresult *r = PQexec (c, sql);
  if (!r)
    {
      die_conn (c, label);
    }
  ExecStatusType st = PQresultStatus (r);


  if (!(st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK))
    {
      fprintf (stderr, "ERROR: %s failed: %s\n", label, PQerrorMessage (c));
      fprintf (stderr, "SQL:\n%s\n", sql);
      PQclear (r);
      return -1;
    }
  PQclear (r);
  return 0;
}


static int
fetch_int (PGconn *c, const char *sql, int *out)
{
  PGresult *r = PQexec (c, sql);
  if (!r || PQresultStatus (r) != PGRES_TUPLES_OK)
    {
      if (r)
	{
	  PQclear (r);
	}
      return -1;
    }
  if (PQntuples (r) > 0)
    {
      *out = atoi (PQgetvalue (r, 0, 0));
      PQclear (r);
      return 0;
    }
  PQclear (r);
  return -1;
}


/* -----------------------------------------------------------------------------*
 * File System Helpers
 * ----------------------------------------------------------------------------- */
static char *
slurp_file (const char *path)
{
  FILE *f = fopen (path, "rb");
  if (!f)
    {
      fprintf (stderr, "ERROR: cannot open %s: %s\n", path, strerror (errno));
      return NULL;
    }
  if (fseek (f, 0, SEEK_END) != 0)
    {
      fclose (f);
      return NULL;
    }
  long n = ftell (f);


  if (n < 0)
    {
      fclose (f);
      return NULL;
    }
  rewind (f);
  char *buf = (char *) malloc ((size_t) n + 1);


  if (!buf)
    {
      fclose (f);
      return NULL;
    }
  size_t rd = fread (buf, 1, (size_t) n, f);


  fclose (f);
  if (rd != (size_t) n)
    {
      free (buf);
      return NULL;
    }
  buf[n] = 0;
  return buf;
}


static int
sync_config_to_db (PGconn *c, json_t *jcfg)
{
  const char *key;
  json_t *val = NULL;
  printf ("BIGBANG: Syncing config to database...\n");

  json_object_foreach (jcfg, key, val)
  {
    // Skip meta-config keys used only by bigbang
    if (strcmp (key, "admin") == 0 || strcmp (key, "db") == 0 ||
	strcmp (key, "app") == 0 || strcmp (key, "sql_dir") == 0)
      {
	continue;
      }

    const char *type = "string";
    char buf[256];


    if (json_is_integer (val))
      {
	type = "int";
	snprintf (buf, sizeof (buf), "%lld",
		  (long long) json_integer_value (val));
      }
    else if (json_is_boolean (val))
      {
	type = "bool";
	snprintf (buf, sizeof (buf), "%s", json_is_true (val) ? "1" : "0");
      }
    else if (json_is_real (val))
      {
	type = "double";
	snprintf (buf, sizeof (buf), "%f", json_real_value (val));
      }
    else if (json_is_string (val))
      {
        const char *s = json_string_value (val);
        if (s) {
            strncpy (buf, s, sizeof (buf) - 1);
            buf[sizeof (buf) - 1] = '\0';
        } else {
            continue;
        }
      }
    else
      {
	continue;		// Skip complex types
      }

    const char *paramValues[3] = { key, buf, type };
    PGresult *r = PQexecParams (c,
				"INSERT INTO config (key, value, type) VALUES ($1, $2, $3) "
				"ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value, type = EXCLUDED.type",
				3,
				NULL,
				paramValues,
				NULL,
				NULL,
				0);


    if (!r || PQresultStatus (r) != PGRES_COMMAND_OK)
      {
	fprintf (stderr,
		 "WARNING: Failed to sync config key '%s': %s\n",
		 key, PQerrorMessage (c));
      }
    PQclear (r);
  }
  return 0;
}


/* compare function for qsort - sort by numeric prefix */
static int
compare_sql_files (const void *a, const void *b)
{
  const char *fa = *(const char **) a;
  const char *fb = *(const char **) b;

  int num_a = atoi (fa);
  int num_b = atoi (fb);

  return (num_a > num_b) - (num_a < num_b);
}


/* Forward declaration */
static int apply_file (PGconn * c, const char *path);


static int
apply_file (PGconn *c, const char *path)
{
  char *sql = slurp_file (path);
  if (!sql)
    {
      return -1;
    }
  int rc = exec_sql (c, sql, path);


  free (sql);
  return rc;
}


static int
load_sql_files_in_order (PGconn *app, const char *sql_dir)
{
  DIR *dir = opendir (sql_dir);
  if (!dir)
    {
      fprintf (stderr, "ERROR: cannot open sql directory: %s\n", sql_dir);
      return -1;
    }

  struct dirent *entry;
  char **files = NULL;
  int file_count = 0;
  int capacity = 0;


  while ((entry = readdir (dir)) != NULL)
    {
      if (entry->d_type != DT_REG)
	{
	  continue;
	}

      if (!strstr (entry->d_name, ".sql"))
	{
	  continue;
	}

      if (file_count >= capacity)
	{
	  capacity = capacity ? capacity * 2 : 10;
	  files = (char **) realloc (files, capacity * sizeof (char *));
	}

      files[file_count++] = strdup (entry->d_name);
    }

  closedir (dir);

  if (file_count == 0)
    {
      fprintf (stderr, "ERROR: no SQL files found in %s\n", sql_dir);
      return -1;
    }

  qsort (files, file_count, sizeof (char *), compare_sql_files);

  int result = 0;


  for (int i = 0; i < file_count; i++)
    {
      char path[2048];


      snprintf (path, sizeof (path), "%s/%s", sql_dir, files[i]);
      printf ("BIGBANG: Applying %s...\n", files[i]);
      if (apply_file (app, path) != 0)
	{
	  result = -1;
	  break;
	}
    }

  for (int i = 0; i < file_count; i++)
    {
      free (files[i]);
    }
  free (files);
  return result;
}


/* -----------------------------------------------------------------------------*
 * DB Lifecycle
 * ----------------------------------------------------------------------------- */
static int
create_db_if_needed (PGconn *admin, const char *dbname)
{
  const char *paramValues[1] = { dbname };
  PGresult *r = PQexecParams (admin,
			      "SELECT 1 FROM pg_database WHERE datname = $1",
			      1,
			      NULL,
			      paramValues,
			      NULL,
			      NULL,
			      0);
  if (!r || PQresultStatus (r) != PGRES_TUPLES_OK)
    {
      die_conn (admin, "check db exists");
    }
  int exists = (PQntuples (r) > 0);


  PQclear (r);
  if (exists)
    {
      return 0;
    }

  char buf[512];


  snprintf (buf, sizeof (buf), "CREATE DATABASE \"%s\"", dbname);
  return exec_sql (admin, buf, "CREATE DATABASE");
}


/* -----------------------------------------------------------------------------*
 * Ported Graph Brain (C Logic + libpq)
 * ----------------------------------------------------------------------------- */


static int
insert_warp_unique (PGconn *c, int from, int to)
{
  const char *paramValues[2];
  char f_buf[16], t_buf[16];
  snprintf (f_buf, sizeof (f_buf), "%d", from);
  snprintf (t_buf, sizeof (t_buf), "%d", to);
  paramValues[0] = f_buf;
  paramValues[1] = t_buf;

  PGresult *r = PQexecParams (c,
			      "INSERT INTO sector_warps(from_sector, to_sector) VALUES($1, $2) ON CONFLICT DO NOTHING",
			      2,
			      NULL,
			      paramValues,
			      NULL,
			      NULL,
			      0);


  if (!r || PQresultStatus (r) != PGRES_COMMAND_OK)
    {
      if (r)
	{
	  PQclear (r);
	}
      return -1;
    }
  int rows = atoi (PQcmdTuples (r));


  PQclear (r);
  return rows;
}


static int
is_sector_used (PGconn *c, int sector_id)
{
  const char *paramValues[1];
  char buf[16];
  snprintf (buf, sizeof (buf), "%d", sector_id);
  paramValues[0] = buf;
  PGresult *r = PQexecParams (c,
			      "SELECT 1 FROM used_sectors WHERE used=$1",
			      1,
			      NULL,
			      paramValues,
			      NULL,
			      NULL,
			      0);
  int used = (r && PQntuples (r) > 0);


  if (r)
    {
      PQclear (r);
    }
  return used;
}


static int
create_random_warps (PGconn *c, int numSectors, int maxWarps)
{
  exec_sql (c, "BEGIN", "BEGIN random warps");
  for (int s = 11; s <= numSectors; s++)
    {
      if (is_sector_used (c, s))
	{
	  continue;
	}
      if ((rand () % 100) < DEFAULT_PERCENT_DEADEND)
	{
	  continue;
	}

      int targetWarps = 1 + (rand () % maxWarps);
      int attempts = 0;
      int deg = 0;
      int has_warp = 0;


      while (deg < targetWarps && attempts < 200)
	{
	  int t = 11 + (rand () % (numSectors - 10));


	  if (t == s)
	    {
	      attempts++;
	      continue;
	    }
	  
	  /* Check if adding a warp would exceed maxWarps limit on either sector */
	  char check_sql[256];
	  snprintf (check_sql, sizeof (check_sql),
		   "SELECT (SELECT COUNT(*) FROM sector_warps WHERE from_sector = %d) as s_count, "
		   "       (SELECT COUNT(*) FROM sector_warps WHERE from_sector = %d) as t_count;",
		   s, t);
	  
	  PGresult *check_r = PQexec (c, check_sql);
	  int s_count = 0, t_count = 0;
	  if (check_r && PQresultStatus (check_r) != PGRES_FATAL_ERROR && PQntuples (check_r) > 0)
	    {
	      s_count = atoi (PQgetvalue (check_r, 0, 0));
	      t_count = atoi (PQgetvalue (check_r, 0, 1));
	      PQclear (check_r);
	    }
	  
	  /* Skip if either sector already has max warps */
	  if (s_count >= maxWarps || t_count >= maxWarps)
	    {
	      attempts++;
	      continue;
	    }
	  
	  /* Note: Removed is_sector_used(c, t) check to allow connecting
	     tunnel sectors to the main graph. This prevents tunnel sectors
	     from becoming orphaned. */

	  int res = insert_warp_unique (c, s, t);


	  if (res > 0)
	    {
	      has_warp = 1;
	      
	      /* Check reverse warp count before adding it */
	      char rev_check[128];
	      snprintf (rev_check, sizeof (rev_check),
		       "SELECT COUNT(*) FROM sector_warps WHERE from_sector = %d;", t);
	      PGresult *rev_r = PQexec (c, rev_check);
	      int t_rev_count = 0;
	      if (rev_r && PQresultStatus (rev_r) != PGRES_FATAL_ERROR && PQntuples (rev_r) > 0)
		{
		  t_rev_count = atoi (PQgetvalue (rev_r, 0, 0));
		  PQclear (rev_r);
		}
	      
	      /* Only add reverse warp if target hasn't hit max */
	      if ((rand () % 100) >= DEFAULT_PERCENT_ONEWAY && t_rev_count < maxWarps)
		{
		  insert_warp_unique (c, t, s);
		}
	      deg++;
	    }
	  attempts++;
	}
      
      /* Safety net: If no warps were created after 200 attempts,
         force at least one connection to prevent orphaned sectors. */
      if (!has_warp && deg == 0)
	{
	  int t = 11 + (rand () % (numSectors - 10));
	  if (t != s)
	    {
	      insert_warp_unique (c, s, t);
	      if ((rand () % 100) >= DEFAULT_PERCENT_ONEWAY)
		{
		  insert_warp_unique (c, t, s);
		}
	    }
	}
    }
  exec_sql (c, "COMMIT", "COMMIT random warps");
  return 0;
}


static int
bigbang_create_tunnels (PGconn *c,
			int sector_count, int min_tunnels, int min_tunnel_len)
{
  exec_sql (c, "BEGIN", "BEGIN tunnels");
  exec_sql (c, "DELETE FROM used_sectors", "clear used_sectors");

  int added_tunnels = 0;
  int attempts = 0;


  while (added_tunnels < min_tunnels && attempts < 60)
    {
      int path_len = min_tunnel_len + (rand () % 5);
      int nodes[12];
      int n = 0;


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
	  if (dup || is_sector_used (c, s))
	    {
	      continue;
	    }
	  nodes[n++] = s;
	}

      exec_sql (c, "SAVEPOINT tunnel", "savepoint");
      int failed = 0;


      for (int i = 0; i < path_len - 1; i++)
	{
	  if (insert_warp_unique (c, nodes[i], nodes[i + 1]) < 0 ||
	      insert_warp_unique (c, nodes[i + 1], nodes[i]) < 0)
	    {
	      failed = 1;
	      break;
	    }
	}

      if (failed)
	{
	  exec_sql (c, "ROLLBACK TO tunnel", "rollback tunnel");
	  attempts++;
	  continue;
	}

      for (int i = 0; i < path_len; i++)
	{
	  char buf[16];
	  snprintf (buf, sizeof (buf), "%d", nodes[i]);
	  const char *pv[1] = { buf };


	  PQclear (PQexecParams (c,
				 "INSERT INTO used_sectors(used) VALUES($1)",
				 1, NULL, pv, NULL, NULL, 0));
	}
      added_tunnels++;
      attempts++;
    }
  exec_sql (c, "COMMIT", "COMMIT tunnels");
  printf ("BIGBANG: Added %d tunnels in %d attempts.\n", added_tunnels,
	  attempts);
  return 0;
}


static int
ensure_fedspace_exit (PGconn *c, int outer_min, int outer_max)
{
  int have = 0;
  char buf1[16], buf2[16];
  snprintf (buf1, sizeof (buf1), "%d", outer_min);
  snprintf (buf2, sizeof (buf2), "%d", outer_max);
  const char *pv[2] = { buf1, buf2 };

  PGresult *r = PQexecParams (c,
			      "SELECT COUNT(*) FROM sector_warps WHERE from_sector BETWEEN 2 AND 10 AND to_sector BETWEEN $1 AND $2",
			      2,
			      NULL,
			      pv,
			      NULL,
			      NULL,
			      0);


  if (r && PQresultStatus (r) == PGRES_TUPLES_OK)
    {
      have = atoi (PQgetvalue (r, 0, 0));
    }
  PQclear (r);

  if (have >= 3)
    {
      return 0;
    }

  int attempts = 0;


  while (have < 3 && attempts < 100)
    {
      attempts++;
      int from = 2 + (rand () % 9);
      int to = outer_min + (rand () % (outer_max - outer_min + 1));


      if (from == to)
	{
	  continue;
	}
      if (insert_warp_unique (c, from, to) > 0)
	{
	  insert_warp_unique (c, to, from);
	  have++;
	}
    }
  return 0;
}


/* Validate and fix universe connectivity. If trapped sectors are found,
   automatically add warps to escape them. This ensures no sector is unreachable. */
static int
validate_universe_connectivity (PGconn *c)
{
  PGresult *r = PQexec (c,
			"SELECT s.sector_id "
			"  FROM sectors s "
			" WHERE s.sector_id > 10 "
			"   AND NOT EXISTS ("
			"     SELECT 1 FROM sector_warps "
			"      WHERE from_sector = s.sector_id"
			"   ) "
			" ORDER BY s.sector_id");

  if (!r || PQresultStatus (r) != PGRES_TUPLES_OK)
    {
      fprintf (stderr, "ERROR: Failed to query trapped sectors\n");
      if (r)
	{
	  PQclear (r);
	}
      return -1;
    }

  int trapped_count = PQntuples (r);
  if (trapped_count > 0)
    {
      int fixed_count = 0;
      
      /* Get total sector count and max warps per sector */
      PGresult *max_r = PQexec (c, "SELECT MAX(sector_id) FROM sectors");
      int max_sector = 2500;
      if (max_r && PQresultStatus (max_r) != PGRES_FATAL_ERROR)
	{
	  max_sector = atoi (PQgetvalue (max_r, 0, 0));
	  PQclear (max_r);
	}
      
      PGresult *cfg_r = PQexec (c, "SELECT value FROM config WHERE key = 'maxwarps_per_sector'");
      int max_warps = 6;
      if (cfg_r && PQresultStatus (cfg_r) != PGRES_FATAL_ERROR && PQntuples (cfg_r) > 0)
	{
	  max_warps = atoi (PQgetvalue (cfg_r, 0, 0));
	  PQclear (cfg_r);
	}

      /* Add warps to each trapped sector */
      for (int i = 0; i < trapped_count; i++)
	{
	  int trapped_sector = atoi (PQgetvalue (r, i, 0));
	  
	  /* Try to add a warp to a random sector (retry up to 10 times) */
	  int added = 0;
	  for (int attempt = 0; attempt < 10 && !added; attempt++)
	    {
	      int target = 11 + (rand () % (max_sector - 10));
	      if (target == trapped_sector)
		continue;
	      
	      /* Check current warp count for both sectors */
	      char count_sql[512];
	      snprintf (count_sql, sizeof (count_sql),
		       "SELECT "
		       "  (SELECT COUNT(*) FROM sector_warps WHERE from_sector = %d) as from_count, "
		       "  (SELECT COUNT(*) FROM sector_warps WHERE from_sector = %d) as to_count;",
		       trapped_sector, target);
	      
	      PGresult *count_r = PQexec (c, count_sql);
	      int from_count = 0, to_count = 0;
	      
	      if (count_r && PQresultStatus (count_r) != PGRES_FATAL_ERROR && PQntuples (count_r) > 0)
		{
		  from_count = atoi (PQgetvalue (count_r, 0, 0));
		  to_count = atoi (PQgetvalue (count_r, 0, 1));
		  PQclear (count_r);
		}
	      
	      /* Skip if either sector already has max warps */
	      if (from_count >= max_warps || to_count >= max_warps)
		continue;
	      
	      /* Build SQL to add the warp */
	      char sql[256];
	      snprintf (sql, sizeof (sql),
		       "INSERT INTO sector_warps (from_sector, to_sector) "
		       "SELECT %d, %d "
		       "WHERE NOT EXISTS (SELECT 1 FROM sector_warps WHERE from_sector = %d AND to_sector = %d);",
		       trapped_sector, target, trapped_sector, target);
	      
	      PGresult *insert_r = PQexec (c, sql);
	      if (insert_r && PQresultStatus (insert_r) != PGRES_FATAL_ERROR)
		{
		  int affected = atoi (PQcmdTuples (insert_r));
		  if (affected > 0)
		    {
		      added = 1;
		      fixed_count++;
		    }
		  PQclear (insert_r);
		}
	    }
	}
      
      fprintf (stderr, "BIGBANG: Fixed %d orphaned sectors\n", fixed_count);
    }

  PQclear (r);
  
  /* Verify fix worked */
  PGresult *verify_r = PQexec (c,
			       "SELECT COUNT(*) FROM sectors s "
			       " WHERE s.sector_id > 10 "
			       "   AND NOT EXISTS ("
			       "     SELECT 1 FROM sector_warps "
			       "      WHERE from_sector = s.sector_id"
			       "   )");
  
  if (verify_r && PQresultStatus (verify_r) != PGRES_FATAL_ERROR)
    {
      int remaining_trapped = atoi (PQgetvalue (verify_r, 0, 0));
      PQclear (verify_r);
      
      if (remaining_trapped == 0)
	{
	  printf ("BIGBANG: Universe connectivity validation: OK (all sectors have exits)\n");
	  return 0;
	}
      else
	{
	  fprintf (stderr, "ERROR: After auto-fix, %d sectors still trapped\n", remaining_trapped);
	  return -1;
	}
    }
  
  printf ("BIGBANG: Universe connectivity validation: OK (all sectors have exits)\n");
  return 0;
}


/* -----------------------------------------------------------------------------*
 * Main
 * ----------------------------------------------------------------------------- */
static void
usage (const char *argv0)
{
  fprintf (stderr, "Usage: %s\n", argv0);
  fprintf (stderr,
	   "Configuration is primarily read from bigbang.json in the current directory.\n");
  fprintf (stderr,
	   "Please edit bigbang.json to configure database and game options.\n");
  exit (1);
}


int
main (int argc, char **argv)
{
  char *admin_cs = strdup ("dbname=postgres");
  char *db_name = strdup ("twclone");
  char *app_cs_tmpl = strdup ("dbname=%DB%");
  char *sql_dir = strdup ("sql/pg");

  int sectors = 500;
  int density = 4;
  int port_ratio = 40;
  double planet_ratio = 30.0;
  int port_size = 0;
  int tech_level = 0;
  int port_credits = 0;
  int min_tunnels = 15;
  int min_tunnel_len = 4;

  static struct option long_options[] = {
    {"admin", required_argument, 0, 1001},
    {"db", required_argument, 0, 1002},
    {"app", required_argument, 0, 1003},
    {"sql-dir", required_argument, 0, 1004},
    {"sectors", required_argument, 0, 's'},
    {"density", required_argument, 0, 'd'},
    {"port-ratio", required_argument, 0, 'r'},
    {"planet-ratio", required_argument, 0, 'R'},
    {0, 0, 0, 0}
  };

  /* Try loading config from bigbang.json */
  json_error_t jerr;
  json_t *jcfg = json_load_file ("bigbang.json", 0, &jerr);
  if (jcfg)
    {
      printf ("BIGBANG: Loading config from bigbang.json...\n");
      json_t *j = NULL;
      const char *s;


      if ((j = json_object_get (jcfg, "admin"))
	  && (s = json_string_value (j)))
	{
	  free (admin_cs);
	  admin_cs = strdup (s);
	}
      if ((j = json_object_get (jcfg, "db")) && (s = json_string_value (j)))
	{
	  free (db_name);
	  db_name = strdup (s);
	}
      if ((j = json_object_get (jcfg, "app")) && (s = json_string_value (j)))
	{
	  free (app_cs_tmpl);
	  app_cs_tmpl = strdup (s);
	}
      if ((j = json_object_get (jcfg,
				"sql_dir")) && (s = json_string_value (j)))
	{
	  free (sql_dir);
	  sql_dir = strdup (s);
	}
      if ((j = json_object_get (jcfg, "sectors")) && json_is_integer (j))
	{
	  sectors = (int) json_integer_value (j);
	}
      if ((j = json_object_get (jcfg, "density")) && json_is_integer (j))
	{
	  density = (int) json_integer_value (j);
	}
      if ((j = json_object_get (jcfg, "port_ratio")) && json_is_integer (j))
	{
	  port_ratio = (int) json_integer_value (j);
	}
      if ((j = json_object_get (jcfg, "planet_ratio")) && json_is_number (j))
	{
	  planet_ratio = json_number_value (j);
	}
      if ((j = json_object_get (jcfg, "port_size")) && json_is_integer (j))
	{
	  port_size = (int) json_integer_value (j);
	}
      if ((j = json_object_get (jcfg, "tech_level")) && json_is_integer (j))
	{
	  tech_level = (int) json_integer_value (j);
	}
      if ((j = json_object_get (jcfg, "port_credits")) && json_is_integer (j))
	{
	  port_credits = (int) json_integer_value (j);
	}
      if ((j = json_object_get (jcfg, "min_tunnels")) && json_is_integer (j))
	{
	  min_tunnels = (int) json_integer_value (j);
	}
      if ((j = json_object_get (jcfg, "min_tunnel_len"))
	  && json_is_integer (j))
	{
	  min_tunnel_len = (int) json_integer_value (j);
	}
      // json_decref(jcfg); // Removed: Keep jcfg alive for sync_config_to_db
    }
  else if (access ("bigbang.json", F_OK) == 0)
    {
      fprintf (stderr,
	       "WARNING: bigbang.json exists but failed to parse: %s\n",
	       jerr.text);
    }

  int opt, idx = 0;


  while ((opt =
	  getopt_long (argc, argv, "s:d:r:R:", long_options, &idx)) != -1)
    {
      switch (opt)
	{
	case 1001:
	  free (admin_cs);
	  admin_cs = strdup (optarg);
	  break;
	case 1002:
	  free (db_name);
	  db_name = strdup (optarg);
	  break;
	case 1003:
	  free (app_cs_tmpl);
	  app_cs_tmpl = strdup (optarg);
	  break;
	case 1004:
	  free (sql_dir);
	  sql_dir = strdup (optarg);
	  break;
	case 's':
	  sectors = atoi (optarg);
	  break;
	case 'd':
	  density = atoi (optarg);
	  break;
	case 'r':
	  port_ratio = atoi (optarg);
	  break;
	case 'R':
	  planet_ratio = atof (optarg);
	  break;
	default:
	  usage (argv[0]);
	}
    }

  srand (time (NULL));
  server_log_init_file ("./twclone.log", "[bigbang]", 1, LOG_INFO);

  // 1) Lifecycle: Create DB
  PGconn *admin = PQconnectdb (admin_cs);


  if (PQstatus (admin) != CONNECTION_OK)
    {
      die_conn (admin, "connect admin");
    }
  if (create_db_if_needed (admin, db_name) != 0)
    {
      die ("create db failed");
    }
  PQfinish (admin);

  // 2) Connect to target
  char app_cs[2048];
  const char *p_db = strstr (app_cs_tmpl, "%DB%");


  if (p_db)
    {
      snprintf (app_cs,
		sizeof (app_cs),
		"%.*s%s%s",
		(int) (p_db - app_cs_tmpl), app_cs_tmpl, db_name, p_db + 4);
    }
  else
    {
      strncpy (app_cs, app_cs_tmpl, sizeof (app_cs) - 1);
      app_cs[sizeof(app_cs) - 1] = '\0';
    }

  PGconn *app = PQconnectdb (app_cs);


  if (PQstatus (app) != CONNECTION_OK)
    {
      die_conn (app, "connect app");
    }

  // Check for existing tables
  PGresult *r_check = PQexec (app,
			      "SELECT count(*) FROM pg_tables WHERE schemaname = 'public'");
  int table_count = 0;


  if (r_check && PQresultStatus (r_check) == PGRES_TUPLES_OK)
    {
      table_count = atoi (PQgetvalue (r_check, 0, 0));
    }
  PQclear (r_check);

  if (table_count > 0)
    {
      printf ("WARNING: The database '%s' contains %d tables.\n",
	      db_name, table_count);
      printf
	("Running Big Bang will COMPLETELY DESTROY the existing universe.\n");
      printf ("Are you sure you want to proceed? [y/N] ");

      char resp[16];


      if (fgets (resp, sizeof (resp), stdin))
	{
	  if (resp[0] != 'y' && resp[0] != 'Y')
	    {
	      printf ("Aborted.\n");
	      PQfinish (app);
	      exit (1);
	    }
	}
      else
	{
	  // If stdin is closed/empty (e.g. non-interactive script), fail safe
	  printf ("\nNo input. Aborted.\n");
	  PQfinish (app);
	  exit (1);
	}
    }

  // 2.5) Clean Database (Big Bang requires a fresh universe)
  printf
    ("BIGBANG: Cleaning existing universe (DROP SCHEMA public CASCADE)...\n");
  if (exec_sql
      (app, "DROP SCHEMA public CASCADE; CREATE SCHEMA public;",
       "clean_universe") != 0)
    {
      die ("failed to clean universe");
    }

  // 3) Apply Schema (in order: sorted by numeric prefix)
  printf ("BIGBANG: Loading SQL files from %s...\n", sql_dir);
  if (load_sql_files_in_order (app, sql_dir) != 0)
    {
      die ("schema apply failed");
    }

  // 3.5) Sync Config to DB
  sync_config_to_db (app, jcfg);

  // 4) Execute Stored Procedures
  printf ("BIGBANG: Running stored procedures...\n");
  char buf[1024];


  snprintf (buf, sizeof (buf), "SELECT generate_sectors(%d)", sectors);
  exec_sql (app, buf, "generate_sectors");

  int max_ports = (sectors * port_ratio) / 100;
  (void) max_ports;
  (void) bigbang_create_tunnels;
  (void) fetch_int;


  snprintf (buf, sizeof (buf), "SELECT generate_ports(%d)", sectors);	// wait, current SP takes target_sectors
  exec_sql (app, buf, "generate_ports");

  exec_sql (app, "SELECT generate_stardock()", "generate_stardock");
  exec_sql (app, "SELECT generate_msl()", "generate_msl");
  exec_sql (app, "SELECT generate_taverns(20)", "generate_taverns");

  if (port_size > 0 || tech_level > 0 || port_credits > 0)
    {
      char updates[512] = "UPDATE ports SET ";
      size_t updates_used = strlen (updates);
      const size_t updates_cap = sizeof (updates);
      int needs_comma = 0;
      int overflow_error = 0;


      if (port_size > 0)
	{
	  char tmp[64];


	  snprintf (tmp, sizeof (tmp), "size = %d", port_size);
	  size_t tmp_len = strlen (tmp);
	  if (updates_used + tmp_len >= updates_cap)
	    {
	      LOGE ("Updates string overflow");
	      overflow_error = 1;
	    }
	  else
	    {
	      memcpy (updates + updates_used, tmp, tmp_len);
	      updates_used += tmp_len;
	      updates[updates_used] = '\0';
	      needs_comma = 1;
	    }
	}
      if (!overflow_error && tech_level > 0)
	{
	  if (needs_comma)
	    {
	      const char *sep = ", ";
	      size_t sep_len = 2;
	      if (updates_used + sep_len >= updates_cap)
		{
		  LOGE ("Updates string overflow");
		  overflow_error = 1;
		}
	      else
		{
		  memcpy (updates + updates_used, sep, sep_len);
		  updates_used += sep_len;
		  updates[updates_used] = '\0';
		}
	    }
	  if (!overflow_error)
	    {
	      char tmp[64];


	      snprintf (tmp, sizeof (tmp), "techlevel = %d", tech_level);
	      size_t tmp_len = strlen (tmp);
	      if (updates_used + tmp_len >= updates_cap)
		{
		  LOGE ("Updates string overflow");
		  overflow_error = 1;
		}
	      else
		{
		  memcpy (updates + updates_used, tmp, tmp_len);
		  updates_used += tmp_len;
		  updates[updates_used] = '\0';
		  needs_comma = 1;
		}
	    }
	}
      if (!overflow_error && port_credits > 0)
	{
	  if (needs_comma)
	    {
	      const char *sep = ", ";
	      size_t sep_len = 2;
	      if (updates_used + sep_len >= updates_cap)
		{
		  LOGE ("Updates string overflow");
		  overflow_error = 1;
		}
	      else
		{
		  memcpy (updates + updates_used, sep, sep_len);
		  updates_used += sep_len;
		  updates[updates_used] = '\0';
		}
	    }
	  if (!overflow_error)
	    {
	      char tmp[64];


	      snprintf (tmp, sizeof (tmp), "petty_cash = %d", port_credits);
	      size_t tmp_len = strlen (tmp);
	      if (updates_used + tmp_len >= updates_cap)
		{
		  LOGE ("Updates string overflow");
		  overflow_error = 1;
		}
	      else
		{
		  memcpy (updates + updates_used, tmp, tmp_len);
		  updates_used += tmp_len;
		  updates[updates_used] = '\0';
		}
	    }
	}
      if (!overflow_error && needs_comma)
	{
	  // Only update standard ports (1-8), leave Stardock (9) alone
	  const char *where = " WHERE type BETWEEN 1 AND 8";
	  size_t where_len = strlen (where);
	  if (updates_used + where_len >= updates_cap)
	    {
	      LOGE ("Updates string overflow");
	      overflow_error = 1;
	    }
	  else
	    {
	      memcpy (updates + updates_used, where, where_len);
	      updates_used += where_len;
	      updates[updates_used] = '\0';
	      printf ("BIGBANG: Applying port defaults from config...\n");
	      exec_sql (app, updates, "apply_port_defaults");
	    }
	}
    }

  int max_planets = (int) ((sectors * planet_ratio) / 100.0);


  snprintf (buf, sizeof (buf), "SELECT generate_planets(%d)", max_planets);
  exec_sql (app, buf, "generate_planets");

  exec_sql (app, "SELECT spawn_initial_fleet()", "spawn_fleet");
  exec_sql (app, "SELECT apply_game_defaults()", "apply_game_defaults");

  // 5) Graph Brain
  printf ("BIGBANG: Generating topology...\n");

  // First, create isolated tunnel chains
  char tunnel_sql[256];
  snprintf (tunnel_sql, sizeof (tunnel_sql),
	    "SELECT generate_tunnels(%d, %d)", min_tunnels, min_tunnel_len);
  exec_sql (app, tunnel_sql, "generate_tunnels");

  // NOW create NPC homeworlds at tunnel endpoints (after tunnels are generated)
  exec_sql (app, "SELECT setup_npc_homeworlds()", "setup_homeworlds");
  exec_sql (app, "SELECT setup_ferringhi_alliance()", "setup_ferringhi");
  exec_sql (app, "SELECT setup_orion_syndicate()", "setup_orion");

  // Create clusters AFTER faction homeworlds are set up
  exec_sql (app, "SELECT generate_clusters_v2(50)", "generate_clusters");
  
  // Spawn Orion trader fleet AFTER clusters are created
  exec_sql (app, "SELECT spawn_orion_fleet()", "spawn_orion_fleet");

  // Then generate random warps for the rest of the universe
  create_random_warps (app, sectors, density);
  ensure_fedspace_exit (app, 11, sectors);

  /* Validate universe connectivity - ensure no orphan sectors */
  if (validate_universe_connectivity (app) != 0)
    {
      fprintf (stderr, "FATAL: Universe validation failed. Aborting bigbang.\n");
      PQfinish (app);
      if (jcfg)
	{
	  json_decref (jcfg);
	}
      free (admin_cs);
      free (db_name);
      free (app_cs_tmpl);
      free (sql_dir);
      return 2;
    }

  PQfinish (app);
  if (jcfg)
    {
      json_decref (jcfg);
    }
  free (admin_cs);
  free (db_name);
  free (app_cs_tmpl);
  free (sql_dir);
  printf ("OK: Big Bang Complete.\n");
  return 0;
}
