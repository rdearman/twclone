/* src/server_cron.h */
#ifndef SERVER_CRON_H
#define SERVER_CRON_H
#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>
#include "db/db_api.h"

int h_traps_process (db_t *db, int64_t now_s);
int h_npc_step (db_t *db, int64_t now_s);
int h_autouncloak_sweeper (db_t *db, int64_t now_s);
int h_fedspace_cleanup (db_t *db, int64_t now_s);
int h_broadcast_ttl_cleanup (db_t *db, int64_t now_s);
int h_planet_growth (db_t *db, int64_t now_s);
int h_daily_turn_reset (db_t *db, int64_t now_s);
int h_terra_replenish (db_t *db, int64_t now_s);
int h_daily_market_settlement (db_t *db, int64_t now_s);
int h_shield_regen_tick (db_t *db, int64_t now_s);
int h_daily_news_compiler (db_t *db, int64_t now_s);
int h_cleanup_old_news (db_t *db, int64_t now_s);
int h_daily_lottery_draw (db_t *db, int64_t now_s);
int h_deadpool_resolution_cron (db_t *db, int64_t now_s);
int h_tavern_notice_expiry_cron (db_t *db, int64_t now_s);
int h_loan_shark_interest_cron (db_t *db, int64_t now_s);
int h_daily_stock_price_recalculation (db_t *db, int64_t now_s);
int h_port_economy_tick (db_t *db, int64_t now_s);
int h_planet_market_tick (db_t *db, int64_t now_s);
int h_reset_turns_for_player (db_t *db, int64_t now_s);

int cron_register_builtins (void);

int try_lock (db_t *db, const char *name, int64_t now_s);
void unlock (db_t *db, const char *name);
int begin (db_t *db);
int commit (db_t *db);
int rollback (db_t *db);

#endif
