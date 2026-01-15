#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_players.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int repo_players_set_pref(db_t *db, int player_id, const char *key, const char *type, const char *value) {
    db_error_t err;
    /* SQL_VERBATIM: Q1 */
    const char *q1 = "INSERT INTO player_prefs (player_id, key, type, value) VALUES ({1}, {2}, {3}, {4}) ON CONFLICT (player_id, key) DO UPDATE SET type = EXCLUDED.type, value = EXCLUDED.value";
    char sql[512]; sql_build(db, q1, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_text(key), db_bind_text(type), db_bind_text(value) }, 4, &err)) return err.code;
    return 0;
}

int repo_players_upsert_bookmark(db_t *db, int player_id, const char *name, int sector_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q2 */
    const char *q2 = "INSERT INTO player_bookmarks (player_id, name, sector_id) VALUES ({1}, {2}, {3}) ON CONFLICT (player_id, name) DO UPDATE SET sector_id = EXCLUDED.sector_id";
    char sql[512]; sql_build(db, q2, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_text(name), db_bind_i32(sector_id) }, 3, &err)) return err.code;
    return 0;
}

int repo_players_delete_bookmark(db_t *db, int player_id, const char *name) {
    db_error_t err;
    /* SQL_VERBATIM: Q3 */
    const char *q3 = "DELETE FROM player_bookmarks WHERE player_id = {1} AND name = {2}";
    char sql[512]; sql_build(db, q3, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_text(name) }, 2, &err)) return err.code;
    return 0;
}

