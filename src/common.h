#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jansson.h>		/* -ljansson */
#include <stdbool.h>
#include <sqlite3.h>
#include <netinet/in.h>
/* local includes */
#include "database.h"
#include "schemas.h"
#include "errors.h"
#include "config.h"
#include <stdatomic.h>
#include <netinet/in.h>
#include <string.h>


#ifndef START_FIGHTERS
#define START_FIGHTERS 20
#endif

#ifndef MAX_FIGHTERS_PER_SECTOR
#define MAX_FIGHTERS_PER_SECTOR 50000 
#endif


#ifndef START_HOLDS
#define START_HOLDS 20
#endif

#ifndef PROCESS_INTERVAL
#define PROCESS_INTERVAL 2
#endif

#ifndef AUTOSAVE
#define AUTOSAVE 10
#endif

#ifndef MAX_SHIP_NAME
#define MAX_SHIP_NAME 12
#endif

#ifndef SHIP_TYPES
#define SHIP_TYPES 10
#endif

#ifndef DEFAULT_NODES
#define DEFAULT_NODES 0
#endif


//  END New
#ifndef MAX_PLAYERS
#define MAX_PLAYERS 200
#endif

#ifndef MAX_SHIPS
#define MAX_SHIPS 1024
#endif

#ifndef MAX_PORTS
#define MAX_PORTS 500
#endif

#ifndef MAX_TOTAL_PLANETS
#define MAX_TOTAL_PLANETS 200
#endif

#ifndef MAX_WARPS_PER_SECTOR
#define MAX_WARPS_PER_SECTOR 6
#endif

#ifndef DEFAULT_PORT
#define DEFAULT_PORT 1234
#endif

#ifndef BUFF_SIZE
#define BUFF_SIZE 5000
#endif

#ifndef MAX_NAME_LENGTH
#define MAX_NAME_LENGTH 25
#endif

#ifndef COMMON_H
#define COMMON_H

typedef enum {
    ASSET_MINE = 1,
    ASSET_FIGHTER = 2,
    ASSET_BEACON = 3,
    ASSET_LIMPET_MINE = 4    
} asset_type_t;


int init_sockaddr (int, struct sockaddr_in *);
int init_clientnetwork (char *hostname, int port);

int init_sockaddr (int, struct sockaddr_in *);
int init_clientnetwork (char *hostname, int port);

//int sendinfo (int sockid, char *buffer);
//int recvinfo (int sockid, char *buffer);
int acceptnewconnection (int sockid);
int randomnum (int min, int max);
int min (int a, int b);
int max (int a, int b);
enum porttype
{
  p_trade,
  p_land,
  p_upgrade,
  p_negotiate,
  p_rob,
  p_smuggle,
  p_attack,
  p_quit,
  p_buyship,
  p_sellship,
  p_priceship,
  p_listships,
  p_deposit,
  p_withdraw,
  p_balance,
  p_buyhardware,
  pn_listnodes,
  pn_travel
};

extern int *usedNames;
extern time_t *timeptr;



/* ships.flags bitmask */
#define SHIPF_FOR_SALE   0x0001	/* Owner intends to sell at Stardock */
#define SHIPF_LOCKED     0x0002	/* Cannot be claimed (admin/quest/Fed/NPC protected) */
#define SHIPF_NO_TRADE   0x0004	/* Cannot be traded/sold at any port */
#define SHIPF_NPC        0x0008	/* Piloted by NPC subsystem when assigned */

/* Bit helpers */
static inline int
ship_has_flag (int flags, int mask)
{
  return (flags & mask) != 0;
}

static inline int
ship_set_flag (int flags, int mask)
{
  return flags | mask;
}

static inline int
ship_clr_flag (int flags, int mask)
{
  return flags & ~mask;
}


typedef struct
{
  int fd;
  volatile sig_atomic_t *running;
  struct sockaddr_in peer;
  uint64_t cid;
  int player_id;
  int sector_id;

  /* --- rate limit hints --- */
  time_t rl_window_start;	/* epoch sec when current window began */
  int rl_count;			/* responses sent in this window */
  int rl_limit;			/* max responses per window */
  int rl_window_sec;		/* window length in seconds */
} client_ctx_t;


void now_iso8601 (char out[25]);	/* "YYYY-MM-DDTHH:MM:SSZ" */
/* Remove ANSI escape sequences from src into dst (cap bytes incl NUL). */
void strip_ansi (char *dst, const char *src, size_t cap);



#endif
