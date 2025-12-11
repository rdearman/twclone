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
#include <jansson.h>            /* -ljansson */
#include <stdbool.h>
#include <sqlite3.h>
/* local includes */
#include "database.h"
#include "schemas.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_ships.h"
#include "common.h"
#include "server_envelope.h"
#include "server_players.h"
#include "server_ports.h"       // at top of server_loop.c
#include "server_auth.h"
#include "server_s2s.h"
#include "server_universe.h"
#include "server_autopilot.h"
#include "server_config.h"
#include "server_communication.h"
#include "server_players.h"
#include "server_planets.h"
#include "server_citadel.h"
#include "server_combat.h"
#include "server_bulk.h"
#include "server_news.h"
#include "server_log.h"
#include "server_stardock.h"    // Include for hardware commands
#include "server_corporation.h"
#include "server_bank.h"        // Added missing include

#ifndef streq
#define streq(a,b) (strcasecmp (json_string_value ((a)), (b)) == 0)
#endif
#define BUF_SIZE    8192
#define RULE_REFUSE(_code,_msg,_hint_json) \
        do { send_enveloped_refused (ctx->fd, \
                                     root, \
                                     (_code), \
                                     (_msg), \
                                     (_hint_json)); goto trade_buy_done; \
          } while (0)
#define RULE_ERROR(_code,_msg) \
        do { send_enveloped_error (ctx->fd, root, (_code), (_msg)); \
             goto trade_buy_done; } while (0)

static __thread client_ctx_t *g_ctx_for_send = NULL;

/* forward declaration to avoid implicit extern */
void send_all_json (int fd, json_t *obj);
int db_player_info_json (int player_id, json_t **out);
/* rate-limit helper prototypes (defined later) */
void attach_rate_limit_meta (json_t *env, client_ctx_t *ctx);
void rl_tick (client_ctx_t *ctx);
int db_sector_basic_json (int sector_id, json_t **out_obj);
int db_adjacent_sectors_json (int sector_id, json_t **out_array);
int db_ports_at_sector_json (int sector_id, json_t **out_array);
int db_sector_scan_core (int sector_id, json_t **out_obj);
int db_players_at_sector_json (int sector_id, json_t **out_array);
int db_beacons_at_sector_json (int sector_id, json_t **out_array);
int db_planets_at_sector_json (int sector_id, json_t **out_array);
int db_player_set_sector (int player_id, int sector_id);
int db_player_get_sector (int player_id, int *out_sector);
void handle_sector_info (int fd, json_t *root, int sector_id, int player_id);
void handle_sector_set_beacon (client_ctx_t *ctx, json_t *root);
/* Fast sector scan handler (IDs+counts only) */
void handle_move_scan (client_ctx_t *ctx, json_t *root);
void handle_move_pathfind (client_ctx_t *ctx, json_t *root);
void send_enveloped_ok (int fd, json_t *root, const char *type,
                        json_t *data);
json_t *build_sector_info_json (int sector_id);

/* Missing declarations for commands found in .c files but not headers */
extern int cmd_bounty_list (client_ctx_t *ctx, json_t *root);
extern int cmd_bounty_post_federation (client_ctx_t *ctx, json_t *root);
extern int cmd_bounty_post_hitlist (client_ctx_t *ctx, json_t *root);
extern int cmd_player_set_trade_account_preference (client_ctx_t *ctx, json_t *root);
extern int cmd_move_transwarp (client_ctx_t *ctx, json_t *root);

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

typedef struct {
    const char *name;
    command_handler_fn handler;
    const char *summary;
    int flags;
} command_entry_t;

/* Wrappers for void or mismatched handlers */
static int w_nav_avoid_add(client_ctx_t *ctx, json_t *root) { cmd_nav_avoid_add(ctx, root); return 0; }
static int w_nav_avoid_remove(client_ctx_t *ctx, json_t *root) { cmd_nav_avoid_remove(ctx, root); return 0; }
static int w_nav_avoid_list(client_ctx_t *ctx, json_t *root) { cmd_nav_avoid_list(ctx, root); return 0; }
static int w_sector_scan(client_ctx_t *ctx, json_t *root) { cmd_sector_scan(ctx, root); return 0; }
static int w_sector_scan_density(client_ctx_t *ctx, json_t *root) { cmd_sector_scan_density((void*)ctx, root); return 0; }

