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
#include <sqlite3.h>
#include <stdlib.h>
#include <stdio.h>


void initconfig (void)
{
  // just keeping this dummy function until I can hunt it down and eradicate it. 
}


struct twconfig *config_load(void) {
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
        "       planet_type_count "
        "FROM config WHERE id=1;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db_get_handle(), sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "config_load prepare error: %s\n",
                sqlite3_errmsg(db_get_handle()));
        return NULL;
    }

    struct twconfig *cfg = malloc(sizeof(struct twconfig));
    if (!cfg) {
        sqlite3_finalize(stmt);
        return NULL;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        cfg->turnsperday            = sqlite3_column_int(stmt, 0);
        cfg->maxwarps_per_sector    = sqlite3_column_int(stmt, 1);
        cfg->startingcredits        = sqlite3_column_int(stmt, 2);
        cfg->startingfighters       = sqlite3_column_int(stmt, 3);
        cfg->startingholds          = sqlite3_column_int(stmt, 4);
        cfg->processinterval        = sqlite3_column_int(stmt, 5);
        cfg->autosave               = sqlite3_column_int(stmt, 6);
        cfg->max_ports              = sqlite3_column_int(stmt, 7);
        cfg->max_planets_per_sector = sqlite3_column_int(stmt, 8);
        cfg->max_total_planets      = sqlite3_column_int(stmt, 9);
        cfg->max_citadel_level      = sqlite3_column_int(stmt, 10);
        cfg->number_of_planet_types = sqlite3_column_int(stmt, 11);
        cfg->max_ship_name_length   = sqlite3_column_int(stmt, 12);
        cfg->ship_type_count        = sqlite3_column_int(stmt, 13);
        cfg->hash_length            = sqlite3_column_int(stmt, 14);
        cfg->default_nodes          = sqlite3_column_int(stmt, 15);
        cfg->buff_size              = sqlite3_column_int(stmt, 16);
        cfg->max_name_length        = sqlite3_column_int(stmt, 17);
        cfg->planet_type_count      = sqlite3_column_int(stmt, 18);

        fprintf(stderr, "DEBUG: maxwarps_per_sector = %d\n",
                cfg->maxwarps_per_sector);
    } else {
        free(cfg);
        cfg = NULL;
    }

    sqlite3_finalize(stmt);
    return cfg;
}
