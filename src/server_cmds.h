/* src/server_cmds.h */
#ifndef SERVER_CMDS_H
#define SERVER_CMDS_H
#include <jansson.h>
#include <stdbool.h>
#include "common.h"
#include "db/db_api.h"

int cmd_auth_login (client_ctx_t * ctx, json_t * root);
int cmd_auth_logout (client_ctx_t * ctx, json_t * root);
int cmd_auth_register (client_ctx_t * ctx, json_t * root);
int cmd_auth_refresh (client_ctx_t * ctx, json_t * root);
int cmd_auth_mfa_totp_verify (client_ctx_t * ctx, json_t * root);
int cmd_user_create (client_ctx_t * ctx, json_t * root);

int play_login (const char *user, const char *pass, int *pid);
int user_create (db_t * db, const char *user, const char *pass,
		 const char *ship_name, int *pid);

int cmd_sys_cluster_init (client_ctx_t * ctx, json_t * root);
int cmd_sys_cluster_seed_illegal_goods (client_ctx_t * ctx, json_t * root);
int cmd_sys_test_news_cron (client_ctx_t * ctx, json_t * root);
int cmd_sys_raw_sql_exec (client_ctx_t * ctx, json_t * root);
int cmd_bounty_post_federation (client_ctx_t * ctx, json_t * root);
int cmd_bounty_post_hitlist (client_ctx_t * ctx, json_t * root);
int cmd_bounty_list (client_ctx_t * ctx, json_t * root);
int cmd_sys_econ_planet_status (client_ctx_t * ctx, json_t * root);
int cmd_sys_econ_port_status (client_ctx_t * ctx, json_t * root);
int cmd_sys_econ_orders_summary (client_ctx_t * ctx, json_t * root);
int cmd_sys_npc_ferengi_tick_once (client_ctx_t * ctx, json_t * root);
int cmd_debug_run_fedspace_cleanup (client_ctx_t * ctx, json_t * root);

int send_error_response (client_ctx_t * ctx,
			 json_t * root, int code, const char *msg);
int send_json_response (client_ctx_t * ctx, json_t * root, json_t * json);

#endif
