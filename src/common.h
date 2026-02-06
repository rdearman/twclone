#ifndef COMMON_H
#define COMMON_H
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
#include <netinet/in.h>
/* local includes */

#include "schemas.h"
#include "errors.h"
#include "config.h"
#include <stdatomic.h>
#include <netinet/in.h>
#include <string.h>

// Entity types for generic trading
#define ENTITY_TYPE_PORT "port"
#define ENTITY_TYPE_PLANET "planet"

typedef struct
{
  int fd;
  volatile sig_atomic_t *running;
  struct sockaddr_in peer;
  uint64_t cid;
  int player_id;
  int ship_id;
  int sector_id;
  int corp_id;			// Added for corporation ID
  /* --- rate limit hints --- */
  time_t rl_window_start;
  int rl_count;
  int rl_limit;
  int rl_window_sec;

  /* --- bulk execution capture --- */
  json_t *captured_envelopes;	// If non-NULL, responses are appended here instead of sent
  int captured_envelopes_valid;	/* 1 if captured_envelopes was allocated by server and is safe to use */
  int responses_sent;		// Hardening: ensure every request gets a reply
  
  /* --- TLS support --- */
  void *ssl_conn;		// SSL* (opaque pointer to avoid OpenSSL in common.h)
  int is_tls;			// 1 if TLS, 0 if plaintext
  unsigned char ssl_read_buf[8192];  // Buffer for SSL_read remnants
  size_t ssl_read_pos;		// Current read position in buffer
  size_t ssl_read_used;		// Bytes actually in buffer
} client_ctx_t;
// Structure to represent a commodity's essential data
typedef struct
{
  int id;
  const char *code;
  const char *name;
  bool is_illegal;		// From commodities.illegal
  // Add other relevant commodity fields if needed, e.g., base_price, volatility
} commodity_t;
// A simple structure to represent the result of the comsume player turn function
typedef enum
{
  TURN_CONSUME_SUCCESS = 0,
  TURN_CONSUME_ERROR_GENERIC = 1,
  TURN_CONSUME_ERROR_NO_TURNS = -1,
  TURN_CONSUME_ERROR_INVALID_AMOUNT = -2,
  TURN_CONSUME_ERROR_DB = -3,
  TURN_CONSUME_ERROR_DB_FAIL = -3,
  TURN_CONSUME_ERROR_PLAYER_NOT_FOUND = -4
} TurnConsumeResult;
#ifndef ASSET_TYPE_T_DEFINED
#define ASSET_TYPE_T_DEFINED
typedef enum
{
  ASSET_MINE = 1,
  ASSET_FIGHTER = 2,
  ASSET_BEACON = 3,
  ASSET_LIMPET_MINE = 4
} asset_type_t;
#endif // ASSET_TYPE_T_DEFINED
typedef enum
{
  OFFENSE_TOLL = 1,
  OFFENSE_DEFEND = 2,
  OFFENSE_ATTACK = 3
} offense_type_t;
#ifndef START_FIGHTERS
#define START_FIGHTERS 20
#endif
#ifndef MAX_FIGHTERS_PER_SECTOR
#define MAX_FIGHTERS_PER_SECTOR 50000
#endif
#ifndef SECTOR_MINE_CAP
#define SECTOR_MINE_CAP 500
#endif
#ifndef SECTOR_FIGHTER_CAP
#define SECTOR_FIGHTER_CAP 10000
#endif
#ifndef MINE_SECTOR_CAP_PER_TYPE
#define MINE_SECTOR_CAP_PER_TYPE 250
#endif
#ifndef START_HOLDS
#define START_HOLDS 20
#endif
#ifndef DEF_PORT_MAX_SLV
#define DEF_PORT_MAX_SLV 10000
#endif
#ifndef DEF_PORT_MAX_WPN
#define DEF_PORT_MAX_WPN 10000
#endif
#ifndef DEF_PORT_MAX_DRG
#define DEF_PORT_MAX_DRG 10000
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
#ifndef BUFF_SIZE
#define BUFF_SIZE 5000
#endif
#ifndef MAX_TOTAL_PLANETS
#define MAX_TOTAL_PLANETS 200
#endif
#ifndef MAX_WARPS_PER_SECTOR
#define MAX_WARPS_PER_SECTOR 6
#endif
#define MIN(a,b) (((a) < (b))?(a):(b))
#define MAX(a,b) (((a) > (b))?(a):(b))
int init_sockaddr (int, struct sockaddr_in *);
int init_clientnetwork (char *hostname, int new_port);
//int sendinfo (int sockid, char *buffer);
//int recvinfo (int sockid, char *buffer);
int acceptnewconnection (int sockid);
int randomnum (int min, int max);
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


// Armid Mine Configuration
typedef struct
{
  struct
  {
    bool enabled;
    double base_trigger_chance;
    double max_trigger_chance;
    int damage_per_mine;
    double min_fraction_exploded;
    double max_fraction_exploded;
  } armid;
  struct
  {
    bool enabled;
    double mines_per_fighter_avg;
    double mines_per_fighter_var;
    double fighter_loss_per_mine;
    double max_fraction_per_sweep;
  } sweep;
} armid_mine_config_t;
// Structure to mirror the sector_assets table for in-memory use
typedef struct
{
  int id;
  int sector;
  int player;
  int corporation;
  int asset_type;
  int offensive_setting;
  int quantity;
  time_t ttl;			// Using time_t for UNIX epoch expiry
  time_t deployed_at;
} sector_asset_t;
// Structure to represent a ship's combat-relevant stats for damage application
typedef struct
{
  int id;
  int shields;
  int fighters;
  int hull;
  // Add other relevant ship fields if needed for future combat logic
} ship_t;
typedef struct
{
  int shields_lost;
  int fighters_lost;
  int hull_lost;
} armid_damage_breakdown_t;
typedef struct
{
  int sector_id;
  int armid_triggered;		/* total exploded */
  int armid_remaining;		/* sum of remaining hostile mines after updates */
  int limpet_triggered;		/* total limpets triggered */
  int limpet_remaining;		/* sum of remaining limpets after updates */
  int shields_lost;
  int fighters_lost;
  int hull_lost;
  bool destroyed;
} armid_encounter_t;
void now_iso8601 (char out[25]);	/* "YYYY-MM-DDTHH:MM:SSZ" */
/* Remove ANSI escape sequences from src into dst (cap bytes incl NUL). */
void strip_ansi (char *dst, const char *src, size_t cap);
const char *get_tow_reason_string (int reason_code);
// Utility functions for random numbers and clamping
double rand01 ();
double rand_range (double min, double max);
double clamp (double value, double min, double max);
int get_random_int (int min, int max);
// JSON helper functions (prototypes)
bool json_get_int_flexible (json_t * data_obj, const char *key, int *out_val);
bool json_get_int64_flexible (json_t * data_obj,
			      const char *key, long long *out_val);
const char *json_get_string_or_null (json_t * data_obj, const char *key);

// Global Atomic Counters
extern atomic_int_fast64_t g_trades_executed;
extern atomic_int_fast64_t g_warps_performed;
extern atomic_int_fast64_t g_combat_rounds;
extern atomic_int_fast64_t g_failed_operations;
extern atomic_int_fast64_t g_server_tick;


void h_copy_cstr (char *dst, size_t dst_sz, const char *src);
void h_format_credits (char *dst, size_t dst_sz, long long credits);


#endif // COMMON_H
