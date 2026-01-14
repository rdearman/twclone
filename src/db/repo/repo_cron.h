#ifndef REPO_CRON_H
#define REPO_CRON_H

#include <jansson.h>
#include <stdbool.h>
#include "db/db_api.h"

int db_cron_get_ship_info (db_t *db, int ship_id, int *sector_id, int *owner_id);
int db_cron_tow_ship (db_t *db, int ship_id, int new_sector_id, int owner_id);
int db_cron_get_max_sector_id (db_t *db, int *max_id);
int db_cron_get_sector_warps (db_t *db, int sector_id, int **out_neighbors, int *out_count);
int db_cron_msl_count (db_t *db, int *count);
int db_cron_create_msl_table (db_t *db);
int db_cron_get_stardock_locations (db_t *db, int **out_sectors, int *out_count);
int db_cron_msl_insert (db_t *db, int sector_id, int *added_count_ptr);
int db_cron_reset_turns_for_all_players (db_t *db, int max_turns, int64_t now_s, int *updated_count);
int db_cron_try_lock (db_t *db, const char *name, int64_t now_s);
int64_t db_cron_get_lock_until (db_t *db, const char *name);
int db_cron_unlock (db_t *db, const char *name);
int db_cron_uncloak_fedspace_ships (db_t *db);
int db_cron_get_illegal_assets_json (db_t *db, json_t **out_array);
int db_cron_delete_sector_asset (db_t *db, int player_id, int asset_type, int sector_id, int quantity);
int db_cron_logout_inactive_players (db_t *db, int64_t cutoff_s);
int db_cron_init_eligible_tows (db_t *db, int start_sec, int end_sec, int64_t stale_cutoff);
int db_cron_get_eligible_tows_json (db_t *db, const char *reason, int limit, json_t **out_array);
int db_cron_clear_eligible_tows (db_t *db);
int db_cron_robbery_decay_suspicion (db_t *db);
int db_cron_robbery_clear_busts (db_t *db, int64_t now_s);
int db_cron_reset_daily_turns (db_t *db, int turns);
int db_cron_autouncloak_ships (db_t *db, int64_t uncloak_threshold_s);
int db_cron_terra_replenish (db_t *db);
int db_cron_planet_pop_growth_tick (db_t *db, double growth_rate);
int db_cron_citadel_treasury_tick (db_t *db, int rate_bps);
int db_cron_planet_update_production_stock (db_t *db, int64_t now_s);
int db_cron_planet_get_market_data_json (db_t *db, json_t **out_array);
int db_cron_broadcast_cleanup (db_t *db, int64_t now_s);
int db_cron_traps_process (db_t *db, int64_t now_s);
int db_cron_port_get_economy_data_json (db_t *db, json_t **out_array);
int db_cron_get_all_commodities_json (db_t *db, json_t **out_array);
int db_cron_expire_market_orders (db_t *db, int64_t now_s);
int db_cron_news_get_events_json (db_t *db, int64_t start_s, int64_t end_s, json_t **out_array);
int db_cron_get_corp_details_json (db_t *db, int corp_id, json_t **out_json);
int db_cron_get_stock_details_json (db_t *db, int corp_id, int stock_id, json_t **out_json);

/* New additions for missing functions */
int db_cron_cleanup_old_news (db_t *db, int64_t cutoff_s);
int db_cron_lottery_check_processed (db_t *db, const char *date_str);
int db_cron_lottery_get_yesterday (db_t *db, const char *date_str, long long *carried_over);
int db_cron_lottery_get_stats (db_t *db, const char *date_str, long long *total_pot);
int db_cron_lottery_get_winners_json (db_t *db, const char *date_str, int winning_number, json_t **out_array);
int db_cron_lottery_update_state (db_t *db, const char *date_str, int winning_number, long long jackpot, long long carried_over);
int db_cron_deadpool_expire_bets (db_t *db, int64_t now_s);
int db_cron_deadpool_get_events_json (db_t *db, json_t **out_array);
int db_cron_deadpool_get_bets_json (db_t *db, int target_id, json_t **out_array);
int db_cron_deadpool_update_bet (db_t *db, int bet_id, const char *result, int64_t resolved_at);
int db_cron_deadpool_update_lost_bets (db_t *db, int target_id, int64_t resolved_at);
int db_cron_tavern_cleanup (db_t *db, int64_t now_s);
int db_cron_get_loans_json (db_t *db, json_t **out_array);
int db_cron_get_stocks_json (db_t *db, json_t **out_array);
int db_cron_get_corp_planet_assets (db_t *db, int corp_id, long long *net_value);
int db_cron_update_stock_price (db_t *db, int stock_id, long long new_price);
int db_cron_shield_regen (db_t *db, int percent);

#endif