#ifndef SERVER_AUTOPILOT_H
#define SERVER_AUTOPILOT_H
#include "common.h"

/* Autopilot is purely client-side.
 * These commands provide pathing data and simple status checks,
 * but no server-side state or automated movement loop is maintained.
 */
int cmd_move_autopilot_start (client_ctx_t * ctx, json_t * root);
int cmd_move_autopilot_status (client_ctx_t * ctx, json_t * root);
int cmd_move_autopilot_stop (client_ctx_t * ctx, json_t * root);
#endif
