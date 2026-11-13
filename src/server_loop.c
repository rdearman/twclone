#include "db/repo/repo_session.h"
/* src/server_loop.c */
#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include "db/repo/repo_database.h"
#include "db/repo/repo_ships.h"
#include "db/repo/repo_communication.h"
#include <jansson.h>
#include <stdbool.h>

/* local includes */
#include "db/repo/repo_database.h"
#include "game_db.h"
#include "schemas.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_ships.h"
#include "common.h"
#include "server_envelope.h"
#include "server_players.h"
#include "server_ports.h"
#include "server_auth.h"
#include "server_s2s.h"
#include "server_universe.h"
#include "server_autopilot.h"
#include "server_config.h"
#include "server_communication.h"
#include "server_planets.h"
#include "server_citadel.h"
#include "server_combat.h"
#include "server_bulk.h"
#include "server_news.h"
#include "server_log.h"
#include "server_stardock.h"
#include "server_corporation.h"
#include "server_bank.h"
#include "server_cron.h"
#include "repo_cmd.h"
#include "db/db_api.h"
#include "db/sql_driver.h"
#include "server_sysop.h"

typedef int (*command_handler_fn) (client_ctx_t * ctx, json_t * root);

typedef struct
{
  const char *name;
  command_handler_fn handler;
  const char *summary;
  schema_generator_fn schema;
  int flags;
  bool is_deprecated;
  const char *replacement;
} command_entry_t;

#ifndef streq
#define streq(a,b) (strcasecmp (json_string_value ((a)), (b)) == 0)
#endif
#define BUF_SIZE    8192

__thread client_ctx_t *g_ctx_for_send = NULL;

/* TLS context (global, thread-safe) */
static SSL_CTX *g_ssl_ctx = NULL;
static atomic_int g_active_client_connections = 0;

/* forward declaration to avoid implicit extern */
void send_all_json (int fd, json_t * obj);

/* Forward declarations */
static void loop_add_client (client_ctx_t *ctx);
static void loop_remove_client (client_ctx_t *ctx);

/* rate-limit helper prototypes (defined later) */
void attach_rate_limit_meta (json_t * env, client_ctx_t * ctx);
void rl_tick (client_ctx_t * ctx);

/* EXTERN FIXES: Added missing externs and corrected signatures */
extern int cmd_bounty_list (client_ctx_t * ctx, json_t * root);
extern int cmd_bounty_post_federation (client_ctx_t * ctx, json_t * root);
extern int cmd_bounty_post_hitlist (client_ctx_t * ctx, json_t * root);
extern int cmd_player_set_trade_account_preference (client_ctx_t * ctx,
						    json_t * root);
extern int cmd_move_transwarp (client_ctx_t * ctx, json_t * root);
extern int cmd_sector_mine_disrupt (client_ctx_t * ctx, json_t * root);
extern int cmd_insurance_policies_list (client_ctx_t * ctx, json_t * root);
extern int cmd_insurance_policies_buy (client_ctx_t * ctx, json_t * root);
extern int cmd_insurance_claim_file (client_ctx_t * ctx, json_t * root);
extern int cmd_player_get_topics (client_ctx_t * ctx, json_t * root);
extern int cmd_player_set_topics (client_ctx_t * ctx, json_t * root);
extern int cmd_sys_cron_planet_tick_once (client_ctx_t * ctx, json_t * root);
extern int cmd_equity_dividend_set (client_ctx_t * ctx, json_t * root);
extern int cmd_trade_accept (client_ctx_t * ctx, json_t * root);
extern int cmd_trade_cancel (client_ctx_t * ctx, json_t * root);
extern int cmd_trade_offer (client_ctx_t * ctx, json_t * root);
extern int cmd_trade_offers (client_ctx_t * ctx, json_t * root);
extern int cmd_ferengi_traders (client_ctx_t *ctx, json_t *root);
extern int cmd_ferengi_deal_accept (client_ctx_t *ctx, json_t *root);
extern int cmd_ferengi_deal_reject (client_ctx_t *ctx, json_t *root);
extern json_t *schema_ferengi_traders (void);
extern json_t *schema_ferengi_deal_action (void);
extern int cmd_ship_list (client_ctx_t * ctx, json_t * root);
extern int cmd_ship_sell (client_ctx_t * ctx, json_t * root);
extern int cmd_ship_transfer (client_ctx_t * ctx, json_t * root);
extern int cmd_shipyard_sell (client_ctx_t * ctx, json_t * root);
extern int cmd_bulk_execute (client_ctx_t * ctx, json_t * root);
extern int cmd_trade_history (client_ctx_t * ctx, json_t * root);
extern int cmd_trade_quote (client_ctx_t * ctx, json_t * root);
extern int cmd_trade_sell (client_ctx_t * ctx, json_t * root);
extern int cmd_police_bribe (client_ctx_t * ctx, json_t * root);
extern int cmd_police_surrender (client_ctx_t * ctx, json_t * root);
extern int handle_police_status (client_ctx_t * ctx, json_t * root);

/* UPDATED SIGNATURE: passing db_t* as first arg */
extern int cmd_move_autopilot_start (db_t * db, client_ctx_t * ctx,
				     json_t * root);

static inline uint64_t
monotonic_millis (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}

/* Insurance settlement operations live in the shared C repository so both
   PostgreSQL and MySQL use the same lock and ledger semantics. */

static int
insurance_subject_owned (db_t *db, const char *subject_type, int64_t subject_id,
                         int64_t player_id)
{
  const char *query = NULL;
  if (strcmp (subject_type, "ship") == 0 || strcmp (subject_type, "cargo") == 0)
    query = "SELECT 1 FROM ship_ownership WHERE ship_id = {1} AND player_id = {2} AND is_primary = {3} LIMIT 1;";
  else if (strcmp (subject_type, "planet") == 0)
    query = "SELECT 1 FROM planets WHERE planet_id = {1} AND owner_id = {2} LIMIT 1;";
  else
    return 0;
  char sql[512]; sql_build (db, query, sql, sizeof sql);
  db_res_t *res = NULL; db_error_t err = {0};
  db_bind_t binds[3] = {db_bind_i64 (subject_id), db_bind_i64 (player_id), db_bind_bool (true)};
  int count = strcmp (subject_type, "planet") == 0 ? 2 : 3;
  if (!db_query (db, sql, binds, count, &res, &err)) return -1;
  int owned = db_res_step (res, &err) ? 1 : 0;
  db_res_finalize (res);
  return owned;
}

static int
insurance_claim_owned (db_t *db, int64_t claim_id, int64_t player_id)
{
  const char *query = "SELECT 1 FROM insurance_claims c JOIN insurance_policies p ON p.insurance_policies_id = c.policy_id WHERE c.insurance_claims_id = {1} AND p.holder_type = {2} AND p.holder_id = {3} LIMIT 1;";
  char sql[512]; sql_build (db, query, sql, sizeof sql);
  db_res_t *res = NULL; db_error_t err = {0};
  if (!db_query (db, sql, (db_bind_t[]){db_bind_i64 (claim_id), db_bind_text ("player"), db_bind_i64 (player_id)}, 3, &res, &err)) return -1;
  int owned = db_res_step (res, &err) ? 1 : 0;
  db_res_finalize (res);
  return owned;
}

int
cmd_insurance_policies_list (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED,
                           "Authentication required");
      return 0;
    }
  db_t *db = game_db_get_handle ();
  db_res_t *res = NULL;
  db_error_t err = {0};
  char sql[512];
  sql_build (db, "SELECT insurance_policies_id, subject_type, subject_id, premium, payout, start_ts, expiry_ts, active FROM insurance_policies WHERE holder_type = {1} AND holder_id = {2} ORDER BY insurance_policies_id DESC;", sql, sizeof sql);
  if (!db_query (db, sql, (db_bind_t[]){db_bind_text ("player"), db_bind_i64 (ctx->player_id)}, 2, &res, &err))
    {
      send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Unable to list insurance policies");
      return 0;
    }
  json_t *payload = json_object ();
  json_t *policies = json_array ();
  while (db_res_step (res, &err))
    {
      json_t *policy = json_object ();
      const char *subject = db_res_col_text (res, 1, &err);
      const char *start = db_res_col_text (res, 5, &err);
      const char *expiry = db_res_col_text (res, 6, &err);
      json_object_set_new (policy, "policy_id", json_integer (db_res_col_i64 (res, 0, &err)));
      json_object_set_new (policy, "subject_type", json_string (subject ? subject : ""));
      json_object_set_new (policy, "subject_id", json_integer (db_res_col_i64 (res, 2, &err)));
      json_object_set_new (policy, "premium", json_integer (db_res_col_i64 (res, 3, &err)));
      json_object_set_new (policy, "payout", json_integer (db_res_col_i64 (res, 4, &err)));
      json_object_set_new (policy, "start_ts", json_string (start ? start : ""));
      if (expiry) json_object_set_new (policy, "expiry_ts", json_string (expiry));
      json_object_set_new (policy, "active", json_boolean (db_res_col_int (res, 7, &err)));
      json_array_append_new (policies, policy);
    }
  db_res_finalize (res);
  json_object_set_new (payload, "policies", policies);
  send_response_ok_take (ctx, root, "insurance.policies.list.response",
			 &payload);
  return 0;
}

