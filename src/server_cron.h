#ifndef SERVER_CRON_H
#define SERVER_CRON_H
#include <sqlite3.h>
#include <stdint.h>
#include <jansson.h>

typedef int (*cron_handler_fn) (sqlite3 * db, int64_t now_s);

/* Lookup by task name (e.g., "fedspace_cleanup"). */
cron_handler_fn cron_find (const char *name);

/* Register all built-in cron handlers. Call once at startup. */
void cron_register_builtins (void);
// server_cron.h

/* --- Public Declarations for Cron Handlers --- */
int h_traps_process (sqlite3 * db, int64_t now_s);
int h_npc_step (sqlite3 * db, int64_t now_s);
int h_autouncloak_sweeper (sqlite3 * db, int64_t now_s);
int h_fedspace_cleanup (sqlite3 * db, int64_t now_s);
int h_broadcast_ttl_cleanup (sqlite3 * db, int64_t now_s);
int h_planet_growth (sqlite3 * db, int64_t now_s);
int h_daily_turn_reset (sqlite3 * db, int64_t now_s);
int h_reset_turns_for_player (sqlite3 * db, int64_t now_s);
int h_terra_replenish (sqlite3 * db, int64_t now_s);
#define NEWS_EXPIRATION_SECONDS 604800L // 7 days in seconds

/* --- Public Declarations for Cron Handlers --- */
int h_traps_process (sqlite3 * db, int64_t now_s);
int h_npc_step (sqlite3 * db, int64_t now_s);
int h_autouncloak_sweeper (sqlite3 * db, int64_t now_s);
int h_fedspace_cleanup (sqlite3 * db, int64_t now_s);
int h_broadcast_ttl_cleanup (sqlite3 * db, int64_t now_s);
int h_planet_growth (sqlite3 * db, int64_t now_s);
int h_daily_turn_reset (sqlite3 * db, int64_t now_s);
int h_terra_replenish (sqlite3 * db, int64_t now_s);
int h_daily_market_settlement(sqlite3 *db, int64_t now_s);
int db_log_engine_event (long long ts, const char *type, const char *actor_owner_type, int actor_id, int sector_id, json_t *payload, const char *idem_key);
int h_daily_news_compiler(sqlite3 *db, int64_t now_s);
int h_cleanup_old_news(sqlite3 *db, int64_t now_s);

#endif /* SERVER_CRON_H */
