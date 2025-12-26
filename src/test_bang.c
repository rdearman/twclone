#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <jansson.h>
#include "db/db_api.h"
#include "game_db.h"
#include "database.h"
#include "server_bigbang.h"
#include "server_log.h"


/* Stubs for linking with schemas.o */
json_t *
loop_get_schema_for_command (const char *name)
{
  (void) name;
  return NULL;
}


json_t *
loop_get_all_schema_keys (void)
{
  return json_array ();
}


static int
update_config_int (db_t *db, const char *key, int value)
{
  db_error_t err;
  const char *sql = "UPDATE config SET value = $1 WHERE key = $2;";
  char val_str[32];
  snprintf (val_str, sizeof (val_str), "%d", value);

  db_bind_t params[] = { db_bind_text (val_str), db_bind_text (key) };


  if (!db_exec (db, sql, params, 2, &err))
    {
      fprintf (stderr,
               "Failed to update config %s: %s\n",
               key, err.message);
      return -1;
    }
  return 0;
}


static int
get_config_int (db_t *db, const char *key, int default_val)
{
  const char *sql = "SELECT value FROM config WHERE key = $1;";
  int result = default_val;
  db_res_t *res = NULL;
  db_error_t err;
  db_bind_t params[] = { db_bind_text (key) };

  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          const char *val = db_res_col_text (res, 0, &err);


          if (val)
            {
              result = atoi (val);
            }
        }
      db_res_finalize (res);
    }
  return result;
}


static void
print_usage (const char *progname)
{
  printf ("Usage: %s [options]\n", progname);
  printf ("Options:\n");
  printf
    ("  -s, --sectors <N>      Total number of sectors (Universe Size)\n");
  printf ("  -d, --density <N>      Max warps per sector (Connectivity)\n");
  printf
    ("  -r, --port-ratio <%%>   Percentage of sectors that have ports (0-100)\n");
  printf
  (
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

  if (game_db_init () != 0)
    {
      fprintf (stderr, "game_db_init failed\n");
      return 1;
    }

  db_t *handle = game_db_get_handle ();
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
                             long_options, &option_index)) != -1)
    {
      switch (opt)
        {
          case 's':
            sectors = atoi (optarg);
            if (sectors < 10)
              {
                sectors = 10;   /* Minimum sanity check */
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
            game_db_close ();
            return 0;
          default:
            print_usage (argv[0]);
            game_db_close ();
            return 1;
        }
    }

  /* clear tables before bigbang if we are re-running?
     Actually, standard bigbang logic usually checks if universe exists.
     But since this is the 'bigbang' tool, we might imply a fresh start.
     For now, we rely on bigbang()'s internal logic or the user deleting the DB.
   */
  if (bigbang () != 0)
    {
      fprintf (stderr, "Bigbang failed\n");
      game_db_close ();
      return 1;
    }
  //    dump_sectors();
  //    dump_warps();
  game_db_close ();
  return 0;
}