int
cmd_insurance_policies_buy (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  const char *subject_type = json_string_value (json_object_get (data, "subject_type"));
  int64_t subject_id = json_integer_value (json_object_get (data, "subject_id"));
  int64_t premium = json_integer_value (json_object_get (data, "premium"));
  int64_t payout = json_integer_value (json_object_get (data, "payout"));
  int64_t fund_id = json_integer_value (json_object_get (data, "fund_id"));
  int duration = (int) json_integer_value (json_object_get (data, "duration"));
  if (!subject_type || strcmp (subject_type, "ship") != 0
      || subject_id <= 0 || premium < 0 || payout < 0 || fund_id <= 0
      || duration <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "A ship subject, premium, payout, fund_id and duration are required");
      return 0;
    }
  db_t *db = game_db_get_handle ();
  int owned = insurance_subject_owned (db, subject_type, subject_id, ctx->player_id);
  if (owned < 0)
    { send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Unable to validate insured subject"); return 0; }
  if (!owned)
    { send_response_error (ctx, root, ERR_PERMISSION_DENIED, "Insured subject is not owned by the player"); return 0; }
  char start_ts[32], expiry_ts[32];
  time_t now = time (NULL);
  snprintf (start_ts, sizeof start_ts, "%lld", (long long) now);
  snprintf (expiry_ts, sizeof expiry_ts, "%lld", (long long) now + duration);
  int64_t policy_id = 0;
  int purchase_rc = repo_ships_insurance_buy_policy
    (db, ctx->player_id, subject_type, subject_id, premium, payout, fund_id,
     start_ts, expiry_ts, &policy_id);
  if (purchase_rc != 0)
    { send_response_error (ctx, root, purchase_rc, "Insurance purchase refused"); return 0; }
  json_t *payload = json_pack ("{s:I,s:s}", "policy_id", (json_int_t) policy_id, "status", "purchased");
  send_response_ok_take (ctx, root, "insurance.policies.buy.response", &payload);
  return 0;
}

