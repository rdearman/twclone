#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_players.h"
#include "repo_universe.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int repo_players_set_pref(db_t *db, int player_id, const char *key, const char *type, const char *value) {
    db_error_t err;
    int64_t rows = 0;
    db_bind_t params[] = { db_bind_i64(player_id), db_bind_text(key), db_bind_text(type), db_bind_text(value) };

    /* 1. Try Update first */
    const char *q_upd = "UPDATE player_prefs SET type = {3}, value = {4} WHERE player_prefs_id = {1} AND key = {2}";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    if (db_exec_rows_affected(db, sql_upd, params, 4, &rows, &err) && rows > 0) return 0;

    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO player_prefs (player_prefs_id, key, type, value) VALUES ({1}, {2}, {3}, {4})";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, params, 4, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, params, 4, &err)) return 0;
        }
        return err.code;
    }
    return 0;
}

int repo_players_upsert_bookmark(db_t *db, int player_id, const char *name, int sector_id) {
    db_error_t err;
    int64_t rows = 0;
    db_bind_t params[] = { db_bind_i64(player_id), db_bind_text(name), db_bind_i64(sector_id) };

    /* 1. Try Update first */
    const char *q_upd = "UPDATE player_bookmarks SET sector_id = {3} WHERE player_id = {1} AND name = {2}";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    if (db_exec_rows_affected(db, sql_upd, params, 3, &rows, &err) && rows > 0) return 0;

    /* 2. Try Insert */
    const char *q_ins = "INSERT INTO player_bookmarks (player_id, name, sector_id) VALUES ({1}, {2}, {3})";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, params, 3, &err)) {
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, params, 3, &err)) return 0;
        }
        return err.code;
    }
    return 0;
}

int repo_players_delete_bookmark(db_t *db, int player_id, const char *name) {
    db_error_t err;
    /* SQL_VERBATIM: Q3 */
    const char *q3 = "DELETE FROM player_bookmarks WHERE player_id = {1} AND name = {2}";
    char sql[512]; sql_build(db, q3, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(player_id), db_bind_text(name) }, 2, &err)) return err.code;
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
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(player_id), db_bind_i64(sector_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_players_delete_avoid(db_t *db, int player_id, int sector_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q5 */
    const char *q5 = "DELETE FROM player_avoid WHERE player_id = {1} AND sector_id = {2}";
    char sql[512]; sql_build(db, q5, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(player_id), db_bind_i64(sector_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_players_disable_subscription(db_t *db, int player_id, const char *topic) {
    db_error_t err;
    /* SQL_VERBATIM: Q6 */
    const char *q6 = "UPDATE player_subscriptions SET enabled = {3} WHERE player_id = {1} AND topic = {2}";
    char sql[512]; sql_build(db, q6, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(player_id), db_bind_text(topic), db_bind_bool(false) }, 3, &err)) return err.code;
    return 0;
}

int repo_players_upsert_subscription(db_t *db, int player_id, const char *topic, const char *delivery, const char *filter) {
    db_error_t err;
    int64_t rows = 0;
    db_bind_t params[] = { 
        db_bind_i64(player_id), 
        db_bind_text(topic), 
        db_bind_text(delivery ? delivery : "push"), 
        db_bind_text(filter ? filter : "") 
    };

    /* 1. Try Update first */
    const char *q_upd = "UPDATE player_subscriptions SET enabled = TRUE, delivery = {3}, filter = {4} WHERE player_id = {1} AND topic = {2}";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    if (db_exec_rows_affected(db, sql_upd, params, 4, &rows, &err) && rows > 0) return 0;

    /* 2. Try Insert */
    const char *q_ins = "INSERT INTO player_subscriptions (player_id, topic, enabled, delivery, filter) VALUES ({1}, {2}, TRUE, {3}, {4})";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, params, 4, &err)) {
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, params, 4, &err)) return 0;
        }
        return err.code;
    }
    return 0;
}

