#ifndef SERVER_SHIPS_H
#define SERVER_SHIPS_H
#ifdef __cplusplus
extern "C"
{
#endif
#include <jansson.h>            // for json_t
#include "server_loop.h"        // for ctx_t definition used in the function bodies
#include "common.h"             // adjust if client_ctx_t is defined in another header
#include "errors.h"
#include <sqlite3.h>            // For sqlite3 *db parameter
#include "server_config.h"      // For g_cfg
// Enum for kill causes
typedef enum
{
  KILL_CAUSE_COMBAT,
  KILL_CAUSE_MINES,
  KILL_CAUSE_QUASAR,
  KILL_CAUSE_NAVHAZ,
  KILL_CAUSE_SELF_DESTRUCT,
  KILL_CAUSE_OTHER
} kill_cause_t;
// Context struct for ship destruction
typedef struct
{
  int victim_player_id;
  int victim_ship_id;
  int killer_player_id;         // 0 if NPC/neutral/hazard
  kill_cause_t cause;
  int sector_id;
} ship_kill_context_t;
// Central handler for ship destruction
int handle_ship_destruction (sqlite3 *db, ship_kill_context_t *ctx);
// Big Sleep handling function
int handle_big_sleep (sqlite3 *db, ship_kill_context_t *ctx);
// Escape Pod spawning function
int handle_escape_pod_spawn (sqlite3 *db, ship_kill_context_t *ctx);
/* Exposed handlers implemented in server_ships.c */
void handle_move_pathfind (client_ctx_t *ctx, json_t *root);
typedef struct ctx ctx_t;       // or: typedef struct server_ctx ctx_t;
int cmd_ship_inspect (client_ctx_t *ctx, json_t *root);
int cmd_ship_rename (client_ctx_t *ctx, json_t *root);
int cmd_ship_claim (client_ctx_t *ctx, json_t *root);
int cmd_ship_status (client_ctx_t *ctx, json_t *root);
int cmd_ship_info_compat (client_ctx_t *ctx, json_t *root);
int cmd_ship_transfer_cargo (client_ctx_t *ctx, json_t *root);
int cmd_ship_jettison (client_ctx_t *ctx, json_t *root);
int cmd_ship_upgrade (client_ctx_t *ctx, json_t *root);
int cmd_ship_repair (client_ctx_t *ctx, json_t *root);
int cmd_ship_self_destruct (client_ctx_t *ctx, json_t *root);
int cmd_ship_tow (client_ctx_t *ctx, json_t *root);
// Helper to get active ship ID for a player
int h_get_active_ship_id (sqlite3 *db, int player_id);
// Helper to update ship cargo with constraints
int h_update_ship_cargo (sqlite3 *db, int ship_id, const char *commodity_code, int delta, int *new_quantity_out);
// Helper to generate a hex UUID
void h_generate_hex_uuid(char *buffer, size_t buffer_size);
// Helper to get ship cargo and holds
int h_get_ship_cargo_and_holds(sqlite3 *db, int ship_id, int *ore, int *organics, int *equipment, int *colonists, int *slaves, int *weapons, int *drugs, int *holds);
#ifdef __cplusplus
}
#endif
#endif                          /* SERVER_SHIPS_H */