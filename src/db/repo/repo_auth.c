#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_auth.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../server_log.h"

int repo_auth_get_player_type_flags(db_t *db, int player_id, int *type_out, int *flags_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q1 */
    const char *q1 = "SELECT COALESCE(type,2), COALESCE(flags,0) FROM players WHERE player_id={1};";
    char sql[512]; sql_build(db, q1, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *type_out = (int)db_res_col_i32(res, 0, &err);
        *flags_out = (int)db_res_col_i32(res, 1, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_auth_upsert_global_sub(db_t *db, int player_id) {
    db_error_t err;
    int64_t rows = 0;

    /* 1. Try Update first */
    const char *q_upd = "UPDATE subscriptions SET locked=1, enabled=1 WHERE player_id={1} AND event_type='global';";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    if (db_exec_rows_affected(db, sql_upd, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &rows, &err) && rows > 0) return 0;

    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO subscriptions(player_id, event_type, delivery, locked, enabled) VALUES({1}, 'global', 'push', 1, 1);";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &err)) return 0;
        }
        return err.code;
    }
    return 0;
}

int repo_auth_upsert_player_sub(db_t *db, int player_id, const char *channel) {
    db_error_t err;
    int64_t rows = 0;

    /* 1. Try Update first */
    const char *q_upd = "UPDATE subscriptions SET locked=1, enabled=1 WHERE player_id={1} AND event_type={2};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_i32(player_id), db_bind_text(channel) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 2, &rows, &err) && rows > 0) return 0;

    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO subscriptions(player_id, event_type, delivery, locked, enabled) VALUES({1}, {2}, 'push', 1, 1);";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, (db_bind_t[]){ db_bind_i32(player_id), db_bind_text(channel) }, 2, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 2, &err)) return 0;
        }
        return err.code;
    }
    return 0;
}

int repo_auth_upsert_sysop_sub(db_t *db, int player_id) {
    db_error_t err;
    int64_t rows = 0;

    /* 1. Try Update first */
    const char *q_upd = "UPDATE subscriptions SET locked=1, enabled=1 WHERE player_id={1} AND event_type='sysop';";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    if (db_exec_rows_affected(db, sql_upd, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &rows, &err) && rows > 0) return 0;

    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO subscriptions(player_id, event_type, delivery, locked, enabled) VALUES({1}, 'sysop', 'push', 1, 1);";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &err)) return 0;
        }
        return err.code;
    }
    return 0;
}

int repo_auth_upsert_locked_sub(db_t *db, int player_id, const char *topic) {
    db_error_t err;
    int64_t rows = 0;

    /* 1. Try Update first */
    const char *q_upd = "UPDATE subscriptions SET enabled=1, locked=CASE WHEN locked > 1 THEN locked ELSE 1 END WHERE player_id={1} AND event_type={2};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_i32(player_id), db_bind_text(topic) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 2, &rows, &err) && rows > 0) return 0;

    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO subscriptions(player_id,event_type,delivery,filter_json,locked,enabled) VALUES({1}, {2}, 'internal', NULL, 1, 1);";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, (db_bind_t[]){ db_bind_i32(player_id), db_bind_text(topic) }, 2, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 2, &err)) return 0;
        }
        return err.code;
    }
    return 0;
}

int repo_auth_insert_pref_if_missing(db_t *db, int player_id, const char *key, const char *type, const char *value) {
    db_error_t err;
    /* SQL_VERBATIM: Q6 */
    const char *q6 = "INSERT INTO player_prefs(player_id,key,type,value) "
    "SELECT {1}, {2}, {3}, {4} "
    "WHERE NOT EXISTS (SELECT 1 FROM player_prefs WHERE player_id={5} AND key={6});";
    char sql[512]; sql_build(db, q6, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_text(key), db_bind_text(type), db_bind_text(value), db_bind_i32(player_id), db_bind_text(key) }, 6, &err)) return err.code;
    return 0;
}

