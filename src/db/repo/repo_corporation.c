#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_corporation.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int repo_corp_get_player_role(db_t *db, int player_id, int corp_id, char *role_buffer, size_t buffer_size) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q1 */
    const char *q1 = "SELECT role FROM corp_members WHERE player_id = {1} AND corporation_id = {2};";
    char sql[512]; sql_build(db, q1, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i32(corp_id) }, 2, &res, &err) && db_res_step(res, &err)) {
        const char *role = db_res_col_text(res, 0, &err);
        if (role) {
            strncpy(role_buffer, role, buffer_size - 1);
            role_buffer[buffer_size - 1] = '\0';
        } else {
            role_buffer[0] = '\0';
        }
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    role_buffer[0] = '\0';
    return ERR_DB_NOT_FOUND;
}

int repo_corp_is_player_ceo(db_t *db, int player_id, int *out_corp_id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q2 */
    const char *q2 = "SELECT corporation_id FROM corporations WHERE owner_id = {1};";
    char sql[512]; sql_build(db, q2, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        if (out_corp_id) *out_corp_id = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 1;
    }
    if (res) db_res_finalize(res);
    return 0;
}

int repo_corp_get_player_corp_id(db_t *db, int player_id) {
    db_res_t *res = NULL;
    db_error_t err;
    int corp_id = 0;
    /* SQL_VERBATIM: Q3 */
    const char *q3 = "SELECT corporation_id FROM corp_members WHERE player_id = {1};";
    char sql[512]; sql_build(db, q3, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        corp_id = db_res_col_i32(res, 0, &err);
    }
    if (res) db_res_finalize(res);
    return corp_id;
}

