/* src/server_loop.c */
#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jansson.h>
#include <stdbool.h>

/* local includes */
#include "database.h"
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
#include "database_cmd.h"
#include "db/db_api.h"

#ifndef streq
#define streq(a,b) (strcasecmp (json_string_value ((a)), (b)) == 0)
#endif
#define BUF_SIZE    8192

__thread client_ctx_t *g_ctx_for_send = NULL;


/* forward declaration to avoid implicit extern */
void send_all_json (int fd, json_t *obj);

/* rate-limit helper prototypes (defined later) */
void attach_rate_limit_meta (json_t *env, client_ctx_t *ctx);
void rl_tick (client_ctx_t *ctx);

/* EXTERN FIXES: Added missing externs and corrected signatures */
extern int cmd_bounty_list (client_ctx_t *ctx, json_t *root);
extern int cmd_bounty_post_federation (client_ctx_t *ctx, json_t *root);
extern int cmd_bounty_post_hitlist (client_ctx_t *ctx, json_t *root);
extern int cmd_player_set_trade_account_preference (client_ctx_t *ctx,
                                                    json_t *root);
extern int cmd_move_transwarp (client_ctx_t *ctx, json_t *root);
extern int cmd_insurance_policies_list (client_ctx_t *ctx, json_t *root);
extern int cmd_insurance_policies_buy (client_ctx_t *ctx, json_t *root);
extern int cmd_insurance_claim_file (client_ctx_t *ctx, json_t *root);
extern int cmd_player_get_topics (client_ctx_t *ctx, json_t *root);
extern int cmd_player_set_topics (client_ctx_t *ctx, json_t *root);
extern int cmd_sys_cron_planet_tick_once (client_ctx_t *ctx, json_t *root);
extern int cmd_trade_accept (client_ctx_t *ctx, json_t *root);
extern int cmd_trade_cancel (client_ctx_t *ctx, json_t *root);
extern int cmd_trade_offer (client_ctx_t *ctx, json_t *root);

/* UPDATED SIGNATURE: passing db_t* as first arg */
extern int cmd_move_autopilot_start (db_t *db, client_ctx_t *ctx, json_t *root);


static inline uint64_t
monotonic_millis (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}


/* --------------------------------------------------------------------------
   Command Registry & Dispatch
   -------------------------------------------------------------------------- */

typedef int (*command_handler_fn)(client_ctx_t *ctx, json_t *root);

#define CMD_FLAG_DEBUG_ONLY 1
#define CMD_FLAG_HIDDEN 2

typedef struct {
  const char *name;
  command_handler_fn handler;
  const char *summary;
  schema_generator_fn schema;
  int flags;
} command_entry_t;


/* Wrappers for void or mismatched handlers */
static int
w_nav_avoid_add (client_ctx_t *ctx, json_t *root)
{
  cmd_nav_avoid_add (ctx, root); return 0;
}


static int
w_nav_avoid_remove (client_ctx_t *ctx, json_t *root)
{
  cmd_nav_avoid_remove (ctx, root); return 0;
}


static int
w_nav_avoid_list (client_ctx_t *ctx, json_t *root)
{
  cmd_nav_avoid_list (ctx, root); return 0;
}


static int
w_sector_scan (client_ctx_t *ctx, json_t *root)
{
  cmd_sector_scan (ctx, root); return 0;
}


