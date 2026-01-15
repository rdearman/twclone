#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_bank.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int repo_bank_player_balance_add(db_t *db, int player_id, long long delta, long long *new_balance_out) {
    db_error_t err;
    int64_t rows = 0;
    int account_id = 0;

    if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) return -err.code;

    /* 1. Try Update */
    /* SQL_VERBATIM: Q1_UPD */
    const char *q_upd = "UPDATE bank_accounts SET balance = balance + {2} "
                        "WHERE owner_type = 'player' AND owner_id = {1} "
                        "AND currency = 'CRD' AND is_active = 1 "
                        "AND (balance + {2}) >= 0;";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    if (!db_exec_rows_affected(db, sql_upd, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i64(delta) }, 2, &rows, &err)) {
        db_tx_rollback(db, NULL);
        return -err.code;
    }

    if (rows == 0) {
        /* 2. Try Insert if delta >= 0 */
        if (delta < 0) {
            db_tx_rollback(db, NULL);
            return -1;
        }
        /* SQL_VERBATIM: Q1_INS */
        const char *q_ins = "INSERT INTO bank_accounts (owner_type, owner_id, currency, balance, is_active) "
                            "SELECT 'player', {1}, 'CRD', {2}, 1 "
                            "WHERE NOT EXISTS (SELECT 1 FROM bank_accounts WHERE owner_type = 'player' AND owner_id = {1} AND currency = 'CRD' AND is_active = 1);";
        char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
        if (!db_exec_rows_affected(db, sql_ins, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i64(delta) }, 2, &rows, &err)) {
            db_tx_rollback(db, NULL);
            return -err.code;
        }
    }

    /* 3. Get ID and Balance */
    if (repo_bank_get_account_id(db, "player", player_id, &account_id) != 0 ||
        repo_bank_get_balance_by_account_id(db, account_id, new_balance_out) != 0) {
        db_tx_rollback(db, NULL);
        return -1;
    }

    if (!db_tx_commit(db, &err)) return -err.code;

    return account_id;
}

int repo_bank_check_account_active(db_t *db, int player_id, int *exists_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q2 */
    const char *q2 = "SELECT 1 FROM bank_accounts "
    "WHERE owner_type='player' AND owner_id={1} AND currency='CRD' AND is_active=1 "
    "LIMIT 1;";
    char sql[256]; sql_build(db, q2, sql, sizeof(sql));
    *exists_out = 0;
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *exists_out = 1;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code;
}

int repo_bank_insert_adj_transaction(db_t *db, int64_t account_id, const char *direction, int64_t amount_abs, int64_t ts, int64_t new_bal) {
    db_error_t err;
    /* SQL_VERBATIM: Q3 */
    const char *q3 = "INSERT INTO bank_transactions "
    "  (account_id, tx_type, direction, amount, currency, description, ts, balance_after) "
    "VALUES "
    "  ({1}, {2}, {3}, {4}, 'CRD', {5}, {6}, {7});";
    char sql[512]; sql_build(db, q3, sql, sizeof(sql));
    const char *desc = "player bank balance adjustment";
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(account_id), db_bind_text("ADJUSTMENT"), db_bind_text(direction), db_bind_i64(amount_abs), db_bind_text(desc), db_bind_i64(ts), db_bind_i64(new_bal) }, 7, &err)) return err.code;
    return 0;
}

int repo_bank_get_balance(db_t *db, const char *owner_type, int owner_id, long long *balance_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q4 */
    const char *q4 = "SELECT balance FROM bank_accounts WHERE owner_type = {1} AND owner_id = {2} AND is_active = 1;";
    char sql[256]; sql_build(db, q4, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_text(owner_type), db_bind_i32(owner_id) }, 2, &res, &err) && db_res_step(res, &err)) {
        *balance_out = db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return ERR_DB_NOT_FOUND;
}

int repo_bank_get_account_id(db_t *db, const char *owner_type, int owner_id, int *account_id_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q5 */
    const char *q5 = "SELECT bank_accounts_id FROM bank_accounts WHERE owner_type = {1} AND owner_id = {2}";
    char sql[256]; sql_build(db, q5, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_text(owner_type), db_bind_i32(owner_id) }, 2, &res, &err) && db_res_step(res, &err)) {
        *account_id_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return ERR_DB_NOT_FOUND;
}