int repo_players_add_avoid(db_t *db, int player_id, int sector_id) {
    db_error_t err;
    const char *conflict_clause = sql_insert_ignore_clause(db);
    if (!conflict_clause) return -1;
    char sql_template[256];
    /* SQL_VERBATIM: Q4 */
    snprintf(sql_template, sizeof(sql_template),
        "INSERT INTO player_avoid (player_id, sector_id) VALUES ({1}, {2}) %s",
        conflict_clause);
    char sql[256]; sql_build(db, sql_template, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i32(sector_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_players_delete_avoid(db_t *db, int player_id, int sector_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q5 */
    const char *q5 = "DELETE FROM player_avoid WHERE player_id = {1} AND sector_id = {2}";
    char sql[512]; sql_build(db, q5, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i32(sector_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_players_disable_subscription(db_t *db, int player_id, const char *topic) {
    db_error_t err;
    /* SQL_VERBATIM: Q6 */
    const char *q6 = "UPDATE player_subscriptions SET enabled = 0 WHERE player_id = {1} AND topic = {2}";
    char sql[512]; sql_build(db, q6, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_text(topic) }, 2, &err)) return err.code;
    return 0;
}

int repo_players_upsert_subscription(db_t *db, int player_id, const char *topic, const char *delivery, const char *filter) {
    db_error_t err;
    /* SQL_VERBATIM: Q7 */
    const char *q7 = "INSERT INTO player_subscriptions (player_id, topic, enabled, delivery, filter) VALUES ({1}, {2}, 1, {3}, {4}) ON CONFLICT (player_id, topic) DO UPDATE SET enabled = 1, delivery = EXCLUDED.delivery, filter = EXCLUDED.filter";
    char sql[512]; sql_build(db, q7, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_text(topic), db_bind_text(delivery ? delivery : "push"), db_bind_text(filter ? filter : "") }, 4, &err)) return err.code;
    return 0;
}

db_res_t* repo_players_get_prefs(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q9 */
    const char *q9 = "SELECT key, type, value FROM player_prefs WHERE player_id = {1}";
    char sql[512]; sql_build(db, q9, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_players_get_bookmarks(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q10 */
    const char *q10 = "SELECT name, sector_id FROM player_bookmarks WHERE player_id={1} ORDER BY name";
    char sql[512]; sql_build(db, q10, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_players_get_avoids(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q11 */
    const char *q11 = "SELECT sector_id FROM player_avoid WHERE player_id={1} ORDER BY sector_id";
    char sql[512]; sql_build(db, q11, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_players_get_subscriptions(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q12 */
    const char *q12 = "SELECT topic, locked, enabled, delivery, filter FROM player_subscriptions WHERE player_id={1}";
    char sql[512]; sql_build(db, q12, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_players_get_my_info(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q13 */
    const char *q13 = "SELECT p.name, p.credits, t.turns_remaining, p.sector_id, p.ship_id, p.alignment, p.experience, cm.corporation_id FROM players p LEFT JOIN turns t ON t.player_id = p.player_id LEFT JOIN corp_members cm ON cm.player_id = p.player_id WHERE p.player_id = {1}";
    char sql[1024]; sql_build(db, q13, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_players_get_title_info(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q14 */
    const char *q14 = "SELECT alignment, experience, commission_id FROM players WHERE player_id = {1};";
    char sql[512]; sql_build(db, q14, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, err);
    return res;
}

int repo_players_send_mail(db_t *db, int sender_id, int recipient_id, const char *subject, const char *message) {
    db_error_t err;
    /* SQL_VERBATIM: Q15 */
    const char *q15 = "INSERT INTO mail (sender_id, recipient_id, subject, body) VALUES ({1}, {2}, {3}, {4});";
    char sql[1024]; sql_build(db, q15, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(sender_id), db_bind_i32(recipient_id), db_bind_text(subject), db_bind_text(message) }, 4, &err)) return err.code;
    return 0;
}

int repo_players_get_cargo_free(db_t *db, int player_id, int *free_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q16 */
    const char *q16 = "SELECT (COALESCE(s.holds, 0) - COALESCE(s.colonists + s.equipment + s.organics + s.ore + s.slaves + s.weapons + s.drugs, 0)) FROM players p JOIN ships s ON s.ship_id = p.ship_id WHERE p.player_id = {1};";
    char sql[1024]; sql_build(db, q16, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *free_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int repo_players_is_npc(db_t *db, int player_id, int *is_npc_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q17 */
    const char *q17 = "SELECT is_npc FROM players WHERE player_id = {1};";
    char sql[512]; sql_build(db, q17, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *is_npc_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int repo_players_get_shiptype_by_name(db_t *db, const char *name, int *id, int *holds, int *fighters, int *shields) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q18 */
    const char *q18 = "SELECT id, initialholds, maxfighters, maxshields FROM shiptypes WHERE name = {1};";
    char sql[512]; sql_build(db, q18, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(name) }, 1, &res, &err) && db_res_step(res, &err)) {
        *id = db_res_col_i32(res, 0, &err);
        *holds = db_res_col_i32(res, 1, &err);
        *fighters = db_res_col_i32(res, 2, &err);
        *shields = db_res_col_i32(res, 3, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int repo_players_insert_ship(db_t *db, const char *name, int type_id, int holds, int fighters, int shields, int sector_id, int *ship_id_out) {
    db_error_t err;
    int64_t new_id = 0;
    /* SQL_VERBATIM: Q19 */
    const char *q19 = "INSERT INTO ships (name, type_id, holds, fighters, shields, sector) VALUES ({1}, {2}, {3}, {4}, {5}, {6});";
    char sql[512]; sql_build(db, q19, sql, sizeof(sql));
    if (db_exec_insert_id(db, sql, (db_bind_t[]){ db_bind_text(name), db_bind_i32(type_id), db_bind_i32(holds), db_bind_i32(fighters), db_bind_i32(shields), db_bind_i32(sector_id) }, 6, "id", &new_id, &err)) {
        *ship_id_out = (int)new_id;
        return 0;
    }
    return -1;
}

int repo_players_set_ship_ownership(db_t *db, int ship_id, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q20 */
    const char *q20 = "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES ({1}, {2}, 1, 1);";
    char sql[512]; sql_build(db, q20, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(ship_id), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_players_update_ship_and_sector(db_t *db, int player_id, int ship_id, int sector_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q21 */
    const char *q21 = "UPDATE players SET ship_id = {1}, sector_id = {2} WHERE player_id = {3};";
    char sql[512]; sql_build(db, q21, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(ship_id), db_bind_i32(sector_id), db_bind_i32(player_id) }, 3, &err)) return err.code;
    return 0;
}

int repo_players_update_podded_status(db_t *db, int player_id, const char *status) {
    db_error_t err;
    /* SQL_VERBATIM: Q22 */
    const char *q22 = "UPDATE podded_status SET status = {1} WHERE player_id = {2};";
    char sql[512]; sql_build(db, q22, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_text(status), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_players_get_credits(db_t *db, int player_id, long long *credits_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q23 */
    const char *q23 = "SELECT credits FROM players WHERE player_id = {1};";
    char sql[512]; sql_build(db, q23, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        if (credits_out) *credits_out = db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int repo_players_deduct_credits_returning(db_t *db, int player_id, long long amount, long long *new_balance_out) {
    db_error_t err;
    int64_t rows = 0;
    /* SQL_VERBATIM: Q24 */
    const char *q24 = "UPDATE players SET credits = credits - {1} WHERE player_id = {2} AND credits >= {1};";
    char sql[512]; sql_build(db, q24, sql, sizeof(sql));
    if (db_exec_rows_affected(db, sql, (db_bind_t[]){ db_bind_i64(amount), db_bind_i32(player_id) }, 2, &rows, &err)) {
        if (rows > 0) {
            if (new_balance_out) {
                return repo_players_get_credits(db, player_id, new_balance_out);
            }
            return 0;
        }
    }
    return -1;
}

int repo_players_add_credits_returning(db_t *db, int player_id, long long amount, long long *new_balance_out) {
    db_error_t err;
    int64_t rows = 0;
    /* SQL_VERBATIM: Q25 */
    const char *q25 = "UPDATE players SET credits = credits + {1} WHERE player_id = {2};";
    char sql[512]; sql_build(db, q25, sql, sizeof(sql));
    if (db_exec_rows_affected(db, sql, (db_bind_t[]){ db_bind_i64(amount), db_bind_i32(player_id) }, 2, &rows, &err)) {
        if (rows > 0) {
            if (new_balance_out) {
                return repo_players_get_credits(db, player_id, new_balance_out);
            }
            return 0;
        }
    }
    return -1;
}

int repo_players_get_turns(db_t *db, int player_id, int *turns_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q26 */
    const char *q26 = "SELECT turns_remaining FROM turns WHERE player_id = {1};";
    char sql[512]; sql_build(db, q26, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *turns_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int repo_players_consume_turns(db_t *db, int player_id, int turns) {
    db_error_t err;
    /* SQL_VERBATIM: Q27 */
    const char *q27 = "UPDATE turns SET turns_remaining = turns_remaining - {1}, last_update = NOW() WHERE player_id = {2} AND turns_remaining >= {1};";
    char sql[512]; sql_build(db, q27, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i32(turns), db_bind_i32(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_players_get_align_exp(db_t *db, int player_id, int *align_out, long long *exp_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q28 */
    const char *q28 = "SELECT alignment, experience FROM players WHERE player_id = {1};";
    char sql[512]; sql_build(db, q28, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *align_out = db_res_col_i32(res, 0, &err);
        *exp_out = db_res_col_i64(res, 1, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int repo_players_update_align_exp(db_t *db, int player_id, int align, long long exp) {
    db_error_t err;
    /* SQL_VERBATIM: Q29 */
    const char *q29 = "UPDATE players SET experience = {1}, alignment = {2} WHERE player_id = {3};";
    char sql[512]; sql_build(db, q29, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(exp), db_bind_i32(align), db_bind_i32(player_id) }, 3, &err)) return err.code;
    return 0;
}

int repo_players_get_sector(db_t *db, int player_id, int *sector_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q30 */
    const char *q30 = "SELECT COALESCE(sector_id, 0) FROM players WHERE player_id = {1};";
    char sql[512]; sql_build(db, q30, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *sector_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int repo_players_update_credits_safe(db_t *db, int player_id, long long delta, long long *new_balance_out) {
    db_error_t err;
    int64_t rows = 0;
    /* SQL_VERBATIM: Q32 */
    const char *q32 = "UPDATE players SET credits = credits + {2} WHERE player_id = {1} AND (credits + {2}) >= 0;";
    char sql[512]; sql_build(db, q32, sql, sizeof(sql));
    if (db_exec_rows_affected(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i64(delta) }, 2, &rows, &err)) {
        if (rows > 0) {
            if (new_balance_out) {
                return repo_players_get_credits(db, player_id, new_balance_out);
            }
            return 0;
        }
    }
    return err.code ? err.code : -1;
}

int repo_players_check_exists(db_t *db, int player_id, int *exists_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q33 */
    const char *q33 = "SELECT 1 FROM players WHERE player_id = {1} LIMIT 1;";
    char sql[512]; sql_build(db, q33, sql, sizeof(sql));
    *exists_out = 0;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *exists_out = 1;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code;
}