int
cmd_insurance_claim_file (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    { send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required"); return 0; }
  json_t *data = json_object_get (root, "data");
  int64_t claim_id = json_integer_value (json_object_get (data, "claim_id"));
  int64_t amount = json_integer_value (json_object_get (data, "amount"));
  const char *event_id = json_string_value (json_object_get (data, "event_id"));
  if (claim_id <= 0 || amount < 0 || !event_id || !*event_id)
    { send_response_error (ctx, root, ERR_INVALID_ARG, "claim_id, event_id and amount are required"); return 0; }
  db_t *db = game_db_get_handle ();
  int owned = insurance_claim_owned (db, claim_id, ctx->player_id);
  if (owned < 0) { send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Unable to validate claim ownership"); return 0; }
  if (!owned) { send_response_error (ctx, root, ERR_PERMISSION_DENIED, "Claim is not owned by the player"); return 0; }
  int64_t settled_id = 0;
  int claim_rc = repo_ships_insurance_settle_claim
    (db, ctx->player_id, claim_id, event_id, amount, &settled_id);
  if (claim_rc != 0)
    { send_response_error (ctx, root, claim_rc, "Insurance claim refused"); return 0; }
  json_t *payload = json_pack ("{s:I,s:s}", "claim_id", (json_int_t) settled_id, "status", "settled");
  send_response_ok_take (ctx, root, "insurance.claim.file.response", &payload);
  return 0;
}

/* --------------------------------------------------------------------------
   Command Registry & Dispatch
   -------------------------------------------------------------------------- */

/* Wrappers for void or mismatched handlers */
static int
w_nav_avoid_add (client_ctx_t *ctx, json_t *root)
{
  cmd_nav_avoid_add (ctx, root);
  return 0;
}

static int
w_nav_avoid_remove (client_ctx_t *ctx, json_t *root)
{
  cmd_nav_avoid_remove (ctx, root);
  return 0;
}

static int
w_nav_bookmark_add (client_ctx_t *ctx, json_t *root)
{
  cmd_nav_bookmark_add (ctx, root);
  return 0;
}

static int
w_nav_bookmark_remove (client_ctx_t *ctx, json_t *root)
{
  cmd_nav_bookmark_remove (ctx, root);
  return 0;
}

static int
w_nav_bookmark_list (client_ctx_t *ctx, json_t *root)
{
  cmd_nav_bookmark_list (ctx, root);
  return 0;
}

static int
w_nav_avoid_list (client_ctx_t *ctx, json_t *root)
{
  cmd_nav_avoid_list (ctx, root);
  return 0;
}

static int
w_sector_scan (client_ctx_t *ctx, json_t *root)
{
  cmd_sector_scan (ctx, root);
  return 0;
}

static int
w_sector_scan_density (client_ctx_t *ctx, json_t *root)
{
  cmd_sector_scan_density ((void *) ctx, root);
  return 0;
}

/* Special logic for auth.login to push notices */
static int
w_auth_login (client_ctx_t *ctx, json_t *root)
{
  int rc = cmd_auth_login (ctx, root);
  if (rc)
    {
      push_unseen_notices_for_player (ctx, ctx->player_id);
    }
  return rc;
}

/* WRAPPER: Pass the DB handle to autopilot start */
static int
w_move_autopilot_start (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  return cmd_move_autopilot_start (db, ctx, root);
}


static const command_entry_t k_command_registry[] = {
  {"admin.notice", cmd_admin_notice, "Admin notice", schema_admin_notice, 0, false, NULL},
  {"admin.shutdown_warning", cmd_admin_shutdown_warning,
   "Admin shutdown warning", schema_admin_shutdown_warning, 0, false, NULL},
  {"auth.login", w_auth_login, "Authenticate", schema_auth_login,
   CMD_FLAG_AUTH_FREE, false, NULL},
  {"auth.logout", cmd_auth_logout, "Log out", schema_auth_logout, 0, false, NULL},
  {"auth.mfa.totp.verify", cmd_auth_mfa_totp_verify, "Second-factor code",
   schema_auth_mfa_totp_verify, 0, false, NULL},
  {"auth.register", cmd_auth_register, "Create a new player",
   schema_auth_register, CMD_FLAG_AUTH_FREE, false, NULL},
  {"auth.refresh", cmd_auth_refresh, "Refresh session token",
   schema_auth_refresh, 0, false, NULL},
  {"auth.change_password", cmd_auth_change_password, "Change password",
   schema_placeholder, 0, false, NULL},
  {"bank.balance", cmd_bank_balance, "Get player bank balance",
   schema_bank_balance, 0, false, NULL},
  {"bank.deposit", cmd_bank_deposit, "Deposit credits to bank",
   schema_bank_deposit, 0, false, NULL},
  {"bank.history", cmd_bank_history, "Get bank history", schema_bank_history,
   0, false, NULL},
  {"bank.leaderboard", cmd_bank_leaderboard, "Get bank leaderboard",
   schema_bank_leaderboard, 0, false, NULL},
  {"bank.orders.create", cmd_bank_orders_create, "Create recurring bank payment",
   schema_bank_orders_create, 0, false, NULL},
  {"bank.orders.list", cmd_bank_orders_list, "List recurring bank payments",
   schema_bank_orders_list, 0, false, NULL},
  {"bank.orders.cancel", cmd_bank_orders_cancel, "Cancel recurring bank payment",
   schema_bank_orders_cancel, 0, false, NULL},
  {"bank.transfer", cmd_bank_transfer, "Transfer credits between players",
   schema_bank_transfer, 0, false, NULL},
  {"bank.withdraw", cmd_bank_withdraw, "Withdraw credits from bank",
   schema_bank_withdraw, 0, false, NULL},
  {"bounty.list", cmd_bounty_list, "List bounties", schema_placeholder, 0, false, NULL},
  {"bounty.post_federation", cmd_bounty_post_federation,
   "Post a Federation bounty", schema_placeholder, 0, false, NULL},
  {"bounty.post_hitlist", cmd_bounty_post_hitlist, "Post a Hit List contract",
   schema_placeholder, 0, false, NULL},
  {"bulk.execute", cmd_bulk_execute, "Execute a bulk command",
   schema_bulk_execute, 0, false, NULL},
  {"fine.list", cmd_fine_list, "List player outstanding fines",
   schema_fine_list, 0, false, NULL},
  {"fine.pay", cmd_fine_pay, "Pay an outstanding fine", schema_fine_pay, 0, false, NULL},
  {"insurance.policies.list", cmd_insurance_policies_list,
   "List player insurance policies", schema_insurance_policies_list, 0, false, NULL},
  {"insurance.policies.buy", cmd_insurance_policies_buy,
   "Buy an insurance policy", schema_insurance_policies_buy, 0, false, NULL},
  {"insurance.claim.file", cmd_insurance_claim_file,
   "File an insurance claim", schema_insurance_claim_file, 0, false, NULL},
  {"chat.broadcast", cmd_chat_broadcast, "Broadcast a chat message",
   schema_chat_broadcast, 0, false, NULL},
  {"chat.history", cmd_chat_history, "Chat history", schema_chat_history, 0, false, NULL},
  {"chat.send", cmd_chat_send, "Send a chat message", schema_chat_send, 0, false, NULL},
  {"citadel.build", cmd_citadel_build, "Build a citadel",
   schema_citadel_build, 0, false, NULL},
  {"citadel.upgrade", cmd_citadel_upgrade, "Upgrade a citadel",
   schema_citadel_upgrade, 0, false, NULL},
  {"combat.attack", cmd_combat_attack, "Attack a target",
   schema_combat_attack, 0, false, NULL},
  {"combat.attack_planet", cmd_combat_attack_planet, "Attack a planet",
   schema_combat_attack_planet, 0, false, NULL},
  {"combat.deploy_fighters", cmd_combat_deploy_fighters, "Deploy fighters",
   schema_combat_deploy_fighters, 0, false, NULL},
  {"combat.deploy_mines", cmd_combat_deploy_mines, "Deploy mines",
   schema_combat_deploy_mines, 0, false, NULL},
  {"combat.lay_mines", cmd_combat_lay_mines, "Lay mines",
   schema_combat_lay_mines, 0, false, NULL},
  {"combat.status", cmd_combat_status, "Combat status", schema_combat_status,
   0, false, NULL},
  {"combat.sweep_mines", cmd_combat_sweep_mines, "Sweep for mines",
   schema_combat_sweep_mines, 0, false, NULL},
  {"deploy.fighters.list", cmd_deploy_fighters_list, "List deployed fighters",
   schema_deploy_fighters_list, 0, false, NULL},
  {"deploy.mines.list", cmd_deploy_mines_list, "List deployed mines",
   schema_deploy_mines_list, 0, false, NULL},
  {"fighters.recall", cmd_fighters_recall, "Recall deployed fighters",
   schema_fighters_recall, 0, false, NULL},
  {"mines.recall", cmd_mines_recall, "Recall deployed mines",
   schema_mines_recall, 0, false, NULL},
  {"corp.balance", cmd_corp_balance, "Get corporation balance",
   schema_corp_no_args, 0, false, NULL},
  {"corp.create", cmd_corp_create, "Create a new corporation",
   schema_corp_create, 0, false, NULL},
  {"corp.deposit", cmd_corp_deposit, "Deposit to corporation",
   schema_corp_amount, 0, false, NULL},
  {"corp.dissolve", cmd_corp_dissolve, "Dissolve a corporation",
   schema_corp_no_args, 0, false, NULL},
  {"corp.invite", cmd_corp_invite, "Invite player to corporation",
   schema_corp_target_player, 0, false, NULL},
  {"corp.join", cmd_corp_join, "Join a corporation via invite",
   schema_corp_join, 0, false, NULL},
  {"corp.kick", cmd_corp_kick, "Kick member from corporation",
   schema_corp_target_player, 0, false, NULL},
  {"corp.leave", cmd_corp_leave, "Leave current corporation",
   schema_corp_no_args, 0, false, NULL},
  {"corp.list", cmd_corp_list, "List all corporations", schema_corp_no_args,
   0, false, NULL},
  {"corp.roster", cmd_corp_roster, "List corporation members",
   schema_corp_roster, 0, false, NULL},
  {"corp.statement", cmd_corp_statement, "Get corporation statement",
   schema_corp_statement, 0, false, NULL},
  {"corp.status", cmd_corp_status, "Get corporation status",
   schema_corp_no_args, 0, false, NULL},
  {"corp.transfer_ceo", cmd_corp_transfer_ceo,
   "Transfer corporation CEO role", schema_corp_target_player, 0, false, NULL},
  {"corp.member.transfer", cmd_corp_member_transfer,
   "Transfer an asset to a corporation member", schema_corp_member_transfer,
   0, false, NULL},
  {"corp.withdraw", cmd_corp_withdraw, "Withdraw from corporation",
   schema_corp_amount, 0, false, NULL},
  {"dock.status", cmd_dock_status,
   "Check/set player docked status at current port", schema_dock_status, 0, false, NULL},
  {"hardware.buy", cmd_hardware_buy, "Buy ship hardware", schema_hardware_buy,
   0, false, NULL},
  {"hardware.list", cmd_hardware_list, "List available ship hardware",
   schema_hardware_list, 0, false, NULL},
  {"mail.delete", cmd_mail_delete, "Delete mail", schema_mail_delete, 0, false, NULL},
  {"mail.inbox", cmd_mail_inbox, "Mail inbox", schema_mail_inbox, 0, false, NULL},
  {"mail.read", cmd_mail_read, "Read mail", schema_mail_read, 0, false, NULL},
  {"mail.send", cmd_mail_send, "Send mail", schema_mail_send, 0, false, NULL},
  {"move.autopilot.start", w_move_autopilot_start, "Start autopilot",
   schema_move_autopilot_start, 0, false, NULL},
  {"move.autopilot.status", cmd_move_autopilot_status, "Autopilot status",
   schema_move_autopilot_status, 0, false, NULL},
  {"move.autopilot.control", cmd_move_autopilot_control,
   "Control a persisted autopilot route", schema_move_autopilot_control,
   0, false, NULL},
  {"move.autopilot.stop", cmd_move_autopilot_stop, "Stop autopilot",
   schema_move_autopilot_stop, 0, false, NULL},
  {"move.describe_sector", cmd_move_describe_sector, "Describe a sector",
   schema_move_describe_sector, 0, false, NULL},
  {"move.pathfind", cmd_move_pathfind, "Find path between sectors",
   schema_move_pathfind, 0, false, NULL},
  {"move.scan", cmd_move_scan, "Scan adjacent sectors", schema_move_scan, 0, false, NULL},
  {"move.transwarp", cmd_move_transwarp, "Transwarp to a sector",
   schema_move_transwarp, 0, false, NULL},
  {"move.warp", cmd_move_warp, "Warp to sector", schema_move_warp, 0, false, NULL},
  {"nav.avoid.add", w_nav_avoid_add, "Add a sector to the avoid list",
   schema_nav_avoid_sector, 0, false, NULL},
  {"nav.avoid.list", w_nav_avoid_list, "List avoided sectors",
   schema_nav_no_args, 0, false, NULL},
  {"nav.avoid.remove", w_nav_avoid_remove, "Remove sector from avoid list",
   schema_nav_avoid_sector, 0, false, NULL},
  {"nav.avoid.set", cmd_player_set_avoids, "Set a sector to the avoid list",
   schema_nav_avoid_set, 0, false, NULL},
  {"nav.bookmark.add", w_nav_bookmark_add, "Add a bookmark",
   schema_nav_bookmark_add, 0, false, NULL},
  {"nav.bookmark.list", w_nav_bookmark_list, "List bookmarks",
   schema_nav_no_args, 0, false, NULL},
  {"nav.bookmark.remove", w_nav_bookmark_remove, "Remove a bookmark",
   schema_nav_bookmark_remove, 0, false, NULL},
  {"nav.bookmark.set", cmd_player_set_bookmarks, "Set a bookmark",
   schema_nav_bookmark_set, 0, false, NULL},
  {"news.get_feed", cmd_news_get_feed, "Get the daily news feed",
   schema_news_no_args, 0, false, NULL},
  {"news.mark_feed_read", cmd_news_mark_feed_read, "Mark news feed as read",
   schema_news_no_args, 0, false, NULL},
  {"news.read", cmd_news_read, "Read news feed", schema_news_read, 0, false, NULL},
  {"notes.delete", cmd_player_delete_note, "Delete a note", schema_note_delete,
   0, false, NULL},
  {"notes.list", cmd_player_get_notes, "List notes", schema_note_list, 0, false, NULL},
  {"notes.set", cmd_player_set_note, "Set a note", schema_note_set, 0, false, NULL},
  {"notice.ack", cmd_notice_ack, "Acknowledge a notice", schema_notice_ack,
   0, false, NULL},
  {"notice.list", cmd_notice_list, "List notices", schema_notice_list, 0, false, NULL},
  {"planet.create", cmd_planet_genesis_create, "Create a planet",
   schema_planet_genesis_create, 0, false, NULL},
  {"planet.deposit", cmd_planet_deposit, "Deposit to a planet",
   schema_planet_deposit, 0, false, NULL},
  {"planet.emancipate", cmd_planet_emancipate,
   "Emancipate all slaves on a planet", schema_planet_emancipate, 0, false, NULL},
  {"planet.genesis", cmd_planet_genesis_create, "Create a planet",
   schema_planet_genesis, 0, false, NULL},
  {"planet.genesis_create", cmd_planet_genesis_create,
   "Create a genesis planet", schema_planet_genesis_create, 0, false, NULL},
  {"planet.harvest", cmd_planet_harvest, "Harvest from a planet",
   schema_planet_harvest, 0, false, NULL},
  {"planet.trade.port", cmd_planet_trade_port,
   "Bulk-sell planetary commodities to a local port",
   schema_planet_trade_port, 0, false, NULL},
  {"planet.view", cmd_planet_view, "Read-only planetary overview",
   schema_planet_info, 0, false, NULL},
  {"planet.info", cmd_planet_info, "Planet information", schema_planet_info,
   0, false, NULL},
  {"planet.fighters.transfer", cmd_planet_fighters_transfer,
   "Transfer fighters between a landed ship and its planet",
   schema_planet_fighters_transfer, 0, false, NULL},
  {"planet.land", cmd_planet_land, "Land on a planet", schema_planet_land, 0, false, NULL},
  {"planet.colonists.get", cmd_planet_colonists_get, "Get colonist info for planet",
   schema_placeholder, 0, false, NULL},
  {"planet.colonists.set", cmd_planet_colonists_set, "Transfer colonists to/from ship",
   schema_placeholder, 0, false, NULL},
  {"planet.colonists.allocate", cmd_planet_colonists_allocate,
   "Assign planet colonists to resource production", schema_planet_colonists_allocate,
   0, false, NULL},
  {"planet.launch", cmd_planet_launch, "Launch from a planet",
   schema_planet_launch, 0, false, NULL},
  {"planet.market.buy_order", cmd_planet_market_buy_order,
   "Create planet market buy order", schema_placeholder, 0, false, NULL},
  {"planet.market.buy_order.cancel", cmd_planet_market_buy_order_cancel,
   "Cancel planet market buy order", schema_placeholder, 0, false, NULL},
  {"planet.market.sell", cmd_planet_market_sell, "Sell to planet market",
   schema_placeholder, 0, false, NULL},
  {"planet.rename", cmd_planet_rename, "Rename a planet",
   schema_planet_rename, 0, false, NULL},
  {"planet.transfer_ownership", cmd_planet_transfer_ownership,
   "Transfer planet ownership", schema_planet_transfer_ownership, 0, false, NULL},
  {"planet.withdraw", cmd_planet_withdraw, "Withdraw from a planet",
   schema_planet_withdraw, 0, false, NULL},

  /* --- SysOp Config v1 --- */
  {"sysop.config.list", cmd_sysop_config_list, "List system configuration", schema_placeholder, 0, false, NULL},
  {"sysop.config.get", cmd_sysop_config_get, "Get system configuration value", schema_placeholder, 0, false, NULL},
  {"sysop.config.set", cmd_sysop_config_set, "Set system configuration value", schema_placeholder, 0, false, NULL},
  {"sysop.config.history", cmd_sysop_config_history, "Get configuration history", schema_placeholder, 0, false, NULL},

  {"sysop.players.search", cmd_sysop_players_search, "Search players", schema_placeholder, 0, false, NULL},
  {"sysop.player.get", cmd_sysop_player_get, "Get player details", schema_placeholder, 0, false, NULL},
  {"sysop.player.kick", cmd_sysop_player_kick, "Kick a player", schema_placeholder, 0, false, NULL},
  {"sysop.player.sessions.get", cmd_sysop_player_sessions_get, "Get player sessions", schema_placeholder, 0, false, NULL},
  {"sysop.universe.summary", cmd_sysop_universe_summary, "Universe summary", schema_placeholder, 0, false, NULL},

  {"sysop.engine_status.get", cmd_sysop_engine_status_get, "Get engine status", schema_placeholder, 0, false, NULL},
  {"sysop.jobs.list", cmd_sysop_jobs_list, "List engine jobs", schema_placeholder, 0, false, NULL},
  {"sysop.jobs.get", cmd_sysop_jobs_get, "Get job details", schema_placeholder, 0, false, NULL},
  {"sysop.jobs.retry", cmd_sysop_jobs_retry, "Retry a job", schema_placeholder, 0, false, NULL},
  {"sysop.jobs.cancel", cmd_sysop_jobs_cancel, "Cancel a job", schema_placeholder, 0, false, NULL},

  {"sysop.notice.create", cmd_sysop_notice_create, "Create a system notice", schema_placeholder, 0, false, NULL},
  {"sysop.notice.delete", cmd_sysop_notice_delete, "Delete a system notice", schema_placeholder, 0, false, NULL},
  {"sysop.broadcast.send", cmd_sysop_broadcast_send, "Send a global broadcast", schema_placeholder, 0, false, NULL},
  {"sysop.logs.tail", cmd_sysop_logs_tail, "Tail system logs", schema_placeholder, 0, false, NULL},
  {"sysop.logs.clear", cmd_sysop_logs_clear, "Clear system logs", schema_placeholder, 0, false, NULL},

  {"player.get_avoids", cmd_player_get_avoids, "Get player avoids",
   schema_nav_no_args, 0, false, NULL},
  {"player.get_bookmarks", cmd_player_get_bookmarks, "Get player bookmarks",
   schema_nav_no_args, 0, false, NULL},
  {"player.get_notes", cmd_player_get_notes, "Get player notes",
   schema_note_list, 0, false, NULL},
  {"player.get_prefs", cmd_player_get_prefs, "Get player preferences",
   schema_player_get_prefs, 0, false, NULL},
  {"player.get_settings", cmd_player_get_settings, "Get player settings",
   schema_player_get_settings, 0, false, NULL},
  {"player.get_subscriptions", cmd_player_get_topics,
   "Get player subscriptions", schema_player_get_topics, 0, false, NULL},
  {"player.get_topics", cmd_player_get_topics, "Get player topics",
   schema_player_get_topics, 0, false, NULL},
  {"player.list_online", cmd_player_list_online, "List online players",
   schema_player_list_online_request, 0, false, NULL},
  {"player.my_info", cmd_player_my_info, "Current player info",
   schema_player_my_info, 0, false, NULL},
  {"player.rankings", cmd_player_rankings, "Player rankings",
   schema_placeholder, 0, false, NULL},
  {"player.computer.recommend_routes", cmd_player_computer_recommend_routes,
   "Recommend trade loops based on personal knowledge",
   schema_player_computer_recommend_routes, 0, false, NULL},
  {"player.set_avoids", cmd_player_set_avoids, "Set player avoids",
   schema_nav_avoid_set, 0, false, NULL},
  {"player.set_bookmarks", cmd_player_set_bookmarks, "Set player bookmarks",
   schema_nav_bookmark_set, 0, false, NULL},
  {"player.set_prefs", cmd_player_set_prefs, "Set player preferences",
   schema_player_set_prefs, 0, false, NULL},
  {"player.set_settings", cmd_player_set_settings, "Set player settings",
   schema_player_set_settings, 0, false, NULL},
  {"player.set_subscriptions", cmd_player_set_topics,
   "Set player subscriptions", schema_player_set_topics, 0, false, NULL},
  {"player.set_topics", cmd_player_set_topics, "Set player topics",
   schema_player_set_topics, 0, false, NULL},
  {"player.set_trade_account_preference",
   cmd_player_set_trade_account_preference, "Set trade account preference",
   schema_player_set_trade_account_preference, 0, false, NULL},
  {"port.describe", cmd_trade_port_info, "Describe a port",
   schema_port_describe, 0, false, NULL},
  {"port.info", cmd_trade_port_info, "Port prices/stock in sector",
   schema_port_info, 0, false, NULL},
  {"port.rob", cmd_port_rob, "Attempt to rob a port", schema_port_rob, 0, false, NULL},
  {"port.status", cmd_trade_port_info, "Port status", schema_port_status, 0, false, NULL},
  {"s2s.event.relay", cmd_s2s_event_relay, "S2S event relay",
   schema_placeholder, CMD_FLAG_HIDDEN, false, NULL},
  {"s2s.event_relay", cmd_s2s_event_relay, "S2S event relay",
   schema_placeholder, CMD_FLAG_HIDDEN, false, NULL},
  {"s2s.planet.genesis", cmd_s2s_planet_genesis, "S2S planet genesis",
   schema_placeholder, 0, false, NULL},
  {"s2s.planet_genesis", cmd_s2s_planet_genesis, "S2S planet genesis",
   schema_placeholder, 0, false, NULL},
  {"s2s.planet.transfer", cmd_s2s_planet_transfer, "S2s planet transfer",
   schema_placeholder, CMD_FLAG_HIDDEN, false, NULL},
  {"s2s.player.migrate", cmd_s2s_player_migrate, "S2S player migrate",
   schema_placeholder, CMD_FLAG_HIDDEN, false, NULL},
  {"s2s.port.restock", cmd_s2s_port_restock, "S2S port restock",
   schema_placeholder, CMD_FLAG_HIDDEN, false, NULL},
  {"s2s.replication.heartbeat", cmd_s2s_replication_heartbeat,
   "S2S replication heartbeat", schema_placeholder, CMD_FLAG_HIDDEN, false, NULL},
  {"sector.info", cmd_move_describe_sector, "Describe current sector",
   schema_sector_info, 0, false, NULL},
  {"sector.mine_disrupt", cmd_sector_mine_disrupt, "Disrupt (remove) owned mines",
   schema_placeholder, 0, false, NULL},
  {"sector.scan", w_sector_scan, "Scan a sector", schema_sector_scan, 0, false, NULL},
  {"sector.scan.density", w_sector_scan_density, "Scan sector density",
   schema_sector_scan_density, 0, false, NULL},
  {"sector.search", cmd_sector_search, "Search a sector",
   schema_sector_search, 0, false, NULL},
  {"sector.set_beacon", cmd_sector_set_beacon, "Set or clear sector beacon",
   schema_sector_set_beacon, 0, false, NULL},
  {"session.disconnect", cmd_session_disconnect, "Disconnect",
   schema_session_disconnect, 0, false, NULL},
  {"session.ping", cmd_session_ping, "Ping", schema_session_ping, 0, false, NULL},
  {"player.ping", cmd_session_ping, "Ping (alias for session.ping)", schema_session_ping, 0, false, NULL},
  {"sys.cluster.init", cmd_sys_cluster_init, "Cluster init",
   schema_placeholder, CMD_FLAG_DEBUG_ONLY | CMD_FLAG_HIDDEN, false, NULL},
  {"sys.cluster.seed_illegal_goods", cmd_sys_cluster_seed_illegal_goods,
   "Seed illegal goods", schema_placeholder,
   CMD_FLAG_DEBUG_ONLY | CMD_FLAG_HIDDEN, false, NULL},
  {"sys.econ.orders_summary", cmd_sys_econ_orders_summary,
   "Economy orders summary", schema_placeholder, CMD_FLAG_HIDDEN, false, NULL},
  {"sys.econ.planet_status", cmd_sys_econ_planet_status,
   "Economy planet status", schema_placeholder, CMD_FLAG_HIDDEN, false, NULL},
  {"sys.econ.port_status", cmd_sys_econ_port_status, "Economy port status",
   schema_placeholder, CMD_FLAG_HIDDEN, false, NULL},
  {"sys.notice.create", cmd_sys_notice_create,
   "Sysop command to create a notice", schema_sys_notice_create, 0, false, NULL},
  {"sys.npc.ferengi_tick_once", cmd_sys_npc_ferengi_tick_once, "Tick Ferengi",
   schema_placeholder, CMD_FLAG_HIDDEN, false, NULL},
  {"sys.cron.planet_tick_once", cmd_sys_cron_planet_tick_once,
   "Trigger planet cron tasks", NULL, 0, false, NULL},
  {"sys.cron.citadel_reap_once", cmd_sys_cron_citadel_reap_once,
   "Trigger citadel construction reaper", NULL, 0, false, NULL},
  {"sys.raw_sql_exec", cmd_sys_raw_sql_exec,
   "Sysop command to execute raw SQL", schema_placeholder,
   CMD_FLAG_DEBUG_ONLY | CMD_FLAG_HIDDEN, false, NULL},
  {"sys.test_news_cron", cmd_sys_test_news_cron,
   "Sysop command to test news cron", schema_placeholder, CMD_FLAG_HIDDEN, false, NULL},
  {"system.capabilities", cmd_system_capabilities,
   "Feature flags, schemas, counts", schema_system_capabilities, 0, false, NULL},
  {"system.cmd_list", cmd_system_cmd_list, "Flat list of all commands",
   schema_placeholder, 0, false, NULL},
  {"system.describe_schema", cmd_system_describe_schema,
   "Describe commands in a schema", schema_system_describe_schema, 0, false, NULL},
  {"system.disconnect", cmd_session_disconnect, "Disconnect",
   schema_system_disconnect, 0, false, NULL},
  {"system.hello", cmd_system_hello, "Handshake / hello", schema_system_hello,
   CMD_FLAG_AUTH_FREE, false, NULL},
  {"system.schema_list", cmd_system_schema_list, "List all schema namespaces",
   schema_placeholder, 0, false, NULL},
  {"ship.claim", cmd_ship_claim, "Claim a ship", schema_ship_claim, 0, false, NULL},
  {"ship.info", cmd_ship_info_compat, "Ship information", schema_ship_info,
   0, false, NULL},
  {"ship.inspect", cmd_ship_inspect, "Inspect a ship", schema_ship_inspect,
   0, false, NULL},
  {"ship.jettison", cmd_trade_jettison, "Jettison cargo",
   schema_ship_jettison, 0, false, NULL},
  {"ship.list", cmd_ship_list, "List ships", schema_placeholder, 0, false, NULL},
  {"ship.rename", cmd_ship_rename, "Rename a ship", schema_ship_rename, 0, false, NULL},
  {"ship.repair", cmd_ship_repair, "Repair a ship", schema_ship_repair, 0, false, NULL},
  {"ship.reregister", cmd_ship_rename, "Re-register a ship",
   schema_ship_reregister, 0, false, NULL},
  {"ship.self_destruct", cmd_ship_self_destruct, "Self-destruct a ship",
   schema_ship_self_destruct, 0, false, NULL},
  {"ship.sell", cmd_ship_sell, "Sell a ship", schema_ship_sell, 0, false, NULL},
  {"ship.status", cmd_ship_status, "Ship status", schema_ship_status, 0, false, NULL},
  {"ship.tow", cmd_ship_tow, "Engage or disengage towing",
   schema_ship_tow, 0, false, NULL},
  {"ship.tow.status", cmd_ship_tow_status, "Ship towing status",
   schema_ship_tow_status, 0, false, NULL},
  {"ship.transfer", cmd_ship_transfer, "Transfer a ship", schema_ship_transfer,
   0, false, NULL},
  {"ship.transfer_cargo", cmd_ship_transfer_cargo, "Transfer cargo",
   schema_ship_transfer_cargo, 0, false, NULL},
  {"ship.upgrade", cmd_ship_upgrade, "Upgrade a ship", schema_ship_upgrade,
   0, false, NULL},
  {"shipyard.list", cmd_shipyard_list, "List available ship hulls",
   schema_nav_no_args, 0, false, NULL},
  {"shipyard.sell", cmd_shipyard_sell, "Sell a ship hull", schema_shipyard_sell,
   0, false, NULL},
    {"shipyard.upgrade", cmd_shipyard_upgrade, "Upgrade to a new ship hull",
     schema_shipyard_upgrade, 0, false, NULL},
    {"equity.buy", cmd_equity_buy, "Purchase corporate shares", schema_placeholder, 0, false, NULL},
    {"equity.sell", cmd_equity_sell, "Sell corporate shares", schema_placeholder, 0, false, NULL},
    {"equity.ipo_register", cmd_equity_ipo_register, "Register corp equity IPO", schema_placeholder, 0, false, NULL},
    {"equity.dividend_set", cmd_equity_dividend_set, "Set dividend payout", schema_placeholder, 0, false, NULL},
    {"equity.exchange.list", cmd_equity, "List exchange stocks", schema_placeholder, 0, false, NULL},
    {"equity.exchange.order.create", cmd_equity, "Create order", schema_placeholder, 0, false, NULL},
    {"equity.exchange.order.cancel", cmd_equity, "Cancel order", schema_placeholder, 0, false, NULL},
  {"equity.portfolio.list", cmd_equity, "List holdings", schema_placeholder, 0, false, NULL},
    {"stock.buy", cmd_equity_buy, "Buy shares in a corporation (DEPRECATED: use equity.buy)", schema_placeholder, 0, true, "equity.buy"},
    {"stock.sell", cmd_equity_sell, "Sell shares in a corporation (DEPRECATED: use equity.sell)", schema_placeholder, 0, true, "equity.sell"},
    {"stock.dividend.set", cmd_equity_dividend_set, "Declare a dividend (DEPRECATED: use equity.dividend_set)", schema_placeholder, 0, true, "equity.dividend_set"},
    {"stock.exchange.list_stocks", cmd_equity, "List stocks on the exchange (DEPRECATED: use equity.exchange.list)", schema_placeholder, 0, true, "equity.exchange.list"},
    {"stock.exchange.orders.cancel", cmd_equity, "Cancel a stock order (DEPRECATED: use equity.exchange.order.cancel)", schema_placeholder, 0, true, "equity.exchange.order.cancel"},
    {"stock.exchange.orders.create", cmd_equity, "Create a stock order (DEPRECATED: use equity.exchange.order.create)", schema_placeholder, 0, true, "equity.exchange.order.create"},
    {"stock.ipo.register", cmd_equity_ipo_register, "Register corporation for IPO (DEPRECATED: use equity.ipo_register)", schema_placeholder, 0, true, "equity.ipo_register"},
    {"stock.portfolio.list", cmd_equity, "List stock portfolio (DEPRECATED: use equity.portfolio.list)", schema_placeholder, 0, true, "equity.portfolio.list"},
    {"events.resume", cmd_events_resume, "Resume the player's event stream",
     schema_events_resume, 0, false, NULL},
    {"subscribe.add", cmd_subscribe_add, "Add a subscription",
   schema_subscribe_add, 0, false, NULL},
  {"subscribe.catalog", cmd_subscribe_catalog, "Subscription catalog",
   schema_subscribe_catalog, 0, false, NULL},
  {"subscribe.list", cmd_subscribe_list, "List subscriptions",
   schema_subscribe_list, 0, false, NULL},
  {"subscribe.remove", cmd_subscribe_remove, "Remove a subscription",
   schema_subscribe_remove, 0, false, NULL},
  {"tavern.barcharts.get_prices_summary",
   cmd_tavern_barcharts_get_prices_summary,
   "Get a summary of commodity prices", schema_placeholder, 0, false, NULL},
  {"tavern.deadpool.place_bet", cmd_tavern_deadpool_place_bet,
   "Place a bet on a player's destruction", schema_placeholder, 0, false, NULL},
  {"tavern.dice.play", cmd_tavern_dice_play, "Play bar dice",
   schema_placeholder, 0, false, NULL},
  {"tavern.graffiti.post", cmd_tavern_graffiti_post,
   "Post a message on the graffiti wall", schema_placeholder, 0, false, NULL},
  {"tavern.highstakes.play", cmd_tavern_highstakes_play,
   "Play at the high-stakes table", schema_placeholder, 0, false, NULL},
  {"tavern.loan.pay", cmd_tavern_loan_pay, "Repay a loan from the loan shark",
   schema_placeholder, 0, false, NULL},
  {"tavern.loan.take", cmd_tavern_loan_take,
   "Take a loan from the loan shark", schema_placeholder, 0, false, NULL},
  {"tavern.lottery.buy_ticket", cmd_tavern_lottery_buy_ticket,
   "Buy a lottery ticket", schema_placeholder, 0, false, NULL},
  {"tavern.lottery.status", cmd_tavern_lottery_status, "Check lottery status",
   schema_placeholder, 0, false, NULL},
  {"tavern.raffle.buy_ticket", cmd_tavern_raffle_buy_ticket,
   "Buy a raffle ticket", schema_placeholder, 0, false, NULL},
  {"tavern.round.buy", cmd_tavern_round_buy, "Buy a round for the tavern",
   schema_placeholder, 0, false, NULL},
  {"tavern.rumour.get_hint", cmd_tavern_rumour_get_hint,
   "Get a hint from the rumour mill", schema_placeholder, 0, false, NULL},
  {"tavern.trader.buy_password", cmd_tavern_trader_buy_password,
   "Buy an underground password from the grimy trader", schema_placeholder,
   0, false, NULL},
  {"trade.accept", cmd_trade_accept, "Accept a trade offer",
   schema_trade_accept, 0, false, NULL},
  {"trade.buy", cmd_trade_buy, "Buy commodity from port", schema_trade_buy,
   0, false, NULL},
  {"trade.cancel", cmd_trade_cancel, "Cancel a trade offer",
   schema_trade_cancel, 0, false, NULL},
  {"trade.history", cmd_trade_history, "View recent trade transactions",
   schema_trade_history, 0, false, NULL},
  {"trade.jettison", cmd_trade_jettison, "Dump cargo into space",
   schema_trade_jettison, 0, false, NULL},
  {"trade.offer", cmd_trade_offer, "Create a trade offer to another player",
   schema_trade_offer, 0, false, NULL},
  {"trade.offers", cmd_trade_offers, "List the authenticated player's pending trade offers",
   schema_trade_offers, 0, false, NULL},
  {"ferengi.traders", cmd_ferengi_traders,
   "View named Ferengi traders and their offers", schema_ferengi_traders,
   0, false, NULL},
  {"ferengi.deal.accept", cmd_ferengi_deal_accept,
   "Accept a Ferengi trader offer", schema_ferengi_deal_action,
   0, false, NULL},
  {"ferengi.deal.reject", cmd_ferengi_deal_reject,
   "Reject a Ferengi trader offer", schema_ferengi_deal_action,
   0, false, NULL},
  {"trade.port_info", cmd_trade_port_info, "Port prices/stock in sector",
   schema_trade_port_info, 0, false, NULL},
  {"trade.quote", cmd_trade_quote, "Get a price quote from a port",
   schema_trade_quote, 0, false, NULL},
  {"trade.sell", cmd_trade_sell, "Sell commodity to port", schema_trade_sell,
   0, false, NULL},
  {"police.bribe", cmd_police_bribe, "Attempt to bribe police",
   schema_placeholder, 0, false, NULL},
  {"police.surrender", cmd_police_surrender, "Surrender to authorities",
   schema_placeholder, 0, false, NULL},
  {"police.status", handle_police_status, "Query incident state",
   schema_placeholder, 0, false, NULL},
  {NULL, NULL, NULL, NULL, 0, false, NULL}	/* Sentinel */
};


/* Provide authoritative list to others */
void
loop_get_supported_commands (const cmd_desc_t **out_tbl, size_t *out_n)
{
  static cmd_desc_t *s_descs = NULL;
  static size_t s_count = 0;

  if (!s_descs)
    {
      size_t n = 0;
      size_t count = 0;

      while (k_command_registry[n].name)
	{
#ifdef BUILD_PRODUCTION
	  if (k_command_registry[n].flags & CMD_FLAG_DEBUG_ONLY)
	    {
	      n++;
	      continue;
	    }
#endif
	  if (k_command_registry[n].flags & CMD_FLAG_HIDDEN)
	    {
	      n++;
	      continue;
	    }
	  n++;
	  count++;
	}
      s_descs = calloc (count, sizeof (cmd_desc_t));
      if (s_descs)
	{
	  size_t j = 0;
	  size_t i = 0;

	  while (k_command_registry[i].name)
	    {
#ifdef BUILD_PRODUCTION
	      if (k_command_registry[i].flags & CMD_FLAG_DEBUG_ONLY)
		{
		  i++;
		  continue;
		}
#endif
	      if (k_command_registry[i].flags & CMD_FLAG_HIDDEN)
		{
		  i++;
		  continue;
		}
	      s_descs[j].name = k_command_registry[i].name;
	      s_descs[j].summary = k_command_registry[i].summary;
	      s_descs[j].is_deprecated = k_command_registry[i].is_deprecated;
	      s_descs[j].replacement = k_command_registry[i].replacement;
	      j++;
	      i++;
	    }
	  s_count = count;
	}
    }
  if (out_tbl)
    {
      *out_tbl = (const cmd_desc_t *) s_descs;
    }
  if (out_n)
    {
      *out_n = s_count;
    }
}

json_t *
loop_get_schema_for_command (const char *name)
{
  if (!name)
    {
      return NULL;
    }
  for (int i = 0; k_command_registry[i].name != NULL; i++)
    {
      if (strcasecmp (name, k_command_registry[i].name) == 0)
	{
	  if (k_command_registry[i].schema)
	    {
	      return k_command_registry[i].schema ();
	    }
	  else
	    {
	      return NULL;
	    }
	}
    }
  return NULL;
}

json_t *
loop_get_all_schema_keys (void)
{
  json_t *keys = json_array ();
  json_array_append_new (keys, json_string ("envelope"));
  for (int i = 0; k_command_registry[i].name != NULL; i++)
    {
      if (k_command_registry[i].flags & CMD_FLAG_HIDDEN)
	{
	  continue;
	}
#ifdef BUILD_PRODUCTION
      if (k_command_registry[i].flags & CMD_FLAG_DEBUG_ONLY)
	{
	  continue;
	}
#endif
      json_array_append_new (keys, json_string (k_command_registry[i].name));
    }
  return keys;
}

/* --------------------------------------------------------------------------
   Broadcast / Registry
   -------------------------------------------------------------------------- */

static void
server_broadcast_to_all_online (json_t *data)
{
  server_broadcast_event ("system.notice", data);
  json_decref (data);
}

static int
broadcast_sweep_once (db_t *db, int max_rows)
{
  if (!db)
    {
      return -1;
    }
  db_error_t err;
  db_res_t *res = NULL;
  int rows = 0;

  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      if (err.code == ERR_DB_BUSY || err.code == ERR_DB_LOCKED)
	{
	  return 0;
	}
      return -1;
    }

  if (repo_session_get_unseen_notices (db, max_rows, &res) != 0)
    {
      db_tx_rollback (db, NULL);
      return -1;
    }

  while (db_res_step (res, &err))
    {
      int notice_id = (int) db_res_col_i64 (res, 0, &err);
      int64_t created_at = db_res_col_i64 (res, 1, &err);
      const char *title = db_res_col_text (res, 2, &err);
      const char *body = db_res_col_text (res, 3, &err);
      const char *severity = db_res_col_text (res, 4, &err);
      int64_t expires_at =
	db_res_col_is_null (res, 5) ? 0 : db_res_col_i64 (res, 5, &err);

      /* Broadcast notice to clients */
      json_t *data = json_object ();

      json_object_set_new (data, "id", json_integer (notice_id));
      json_object_set_new (data, "created_at", json_integer (created_at));
      json_object_set_new (data, "title", json_string (title ? title : ""));
      json_object_set_new (data, "body", json_string (body ? body : ""));
      json_object_set_new (data, "severity",
			   json_string (severity ? severity : "info"));
      if (expires_at > 0)
	{
	  json_object_set_new (data, "expires_at", json_integer (expires_at));
	}

      server_broadcast_to_all_online (data);

      /* Mark as seen for system player (player_id = 0) */
      if (db_notice_mark_seen (db, notice_id, 0) != 0)
	{
	  db_res_finalize (res);
	  db_tx_rollback (db, NULL);
	  return -1;
	}
      rows++;
    }

  db_res_finalize (res);

  if (!db_tx_commit (db, &err))
    {
      db_tx_rollback (db, NULL);
      return -1;
    }

  return rows;
}

client_node_t *g_clients = NULL;
pthread_mutex_t g_clients_mu = PTHREAD_MUTEX_INITIALIZER;

void
server_register_client (client_ctx_t *ctx)
{
  pthread_mutex_lock (&g_clients_mu);
  client_node_t *n = (client_node_t *) calloc (1, sizeof (*n));

  if (n)
    {
      n->ctx = ctx;
      n->next = g_clients;
      g_clients = n;
    }
  pthread_mutex_unlock (&g_clients_mu);
}

void
server_unregister_client (client_ctx_t *ctx)
{
  pthread_mutex_lock (&g_clients_mu);
  client_node_t **pp = &g_clients;

  while (*pp)
    {
      if ((*pp)->ctx == ctx)
	{
	  client_node_t *dead = *pp;
	  *pp = (*pp)->next;
	  free (dead);
	  break;
	}
      pp = &((*pp)->next);
    }
  pthread_mutex_unlock (&g_clients_mu);
}

int
server_deliver_to_player_topic (int player_id, const char *topic,
                                const char *event_type, json_t *data)
{
  if (player_id <= 0 || !event_type || !data)
    return -1;
  char *event_id = gen_uuid_v4 ();
  if (!event_id)
    return -1;

  int replayable = 0;
  char timestamp[32] = {0};
  char stream_id[37] = {0};
  int64_t sequence = 0;
  db_t *db = game_db_get_handle ();
  if (db)
    {
      char *payload = json_dumps (data, JSON_COMPACT | JSON_SORT_KEYS);
      if (payload && repo_comm_event_append (db, player_id, event_id,
                                              event_type, topic, payload,
                                              stream_id, &sequence,
                                              timestamp) == 0)
        replayable = 1;
      else
        LOGE ("event journal append failed for player %d event %s; delivering live-only",
              player_id, event_type);
      free (payload);
    }

  char cursor[96];
  if (replayable)
    snprintf (cursor, sizeof cursor, "v1.%s.%" PRId64, stream_id, sequence);
  else
    {
      free (event_id);
      event_id = gen_uuid_v4 ();
      timestamp[0] = '\0';
      cursor[0] = '\0';
      if (!event_id)
        return -1;
    }

  int delivered = 0;
  pthread_mutex_lock (&g_clients_mu);
  for (client_node_t * n = g_clients; n; n = n->next)
    {
      client_ctx_t *c = n->ctx;

      if (!c)
	{
	  continue;
	}
      if (c->player_id == player_id && c->fd >= 0)
	{
	  send_player_event (c, event_id, event_type, data,
                             replayable ? timestamp : NULL, topic,
                             replayable ? cursor : NULL, replayable);
	  delivered++;
	}
    }
  pthread_mutex_unlock (&g_clients_mu);
  free (event_id);
  return (delivered > 0) ? 0 : -1;
}

int
server_deliver_to_player (int player_id, const char *event_type, json_t *data)
{
  return server_deliver_to_player_topic (player_id, event_type, event_type,
                                         data);
}

/* ------------------------ idempotency helpers  ------------------------ */
static uint64_t
fnv1a64 (const unsigned char *s, size_t n)
{
  uint64_t h = 1469598103934665603ULL;
  for (size_t i = 0; i < n; ++i)
    {
      h ^= (uint64_t) s[i];
      h *= 1099511628211ULL;
    }
  return h;
}

static void
hex64 (uint64_t v, char out[17])
{
  static const char hexd[] = "0123456789abcdef";
  for (int i = 15; i >= 0; --i)
    {
      out[i] = hexd[v & 0xF];
      v >>= 4;
    }
  out[16] = '\0';
}

void
idemp_fingerprint_json (json_t *obj, char out[17])
{
  char *s = json_dumps (obj, JSON_COMPACT | JSON_SORT_KEYS);
  if (!s)
    {
      strcpy (out, "0");
      return;
    }
  uint64_t h = fnv1a64 ((const unsigned char *) s, strlen (s));

  free (s);
  hex64 (h, out);
}

/* ------------------------ socket helpers ------------------------ */
static int
set_reuseaddr (int fd)
{
  int yes = 1;
  return setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
}

static int
make_listen_socket (uint16_t port, const char *bind_address)
{
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    {
      perror ("socket");
      return -1;
    }
  if (set_reuseaddr (fd) < 0)
    {
      perror ("setsockopt(SO_REUSEADDR)");
      close (fd);
      return -1;
    }
  struct sockaddr_in sa;

  memset (&sa, 0, sizeof (sa));
  sa.sin_family = AF_INET;
  if (!bind_address || inet_pton (AF_INET, bind_address, &sa.sin_addr) != 1)
    {
      close (fd);
      errno = EINVAL;
      return -1;
    }
  sa.sin_port = htons (port);
  if (bind (fd, (struct sockaddr *) &sa, sizeof (sa)) < 0)
    {
      perror ("bind");
      close (fd);
      return -1;
    }
  if (listen (fd, 128) < 0)
    {
      perror ("listen");
      close (fd);
      return -1;
    }
  return fd;
}

void
rl_tick (client_ctx_t *ctx)
{
  if (!ctx)
    {
      return;
    }
  time_t now = time (NULL);

  if (ctx->rl_limit <= 0)
    {
      ctx->rl_limit = 60;
    }
  if (ctx->rl_window_sec <= 0)
    {
      ctx->rl_window_sec = 60;
    }
  if (now - ctx->rl_window_start >= ctx->rl_window_sec)
    {
      ctx->rl_window_start = now;
      ctx->rl_count = 0;
    }
  ctx->rl_count++;
}

static int
request_rate_allow (client_ctx_t *ctx)
{
  time_t now = time (NULL);
  if (ctx->request_window_start == 0)
    ctx->request_window_start = now;
  if (now - ctx->request_window_start >= 60)
    {
      ctx->request_window_start = now;
      ctx->request_count = 0;
    }
  if (ctx->request_count >= ctx->rl_limit)
    return 0;
  ctx->request_count++;
  return 1;
}

static json_t *
rl_build_meta (const client_ctx_t *ctx)
{
  if (!ctx)
    {
      return json_null ();
    }
  time_t now = time (NULL);
  int reset = (int) (ctx->rl_window_start + ctx->rl_window_sec - now);

  if (reset < 0)
    {
      reset = 0;
    }
  int remaining = ctx->rl_limit - ctx->rl_count;

  if (remaining < 0)
    {
      remaining = 0;
    }
  json_t *root = json_object ();

  json_object_set_new (root, "limit", json_integer (ctx->rl_limit));
  json_object_set_new (root, "remaining", json_integer (remaining));
  json_object_set_new (root, "reset", json_integer (reset));
  return root;
}

void
attach_rate_limit_meta (json_t *env, client_ctx_t *ctx)
{
  if (!env || !ctx)
    {
      return;
    }
  json_t *meta = json_object_get (env, "meta");

  if (!json_is_object (meta))
    {
      meta = json_object ();
      json_object_set_new (env, "meta", meta);
    }
  json_t *rl = rl_build_meta (ctx);

  json_object_set_new (meta, "rate_limit", rl);
}

int
server_dispatch_command (client_ctx_t *ctx, json_t *root)
{
  json_t *cmd = json_object_get (root, "command");
  if (!cmd || !json_is_string (cmd))
    {
      return -1;
    }

  const char *c = json_string_value (cmd);
  // LOGD ("Dispatching command: '%s'", c);

  for (int i = 0; k_command_registry[i].name != NULL; i++)
    {
      if (strcasecmp (c, k_command_registry[i].name) == 0)
	{
#ifdef BUILD_PRODUCTION
	  if (k_command_registry[i].flags & CMD_FLAG_DEBUG_ONLY)
	    {
	      return -1;
	    }
#endif

	  /* AUTH GATE — SINGLE SOURCE OF TRUTH */
	  if (!(k_command_registry[i].flags & CMD_FLAG_AUTH_FREE))
	    {
	      if (ctx->player_id <= 0)
		{
		  send_response_error (ctx,
				       root,
				       ERR_NOT_AUTHENTICATED,
				       "Not authenticated");
		return -1;
		}
	    }

	  /* SCHEMA VALIDATION */
	  json_t *data = json_object_get (root, "data");
	  char *why = NULL;
	  if (k_command_registry[i].schema != schema_placeholder &&
	      schema_validate_payload (k_command_registry[i].name, data, &why) !=
	      0)
	    {
	      int err_code = ERR_INVALID_SCHEMA;
	      if (why && strstr (why, "missing"))
		{
		  err_code = ERR_MISSING_FIELD;
		}
	      send_response_error (ctx, root, err_code, why ? why : "Invalid request schema");
	      free (why);
	      return 0;		/* Error handled */
	    }

	  return k_command_registry[i].handler (ctx, root);
	}
    }

  return -1;
}

static void
process_message (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "Database connection error");
      return;
    }
  g_ctx_for_send = ctx;
  ctx->responses_sent = 0;
  json_t *cmd = json_object_get (root, "command");
  json_t *evt = json_object_get (root, "event");
  json_t *jmeta = json_object_get (root, "meta");
  json_t *jauth = json_object_get (root, "auth");
  const char *session_token = NULL;

  if (json_is_object (jmeta))
    {
      json_t *jtok = json_object_get (jmeta, "session_token");

      if (json_is_string (jtok))
	{
	  session_token = json_string_value (jtok);
	}
    }
  if (session_token == NULL && json_is_object (jauth))
    {
      json_t *jtok = json_object_get (jauth, "session");

      if (json_is_string (jtok))
	{
	  session_token = json_string_value (jtok);
	}
    }

  /* Per-request auth: if no session token was provided, treat request as unauth.
     This prevents prior ctx->player_id from "leaking" into missing-auth tests. */
  if (session_token == NULL)
    {
      ctx->player_id = 0;
      ctx->corp_id = 0;
      ctx->ship_id = 0;
      /* sector_id: keep as-is if already set, but do not treat it as auth. */
    }

  if (session_token)
    {
      int32_t pid = 0;
      int64_t exp = 0;
      int rc = repo_session_lookup (db, session_token, &pid, &exp);
      if (rc == 0 && pid > 0)
	{
	  ctx->player_id = (int) pid;
	  ctx->corp_id = h_get_player_corp_id (db, (int) pid);
	  ctx->ship_id = h_get_active_ship_id (db, (int) pid);
	  int db_sector = h_get_player_sector (db, (int) pid);

	  if (db_sector > 0)
	    {
	      ctx->sector_id = db_sector;
	    }
	  if (ctx->sector_id <= 0)
	    {
	      ctx->sector_id = 1;
	    }
	}
      else
	{
	  /* Invalid/expired token: force unauth for this request */
	  ctx->player_id = 0;
	  ctx->corp_id = 0;
	  ctx->ship_id = 0;
	}
    }
  if (ctx->player_id == 0 && ctx->sector_id <= 0)
    {
      ctx->sector_id = 1;
    }
  if (!(cmd && json_is_string (cmd)) && !(evt && json_is_string (evt)))
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_SCHEMA, "Invalid request schema");
      return;
    }
  if (evt && json_is_string (evt))
    {
      send_response_error (ctx,
			   root,
			   ERR_INVALID_SCHEMA, "Invalid request schema");
      return;
    }
  if (server_dispatch_command (ctx, root) == -1)
    {
      if (ctx->responses_sent == 0)
	{
	  send_response_error (ctx, root, ERR_INVALID_SCHEMA,
			       "Unknown command");
	}
    }
}

