#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_planets.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int db_planets_apply_terra_sanctions(db_t *db, int player_id) {
    db_error_t err;
    db_bind_t params[] = { db_bind_i32 (player_id) };

    /* SQL_VERBATIM: Q1 */
    const char *q1 = "DELETE FROM ships WHERE ship_id IN (  SELECT ship_id FROM ship_ownership WHERE player_id = {1});";
    char sql1[1024]; sql_build(db, q1, sql1, sizeof(sql1));
    db_exec(db, sql1, params, 1, &err);

    /* SQL_VERBATIM: Q2 */
    const char *q2 = "UPDATE players SET credits = 0 WHERE player_id = {1};";
    char sql2[512]; sql_build(db, q2, sql2, sizeof(sql2));
    db_exec(db, sql2, params, 1, &err);

    /* SQL_VERBATIM: Q3 */
    const char *q3 = "UPDATE bank_accounts SET balance = 0 WHERE owner_type = 'player' AND owner_id = {1};";
    char sql3[512]; sql_build(db, q3, sql3, sizeof(sql3));
    db_exec(db, sql3, params, 1, &err);

    /* SQL_VERBATIM: Q4 */
    const char *q4 = "DELETE FROM sector_assets WHERE player = {1};";
    char sql4[512]; sql_build(db, q4, sql4, sizeof(sql4));
    db_exec(db, sql4, params, 1, &err);

    /* SQL_VERBATIM: Q5 */
    const char *q5 = "DELETE FROM limpet_attached WHERE owner_player_id = {1};";
    char sql5[512]; sql_build(db, q5, sql5, sizeof(sql5));
    db_exec(db, sql5, params, 1, &err);

    return 0;
}

int db_planets_get_ship_sector(db_t *db, int ship_id, int *sector_id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q6 */
    const char *q6 = "SELECT sector_id FROM ships WHERE ship_id={1};";
    char sql[512]; sql_build(db, q6, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *sector_id = db_res_col_int(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_get_attack_info(db_t *db, int planet_id, int *sector_id, int *owner_id, int *fighters) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q7 */
    const char *q7 = "SELECT sector_id, owner_id, fighters FROM planets WHERE id={1};";
    char sql[512]; sql_build(db, q7, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *sector_id = db_res_col_int(res, 0, &err);
        *owner_id = db_res_col_int(res, 1, &err);
        *fighters = db_res_col_int(res, 2, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_get_ship_fighters(db_t *db, int ship_id, int *fighters) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q8 */
    const char *q8 = "SELECT fighters FROM ships WHERE ship_id={1};";
    char sql[512]; sql_build(db, q8, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *fighters = db_res_col_int(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_get_citadel_defenses(db_t *db, int planet_id, int *level, int *shields, int *reaction) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q9 */
    const char *q9 = "SELECT level, planetary_shields, military_reaction_level FROM citadels WHERE planet_id={1};";
    char sql[512]; sql_build(db, q9, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *level = db_res_col_int(res, 0, &err);
        *shields = db_res_col_int(res, 1, &err);
        *reaction = db_res_col_int(res, 2, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_update_citadel_shields(db_t *db, int planet_id, int new_shields) {
    db_error_t err;
    /* SQL_VERBATIM: Q10 */
    const char *q10 = "UPDATE citadels SET planetary_shields={1} WHERE planet_id={2};";
    char sql[512]; sql_build(db, q10, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(new_shields), db_bind_i32(planet_id) }, 2, &err)) return 0;
    return -1;
}

int db_planets_update_ship_fighters(db_t *db, int ship_id, int loss) {
    db_error_t err;
    /* SQL_VERBATIM: Q11 */
    const char *q11 = "UPDATE ships SET fighters = fighters - {1} WHERE ship_id = {2};";
    char sql[512]; sql_build(db, q11, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(loss), db_bind_i32(ship_id) }, 2, &err)) return 0;
    return -1;
}

int db_planets_capture(db_t *db, int planet_id, int new_owner, const char *new_type) {
    db_error_t err;
    /* SQL_VERBATIM: Q12 */
    const char *q12 = "UPDATE planets SET fighters=0, owner_id={1}, owner_type={2} WHERE id={3};";
    char sql[512]; sql_build(db, q12, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(new_owner), db_bind_text(new_type), db_bind_i32(planet_id) }, 3, &err)) return 0;
    return -1;
}

int db_planets_lose_fighters(db_t *db, int planet_id, int loss) {
    db_error_t err;
    /* SQL_VERBATIM: Q13 */
    const char *q13 = "UPDATE planets SET fighters = fighters - {1} WHERE planet_id = {2};";
    char sql[512]; sql_build(db, q13, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(loss), db_bind_i32(planet_id) }, 2, &err)) return 0;
    return -1;
}

int db_planets_is_commodity_illegal(db_t *db, const char *code, bool *illegal) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q14 */
    const char *q14 = "SELECT illegal FROM commodities WHERE code = {1} LIMIT 1";
    char sql[512]; sql_build(db, q14, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(code) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) *illegal = (db_res_col_i32(res, 0, &err) != 0);
        else *illegal = false;
        db_res_finalize(res);
        return 0;
    }
    return -1;
}

