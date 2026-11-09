#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_citadel.h"
#include "db/sql_driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int repo_citadel_get_construction_state(db_t *db, int32_t planet_id,
                                        int32_t *level, char *status,
                                        size_t status_size,
                                        int32_t *target_level,
                                        int64_t *start_time,
                                        int64_t *completion_time)
{
    if (!db || planet_id <= 0 || !level || !status || status_size == 0
        || !target_level || !start_time || !completion_time)
        return ERR_INVALID_ARG;
    const char *query =
        "SELECT COALESCE(level, 0), COALESCE(construction_status, 'idle'), "
        "COALESCE(target_level, 0), COALESCE(construction_start_time, 0), "
        "COALESCE(construction_end_time, 0) FROM citadels WHERE planet_id = {1};";
    char sql[512];
    if (sql_build(db, query, sql, sizeof(sql)) != 0)
        return ERR_DB_QUERY_FAILED;
    db_res_t *res = NULL;
    db_error_t err = {0};
    if (!db_query(db, sql, (db_bind_t[]){db_bind_i32(planet_id)}, 1,
                  &res, &err))
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
    if (!db_res_step(res, &err)) {
        db_res_finalize(res);
        return err.code ? err.code : ERR_DB_NOT_FOUND;
    }
    const char *value = db_res_col_text(res, 1, &err);
    if (!value || strlen(value) >= status_size) {
        db_res_finalize(res);
        return value ? ERR_INVALID_ARG : (err.code ? err.code : ERR_DB_QUERY_FAILED);
    }
    *level = db_res_col_i32(res, 0, &err);
    memcpy(status, value, strlen(value) + 1);
    *target_level = db_res_col_i32(res, 2, &err);
    *start_time = db_res_col_i64(res, 3, &err);
    *completion_time = db_res_col_i64(res, 4, &err);
    db_res_finalize(res);
    return err.code;
}

int repo_citadel_get_qcannon_state(db_t *db, int32_t planet_id,
                                   int32_t *sector_level,
                                   int32_t *atmosphere_level)
{
    if (!db || planet_id <= 0 || !sector_level || !atmosphere_level)
        return ERR_INVALID_ARG;
    *sector_level = 0;
    *atmosphere_level = 0;
    const char *query =
        "SELECT COALESCE(qCannonSector, 0), COALESCE(qCannonAtmosphere, 0) "
        "FROM citadels WHERE planet_id = {1};";
    char sql[384];
    if (sql_build(db, query, sql, sizeof(sql)) != 0)
        return ERR_DB_QUERY_FAILED;
    db_res_t *res = NULL;
    db_error_t err = {0};
    if (!db_query(db, sql, (db_bind_t[]){db_bind_i32(planet_id)}, 1,
                  &res, &err))
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
    if (!db_res_step(res, &err)) {
        db_res_finalize(res);
        return err.code ? err.code : ERR_DB_NOT_FOUND;
    }
    *sector_level = db_res_col_i32(res, 0, &err);
    *atmosphere_level = db_res_col_i32(res, 1, &err);
    db_res_finalize(res);
    return err.code;
}

