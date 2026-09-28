#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_universe.h"
#include "repo_engine.h"
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
    (void) owner_id;  /* Not used - we get Orion ships from corporation membership */
    /* SQL_VERBATIM: Q7 */
    const char *q7 = "SELECT DISTINCT s.ship_id, s.sector_id, s.personality, "
                     "c.ship_personality, st.default_personality "
                     "FROM ships s "
                     "LEFT JOIN shiptypes st ON st.shiptypes_id = s.type_id "
                     "JOIN ship_ownership so ON s.ship_id = so.ship_id "
                     "JOIN corp_members cm ON so.player_id = cm.player_id "
                     "JOIN corporations c ON cm.corporation_id = c.corporation_id "
                     "WHERE c.tag = 'ORION';";
    char sql[1024]; sql_build(db, q7, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){}, 0, &res, err);
    return res;
}

int repo_universe_get_random_unprotected_sector(db_t *db, int *sector_out) {
    if (!db || !sector_out) return -1;
    *sector_out = 0;
    db_res_t *res = NULL;
    db_error_t err = {0};
    if (!db_query(db, "SELECT sector_id FROM sectors WHERE sector_id > 10 ORDER BY RANDOM() LIMIT 1", NULL, 0, &res, &err)) return -1;
    int found = db_res_step(res, &err);
    if (found) *sector_out = db_res_col_i32(res, 0, &err);
    db_res_finalize(res);
    return (found && err.code == 0) ? 0 : -1;
}

