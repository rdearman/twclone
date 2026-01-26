#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_sysop.h"
#include "db/sql_driver.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int repo_sysop_audit(db_t *db, int actor_id, const char *cmd_type, const char *details_param, int64_t *new_id_out) {
    db_error_t err;
    /* timestamp handled in C */
    time_t now = time(NULL);
    
    const char *sql_tmpl = "INSERT INTO engine_audit (ts, actor_player_id, cmd_type, details) "
                           "VALUES ({1}, {2}, {3}, {4})";
    
    char sql[512];
    sql_build(db, sql_tmpl, sql, sizeof(sql));

    db_bind_t params[4];
    params[0] = db_bind_i64(now);
    params[1] = db_bind_i64(actor_id);
    params[2] = db_bind_text(cmd_type);
    params[3] = db_bind_text(details_param);


    if (!db_exec_insert_id(db, sql, params, 4, "engine_audit_id", new_id_out, &err)) {
        return err.code;
    }
    return 0;
}

db_res_t* repo_sysop_audit_tail(db_t *db, int limit, db_error_t *err) {
    /* SQL_VERBATIM: Q_SYSOP_1 */
    const char *q = "SELECT engine_audit_id, ts, actor_player_id, cmd_type, details "
                    "FROM engine_audit ORDER BY engine_audit_id DESC LIMIT {1}";
    
    char sql[512];
    sql_build(db, q, sql, sizeof(sql));
    
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(limit) }, 1, &res, err);
    return res;
}

db_res_t* repo_sysop_search_players(db_t *db, const char *query, int limit, db_error_t *err) {
    /* SQL_VERBATIM: Q_SYS_3 */
    static const char *sql = 
        "SELECT player_id, name, type, loggedin, sector_id "
        "FROM players "
        "WHERE name LIKE {1} "
        "ORDER BY name ASC "
        "LIMIT {2};";

    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));
    
    /* Prepare wildcard query: %query% */
    char like_query[128];
    snprintf(like_query, sizeof(like_query), "%%%s%%", query);

    db_res_t *res = NULL;
    db_query(db, sql_converted, (db_bind_t[]){ db_bind_text(like_query), db_bind_i64(limit) }, 2, &res, err);
    return res;
}

db_res_t* repo_sysop_get_player_basic(db_t *db, int player_id, db_error_t *err) {
    /* SQL_VERBATIM: Q_SYS_4 */
    static const char *sql = 
        "SELECT p.player_id, p.name, p.credits, t.turns_remaining, p.sector_id, p.ship_id, p.type, p.is_npc, p.loggedin "
        "FROM players p "
        "LEFT JOIN turns t ON t.player_id = p.player_id "
        "WHERE p.player_id = {1};";

    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, err);
    return res;
}

db_res_t* repo_sysop_get_player_sessions(db_t *db, int player_id, int limit, db_error_t *err) {
    /* SQL_VERBATIM: Q_SYS_5 */
    /* Note: sessions table schema assumed based on repo_session.c usage: token, player_id, expires */
    /* We return token (masked), expires */
    static const char *sql = 
        "SELECT token, expires "
        "FROM sessions "
        "WHERE player_id = {1} "
        "LIMIT {2};";

    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(player_id), db_bind_i64(limit) }, 2, &res, err);
    return res;
}

db_res_t* repo_sysop_get_universe_summary(db_t *db, db_error_t *err) {
    /* SQL_VERBATIM: Q_SYS_SUMMARY */
    static const char *sql = 
        "SELECT "
        "  (SELECT COUNT(*) FROM sectors) as sectors, "
        "  (SELECT COUNT(*) FROM sector_warps) as warps, "
        "  (SELECT COUNT(*) FROM ports) as ports, "
        "  (SELECT COUNT(*) FROM planets) as planets, "
        "  (SELECT COUNT(*) FROM players) as players, "
        "  (SELECT COUNT(*) FROM ships) as ships;";

    char sql_converted[1024];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_query(db, sql_converted, NULL, 0, &res, err);
    return res;
}

/* Phase 3: Engine & Jobs */

db_res_t* repo_sysop_get_engine_status(db_t *db, db_error_t *err) {
    /* SQL_VERBATIM: Q_SYS_6 */
    /* Returns last_event_id processed vs current max event_id. 
       We use COALESCE and a more robust query to handle missing rows. */
    static const char *sql = 
        "SELECT 'consumer' as component, COALESCE(last_event_id, 0), (SELECT COALESCE(MAX(engine_events_id), 0) FROM engine_events) as max_event_id "
        "FROM (SELECT 1) dummy "
        "LEFT JOIN engine_offset ON key = 'main_consumer' "
        "UNION ALL "
        "SELECT 'commands' as component, (SELECT COUNT(*) FROM engine_commands WHERE status='finished') as last_event_id, "
        "       (SELECT COUNT(*) FROM engine_commands WHERE status='ready') as max_event_id "
        "FROM (SELECT 1) dummy;";

    char sql_converted[1024];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_query(db, sql_converted, NULL, 0, &res, err);
    return res;
}

db_res_t* repo_sysop_list_jobs(db_t *db, int limit, db_error_t *err) {
    /* SQL_VERBATIM: Q_SYS_7 */
    static const char *sql = 
        "SELECT engine_commands_id, type, status, attempts, created_at, due_at "
        "FROM engine_commands "
        "ORDER BY engine_commands_id DESC "
        "LIMIT {1};";

    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(limit) }, 1, &res, err);
    return res;
}

db_res_t* repo_sysop_get_job(db_t *db, int64_t job_id, db_error_t *err) {
    /* SQL_VERBATIM: Q_SYS_8 */
    static const char *sql = 
        "SELECT engine_commands_id, type, payload, status, attempts, created_at, due_at, started_at, finished_at, worker "
        "FROM engine_commands "
        "WHERE engine_commands_id = {1};";

    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(job_id) }, 1, &res, err);
    return res;
}

int repo_sysop_retry_job(db_t *db, int64_t job_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q_SYS_9 */
    const char *sql_raw = "UPDATE engine_commands SET status = 'ready', attempts = 0 WHERE engine_commands_id = {1};";
    char sql[512]; sql_build(db, sql_raw, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(job_id) }, 1, &err)) return err.code;
    return 0;
}

int repo_sysop_cancel_job(db_t *db, int64_t job_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q_SYS_10 */
    const char *sql_raw = "UPDATE engine_commands SET status = 'cancelled' WHERE engine_commands_id = {1};";
    char sql[512]; sql_build(db, sql_raw, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(job_id) }, 1, &err)) return err.code;
    return 0;
}
