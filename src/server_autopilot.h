/* src/server_autopilot.h */
#ifndef SERVER_AUTOPILOT_H
#define SERVER_AUTOPILOT_H
#include <jansson.h>
#include "common.h"
#include "db/db_api.h"

int cmd_move_autopilot_start (db_t * db, client_ctx_t * ctx, json_t * root);
int cmd_move_autopilot_status (client_ctx_t * ctx, json_t * root);
int cmd_move_autopilot_stop (client_ctx_t * ctx, json_t * root);
int cmd_move_autopilot_control (client_ctx_t * ctx, json_t * root);
int server_autopilot_on_warp (db_t *db, int player_id, int sector_id);

#endif