int repo_citadel_get_onplanet(db_t *db, int32_t ship_id, int32_t *planet_id_out)
{
    /* SQL_VERBATIM: Q1 */
    /* Join players to get lastplanet, but only if ship.onplanet is TRUE */
    const char *sql = "SELECT p.lastplanet FROM players p JOIN ships s ON p.ship_id = s.ship_id WHERE s.ship_id = {1} AND s.onplanet = TRUE;";
    char sql_converted[256];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_error_t err = {0};
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (planet_id_out) *planet_id_out = db_res_col_i32(res, 0, &err);
        } else {
            if (planet_id_out) *planet_id_out = 0;
        }
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_citadel_get_planet_info(db_t *db, int32_t planet_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q2 */
    const char *sql_planet =
        "SELECT p.type, p.owner_id, p.owner_type, p.colonists_unassigned, "
        "COALESCE((SELECT quantity FROM entity_stock WHERE entity_type='planet' AND entity_id=p.planet_id AND commodity_code='ORE'),0), "
        "COALESCE((SELECT quantity FROM entity_stock WHERE entity_type='planet' AND entity_id=p.planet_id AND commodity_code='ORG'),0), "
        "COALESCE((SELECT quantity FROM entity_stock WHERE entity_type='planet' AND entity_id=p.planet_id AND commodity_code='EQU'),0) "
        "FROM planets p WHERE p.planet_id = {1};";
    char sql_planet_converted[512];
    sql_build(db, sql_planet, sql_planet_converted, sizeof(sql_planet_converted));

    db_error_t err = {0};
    if (db_query(db, sql_planet_converted, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_citadel_get_status(db_t *db, int32_t planet_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q3 */
    const char *sql_citadel =
        "SELECT level, construction_status FROM citadels WHERE planet_id = {1};";
    char sql_citadel_converted[256];
    sql_build(db, sql_citadel, sql_citadel_converted, sizeof(sql_citadel_converted));

    db_error_t err;
    if (db_query(db, sql_citadel_converted, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_citadel_get_upgrade_reqs(db_t *db, int target_level, int32_t planet_type_id, db_res_t **out_res)
{
    const char *sql = NULL;
    switch (target_level) {
        case 1:
            /* SQL_VERBATIM: Q4_L1 */
            sql = "SELECT citadelUpgradeColonist_lvl1, citadelUpgradeOre_lvl1, citadelUpgradeOrganics_lvl1, citadelUpgradeEquipment_lvl1, citadelUpgradeTime_lvl1 FROM planettypes WHERE planettypes_id = {1};";
            break;
        case 2:
            /* SQL_VERBATIM: Q4_L2 */
            sql = "SELECT citadelUpgradeColonist_lvl2, citadelUpgradeOre_lvl2, citadelUpgradeOrganics_lvl2, citadelUpgradeEquipment_lvl2, citadelUpgradeTime_lvl2 FROM planettypes WHERE planettypes_id = {1};";
            break;
        case 3:
            /* SQL_VERBATIM: Q4_L3 */
            sql = "SELECT citadelUpgradeColonist_lvl3, citadelUpgradeOre_lvl3, citadelUpgradeOrganics_lvl3, citadelUpgradeEquipment_lvl3, citadelUpgradeTime_lvl3 FROM planettypes WHERE planettypes_id = {1};";
            break;
        case 4:
            /* SQL_VERBATIM: Q4_L4 */
            sql = "SELECT citadelUpgradeColonist_lvl4, citadelUpgradeOre_lvl4, citadelUpgradeOrganics_lvl4, citadelUpgradeEquipment_lvl4, citadelUpgradeTime_lvl4 FROM planettypes WHERE planettypes_id = {1};";
            break;
        case 5:
            /* SQL_VERBATIM: Q4_L5 */
            sql = "SELECT citadelUpgradeColonist_lvl5, citadelUpgradeOre_lvl5, citadelUpgradeOrganics_lvl5, citadelUpgradeEquipment_lvl5, citadelUpgradeTime_lvl5 FROM planettypes WHERE planettypes_id = {1};";
            break;
        case 6:
            /* SQL_VERBATIM: Q4_L6 */
            sql = "SELECT citadelUpgradeColonist_lvl6, citadelUpgradeOre_lvl6, citadelUpgradeOrganics_lvl6, citadelUpgradeEquipment_lvl6, citadelUpgradeTime_lvl6 FROM planettypes WHERE planettypes_id = {1};";
            break;
        default:
            return -1;
    }

    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(planet_type_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_citadel_deduct_resources(db_t *db, int64_t colonists, int64_t ore, int64_t org, int64_t equip, int32_t planet_id)
{
    db_error_t err;
    int64_t affected = 0;
    const char *codes[] = {"ORE", "ORG", "EQU"};
    int64_t costs[] = {ore, org, equip};
    const char *stock_sql = "UPDATE entity_stock SET quantity = quantity - {1} WHERE entity_type='planet' AND entity_id={2} AND commodity_code={3} AND quantity >= {4}";
    char stock_stmt[512];
    sql_build(db, stock_sql, stock_stmt, sizeof(stock_stmt));
    for (int i = 0; i < 3; i++) {
        if (costs[i] <= 0) continue;
        if (!db_exec_rows_affected(db, stock_stmt,
                                   (db_bind_t[]){db_bind_i64(costs[i]), db_bind_i32(planet_id), db_bind_text(codes[i]), db_bind_i64(costs[i])},
                                   4, &affected, &err)) return err.code;
        if (affected != 1) return ERR_DB_CONSTRAINT;
    }
    if (colonists > 0) {
        const char *population_sql = "UPDATE planets SET population = GREATEST(0, GREATEST(COALESCE(population, 0), colonists_unassigned + colonists_ore + colonists_org + colonists_eq + colonists_weapons) - {1}), colonists_unassigned = colonists_unassigned - {1} WHERE planet_id = {2} AND colonists_unassigned >= {3}";
        char population_stmt[512];
        sql_build(db, population_sql, population_stmt, sizeof(population_stmt));
        if (!db_exec_rows_affected(db, population_stmt,
                                   (db_bind_t[]){db_bind_i64(colonists), db_bind_i32(planet_id), db_bind_i64(colonists)},
                                   3, &affected, &err)) return err.code;
        if (affected != 1) return ERR_DB_CONSTRAINT;
    }
    return 0;
}

int repo_citadel_start_construction(db_t *db, int32_t planet_id, int32_t current_level, int32_t player_id, int32_t target_level, int64_t start_time, int64_t end_time)
{
    if (!db || planet_id <= 0 || current_level < 0 || current_level > 6
        || player_id <= 0 || target_level <= current_level || target_level > 6
        || start_time <= 0 || end_time <= start_time)
        return ERR_INVALID_ARG;

    db_error_t err = {0};
    int64_t rows = 0;

    /* Compare-and-set the current level and idle state. A concurrent start
     * that waited on this row will recheck the predicate after the winner
     * commits and must not replace its target or completion time. */
    const char *q_upd =
        "UPDATE citadels SET construction_status='upgrading', target_level={1}, "
        "construction_start_time={2}, construction_end_time={3} "
        "WHERE planet_id={4} AND level={5} "
        "AND COALESCE(construction_status, 'idle')='idle';";
    char sql_upd[512];
    if (sql_build(db, q_upd, sql_upd, sizeof(sql_upd)) != 0)
        return ERR_DB_QUERY_FAILED;
    db_bind_t upd_params[] = { db_bind_i32(target_level), db_bind_i64(start_time),
                               db_bind_i64(end_time), db_bind_i32(planet_id),
                               db_bind_i32(current_level) };
    if (!db_exec_rows_affected(db, sql_upd, upd_params, 5, &rows, &err))
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
    if (rows == 1)
        return 0;

    /* Distinguish a pre-existing busy/stale citadel from the initial build
     * where no row exists yet. */
    const char *q_exists = "SELECT 1 FROM citadels WHERE planet_id={1};";
    char sql_exists[256];
    if (sql_build(db, q_exists, sql_exists, sizeof(sql_exists)) != 0)
        return ERR_DB_QUERY_FAILED;
    db_res_t *res = NULL;
    if (!db_query(db, sql_exists, (db_bind_t[]){db_bind_i32(planet_id)}, 1,
                  &res, &err))
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
    bool exists = db_res_step(res, &err);
    db_res_finalize(res);
    if (err.code != 0)
        return err.code;
    if (exists)
        return ERR_DB_BUSY;

    /* Initial construction only: a racing insert loses the primary-key race
     * and returns an error to the caller, which rolls back resource debits. */
    const char *q_ins =
        "INSERT INTO citadels (planet_id, level, owner_id, construction_status, "
        "target_level, construction_start_time, construction_end_time) "
        "VALUES ({1}, {2}, {3}, 'upgrading', {4}, {5}, {6});";
    char sql_ins[1024];
    if (sql_build(db, q_ins, sql_ins, sizeof(sql_ins)) != 0)
        return ERR_DB_QUERY_FAILED;
    db_bind_t ins_params[] = { db_bind_i32(planet_id), db_bind_i32(current_level), db_bind_i32(player_id), db_bind_i32(target_level), db_bind_i64(start_time), db_bind_i64(end_time) };

    if (!db_exec(db, sql_ins, ins_params, 6, &err))
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
    return 0;
}

int repo_citadel_reap_upgrades(db_t *db, int64_t now_s, int limit)
{
    if (!db || now_s <= 0 || limit <= 0 || limit > 1000)
        return ERR_INVALID_ARG;
    db_error_t err = {0};
    db_res_t *res = NULL;

    /* Candidate discovery is advisory only. Each candidate is re-read and
     * locked inside the transaction that completes it, so concurrent workers
     * cannot act on stale target levels or lose commit failures. */
    int32_t *planet_ids = calloc((size_t) limit, sizeof(*planet_ids));
    if (!planet_ids)
        return ERR_DB_INTERNAL;
    char q_sel[512];
    if (sql_build(db,
        "SELECT planet_id FROM citadels WHERE construction_status='upgrading' "
        "AND construction_end_time <= {1} ORDER BY construction_end_time, planet_id "
        "LIMIT {2}", q_sel, sizeof(q_sel)) != 0) {
        free(planet_ids);
        return ERR_DB_QUERY_FAILED;
    }
    db_bind_t sel_params[] = { db_bind_i64(now_s), db_bind_i32(limit) };
    db_error_clear(&err);
    if (!db_query(db, q_sel, sel_params, 2, &res, &err)) {
        free(planet_ids);
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
    }
    int candidate_count = 0;
    while (candidate_count < limit && db_res_step(res, &err))
        planet_ids[candidate_count++] = db_res_col_i32(res, 0, &err);
    db_res_finalize(res);
    if (err.code) {
        free(planet_ids);
        return err.code;
    }

    for (int candidate = 0; candidate < candidate_count; candidate++) {
        int32_t planet_id = planet_ids[candidate];
        if (!db_tx_begin(db, DB_TX_DEFAULT, &err)) {
            free(planet_ids);
            return err.code ? err.code : ERR_DB_EXEC_FAILED;
        }

        char q_lock_tmpl[384], q_lock[512];
        snprintf(q_lock_tmpl, sizeof(q_lock_tmpl),
                 "SELECT target_level FROM citadels WHERE planet_id={1} "
                 "AND construction_status='upgrading' AND construction_end_time <= {2}%s",
                 sql_for_update(db));
        if (sql_build(db, q_lock_tmpl, q_lock, sizeof(q_lock)) != 0) {
            db_tx_rollback(db, NULL);
            free(planet_ids);
            return ERR_DB_QUERY_FAILED;
        }
        res = NULL;
        db_error_clear(&err);
        if (!db_query(db, q_lock,
                      (db_bind_t[]){db_bind_i32(planet_id), db_bind_i64(now_s)},
                      2, &res, &err)) {
            db_tx_rollback(db, NULL);
            free(planet_ids);
            return err.code ? err.code : ERR_DB_QUERY_FAILED;
        }
        if (!db_res_step(res, &err)) {
            db_res_finalize(res);
            db_tx_rollback(db, NULL);
            if (err.code) {
                free(planet_ids);
                return err.code;
            }
            continue;
        }
        int32_t target_level = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        if (err.code || target_level < 1 || target_level > 6) {
            db_tx_rollback(db, NULL);
            free(planet_ids);
            return err.code ? err.code : ERR_DB_CONSTRAINT;
        }

        /* Apply derived defenses based on level */
        const char *defense_sql = "";
        switch (target_level) {
            case 1: defense_sql = ", militaryReactionLevel = GREATEST(militaryReactionLevel, 1)"; break;
            case 2: defense_sql = ", qCannonAtmosphere = GREATEST(qCannonAtmosphere, 1)"; break;
            case 3: defense_sql = ", qCannonSector = GREATEST(qCannonSector, 1)"; break;
            case 4: defense_sql = ", interdictor = GREATEST(interdictor, 1)"; break;
            case 5: defense_sql = ", planetaryShields = GREATEST(planetaryShields, 500)"; break;
            case 6: defense_sql = ", transporterlvl = GREATEST(transporterlvl, 1)"; break;
            default: break;
        }

        char q_upd_tmpl[1024];
        snprintf(q_upd_tmpl, sizeof(q_upd_tmpl),
                 "UPDATE citadels SET level = {1}, construction_status = 'idle', target_level = 0, construction_start_time = 0, construction_end_time = 0%s WHERE planet_id = {2} AND construction_status = 'upgrading' AND target_level = {3} AND construction_end_time <= {4};",
                 defense_sql);

        char sql_upd[1024];
        int64_t affected = 0;
        if (sql_build(db, q_upd_tmpl, sql_upd, sizeof(sql_upd)) != 0
            || !db_exec_rows_affected(db, sql_upd,
                (db_bind_t[]){db_bind_i32(target_level), db_bind_i32(planet_id),
                              db_bind_i32(target_level), db_bind_i64(now_s)},
                4, &affected, &err)
            || affected != 1) {
            db_tx_rollback(db, NULL);
            free(planet_ids);
            return err.code ? err.code : ERR_DB_BUSY;
        }
        if (!db_tx_commit(db, &err)) {
            db_tx_rollback(db, NULL);
            free(planet_ids);
            return err.code ? err.code : ERR_DB_EXEC_FAILED;
        }
    }
    free(planet_ids);
    return 0;
}