/* Special logic for auth.login to push notices */
static int w_auth_login(client_ctx_t *ctx, json_t *root) {
    int rc = cmd_auth_login(ctx, root);
    if (rc) {
        /* Matches original process_message logic:
           if (rc) { push_unseen_notices_for_player (ctx, ctx->player_id); }
           (Logic seems questionable if rc=0 is success, but preserving as-is) */
        push_unseen_notices_for_player (ctx, ctx->player_id);
    }
    return rc;
}

static const command_entry_t k_command_registry[] = {
    /* Admin */
    {"admin.notice", cmd_admin_notice, "Admin notice"},
    {"admin.shutdown_warning", cmd_admin_shutdown_warning, "Admin shutdown warning"},
    
    /* Auth */
    {"auth.login", w_auth_login, "Authenticate"},
    {"login", w_auth_login, "Authenticate"},
    {"auth.logout", cmd_auth_logout, "Log out"},
    {"auth.mfa.totp.verify", cmd_auth_mfa_totp_verify, "Second-factor code"},
    {"auth.register", cmd_auth_register, "Create a new player"},
    {"auth.refresh", cmd_auth_refresh, "Refresh session token"},

    /* Bank */
    {"bank.balance", cmd_bank_balance, "Get player bank balance"},
    {"bank.deposit", cmd_bank_deposit, "Deposit credits to bank"},
    {"bank.history", cmd_bank_history, "Get bank history"},
    {"bank.leaderboard", cmd_bank_leaderboard, "Get bank leaderboard"},
    {"bank.transfer", cmd_bank_transfer, "Transfer credits between players"},
    {"bank.withdraw", cmd_bank_withdraw, "Withdraw credits from bank"},

    /* Bounty */
    {"bounty.list", cmd_bounty_list, "List bounties"},
    {"bounty.post_federation", cmd_bounty_post_federation, "Post a Federation bounty"},
    {"bounty.post_hitlist", cmd_bounty_post_hitlist, "Post a Hit List contract"},

    /* Bulk */
    {"bulk.execute", cmd_bulk_execute, "Execute a bulk command"},

    /* Chat */
    {"chat.broadcast", cmd_chat_broadcast, "Broadcast a chat message"},
    {"chat.history", cmd_chat_history, "Chat history"},
    {"chat.send", cmd_chat_send, "Send a chat message"},

    /* Citadel */
    {"citadel.build", cmd_citadel_build, "Build a citadel"},
    {"citadel.upgrade", cmd_citadel_upgrade, "Upgrade a citadel"},

    /* Combat */
    {"combat.attack", cmd_combat_attack, "Attack a target"},
    {"combat.attack_planet", cmd_combat_attack_planet, "Attack a planet"},
    {"combat.deploy_fighters", cmd_combat_deploy_fighters, "Deploy fighters"},
    {"combat.deploy_mines", cmd_combat_deploy_mines, "Deploy mines"},
    {"combat.lay_mines", cmd_combat_lay_mines, "Lay mines"},
    {"combat.status", cmd_combat_status, "Combat status"},
    {"combat.sweep_mines", cmd_combat_sweep_mines, "Sweep for mines"},
    {"deploy.fighters.list", cmd_deploy_fighters_list, "List deployed fighters"},
    {"deploy.mines.list", cmd_deploy_mines_list, "List deployed mines"},
    {"fighters.recall", cmd_fighters_recall, "Recall deployed fighters"},
    {"mines.recall", cmd_mines_recall, "Recall deployed mines"},

    /* Corporation */
    {"corp.balance", cmd_corp_balance, "Get corporation balance"},
    {"corp.create", cmd_corp_create, "Create a new corporation"},
    {"corp.deposit", cmd_corp_deposit, "Deposit to corporation"},
    {"corp.dissolve", cmd_corp_dissolve, "Dissolve a corporation"},
    {"corp.invite", cmd_corp_invite, "Invite player to corporation"},
    {"corp.join", cmd_corp_join, "Join a corporation via invite"},
    {"corp.kick", cmd_corp_kick, "Kick member from corporation"},
    {"corp.leave", cmd_corp_leave, "Leave current corporation"},
    {"corp.list", cmd_corp_list, "List all corporations"},
    {"corp.roster", cmd_corp_roster, "List corporation members"},
    {"corp.statement", cmd_corp_statement, "Get corporation statement"},
    {"corp.status", cmd_corp_status, "Get corporation status"},
    {"corp.transfer_ceo", cmd_corp_transfer_ceo, "Transfer corporation CEO role"},
    {"corp.withdraw", cmd_corp_withdraw, "Withdraw from corporation"},

    /* Dock */
    {"dock.status", cmd_dock_status, "Check/set player docked status at current port"},

    /* Hardware */
    {"hardware.buy", cmd_hardware_buy, "Buy ship hardware"},
    {"hardware.list", cmd_hardware_list, "List available ship hardware"},

    /* Mail */
    {"mail.delete", cmd_mail_delete, "Delete mail"},
    {"mail.inbox", cmd_mail_inbox, "Mail inbox"},
    {"mail.read", cmd_mail_read, "Read mail"},
    {"mail.send", cmd_mail_send, "Send mail"},

    /* Move */
    {"move.autopilot.start", cmd_move_autopilot_start, "Start autopilot"},
    {"move.autopilot.status", cmd_move_autopilot_status, "Autopilot status"},
    {"move.autopilot.stop", cmd_move_autopilot_stop, "Stop autopilot"},
    {"move.describe_sector", cmd_move_describe_sector, "Describe a sector"},
    {"move.pathfind", cmd_move_pathfind, "Find path between sectors"},
    {"move.scan", cmd_move_scan, "Scan adjacent sectors"},
    {"move.transwarp", cmd_move_transwarp, "Transwarp to a sector"},
    {"move.warp", cmd_move_warp, "Warp to sector"},

    /* Nav / Bookmarks / Avoids */
    {"nav.avoid.add", w_nav_avoid_add, "Add a sector to the avoid list"},
    {"nav.avoid.list", w_nav_avoid_list, "List avoided sectors"},
    {"nav.avoid.remove", w_nav_avoid_remove, "Remove a sector from the avoid list"},
    {"nav.avoid.set", cmd_player_set_avoids, "Set a sector to the avoid list"},
    {"nav.bookmark.add", cmd_player_set_bookmarks, "Add a bookmark"},
    {"nav.bookmark.list", cmd_player_get_bookmarks, "List bookmarks"},
    {"nav.bookmark.remove", cmd_player_set_bookmarks, "Remove a bookmark"},
    {"nav.bookmark.set", cmd_player_set_bookmarks, "Set a bookmark"},

    /* News */
    {"news.get_feed", cmd_news_get_feed, "Get the daily news feed"},
    {"news.mark_feed_read", cmd_news_mark_feed_read, "Mark news feed as read"},
    {"news.read", cmd_news_get_feed, "Get the daily news feed"},

    /* Notes */
    {"notes.list", cmd_player_get_notes, "List notes"},

    /* Notice */
    {"notice.ack", cmd_notice_ack, "Acknowledge a notice"},
    {"notice.list", cmd_notice_list, "List notices"},

    /* Planet */
    {"planet.create", cmd_planet_genesis, "Create a planet"},
    {"planet.deposit", cmd_planet_deposit, "Deposit to a planet"},
    {"planet.genesis", cmd_planet_genesis, "Create a planet"},
    {"planet.genesis_create", cmd_planet_genesis_create, "Create a genesis planet"},
    {"planet.harvest", cmd_planet_harvest, "Harvest from a planet"},
    {"planet.info", cmd_planet_info, "Planet information"},
    {"planet.land", cmd_planet_land, "Land on a planet"},
    {"planet.launch", cmd_planet_launch, "Launch from a planet"},
    {"planet.market.buy_order", cmd_planet_market_buy_order, "Create planet market buy order"},
    {"planet.market.sell", cmd_planet_market_sell, "Sell to planet market"},
    {"planet.rename", cmd_planet_rename, "Rename a planet"},
    {"planet.transfer_ownership", cmd_planet_transfer_ownership, "Transfer planet ownership"},
    {"planet.withdraw", cmd_planet_withdraw, "Withdraw from a planet"},

    /* Player */
    {"player.get_avoids", cmd_player_get_avoids, "Get player avoids"},
    {"player.get_bookmarks", cmd_player_get_bookmarks, "Get player bookmarks"},
    {"player.get_notes", cmd_player_get_notes, "Get player notes"},
    {"player.get_prefs", cmd_player_get_prefs, "Get player preferences"},
    {"player.get_settings", cmd_player_get_settings, "Get player settings"},
    {"player.get_subscriptions", cmd_player_get_topics, "Get player subscriptions"},
    {"player.get_topics", cmd_player_get_topics, "Get player topics"},
    {"player.list_online", cmd_player_list_online, "List online players"},
    {"player.my_info", cmd_player_my_info, "Current player info"},
    {"player.rankings", cmd_player_rankings, "Player rankings"},
    {"player.set_avoids", cmd_player_set_avoids, "Set player avoids"},
    {"player.set_bookmarks", cmd_player_set_bookmarks, "Set player bookmarks"},
    {"player.set_prefs", cmd_player_set_prefs, "Set player preferences"},
    {"player.set_settings", cmd_player_set_settings, "Set player settings"},
    {"player.set_subscriptions", cmd_player_set_topics, "Set player subscriptions"},
    {"player.set_topics", cmd_player_set_topics, "Set player topics"},
    {"player.set_trade_account_preference", cmd_player_set_trade_account_preference, "Set trade account preference"},

    /* Port */
    {"port.describe", cmd_trade_port_info, "Describe a port"},
    {"port.info", cmd_trade_port_info, "Port prices/stock in sector"},
    {"port.rob", cmd_port_rob, "Attempt to rob a port"},
    {"port.status", cmd_trade_port_info, "Port status"},

    /* S2S */
    {"s2s.event.relay", cmd_s2s_event_relay, "S2S event relay"},
    {"s2s.planet.genesis", cmd_s2s_planet_genesis, "S2S planet genesis"},
    {"s2s.planet.transfer", cmd_s2s_planet_transfer, "S2S planet transfer"},
    {"s2s.player.migrate", cmd_s2s_player_migrate, "S2S player migrate"},
    {"s2s.port.restock", cmd_s2s_port_restock, "S2S port restock"},
    {"s2s.replication.heartbeat", cmd_s2s_replication_heartbeat, "S2S replication heartbeat"},

    /* Sector */
    {"sector.info", cmd_move_describe_sector, "Describe current sector"},
    {"sector.scan", w_sector_scan, "Scan a sector"},
    {"sector.scan.density", w_sector_scan_density, "Scan sector density"},
    {"sector.search", cmd_sector_search, "Search a sector"},
    {"sector.set_beacon", cmd_sector_set_beacon, "Set or clear sector beacon"},

    /* Session / System */
    {"session.disconnect", cmd_session_disconnect, "Disconnect"},
    {"session.hello", cmd_system_hello, "Handshake / hello"},
    {"session.ping", cmd_system_hello, "Ping"},
    {"player.ping", cmd_system_hello, "Ping"}, // Alias
    {"sys.cluster.init", cmd_sys_cluster_init, "Cluster init", CMD_FLAG_DEBUG_ONLY},
    {"sys.cluster.seed_illegal_goods", cmd_sys_cluster_seed_illegal_goods, "Seed illegal goods", CMD_FLAG_DEBUG_ONLY},
    {"sys.econ.orders_summary", cmd_sys_econ_orders_summary, "Economy orders summary"},
    {"sys.econ.planet_status", cmd_sys_econ_planet_status, "Economy planet status"},
    {"sys.econ.port_status", cmd_sys_econ_port_status, "Economy port status"},
    {"sys.notice.create", cmd_sys_notice_create, "Sysop command to create a notice"},
    {"sys.npc.ferengi_tick_once", cmd_sys_npc_ferengi_tick_once, "Tick Ferengi"},
    {"sys.raw_sql_exec", cmd_sys_raw_sql_exec, "Sysop command to execute raw SQL", CMD_FLAG_DEBUG_ONLY},
    {"sys.test_news_cron", cmd_sys_test_news_cron, "Sysop command to test news cron"},
    {"system.capabilities", cmd_system_capabilities, "Feature flags, schemas, counts"},
    {"system.cmd_list", cmd_system_cmd_list, "Flat list of all commands"},
    {"system.describe_schema", cmd_system_describe_schema, "Describe commands in a schema"},
    {"system.disconnect", cmd_session_disconnect, "Disconnect"},
    {"system.hello", cmd_system_hello, "Handshake / hello"},
    {"system.schema_list", cmd_system_schema_list, "List all schema namespaces"},

    /* Ship */
    {"ship.claim", cmd_ship_claim, "Claim a ship"},
    {"ship.info", cmd_ship_info_compat, "Ship information"},
    {"ship.inspect", cmd_ship_inspect, "Inspect a ship"},
    {"ship.jettison", cmd_trade_jettison, "Jettison cargo"},
    {"ship.rename", cmd_ship_rename, "Rename a ship"},
    {"ship.repair", cmd_ship_repair, "Repair a ship"},
    {"ship.reregister", cmd_ship_rename, "Re-register a ship"},
    {"ship.self_destruct", cmd_ship_self_destruct, "Self-destruct a ship"},
    {"ship.status", cmd_ship_status, "Ship status"},
    {"ship.transfer_cargo", cmd_ship_transfer_cargo, "Transfer cargo"},
    {"ship.upgrade", cmd_ship_upgrade, "Upgrade a ship"},

    /* Shipyard */
    {"shipyard.list", cmd_shipyard_list, "List available ship hulls"},
    {"shipyard.upgrade", cmd_shipyard_upgrade, "Upgrade to a new ship hull"},

    /* Stock */
    {"stock.buy", cmd_stock, "Buy shares in a corporation"},
    {"stock.dividend.set", cmd_stock, "Declare a dividend"},
    {"stock.exchange.list_stocks", cmd_stock, "List stocks on the exchange"},
    {"stock.exchange.orders.cancel", cmd_stock, "Cancel a stock order"},
    {"stock.exchange.orders.create", cmd_stock, "Create a stock order"},
    {"stock.ipo.register", cmd_stock, "Register corporation for IPO"},
    {"stock.portfolio.list", cmd_stock, "List stock portfolio"},

    /* Subscribe */
    {"subscribe.add", cmd_subscribe_add, "Add a subscription"},
    {"subscribe.catalog", cmd_subscribe_catalog, "Subscription catalog"},
    {"subscribe.list", cmd_subscribe_list, "List subscriptions"},
    {"subscribe.remove", cmd_subscribe_remove, "Remove a subscription"},

    /* Tavern */
    {"tavern.barcharts.get_prices_summary", cmd_tavern_barcharts_get_prices_summary, "Get a summary of commodity prices"},
    {"tavern.deadpool.place_bet", cmd_tavern_deadpool_place_bet, "Place a bet on a player's destruction"},
    {"tavern.dice.play", cmd_tavern_dice_play, "Play bar dice"},
    {"tavern.graffiti.post", cmd_tavern_graffiti_post, "Post a message on the graffiti wall"},
    {"tavern.highstakes.play", cmd_tavern_highstakes_play, "Play at the high-stakes table"},
    {"tavern.loan.pay", cmd_tavern_loan_pay, "Repay a loan from the loan shark"},
    {"tavern.loan.take", cmd_tavern_loan_take, "Take a loan from the loan shark"},
    {"tavern.lottery.buy_ticket", cmd_tavern_lottery_buy_ticket, "Buy a lottery ticket"},
    {"tavern.lottery.status", cmd_tavern_lottery_status, "Check lottery status"},
    {"tavern.raffle.buy_ticket", cmd_tavern_raffle_buy_ticket, "Buy a raffle ticket"},
    {"tavern.round.buy", cmd_tavern_round_buy, "Buy a round for the tavern"},
    {"tavern.rumour.get_hint", cmd_tavern_rumour_get_hint, "Get a hint from the rumour mill"},
    {"tavern.trader.buy_password", cmd_tavern_trader_buy_password, "Buy an underground password from the grimy trader"},

    /* Trade */
    {"trade.accept", cmd_trade_accept, "Accept a private trade offer"},
    {"trade.buy", cmd_trade_buy, "Buy commodity from port"},
    {"trade.cancel", cmd_trade_cancel, "Cancel a pending trade offer"},
    {"trade.history", cmd_trade_history, "View recent trade transactions"},
    {"trade.jettison", cmd_trade_jettison, "Dump cargo into space"},
    {"trade.offer", cmd_trade_offer, "Create a private player-to-player trade offer"},
    {"trade.port_info", cmd_trade_port_info, "Port prices/stock in sector"},
    {"trade.quote", cmd_trade_quote, "Get a quote for a trade action"},
    {"trade.sell", cmd_trade_sell, "Sell commodity to port"},

    {NULL, NULL, NULL} /* Sentinel */
};

