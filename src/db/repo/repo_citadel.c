#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_citadel.h"
#include "db/sql_driver.h"
#include <stdio.h>
#include <string.h>

int repo_citadel_get_onplanet(db_t *db, int32_t ship_id, int32_t *planet_id_out)
{
    /* SQL_VERBATIM: Q1 */
    const char *sql = "SELECT onplanet FROM ships WHERE ship_id = {1};";
    char sql_converted[256];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (planet_id_out) *planet_id_out = db_res_col_i32(res, 0, &err);
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
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(planet_type_id) }, 1, out_res, &err)) {
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
    /* SQL_VERBATIM: Q6 */
    const char *sql_update_citadel =
        "INSERT INTO citadels (planet_id, level, owner_id, construction_status, target_level, construction_start_time, construction_end_time) VALUES ({1}, {2}, {3}, 'upgrading', {4}, {5}, {6}) ON CONFLICT(planet_id) DO UPDATE SET construction_status='upgrading', target_level={4}, construction_start_time={5}, construction_end_time={6};";
    char sql_converted[1024];
    sql_build(db, sql_update_citadel, sql_converted, sizeof(sql_converted));

    db_bind_t params[] = {
        db_bind_i32(planet_id),
        db_bind_i32(current_level),
        db_bind_i32(player_id),
        db_bind_i32(target_level),
        db_bind_i64(start_time),
        db_bind_i64(end_time)
    };

    db_error_t err;
    if (!db_exec(db, sql_converted, params, 6, &err)) {
        return err.code;
    }
    return 0;
}