int repo_bank_get_balance_by_account_id(db_t *db, int account_id, long long *balance_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q18 */
    const char *q18 = "SELECT balance FROM bank_accounts WHERE bank_accounts_id = {1};";
    char sql[256]; sql_build(db, q18, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(account_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        if (balance_out) *balance_out = db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return ERR_DB_NOT_FOUND;
}

int repo_bank_create_system_notice(db_t *db, const char *now_ts, int player_id, const char *msg) {
    db_error_t err;
    /* SQL_VERBATIM: Q6 */
    char sql_template[512];
    snprintf(sql_template, sizeof(sql_template),
        "INSERT INTO system_notice (created_at, scope, player_id, title, body, severity) VALUES (%s, 'player', {1}, 'Bank Alert', {2}, 'info');",
        now_ts);
    char sql[512]; sql_build(db, sql_template, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_text(msg) }, 2, &err)) return err.code;
    return 0;
}

int repo_bank_add_credits_returning(db_t *db, int account_id, long long amount, long long *new_balance_out) {
    db_error_t err;
    int64_t rows = 0;
    /* SQL_VERBATIM: Q7 */
    const char *q7 = "UPDATE bank_accounts SET balance = balance + {1} WHERE bank_accounts_id = {2};";
    char sql[256]; sql_build(db, q7, sql, sizeof(sql));
    if (db_exec_rows_affected(db, sql, (db_bind_t[]){ db_bind_i64(amount), db_bind_i32(account_id) }, 2, &rows, &err)) {
        if (rows > 0) {
            return repo_bank_get_balance_by_account_id(db, account_id, new_balance_out);
        }
    }
    return ERR_DB_NOT_FOUND;
}

int repo_bank_insert_transaction(db_t *db, const char *sql_template, int account_id, const char *tx_type, const char *direction, long long amount, long long balance_after, const char *tx_group_id, const char *now_epoch) {
    db_error_t err;
    char sql[512];
    snprintf(sql, sizeof(sql), sql_template, now_epoch);
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(account_id), db_bind_text(tx_type), db_bind_i64(amount), db_bind_i64(balance_after), db_bind_text(tx_group_id) }, 5, &err)) return err.code;
    return 0;
}

int repo_bank_deduct_credits_returning(db_t *db, int account_id, long long amount, long long *new_balance_out) {
    db_error_t err;
    int64_t rows = 0;
    /* SQL_VERBATIM: Q9 */
    const char *q9 = "UPDATE bank_accounts SET balance = balance - {1} WHERE bank_accounts_id = {2} AND balance >= {1};";
    char sql[256]; sql_build(db, q9, sql, sizeof(sql));
    if (db_exec_rows_affected(db, sql, (db_bind_t[]){ db_bind_i64(amount), db_bind_i32(account_id) }, 2, &rows, &err)) {
        if (rows > 0) {
            return repo_bank_get_balance_by_account_id(db, account_id, new_balance_out);
        }
    }
    return ERR_DB_NOT_FOUND;
}

int repo_bank_create_account_if_not_exists(db_t *db, const char *owner_type, int owner_id, long long initial_balance) {

    db_error_t err;

    /* SQL_VERBATIM: Q11 */

    const char *q11 = "INSERT INTO bank_accounts (owner_type, owner_id, balance, interest_rate_bp, is_active) VALUES ({1}, {2}, {3}, 0, 1) ON CONFLICT DO NOTHING;";

    char sql[256]; sql_build(db, q11, sql, sizeof(sql));

    if (!db_exec (db, sql, (db_bind_t[]){ db_bind_text(owner_type), db_bind_i32(owner_id), db_bind_i64(initial_balance) }, 3, &err)) return err.code;

    return 0;

}



int repo_bank_set_frozen_status(db_t *db, int player_id, int is_frozen) {
    db_error_t err;
    /* SQL_VERBATIM: Q13 */
    const char *q13 = "INSERT INTO bank_flags (player_id, is_frozen) VALUES ({1}, {2}) ON CONFLICT(player_id) DO UPDATE SET is_frozen = excluded.is_frozen;";
    char sql[256]; sql_build(db, q13, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i32(is_frozen) }, 2, &err)) return err.code;
    return 0;
}