/* Provide authoritative list to others */
void
loop_get_supported_commands (const cmd_desc_t **out_tbl, size_t *out_n)
{
    static cmd_desc_t *s_descs = NULL;
    static size_t s_count = 0;
    
    if (!s_descs) {
        /* Count entries */
        size_t n = 0;
        size_t count = 0;
        while (k_command_registry[n].name) {
#ifdef BUILD_PRODUCTION
            if (k_command_registry[n].flags & CMD_FLAG_DEBUG_ONLY) {
                n++;
                continue;
            }
#endif
            n++;
            count++;
        }
        
        /* Allocate static array once (or use a large static buffer if malloc disallowed) */
        /* Since we are in phase G0, we can use malloc/calloc once. */
        s_descs = calloc(count, sizeof(cmd_desc_t));
        if (s_descs) {
            size_t j = 0;
            size_t i = 0;
            while (k_command_registry[i].name) {
#ifdef BUILD_PRODUCTION
                if (k_command_registry[i].flags & CMD_FLAG_DEBUG_ONLY) {
                    i++;
                    continue;
                }
#endif
                s_descs[j].name = k_command_registry[i].name;
                s_descs[j].summary = k_command_registry[i].summary;
                j++;
                i++;
            }
            s_count = count;
        }
    }
    
    if (out_tbl) *out_tbl = s_descs;
    if (out_n) *out_n = s_count;
}