static int
wait_for_socket (int fd, short events, uint64_t deadline_ms)
{
  for (;;)
    {
      uint64_t now = monotonic_millis ();
      if (now >= deadline_ms)
        return 0;
      int remain = (deadline_ms - now > INT_MAX)
        ? INT_MAX : (int) (deadline_ms - now);
      struct pollfd pfd = {.fd = fd, .events = events, .revents = 0};
      int rc = poll (&pfd, 1, remain);
      if (rc > 0)
        return (pfd.revents & (events | POLLERR | POLLHUP | POLLNVAL)) ? 1 : 0;
      if (rc == 0)
        return 0;
      if (errno != EINTR)
        return -1;
    }
}

static int
connection_tls_handshake (client_ctx_t *ctx)
{
  SSL *ssl = SSL_new (g_ssl_ctx);
  if (!ssl || SSL_set_fd (ssl, ctx->fd) <= 0)
    {
      if (ssl) SSL_free (ssl);
      return -1;
    }
  int old_flags = fcntl (ctx->fd, F_GETFL, 0);
  if (old_flags < 0 || fcntl (ctx->fd, F_SETFL, old_flags | O_NONBLOCK) < 0)
    {
      SSL_free (ssl);
      return -1;
    }

  uint64_t deadline = monotonic_millis () + (uint64_t) g_cfg.server_request_timeout_ms;
  int result = -1;
  for (;;)
    {
      int rc = SSL_accept (ssl);
      if (rc == 1)
        {
          result = 0;
          break;
        }
      int ssl_error = SSL_get_error (ssl, rc);
      short events = ssl_error == SSL_ERROR_WANT_WRITE ? POLLOUT : POLLIN;
      if (ssl_error != SSL_ERROR_WANT_READ && ssl_error != SSL_ERROR_WANT_WRITE)
        break;
      if (wait_for_socket (ctx->fd, events, deadline) <= 0)
        break;
    }
  if (fcntl (ctx->fd, F_SETFL, old_flags) < 0)
    result = -1;
  if (result != 0)
    {
      SSL_free (ssl);
      return -1;
    }
  struct timeval io_timeout = {
    .tv_sec = g_cfg.server_request_timeout_ms / 1000,
    .tv_usec = (g_cfg.server_request_timeout_ms % 1000) * 1000
  };
  (void) setsockopt (ctx->fd, SOL_SOCKET, SO_RCVTIMEO, &io_timeout,
                     sizeof io_timeout);
  (void) setsockopt (ctx->fd, SOL_SOCKET, SO_SNDTIMEO, &io_timeout,
                     sizeof io_timeout);
  ctx->ssl_conn = ssl;
  ctx->is_tls = 1;
  return 0;
}

