#ifndef REPO_BANK_H
#define REPO_BANK_H

#include "db/db_api.h"
#include <stdint.h>

int repo_bank_player_balance_add(db_t *db, int player_id, long long delta, long long *new_balance_out);
int repo_bank_check_account_active(db_t *db, int player_id, int *exists_out);
int repo_bank_insert_adj_transaction(db_t *db, int64_t account_id, const char *direction, int64_t amount_abs, int64_t ts, int64_t new_bal);
int repo_bank_get_balance(db_t *db, const char *owner_type, int owner_id, long long *balance_out);
int repo_bank_get_account_id(db_t *db, const char *owner_type, int owner_id, int *account_id_out);
int repo_bank_create_system_notice(db_t *db, const char *now_ts, int player_id, const char *msg);
int repo_bank_add_credits_returning(db_t *db, int account_id, long long amount, long long *new_balance_out);
int repo_bank_insert_transaction(db_t *db, const char *sql_template, int account_id, const char *tx_type, const char *direction, long long amount, long long balance_after, const char *tx_group_id, const char *now_epoch);
int repo_bank_deduct_credits_returning(db_t *db, int account_id, long long amount, long long *new_balance_out);
int repo_bank_create_account_if_not_exists(db_t *db, const char *owner_type, int owner_id, long long initial_balance);
int repo_bank_set_frozen_status(db_t *db, int player_id, int is_frozen);
int repo_bank_get_frozen_status(db_t *db, int player_id, int *out_frozen);
db_res_t* repo_bank_get_leaderboard(db_t *db, int limit, db_error_t *err);
db_res_t* repo_bank_get_fines(db_t *db, int player_id, db_error_t *err);
db_res_t* repo_bank_get_fine_details(db_t *db, int fine_id, db_error_t *err);
int repo_bank_update_fine(db_t *db, int fine_id, const char *new_status, long long amount_to_pay);
int repo_bank_get_transactions(db_t *db, const char *owner_type, int owner_id, int limit, const char *filter, long long start, long long end, long long min, long long max, db_res_t **out_res, db_error_t *err);

#endif // REPO_BANK_H