int repo_corp_get_bank_account_id(db_t *db, int corp_id) {
    db_res_t *res = NULL;
    db_error_t err;
    int account_id = -1;
    /* SQL_VERBATIM: Q4 */
    const char *q4 = "SELECT bank_accounts_id FROM bank_accounts WHERE owner_type = 'corp' AND owner_id = {1};";
    char sql[512]; sql_build(db, q4, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(corp_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        account_id = db_res_col_i32(res, 0, &err);
    }
    if (res) db_res_finalize(res);
    return account_id;
}

int repo_corp_get_credit_rating(db_t *db, int corp_id, int *rating) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q5 */
    const char *q5 = "SELECT credit_rating FROM corporations WHERE corporation_id = {1};";
    char sql[512]; sql_build(db, q5, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(corp_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        if (rating) *rating = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return ERR_DB_NOT_FOUND;
}

int repo_corp_get_stock_id(db_t *db, int corp_id, int *out_stock_id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q6 */
    const char *q6 = "SELECT id FROM stocks WHERE corp_id = {1};";
    char sql[512]; sql_build(db, q6, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(corp_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        if (out_stock_id) *out_stock_id = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return ERR_DB_NOT_FOUND;
}

int repo_corp_get_stock_info(db_t *db, int stock_id, char **out_ticker, int *out_corp_id, int *out_total_shares, int *out_par_value, int *out_current_price, long long *out_last_dividend_ts) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q7 */
    const char *q7 = "SELECT ticker, corp_id, total_shares, par_value, current_price, last_dividend_ts FROM stocks WHERE id = {1};";
    char sql[512]; sql_build(db, q7, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(stock_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        if (out_ticker) {
            const char *tmp = db_res_col_text(res, 0, &err);
            *out_ticker = tmp ? strdup(tmp) : NULL;
        }
        if (out_corp_id) *out_corp_id = db_res_col_i32(res, 1, &err);
        if (out_total_shares) *out_total_shares = db_res_col_i32(res, 2, &err);
        if (out_par_value) *out_par_value = db_res_col_i32(res, 3, &err);
        if (out_current_price) *out_current_price = db_res_col_i32(res, 4, &err);
        if (out_last_dividend_ts) *out_last_dividend_ts = db_res_col_i64(res, 5, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return ERR_DB_NOT_FOUND;
}

int repo_corp_add_shares(db_t *db, int player_id, int stock_id, int quantity_change) {
    db_error_t err;
    const char *conflict_fmt = sql_conflict_target_fmt(db);
    if (!conflict_fmt) return -1;
    char conflict_clause[128];
    snprintf(conflict_clause, sizeof(conflict_clause), conflict_fmt, "player_id, corp_id");
    char sql_template[512];
    /* SQL_VERBATIM: Q8 */
    snprintf(sql_template, sizeof(sql_template),
        "INSERT INTO corp_shareholders (player_id, corp_id, shares) "
        "VALUES ({1}, (SELECT corp_id FROM stocks WHERE id = {2}), {3}) "
        "%s UPDATE SET shares = shares + excluded.shares;",
        conflict_clause);
    char sql[512]; sql_build(db, sql_template, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i32(stock_id), db_bind_i32(quantity_change) }, 3, &err)) return err.code;
    return 0;
}

int repo_corp_deduct_shares(db_t *db, int player_id, int stock_id, int quantity_change, int64_t *rows_affected) {
    db_error_t err;
    /* SQL_VERBATIM: Q9 */
    const char *q9 = "UPDATE corp_shareholders SET shares = shares + {1} WHERE player_id = {2} AND corp_id = (SELECT corp_id FROM stocks WHERE id = {3}) AND (shares + {4}) >= 0;";
    char sql[512]; sql_build(db, q9, sql, sizeof(sql));
    if (!db_exec_rows_affected(db, sql, (db_bind_t[]){ db_bind_i32(quantity_change), db_bind_i32(player_id), db_bind_i32(stock_id), db_bind_i32(quantity_change) }, 4, rows_affected, &err)) return err.code;
    return 0;
}

int repo_corp_delete_zero_shares(db_t *db) {
    db_error_t err;
    /* SQL_VERBATIM: Q10 */
    const char *q10 = "DELETE FROM corp_shareholders WHERE shares = 0;";
    if (!db_exec(db, q10, NULL, 0, &err)) return err.code;
    return 0;
}

int repo_corp_check_member_role(db_t *db, int corp_id, int player_id, char *role_buffer, size_t buffer_size) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q11 */
    const char *q11 = "SELECT role FROM corp_members WHERE corp_id = {1} AND player_id = {2};";
    char sql[512]; sql_build(db, q11, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_i32(player_id) }, 2, &res, &err) && db_res_step(res, &err)) {
        const char *role = db_res_col_text(res, 0, &err);
        if (role) {
            strncpy(role_buffer, role, buffer_size - 1);
            role_buffer[buffer_size - 1] = '\0';
        } else {
            role_buffer[0] = '\0';
        }
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    role_buffer[0] = '\0';
    return ERR_DB_NOT_FOUND;
}

int repo_corp_get_player_ship_type_name(db_t *db, int player_id, char *name_buffer, size_t buffer_size) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q12 */
    const char *q12 = "SELECT st.name FROM players p JOIN ships s ON p.ship_id = s.ship_id JOIN shiptypes st ON s.type_id = st.shiptypes_id WHERE p.player_id = {1};";
    char sql[512]; sql_build(db, q12, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        const char *name = db_res_col_text(res, 0, &err);
        if (name) {
            strncpy(name_buffer, name, buffer_size - 1);
            name_buffer[buffer_size - 1] = '\0';
        } else {
            name_buffer[0] = '\0';
        }
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    name_buffer[0] = '\0';
    return ERR_DB_NOT_FOUND;
}

int repo_corp_demote_ceo(db_t *db, int corp_id, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q13 */
    const char *q13 = "UPDATE corp_members SET role = 'Officer' WHERE corp_id = {1} AND player_id = {2} AND role = 'Leader';";
    char sql[512]; sql_build(db, q13, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_corp_insert_member_ignore(db_t *db, int corp_id, int player_id, const char *role) {
    db_error_t err;
    const char *conflict_clause = sql_insert_ignore_clause(db);
    if (!conflict_clause) return -1;
    char sql_template[256];
    /* SQL_VERBATIM: Q14 */
    snprintf(sql_template, sizeof(sql_template),
        "INSERT INTO corp_members (corp_id, player_id, role) "
        "VALUES ({1}, {2}, 'Member') %s;",
        conflict_clause);
    char sql[256]; sql_build(db, sql_template, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_corp_promote_leader(db_t *db, int corp_id, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q15 */
    const char *q15 = "UPDATE corp_members SET role = 'Leader' WHERE corp_id = {1} AND player_id = {2};";
    char sql[512]; sql_build(db, q15, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_corp_update_owner(db_t *db, int corp_id, int target_player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q16 */
    const char *q16 = "UPDATE corporations SET owner_id = {1} WHERE corporation_id = {2};";
    char sql[512]; sql_build(db, q16, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(target_player_id), db_bind_i32(corp_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_corp_create(db_t *db, const char *name, int owner_id, int64_t *new_corp_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q17 */
    const char *q17 = "INSERT INTO corporations (name, owner_id) VALUES ({1}, {2})";
    char sql[512]; sql_build(db, q17, sql, sizeof(sql));
    if (!db_exec_insert_id(db, sql, (db_bind_t[]){ db_bind_text(name), db_bind_i32(owner_id) }, 2, "corporation_id", new_corp_id, &err)) return err.code;
    return 0;
}

int repo_corp_insert_member(db_t *db, int corp_id, int player_id, const char *role) {
    db_error_t err;
    /* SQL_VERBATIM: Q18 */
    const char *q18 = "INSERT INTO corp_members (corp_id, player_id, role) VALUES ({1}, {2}, 'Leader');";
    char sql[512]; sql_build(db, q18, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_corp_create_bank_account(db_t *db, int corp_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q19 */
    const char *q19 = "INSERT INTO bank_accounts (owner_type, owner_id, currency) VALUES ('corp', {1}, 'CRD');";
    char sql[512]; sql_build(db, q19, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id) }, 1, &err)) return err.code;
    return 0;
}

int repo_corp_transfer_planets_to_corp(db_t *db, int corp_id, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q20 */
    const char *q20 = "UPDATE planets SET owner_id = {1}, owner_type = 'corp' WHERE owner_id = {2} AND owner_type = 'player';";
    char sql[512]; sql_build(db, q20, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_corp_get_invite_expiry(db_t *db, int corp_id, int player_id, long long *expires_at) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q21 */
    const char *q21 = "SELECT expires_at FROM corp_invites WHERE corp_id = {1} AND player_id = {2};";
    char sql[512]; sql_build(db, q21, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_i32(player_id) }, 2, &res, &err) && db_res_step(res, &err)) {
        if (expires_at) *expires_at = db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return ERR_DB_NOT_FOUND;
}

int repo_corp_insert_member_basic(db_t *db, int corp_id, int player_id, const char *role) {
    db_error_t err;
    /* SQL_VERBATIM: Q22 */
    const char *q22 = "INSERT INTO corp_members (corp_id, player_id, role) VALUES ({1}, {2}, 'Member');";
    char sql[512]; sql_build(db, q22, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_corp_delete_invite(db_t *db, int corp_id, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q23 */
    const char *q23 = "DELETE FROM corp_invites WHERE corp_id = {1} AND player_id = {2};";
    char sql[512]; sql_build(db, q23, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

db_res_t* repo_corp_list(db_t *db, db_error_t *err) {
    /* SQL_VERBATIM: Q24 */
    const char *q24 = "SELECT c.corporation_id, c.name, c.tag, c.owner_id, p.name, (SELECT COUNT(*) FROM corp_members cm WHERE cm.corporation_id = c.corporation_id) as member_count FROM corporations c LEFT JOIN players p ON c.owner_id = p.player_id WHERE c.corporation_id > 0;";
    db_res_t *res = NULL;
    db_query(db, q24, NULL, 0, &res, err);
    return res;
}

db_res_t* repo_corp_roster(db_t *db, int corp_id, db_error_t *err) {
    /* SQL_VERBATIM: Q25 */
    const char *q25 = "SELECT cm.player_id, p.name, cm.role FROM corp_members cm JOIN players p ON cm.player_id = p.player_id WHERE cm.corporation_id = {1};";
    char sql[1024]; sql_build(db, q25, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(corp_id) }, 1, &res, err);
    return res;
}

int repo_corp_delete_member(db_t *db, int corp_id, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q26 */
    const char *q26 = "DELETE FROM corp_members WHERE corp_id = {1} AND player_id = {2};";
    char sql[512]; sql_build(db, q26, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_corp_transfer_planets_to_player(db_t *db, int corp_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q27 */
    const char *q27 = "UPDATE planets SET owner_id = 0, owner_type = 'player' WHERE owner_id = {1} AND owner_type = 'corp';";
    char sql[512]; sql_build(db, q27, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id) }, 1, &err)) return err.code;
    return 0;
}

int repo_corp_delete(db_t *db, int corp_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q28 */
    const char *q28 = "DELETE FROM corporations WHERE corporation_id = {1};";
    char sql[512]; sql_build(db, q28, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id) }, 1, &err)) return err.code;
    return 0;
}

int repo_corp_get_member_count(db_t *db, int corp_id, int *count) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q29 */
    const char *q29 = "SELECT COUNT(*) FROM corp_members WHERE corp_id = {1};";
    char sql[512]; sql_build(db, q29, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(corp_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        if (count) *count = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return ERR_DB_NOT_FOUND;
}

int repo_corp_upsert_invite(db_t *db, int corp_id, int player_id, long long invited_at, long long expires_at) {
    db_error_t err;
    const char *conflict_fmt = sql_conflict_target_fmt(db);
    if (!conflict_fmt) return -1;
    char conflict_clause[128];
    snprintf(conflict_clause, sizeof(conflict_clause), conflict_fmt, "corp_id, player_id");
    char sql_template[512];
    /* SQL_VERBATIM: Q32 */
    snprintf(sql_template, sizeof(sql_template),
        "INSERT INTO corp_invites (corp_id, player_id, invited_at, expires_at) VALUES ({1}, {2}, {3}, {4}) "
        "%s UPDATE SET invited_at = excluded.invited_at, expires_at = excluded.expires_at;",
        conflict_clause);
    char sql[512]; sql_build(db, sql_template, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_i32(player_id), db_bind_i64(invited_at), db_bind_i64(expires_at) }, 4, &err)) return err.code;
    return 0;
}

db_res_t* repo_corp_get_info(db_t *db, int corp_id, db_error_t *err) {
    /* SQL_VERBATIM: Q33 */
    const char *q33 = "SELECT name, tag, created_at, owner_id FROM corporations WHERE corporation_id = {1};";
    char sql[512]; sql_build(db, q33, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(corp_id) }, 1, &res, err);
    return res;
}

int repo_corp_register_stock(db_t *db, int corp_id, const char *ticker, int total_shares, int par_value, int64_t *new_stock_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q35 */
    const char *q35 = "INSERT INTO stocks (corp_id, ticker, total_shares, par_value, current_price) VALUES ({1}, {2}, {3}, {4}, {5})";
    char sql[512]; sql_build(db, q35, sql, sizeof(sql));
    if (!db_exec_insert_id(db, sql, (db_bind_t[]){ db_bind_i32(corp_id), db_bind_text(ticker), db_bind_i32(total_shares), db_bind_i32(par_value), db_bind_i32(par_value) }, 5, "id", new_stock_id, &err)) return err.code;
    return 0;
}

int repo_corp_get_shares_owned(db_t *db, int player_id, int stock_id, int *shares) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q36 */
    const char *q36 = "SELECT shares FROM corp_shareholders WHERE player_id = {1} AND corp_id = (SELECT corp_id FROM stocks WHERE id = {2});";
    char sql[512]; sql_build(db, q36, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i32(stock_id) }, 2, &res, &err) && db_res_step(res, &err)) {
        if (shares) *shares = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return ERR_DB_NOT_FOUND;
}

int repo_corp_declare_dividend(db_t *db, int stock_id, int amount_per_share, long long declared_ts) {
    db_error_t err;
    /* SQL_VERBATIM: Q37 */
    const char *q37 = "INSERT INTO stock_dividends (stock_id, amount_per_share, declared_ts) VALUES ({1}, {2}, {3});";
    char sql[512]; sql_build(db, q37, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(stock_id), db_bind_i32(amount_per_share), db_bind_i64(declared_ts) }, 3, &err)) return err.code;
    return 0;
}

int repo_corp_is_public(db_t *db, int corp_id, bool *is_public) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q38 */
    const char *q38 = "SELECT 1 FROM stocks WHERE corp_id = {1};";
    char sql[512]; sql_build(db, q38, sql, sizeof(sql));
    *is_public = false;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(corp_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *is_public = true;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code == 0 ? 0 : err.code;
}

db_res_t* repo_corp_get_all_corps(db_t *db, db_error_t *err) {
    /* SQL_VERBATIM: Q39 */
    const char *q39 = "SELECT corporation_id, name FROM corporations;";
    db_res_t *res = NULL;
    db_query(db, q39, NULL, 0, &res, err);
    return res;
}

db_res_t* repo_corp_get_unpaid_dividends(db_t *db, db_error_t *err) {
    /* SQL_VERBATIM: Q40 */
    const char *q40 = "SELECT id, stock_id, amount_per_share FROM stock_dividends WHERE paid_ts IS NULL;";
    db_res_t *res = NULL;
    db_query(db, q40, NULL, 0, &res, err);
    return res;
}

db_res_t* repo_corp_get_stock_holders(db_t *db, int stock_id, db_error_t *err) {
    /* SQL_VERBATIM: Q41 */
    const char *q41 = "SELECT player_id, shares FROM corp_shareholders WHERE corp_id = (SELECT corp_id FROM stocks WHERE id = {1}) AND shares > 0;";
    char sql[512]; sql_build(db, q41, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(stock_id) }, 1, &res, err);
    return res;
}

int repo_corp_mark_dividend_paid(db_t *db, int div_id, long long paid_ts) {
    db_error_t err;
    /* SQL_VERBATIM: Q42 */
    const char *q42 = "UPDATE stock_dividends SET paid_ts = {1} WHERE id = {2};";
    char sql[512]; sql_build(db, q42, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(paid_ts), db_bind_i32(div_id) }, 2, &err)) return err.code;
    return 0;
}