/* Read one newline-framed request, bounded by both bytes and an absolute
   deadline. Returns -2 for an oversized frame and -3 for a deadline expiry. */
static ssize_t
client_readline (client_ctx_t *ctx, char *buf, size_t cap, int timeout_ms)
{
  size_t used = 0;
  uint64_t deadline = monotonic_millis () + (uint64_t) timeout_ms;
  while (used < cap - 1)
    {
      if (!(ctx->is_tls && SSL_pending ((SSL *) ctx->ssl_conn) > 0))
        {
          int ready = wait_for_socket (ctx->fd, POLLIN, deadline);
          /* A partial frame is not a completed request. Do not hand it to
             the parser and start a fresh deadline, or a slow client could
             keep a connection alive indefinitely by dripping bytes. */
          if (ready == 0) return -3;
          if (ready < 0) return -1;
        }
      char ch;
      ssize_t n;
      if (ctx->is_tls)
        {
          int rc = SSL_read ((SSL *) ctx->ssl_conn, &ch, 1);
          if (rc > 0) n = rc;
          else
            {
              int ssl_error = SSL_get_error ((SSL *) ctx->ssl_conn, rc);
              if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE)
                continue;
              if (ssl_error == SSL_ERROR_ZERO_RETURN)
                return used ? (ssize_t) used : 0;
              return -1;
            }
        }
      else
        {
          n = recv (ctx->fd, &ch, 1, 0);
          if (n == 0) return used ? (ssize_t) used : 0;
          if (n < 0)
            {
              if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
              return -1;
            }
        }
      buf[used++] = ch;
      if (ch == '\n')
        {
          buf[used] = '\0';
          return (ssize_t) used;
        }
    }
  return -2;
}