int repo_auth_get_podded_status(db_t *db, int player_id, char *status_out, size_t status_sz, int64_t *big_sleep_until_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q7 */
    const char *q7 = "SELECT status, big_sleep_until FROM podded_status WHERE player_id = {1};";
    char sql[512]; sql_build(db, q7, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        const char *st = db_res_col_text(res, 0, &err);
        if (st && status_out) strncpy(status_out, st, status_sz - 1);
        *big_sleep_until_out = db_res_col_i64(res, 1, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_auth_get_unread_news_count(db_t *db, int player_id, int *count_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q8 */
    const char *q8 = "SELECT COUNT(*) FROM news_feed WHERE timestamp > (SELECT last_news_read_timestamp FROM players WHERE player_id = {1});";
    char sql[512]; sql_build(db, q8, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *count_out = (int)db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_auth_upsert_system_sub(db_t *db, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q9 */
    const char *q9 = "INSERT INTO subscriptions(player_id,event_type,delivery,enabled) VALUES({1},'system.*','push',1);";
    char sql[512]; sql_build(db, q9, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &err)) {
        if (err.code == ERR_DB_CONSTRAINT) {
            return 0; // DO NOTHING
        }
        return err.code;
    }
    return 0;
}

int repo_auth_register_player(db_t *db, const char *name, const char *pass, const char *ship_name, int spawn_sid, int64_t *player_id_out, db_error_t *err_out) {
    db_res_t *res = NULL;
    /* SQL_VERBATIM: Q10 */
    const char *q10 = "SELECT register_player({1}, {2}, {3}, false, {4});";
    char sql[512]; sql_build(db, q10, sql, sizeof(sql));

    if (!db_query(db, sql, (db_bind_t[]){db_bind_text(name), db_bind_text(pass), db_bind_text(ship_name), db_bind_i32(spawn_sid)}, 4, &res, err_out)) {
        goto cleanup;
    }

    if (!db_res_step(res, err_out)) {
        goto cleanup;
    }

    *player_id_out = db_res_col_i64(res, 0, err_out);
    if (err_out->code != 0) {
        goto cleanup;
    }

    db_res_finalize(res);
    return 0;

cleanup:
    if (res) db_res_finalize(res);
    return err_out->code ? err_out->code : -1;
}

int repo_auth_insert_initial_turns(db_t *db, const char *now_ts, int player_id) {
    db_error_t err;
    char sql_turns_tmpl[512];
    /* SQL_VERBATIM: Q11 */
    snprintf(sql_turns_tmpl, sizeof(sql_turns_tmpl),
        "INSERT INTO turns (player_id, turns_remaining, last_update) VALUES ({1}, 750, %s);",
        now_ts);
    char sql[512]; sql_build (db, sql_turns_tmpl, sql, sizeof (sql));
    if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i32 (player_id) }, 1, &err)) return err.code;
    return 0;
}

int repo_auth_update_player_credits(db_t *db, int credits, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q12 */
    const char *q12 = "UPDATE players SET credits = {1} WHERE player_id = {2};";
    char sql[512]; sql_build(db, q12, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(credits), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_auth_upsert_news_sub(db_t *db, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q13 */
    const char *q13 = "INSERT INTO subscriptions(player_id,event_type,delivery,enabled) VALUES({1},'news.*','push',1);";
    char sql[512]; sql_build(db, q13, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &err)) {
        if (err.code == ERR_DB_CONSTRAINT) {
            return 0; // DO NOTHING
        }
        return err.code;
    }
    return 0;
}

int repo_auth_check_username_exists(db_t *db, const char *username, int *exists_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q14 */
    const char *q14 = "SELECT 1 FROM players WHERE name = {1};";
    char sql[512]; sql_build(db, q14, sql, sizeof(sql));
    *exists_out = 0;
    if (db_query (db, sql, (db_bind_t[]){ db_bind_text(username) }, 1, &res, &err) && db_res_step(res, &err)) {
        *exists_out = 1;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code;
}
