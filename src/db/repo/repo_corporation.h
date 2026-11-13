#ifndef REPO_CORPORATION_H
#define REPO_CORPORATION_H

#include "db/db_api.h"
#include <stdbool.h>

int repo_corp_get_player_role(db_t *db, int player_id, int corp_id, char *role_buffer, size_t buffer_size);
int repo_corp_is_player_ceo(db_t *db, int player_id, int *out_corp_id);
int repo_corp_get_player_corp_id(db_t *db, int player_id);
int repo_corp_get_bank_account_id(db_t *db, int corp_id);
int repo_corp_get_credit_rating(db_t *db, int corp_id, int *rating);
int repo_corp_get_equity_id(db_t *db, int corp_id, int *out_equity_id);
int repo_corp_get_equity_info(db_t *db, int equity_id, char **out_ticker, int *out_corp_id, int *out_total_shares, int *out_par_value, int *out_current_price, long long *out_last_dividend_ts);
int repo_corp_add_shares(db_t *db, int player_id, int equity_id, int quantity_change);
int repo_corp_deduct_shares(db_t *db, int player_id, int equity_id, int quantity_change, int64_t *rows_affected);
int repo_corp_delete_zero_shares(db_t *db);
int repo_corp_check_member_role(db_t *db, int corp_id, int player_id, char *role_buffer, size_t buffer_size);
/* Revalidate and lock current membership; requires an active transaction. */
int repo_corp_lock_member_role(db_t *db, int corp_id, int player_id,
                               char *role_buffer, size_t buffer_size);
typedef enum {
    REPO_CORP_TRANSFER_OK = 0,
    REPO_CORP_TRANSFER_INVALID = 1,
    REPO_CORP_TRANSFER_INSUFFICIENT = 2,
    REPO_CORP_TRANSFER_CAPACITY = 3,
    REPO_CORP_TRANSFER_DATABASE = 4
} repo_corp_transfer_result_t;
/* Atomic member-to-member transfer; both players must be current members in
 * the same sector. asset is credits/fighters/mines/shields. Ship assets move
 * between each member's active ship. */
repo_corp_transfer_result_t repo_corp_transfer_member_asset(
    db_t *db, int sender_id, int recipient_id, const char *asset,
    int64_t quantity, int64_t *sender_remaining, int64_t *recipient_total);
int repo_corp_get_player_ship_type_name(db_t *db, int player_id, char *name_buffer, size_t buffer_size);
int repo_corp_demote_ceo(db_t *db, int corp_id, int player_id);
int repo_corp_insert_member_ignore(db_t *db, int corp_id, int player_id, const char *role);
int repo_corp_promote_leader(db_t *db, int corp_id, int player_id);
int repo_corp_update_owner(db_t *db, int corp_id, int target_player_id);
int repo_corp_create(db_t *db, const char *name, int owner_id, int64_t *new_corp_id);
int repo_corp_create_with_tag(db_t *db, const char *name, const char *tag, int owner_id, int64_t *new_corp_id);
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
int repo_corp_register_equity(db_t *db, int corp_id, const char *ticker, int total_shares, int par_value, int64_t *new_equity_id);
int repo_corp_get_shares_owned(db_t *db, int player_id, int equity_id, int *shares);
int repo_corp_declare_dividend(db_t *db, int equity_id, int amount_per_share, long long declared_ts);
int repo_corp_is_public(db_t *db, int corp_id, bool *is_public);
db_res_t* repo_corp_get_all_corps(db_t *db, db_error_t *err);
db_res_t* repo_corp_get_unpaid_dividends(db_t *db, db_error_t *err);
db_res_t* repo_corp_get_equity_holders(db_t *db, int equity_id, db_error_t *err);
int repo_corp_mark_dividend_paid(db_t *db, int div_id, long long paid_ts);

#endif // REPO_CORPORATION_H