void *
connection_thread (void *arg)
{
  client_ctx_t *ctx = (client_ctx_t *) arg;
  g_ctx_for_send = ctx;
  int fd = ctx->fd;
  char *line = malloc ((size_t) g_cfg.server_max_request_bytes + 1);
  ssize_t read_len;

  if (line && g_cfg.tls_enabled && connection_tls_handshake (ctx) != 0)
    goto cleanup;
  if (!line)
    goto cleanup;

  loop_add_client (ctx);
  ctx->rl_limit = g_cfg.server_requests_per_minute;
  ctx->rl_window_sec = 60;
  ctx->rl_window_start = time (NULL);
  struct timeval io_timeout = {
    .tv_sec = g_cfg.server_request_timeout_ms / 1000,
    .tv_usec = (g_cfg.server_request_timeout_ms % 1000) * 1000
  };
  (void) setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &io_timeout, sizeof io_timeout);
  (void) setsockopt (fd, SOL_SOCKET, SO_SNDTIMEO, &io_timeout, sizeof io_timeout);

  while ((read_len = client_readline (ctx, line,
                                      (size_t) g_cfg.server_max_request_bytes + 1,
                                      g_cfg.server_request_timeout_ms)) != 0)
    {
      if (read_len == -2)
        {
          send_response_error (ctx, NULL, ERR_BAD_REQUEST, "Request exceeds maximum frame size");
          break;
        }
      if (read_len == -3)
        {
          send_response_error (ctx, NULL, ERR_TIMEOUT, "Request deadline exceeded");
          break;
        }
      if (read_len < 0)
        break;
      if (!request_rate_allow (ctx))
        {
          send_response_error (ctx, NULL, ERR_RATE_LIMITED, "Request rate limit exceeded");
          break;
        }
      json_error_t jerr;
      json_t *root = json_loads (line, 0, &jerr);
      if (!root || !json_is_object (root))
        {
          send_enveloped_error (fd, NULL, ERR_INVALID_SCHEMA, "Malformed JSON");
          if (root) json_decref (root);
        }
      else
        {
          process_message (ctx, root);
          json_decref (root);
        }
    }

