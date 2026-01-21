#ifndef SERVER_SYSOP_H
#define SERVER_SYSOP_H

#include <jansson.h>
#include "common.h"

/* Config v1 */
int cmd_sysop_config_list(client_ctx_t *ctx, json_t *root);
int cmd_sysop_config_get(client_ctx_t *ctx, json_t *root);
int cmd_sysop_config_set(client_ctx_t *ctx, json_t *root);
int cmd_sysop_config_history(client_ctx_t *ctx, json_t *root);

/* Phase 2: Player Ops */
int cmd_sysop_players_search(client_ctx_t *ctx, json_t *root);
int cmd_sysop_player_get(client_ctx_t *ctx, json_t *root);
int cmd_sysop_player_kick(client_ctx_t *ctx, json_t *root);
int cmd_sysop_player_sessions_get(client_ctx_t *ctx, json_t *root);
int cmd_sysop_universe_summary(client_ctx_t *ctx, json_t *root);

/* Phase 3: Engine & Jobs */
int cmd_sysop_engine_status_get(client_ctx_t *ctx, json_t *root);
int cmd_sysop_jobs_list(client_ctx_t *ctx, json_t *root);
int cmd_sysop_jobs_get(client_ctx_t *ctx, json_t *root);
int cmd_sysop_jobs_retry(client_ctx_t *ctx, json_t *root);
int cmd_sysop_jobs_cancel(client_ctx_t *ctx, json_t *root);

/* Phase 4: Messaging */
int cmd_sysop_notice_create(client_ctx_t *ctx, json_t *root);
int cmd_sysop_notice_delete(client_ctx_t *ctx, json_t *root);
int cmd_sysop_broadcast_send(client_ctx_t *ctx, json_t *root);
int cmd_sysop_logs_tail(client_ctx_t *ctx, json_t *root);
int cmd_sysop_logs_clear(client_ctx_t *ctx, json_t *root);

/* Helper to kick a player (close connection) */
int server_sysop_kick_player(int target_player_id);

#endif /* SERVER_SYSOP_H */