/* --------------------------------------------------------------------------
   Broadcast / Registry
   -------------------------------------------------------------------------- */

/* Broadcast a system.notice to everyone (uses subscription infra).
   Data is BORROWED here; we incref for the call. */
static void
server_broadcast_to_all_online (json_t *data)
{
  server_broadcast_event ("system.notice", json_incref (data));
  json_decref (data);
}


/* Sector-scoped, ephemeral event to subscribers of sector.* / sector.{id}.
   NOTE: comm_publish_sector_event STEALS a ref to 'data'. */


/*
   static void
   server_broadcast_to_sector (int sector_id, const char *event_type,
                            json_t *data)
   {
   comm_publish_sector_event (sector_id, event_type, data); // steals 'data'
   }
 */
static int
broadcast_sweep_once (sqlite3 *db, int max_rows)
{
  static const char *SQL_SEL =
    "SELECT id, created_at, title, body, severity, expires_at "
    "FROM system_notice "
    "WHERE id NOT IN (SELECT DISTINCT notice_id FROM notice_seen WHERE player_id=0) "
    "ORDER BY created_at ASC, id ASC LIMIT ?1;";
  sqlite3_stmt *st = NULL;
  int processed = 0;
  if (max_rows <= 0 || max_rows > 1000)
    {
      max_rows = 64;
    }
  if (sqlite3_prepare_v2 (db, SQL_SEL, -1, &st, NULL) != SQLITE_OK)
    {
      return 0;
    }
  sqlite3_bind_int (st, 1, max_rows);
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int id = sqlite3_column_int (st, 0);
      int created_at = sqlite3_column_int (st, 1);
      const char *title = (const char *) sqlite3_column_text (st, 2);
      const char *body = (const char *) sqlite3_column_text (st, 3);
      const char *severity = (const char *) sqlite3_column_text (st, 4);
      int expires_at =
        (sqlite3_column_type (st, 5) ==
         SQLITE_NULL) ? 0 : sqlite3_column_int (st, 5);
      json_t *data = json_pack ("{s:i, s:i, s:s, s:s, s:s, s:O}",
                                "id", id,
                                "ts", created_at,
                                "title", title ? title : "",
                                "body", body ? body : "",
                                "severity", severity ? severity : "info",
                                "expires_at",
                                expires_at ? json_integer (expires_at) :
                                json_null ());


      server_broadcast_to_all_online (json_incref (data));      /* fan-out live */
      json_decref (data);
      /* mark-as-published sentinel (player_id=0) to avoid re-sending */
      sqlite3_stmt *st2 = NULL;


      if (sqlite3_prepare_v2 (db,
                              "INSERT OR IGNORE INTO notice_seen(notice_id, player_id, seen_at) "
                              "VALUES(?1, 0, strftime('%s','now'));",
                              -1,
                              &st2,
                              NULL) == SQLITE_OK)
        {
          sqlite3_bind_int (st2, 1, id);
          (void) sqlite3_step (st2);
        }
      if (st2)
        {
          sqlite3_finalize (st2);
        }
      processed++;
    }
  sqlite3_finalize (st);
  return processed;
}