int db_planets_get_sector(db_t *db, int planet_id, int *sector_id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q15 */
    const char *q15 = "SELECT sector_id FROM planets WHERE planet_id = {1}";
    char sql[512]; sql_build(db, q15, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *sector_id = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_get_owner_info(db_t *db, int planet_id, int *owner_id, char **owner_type) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q16 */
    const char *q16 = "SELECT planet_id, owner_id, owner_type FROM planets WHERE planet_id = {1};";
    char sql[512]; sql_build(db, q16, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *owner_id = db_res_col_i32(res, 1, &err);
        const char *ot = db_res_col_text(res, 2, &err);
        *owner_type = ot ? strdup(ot) : NULL;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_rename(db_t *db, int planet_id, const char *new_name) {
    db_error_t err;
    /* SQL_VERBATIM: Q17 */
    const char *q17 = "UPDATE planets SET name = {1} WHERE planet_id = {2};";
    char sql[512]; sql_build(db, q17, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_text(new_name), db_bind_i32(planet_id) }, 2, &err)) return 0;
    return -1;
}

int db_planets_get_land_info(db_t *db, int planet_id, int *sector_id, int *owner_id, char **owner_type) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q18 */
    const char *q18 = "SELECT sector_id, owner_id, owner_type FROM planets WHERE planet_id = {1};";
    char sql[512]; sql_build(db, q18, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *sector_id = db_res_col_i32(res, 0, &err);
        *owner_id = db_res_col_i32(res, 1, &err);
        const char *ot = db_res_col_text(res, 2, &err);
        *owner_type = ot ? strdup(ot) : NULL;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_check_player_exists(db_t *db, int player_id, bool *exists) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q19 */
    const char *q19 = "SELECT player_id FROM players WHERE player_id={1}";
    char sql[512]; sql_build(db, q19, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &res, &err)) {
        *exists = db_res_step(res, &err);
        db_res_finalize(res);
        return 0;
    }
    return -1;
}

int db_planets_check_corp_exists(db_t *db, int corp_id, bool *exists) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q20 */
    const char *q20 = "SELECT corporation_id FROM corporations WHERE corporation_id={1}";
    char sql[512]; sql_build(db, q20, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(corp_id) }, 1, &res, &err)) {
        *exists = db_res_step(res, &err);
        db_res_finalize(res);
        return 0;
    }
    return -1;
}

int db_planets_transfer_ownership(db_t *db, int planet_id, int target_id, const char *target_type) {
    db_error_t err;
    /* SQL_VERBATIM: Q21 */
    const char *q21 = "UPDATE planets SET owner_id = {1}, owner_type = {2} WHERE planet_id = {3};";
    char sql[512]; sql_build(db, q21, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(target_id), db_bind_text(target_type), db_bind_i32(planet_id) }, 3, &err)) return 0;
    return -1;
}

int db_planets_get_citadel_level(db_t *db, int planet_id, int *level) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q22 */
    const char *q22 = "SELECT level FROM citadels WHERE planet_id = {1};";
    char sql[512]; sql_build(db, q22, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *level = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_add_treasury(db_t *db, int planet_id, int amount) {
    db_error_t err;
    /* SQL_VERBATIM: Q23 */
    const char *q23 = "UPDATE citadels SET treasury = treasury + {1} WHERE planet_id = {2};";
    char sql[512]; sql_build(db, q23, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(amount), db_bind_i32(planet_id) }, 2, &err)) return 0;
    return -1;
}

int db_planets_get_treasury(db_t *db, int planet_id, long long *balance) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q24 */
    const char *q24 = "SELECT treasury FROM citadels WHERE planet_id = {1};";
    char sql[512]; sql_build(db, q24, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *balance = db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_get_citadel_info(db_t *db, int planet_id, int *level, long long *treasury) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q25 */
    const char *q25 = "SELECT level, treasury FROM citadels WHERE planet_id = {1};";
    char sql[512]; sql_build(db, q25, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *level = db_res_col_i32(res, 0, &err);
        *treasury = db_res_col_i64(res, 1, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_deduct_treasury(db_t *db, int planet_id, int amount) {
    db_error_t err;
    /* SQL_VERBATIM: Q26 */
    const char *q26 = "UPDATE citadels SET treasury = treasury - {1} WHERE planet_id = {2};";
    char sql[512]; sql_build(db, q26, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(amount), db_bind_i32(planet_id) }, 2, &err)) return 0;
    return -1;
}

int db_planets_lookup_genesis_idem(db_t *db, const char *key, char **prev_json) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q27 */
    const char *q27 = "SELECT response FROM idempotency WHERE key = {1} AND cmd = 'planet.genesis_create';";
    char sql[512]; sql_build(db, q27, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(key) }, 1, &res, &err) && db_res_step(res, &err)) {
        const char *tmp = db_res_col_text(res, 0, &err);
        *prev_json = tmp ? strdup(tmp) : NULL;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_is_msl_sector(db_t *db, int sector_id, bool *is_msl) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q28 */
    const char *q28 = "SELECT 1 FROM msl_sectors WHERE sector_id = {1};";
    char sql[512]; sql_build(db, q28, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) {
        *is_msl = db_res_step(res, &err);
        db_res_finalize(res);
        return 0;
    }
    return -1;
}

