#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <getopt.h>
#include <string.h>
#include "database.h"
#include "server_bigbang.h"
#include "server_log.h"


static int
update_config_int (sqlite3 *db, const char *key, int value)
{
  sqlite3_stmt *stmt;
  const char *sql = "UPDATE config SET value = ? WHERE key = ?;";
  if (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
      fprintf (stderr,
               "Failed to prepare config update for %s: %s\n",
               key,
               sqlite3_errmsg (db));
      return -1;
    }
  char val_str[32];


  snprintf (val_str, sizeof(val_str), "%d", value);
  sqlite3_bind_text (stmt, 1, val_str, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, 2, key, -1, SQLITE_STATIC);
  int rc = sqlite3_step (stmt);


  sqlite3_finalize (stmt);
  if (rc != SQLITE_DONE)
    {
      fprintf (stderr, "Failed to update config %s: %s\n", key,
               sqlite3_errmsg (db));
      return -1;
    }
  return 0;
}


static int
get_config_int (sqlite3 *db, const char *key, int default_val)
{
  sqlite3_stmt *stmt;
  const char *sql = "SELECT value FROM config WHERE key = ?;";
  int result = default_val;
  if (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
      return default_val;
    }
  sqlite3_bind_text (stmt, 1, key, -1, SQLITE_STATIC);
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      const char *val = (const char *)sqlite3_column_text (stmt, 0);


      if (val)
        {
          result = atoi (val);
        }
    }
  sqlite3_finalize (stmt);
  return result;
}


static void
print_usage (const char *progname)
{
  printf ("Usage: %s [options]\n", progname);
  printf ("Options:\n");
  printf ("  -s, --sectors <N>      Total number of sectors (Universe Size)\n");
  printf ("  -d, --density <N>      Max warps per sector (Connectivity)\n");
  printf (
    "  -r, --port-ratio <%%>   Percentage of sectors that have ports (0-100)\n");
  printf (
    "  -R, --planet-ratio <%%> Percentage of sectors that have planets (0-100+)\n");
  printf ("  -c, --credits <N>      Starting credits for new players\n");
  printf ("  -f, --fighters <N>     Starting fighters for new players\n");
  printf ("  -H, --holds <N>        Starting cargo holds for new players\n");
  printf ("  -t, --turns <N>        Turns allocated per day\n");
  printf ("  -h, --help             Show this help message\n");
}


int
main (int argc, char *argv[])
{
  server_log_init_file ("./twclone.log", "[server]", 0, LOG_INFO);
  sqlite3 *handle = db_get_handle ();
  /* Default values loaded from DB (so we honor defaults if arguments aren't provided) */
  int sectors = get_config_int (handle, "default_nodes", 500);
  int density = -1;
  int port_ratio = -1;
  int planet_ratio = -1;
  int credits = -1;
  int fighters = -1;
  int holds = -1;
  int turns = -1;
  static struct option long_options[] = {
    {"sectors", required_argument, 0, 's'},
    {"density", required_argument, 0, 'd'},
    {"port-ratio", required_argument, 0, 'r'},
    {"planet-ratio", required_argument, 0, 'R'},
    {"credits", required_argument, 0, 'c'},
    {"fighters", required_argument, 0, 'f'},
    {"holds", required_argument, 0, 'H'},
    {"turns", required_argument, 0, 't'},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}
  };
  int opt;
  int option_index = 0;


  while ((opt = getopt_long (argc,
                             argv,
                             "s:d:r:R:c:f:H:t:h",
                             long_options,
                             &option_index)) != -1)
    {
      switch (opt)
        {
          case 's':
            sectors = atoi (optarg);
            if (sectors < 10)
              {
                sectors = 10;             /* Minimum sanity check */
              }
            break;
          case 'd':
            density = atoi (optarg);
            break;
          case 'r':
            port_ratio = atoi (optarg);
            if (port_ratio < 0)
              {
                port_ratio = 0;
              }
            if (port_ratio > 100)
              {
                port_ratio = 100;
              }
            break;
          case 'R':
            planet_ratio = atoi (optarg);
            if (planet_ratio < 0)
              {
                planet_ratio = 0;
              }
            break;
          case 'c':
            credits = atoi (optarg);
            break;
          case 'f':
            fighters = atoi (optarg);
            break;
          case 'H':
            holds = atoi (optarg);
            break;
          case 't':
            turns = atoi (optarg);
            break;
          case 'h':
            print_usage (argv[0]);
            db_close ();
            return 0;
          default:
            print_usage (argv[0]);
            db_close ();
            return 1;
        }
    }
  if (db_init () != 0)
    {
      fprintf (stderr, "DB init failed\n");
      return 1;
    }
  /* Update Config in DB */
  printf ("Configuring Universe:\n");
  if (update_config_int (handle, "default_nodes", sectors) == 0)
    {
      printf ("  Sectors: %d\n", sectors);
    }
  if (density != -1)
    {
      update_config_int (handle, "maxwarps_per_sector", density);
      printf ("  Density: %d warps/sector\n", density);
    }
  if (port_ratio != -1)
    {
      int max_ports = (int)((long long)sectors * port_ratio / 100);


      if (max_ports < 1)
        {
          max_ports = 1;
        }
      update_config_int (handle, "max_ports", max_ports);
      printf ("  Ports: %d (%d%%)\n", max_ports, port_ratio);
    }
  else
    {
      // Default behavior: 40% if not specified
      int max_ports = (int)((long long)sectors * 40 / 100);


      if (max_ports < 1)
        {
          max_ports = 1;
        }
      update_config_int (handle, "max_ports", max_ports);
      printf ("  Ports: %d (40%% default)\n", max_ports);
    }
  if (planet_ratio != -1)
    {
      int max_planets = (int)((long long)sectors * planet_ratio / 100);


      if (max_planets < 1)
        {
          max_planets = 1;
        }
      update_config_int (handle, "max_total_planets", max_planets);
      printf ("  Planets: %d (%d%%)\n", max_planets, planet_ratio);
    }
  if (credits != -1)
    {
      update_config_int (handle, "startingcredits", credits);
      printf ("  Starting Credits: %d\n", credits);
    }
  if (fighters != -1)
    {
      update_config_int (handle, "startingfighters", fighters);
      printf ("  Starting Fighters: %d\n", fighters);
    }
  if (holds != -1)
    {
      update_config_int (handle, "startingholds", holds);
      printf ("  Starting Holds: %d\n", holds);
    }
  if (turns != -1)
    {
      update_config_int (handle, "turnsperday", turns);
      printf ("  Turns Per Day: %d\n", turns);
    }

  /* clear tables before bigbang if we are re-running?
     Actually, standard bigbang logic usually checks if universe exists.
     But since this is the 'bigbang' tool, we might imply a fresh start.
     For now, we rely on bigbang()'s internal logic or the user deleting the DB.
   */
  if (bigbang () != 0)
    {
      fprintf (stderr, "Bigbang failed\n");
      db_close ();
      return 1;
    }
  //    dump_sectors();
  //    dump_warps();
  db_close ();
  return 0;
}

