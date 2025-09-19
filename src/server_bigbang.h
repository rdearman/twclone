#ifndef SERVER_BIGBANG_H
#define SERVER_BIGBANG_H

#include <sqlite3.h>


/* Shared defaults for warp generation */
#ifndef DEFAULT_PERCENT_DEADEND
#define DEFAULT_PERCENT_DEADEND 25
#endif
#ifndef DEFAULT_PERCENT_ONEWAY
#define DEFAULT_PERCENT_ONEWAY 50
#endif
#ifndef DEFAULT_PERCENT_JUMP
#define DEFAULT_PERCENT_JUMP 10
#endif



/* Initialise and populate the universe if missing */
int bigbang(void);

/* Internal helpers */
int create_sectors(void);
int create_ports(void);
int create_ferringhi(void);
int create_planets(void);
extern int create_complex_warps(sqlite3 *db, int numSectors);

#endif /* SERVER_BIGBANG_H */