cleanup:
  free (line);
  /* TLS cleanup */
  if (ctx->is_tls && ctx->ssl_conn)
    {
      SSL_shutdown (ctx->ssl_conn);
      SSL_free (ctx->ssl_conn);
    }

  db_close_thread ();
  loop_remove_client (ctx);
  if (ctx->connection_counted)
    atomic_fetch_sub (&g_active_client_connections, 1);
  close (fd);
  free (ctx);
  return NULL;
}

int
server_loop (volatile sig_atomic_t *running)
{
  LOGI ("Server loop starting...\n");
#ifdef SIGPIPE
  signal (SIGPIPE, SIG_IGN);
#endif

  /* Initialize TLS if enabled */
  if (g_cfg.tls_enabled)
    {
      SSL_library_init ();
      SSL_load_error_strings ();
      g_ssl_ctx = SSL_CTX_new (TLS_server_method ());
      if (!g_ssl_ctx)
	{
	  LOGE ("Failed to create SSL context\n");
	  return -1;
	}

      /* Load certificate and key */
      if (SSL_CTX_use_certificate_file (g_ssl_ctx, g_cfg.tls_cert_path,
					 SSL_FILETYPE_PEM) <= 0)
	{
	  LOGE ("Failed to load TLS certificate: %s\n", g_cfg.tls_cert_path);
	  SSL_CTX_free (g_ssl_ctx);
	  return -1;
	}

      if (SSL_CTX_use_PrivateKey_file (g_ssl_ctx, g_cfg.tls_key_path,
					SSL_FILETYPE_PEM) <= 0)
	{
	  LOGE ("Failed to load TLS private key: %s\n", g_cfg.tls_key_path);
	  SSL_CTX_free (g_ssl_ctx);
	  return -1;
	}

      if (!SSL_CTX_check_private_key (g_ssl_ctx))
	{
	  LOGE ("TLS private key does not match certificate\n");
	  SSL_CTX_free (g_ssl_ctx);
	  return -1;
	}

      SSL_CTX_set_options (g_ssl_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
      SSL_CTX_set_min_proto_version (g_ssl_ctx, TLS1_2_VERSION);
      LOGI ("TLS initialized with cert=%s, key=%s\n",
	    g_cfg.tls_cert_path, g_cfg.tls_key_path);
    }

  int listen_fd = make_listen_socket (g_cfg.server_port,
                                      g_cfg.server_bind_address);

  if (listen_fd < 0)
    {
      LOGE ("Server loop exiting due to listen socket error.\n");
      if (g_ssl_ctx)
	SSL_CTX_free (g_ssl_ctx);
      return -1;
    }
  LOGI ("Listening on %s:%d\n", g_cfg.server_bind_address,
        g_cfg.server_port);
  struct pollfd pfd = {.fd = listen_fd,.events = POLLIN,.revents = 0 };
  pthread_attr_t attr;

  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);

  while (*running)
    {
      g_server_tick++;
      int rc = poll (&pfd, 1, 100);
      {
	static uint64_t last_broadcast_ms = 0;
	static uint64_t last_event_prune_ms = 0;
	uint64_t now_ms = monotonic_millis ();

	if (now_ms - last_broadcast_ms >= 500)
	  {
	    (void) broadcast_sweep_once (game_db_get_handle (), 64);
	    last_broadcast_ms = now_ms;
	  }
	if (now_ms - last_event_prune_ms >= 60000)
	  {
	    int64_t deleted = 0;
	    int prune_rc = repo_comm_event_prune (game_db_get_handle (), 128,
	                                          &deleted);
	    if (prune_rc != 0)
	      LOGE ("player event retention cleanup failed (%d)", prune_rc);
	    else if (deleted > 0)
	      LOGI ("player event retention removed %" PRId64 " expired rows",
	            deleted);
	    last_event_prune_ms = now_ms;
	}
      }

      if (rc < 0)
	{
	  if (errno == EINTR)
	    {
	      continue;
	    }
	  perror ("poll");
	  break;
	}
      if (rc == 0)
	{
	  continue;
	}
      if (pfd.revents & POLLIN)
	{
	  client_ctx_t *ctx = calloc (1, sizeof (*ctx));

	  if (!ctx)
	    {
	      LOGE ("malloc failed\n");
	      continue;
	    }
	  socklen_t sl = sizeof (ctx->peer);
	  int cfd = accept (listen_fd, (struct sockaddr *) &ctx->peer, &sl);

	  if (cfd < 0)
	    {
	      free (ctx);
	      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
		{
		  continue;
		}
	      perror ("accept");
	      continue;
	    }
	  int previous = atomic_fetch_add (&g_active_client_connections, 1);
	  if (previous >= g_cfg.server_max_connections)
	    {
	      atomic_fetch_sub (&g_active_client_connections, 1);
	      close (cfd);
	      free (ctx);
	      continue;
	    }
	  ctx->connection_counted = 1;
	  ctx->fd = cfd;
	  ctx->running = (sig_atomic_t *) running;
	  char ip[INET_ADDRSTRLEN];

	  inet_ntop (AF_INET, &ctx->peer.sin_addr, ip, sizeof (ip));
	  LOGI ("Client connected: %s:%u (fd=%d, tls=%d)\n", ip,
		(unsigned) ntohs (ctx->peer.sin_port), cfd, ctx->is_tls);
	  pthread_t th;
	  int prc = pthread_create (&th, &attr, connection_thread, ctx);

	  if (prc == 0)
	    {
	      LOGI ("[cid=%" PRIu64 "] thread created", ctx->cid);
	    }
	  else
	    {
	      LOGE ("pthread_create: %s\n", strerror (prc));
	      atomic_fetch_sub (&g_active_client_connections, 1);
	      close (cfd);
	      free (ctx);
	    }
	}
    }
  pthread_attr_destroy (&attr);
  close (listen_fd);
  LOGI ("Server loop exiting...\n");
  return 0;
}

void
loop_add_client (client_ctx_t *ctx)
{
  pthread_mutex_lock (&g_clients_mu);
  client_node_t *n = malloc (sizeof (client_node_t));
  n->ctx = ctx;
  n->next = g_clients;
  g_clients = n;
  pthread_mutex_unlock (&g_clients_mu);
}

void
loop_remove_client (client_ctx_t *ctx)
{
  pthread_mutex_lock (&g_clients_mu);
  client_node_t **pp = &g_clients;
  while (*pp)
    {
      if ((*pp)->ctx == ctx)
	{
	  client_node_t *tmp = *pp;
	  *pp = (*pp)->next;
	  free (tmp);
	  break;
	}
      pp = &((*pp)->next);
    }
  pthread_mutex_unlock (&g_clients_mu);
}
