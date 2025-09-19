#ifndef SERVER_BIGBANG_H
#define SERVER_BIGBANG_H

#include <sqlite3.h>

/* Initialise and populate the universe if missing */
int bigbang(void);

/* Internal helpers */
int create_sectors(void);
int create_ports(void);
int create_ferringhi(void);
int create_planets(void);

#endif /* SERVER_BIGBANG_H */