static int
w_sector_scan_density (client_ctx_t *ctx, json_t *root)
{
  cmd_sector_scan_density ((void *)ctx, root); return 0;
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


/* WRAPPER FIX: Pass the DB handle to autopilot start */
static int
w_move_autopilot_start (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  return cmd_move_autopilot_start (db, ctx, root);
}


static const command_entry_t k_command_registry[] = {
  /* Admin */
  {"admin.notice", cmd_admin_notice, "Admin notice", schema_admin_notice, 0},
  {"admin.shutdown_warning", cmd_admin_shutdown_warning,
   "Admin shutdown warning", schema_admin_shutdown_warning, 0},

  /* Auth */
  {"auth.login", w_auth_login, "Authenticate", schema_auth_login, 0},
  {"auth.logout", cmd_auth_logout, "Log out", schema_auth_logout, 0},
  {"auth.mfa.totp.verify", cmd_auth_mfa_totp_verify, "Second-factor code",
   schema_auth_mfa_totp_verify, 0},
  {"auth.register", cmd_auth_register, "Create a new player",
   schema_auth_register, 0},
  {"auth.refresh", cmd_auth_refresh, "Refresh session token",
   schema_auth_refresh, 0},

  /* Bank */
  {"bank.balance", cmd_bank_balance, "Get player bank balance",
   schema_bank_balance, 0},
  {"bank.deposit", cmd_bank_deposit, "Deposit credits to bank",
   schema_placeholder, 0},
  {"bank.history", cmd_bank_history, "Get bank history", schema_bank_history,
   0},
  {"bank.leaderboard", cmd_bank_leaderboard, "Get bank leaderboard",
   schema_bank_leaderboard, 0},
  {"bank.transfer", cmd_bank_transfer, "Transfer credits between players",
   schema_placeholder, 0},
  {"bank.withdraw", cmd_bank_withdraw, "Withdraw credits from bank",
   schema_placeholder, 0},

  /* Bounty */
  {"bounty.list", cmd_bounty_list, "List bounties", schema_placeholder, 0},
  {"bounty.post_federation", cmd_bounty_post_federation,
   "Post a Federation bounty", schema_placeholder, 0},
  {"bounty.post_hitlist", cmd_bounty_post_hitlist, "Post a Hit List contract",
   schema_placeholder, 0},

  /* Bulk */
  {"bulk.execute", cmd_bulk_execute, "Execute a bulk command",
   schema_bulk_execute, 0},

  /* Fines */
  {"fine.list", cmd_fine_list, "List player outstanding fines",
   schema_fine_list, 0},
  {"fine.pay", cmd_fine_pay, "Pay an outstanding fine", schema_fine_pay, 0},

  /* Insurance */
  {"insurance.policies.list", cmd_insurance_policies_list,
   "List player insurance policies", schema_insurance_policies_list, 0},
  {"insurance.policies.buy", cmd_insurance_policies_buy,
   "Buy an insurance policy", schema_insurance_policies_buy, 0},
  {"insurance.claim.file", cmd_insurance_claim_file, "File an insurance claim",
   schema_insurance_claim_file, 0},

  /* Chat */
  {"chat.broadcast", cmd_chat_broadcast, "Broadcast a chat message",
   schema_chat_broadcast, 0},
  {"chat.history", cmd_chat_history, "Chat history", schema_chat_history, 0},
  {"chat.send", cmd_chat_send, "Send a chat message", schema_chat_send, 0},

  /* Citadel */
  {"citadel.build", cmd_citadel_build, "Build a citadel", schema_citadel_build,
   0},
  {"citadel.upgrade", cmd_citadel_upgrade, "Upgrade a citadel",
   schema_citadel_upgrade, 0},

  /* Combat */
  {"combat.attack", cmd_combat_attack, "Attack a target", schema_combat_attack,
   0},
  {"combat.attack_planet", cmd_combat_attack_planet, "Attack a planet",
   schema_placeholder, 0},
  {"combat.deploy_fighters", cmd_combat_deploy_fighters, "Deploy fighters",
   schema_combat_deploy_fighters, 0},
  {"combat.deploy_mines", cmd_combat_deploy_mines, "Deploy mines",
   schema_combat_deploy_mines, 0},
  {"combat.lay_mines", cmd_combat_lay_mines, "Lay mines",
   schema_combat_lay_mines, 0},
  {"combat.status", cmd_combat_status, "Combat status", schema_combat_status,
   0},
  {"combat.sweep_mines", cmd_combat_sweep_mines, "Sweep for mines",
   schema_combat_sweep_mines, 0},
  {"deploy.fighters.list", cmd_deploy_fighters_list, "List deployed fighters",
   schema_deploy_fighters_list, 0},
  {"deploy.mines.list", cmd_deploy_mines_list, "List deployed mines",
   schema_deploy_mines_list, 0},
  {"fighters.recall", cmd_fighters_recall, "Recall deployed fighters",
   schema_fighters_recall, 0},
  {"mines.recall", cmd_mines_recall, "Recall deployed mines",
   schema_mines_recall, 0},

  /* Corporation */
  {"corp.balance", cmd_corp_balance, "Get corporation balance",
   schema_placeholder, 0},
  {"corp.create", cmd_corp_create, "Create a new corporation",
   schema_placeholder, 0},
  {"corp.deposit", cmd_corp_deposit, "Deposit to corporation",
   schema_placeholder, 0},
  {"corp.dissolve", cmd_corp_dissolve, "Dissolve a corporation",
   schema_placeholder, 0},
  {"corp.invite", cmd_corp_invite, "Invite player to corporation",
   schema_placeholder, 0},
  {"corp.join", cmd_corp_join, "Join a corporation via invite",
   schema_placeholder, 0},
  {"corp.kick", cmd_corp_kick, "Kick member from corporation",
   schema_placeholder, 0},
  {"corp.leave", cmd_corp_leave, "Leave current corporation",
   schema_placeholder, 0},
  {"corp.list", cmd_corp_list, "List all corporations", schema_placeholder, 0},
  {"corp.roster", cmd_corp_roster, "List corporation members",
   schema_placeholder, 0},
  {"corp.statement", cmd_corp_statement, "Get corporation statement",
   schema_placeholder, 0},
  {"corp.status", cmd_corp_status, "Get corporation status", schema_placeholder,
   0},
  {"corp.transfer_ceo", cmd_corp_transfer_ceo, "Transfer corporation CEO role",
   schema_placeholder, 0},
  {"corp.withdraw", cmd_corp_withdraw, "Withdraw from corporation",
   schema_placeholder, 0},

  /* Dock */
  {"dock.status", cmd_dock_status,
   "Check/set player docked status at current port", schema_placeholder, 0},

  /* Hardware */
  {"hardware.buy", cmd_hardware_buy, "Buy ship hardware", schema_hardware_buy,
   0},
  {"hardware.list", cmd_hardware_list, "List available ship hardware",
   schema_hardware_list, 0},

  /* Mail */
  {"mail.delete", cmd_mail_delete, "Delete mail", schema_mail_delete, 0},
  {"mail.inbox", cmd_mail_inbox, "Mail inbox", schema_mail_inbox, 0},
  {"mail.read", cmd_mail_read, "Read mail", schema_mail_read, 0},
  {"mail.send", cmd_mail_send, "Send mail", schema_mail_send, 0},

  /* Move */
  {"move.autopilot.start", w_move_autopilot_start, "Start autopilot",
   schema_move_autopilot_start, 0},
  {"move.autopilot.status", cmd_move_autopilot_status, "Autopilot status",
   schema_move_autopilot_status, 0},
  {"move.autopilot.stop", cmd_move_autopilot_stop, "Stop autopilot",
   schema_move_autopilot_stop, 0},
  {"move.describe_sector", cmd_move_describe_sector, "Describe a sector",
   schema_move_describe_sector, 0},
  {"move.pathfind", cmd_move_pathfind, "Find path between sectors",
   schema_move_pathfind, 0},
  {"move.scan", cmd_move_scan, "Scan adjacent sectors", schema_move_scan, 0},
  {"move.transwarp", cmd_move_transwarp, "Transwarp to a sector",
   schema_placeholder, 0},
  {"move.warp", cmd_move_warp, "Warp to sector", schema_move_warp, 0},

  /* Nav / Bookmarks / Avoids */
  {"nav.avoid.add", w_nav_avoid_add, "Add a sector to the avoid list",
   schema_placeholder, 0},
  {"nav.avoid.list", w_nav_avoid_list, "List avoided sectors",
   schema_placeholder, 0},
  {"nav.avoid.remove", w_nav_avoid_remove, "Remove sector from avoid list",
   schema_placeholder, 0},
  {"nav.avoid.set", cmd_player_set_avoids, "Set a sector to the avoid list",
   schema_placeholder, 0},
  {"nav.bookmark.add", cmd_player_set_bookmarks, "Add a bookmark",
   schema_placeholder, 0},
  {"nav.bookmark.list", cmd_player_get_bookmarks, "List bookmarks",
   schema_placeholder, 0},
  {"nav.bookmark.remove", cmd_player_set_bookmarks, "Remove a bookmark",
   schema_placeholder, 0},
  {"nav.bookmark.set", cmd_player_set_bookmarks, "Set a bookmark",
   schema_placeholder, 0},

  /* News */
  {"news.get_feed", cmd_news_get_feed, "Get the daily news feed",
   schema_placeholder, 0},
  {"news.mark_feed_read", cmd_news_mark_feed_read, "Mark news feed as read",
   schema_placeholder, 0},
  {"news.read", cmd_news_read, "Read news feed", schema_news_read, 0},

  /* Notes */
  {"notes.list", cmd_player_get_notes, "List notes", schema_placeholder, 0},

  /* Notice */
  {"notice.ack", cmd_notice_ack, "Acknowledge a notice", schema_notice_ack, 0},
  {"notice.list", cmd_notice_list, "List notices", schema_notice_list, 0},

  /* Planet */
  {"planet.create", cmd_planet_genesis_create, "Create a planet",
   schema_planet_genesis_create, 0},
  {"planet.deposit", cmd_planet_deposit, "Deposit to a planet",
   schema_planet_deposit, 0},
  {"planet.genesis", cmd_planet_genesis_create, "Create a planet",
   schema_planet_genesis, 0},
  {"planet.genesis_create", cmd_planet_genesis_create,
   "Create a genesis planet", schema_planet_genesis_create, 0},
  {"planet.harvest", cmd_planet_harvest, "Harvest from a planet",
   schema_planet_harvest, 0},
  {"planet.info", cmd_planet_info, "Planet information", schema_planet_info, 0},
  {"planet.land", cmd_planet_land, "Land on a planet", schema_planet_land, 0},
  {"planet.launch", cmd_planet_launch, "Launch from a planet",
   schema_planet_launch, 0},
  {"planet.market.buy_order", cmd_planet_market_buy_order,
   "Create planet market buy order", schema_placeholder, 0},
  {"planet.market.sell", cmd_planet_market_sell, "Sell to planet market",
   schema_placeholder, 0},
  {"planet.rename", cmd_planet_rename, "Rename a planet", schema_planet_rename,
   0},
  {"planet.transfer_ownership", cmd_planet_transfer_ownership,
   "Transfer planet ownership", schema_planet_transfer_ownership, 0},
  {"planet.withdraw", cmd_planet_withdraw, "Withdraw from a planet",
   schema_planet_withdraw, 0},

  /* Player */
  {"player.get_avoids", cmd_player_get_avoids, "Get player avoids",
   schema_placeholder, 0},
  {"player.get_bookmarks", cmd_player_get_bookmarks, "Get player bookmarks",
   schema_placeholder, 0},
  {"player.get_notes", cmd_player_get_notes, "Get player notes",
   schema_placeholder, 0},
  {"player.get_prefs", cmd_player_get_prefs, "Get player preferences",
   schema_placeholder, 0},
  {"player.get_settings", cmd_player_get_settings, "Get player settings",
   schema_placeholder, 0},
  {"player.get_subscriptions", cmd_player_get_topics,
   "Get player subscriptions", schema_placeholder, 0},
  {"player.get_topics", cmd_player_get_topics, "Get player topics",
   schema_placeholder, 0},
  {"player.list_online", cmd_player_list_online, "List online players",
   schema_player_list_online_request, 0},
  {"player.my_info", cmd_player_my_info, "Current player info",
   schema_player_my_info, 0},
  {"player.rankings", cmd_player_rankings, "Player rankings",
   schema_placeholder, 0},
  {"player.set_avoids", cmd_player_set_avoids, "Set player avoids",
   schema_placeholder, 0},
  {"player.set_bookmarks", cmd_player_set_bookmarks, "Set player bookmarks",
   schema_placeholder, 0},
  {"player.set_prefs", cmd_player_set_prefs, "Set player preferences",
   schema_placeholder, 0},
  {"player.set_settings", cmd_player_set_settings, "Set player settings",
   schema_placeholder, 0},
  {"player.set_subscriptions", cmd_player_set_topics,
   "Set player subscriptions", schema_placeholder, 0},
  {"player.set_topics", cmd_player_set_topics, "Set player topics",
   schema_placeholder, 0},
  {"player.set_trade_account_preference",
   cmd_player_set_trade_account_preference, "Set trade account preference",
   schema_player_set_trade_account_preference, 0},

  /* Port */
  {"port.describe", cmd_trade_port_info, "Describe a port",
   schema_port_describe, 0},
  {"port.info", cmd_trade_port_info, "Port prices/stock in sector",
   schema_port_info, 0},
  {"port.rob", cmd_port_rob, "Attempt to rob a port", schema_port_rob, 0},
  {"port.status", cmd_trade_port_info, "Port status", schema_port_status, 0},

  /* S2S (Hidden) */
  {"s2s.event.relay", cmd_s2s_event_relay, "S2S event relay",
   schema_placeholder, CMD_FLAG_HIDDEN},
  {"s2s.planet.genesis", cmd_s2s_planet_genesis, "S2S planet genesis",
   schema_placeholder, CMD_FLAG_HIDDEN},
  {"s2s.planet.transfer", cmd_s2s_planet_transfer, "S2s planet transfer",
   schema_placeholder, CMD_FLAG_HIDDEN},
  {"s2s.player.migrate", cmd_s2s_player_migrate, "S2S player migrate",
   schema_placeholder, CMD_FLAG_HIDDEN},
  {"s2s.port.restock", cmd_s2s_port_restock, "S2S port restock",
   schema_placeholder, CMD_FLAG_HIDDEN},
  {"s2s.replication.heartbeat", cmd_s2s_replication_heartbeat,
   "S2S replication heartbeat", schema_placeholder, CMD_FLAG_HIDDEN},

  /* Sector */
  {"sector.info", cmd_move_describe_sector, "Describe current sector",
   schema_sector_info, 0},
  {"sector.scan", w_sector_scan, "Scan a sector", schema_sector_scan, 0},
  {"sector.scan.density", w_sector_scan_density, "Scan sector density",
   schema_sector_scan_density, 0},
  {"sector.search", cmd_sector_search, "Search a sector", schema_sector_search,
   0},
  {"sector.set_beacon", cmd_sector_set_beacon, "Set or clear sector beacon",
   schema_sector_set_beacon, 0},

  /* Session / System */
  {"session.disconnect", cmd_session_disconnect, "Disconnect",
   schema_session_disconnect, 0},
  {"session.hello", cmd_system_hello, "Handshake / hello", schema_session_hello,
   0},
  {"session.ping", cmd_system_hello, "Ping", schema_session_ping, 0},
  {"player.ping", cmd_system_hello, "Ping", schema_session_ping, 0},
  {"sys.cluster.init", cmd_sys_cluster_init, "Cluster init", schema_placeholder,
   CMD_FLAG_DEBUG_ONLY | CMD_FLAG_HIDDEN},
  {"sys.cluster.seed_illegal_goods", cmd_sys_cluster_seed_illegal_goods,
   "Seed illegal goods", schema_placeholder,
   CMD_FLAG_DEBUG_ONLY | CMD_FLAG_HIDDEN},
  {"sys.econ.orders_summary", cmd_sys_econ_orders_summary,
   "Economy orders summary", schema_placeholder, CMD_FLAG_HIDDEN},
  {"sys.econ.planet_status", cmd_sys_econ_planet_status,
   "Economy planet status", schema_placeholder, CMD_FLAG_HIDDEN},
  {"sys.econ.port_status", cmd_sys_econ_port_status, "Economy port status",
   schema_placeholder, CMD_FLAG_HIDDEN},
  {"sys.notice.create", cmd_sys_notice_create,
   "Sysop command to create a notice", schema_sys_notice_create, 0},
  {"sys.npc.ferengi_tick_once", cmd_sys_npc_ferengi_tick_once, "Tick Ferengi",
   schema_placeholder, CMD_FLAG_HIDDEN},
  {"sys.cron.planet_tick_once", cmd_sys_cron_planet_tick_once,
   "Force a planet cron tick (production, market)", schema_placeholder,
   CMD_FLAG_DEBUG_ONLY | CMD_FLAG_HIDDEN},
  {"sys.raw_sql_exec", cmd_sys_raw_sql_exec, "Sysop command to execute raw SQL",
   schema_placeholder, CMD_FLAG_DEBUG_ONLY | CMD_FLAG_HIDDEN},
  {"sys.test_news_cron", cmd_sys_test_news_cron,
   "Sysop command to test news cron", schema_placeholder, CMD_FLAG_HIDDEN},
  {"system.capabilities", cmd_system_capabilities,
   "Feature flags, schemas, counts", schema_system_capabilities, 0},
  {"system.cmd_list", cmd_system_cmd_list, "Flat list of all commands",
   schema_placeholder, 0},
  {"system.describe_schema", cmd_system_describe_schema,
   "Describe commands in a schema", schema_system_describe_schema, 0},
  {"system.disconnect", cmd_session_disconnect, "Disconnect",
   schema_system_disconnect, 0},
  {"system.hello", cmd_system_hello, "Handshake / hello", schema_system_hello,
   0},
  {"system.schema_list", cmd_system_schema_list, "List all schema namespaces",
   schema_placeholder, 0},

  /* Ship */
  {"ship.claim", cmd_ship_claim, "Claim a ship", schema_ship_claim, 0},
  {"ship.info", cmd_ship_info_compat, "Ship information", schema_ship_info, 0},
  {"ship.inspect", cmd_ship_inspect, "Inspect a ship", schema_ship_inspect, 0},
  {"ship.jettison", cmd_trade_jettison, "Jettison cargo", schema_ship_jettison,
   0},
  {"ship.rename", cmd_ship_rename, "Rename a ship", schema_ship_rename, 0},
  {"ship.repair", cmd_ship_repair, "Repair a ship", schema_ship_repair, 0},
  {"ship.reregister", cmd_ship_rename, "Re-register a ship",
   schema_ship_reregister, 0},
  {"ship.self_destruct", cmd_ship_self_destruct, "Self-destruct a ship",
   schema_ship_self_destruct, 0},
  {"ship.status", cmd_ship_status, "Ship status", schema_ship_status, 0},
  {"ship.transfer_cargo", cmd_ship_transfer_cargo, "Transfer cargo",
   schema_ship_transfer_cargo, 0},
  {"ship.upgrade", cmd_ship_upgrade, "Upgrade a ship", schema_ship_upgrade, 0},

  /* Shipyard */
  {"shipyard.list", cmd_shipyard_list, "List available ship hulls",
   schema_placeholder, 0},
  {"shipyard.upgrade", cmd_shipyard_upgrade, "Upgrade to a new ship hull",
   schema_placeholder, 0},

  /* Stock */
  {"stock.buy", cmd_stock, "Buy shares in a corporation", schema_placeholder,
   0},
  {"stock.dividend.set", cmd_stock, "Declare a dividend", schema_placeholder,
   0},
  {"stock.exchange.list_stocks", cmd_stock, "List stocks on the exchange",
   schema_placeholder, 0},
  {"stock.exchange.orders.cancel", cmd_stock, "Cancel a stock order",
   schema_placeholder, 0},
  {"stock.exchange.orders.create", cmd_stock, "Create a stock order",
   schema_placeholder, 0},
  {"stock.ipo.register", cmd_stock, "Register corporation for IPO",
   schema_placeholder, 0},
  {"stock.portfolio.list", cmd_stock, "List stock portfolio",
   schema_placeholder, 0},

  /* Subscribe */
  {"subscribe.add", cmd_subscribe_add, "Add a subscription",
   schema_subscribe_add, 0},
  {"subscribe.catalog", cmd_subscribe_catalog, "Subscription catalog",
   schema_subscribe_catalog, 0},
  {"subscribe.list", cmd_subscribe_list, "List subscriptions",
   schema_subscribe_list, 0},
  {"subscribe.remove", cmd_subscribe_remove, "Remove a subscription",
   schema_subscribe_remove, 0},

  /* Tavern */
  {"tavern.barcharts.get_prices_summary",
   cmd_tavern_barcharts_get_prices_summary, "Get a summary of commodity prices",
   schema_placeholder, 0},
  {"tavern.deadpool.place_bet", cmd_tavern_deadpool_place_bet,
   "Place a bet on a player's destruction", schema_placeholder, 0},
  {"tavern.dice.play", cmd_tavern_dice_play, "Play bar dice",
   schema_placeholder, 0},
  {"tavern.graffiti.post", cmd_tavern_graffiti_post,
   "Post a message on the graffiti wall", schema_placeholder, 0},
  {"tavern.highstakes.play", cmd_tavern_highstakes_play,
   "Play at the high-stakes table", schema_placeholder, 0},
  {"tavern.loan.pay", cmd_tavern_loan_pay, "Repay a loan from the loan shark",
   schema_placeholder, 0},
  {"tavern.loan.take", cmd_tavern_loan_take, "Take a loan from the loan shark",
   schema_placeholder, 0},
  {"tavern.lottery.buy_ticket", cmd_tavern_lottery_buy_ticket,
   "Buy a lottery ticket", schema_placeholder, 0},
  {"tavern.lottery.status", cmd_tavern_lottery_status, "Check lottery status",
   schema_placeholder, 0},
  {"tavern.raffle.buy_ticket", cmd_tavern_raffle_buy_ticket,
   "Buy a raffle ticket", schema_placeholder, 0},
  {"tavern.round.buy", cmd_tavern_round_buy, "Buy a round for the tavern",
   schema_placeholder, 0},
  {"tavern.rumour.get_hint", cmd_tavern_rumour_get_hint,
   "Get a hint from the rumour mill", schema_placeholder, 0},
  {"tavern.trader.buy_password", cmd_tavern_trader_buy_password,
   "Buy an underground password from the grimy trader", schema_placeholder, 0},

  /* Trade */
  {"trade.accept", cmd_trade_accept, "Accept a private trade offer",
   schema_trade_accept, 0},
  {"trade.buy", cmd_trade_buy, "Buy commodity from port", schema_trade_buy, 0},
  {"trade.cancel", cmd_trade_cancel, "Cancel a pending trade offer",
   schema_trade_cancel, 0},
  {"trade.history", cmd_trade_history, "View recent trade transactions",
   schema_trade_history, 0},
  {"trade.jettison", cmd_trade_jettison, "Dump cargo into space",
   schema_trade_jettison, 0},
  {"trade.offer", cmd_trade_offer,
   "Create a private player-to-player trade offer", schema_trade_offer, 0},
  {"trade.port_info", cmd_trade_port_info, "Port prices/stock in sector",
   schema_trade_port_info, 0},
  {"trade.quote", cmd_trade_quote, "Get a quote for a trade action",
   schema_trade_quote, 0},
  {"trade.sell", cmd_trade_sell, "Sell commodity to port", schema_trade_sell,
   0},

  {NULL, NULL, NULL, NULL, 0}   /* Sentinel */
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
              n++; continue;
            }
#endif
          if (k_command_registry[n].flags & CMD_FLAG_HIDDEN)
            {
              n++; continue;
            }
          n++; count++;
        }
      s_descs = calloc (count, sizeof(cmd_desc_t));
      if (s_descs)
        {
          size_t j = 0; size_t i = 0;


          while (k_command_registry[i].name)
            {
#ifdef BUILD_PRODUCTION
              if (k_command_registry[i].flags & CMD_FLAG_DEBUG_ONLY)
                {
                  i++; continue;
                }
#endif
              if (k_command_registry[i].flags & CMD_FLAG_HIDDEN)
                {
                  i++; continue;
                }
              s_descs[j].name = k_command_registry[i].name;
              s_descs[j].summary = k_command_registry[i].summary;
              j++; i++;
            }
          s_count = count;
        }
    }
  if (out_tbl)
    {
      *out_tbl = s_descs;
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
  server_broadcast_event ("system.notice", json_incref (data));
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

  static const char *sql =
    "SELECT id, created_at, title, body, severity, expires_at "
    "FROM system_notice "
    "WHERE id NOT IN ("
    "    SELECT notice_id FROM notice_seen WHERE player_id = 0"
    ") "
    "ORDER BY created_at ASC, id ASC "
    "LIMIT $1;";


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      if (err.code == ERR_DB_BUSY || err.code == ERR_DB_LOCKED)
        {
          return 0;
        }
      return -1;
    }

  db_bind_t params[] = { db_bind_i32 (max_rows) };


  if (!db_query (db, sql, params, 1, &res, &err))
    {
      db_tx_rollback (db, NULL);
      return -1;
    }

  while (db_res_step (res, &err))
    {
      int notice_id = db_res_col_i32 (res, 0, &err);
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
      n->ctx = ctx; n->next = g_clients; g_clients = n;
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
          client_node_t *dead = *pp; *pp = (*pp)->next; free (dead); break;
        }
      pp = &((*pp)->next);
    }
  pthread_mutex_unlock (&g_clients_mu);
}


