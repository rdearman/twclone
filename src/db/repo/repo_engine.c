#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_engine.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int repo_engine_get_config_int(db_t *db, const char *key, int *value_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q1 */
    const char *q1 = "SELECT value FROM config WHERE key = {1} AND type = 'int' LIMIT 1;";
    char sql[512]; sql_build(db, q1, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_text(key) }, 1, &res, &err) && db_res_step(res, &err)) {
        *value_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_engine_get_alignment_band_info(db_t *db, int band_id, int *is_good_out, int *is_evil_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q3 */
    const char *q3 = "SELECT is_good, is_evil FROM alignment_band WHERE alignment_band_id = {1};";
    char sql[512]; sql_build(db, q3, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(band_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *is_good_out = db_res_col_i32(res, 0, &err);
        *is_evil_out = db_res_col_i32(res, 1, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_engine_create_broadcast_notice(db_t *db, const char *ts_fmt, int64_t now_s, const char *title, const char *body, const char *severity, int64_t expires_at, int64_t *new_id_out) {
    db_error_t err;
    char sql_tmpl[512];
    /* SQL_VERBATIM: Q4 */
    snprintf(sql_tmpl, sizeof(sql_tmpl),
        "INSERT INTO system_notice(created_at, title, body, severity, expires_at) "
        "VALUES(%s, {2}, {3}, {4}, %s)",
        ts_fmt, ts_fmt);
    char sql[512]; sql_build(db, sql_tmpl, sql, sizeof(sql));
    db_bind_t params[5];
    params[0] = db_bind_i64 (now_s);
    params[1] = db_bind_text (title);
    params[2] = db_bind_text (body);
    params[3] = db_bind_text (severity ? severity : "info");
    if (expires_at > 0) params[4] = db_bind_i64 (expires_at);
    else params[4] = db_bind_null ();
    if (!db_exec_insert_id (db, sql, params, 5, new_id_out, &err)) return err.code;
    return 0;
}

int repo_engine_publish_notice(db_t *db, const char *ts_fmt, int64_t now_s, const char *scope, int player_id, const char *message, const char *severity, int64_t expires_at, int64_t *new_id_out) {
    db_error_t err;
    char sql_tmpl[512];
    /* SQL_VERBATIM: Q5 */
    snprintf(sql_tmpl, sizeof(sql_tmpl),
        "INSERT INTO system_notice(created_at, scope, player_id, title, body, severity, expires_at) "
        "VALUES(%s, {2}, {3}, 'Notice', {4}, {5}, %s)",
        ts_fmt, ts_fmt);
    char sql[512]; sql_build(db, sql_tmpl, sql, sizeof(sql));
    db_bind_t params[6];
    params[0] = db_bind_i64 (now_s);
    params[1] = db_bind_text (scope);
    params[2] = db_bind_i32 (player_id);
    params[3] = db_bind_text (message);
    params[4] = db_bind_text (severity);
    if (expires_at > 0) params[5] = db_bind_i64 (expires_at);
    else params[5] = db_bind_null ();
    if (!db_exec_insert_id (db, sql, params, 6, new_id_out, &err)) return err.code;
    return 0;
}

db_res_t* repo_engine_get_ready_commands(db_t *db, int64_t now_s, int max_rows, db_error_t *err) {
    /* SQL_VERBATIM: Q6 */
    const char *q6 = "SELECT engine_commands_id, type, payload, idem_key FROM engine_commands WHERE status='ready' AND due_at <= {1} ORDER BY priority ASC, due_at ASC, engine_commands_id ASC LIMIT {2};";
    char sql[512]; sql_build(db, q6, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(now_s), db_bind_i32(max_rows) }, 2, &res, err);
    return res;
}

int repo_engine_mark_command_running(db_t *db, const char *ts_fmt, int64_t now_s, int64_t cmd_id) {
    db_error_t err;
    char sql_running_tmpl[256];
    /* SQL_VERBATIM: Q7 */
    snprintf(sql_running_tmpl, sizeof(sql_running_tmpl),
        "UPDATE engine_commands SET status='running', started_at=%s WHERE engine_commands_id={2}",
        ts_fmt);
    char sql[256]; sql_build(db, sql_running_tmpl, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(now_s), db_bind_i64(cmd_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_engine_mark_command_done(db_t *db, const char *ts_fmt, int64_t now_s, int64_t cmd_id) {
    db_error_t err;
    char sql_done_tmpl[256];
    /* SQL_VERBATIM: Q8 */
    snprintf(sql_done_tmpl, sizeof(sql_done_tmpl),
        "UPDATE engine_commands SET status='done', finished_at=%s WHERE engine_commands_id={2}",
        ts_fmt);
    char sql[256]; sql_build(db, sql_done_tmpl, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(now_s), db_bind_i64(cmd_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_engine_mark_command_error(db_t *db, const char *ts_fmt, int64_t now_s, int64_t cmd_id) {
    db_error_t err;
    char sql_err_tmpl[256];
    /* SQL_VERBATIM: Q9 */
    snprintf(sql_err_tmpl, sizeof(sql_err_tmpl),
        "UPDATE engine_commands SET status='error', attempts=attempts+1, finished_at=%s WHERE engine_commands_id={2}",
        ts_fmt);
    char sql[256]; sql_build(db, sql_err_tmpl, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(now_s), db_bind_i64(cmd_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_engine_reclaim_stale_locks(db_t *db, int64_t stale_threshold_ms) {
    db_error_t err;
    /* SQL_VERBATIM: Q10 */
    const char *q10 = "DELETE FROM locks WHERE owner='server' AND until_ms < {1};";
    char sql[512]; sql_build(db, q10, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(stale_threshold_ms) }, 1, &err)) return err.code;
    return 0;
}

db_res_t* repo_engine_get_pending_cron_tasks(db_t *db, const char *ts_expr, int64_t now_s, int limit, db_error_t *err) {
    char sql_pick_tmpl[512];
    /* SQL_VERBATIM: Q11 */
    snprintf(sql_pick_tmpl, sizeof(sql_pick_tmpl),
        "SELECT cron_tasks_id, name, schedule FROM cron_tasks "
        "WHERE enabled=TRUE "
        "  AND (next_due_at IS NULL OR next_due_at <= %s) "
        "ORDER BY next_due_at ASC "
        "LIMIT {2}",
        ts_expr);
    char sql[512]; sql_build(db, sql_pick_tmpl, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(now_s), db_bind_i32(limit) }, 2, &res, err);
    return res;
}

int repo_engine_update_cron_task_schedule(db_t *db, const char *ts_expr1, const char *ts_expr2, int64_t id, int64_t now_s, int64_t next_due) {
    db_error_t err;
    char sql_upd_tmpl[256];
    /* SQL_VERBATIM: Q12 */
    snprintf(sql_upd_tmpl, sizeof(sql_upd_tmpl),
        "UPDATE cron_tasks "
        "SET last_run_at=%s, next_due_at=%s "
        "WHERE cron_tasks_id={1};",
        ts_expr1, ts_expr2);
    char sql[256]; sql_build(db, sql_upd_tmpl, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(id), db_bind_i64(now_s), db_bind_i64(next_due) }, 3, &err)) return err.code;
    return 0;
}

int repo_engine_sweep_expired_notices(db_t *db, const char *ts_fmt, int64_t now_s) {
    db_error_t err;
    char sql[512];
    /* SQL_VERBATIM: Q13 */
    snprintf(sql, sizeof(sql),
        "DELETE FROM system_notice "
        "WHERE expires_at IS NOT NULL AND expires_at <= %s "
        "AND system_notice_id IN (SELECT system_notice_id FROM system_notice WHERE expires_at IS NOT NULL AND expires_at <= %s LIMIT 500);",
        ts_fmt, ts_fmt);
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(now_s) }, 1, &err)) return err.code;
    return 0;
}

db_res_t* repo_engine_get_retryable_commands(db_t *db, int max_retries, db_error_t *err) {
    /* SQL_VERBATIM: Q14 */
    const char *q14 = "SELECT engine_commands_id, attempts FROM engine_commands WHERE status='error' AND attempts < {1} LIMIT 500;";
    char sql[512]; sql_build(db, q14, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(max_retries) }, 1, &res, err);
    return res;
}

int repo_engine_reschedule_deadletter(db_t *db, int64_t now_s, int64_t cmd_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q15 */
    const char *q15 = "UPDATE engine_commands SET status='ready', due_at={1} + (attempts * 60) WHERE engine_commands_id={2};";
    char sql[512]; sql_build(db, q15, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(now_s), db_bind_i64(cmd_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_engine_cleanup_expired_limpets(db_t *db, const char *deployed_as_epoch, int asset_type, int64_t threshold_s) {
    db_error_t err;
    char sql_delete_template[512];
    /* SQL_VERBATIM: Q16 */
    snprintf(sql_delete_template, sizeof(sql_delete_template),
        "DELETE FROM sector_assets "
        "WHERE asset_type = {1} AND %s <= {2}",
        deployed_as_epoch);
    char sql[512]; sql_build(db, sql_delete_template, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(asset_type), db_bind_i64(threshold_s) }, 2, &err)) return err.code;
    return 0;
}

db_res_t* repo_engine_get_active_interest_accounts(db_t *db, db_error_t *err) {
    /* SQL_VERBATIM: Q17 */
    const char *q17 = "SELECT id, owner_type, owner_id, balance, interest_rate_bp, last_interest_tick FROM bank_accounts WHERE is_active = 1 AND interest_rate_bp > 0;";
    db_res_t *res = NULL;
    db_query(db, q17, NULL, 0, &res, err);
    return res;
}

int repo_engine_update_last_interest_tick(db_t *db, int current_epoch_day, int account_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q18 */
    const char *q18 = "UPDATE bank_accounts SET last_interest_tick = {1} WHERE bank_accounts_id = {2};";
    char sql[512]; sql_build(db, q18, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(current_epoch_day), db_bind_i32(account_id) }, 2, &err)) return err.code;
    return 0;
}
