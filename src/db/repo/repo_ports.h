#ifndef REPO_PORTS_H
#define REPO_PORTS_H

#include <jansson.h>
#include <stdbool.h>
#include <time.h>
#include "db/db_api.h"

/* Q1: Update Entity Stock */
int db_ports_upsert_stock(db_t *db, const char *entity_type, int entity_id, const char *commodity_code, int quantity, int64_t ts);

/* Q2: Port Buy Eligibility Check */
int db_ports_get_buy_eligibility(db_t *db, int port_id, const char *commodity_code, int *quantity, int *max_capacity);

/* Q3: Get Entity Stock Quantity */
int db_ports_get_stock_quantity(db_t *db, const char *entity_type, int entity_id, const char *commodity_code, int *quantity);

/* Q4: Canonical Commodity Code Lookup */
int db_ports_get_commodity_code(db_t *db, const char *commodity, char **code);

/* Q5: Port Price Calculation Info (Sell) */
int db_ports_get_price_info(db_t *db, int port_id, const char *commodity_code, int *base_price, int *quantity, int *max_capacity, int *techlevel, double *elasticity, double *volatility_factor);

/* Q6: Illegal Commodity Check */
int db_ports_get_illegal_status(db_t *db, const char *commodity_code, bool *illegal);

/* Q7: Port Sector Lookup */
int db_ports_get_port_sector(db_t *db, int port_id, int *sector_id);

/* Q8: Market Move Info */
int db_ports_get_market_move_info(db_t *db, int port_id, const char *commodity_code, int *current_quantity, int *max_capacity);

/* Q9: Trade History (No Cursor) */
int db_ports_get_trade_history(db_t *db, int player_id, int limit, json_t **history);

/* Q10: Trade History (With Cursor) */
int db_ports_get_trade_history_cursor(db_t *db, int player_id, int limit, int64_t ts, int64_t id, json_t **history);

/* Q11: Update Ported Status */
int db_ports_set_ported_status(db_t *db, int ship_id, int port_id);

/* Q12: Check Ported Status */
int db_ports_get_ported_status(db_t *db, int ship_id, int *port_id);

/* Q13: (Dup of Q5) */

/* Q14: Port Header Info (By ID) */
int db_ports_get_header_by_id(db_t *db, int port_id, json_t **port_obj, int *port_size);

/* Q15: Port Header Info (By Sector) */
int db_ports_get_header_by_sector(db_t *db, int sector_id, json_t **port_obj, int *port_size);

/* Q16: Port Commodities from entity_stock */
int db_ports_get_commodities(db_t *db, int port_id, json_t **commodities_array);

/* Q17: Robbery Config */
int db_ports_get_robbery_config(db_t *db, int *threshold, int *xp_per_hold, int *cred_per_xp, double *chance_base, int *turn_cost, double *good_bonus, double *pro_delta, double *evil_cluster_bonus, double *good_penalty_mult, int *ttl_days);

/* Q18: Active Bust Check */
int db_ports_check_active_bust(db_t *db, int port_id, int player_id, bool *active);

/* Q19: Sector Cluster Lookup */
int db_ports_get_cluster_id(db_t *db, int sector_id, int *cluster_id);

/* Q20: Cluster Ban Check */
int db_ports_check_cluster_ban(db_t *db, int cluster_id, int player_id, bool *banned);

/* Q21: Player Experience Lookup */
int db_ports_get_player_xp(db_t *db, int player_id, int *xp);

/* Q22: Last Robbery Attempt Lookup */
int db_ports_get_last_rob(db_t *db, int player_id, int *last_port, int64_t *last_ts, bool *success);

/* Q23: Insert Fake Bust */
int db_ports_insert_fake_bust(db_t *db, int port_id, int player_id);

/* Q24: Update Last Rob Attempt (Fake) */
int db_ports_update_last_rob_attempt(db_t *db, int player_id, int port_id);

/* Q25: Get Port Cash */
int db_ports_get_cash(db_t *db, int port_id, int64_t *cash);

/* Q26: Update Port Cash */
int db_ports_update_cash(db_t *db, int port_id, int64_t loot);

/* Q27: Increase Suspicion */
int db_ports_increase_suspicion(db_t *db, int cluster_id, int player_id, int susp_inc);

/* Q28: Update Last Rob Attempt (Success) */
int db_ports_update_last_rob_success(db_t *db, int player_id, int port_id);

/* Q29: Insert Real Bust */
int db_ports_insert_real_bust(db_t *db, int port_id, int player_id);

/* Q30: Update Cluster Status (Bust) */
int db_ports_update_cluster_bust(db_t *db, int cluster_id, int player_id, int susp_inc);

/* Q31: Ban Player in Cluster */
int db_ports_ban_player_in_cluster(db_t *db, int cluster_id, int player_id);

/* Q32: Update Last Rob Attempt (Fail) */
int db_ports_update_last_rob_fail(db_t *db, int player_id, int port_id);

/* Q33: Idempotency Lookup */
int db_ports_lookup_idemp(db_t *db, const char *key, int player_id, int sector_id, char **req_json, char **resp_json);

/* Q34: Check Port ID Exists */
int db_ports_check_port_id(db_t *db, int port_id, bool *exists);

/* Q35: Check Port Sector Exists */
int db_ports_check_port_sector(db_t *db, int sector_id, int *port_id);

/* Q36: Log Trade (Sell) */
int db_ports_log_trade_sell(db_t *db, int player_id, int port_id, int sector_id, const char *commodity, int amount, int buy_price);

/* Q37: Insert Idempotency (Sell) */
int db_ports_insert_idemp_sell(db_t *db, const char *key, int player_id, int sector_id, const char *req_json, const char *resp_json);

/* Q38: Idempotency Lookup (Race) */
int db_ports_lookup_idemp_race(db_t *db, const char *key, int player_id, int sector_id, char **req_json, char **resp_json);

/* Q39: Idempotency Lookup (Buy) */
int db_ports_lookup_idemp_buy(db_t *db, const char *key, int player_id, int sector_id, char **req_json, char **resp_json);

/* Q40: Port ID Exists (Buy) */
int db_ports_check_port_id_buy(db_t *db, int port_id, int *found_id);

/* Q41: Port Sector Exists (Buy) */
int db_ports_check_port_sector_buy(db_t *db, int sector_id, int *found_id);

/* Q42: Log Trade (Buy) */
int db_ports_log_trade_buy(db_t *db, int player_id, int port_id, int sector_id, const char *commodity, int amount, int unit_price);

/* Q43: Apply Alignment Hit (Illegal Buy) */
int db_ports_apply_alignment_hit(db_t *db, int player_id);

/* Q44: Insert Idempotency (Buy) */
int db_ports_insert_idemp_buy(db_t *db, const char *key, int player_id, int sector_id, const char *req_json, const char *resp_json);

/* Q45: Idempotency Lookup (Buy Race) */
int db_ports_lookup_idemp_buy_race(db_t *db, const char *key, int player_id, int sector_id, char **req_json, char **resp_json);

/* Q46: Idempotency Response Replay (Buy) */
int db_ports_replay_idemp_buy(db_t *db, const char *key, int player_id, int sector_id, char **resp_json);

/* Q47: Port Commodity Details */
int db_ports_get_commodity_details(db_t *db, int port_id, const char *commodity_code, int *quantity, int *max_capacity, bool *buys, bool *sells);

/* Q48: Update Port Sector */
int db_ports_update_sector(db_t *db, int port_id, int new_sector_id);

#endif
