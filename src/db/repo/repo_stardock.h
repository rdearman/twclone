#ifndef REPO_STARDOCK_H
#define REPO_STARDOCK_H

#include "db/db_api.h"
#include <stdint.h>
#include <stdbool.h>

int repo_stardock_get_port_by_sector(db_t *db, int32_t sector_id, int32_t *out_port_id, int32_t *out_type);

int repo_stardock_get_ship_state(db_t *db, int32_t ship_id, db_res_t **out_res);

int repo_stardock_get_hardware_items(db_t *db, const char *location_type, db_res_t **out_res);

int repo_stardock_get_hardware_item_details(db_t *db, const char *code, db_res_t **out_res);

int repo_stardock_get_ship_limit_info(db_t *db, int32_t ship_id, const char *col_name, const char *limit_col, bool is_max_check_needed, db_res_t **out_res);

int repo_stardock_update_ship_hardware(db_t *db, const char *col_name, int32_t quantity, int32_t ship_id);

int repo_stardock_check_shipyard_location(db_t *db, int32_t sector_id, int32_t player_id, int32_t *out_port_id);

int repo_stardock_get_player_ship_info(db_t *db, int32_t player_id, db_res_t **out_res);

int repo_stardock_get_shipyard_inventory(db_t *db, int32_t port_id, db_res_t **out_res);

int repo_stardock_get_shiptype_details(db_t *db, int32_t type_id, db_res_t **out_res);

int repo_stardock_upgrade_ship(db_t *db, int32_t new_type_id, const char *new_name, int64_t cost, int32_t ship_id);

int repo_stardock_get_tavern_settings(db_t *db, db_res_t **out_res);

int repo_stardock_is_in_tavern(db_t *db, int32_t sector_id, bool *out_in_tavern);

int repo_stardock_get_loan(db_t *db, int32_t player_id, db_res_t **out_res);

int repo_stardock_get_player_credits(db_t *db, int32_t player_id, int64_t *out_credits);

int repo_stardock_get_player_alignment(db_t *db, int32_t player_id, int64_t *out_alignment);

int repo_stardock_update_player_credits(db_t *db, int32_t player_id, int64_t amount, bool is_win);

int repo_stardock_mark_loan_defaulted(db_t *db, int32_t player_id);

int repo_stardock_update_loan_principal(db_t *db, int32_t player_id, int64_t new_principal);

int repo_stardock_insert_lottery_ticket(db_t *db, const char *draw_date, int32_t player_id, int32_t number, int64_t cost, int32_t purchased_at);

int repo_stardock_get_lottery_state(db_t *db, const char *draw_date, db_res_t **out_res);

int repo_stardock_get_player_lottery_tickets(db_t *db, int32_t player_id, const char *draw_date, db_res_t **out_res);

int repo_stardock_insert_deadpool_bet(db_t *db, int32_t bettor_id, int32_t target_id, int64_t amount, int32_t odds_bp, int32_t placed_at, int32_t expires_at);

int repo_stardock_insert_graffiti(db_t *db, int32_t player_id, const char *text, int32_t created_at);

int repo_stardock_get_graffiti_count(db_t *db, int64_t *out_count);

int repo_stardock_delete_oldest_graffiti(db_t *db, int32_t limit);

int repo_stardock_update_alignment(db_t *db, int32_t player_id, int32_t alignment_gain);

int repo_stardock_insert_loan(db_t *db, int32_t player_id, int64_t principal, int32_t interest_rate_bp, int32_t due_date);

int repo_stardock_delete_loan(db_t *db, int32_t player_id);

int repo_stardock_update_loan_repayment(db_t *db, int32_t player_id, int64_t new_principal);

int repo_stardock_get_raffle_state(db_t *db, db_res_t **out_res);

int repo_stardock_update_raffle_on_win(db_t *db, int32_t player_id, int64_t winnings, int32_t now);

int repo_stardock_update_raffle_pot(db_t *db, int64_t current_pot);

int repo_stardock_get_commodity_prices(db_t *db, db_res_t **out_res);

#endif