int repo_universe_get_random_port_sector(db_t *db, int *sector_out) {
    if (!db || !sector_out) return -1;
    *sector_out = 0;
    db_res_t *res = NULL;
    db_error_t err = {0};
    if (!db_query(db, "SELECT sector_id FROM (SELECT DISTINCT sector_id FROM ports) AS port_sectors ORDER BY RANDOM() LIMIT 1", NULL, 0, &res, &err)) return -1;
    int found = db_res_step(res, &err);
    if (found) *sector_out = db_res_col_i32(res, 0, &err);
    db_res_finalize(res);
    return (found && err.code == 0) ? 0 : -1;
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

int repo_universe_get_cluster_center_sector_by_role(db_t *db, const char *role, int *sector_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q10b */
    const char *q10b = "SELECT center_sector FROM clusters WHERE role={1};";
    char sql[512]; sql_build(db, q10b, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(role) }, 1, &res, &err) && db_res_step(res, &err)) {
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

static void
ferengi_copy_text(char *out, size_t out_size, const char *in)
{
    if (!out || out_size == 0) return;
    snprintf(out, out_size, "%s", in ? in : "");
}

int
repo_universe_ensure_ferengi_traders(db_t *db, int corporation_id,
                                      int home_sector)
{
    if (!db || corporation_id <= 0 || home_sector <= 0) return -1;
    db_error_t err;
    db_res_t *res = NULL;
    if (!db_query(db,
          "SELECT trader_code, display_name, ship_type_id, active FROM ferengi_trader_definitions ORDER BY trader_code",
          NULL, 0, &res, &err)) return err.code ? err.code : -1;

    while (db_res_step(res, &err)) {
        char trader_code[96], display_name[128];
        ferengi_copy_text(trader_code, sizeof trader_code, db_res_col_text(res, 0, &err));
        ferengi_copy_text(display_name, sizeof display_name, db_res_col_text(res, 1, &err));
        int ship_type_id = db_res_col_i32(res, 2, &err);
        bool active = db_res_col_bool(res, 3, &err);
        if (err.code || ship_type_id <= 0) {
            db_res_finalize(res);
            return err.code ? err.code : -1;
        }
        char lookup[256];
        sql_build(db, "SELECT ship_id FROM ferengi_traders WHERE trader_code = {1}",
                  lookup, sizeof lookup);
        db_res_t *existing = NULL;
        if (!db_query(db, lookup, (db_bind_t[]){db_bind_text(trader_code)},
                      1, &existing, &err)) {
            db_res_finalize(res);
            return err.code ? err.code : -1;
        }
        int exists = db_res_step(existing, &err);
        db_res_finalize(existing);
        if (exists) {
            char rename_sql[512];
            sql_build(db, "UPDATE ferengi_traders SET display_name = {1}, active = {2} WHERE trader_code = {3}",
                      rename_sql, sizeof rename_sql);
            if (!db_exec(db, rename_sql,
                         (db_bind_t[]){db_bind_text(display_name), db_bind_bool(active),
                                       db_bind_text(trader_code)}, 3, &err)) {
                db_res_finalize(res);
                return err.code ? err.code : -1;
            }
            continue;
        }
        if (!active) continue;

        db_error_t tx_err;
        if (!db_tx_begin(db, DB_TX_DEFAULT, &tx_err)) {
            db_res_finalize(res);
            return tx_err.code;
        }
        int64_t ship_id = 0;
        char insert_ship[512];
        sql_build(db,
          "INSERT INTO ships (name, type_id, holds, fighters, shields, sector_id, hull) VALUES ({1}, {2}, 100, 0, 100, {3}, 100)",
          insert_ship, sizeof insert_ship);
        if (!db_exec_insert_id(db, insert_ship,
              (db_bind_t[]){db_bind_text(display_name),
                            db_bind_i64(ship_type_id), db_bind_i64(home_sector)},
              3, "ship_id", &ship_id, &err)) {
            db_tx_rollback(db, &tx_err);
            db_res_finalize(res);
            return err.code ? err.code : -1;
        }
        char insert_trader[512];
        sql_build(db,
          "INSERT INTO ferengi_traders (trader_code, display_name, corporation_id, ship_id) VALUES ({1}, {2}, {3}, {4})",
          insert_trader, sizeof insert_trader);
        if (!db_exec(db, insert_trader,
              (db_bind_t[]){db_bind_text(trader_code),
                            db_bind_text(display_name),
                            db_bind_i64(corporation_id), db_bind_i64(ship_id)},
              4, &err)) {
            db_tx_rollback(db, &tx_err);
            db_res_finalize(res);
            return err.code ? err.code : -1;
        }
        char cargo[512];
        sql_build(db,
          "INSERT INTO ship_cargo (ship_id, commodity_code, quantity) SELECT {1}, commodity_code, quantity FROM ferengi_trader_definition_cargo WHERE trader_code = {2} ON CONFLICT (ship_id, commodity_code) DO NOTHING",
          cargo, sizeof cargo);
        if (!db_exec(db, cargo,
                     (db_bind_t[]){db_bind_i64(ship_id), db_bind_text(trader_code)},
                     2, &err)
            || !db_tx_commit(db, &tx_err)) {
            db_tx_rollback(db, &tx_err);
            db_res_finalize(res);
            return err.code ? err.code : -1;
        }
    }
    int rc = err.code ? err.code : 0;
    db_res_finalize(res);
    return rc;
}

db_res_t *
repo_universe_get_all_ferengi_traders(db_t *db, db_error_t *err)
{
    const char *query =
      "SELECT ft.ferengi_trader_id, ft.trader_code, ft.display_name, ft.ship_id, "
      "ft.corporation_id, s.sector_id, ft.visit_number FROM ferengi_traders ft "
      "JOIN ships s ON s.ship_id=ft.ship_id WHERE ft.active=TRUE ORDER BY ft.ferengi_trader_id";
    db_res_t *res = NULL;
    if (!db_query(db, query, NULL, 0, &res, err)) return NULL;
    return res;
}

db_res_t *
repo_universe_get_players_in_sector(db_t *db, int sector_id, db_error_t *err)
{
    char sql[512];
    sql_build(db,
      "SELECT player_id FROM players WHERE sector_id={1} AND COALESCE(is_npc,FALSE)=FALSE ORDER BY player_id",
      sql, sizeof sql);
    db_res_t *res = NULL;
    if (!db_query(db, sql, (db_bind_t[]){db_bind_i64(sector_id)}, 1, &res, err)) return NULL;
    return res;
}

db_res_t *
repo_universe_get_ferengi_traders_at_sector(db_t *db, int sector_id,
                                             int player_id, db_error_t *err)
{
    const char *query =
      "SELECT ft.ferengi_trader_id, ft.trader_code, ft.display_name, ft.reputation, "
      "COALESCE(rel.reputation, 0), ft.ship_id, s.sector_id, ft.visit_number "
      "FROM ferengi_traders ft JOIN ships s ON s.ship_id = ft.ship_id "
      "LEFT JOIN ferengi_player_relationships rel ON rel.ferengi_trader_id = ft.ferengi_trader_id AND rel.player_id = {2} "
      "WHERE ft.active = TRUE AND s.sector_id = {1} ORDER BY ft.display_name";
    char sql[1024]; sql_build(db, query, sql, sizeof sql);
    db_res_t *res = NULL;
    if (!db_query(db, sql, (db_bind_t[]){db_bind_i64(sector_id), db_bind_i64(player_id)}, 2, &res, err)) return NULL;
    return res;
}

db_res_t *
repo_universe_get_ferengi_trader_deals(db_t *db, int player_id,
                                        db_error_t *err)
{
    const char *query =
      "SELECT d.ferengi_trader_deal_id, d.ferengi_trader_id, ft.trader_code, ft.display_name, "
      "d.commodity_code, d.side, d.quantity, d.unit_price, d.status, d.expires_at, s.sector_id "
      "FROM ferengi_trader_deals d JOIN ferengi_traders ft ON ft.ferengi_trader_id = d.ferengi_trader_id "
      "JOIN ships s ON s.ship_id = ft.ship_id WHERE d.player_id = {1} "
      "AND d.status = 'open' AND d.expires_at > CURRENT_TIMESTAMP ORDER BY d.created_at, d.ferengi_trader_deal_id";
    char sql[1024]; sql_build(db, query, sql, sizeof sql);
    db_res_t *res = NULL;
    if (!db_query(db, sql, (db_bind_t[]){db_bind_i64(player_id)}, 1, &res, err)) return NULL;
    return res;
}

int
repo_universe_get_ferengi_deal(db_t *db, int64_t deal_id, int for_update,
                                ferengi_deal_t *out)
{
    if (!db || deal_id <= 0 || !out) return -1;
    const char *base =
      "SELECT d.ferengi_trader_deal_id, d.ferengi_trader_id, d.player_id, ft.ship_id, ft.corporation_id, "
      "ft.trader_code, ft.display_name, d.commodity_code, d.side, d.status, d.quantity, d.unit_price, "
      "d.expires_at::text FROM ferengi_trader_deals d "
      "JOIN ferengi_traders ft ON ft.ferengi_trader_id = d.ferengi_trader_id WHERE d.ferengi_trader_deal_id = {1}";
    char q[768], sql[1024];
    snprintf(q, sizeof q, "%s%s", base, for_update ? " FOR UPDATE" : "");
    sql_build(db, q, sql, sizeof sql);
    db_res_t *res = NULL; db_error_t err;
    if (!db_query(db, sql, (db_bind_t[]){db_bind_i64(deal_id)}, 1, &res, &err)) return err.code ? err.code : -1;
    if (!db_res_step(res, &err)) { db_res_finalize(res); return 1; }
    memset(out, 0, sizeof *out);
    out->deal_id = db_res_col_i64(res, 0, &err);
    out->trader_id = db_res_col_i32(res, 1, &err);
    out->player_id = db_res_col_i32(res, 2, &err);
    out->ship_id = db_res_col_i32(res, 3, &err);
    out->corporation_id = db_res_col_i32(res, 4, &err);
    ferengi_copy_text(out->trader_code, sizeof out->trader_code, db_res_col_text(res, 5, &err));
    ferengi_copy_text(out->display_name, sizeof out->display_name, db_res_col_text(res, 6, &err));
    ferengi_copy_text(out->commodity_code, sizeof out->commodity_code, db_res_col_text(res, 7, &err));
    ferengi_copy_text(out->side, sizeof out->side, db_res_col_text(res, 8, &err));
    ferengi_copy_text(out->status, sizeof out->status, db_res_col_text(res, 9, &err));
    out->quantity = db_res_col_i32(res, 10, &err);
    out->unit_price = db_res_col_i64(res, 11, &err);
    ferengi_copy_text(out->expires_at, sizeof out->expires_at,
                      db_res_col_text(res, 12, &err));
    db_res_finalize(res);
    return err.code ? err.code : 0;
}

int
repo_universe_create_ferengi_offer(db_t *db, int trader_id, int player_id,
                                    int visit_number, int ship_id,
                                    int corporation_id, int64_t now_s,
                                    int64_t *deal_id_out)
{
    if (!db || trader_id <= 0 || player_id <= 0 || visit_number < 0 || ship_id <= 0 || corporation_id <= 0) return -1;
    db_res_t *res = NULL; db_error_t err;
    if (!db_query(db,
      "SELECT c.code, c.base_price, COALESCE(sc.quantity, 0) "
      "FROM ferengi_traders ft "
      "JOIN ferengi_trader_definitions td ON td.trader_code = ft.trader_code "
      "JOIN ferengi_trader_definition_rotation rotation ON rotation.trader_code = td.trader_code "
      "JOIN commodities c ON c.code = rotation.commodity_code "
      "LEFT JOIN ship_cargo sc ON sc.commodity_code = c.code AND sc.ship_id = ft.ship_id "
      "WHERE ft.ferengi_trader_id = {1} ORDER BY rotation.position",
      (db_bind_t[]){db_bind_i64(trader_id)}, 1, &res, &err)) return err.code ? err.code : -1;
    char commodity[16] = "ORE"; int64_t stock = 0, base_price = 0;
    while (db_res_step(res, &err)) {
        const char *code = db_res_col_text(res, 0, &err);
        int64_t price = db_res_col_i64(res, 1, &err);
        int64_t qty = db_res_col_i64(res, 2, &err);
        if (qty > 0) { ferengi_copy_text(commodity, sizeof commodity, code); stock = qty; base_price = price; break; }
        if (base_price == 0) base_price = price;
    }
    db_res_finalize(res);
    if (base_price <= 0) return -1;
    const char *side = stock > 0 ? "trader_sells" : "trader_buys";
    int quantity = stock > 0 ? (stock < 10 ? (int)stock : 10) : 5;
    int64_t price = stock > 0 ? (base_price * 11 + 9) / 10 : (base_price * 9) / 10;
    if (strcmp(side, "trader_buys") == 0)
      {
        char treasury_sql[512];
        sql_build(db,
          "SELECT COALESCE((SELECT balance FROM bank_accounts WHERE owner_type = 'corp' AND owner_id = {1} AND is_active = TRUE LIMIT 1), 0)",
          treasury_sql, sizeof treasury_sql);
        res = NULL;
        if (!db_query(db, treasury_sql, (db_bind_t[]){db_bind_i64(corporation_id)},
                      1, &res, &err) || !db_res_step(res, &err))
          {
            if (res) db_res_finalize(res);
            return err.code ? err.code : -1;
          }
        int64_t treasury = db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        if (err.code) return err.code;
        if (price > 0 && quantity > INT64_MAX / price) return -1;
        if (treasury < price * quantity) return 1;
      }
    int offer_lifetime = 21600;
    if (repo_engine_get_config_int(db, "ferengi.offer_lifetime_seconds",
                                   &offer_lifetime) != 0
        || offer_lifetime < 60 || offer_lifetime > 604800)
      offer_lifetime = 21600;
    char idem[96];
    snprintf(idem, sizeof idem, "visit:%d:%d:%d", trader_id, visit_number, player_id);
    const char *query =
      "INSERT INTO ferengi_trader_deals (ferengi_trader_id, player_id, commodity_code, side, quantity, unit_price, idempotency_key, expires_at) "
      "VALUES ({1},{2},{3},{4},{5},{6},{7},CAST({8} AS timestamptz) + ({9} * INTERVAL '1 second')) ON CONFLICT (ferengi_trader_id,idempotency_key) DO NOTHING RETURNING ferengi_trader_deal_id";
    char sql[1024]; sql_build(db, query, sql, sizeof sql);
    if (!db_exec_returning(db, sql,
       (db_bind_t[]){db_bind_i64(trader_id), db_bind_i64(player_id), db_bind_text(commodity), db_bind_text(side),
                     db_bind_i64(quantity), db_bind_i64(price), db_bind_text(idem),
                     db_bind_timestamp_text(now_s), db_bind_i64(offer_lifetime)},
       9, &res, &err)) return err.code ? err.code : -1;
    int inserted = db_res_step(res, &err);
    if (inserted && deal_id_out) *deal_id_out = db_res_col_i64(res, 0, &err);
    db_res_finalize(res);
    return err.code ? err.code : (inserted ? 0 : 1);
}

int
repo_universe_record_ferengi_interaction(db_t *db, const ferengi_deal_t *deal,
                                          const char *type, int delta,
                                          const char *key)
{
    if (!db || !deal || !type || !key) return -1;
    const char *query =
      "WITH inserted AS (INSERT INTO ferengi_trader_interactions (ferengi_trader_id, player_id, deal_id, interaction_type, reputation_delta, idempotency_key) "
      "VALUES ({1},{2},NULLIF({3},0),{4},{5},{6}) ON CONFLICT (idempotency_key) DO NOTHING "
      "RETURNING ferengi_trader_id, player_id, reputation_delta), "
      "rel AS (INSERT INTO ferengi_player_relationships (ferengi_trader_id, player_id, reputation, encounters, last_interaction_at) "
      "SELECT ferengi_trader_id, player_id, reputation_delta, 1, CURRENT_TIMESTAMP FROM inserted "
      "ON CONFLICT (ferengi_trader_id,player_id) DO UPDATE SET reputation = GREATEST(-10000,LEAST(10000,ferengi_player_relationships.reputation + EXCLUDED.reputation)), encounters = ferengi_player_relationships.encounters + 1, last_interaction_at = CURRENT_TIMESTAMP RETURNING ferengi_trader_id), "
      "global_rep AS (UPDATE ferengi_traders SET reputation = GREATEST(-10000,LEAST(10000,reputation + {5})), last_interaction_at = CURRENT_TIMESTAMP, updated_at = CURRENT_TIMESTAMP WHERE ferengi_trader_id IN (SELECT ferengi_trader_id FROM inserted) RETURNING ferengi_trader_id) SELECT COUNT(*) FROM rel";
    char sql[2048]; sql_build(db, query, sql, sizeof sql);
    db_res_t *res = NULL; db_error_t err;
    if (!db_query(db, sql,
      (db_bind_t[]){db_bind_i64(deal->trader_id), db_bind_i64(deal->player_id), db_bind_i64(deal->deal_id),
                    db_bind_text(type), db_bind_i64(delta), db_bind_text(key)}, 6, &res, &err)) return err.code ? err.code : -1;
    int changed = res && db_res_step(res, &err)
                  && db_res_col_i64(res, 0, &err) > 0;
    if (res) db_res_finalize(res);
    return err.code ? err.code : (changed ? 0 : 1);
}

int
repo_universe_transition_ferengi_deal(db_t *db, int64_t deal_id,
                                       const char *status, int64_t now_s,
                                       int *changed_out)
{
    if (!db || deal_id <= 0 || !status || !changed_out) return -1;
    const char *query = NULL;
    if (strcmp(status, "settled") == 0) query = "UPDATE ferengi_trader_deals SET status={1},settled_at=CAST({2} AS timestamptz) WHERE ferengi_trader_deal_id={3} AND status='open' AND expires_at>CAST({2} AS timestamptz)";
    else if (strcmp(status, "declined") == 0) query = "UPDATE ferengi_trader_deals SET status={1} WHERE ferengi_trader_deal_id={3} AND status='open' AND expires_at>CAST({2} AS timestamptz)";
    else if (strcmp(status, "expired") == 0) query = "UPDATE ferengi_trader_deals SET status={1} WHERE ferengi_trader_deal_id={3} AND status='open' AND expires_at<=CAST({2} AS timestamptz)";
    else return -1;
    char sql[768]; sql_build(db, query, sql, sizeof sql); int64_t rows = 0; db_error_t err;
    if (!db_exec_rows_affected(db, sql, (db_bind_t[]){db_bind_text(status),db_bind_timestamp_text(now_s),db_bind_i64(deal_id)}, 3, &rows, &err)) return err.code ? err.code : -1;
    *changed_out = rows > 0; return 0;
}

int
repo_universe_expire_ferengi_deals(db_t *db, int64_t now_s)
{
    char sql[2048]; sql_build(db,
      "WITH expired AS (UPDATE ferengi_trader_deals SET status='expired' WHERE status='open' AND expires_at<=CAST({1} AS timestamptz) RETURNING ferengi_trader_deal_id,ferengi_trader_id,player_id), "
      "ins AS (INSERT INTO ferengi_trader_interactions (ferengi_trader_id,player_id,deal_id,interaction_type,reputation_delta,idempotency_key) "
      "SELECT ferengi_trader_id,player_id,ferengi_trader_deal_id,'expired',0,'deal:'||ferengi_trader_deal_id||':expired' FROM expired ON CONFLICT (idempotency_key) DO NOTHING "
      "RETURNING ferengi_trader_id,player_id,reputation_delta), "
      "rel AS (INSERT INTO ferengi_player_relationships (ferengi_trader_id,player_id,reputation,encounters,last_interaction_at) "
      "SELECT ferengi_trader_id,player_id,SUM(reputation_delta),COUNT(*),CURRENT_TIMESTAMP FROM ins GROUP BY ferengi_trader_id,player_id ON CONFLICT (ferengi_trader_id,player_id) "
      "DO UPDATE SET reputation=ferengi_player_relationships.reputation+EXCLUDED.reputation, encounters=ferengi_player_relationships.encounters+EXCLUDED.encounters,last_interaction_at=CURRENT_TIMESTAMP RETURNING ferengi_trader_id) "
      "UPDATE ferengi_traders SET last_interaction_at=CURRENT_TIMESTAMP,updated_at=CURRENT_TIMESTAMP WHERE ferengi_trader_id IN (SELECT ferengi_trader_id FROM rel)",
      sql, sizeof sql);
    db_error_t err;
    return db_exec(db, sql, (db_bind_t[]){db_bind_timestamp_text(now_s)}, 1, &err) ? 0 : (err.code ? err.code : -1);
}

int
repo_universe_advance_ferengi_trader(db_t *db, int trader_id, int ship_id,
                                      int sector_id)
{
    char sql[512]; sql_build(db,
      "WITH moved AS (UPDATE ships SET sector_id={1},ported=0 WHERE ship_id={2} RETURNING ship_id) UPDATE ferengi_traders SET visit_number=visit_number+1,updated_at=CURRENT_TIMESTAMP WHERE ferengi_trader_id={3} AND EXISTS (SELECT 1 FROM moved)",
      sql, sizeof sql);
    db_error_t err;
    return db_exec(db, sql, (db_bind_t[]){db_bind_i64(sector_id),db_bind_i64(ship_id),db_bind_i64(trader_id)}, 3, &err) ? 0 : (err.code ? err.code : -1);
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

int repo_universe_get_candidate_homeworld_sectors(db_t *db, int **sectors_out, int *count_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q30 */
    /* Select exit_sectors from longest_tunnels */
    const char *q30 = "SELECT exit_sector FROM longest_tunnels;";
    if (db_query(db, q30, NULL, 0, &res, &err)) {
        int count = db_res_col_count(res) > 0 ? res->num_rows : 0; // num_rows is reliable after step, but for PG query might need count first or array approach
        /* Actually db_query usually returns full result in memory for this driver impl? 
           Check db_res structure. Yes, num_rows is populated. */
        
        if (count > 0) {
            *sectors_out = calloc(count, sizeof(int));
            *count_out = 0;
            while (db_res_step(res, &err)) {
                (*sectors_out)[(*count_out)++] = db_res_col_i32(res, 0, &err);
            }
        } else {
            *sectors_out = NULL;
            *count_out = 0;
        }
        db_res_finalize(res);
        return 0;
    }
    return -1;
}

int repo_universe_ensure_ferengi_homeworld(db_t *db, int sector_id, int owner_id) {
    db_error_t err;
    int64_t now_ts = (int64_t)time(NULL);
    /* Try to update existing Planet 2 */
    /* SQL_VERBATIM: Q31 */
    const char *q31_upd = "UPDATE planets SET sector_id = {1}, owner_id = {2}, owner_type = 'corp', name = 'Ferenginar' WHERE planet_id = 2;";
    char sql_upd[512]; sql_build(db, q31_upd, sql_upd, sizeof(sql_upd));
    int64_t rows = 0;
    
    if (db_exec_rows_affected(db, sql_upd, (db_bind_t[]){ db_bind_i64(sector_id), db_bind_i64(owner_id) }, 2, &rows, &err)) {
        if (rows > 0) return 0;
    }

    /* If update failed (rows=0), insert with explicit ID 2 only if not exists. */
    /* SQL_VERBATIM: Q32a - Check if planet 2 exists */
    const char *q32a = "SELECT 1 FROM planets WHERE planet_id = 2 LIMIT 1;";
    char sql_check[512]; sql_build(db, q32a, sql_check, sizeof(sql_check));
    db_res_t *check_res = NULL;
    if (db_query(db, sql_check, NULL, 0, &check_res, &err) == 0 && db_res_step(check_res, &err) == 0) {
        /* Planet 2 already exists, just update it */
        if (check_res) db_res_finalize(check_res);
        /* SQL_VERBATIM: Q32b */
        const char *q32b = "UPDATE planets SET sector_id = {1} WHERE planet_id = 2;";
        char sql_upd[512]; sql_build(db, q32b, sql_upd, sizeof(sql_upd));
        if (!db_exec(db, sql_upd, (db_bind_t[]){ db_bind_i64(sector_id) }, 1, &err)) {
            return err.code;
        }
    } else {
        /* Planet 2 doesn't exist, insert it */
        if (check_res) db_res_finalize(check_res);
        /* SQL_VERBATIM: Q32_ins */
        const char *q32_ins = "INSERT INTO planets (planet_id, sector_id, name, owner_id, owner_type, class, type, created_at, created_by, genesis_flag) "
                              "VALUES (2, {1}, 'Ferenginar', {2}, 'corp', 'M', 1, {3}, 0, FALSE);"; 
        
        char sql_ins[1024]; sql_build(db, q32_ins, sql_ins, sizeof(sql_ins));
        if (!db_exec(db, sql_ins, (db_bind_t[]){ db_bind_i64(sector_id), db_bind_i64(owner_id), db_bind_timestamp_text(now_ts) }, 3, &err)) {
            return err.code;
        }
    }
    return 0;
}

int repo_universe_relocate_orion_base(db_t *db, int sector_id, int port_id, int planet_id, int owner_id) {
    db_error_t err;
    /* Move Port */
    /* SQL_VERBATIM: Q33 */
    const char *q33 = "UPDATE ports SET sector_id = {1} WHERE port_id = {2};";
    char sql_port[512]; sql_build(db, q33, sql_port, sizeof(sql_port));
    if (!db_exec(db, sql_port, (db_bind_t[]){ db_bind_i64(sector_id), db_bind_i64(port_id) }, 2, &err)) return err.code;

    /* Move Planet */
    /* SQL_VERBATIM: Q34 */
    const char *q34 = "UPDATE planets SET sector_id = {1}, owner_id = {2}, owner_type = 'corp', name = 'Orion Hideout' WHERE planet_id = {3};";
    char sql_planet[512]; sql_build(db, q34, sql_planet, sizeof(sql_planet));
    int64_t rows = 0;
    if (db_exec_rows_affected(db, sql_planet, (db_bind_t[]){ db_bind_i64(sector_id), db_bind_i64(owner_id), db_bind_i64(planet_id) }, 3, &rows, &err)) {
        if (rows > 0) return 0;
    }

    /* If planet missing, insert it only if not exists */
    int64_t now_ts = (int64_t)time(NULL);
    /* SQL_VERBATIM: Q34a - Check if planet exists */
    const char *q34a = "SELECT 1 FROM planets WHERE planet_id = {1} LIMIT 1;";
    char sql_check[512]; sql_build(db, q34a, sql_check, sizeof(sql_check));
    db_res_t *check_res = NULL;
    db_bind_t check_bind[] = { db_bind_i64(planet_id) };
    if (db_query(db, sql_check, check_bind, 1, &check_res, &err) == 0 && db_res_step(check_res, &err) == 0) {
        /* Planet already exists, no need to insert */
        if (check_res) db_res_finalize(check_res);
    } else {
        /* Planet doesn't exist, insert it */
        if (check_res) db_res_finalize(check_res);
        /* SQL_VERBATIM: Q35 */
        const char *q35 = "INSERT INTO planets (planet_id, sector_id, name, owner_id, owner_type, class, type, created_at, created_by, genesis_flag) "
                          "VALUES ({1}, {2}, 'Orion Hideout', {3}, 'corp', 'M', 1, {4}, 0, FALSE);";
        char sql_ins[1024]; sql_build(db, q35, sql_ins, sizeof(sql_ins));
        if (!db_exec(db, sql_ins, (db_bind_t[]){ db_bind_i64(planet_id), db_bind_i64(sector_id), db_bind_i64(owner_id), db_bind_timestamp_text(now_ts) }, 4, &err)) {
            return err.code;
        }
    }

    return 0;
}