/* ===== Client registry for broadcasts (#195) ===== */
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


/* Deliver an envelope (type+data) to any online socket for player_id. */
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
          /* send_enveloped_ok does its own timestamp/meta/sanitize. */
          send_enveloped_ok (c->fd, NULL, event_type, json_incref (data));
          delivered++;
        }
    }
  pthread_mutex_unlock (&g_clients_mu);
  return (delivered > 0) ? 0 : -1;
}


/* ------------------------ idempotency helpers  ------------------------ */


/* FNV-1a 64-bit */
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


/* Canonicalize a JSON object (sorted keys, compact) then FNV-1a */
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
make_listen_socket (uint16_t port)
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
  sa.sin_addr.s_addr = htonl (INADDR_ANY);
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


/* Roll the window and increment count for this response */
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
    }                           /* safety */
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


/* Build {limit, remaining, reset} */
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
  return json_pack ("{s:i,s:i,s:i}", "limit", ctx->rl_limit, "remaining",
                    remaining, "reset", reset);
}


/* Ensure env.meta exists and add meta.rate_limit */
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


static void
process_message (client_ctx_t *ctx, json_t *root)
{
  LOGE("DEBUG: process_message entered for fd=%d, ctx->player_id=%d", ctx->fd, ctx->player_id); // NEW
  // db_close_thread ();                   /* Ensure a fresh DB connection */
  sqlite3 *db = db_get_handle ();       /* Re-open (or get) fresh DB conn */
  if (!db) {                            /* Handle case where we can't get a connection */
      send_enveloped_error (ctx->fd, root, 1500, "Database connection error");
      return;
  }

  /* Make ctx visible to send helpers for rate-limit meta */
  g_ctx_for_send = ctx;
  json_t *cmd = json_object_get (root, "command");
  json_t *evt = json_object_get (root, "event");
  /* Auto-auth from meta.session_token (transport-agnostic clients) */
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


      if (rc == SQLITE_OK && pid > 0)
        {
          ctx->player_id = pid;
          ctx->corp_id = h_get_player_corp_id (db_get_handle (), pid);
          if (ctx->sector_id <= 0)
            {
              ctx->sector_id = 1; /* or load from DB */
            }
        }
      /* If invalid/expired, we silently ignore; individual commands can refuse with 1401 */
    }
  if (ctx->player_id == 0 && ctx->sector_id <= 0)
    {
      ctx->sector_id = 1;
    }
  if (!(cmd && json_is_string (cmd)) && !(evt && json_is_string (evt)))
    {
      send_enveloped_error (ctx->fd, root, 1300, "Invalid request schema");
      return;
    }
  if (evt && json_is_string (evt))
    {
      send_enveloped_error (ctx->fd, root, 1300, "Invalid request schema");
      return;
    }
  /* Rate-limit defaults: 60 responses / 60 seconds */
  ctx->rl_limit = 60;
  ctx->rl_window_sec = 60;
  ctx->rl_window_start = time (NULL);
  ctx->rl_count = 0;
  const char *c = json_string_value (cmd);
  
  /* Dispatch via Registry */
  for (int i = 0; k_command_registry[i].name != NULL; i++) {
      if (strcasecmp(c, k_command_registry[i].name) == 0) {
#ifdef BUILD_PRODUCTION
          if (k_command_registry[i].flags & CMD_FLAG_DEBUG_ONLY) {
              break; /* Treat as unknown/disabled */
          }
#endif
          int rc = k_command_registry[i].handler(ctx, root);
          (void)rc; /* return value ignored in loop logic mostly, wrapper handles side-effects */
          return;
      }
  }

  send_enveloped_error (ctx->fd, root, 1400, "Unknown command");
}


