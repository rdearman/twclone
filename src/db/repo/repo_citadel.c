#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_citadel.h"
#include "db/sql_driver.h"
#include <stdio.h>
#include <string.h>

int repo_citadel_get_onplanet(db_t *db, int32_t ship_id, int32_t *planet_id_out)
{
    /* SQL_VERBATIM: Q1 */
    /* Join players to get lastplanet, but only if ship.onplanet is TRUE */
    const char *sql = "SELECT p.lastplanet FROM players p JOIN ships s ON p.ship_id = s.ship_id WHERE s.ship_id = {1} AND s.onplanet = TRUE;";
    char sql_converted[256];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_error_t err;
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
        "SELECT type, owner_id, owner_type, colonist, ore_on_hand, organics_on_hand, equipment_on_hand FROM planets WHERE planet_id = {1};";
    char sql_planet_converted[512];
    sql_build(db, sql_planet, sql_planet_converted, sizeof(sql_planet_converted));

    db_error_t err;
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

int repo_citadel_deduct_resources(db_t *db, int64_t ore, int64_t org, int64_t equip, int32_t planet_id)
{
    /* SQL_VERBATIM: Q5 */
    const char *sql_update_planet =
        "UPDATE planets SET ore_on_hand = ore_on_hand - {1}, organics_on_hand = organics_on_hand - {2}, equipment_on_hand = equipment_on_hand - {3} WHERE planet_id = {4};";
    char sql_converted[512];
    sql_build(db, sql_update_planet, sql_converted, sizeof(sql_converted));

    db_bind_t params[] = { db_bind_i64(ore), db_bind_i64(org), db_bind_i64(equip), db_bind_i32(planet_id) };
    db_error_t err;
    if (!db_exec(db, sql_converted, params, 4, &err)) {
        return err.code;
    }
    return 0;
}

int repo_citadel_start_construction(db_t *db, int32_t planet_id, int32_t current_level, int32_t player_id, int32_t target_level, int64_t start_time, int64_t end_time)
{
    db_error_t err;
    int64_t rows = 0;

    /* 1. Try Update first */
    const char *q_upd = "UPDATE citadels SET construction_status='upgrading', target_level={1}, construction_start_time={2}, construction_end_time={3} WHERE planet_id = {4};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_i32(target_level), db_bind_i64(start_time), db_bind_i64(end_time), db_bind_i32(planet_id) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 4, &rows, &err) && rows > 0) return 0;

    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO citadels (planet_id, level, owner_id, construction_status, target_level, construction_start_time, construction_end_time) VALUES ({1}, {2}, {3}, 'upgrading', {4}, {5}, {6});";
    char sql_ins[1024]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    db_bind_t ins_params[] = { db_bind_i32(planet_id), db_bind_i32(current_level), db_bind_i32(player_id), db_bind_i32(target_level), db_bind_i64(start_time), db_bind_i64(end_time) };

    if (!db_exec(db, sql_ins, ins_params, 6, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 4, &err)) return 0;
        }
        return err.code;
    }
    return 0;
}

int repo_citadel_reap_upgrades(db_t *db, int64_t now_s, int limit)
{
    if (!db) return -1;
    db_error_t err;
    db_res_t *res = NULL;

    /* 1. Find citadels due for completion */
    /* Rule 3.2: No FOR UPDATE SKIP LOCKED directly, use sql_driver helper. */
    char q_sel_tmpl[512];
    snprintf(q_sel_tmpl, sizeof(q_sel_tmpl),
             "SELECT planet_id, target_level FROM citadels "
             "WHERE construction_status = 'upgrading' AND construction_end_time <= {1} "
             "LIMIT {2}%s;", sql_for_update_skip_locked(db));

    char q_sel[512];
    sql_build(db, q_sel_tmpl, q_sel, sizeof(q_sel));

    db_bind_t sel_params[] = { db_bind_i64(now_s), db_bind_i32(limit) };
    if (!db_query(db, q_sel, sel_params, 2, &res, &err)) return -1;

    while (db_res_step(res, &err)) {
        int32_t planet_id = db_res_col_i32(res, 0, &err);
        int32_t target_level = db_res_col_i32(res, 1, &err);

        /* 2. Atomic update for this citadel */
        if (!db_tx_begin(db, DB_TX_DEFAULT, &err)) continue;

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
                 "UPDATE citadels SET level = {1}, construction_status = 'idle', target_level = 0, construction_start_time = 0, construction_end_time = 0%s WHERE planet_id = {2} AND construction_status = 'upgrading';",
                 defense_sql);

        char sql_upd[1024];
        sql_build(db, q_upd_tmpl, sql_upd, sizeof(sql_upd));
        db_bind_t upd_params[] = { db_bind_i32(target_level), db_bind_i32(planet_id) };

        if (db_exec(db, sql_upd, upd_params, 2, &err)) {
            db_tx_commit(db, &err);
        } else {
            db_tx_rollback(db, &err);
        }
    }
    db_res_finalize(res);
    return 0;
}
