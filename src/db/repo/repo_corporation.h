#ifndef REPO_CORPORATION_H
#define REPO_CORPORATION_H

#include "db/db_api.h"
#include <stdbool.h>

int repo_corp_get_player_role(db_t *db, int player_id, int corp_id, char *role_buffer, size_t buffer_size);
int repo_corp_is_player_ceo(db_t *db, int player_id, int *out_corp_id);
int repo_corp_get_player_corp_id(db_t *db, int player_id);
int repo_corp_get_bank_account_id(db_t *db, int corp_id);
int repo_corp_get_credit_rating(db_t *db, int corp_id, int *rating);
int repo_corp_get_stock_id(db_t *db, int corp_id, int *out_stock_id);
int repo_corp_get_stock_info(db_t *db, int stock_id, char **out_ticker, int *out_corp_id, int *out_total_shares, int *out_par_value, int *out_current_price, long long *out_last_dividend_ts);
int repo_corp_add_shares(db_t *db, int player_id, int stock_id, int quantity_change);
int repo_corp_deduct_shares(db_t *db, int player_id, int stock_id, int quantity_change, int64_t *rows_affected);
int repo_corp_delete_zero_shares(db_t *db);
int repo_corp_check_member_role(db_t *db, int corp_id, int player_id, char *role_buffer, size_t buffer_size);
int repo_corp_get_player_ship_type_name(db_t *db, int player_id, char *name_buffer, size_t buffer_size);
int repo_corp_demote_ceo(db_t *db, int corp_id, int player_id);
int repo_corp_insert_member_ignore(db_t *db, int corp_id, int player_id, const char *role);
int repo_corp_promote_leader(db_t *db, int corp_id, int player_id);
int repo_corp_update_owner(db_t *db, int corp_id, int target_player_id);
int repo_corp_create(db_t *db, const char *name, int owner_id, int64_t *new_corp_id);
int repo_corp_insert_member(db_t *db, int corp_id, int player_id, const char *role);
int repo_corp_insert_member_basic(db_t *db, int corp_id, int player_id, const char *role);
int repo_corp_create_bank_account(db_t *db, int corp_id);
int repo_corp_transfer_planets_to_corp(db_t *db, int corp_id, int player_id);
int repo_corp_get_invite_expiry(db_t *db, int corp_id, int player_id, long long *expires_at);
int repo_corp_delete_invite(db_t *db, int corp_id, int player_id);
db_res_t* repo_corp_list(db_t *db, db_error_t *err);
db_res_t* repo_corp_roster(db_t *db, int corp_id, db_error_t *err);
int repo_corp_delete_member(db_t *db, int corp_id, int player_id);
int repo_corp_transfer_planets_to_player(db_t *db, int corp_id);
int repo_corp_delete(db_t *db, int corp_id);
int repo_corp_get_member_count(db_t *db, int corp_id, int *count);
int repo_corp_upsert_invite(db_t *db, int corp_id, int player_id, long long invited_at, long long expires_at);
db_res_t* repo_corp_get_info(db_t *db, int corp_id, db_error_t *err);
int repo_corp_register_stock(db_t *db, int corp_id, const char *ticker, int total_shares, int par_value, int64_t *new_stock_id);
int repo_corp_get_shares_owned(db_t *db, int player_id, int stock_id, int *shares);
int repo_corp_declare_dividend(db_t *db, int stock_id, int amount_per_share, long long declared_ts);
int repo_corp_is_public(db_t *db, int corp_id, bool *is_public);
db_res_t* repo_corp_get_all_corps(db_t *db, db_error_t *err);
db_res_t* repo_corp_get_unpaid_dividends(db_t *db, db_error_t *err);
db_res_t* repo_corp_get_stock_holders(db_t *db, int stock_id, db_error_t *err);
int repo_corp_mark_dividend_paid(db_t *db, int div_id, long long paid_ts);

#endif // REPO_CORPORATION_H
