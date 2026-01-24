#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_ships.h"
#include "db/sql_driver.h"
#include <stdio.h>
#include <string.h>

int repo_ships_handle_destruction(db_t *db,
                                  int64_t victim_pid,
                                  int64_t victim_sid,
                                  int64_t killer_pid,
                                  const char *cause,
                                  int64_t sector_id,
                                  int64_t xp_loss_flat,
                                  int64_t xp_loss_percent,
                                  int64_t max_per_day,
                                  int64_t big_sleep,
                                  int64_t now_ts,
                                  int32_t *result_code_out)
{
    /* SQL_VERBATIM: Q1 */
    const char *sql =
        "SELECT result_code "
        "FROM public.handle_ship_destruction("
        "{1},{2},{3},{4},{5},"
        "{6},{7},{8},{9},{10},{11}"
        ");";

    db_bind_t params[] = {
        db_bind_i64(victim_pid),
        db_bind_i64(victim_sid),
        db_bind_i64(killer_pid),
        db_bind_text(cause),
        db_bind_i64(sector_id),
        db_bind_i64(xp_loss_flat),
        db_bind_i64(xp_loss_percent),
        db_bind_i64(max_per_day),
        db_bind_i64(big_sleep),
        db_bind_i64(now_ts),
        db_bind_i64(0)
    };

    db_res_t *res = NULL;
    db_error_t err;
    char sql_converted[1024];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    if (!db_query(db, sql_converted, params, 11, &res, &err)) {
        return err.code;
    }

    if (db_res_step(res, &err)) {
        if (result_code_out) *result_code_out = db_res_col_i32(res, 0, &err);
    }
    db_res_finalize(res);
    return 0;
}

int repo_ships_get_active_id(db_t *db, int32_t player_id, int32_t *ship_id_out)
{
    /* SQL_VERBATIM: Q2 */
    const char *sql_template = "SELECT ship_id FROM players WHERE player_id = {1};";
    char sql[256];
    sql_build(db, sql_template, sql, sizeof sql);

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (ship_id_out) *ship_id_out = db_res_col_i32(res, 0, &err);
        }
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_ships_get_towing_id(db_t *db, int32_t ship_id, int32_t *towing_id_out)
{
    /* SQL_VERBATIM: Q3 */
    const char *sql_template = "SELECT towing_ship_id FROM ships WHERE ship_id = {1};";
    char sql[256];
    sql_build(db, sql_template, sql, sizeof sql);

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (towing_id_out) *towing_id_out = db_res_col_i32(res, 0, &err);
        }
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_ships_clear_towing_id(db_t *db, int32_t ship_id)
{
    /* SQL_VERBATIM: Q4 */
    const char *sql_template = "UPDATE ships SET towing_ship_id = 0 WHERE ship_id = {1};";
    char sql[256];
    sql_build(db, sql_template, sql, sizeof sql);

    db_error_t err;
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, &err)) {
        return err.code;
    }
    return 0;
}

int repo_ships_clear_is_being_towed_by(db_t *db, int32_t ship_id)
{
    /* SQL_VERBATIM: Q5 */
    const char *sql_template = "UPDATE ships SET is_being_towed_by = 0 WHERE ship_id = {1};";
    char sql[256];
    sql_build(db, sql_template, sql, sizeof sql);

    db_error_t err;
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, &err)) {
        return err.code;
    }
    return 0;
}

int repo_ships_get_is_being_towed_by(db_t *db, int32_t ship_id, int32_t *towed_by_id_out)
{
    /* SQL_VERBATIM: Q6 */
    const char *sql_template = "SELECT is_being_towed_by FROM ships WHERE ship_id = {1};";
    char sql[256];
    sql_build(db, sql_template, sql, sizeof sql);

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (towed_by_id_out) *towed_by_id_out = db_res_col_i32(res, 0, &err);
        }
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_ships_set_towing_id(db_t *db, int32_t ship_id, int32_t target_ship_id)
{
    /* SQL_VERBATIM: Q7 */
    const char *sql_template = "UPDATE ships SET towing_ship_id = {1} WHERE ship_id = {2};";
    char sql[256];
    sql_build(db, sql_template, sql, sizeof sql);

    db_error_t err;
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(target_ship_id), db_bind_i64(ship_id) }, 2, &err)) {
        return err.code;
    }
    return 0;
}

int repo_ships_set_is_being_towed_by(db_t *db, int32_t target_ship_id, int32_t player_ship_id)
{
    /* SQL_VERBATIM: Q8 */
    const char *sql_template = "UPDATE ships SET is_being_towed_by = {1} WHERE ship_id = {2};";
    char sql[256];
    sql_build(db, sql_template, sql, sizeof sql);

    db_error_t err;
    if (!db_exec(db, sql, (db_bind_t[]){ db_bind_i64(player_ship_id), db_bind_i64(target_ship_id) }, 2, &err)) {
        return err.code;
    }
    return 0;
}

int repo_ships_get_cargo_and_holds(db_t *db,
                                   int32_t ship_id,
                                   int *ore,
                                   int *organics,
                                   int *equipment,
                                   int *colonists,
                                   int *slaves,
                                   int *weapons,
                                   int *drugs,
                                   int *holds)
{
    /* SQL_VERBATIM: Q9 */
    /* SQL_VERBATIM: Q11 */
    const char *sql =
        "SELECT ore, organics, equipment, colonists, slaves, weapons, drugs, holds FROM ships WHERE ship_id = {1};";

    char sql_final[512];
    sql_build(db, sql, sql_final, sizeof(sql_final));

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_final, (db_bind_t[]){ db_bind_i64(ship_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (ore) *ore = db_res_col_i32(res, 0, &err);
            if (organics) *organics = db_res_col_i32(res, 1, &err);
            if (equipment) *equipment = db_res_col_i32(res, 2, &err);
            if (colonists) *colonists = db_res_col_i32(res, 3, &err);
            if (slaves) *slaves = db_res_col_i32(res, 4, &err);
            if (weapons) *weapons = db_res_col_i32(res, 5, &err);
            if (drugs) *drugs = db_res_col_i32(res, 6, &err);
            if (holds) *holds = db_res_col_i32(res, 7, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1; // Not found
    }
    return err.code;
}

int repo_ships_update_cargo_column(db_t *db, int32_t ship_id, const char *col_name, int32_t new_qty)
{
    /* SQL_VERBATIM: Q10 */
    char sql_up[512];
    snprintf(sql_up,
             sizeof(sql_up),
             "UPDATE ships SET %s = {1} WHERE ship_id = {2};",
             col_name);

    char sql_up_converted[512];
    sql_build(db, sql_up, sql_up_converted, sizeof(sql_up_converted));

    db_error_t err;
    if (!db_exec(db,
                 sql_up_converted,
                 (db_bind_t[]){ db_bind_i64(new_qty), db_bind_i64(ship_id) },
                 2,
                 &err)) {
        return err.code;
    }
    return 0;
}