int
server_deliver_to_player (int player_id, const char *event_type, json_t *data)
{
  int delivered = 0;
  pthread_mutex_lock (&g_clients_mu);
  for (client_node_t *n = g_clients; n; n = n->next)
    {
      client_ctx_t *c = n->ctx;


      if (!c)
        {
          continue;
        }
      if (c->player_id == player_id && c->fd >= 0)
        {
          json_t *tmp = json_incref (data);


          send_response_ok_take (c, NULL, event_type, &tmp);
          delivered++;
        }
    }
  pthread_mutex_unlock (&g_clients_mu);
  return (delivered > 0) ? 0 : -1;
}


/* ------------------------ idempotency helpers  ------------------------ */
static uint64_t
fnv1a64 (const unsigned char *s, size_t n)
{
  uint64_t h = 1469598103934665603ULL;
  for (size_t i = 0; i < n; ++i)
    {
      h ^= (uint64_t) s[i]; h *= 1099511628211ULL;
    }
  return h;
}


static void
hex64 (uint64_t v, char out[17])
{
  static const char hexd[] = "0123456789abcdef";
  for (int i = 15; i >= 0; --i)
    {
      out[i] = hexd[v & 0xF]; v >>= 4;
    }
  out[16] = '\0';
}


void
idemp_fingerprint_json (json_t *obj, char out[17])
{
  char *s = json_dumps (obj, JSON_COMPACT | JSON_SORT_KEYS);
  if (!s)
    {
      strcpy (out, "0"); return;
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
make_listen_socket (uint16_t port)
{
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    {
      perror ("socket"); return -1;
    }
  if (set_reuseaddr (fd) < 0)
    {
      perror ("setsockopt(SO_REUSEADDR)"); close (fd); return -1;
    }
  struct sockaddr_in sa;


  memset (&sa, 0, sizeof (sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl (INADDR_ANY);
  sa.sin_port = htons (port);
  if (bind (fd, (struct sockaddr *) &sa, sizeof (sa)) < 0)
    {
      perror ("bind"); close (fd); return -1;
    }
  if (listen (fd, 128) < 0)
    {
      perror ("listen"); close (fd); return -1;
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
      send_response_error (ctx,
                           root,
                           ERR_PLANET_NOT_FOUND,
                           "Database connection error"); return;
    }
  g_ctx_for_send = ctx;
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
  if (session_token)
    {
      int pid = 0;
      long long exp = 0;
      int rc = db_session_lookup (session_token, &pid, &exp);


      if (rc == 0 && pid > 0)
        {
          ctx->player_id = pid;
          ctx->corp_id = h_get_player_corp_id (db, pid);
          ctx->ship_id = h_get_active_ship_id (db, pid);
          int db_sector = h_get_player_sector (db, pid);


          if (db_sector > 0)
            {
              ctx->sector_id = db_sector;
            }
          if (ctx->sector_id <= 0)
            {
              ctx->sector_id = 1;
            }
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
                           ERR_INVALID_SCHEMA,
                           "Invalid request schema");
      return;
    }
  if (evt && json_is_string (evt))
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_SCHEMA,
                           "Invalid request schema");
      return;
    }
  ctx->rl_limit = 60;
  ctx->rl_window_sec = 60;
  ctx->rl_window_start = time (NULL);
  ctx->rl_count = 0;

  int responses_before = ctx->responses_sent;
  if (server_dispatch_command (ctx, root) == -1)
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA, "Unknown command");
    }

  if (ctx->responses_sent == responses_before) {
      send_response_error(ctx, root, 500, "Handler produced no response");
  }
}


