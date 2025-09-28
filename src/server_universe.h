#ifndef SERVER_UNIVERSE_H
#define SERVER_UNIVERSE_H

#include "config.h"

/* Insert default config values into DB if missing */
int initconfig (void);

/* Initialise universe (check DB, run bigbang if needed) */
int universe_init (void);

/* Shutdown universe (cleanup hooks if needed) */
void universe_shutdown (void);

json_t *build_sector_info_json (int sector_id);

int cmd_move_warp(client_ctx_t *ctx, json_t *root);
int cmd_move_pathfind (client_ctx_t *ctx, json_t *root);
int cmd_move_autopilot_start (client_ctx_t *ctx, json_t *root);
int cmd_move_autopilot_stop (client_ctx_t *ctx, json_t *root);
int cmd_move_autopilot_status (client_ctx_t *ctx, json_t *root);
int cmd_sector_search (client_ctx_t *ctx, json_t *root);

#endif /* SERVER_UNIVERSE_H */