int repo_bank_get_frozen_status(db_t *db, int player_id, int *out_frozen) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q14 */
    const char *q14 = "SELECT is_frozen FROM bank_flags WHERE player_id = {1};";
    char sql[256]; sql_build(db, q14, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *out_frozen = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code;
}

db_res_t* repo_bank_get_leaderboard(db_t *db, int limit, db_error_t *err) {
    db_res_t *res = NULL;
    /* SQL_VERBATIM: Q15 */
    const char *q15 = "SELECT P.name, BA.balance FROM bank_accounts BA JOIN players P ON P.player_id = BA.owner_id WHERE BA.owner_type = 'player' ORDER BY BA.balance DESC LIMIT {1};";
    char sql[512]; sql_build(db, q15, sql, sizeof(sql));
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(limit) }, 1, &res, err);
    return res;
}

db_res_t* repo_bank_get_fines(db_t *db, int player_id, db_error_t *err) {
    db_res_t *res = NULL;
    /* SQL_VERBATIM: Q16 */
    const char *q16 = "SELECT fines_id as id, reason, amount, issued_ts, status FROM fines WHERE recipient_type = 'player' AND recipient_id = {1} AND status != 'paid';";
    char sql[512]; sql_build(db, q16, sql, sizeof(sql));
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_bank_get_fine_details(db_t *db, int fine_id, db_error_t *err) {
    db_res_t *res = NULL;
    /* SQL_VERBATIM: Q17 */
    const char *q17 = "SELECT amount, recipient_id, status, recipient_type FROM fines WHERE fines_id = {1};";
    char sql[512]; sql_build(db, q17, sql, sizeof(sql));
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(fine_id) }, 1, &res, err);
    return res;
}

int repo_bank_update_fine(db_t *db, int fine_id, const char *new_status, long long amount_to_pay) {
    db_error_t err;
    /* SQL_VERBATIM: Q18 */
    const char *q18 = "UPDATE fines SET status = {1}, amount = amount - {2} WHERE fines_id = {3};";
    char sql[512]; sql_build(db, q18, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_text(new_status), db_bind_i64(amount_to_pay), db_bind_i32(fine_id) }, 3, &err)) return err.code;
    return 0;
}

int repo_bank_get_transactions(db_t *db, const char *owner_type, int owner_id, int limit, const char *filter, long long start, long long end, long long min, long long max, db_res_t **out_res, db_error_t *err) {
    char sql[2048];
    /* SQL_VERBATIM: Q12 */
    snprintf (sql,
            sizeof(sql),
            "SELECT ts, account_id, tx_type, amount, balance_after, description, tx_group_id FROM bank_transactions "
            "WHERE account_id = (SELECT bank_accounts_id FROM bank_accounts WHERE owner_type = {1} AND owner_id = {2}) ");

    db_bind_t params[12]; int idx = 0;
    params[idx++] = db_bind_text (owner_type);
    params[idx++] = db_bind_i32 (owner_id);

    if (filter && *filter) {
        char buf[32]; snprintf (buf, sizeof(buf), "AND tx_type = $%d ", idx + 1);
        strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);
        params[idx++] = db_bind_text (filter);
    }
    if (start > 0) {
        char buf[32]; snprintf (buf, sizeof(buf), "AND ts >= $%d ", idx + 1);
        strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);
        params[idx++] = db_bind_i64 (start);
    }
    if (end > 0) {
        char buf[32]; snprintf (buf, sizeof(buf), "AND ts <= $%d ", idx + 1);
        strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);
        params[idx++] = db_bind_i64 (end);
    }
    if (min > 0) {
        char buf[32]; snprintf (buf, sizeof(buf), "AND ABS(amount) >= $%d ", idx + 1);
        strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);
        params[idx++] = db_bind_i64 (min);
    }
    if (max > 0) {
        char buf[32]; snprintf (buf, sizeof(buf), "AND ABS(amount) <= $%d ", idx + 1);
        strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);
        params[idx++] = db_bind_i64 (max);
    }
    strncat (sql, "ORDER BY ts DESC, id DESC LIMIT ", sizeof(sql) - strlen (sql) - 1);
    char buf[16]; snprintf (buf, sizeof(buf), "$%d;", idx + 1);
    strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);
    params[idx++] = db_bind_i32 (limit);

    char sql_converted[2048];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    if (!db_query(db, sql_converted, params, idx, out_res, err)) return err->code;
    return 0;
}
