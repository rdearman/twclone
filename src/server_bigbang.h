#ifndef SERVER_BIGBANG_H
#define SERVER_BIGBANG_H
#include "db/db_api.h"
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
int bigbang (void);
/* Internal helpers */
int create_sectors (void);
int create_ports (void);
int create_ferringhi (int sector);
extern int create_planets (void);
extern int create_full_port (db_t *db,
                             int sector,
                             int port_number,
                             const char *base_name,
                             int type_id, int *port_id_out);
extern int create_complex_warps (db_t *db, int numSectors);
int create_imperial (void);
int create_taverns (void);
#endif /* SERVER_BIGBANG_H */
