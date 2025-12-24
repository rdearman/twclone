/* src/server_cron.c */
#include <unistd.h>
#include <jansson.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>

/* local includes */
#include "server_cron.h"
#include "server_log.h"
#include "common.h"
#include "server_players.h"
#include "server_universe.h"
#include "server_ports.h"
#include "server_planets.h"
#include "database.h"
#include "game_db.h"
#include "server_config.h"
#include "database_market.h"
#include "database_cmd.h"
#include "server_stardock.h"
#include "server_corporation.h"
#include "server_clusters.h"
#include "db/db_api.h"

int h_traps_process (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_npc_step (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_autouncloak_sweeper (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_fedspace_cleanup (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_broadcast_ttl_cleanup (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_planet_growth (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_daily_turn_reset (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_terra_replenish (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_daily_market_settlement (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_shield_regen_tick (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_daily_news_compiler (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_cleanup_old_news (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_daily_lottery_draw (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_deadpool_resolution_cron (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_tavern_notice_expiry_cron (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_loan_shark_interest_cron (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_daily_stock_price_recalculation (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_port_economy_tick (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_planet_market_tick (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }
int h_reset_turns_for_player (db_t *db, int64_t now_s) { (void)db; (void)now_s; return 0; }

int cron_register_builtins (void) { return 0; }

int try_lock (db_t *db, const char *name, int64_t now_s) { (void)db; (void)name; (void)now_s; return 1; }
void unlock (db_t *db, const char *name) { (void)db; (void)name; }
int begin (db_t *db) { db_error_t err; return db_tx_begin(db, DB_TX_IMMEDIATE, &err) ? 0 : -1; }
int commit (db_t *db) { db_error_t err; return db_tx_commit(db, &err) ? 0 : -1; }
int rollback (db_t *db) { return db_tx_rollback(db, NULL) ? 0 : -1; }