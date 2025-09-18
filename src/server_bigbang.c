#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sqlite3.h>
#include "database.h"
#include "server_bigbang.h"

/* 
 * bigbang – entry point
 * Creates the entire universe from scratch and stores in the DB.
 */
int bigbang(void) {
    srand((unsigned int) time(NULL));

    fprintf(stderr, "BIGBANG: Creating sectors...\n");
    if (create_sectors(500) != 0) return -1;

    fprintf(stderr, "BIGBANG: Creating Fedspace...\n");
    if (create_fedspace() != 0) return -1;

    fprintf(stderr, "BIGBANG: Creating ports...\n");
    if (create_ports(140) != 0) return -1;

    fprintf(stderr, "BIGBANG: Creating Ferringhi home sector...\n");
    if (create_ferringhi() != 0) return -1;

    fprintf(stderr, "BIGBANG: Creating planets...\n");
    if (create_planets(10) != 0) return -1;

    fprintf(stderr, "BIGBANG: Universe successfully created.\n");
    return 0;
}

/* 
 * Each of the following stubs should execute SQL against db_handle. 
 * Replace the SQL below with the proper schema from database.h
 */

int create_sectors(int numSectors) {
    char sql[256];
    for (int i = 1; i <= numSectors; i++) {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO sectors (id, name, beacon, nebulae) "
                 "VALUES (%d, 'Sector %d', '', '');",
                 i, i);
        if (sqlite3_exec(db_handle, sql, NULL, NULL, NULL) != SQLITE_OK) {
            fprintf(stderr, "create_sectors failed at %d: %s\n",
                    i, sqlite3_errmsg(db_handle));
            return -1;
        }
    }
    return 0;
}

int create_fedspace(void) {
    /* Example: set beacon/nebulae on first 10 sectors */
    const char *sql =
        "UPDATE sectors SET beacon='The Federation -- Do Not Dump!', "
        "nebulae='The Federation' WHERE id BETWEEN 1 AND 10;";
    if (sqlite3_exec(db_handle, sql, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "create_fedspace failed: %s\n", sqlite3_errmsg(db_handle));
        return -1;
    }
    return 0;
}

int create_ports(int numPorts) {
    for (int i = 1; i <= numPorts; i++) {
        char sql[512];
        snprintf(sql, sizeof(sql),
                 "INSERT INTO ports (number, name, location, size, techlevel, credits, type, invisible) "
                 "VALUES (%d, 'Port %d', %d, 10, 5, 50000, 1, 0);",
                 i, i, (i % 500) + 1);
        if (sqlite3_exec(db_handle, sql, NULL, NULL, NULL) != SQLITE_OK) {
            fprintf(stderr, "create_ports failed at %d: %s\n",
                    i, sqlite3_errmsg(db_handle));
            return -1;
        }
    }
    return 0;
}

int create_ferringhi(void) {
    const char *sql =
        "UPDATE sectors SET beacon='Ferringhi', nebulae='Ferringhi' "
        "WHERE id=20;";
    if (sqlite3_exec(db_handle, sql, NULL, NULL, NULL) != SQLITE_OK) {
        fprintf(stderr, "create_ferringhi failed: %s\n", sqlite3_errmsg(db_handle));
        return -1;
    }
    return 0;
}

int create_planets(int numPlanets) {
    for (int i = 1; i <= numPlanets; i++) {
        char sql[512];
        snprintf(sql, sizeof(sql),
                 "INSERT INTO planets (num, sector, name, owner, population, minerals, ore, energy, type) "
                 "VALUES (%d, %d, 'Planet%d', -1, 1000, 100, 50, 200, 1);",
                 i, (i % 500) + 1, i);
        if (sqlite3_exec(db_handle, sql, NULL, NULL, NULL) != SQLITE_OK) {
            fprintf(stderr, "create_planets failed at %d: %s\n",
                    i, sqlite3_errmsg(db_handle));
            return -1;
        }
    }
    return 0;
}