db_res_t* repo_players_get_prefs(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q9 */
    const char *q9 = "SELECT key, type, value FROM player_prefs WHERE player_prefs_id = {1}";
    char sql[512]; sql_build(db, q9, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_players_get_bookmarks(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q10 */
    const char *q10 = "SELECT name, sector_id FROM player_bookmarks WHERE player_id={1} ORDER BY name";
    char sql[512]; sql_build(db, q10, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_players_get_avoids(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q11 */
    const char *q11 = "SELECT sector_id FROM player_avoid WHERE player_id={1} ORDER BY sector_id";
    char sql[512]; sql_build(db, q11, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_players_get_subscriptions(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q12 */
    const char *q12 = "SELECT topic, locked, enabled, delivery, filter FROM player_subscriptions WHERE player_id={1}";
    char sql[512]; sql_build(db, q12, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_players_get_my_info(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q13 */
    const char *q13 = "SELECT p.name, p.credits, t.turns_remaining, p.sector_id, p.ship_id, p.alignment, p.experience, cm.corporation_id FROM players p LEFT JOIN turns t ON t.player_id = p.player_id LEFT JOIN corp_members cm ON cm.player_id = p.player_id WHERE p.player_id = {1}";
    char sql[1024]; sql_build(db, q13, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_players_get_title_info(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q14 */
    const char *q14 = "SELECT alignment, experience, commission_id FROM players WHERE player_id = {1};";
    char sql[512]; sql_build(db, q14, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, err);
    return res;
}

int repo_players_send_mail(db_t *db, int sender_id, int recipient_id, const char *subject, const char *message) {
    db_error_t err;
    /* Map sender_id 0 to System (1) to satisfy FK constraint */
    int sid = (sender_id <= 0) ? 1 : sender_id;
    /* SQL_VERBATIM: Q15 */
    const char *q15 = "INSERT INTO mail (sender_id, recipient_id, subject, body) VALUES ({1}, {2}, {3}, {4});";
    char sql[1024]; sql_build(db, q15, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(sid), db_bind_i64(recipient_id), db_bind_text(subject), db_bind_text(message) }, 4, &err)) return err.code;
    return 0;
}

int repo_players_get_cargo_free(db_t *db, int player_id, int *free_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q16 */
    const char *q16 = "SELECT (COALESCE(s.holds, 0) - COALESCE(s.colonists + s.equipment + s.organics + s.ore + s.slaves + s.weapons + s.drugs, 0)) FROM players p JOIN ships s ON s.ship_id = p.ship_id WHERE p.player_id = {1};";
    char sql[1024]; sql_build(db, q16, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
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
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
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
    const char *q18 = "SELECT shiptypes_id, initialholds, maxfighters, maxshields FROM shiptypes WHERE name = {1};";
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
    const char *q19 = "INSERT INTO ships (name, type_id, holds, fighters, shields, sector) VALUES ({1}, {2}, {3}, {4}, {5}, {6})";
    char sql[512]; sql_build(db, q19, sql, sizeof(sql));
    if (db_exec_insert_id(db, sql, (db_bind_t[]){ db_bind_text(name), db_bind_i64(type_id), db_bind_i64(holds), db_bind_i64(fighters), db_bind_i64(shields), db_bind_i64(sector_id) }, 6, "ship_id", &new_id, &err)) {
        *ship_id_out = (int)new_id;
        return 0;
    }
    return -1;
}

int repo_players_set_ship_ownership(db_t *db, int ship_id, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q20 */
    const char *q20 = "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES ({1}, {2}, 1, TRUE);";
    char sql[512]; sql_build(db, q20, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(ship_id), db_bind_i64(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_players_update_ship_and_sector(db_t *db, int player_id, int ship_id, int sector_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q21 */
    const char *q21 = "UPDATE players SET ship_id = {1}, sector_id = {2} WHERE player_id = {3};";
    char sql[512]; sql_build(db, q21, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(ship_id), db_bind_i64(sector_id), db_bind_i64(player_id) }, 3, &err)) return err.code;
    return 0;
}

int repo_players_update_podded_status(db_t *db, int player_id, const char *status) {
    db_error_t err;
    /* SQL_VERBATIM: Q22 */
    const char *q22 = "UPDATE podded_status SET status = {1} WHERE player_id = {2};";
    char sql[512]; sql_build(db, q22, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_text(status), db_bind_i64(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_players_get_credits(db_t *db, int player_id, long long *credits_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q23 */
    const char *q23 = "SELECT credits FROM players WHERE player_id = {1};";
    char sql[512]; sql_build(db, q23, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
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
    if (db_exec_rows_affected(db, sql, (db_bind_t[]){ db_bind_i64(amount), db_bind_i64(player_id) }, 2, &rows, &err)) {
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
    if (db_exec_rows_affected(db, sql, (db_bind_t[]){ db_bind_i64(amount), db_bind_i64(player_id) }, 2, &rows, &err)) {
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
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *turns_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int repo_players_consume_turns(db_t *db, int player_id, int turns) {
    db_error_t err;
    int64_t now_ts = (int64_t)time(NULL);
    /* SQL_VERBATIM: Q27 */
    const char *q27 = "UPDATE turns SET turns_remaining = turns_remaining - {1}, last_update = {3} WHERE player_id = {2} AND turns_remaining >= {1};";
    char sql[512]; sql_build(db, q27, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(turns), db_bind_i64(player_id), db_bind_timestamp_text(now_ts) }, 3, &err)) return err.code;
    return 0;
}

int repo_players_get_align_exp(db_t *db, int player_id, int *align_out, long long *exp_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q28 */
    const char *q28 = "SELECT alignment, experience FROM players WHERE player_id = {1};";
    char sql[512]; sql_build(db, q28, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
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
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(exp), db_bind_i64(align), db_bind_i64(player_id) }, 3, &err)) return err.code;
    return 0;
}

int repo_players_get_sector(db_t *db, int player_id, int *sector_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q30 */
    const char *q30 = "SELECT COALESCE(sector_id, 0) FROM players WHERE player_id = {1};";
    char sql[512]; sql_build(db, q30, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
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
    if (db_exec_rows_affected(db, sql, (db_bind_t[]){ db_bind_i64(player_id), db_bind_i64(delta) }, 2, &rows, &err)) {
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
    /* SQL_VERBATIM: Qn */
    const char *qn = "SELECT 1 FROM players WHERE player_id = {1}";
    char sql[512]; sql_build(db, qn, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *exists_out = 1;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    *exists_out = 0;
    return err.code ? err.code : 0;
}

int repo_players_record_port_knowledge(db_t *db, int player_id, int port_id) {
    db_error_t err;
    db_bind_t params[] = { db_bind_i64(player_id), db_bind_i64(port_id) };

    const char *q_upd = "UPDATE player_known_ports SET updated_at = CURRENT_TIMESTAMP WHERE player_id = {1} AND port_id = {2}";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    int64_t rows = 0;
    if (db_exec_rows_affected(db, sql_upd, params, 2, &rows, &err) && rows > 0) return 0;

    const char *q_ins = "INSERT INTO player_known_ports (player_id, port_id) VALUES ({1}, {2})";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, params, 2, &err)) {
        if (err.code == ERR_DB_CONSTRAINT) {
            db_exec(db, sql_upd, params, 2, &err);
        }
        return err.code;
    }
    return 0;
}

int repo_players_record_visit(db_t *db, int player_id, int sector_id) {
    db_error_t err;
    int64_t rows = 0;
    db_bind_t params[] = { db_bind_i64(player_id), db_bind_i64(sector_id) };

    const char *q_upd = "UPDATE player_visited_sectors SET visit_count = visit_count + 1, last_visited_at = CURRENT_TIMESTAMP WHERE player_id = {1} AND sector_id = {2}";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    if (db_exec_rows_affected(db, sql_upd, params, 2, &rows, &err) && rows > 0) return 0;

    const char *q_ins = "INSERT INTO player_visited_sectors (player_id, sector_id) VALUES ({1}, {2})";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, params, 2, &err)) {
        if (err.code == ERR_DB_CONSTRAINT) {
            db_exec(db, sql_upd, params, 2, &err);
        }
        return err.code;
    }
    return 0;
}

/* Adjacency list for BFS */
typedef struct {
    int *head;
    int *next;
    int *to;
    int nodes;
    int edges;
} repo_adj_list_t;

static int h_repo_players_bfs_fast(repo_adj_list_t *adj, int from, int to, int max_dist, int *dist, int *queue) {
    if (from == to) return 0;
    if (max_dist <= 0) return -1;
    if (from < 0 || from >= adj->nodes || to < 0 || to >= adj->nodes) return -1;

    for (int i = 0; i < adj->nodes; i++) dist[i] = -1;
    dist[from] = 0;
    queue[0] = from;
    int q_head = 0, q_tail = 1;

    while (q_head < q_tail) {
        int curr = queue[q_head++];
        if (dist[curr] >= max_dist) continue;

        for (int i = adj->head[curr]; i != -1; i = adj->next[i]) {
            int v = adj->to[i];
            if (dist[v] == -1) {
                dist[v] = dist[curr] + 1;
                if (v == to) return dist[v];
                queue[q_tail++] = v;
            }
        }
    }
    return -1;
}

int repo_players_get_recommended_routes(db_t *db, int player_id, int current_sector_id,
                                        int max_hops_between, int max_hops_from_player,
                                        int require_two_way,
                                        trade_route_t **out_routes, int *out_count,
                                        int *out_truncated, int *out_pairs_checked) {
    if (!db || !out_routes || !out_count) return -1;
    *out_routes = NULL;
    *out_count = 0;
    if (out_truncated) *out_truncated = 0;
    if (out_pairs_checked) *out_pairs_checked = 0;

    int max_id = 0;
    if (repo_universe_get_max_sector_id(db, &max_id) != 0) return -1;
    int nodes = max_id + 1;

    int warp_count = 0;
    if (repo_universe_get_warp_count(db, &warp_count) != 0) return -1;

    repo_adj_list_t adj;
    adj.nodes = nodes;
    adj.edges = warp_count;
    adj.head = malloc(nodes * sizeof(int));
    adj.next = malloc(warp_count * sizeof(int));
    adj.to = malloc(warp_count * sizeof(int));
    int *dist_buf = malloc(nodes * sizeof(int));
    int *queue_buf = malloc(nodes * sizeof(int));

    if (!adj.head || !adj.next || !adj.to || !dist_buf || !queue_buf) {
        free(adj.head); free(adj.next); free(adj.to);
        free(dist_buf); free(queue_buf);
        return -1;
    }

    for (int i = 0; i < nodes; i++) adj.head[i] = -1;
    
    db_error_t err;
    db_res_t *res_warps = repo_universe_get_all_warps(db, &err);
    int edge_idx = 0;
    if (res_warps) {
        while (db_res_step(res_warps, &err)) {
            int u = db_res_col_i32(res_warps, 0, &err);
            int v = db_res_col_i32(res_warps, 1, &err);
            if (u >= 0 && u < nodes && v >= 0 && v < nodes && edge_idx < warp_count) {
                adj.to[edge_idx] = v;
                adj.next[edge_idx] = adj.head[u];
                adj.head[u] = edge_idx++;
            }
        }
        db_res_finalize(res_warps);
    }

    db_res_t *res = NULL;
    /* SQL_VERBATIM: Q_ROUTES */
    const char *q_routes = 
        "SELECT kp1.port_id, kp2.port_id, p1.sector_id, p2.sector_id, "
        "SUM(CASE WHEN pt1.mode = 'sell' AND pt2.mode = 'buy' THEN 1 ELSE 0 END) as a_to_b_count, "
        "SUM(CASE WHEN pt2.mode = 'sell' AND pt1.mode = 'buy' THEN 1 ELSE 0 END) as b_to_a_count "
        "FROM player_known_ports kp1 "
        "JOIN player_known_ports kp2 ON kp1.port_id < kp2.port_id "
        "JOIN ports p1 ON kp1.port_id = p1.port_id "
        "JOIN ports p2 ON kp2.port_id = p2.port_id "
        "JOIN port_trade pt1 ON kp1.port_id = pt1.port_id "
        "JOIN port_trade pt2 ON kp2.port_id = pt2.port_id AND pt1.commodity = pt2.commodity "
        "WHERE kp1.player_id = {1} AND kp2.player_id = {1} "
        "GROUP BY kp1.port_id, kp2.port_id, p1.sector_id, p2.sector_id "
        "HAVING SUM(CASE WHEN pt1.mode = 'sell' AND pt2.mode = 'buy' THEN 1 ELSE 0 END) > 0 "
        "    OR SUM(CASE WHEN pt2.mode = 'sell' AND pt1.mode = 'buy' THEN 1 ELSE 0 END) > 0";

    char sql[2048]; sql_build(db, q_routes, sql, sizeof(sql));
    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err)) {
        free(adj.head); free(adj.next); free(adj.to);
        free(dist_buf); free(queue_buf);
        return err.code;
    }

    int capacity = 32;
    trade_route_t *routes = malloc(capacity * sizeof(trade_route_t));
    int count = 0;
    int pairs_checked = 0;
    int truncated = 0;
    const int MAX_PAIRS_CHECKED = 2000;

    while (db_res_step(res, &err)) {
        if (pairs_checked >= MAX_PAIRS_CHECKED) {
            truncated = 1;
            break;
        }
        pairs_checked++;

        int p_a = db_res_col_i32(res, 0, &err);
        int p_b = db_res_col_i32(res, 1, &err);
        int s_a = db_res_col_i32(res, 2, &err);
        int s_b = db_res_col_i32(res, 3, &err);
        int a_to_b = db_res_col_i32(res, 4, &err);
        int b_to_a = db_res_col_i32(res, 5, &err);

        if (require_two_way && (a_to_b == 0 || b_to_a == 0)) continue;

        /* Calculate distances */
        int dist_ab = h_repo_players_bfs_fast(&adj, s_a, s_b, max_hops_between, dist_buf, queue_buf);
        if (dist_ab == -1 || dist_ab > max_hops_between) continue;

        int dist_pa = h_repo_players_bfs_fast(&adj, current_sector_id, s_a, max_hops_from_player, dist_buf, queue_buf);
        int dist_pb = h_repo_players_bfs_fast(&adj, current_sector_id, s_b, max_hops_from_player, dist_buf, queue_buf);
        
        int dist_player = -1;
        if (dist_pa != -1 && dist_pb != -1) dist_player = (dist_pa < dist_pb) ? dist_pa : dist_pb;
        else if (dist_pa != -1) dist_player = dist_pa;
        else if (dist_pb != -1) dist_player = dist_pb;

        if (dist_player == -1 || dist_player > max_hops_from_player) continue;

        if (count >= capacity) {
            capacity *= 2;
            routes = realloc(routes, capacity * sizeof(trade_route_t));
        }

        routes[count].port_a_id = p_a;
        routes[count].port_b_id = p_b;
        routes[count].sector_a_id = s_a;
        routes[count].sector_b_id = s_b;
        routes[count].hops_between = dist_ab;
        routes[count].hops_from_player = dist_player;
        routes[count].is_two_way = (a_to_b > 0 && b_to_a > 0) ? 1 : 0;
        routes[count].commodity = NULL; 
        count++;
    }
    db_res_finalize(res);

    free(adj.head); free(adj.next); free(adj.to);
    free(dist_buf); free(queue_buf);

    *out_routes = routes;
    *out_count = count;
    if (out_truncated) *out_truncated = truncated;
    if (out_pairs_checked) *out_pairs_checked = pairs_checked;
    return 0;
}

void repo_players_free_routes(trade_route_t *routes, int count) {
    if (!routes) return;
    for (int i = 0; i < count; i++) {
        if (routes[i].commodity) free(routes[i].commodity);
    }
    free(routes);
}

