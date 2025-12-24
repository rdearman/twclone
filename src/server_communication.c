/* src/server_communication.c */
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

/* local includes */
#include "server_communication.h"
#include "server_envelope.h"
#include "server_players.h"
#include "errors.h"
#include "config.h"
#include "game_db.h"
#include "server_cmds.h"
#include "database.h"
#include "server_loop.h"
#include "db_player_settings.h"
#include "server_log.h"
#include "database_cmd.h"
#include "db/db_api.h"

int cmd_sys_notice_create (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_notice_list (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_notice_ack (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int server_broadcast_event (const char *type, json_t *data) { (void)type; (void)data; return 0; }
int server_broadcast_to_sector (int sid, const char *name, json_t *payload) { (void)sid; (void)name; (void)payload; return 0; }
void comm_broadcast_message (comm_scope_t scope, long long id, const char *msg, json_t *extra) { (void)scope; (void)id; (void)msg; (void)extra; }
void comm_publish_sector_event (int sid, const char *name, json_t *data) { (void)sid; (void)name; (void)data; }
void push_unseen_notices_for_player (client_ctx_t *ctx, int pid) { (void)ctx; (void)pid; }
int cmd_admin_notice_create (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_notice_dismiss (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_chat_send (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_chat_broadcast (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_chat_history (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_mail_send (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_mail_inbox (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_mail_read (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_mail_delete (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
void comm_clear_subscriptions (client_ctx_t *ctx) { (void)ctx; }
int cmd_subscribe_add (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_subscribe_remove (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_subscribe_list (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_subscribe_catalog (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_s2s_event_relay (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_s2s_planet_genesis (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_s2s_planet_transfer (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_s2s_player_migrate (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_s2s_port_restock (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_s2s_replication_heartbeat (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_admin_notice (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_admin_shutdown_warning (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_stock_portfolio_list (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }