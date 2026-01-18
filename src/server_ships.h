/* src/server_ships.h */
#ifndef SERVER_SHIPS_H
#define SERVER_SHIPS_H
#include <jansson.h>
#include <stdbool.h>
#include "common.h"
#include "db/db_api.h"

typedef enum
{
  KILL_CAUSE_COMBAT = 1,
  KILL_CAUSE_MINES = 2,
  KILL_CAUSE_QUASAR = 3,
  KILL_CAUSE_NAVHAZ = 4,
  KILL_CAUSE_SELF_DESTRUCT = 5,
  KILL_CAUSE_OTHER = 99
} ship_kill_cause_t;

typedef struct
{
  int victim_player_id;
  int victim_ship_id;
  int killer_player_id;
  ship_kill_cause_t cause;
  int sector_id;
} ship_kill_context_t;

int handle_ship_destruction (db_t * db, ship_kill_context_t * ctx);


int cmd_ship_transfer_cargo (client_ctx_t * ctx, json_t * root);
int cmd_ship_upgrade (client_ctx_t * ctx, json_t * root);
int cmd_ship_repair (client_ctx_t * ctx, json_t * root);
int cmd_ship_inspect (client_ctx_t * ctx, json_t * root);
int cmd_ship_rename (client_ctx_t * ctx, json_t * root);
int cmd_ship_claim (client_ctx_t * ctx, json_t * root);
int cmd_ship_status (client_ctx_t * ctx, json_t * root);
int cmd_ship_info_compat (client_ctx_t * ctx, json_t * root);
int cmd_ship_self_destruct (client_ctx_t * ctx, json_t * root);
int cmd_ship_tow (client_ctx_t * ctx, json_t * root);
int cmd_ship_list (client_ctx_t * ctx, json_t * root);
int cmd_ship_sell (client_ctx_t * ctx, json_t * root);
int cmd_ship_transfer (client_ctx_t * ctx, json_t * root);
// void handle_move_pathfind (client_ctx_t *ctx, json_t *root);
int h_get_active_ship_id (db_t * db, int player_id);
int h_update_ship_cargo (db_t * db,
			 int ship_id,
			 const char *commodity, int delta, int *new_qty);


#endif