int db_planets_count_in_sector(db_t *db, int sector_id, int *count) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q29 */
    const char *q29 = "SELECT COUNT(*) FROM planets WHERE sector_id = {1};";
    char sql[512]; sql_build(db, q29, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *count = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_get_ship_genesis(db_t *db, int ship_id, int *torps) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q30 */
    const char *q30 = "SELECT genesis FROM ships WHERE ship_id = {1};";
    char sql[512]; sql_build(db, q30, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *torps = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_get_type_weights(db_t *db, json_t **weights_array) {
    db_res_t *res = NULL;
    db_error_t err;
    *weights_array = json_array();
    /* SQL_VERBATIM: Q31 */
    const char *q31 = "SELECT code, genesis_weight FROM planettypes ORDER BY planettypes_id;";
    if (db_query(db, q31, NULL, 0, &res, &err)) {
        while (db_res_step(res, &err)) {
            json_t *entry = json_object();
            json_object_set_new(entry, "code", json_string(db_res_col_text(res, 0, &err)));
            json_object_set_new(entry, "weight", json_integer(db_res_col_i32(res, 1, &err)));
            json_array_append_new(*weights_array, entry);
        }
        db_res_finalize(res);
        return 0;
    }
    return -1;
}

int db_planets_get_type_id_by_code(db_t *db, const char *code, int *type_id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q32 */
    const char *q32 = "SELECT planettypes_id FROM planettypes WHERE code = {1};";
    char sql[512]; sql_build(db, q32, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(code) }, 1, &res, &err) && db_res_step(res, &err)) {
        *type_id = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_create(db_t *db, int sector_id, const char *name, int owner_id, const char *owner_type, const char *class_str, int type_id, long long ts, int created_by, int64_t *new_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q33 */
    const char *q33 = "INSERT INTO planets (sector_id, name, owner_id, owner_type, class, type, created_at, created_by, genesis_flag) VALUES ({1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, TRUE);";
    char sql[1024]; sql_build(db, q33, sql, sizeof(sql));
    db_bind_t params[] = {
        db_bind_i32 (sector_id), db_bind_text (name), db_bind_i32 (owner_id),
        db_bind_text (owner_type), db_bind_text (class_str), db_bind_i32 (type_id),
        db_bind_timestamp_text (ts), db_bind_i32 (created_by)
    };
    if (db_exec_insert_id(db, sql, params, 8, "planet_id", new_id, &err)) return 0;
    return -1;
}

int db_planets_consume_genesis(db_t *db, int ship_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q34 */
    const char *q34 = "UPDATE ships SET genesis = genesis - 1 WHERE ship_id = {1} AND genesis >= 1;";
    char sql[512]; sql_build(db, q34, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &err)) return 0;
    return -1;
}

int db_planets_update_navhaz(db_t *db, int sector_id, int delta) {
    db_error_t err;
    /* SQL_VERBATIM: Q35 */
    const char *q35 = "UPDATE sectors SET navhaz = GREATEST(0, COALESCE(navhaz, 0) + {1}) WHERE sector_id = {2};";
    char sql[512]; sql_build(db, q35, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(delta), db_bind_i32(sector_id) }, 2, &err)) return 0;
    return -1;
}

int db_planets_insert_genesis_idem(db_t *db, const char *key, const char *payload, long long ts) {
    db_error_t err;
    /* SQL_VERBATIM: Q36 */
    const char *q36 = "INSERT INTO idempotency (key, cmd, response, created_at) VALUES ({1}, 'planet.genesis_create', {2}, {3});";
    char sql[1024]; sql_build(db, q36, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_text(key), db_bind_text(payload), db_bind_timestamp_text(ts) }, 3, &err)) return 0;
    return -1;
}

