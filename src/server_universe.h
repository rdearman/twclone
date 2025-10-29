#ifndef SERVER_UNIVERSE_H
#define SERVER_UNIVERSE_H

#include "config.h"
#include <stdint.h>

/* --- Ferringhi traders (NPC) --- */
int  fer_init_once(void);          /* returns 1 if homeworld found, else 0 */
void fer_tick(int64_t now_ms);     /* drive traders on a schedule */
void fer_attach_db(sqlite3 *db);

/* Orion Syndicate (ORI) patrol API */
int ori_init_once(void);                           // returns 1 if Orion ships/sectors found
void ori_attach_db(sqlite3 *db);                   // Attaches the main DB handle
void ori_tick(int64_t now_ms);                     // Executes the Orion movement logic


/* --- Small nav helpers over sector_warps (no DB args; use cached handle) --- */
int  nav_next_hop(int start, int goal);   /* one-hop BFS toward goal */
int  nav_random_neighbor(int sector);     /* random linked neighbour */
int  sector_has_port (int sector);         /* 1 if sector has a port */


 /* ISS patrol, universe helpers, etc. */
 /* Imperial Starship (ISS) patrol/summon API */
int iss_init_once (void);	// returns 1 if ISS + Stardock found
void iss_tick (int64_t now_ms);
void iss_summon (int sector_id, int offender_id);	// call this from violation handlers

/* Insert default config values into DB if missing */
int initconfig (void);

/* Initialise universe (check DB, run bigbang if needed) */
int universe_init (void);

/* Shutdown universe (cleanup hooks if needed) */
void universe_shutdown (void);

json_t *build_sector_info_json (int sector_id);

int cmd_move_warp (client_ctx_t * ctx, json_t * root);
int cmd_move_pathfind (client_ctx_t * ctx, json_t * root);
int cmd_move_autopilot_start (client_ctx_t * ctx, json_t * root);
int cmd_move_autopilot_stop (client_ctx_t * ctx, json_t * root);
int cmd_move_autopilot_status (client_ctx_t * ctx, json_t * root);
int cmd_sector_search (client_ctx_t * ctx, json_t * root);
int cmd_move_describe_sector (client_ctx_t * ctx, json_t * root);
void cmd_move_scan (client_ctx_t * ctx, json_t * root);
int cmd_sector_set_beacon (client_ctx_t * ctx, json_t * root);
void cmd_sector_info (int fd, json_t * root, int sector_id, int player_id);


/**** Think this could be removed later. *******/
#define MAX_CITADEL_LEVEL 5

struct planetType_struct
{
  char *typeDescription;
  char *typeClass;
  char *typeName;
  int citadelUpgradeTime[MAX_CITADEL_LEVEL];
  int citadelUpgradeOre[MAX_CITADEL_LEVEL];
  int citadelUpgradeOrganics[MAX_CITADEL_LEVEL];
  int citadelUpgradeEquipment[MAX_CITADEL_LEVEL];
  int citadelUpgradeColonist[MAX_CITADEL_LEVEL];
  int maxColonist[3];		/* max colonist in ore,organics,equp */
  int fighters;
  int fuelProduction;
  int organicsProduction;
  int equipmentProduction;
  int fighterProduction;
  int maxore;
  int maxorganics;
  int maxequipment;
  int maxfighters;
  float breeding;
};

typedef struct planetType_struct planetClass;



#endif /* SERVER_UNIVERSE_H */