/* src/server_loop.c */
static void *
connection_thread (void *arg)
{
  client_ctx_t *ctx = (client_ctx_t *) arg;
  int fd = ctx->fd;
  /* Per-thread initialisation (DB/session/etc.) goes here */
  /* Note: db_get_handle() called inside cmd_* functions handles init implicitly */
  /* Make recv interruptible via timeout so we can stop promptly */
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
          /* Process complete lines (newline-terminated frames) */
          size_t start = 0;


          for (size_t i = 0; i < have; ++i)
            {
              if (buf[i] == '\n')
                {
                  /* Trim optional CR */
                  size_t linelen = i - start;
                  const char *line = buf + start;


                  while (linelen && line[linelen - 1] == '\r')
                    {
                      linelen--;
                    }
                  /* Parse and dispatch */
                  //LOGI ("CORE DUMP DEBUG: Received from client: %.*s\n",
                  //    (int) linelen, line);
                  json_error_t jerr;
                  json_t *root = json_loadb (line, linelen, 0, &jerr);


                  if (!root || !json_is_object (root))
                    {
                      send_enveloped_error (fd, NULL, 1300,
                                            "Invalid request schema");
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
          /* Shift any partial line to front */
          if (start > 0)
            {
              memmove (buf, buf + start, have - start);
              have -= start;
            }
          /* Overflow without newline → guard & reset */
          if (have == sizeof (buf))
            {
              send_error_json (fd, 1300, "invalid request schema");
              have = 0;
            }
        }
      else if (n == 0)
        {
          /* peer closed */
          break;
        }
      else
        {
          if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            {
              continue;
            }
          /* hard error */
          break;
        }
    }
  /* Per-thread teardown */
  /* FIX: Explicitly close the thread-local database connection */
  db_close_thread ();
  close (fd);
  free (ctx);
  return NULL;
}


int
server_loop (volatile sig_atomic_t *running)
{
  LOGI ("Server loop starting...\n");
  //  LOGE( "Server loop starting...\n");
#ifdef SIGPIPE
  signal (SIGPIPE, SIG_IGN);    /* don’t die on write to closed socket */
#endif
  int listen_fd = make_listen_socket (g_cfg.server_port);


  if (listen_fd < 0)
    {
      LOGE ("Server loop exiting due to listen socket error.\n");
      //      LOGE( "Server loop exiting due to listen socket error.\n");
      return -1;
    }
  LOGI ("Listening on 0.0.0.0:%d\n", g_cfg.server_port);
  //  LOGE( "Listening on 0.0.0.0:%d\n", LISTEN_PORT);
  struct pollfd pfd = {.fd = listen_fd,.events = POLLIN,.revents = 0 };
  pthread_attr_t attr;


  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
  while (*running)
    {
      g_server_tick++;          /* Increment server tick */
      int rc = poll (&pfd, 1, 100);     /* was 1000; 100ms gives us a ~10Hz tick */
      // int rc = poll (&pfd, 1, 1000); /* 1s tick re-checks *running */
      /* === Broadcast pump tick (every ~500ms) === */
      {
        static uint64_t last_broadcast_ms = 0;
        uint64_t now_ms = monotonic_millis ();


        if (now_ms - last_broadcast_ms >= 500)
          {
            (void) broadcast_sweep_once (db_get_handle (), 64);
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
          /* timeout only: we still ran the pump above; just loop again */
          continue;
        }
      if (pfd.revents & POLLIN)
        {
          client_ctx_t *ctx = calloc (1, sizeof (*ctx));


          if (!ctx)
            {
              LOGE ("malloc failed\n");
              //              LOGE( "malloc failed\n");
              continue;
            }
          server_register_client (ctx);
          socklen_t sl = sizeof (ctx->peer);
          int cfd = accept (listen_fd, (struct sockaddr *) &ctx->peer, &sl);


          if (cfd < 0)
            {
              free (ctx);
              if (errno == EINTR)
                {
                  continue;
                }
              if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                  continue;
                }
              perror ("accept");
              continue;
            }
          ctx->fd = cfd;
          ctx->running = running;
          char ip[INET_ADDRSTRLEN];


          inet_ntop (AF_INET, &ctx->peer.sin_addr, ip, sizeof (ip));
          LOGI ("Client connected: %s:%u (fd=%d)\n",
                ip, (unsigned) ntohs (ctx->peer.sin_port), cfd);
          //      LOGE( "Client connected: %s:%u (fd=%d)\n",
          //       ip, (unsigned) ntohs (ctx->peer.sin_port), cfd);
          // after filling ctx->fd, ctx->running, ctx->peer, and assigning ctx->cid
          pthread_t th;
          int prc = pthread_create (&th, &attr, connection_thread, ctx);


          if (prc == 0)
            {
              LOGI ("[cid=%%" PRIu64 "] thread created (pthread=%%lu)\n",
                    ctx->cid, (unsigned long) th);
              //              LOGE(
              //       "[cid=%%" PRIu64 "] thread created (pthread=%%lu)\n",
              //       ctx->cid, (unsigned long) th);
            }
          else
            {
              LOGE ("pthread_create: %s\n", strerror (prc));
              //              LOGE( "pthread_create: %s\n", strerror (prc));
              close (cfd);
              free (ctx);
            }
        }
    }
  // server_unregister_client(ctx);
  pthread_attr_destroy (&attr);
  close (listen_fd);
  LOGI ("Server loop exiting...\n");
  //  LOGE( "Server loop exiting...\n");
  return 0;
}
