#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <jansson.h>
#include <ctype.h>
#include "server_config.h"

#include "database.h"
#include "config.h"
#include <sqlite3.h>
#include <stdlib.h>
#include <stdio.h>

struct config *
loadconfig (void)
{
  sqlite3_stmt *stmt;
  const char *sql =
    "SELECT turnsperday, maxwarps, startingcredits, startingfighters, "
    "startingholds, processinterval, autosave, max_players, max_ships, "
    "max_ports, max_planets, max_total_planets, max_safe_planets, "
    "max_citadel_level, number_of_planet_types, max_ship_name_length, "
    "ship_type_count, hash_length, default_port, default_nodes, "
    "warps_per_sector, buff_size, max_name_length, planet_type_count "
    "FROM config WHERE id = 1;";

  if (sqlite3_prepare_v2 (db_handle, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
      fprintf (stderr, "DB loadconfig error: %s\n",
	       sqlite3_errmsg (db_handle));
      return NULL;
    }

  struct config *cfg = malloc (sizeof (struct config));
  if (!cfg)
    return NULL;

  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      cfg->turnsperday = sqlite3_column_int (stmt, 0);
      cfg->maxwarps = sqlite3_column_int (stmt, 1);
      cfg->startingcredits = sqlite3_column_int (stmt, 2);
      cfg->startingfighters = sqlite3_column_int (stmt, 3);
      cfg->startingholds = sqlite3_column_int (stmt, 4);
      cfg->processinterval = sqlite3_column_int (stmt, 5);
      cfg->autosave = sqlite3_column_int (stmt, 6);
      cfg->max_players = sqlite3_column_int (stmt, 7);
      cfg->max_ships = sqlite3_column_int (stmt, 8);
      cfg->max_ports = sqlite3_column_int (stmt, 9);
      cfg->max_planets = sqlite3_column_int (stmt, 10);
      cfg->max_total_planets = sqlite3_column_int (stmt, 11);
      cfg->max_safe_planets = sqlite3_column_int (stmt, 12);
      cfg->max_citadel_level = sqlite3_column_int (stmt, 13);
      cfg->number_of_planet_types = sqlite3_column_int (stmt, 14);
      cfg->max_ship_name_length = sqlite3_column_int (stmt, 15);
      cfg->ship_type_count = sqlite3_column_int (stmt, 16);
      cfg->hash_length = sqlite3_column_int (stmt, 17);
      cfg->default_port = sqlite3_column_int (stmt, 18);
      cfg->default_nodes = sqlite3_column_int (stmt, 19);
      cfg->warps_per_sector = sqlite3_column_int (stmt, 20);
      cfg->buff_size = sqlite3_column_int (stmt, 21);
      cfg->max_name_length = sqlite3_column_int (stmt, 22);
      cfg->planet_type_count = sqlite3_column_int (stmt, 23);
    }
  else
    {
      free (cfg);
      cfg = NULL;
    }

  sqlite3_finalize (stmt);
  return cfg;
}


/////////////// OLD //////////////////

/* // Data loading functions (placeholders) */
/* struct config * */
/* loadconfig (const char *file_path) */
/* { */
/*   FILE *fp = fopen (file_path, "rb"); */
/*   if (!fp) */
/*     return NULL; */
/*   struct config *data = malloc (sizeof (struct config)); */
/*   (void) fread (data, sizeof (struct config), 1, fp); */
/*   fclose (fp); */
/*   return data; */
/* } */

/* void */
/* initconfig (struct config *configdata) */
/* { */
/*   configdata->turnsperday = 100; */
/*   configdata->maxwarps = 6; */
/*   configdata->startingcredits = 2000; */
/*   configdata->startingfighters = 20; */
/*   configdata->startingholds = 20; */
/*   configdata->processinterval = 2; */
/*   configdata->autosave = 10; */
/*   configdata->max_players = 200; */
/*   configdata->max_ships = 1024; */
/*   configdata->max_ports = 500; */
/*   configdata->max_planets = 200; */
/*   configdata->max_total_planets = 200; */
/*   configdata->max_safe_planets = 5; */
/*   configdata->max_citadel_level = 7; */
/*   configdata->number_of_planet_types = 8; */
/*   configdata->max_ship_name_length = 40; */
/*   configdata->ship_type_count = 15; */
/*   configdata->hash_length = 200; */
/*   configdata->default_port = 1234; */
/*   configdata->default_nodes = 0; */
/*   configdata->warps_per_sector = 6; */
/*   configdata->buff_size = 5000; */
/*   configdata->max_name_length = 25; */
/*   configdata->planet_type_count = 8; */
/* } */
