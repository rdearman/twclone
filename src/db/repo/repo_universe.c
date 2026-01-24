#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_universe.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int repo_universe_log_engine_event(db_t *db, const char *type, int sector_id, const char *payload) {
    db_error_t err;
    int64_t now_ts = (int64_t)time(NULL);
    /* SQL_VERBATIM: Q1 */
    const char *q1 = "INSERT INTO engine_events(type, sector_id, payload, ts) VALUES ({1}, {2}, {3}, {4})";
    char sql[1024]; sql_build(db, q1, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_text(type), db_bind_i64(sector_id), db_bind_text(payload), db_bind_timestamp_text(now_ts) }, 4, &err)) return err.code;
    return 0;
}

db_res_t* repo_universe_get_adjacent_sectors(db_t *db, int sector_id, db_error_t *err) {
    /* SQL_VERBATIM: Q2 */
    const char *q2 = "SELECT to_sector FROM sector_warps WHERE from_sector={1};";
    char sql[512]; sql_build(db, q2, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(sector_id) }, 1, &res, err);
    return res;
}

int repo_universe_get_random_neighbor(db_t *db, int sector_id, int *neighbor_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q3 */
    const char *q3 = "SELECT to_sector FROM sector_warps WHERE from_sector={1} ORDER BY RANDOM() LIMIT 1;";
    char sql[512]; sql_build(db, q3, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(sector_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *neighbor_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_universe_update_ship_sector(db_t *db, int ship_id, int sector_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q4 */
    const char *q4 = "UPDATE ships SET sector_id = {1} WHERE ship_id = {2};";
    char sql[512]; sql_build(db, q4, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(sector_id), db_bind_i64(ship_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_universe_mass_randomize_zero_sector_ships(db_t *db) {
    db_error_t err;
    const char *sql;
    if (db_backend(db) == DB_BACKEND_POSTGRES) {
        /* SQL_VERBATIM: Q5 */
        sql = "UPDATE ships SET sector_id = floor(random() * 90) + 11 WHERE sector_id = 0;";
    } else {
        /* SQL_VERBATIM: Q6 */
        sql = "UPDATE ships SET sector_id = ABS(RANDOM() % 90) + 11 WHERE sector_id = 0;";
    }
    if (!db_exec(db, sql, NULL, 0, &err)) return err.code;
    return 0;
}

db_res_t* repo_universe_get_orion_ships(db_t *db, int owner_id, db_error_t *err) {
    /* SQL_VERBATIM: Q7 */
    const char *q7 = "SELECT s.id, s.sector, s.target_sector FROM ships s JOIN ship_ownership so ON s.id = so.ship_id WHERE so.player_id = {1};";
    char sql[1024]; sql_build(db, q7, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(owner_id) }, 1, &res, err);
    return res;
}

int repo_universe_update_ship_target(db_t *db, int ship_id, int target_sector) {
    db_error_t err;
    /* SQL_VERBATIM: Q8 */
    const char *q8 = "UPDATE ships SET target_sector = {1} WHERE ship_id = {2};";
    char sql[512]; sql_build(db, q8, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(target_sector), db_bind_i64(ship_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_universe_get_corp_owner_by_tag(db_t *db, const char *tag, int *owner_id_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q9 */
    const char *q9 = "SELECT owner_id FROM corporations WHERE tag={1};";
    char sql[512]; sql_build(db, q9, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(tag) }, 1, &res, &err) && db_res_step(res, &err)) {
        *owner_id_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_universe_get_port_sector_by_id_name(db_t *db, int port_id, const char *name, int *sector_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q10 */
    const char *q10 = "SELECT sector_id FROM ports WHERE port_id={1} AND name={2};";
    char sql[512]; sql_build(db, q10, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(port_id), db_bind_text(name) }, 2, &res, &err) && db_res_step(res, &err)) {
        *sector_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

db_res_t* repo_universe_search_index(db_t *db, const char *q, int limit, int offset, int search_type, db_error_t *err) {
    const char *op = sql_ilike_op(db);
    char q_tmpl[1024];
    if (search_type == 0) { /* type_any */
        /* SQL_VERBATIM: Q11 */
        snprintf(q_tmpl, sizeof(q_tmpl), "SELECT kind, id, name, sector_id, sector_name FROM sector_search_index WHERE (({1} = '') OR (search_term_1 %s {1})) ORDER BY kind, name, id LIMIT {2} OFFSET {3}", op);
    } else if (search_type == 1) { /* type_sector */
        /* SQL_VERBATIM: Q12 */
        snprintf(q_tmpl, sizeof(q_tmpl), "SELECT kind, id, name, sector_id, sector_name FROM sector_search_index WHERE kind = 'sector' AND (({1} = '') OR (search_term_1 %s {1})) ORDER BY kind, name, id LIMIT {2} OFFSET {3}", op);
    } else { /* type_port */
        /* SQL_VERBATIM: Q13 */
        snprintf(q_tmpl, sizeof(q_tmpl), "SELECT kind, id, name, sector_id, sector_name FROM sector_search_index WHERE kind = 'port' AND (({1} = '') OR (search_term_1 %s {1})) ORDER BY kind, name, id LIMIT {2} OFFSET {3}", op);
    }
    char sql[1024]; sql_build(db, q_tmpl, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_text(q), db_bind_i64(limit), db_bind_i64(offset) }, 3, &res, err);
    return res;
}

db_res_t* repo_universe_get_density_sector_list(db_t *db, int target_sector, db_error_t *err) {
    /* SQL_VERBATIM: Q14 */
    char cast_fragment[64];
    if (sql_cast_int(db, "{1}", cast_fragment, sizeof(cast_fragment)) != 0) {
        if (err) err->code = ERR_DB_INTERNAL;
        return NULL;
    }
    char q_tmpl[1024];
    snprintf(q_tmpl, sizeof(q_tmpl), "WITH sector_list AS ( SELECT %s as sector_id UNION SELECT to_sector FROM sector_warps WHERE from_sector = {1} ) SELECT DISTINCT sector_id FROM sector_list ORDER BY sector_id;", cast_fragment);
    char sql[1024]; sql_build(db, q_tmpl, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(target_sector) }, 1, &res, err);
    return res;
}

int repo_universe_get_sector_density(db_t *db, int sector_id, int *density_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q15 */
    const char *q15 = "SELECT COALESCE((SELECT SUM(quantity) FROM sector_assets WHERE sector_id = {1} AND asset_type = 2), 0) + COALESCE((SELECT SUM(quantity) FROM sector_assets WHERE sector_id = {1} AND (asset_type = 1 OR asset_type = 4)), 0) + CASE WHEN EXISTS(SELECT 1 FROM sectors WHERE sector_id = {1} AND beacon IS NOT NULL) THEN 1 ELSE 0 END + (SELECT COALESCE(COUNT(*), 0) * 10 FROM ships WHERE sector_id = {1}) + (SELECT COALESCE(COUNT(*), 0) * 100 FROM planets WHERE sector_id = {1}) + (SELECT COALESCE(COUNT(*), 0) * 100 FROM ports WHERE sector_id = {1}) as total_density;";
    char sql[2048]; sql_build(db, q15, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(sector_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *density_out = (int)db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_universe_warp_exists(db_t *db, int from, int to, int *exists_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q16 */
    const char *q16 = "SELECT 1 FROM sector_warps WHERE from_sector = {1} AND to_sector = {2} LIMIT 1;";
    char sql[512]; sql_build(db, q16, sql, sizeof(sql));
    *exists_out = 0;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(from), db_bind_i64(to) }, 2, &res, &err) && db_res_step(res, &err)) {
        *exists_out = 1;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code;
}

db_res_t* repo_universe_get_interdictors(db_t *db, int sector_id, db_error_t *err) {
    /* SQL_VERBATIM: Q17 */
    const char *q17 = "SELECT p.owner_id, p.owner_type FROM planets p JOIN citadels c ON p.planet_id = c.planet_id WHERE p.sector_id = {1} AND c.level >= 6 AND c.interdictor > 0;";
    char sql[1024]; sql_build(db, q17, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(sector_id) }, 1, &res, err);
    return res;
}

int repo_universe_sector_has_port(db_t *db, int sector_id, int *has_port_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q18 */
    const char *q18 = "SELECT 1 FROM ports WHERE sector_id={1} LIMIT 1;";
    char sql[512]; sql_build(db, q18, sql, sizeof(sql));
    *has_port_out = 0;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(sector_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *has_port_out = 1;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code;
}

int repo_universe_get_max_sector_id(db_t *db, int *max_id_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q19 */
    const char *q19 = "SELECT MAX(sector_id) FROM sectors;";
    if (db_query(db, q19, NULL, 0, &res, &err) && db_res_step(res, &err)) {
        *max_id_out = (int)db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_universe_get_warp_count(db_t *db, int *count_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q20 */
    const char *q20 = "SELECT COUNT(*) FROM sector_warps;";
    if (db_query(db, q20, NULL, 0, &res, &err) && db_res_step(res, &err)) {
        *count_out = (int)db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

db_res_t* repo_universe_get_all_warps(db_t *db, db_error_t *err) {
    /* SQL_VERBATIM: Q21 */
    const char *q21 = "SELECT from_sector, to_sector FROM sector_warps;";
    db_res_t *res = NULL;
    db_query(db, q21, NULL, 0, &res, err);
    return res;
}

db_res_t* repo_universe_get_asset_counts(db_t *db, int sector_id, db_error_t *err) {
    /* SQL_VERBATIM: Q22 */
    const char *q22 = "SELECT asset_type, SUM(quantity) FROM sector_assets WHERE sector_id = {1} GROUP BY asset_type;";
    char sql[512]; sql_build(db, q22, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i64(sector_id) }, 1, &res, err);
    return res;
}

int repo_universe_set_beacon(db_t *db, int sector_id, const char *text) {
    db_error_t err;
    /* SQL_VERBATIM: Q23 */
    const char *q23 = "UPDATE sectors SET beacon = {1} WHERE sector_id = {2};";
    char sql[512]; sql_build(db, q23, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_text(text), db_bind_i64(sector_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_universe_check_transwarp(db_t *db, int ship_id, int *enabled_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q24 */
    const char *q24 = "SELECT 1 FROM ships WHERE ship_id = {1} AND has_transwarp = TRUE LIMIT 1;";
    char sql[512]; sql_build(db, q24, sql, sizeof(sql));
    *enabled_out = 0;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *enabled_out = 1;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code;
}

int repo_universe_update_player_sector(db_t *db, int player_id, int sector_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q25 */
    const char *q25 = "UPDATE players SET sector_id = {1} WHERE player_id = {2};";
    char sql[512]; sql_build(db, q25, sql, sizeof(sql));
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(sector_id), db_bind_i64(player_id) }, 2, &err)) return err.code;
    return 0;
}

int repo_universe_get_ferengi_corp_info(db_t *db, int *corp_id_out, int *player_id_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q26 */
    const char *q26 = "SELECT corporation_id, owner_id FROM corporations WHERE tag='FENG' LIMIT 1;";
    if (db_query(db, q26, NULL, 0, &res, &err) && db_res_step(res, &err)) {
        *corp_id_out = db_res_col_i32(res, 0, &err);
        *player_id_out = db_res_col_i32(res, 1, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_universe_get_ferengi_homeworld_sector(db_t *db, int *sector_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q27 */
    const char *q27 = "SELECT sector_id FROM planets WHERE planet_id=2 LIMIT 1;";
    if (db_query(db, q27, NULL, 0, &res, &err) && db_res_step(res, &err)) {
        *sector_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_universe_get_ferengi_warship_type_id(db_t *db, int *type_id_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q28 */
    const char *q28 = "SELECT shiptypes_id FROM shiptypes WHERE name='Ferengi Warship' LIMIT 1;";
    if (db_query(db, q28, NULL, 0, &res, &err) && db_res_step(res, &err)) {
        *type_id_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}

int repo_universe_get_random_wormhole_neighbor(db_t *db, int sector_id, int *neighbor_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q29 */
    const char *q29 = "SELECT to_sector_id FROM wormholes WHERE from_sector_id = {1} ORDER BY RANDOM() LIMIT 1;";
    char sql[512]; sql_build(db, q29, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(sector_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *neighbor_out = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return err.code ? err.code : -1;
}