static void *
connection_thread (void *arg)
{
  client_ctx_t *ctx = (client_ctx_t *) arg;
  int fd = ctx->fd;
  struct timeval tv = {.tv_sec = 1,.tv_usec = 0 };
  setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv));
  char buf[BUF_SIZE];
  size_t have = 0;


  for (;;)
    {
      if (!*ctx->running)
        {
          break;
        }
      ssize_t n = recv (fd, buf + have, sizeof (buf) - have, 0);


      if (n > 0)
        {
          have += (size_t) n;
          size_t start = 0;


          for (size_t i = 0; i < have; ++i)
            {
              if (buf[i] == '\n')
                {
                  size_t linelen = i - start;
                  const char *line = buf + start;


                  while (linelen && line[linelen - 1] == '\r')
                    {
                      linelen--;
                    }
                  json_error_t jerr;
                  json_t *root = json_loadb (line, linelen, 0, &jerr);


                  if (!root || !json_is_object (root))
                    {
                      send_response_error (ctx,
                                           NULL,
                                           ERR_SERVER_ERROR,
                                           "Protocol Error: Malformed JSON");
                      if (root)
                        {
                          json_decref (root);
                        }
                    }
                  else
                    {
                      process_message (ctx, root);
                      json_decref (root);
                    }
                  start = i + 1;
                }
            }
          if (start > 0)
            {
              memmove (buf, buf + start, have - start);
              have -= start;
            }
          if (have == sizeof (buf))
            {
              send_error_json (fd, 1300, "invalid request schema");
              have = 0;
            }
        }
      else if (n == 0)
        {
          break;
        }
      else
        {
          if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
              continue;
            }
          break;
        }
    }
  db_close_thread ();
  close (fd);
  server_unregister_client (ctx);
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
  int listen_fd = make_listen_socket (g_cfg.server_port);


  if (listen_fd < 0)
    {
      LOGE ("Server loop exiting due to listen socket error.\n"); return -1;
    }
  LOGI ("Listening on 0.0.0.0:%d\n", g_cfg.server_port);
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
        uint64_t now_ms = monotonic_millis ();


        if (now_ms - last_broadcast_ms >= 500)
          {
            (void) broadcast_sweep_once (game_db_get_handle (), 64);
            last_broadcast_ms = now_ms;
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
              LOGE ("malloc failed\n"); continue;
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
          server_register_client (ctx);
          ctx->fd = cfd;
          ctx->running = running;
          char ip[INET_ADDRSTRLEN];


          inet_ntop (AF_INET, &ctx->peer.sin_addr, ip, sizeof (ip));
          LOGI ("Client connected: %s:%u (fd=%d)\n", ip,
                (unsigned) ntohs (ctx->peer.sin_port), cfd);
          pthread_t th;
          int prc = pthread_create (&th, &attr, connection_thread, ctx);


          if (prc == 0)
            {
              LOGI ("[cid=%" PRIu64 "] thread created (pthread=%lu)\n",
                    ctx->cid,
                    (unsigned long) th);
            }
          else
            {
              LOGE ("pthread_create: %s\n", strerror (prc));
              server_unregister_client (ctx);
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


int cmd_insurance_policies_list(client_ctx_t *ctx, json_t *root) { return 0; }
int cmd_insurance_policies_buy(client_ctx_t *ctx, json_t *root) { return 0; }
int cmd_insurance_claim_file(client_ctx_t *ctx, json_t *root) { return 0; }
int cmd_combat_attack_planet(client_ctx_t *ctx, json_t *root) { return 0; }
int cmd_mines_recall(client_ctx_t *ctx, json_t *root) { return 0; }
int cmd_dock_status(client_ctx_t *ctx, json_t *root) { return 0; }
int cmd_player_list_online(client_ctx_t *ctx, json_t *root) { return 0; }
int cmd_player_rankings(client_ctx_t *ctx, json_t *root) { return 0; }
int cmd_trade_port_info(client_ctx_t *ctx, json_t *root) { return 0; }
int cmd_trade_buy(client_ctx_t *ctx, json_t *root) { return 0; }
int cmd_trade_history(client_ctx_t *ctx, json_t *root) { return 0; }
int cmd_trade_sell(client_ctx_t *ctx, json_t *root) { return 0; }