int db_planets_get_stock(db_t *db, int planet_id, const char *code, int *stock) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q37 */
    const char *q37 = "SELECT quantity FROM entity_stock WHERE entity_type='planet' AND entity_id={1} AND commodity_code={2}";
    char sql[512]; sql_build(db, q37, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id), db_bind_text(code) }, 2, &res, &err) && db_res_step(res, &err)) {
        *stock = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_get_commodity_price(db_t *db, const char *code, int *price) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q38 */
    const char *q38 = "SELECT base_price FROM commodities WHERE code={1}";
    char sql[512]; sql_build(db, q38, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(code) }, 1, &res, &err) && db_res_step(res, &err)) {
        *price = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_get_commodity_id(db_t *db, const char *code, int *id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q39 */
    const char *q39 = "SELECT commodities_id FROM commodities WHERE code = {1};";
    char sql[512]; sql_build(db, q39, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(code) }, 1, &res, &err) && db_res_step(res, &err)) {
        *id = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_add_treasury_buy(db_t *db, int planet_id, long long amount) {
    db_error_t err;
    /* SQL_VERBATIM: Q41 */
    const char *q41 = "UPDATE citadels SET treasury = treasury + {1} WHERE planet_id = {2}";
    char sql[512]; sql_build(db, q41, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i64(amount), db_bind_i32(planet_id) }, 2, &err)) return 0;
    return -1;
}

int db_planets_insert_buy_order(db_t *db, int player_id, int planet_id, int commodity_id, int qty, int price, int64_t *order_id) {
    db_error_t err;
    const char *ts_epoch_str = sql_epoch_now(db);
    /* SQL_VERBATIM: Q42 */
    const char *q42 = "INSERT INTO commodity_orders (actor_type, actor_id, location_type, location_id, commodity_id, side, quantity, price, status, ts, expires_at, filled_quantity) VALUES ({1}, {2}, 'planet', {3}, {4}, 'buy', {5}, {6}, 'open', %s, {7}, 0)";
    char sql_tmpl[1024], sql[1024];
    snprintf(sql_tmpl, sizeof(sql_tmpl), q42, ts_epoch_str);
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    db_bind_t params[] = {
        db_bind_text("player"), db_bind_i32(player_id),
        db_bind_i32(planet_id), db_bind_i32(commodity_id),
        db_bind_i32(qty), db_bind_i32(price), db_bind_null()
    };
    if (db_exec_insert_id(db, sql, params, 7, "id", order_id, &err)) return 0;
    return -1;
}

int db_planets_get_fuel_stock(db_t *db, int planet_id, int *stock) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q44 */
    const char *q44 = "SELECT quantity FROM entity_stock WHERE entity_type='planet' AND entity_id={1} AND commodity_code='FUE'";
    char sql[512]; sql_build(db, q44, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *stock = db_res_col_i32(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_set_sector(db_t *db, int planet_id, int sector_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q45 */
    const char *q45 = "UPDATE planets SET sector_id={1} WHERE planet_id={2}";
    char sql[512]; sql_build(db, q45, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(sector_id), db_bind_i32(planet_id) }, 2, &err)) return 0;
    return -1;
}

int db_planets_get_market_move_info(db_t *db, int planet_id, const char *code, int *current_qty, int *max_ore, int *max_org, int *max_equ) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q46 */
    const char *q46 = "SELECT es.quantity, pt.maxore, pt.maxorganics, pt.maxequipment FROM planets p JOIN planettypes pt ON p.type = pt.planettypes_id LEFT JOIN entity_stock es ON p.planet_id = es.entity_id AND es.entity_type = 'planet' AND es.commodity_code = {2} WHERE p.planet_id = {1};";
    char sql[1024]; sql_build(db, q46, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(planet_id), db_bind_text(code) }, 2, &res, &err) && db_res_step(res, &err)) {
        *current_qty = db_res_col_i32(res, 0, &err);
        *max_ore = db_res_col_i32(res, 1, &err);
        *max_org = db_res_col_i32(res, 2, &err);
        *max_equ = db_res_col_i32(res, 3, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_planets_upsert_stock(db_t *db, int planet_id, const char *code, int quantity) {
    db_error_t err;
    const char *epoch_expr = sql_epoch_now(db);
    const char *sql_fmt = sql_entity_stock_upsert_epoch_fmt(db);
    /* SQL_VERBATIM: Q47 */
    char sql[512];
    snprintf(sql, sizeof(sql), sql_fmt, epoch_expr, epoch_expr);
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(planet_id), db_bind_text(code), db_bind_i32(quantity) }, 3, &err)) return 0;
    return -1;
}

int db_planets_get_commodity_id_v2(db_t *db, const char *code, int *id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q48 */
    const char *q48 = "SELECT id FROM commodities WHERE code = {1} LIMIT 1;";
    char sql[512]; sql_build(db, q48, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(code) }, 1, &res, &err) && db_res_step(res, &err)) {
        *id = db_res_col_int(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}
