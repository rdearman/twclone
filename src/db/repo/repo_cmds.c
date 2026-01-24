#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_cmds.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

int repo_cmds_get_port_name(db_t *db, int32_t port_id, char *name_out, size_t name_sz)
{
    /* SQL_VERBATIM: Q1 */
    const char *sql = "SELECT name FROM ports WHERE port_id = {1};";
    char sql_converted[256];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i64(port_id) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            const char *name = db_res_col_text(res, 0, &err);
            if (name && name_out) {
                strncpy(name_out, name, name_sz - 1);
                name_out[name_sz - 1] = '\0';
            }
        }
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_cmds_get_login_info(db_t *db, const char *username, int32_t *player_id_out, char *pass_out, size_t pass_sz, bool *is_npc_out)
{
    /* SQL_VERBATIM: Q2 */
    const char *sql = "SELECT player_id, passwd, is_npc FROM players WHERE name = {1};";
    char sql_converted[256];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_text(username) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (player_id_out) *player_id_out = db_res_col_i32(res, 0, &err);
            const char *pw = db_res_col_text(res, 1, &err);
            if (pw && pass_out) {
                strncpy(pass_out, pw, pass_sz - 1);
                pass_out[pass_sz - 1] = '\0';
            }
            if (is_npc_out) *is_npc_out = (bool)db_res_col_i32(res, 2, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1; // Not found
    }
    return err.code;
}

int repo_cmds_register_player(db_t *db, const char *user, const char *pass, const char *ship_name, int64_t *player_id_out)
{
    /* SQL_VERBATIM: Q3 */
    const char *sql_reg = "SELECT register_player({1}, {2}, {3})";
    char sql_register[256];
    sql_build(db, sql_reg, sql_register, sizeof(sql_register));

    db_error_t err;
    if (!db_exec_insert_id(db,
                           sql_register,
                           (db_bind_t[]){ db_bind_text(user),
                                         db_bind_text(pass),
                                         db_bind_text(ship_name) },
                           3,
                           NULL,
                           player_id_out,
                           &err)) {
        return err.code;
    }
    return 0;
}

int repo_cmds_upsert_turns(db_t *db, int64_t player_id)
{
    db_error_t err;
    int64_t rows = 0;
    int32_t turns_per_day = 0;
    int64_t now_ts = time(NULL);

    // Get turns_per_day from config table first
    db_res_t *res = NULL;
    const char *sql_get_turns = "SELECT value FROM config WHERE key='turnsperday';";
    char sql_get_turns_converted[256];
    sql_build(db, sql_get_turns, sql_get_turns_converted, sizeof(sql_get_turns_converted));
    if (db_query(db, sql_get_turns_converted, NULL, 0, &res, &err) && db_res_step(res, &err)) {
        turns_per_day = (int32_t)db_res_col_i64(res, 0, &err);
    }
    if(res) db_res_finalize(res);
    if(err.code != 0) return err.code;


    /* 1. Try Update first */
    const char *q_upd = "UPDATE turns SET turns_remaining = {1}, last_update = {2} WHERE player_id = {3};";
    char sql_upd[256]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_i64(turns_per_day), db_bind_timestamp_text(now_ts), db_bind_i64(player_id) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 3, &rows, &err) && rows > 0) return 0;


    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO turns (player_id, turns_remaining, last_update) VALUES ({1}, {2}, {3});";
    char sql_ins[256]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    db_bind_t ins_params[] = { db_bind_i64(player_id), db_bind_i64(turns_per_day), db_bind_timestamp_text(now_ts) };
    if (!db_exec(db, sql_ins, ins_params, 3, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 3, &err)) return 0;
        }
        return err.code;
    }

    return 0;
}

int repo_cmds_get_bounties(db_t *db, int alignment, db_res_t **out_res)
{
    const char *sql = NULL;
    if (alignment >= 0) {
        /* SQL_VERBATIM: Q5 */
        sql =
            "SELECT b.bounties_id, b.target_id, p.name, b.reward, b.posted_by_type "
            "FROM bounties b "
            "JOIN players p ON b.target_id = p.player_id "
            "WHERE b.status = 'open' AND p.alignment < 0 "
            "ORDER BY b.reward DESC LIMIT 20;";
    } else {
        /* SQL_VERBATIM: Q6 */
        sql =
            "SELECT b.bounties_id, b.target_id, p.name, b.reward, b.posted_by_type "
            "FROM bounties b "
            "JOIN players p ON b.target_id = p.player_id "
            "WHERE b.status = 'open' AND (p.alignment > 0 OR b.posted_by_type = 'gov') "
            "ORDER BY b.reward DESC LIMIT 20;";
    }

    db_error_t err;
    if (db_query(db, sql, NULL, 0, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_cmds_get_planet_info(db_t *db, int32_t planet_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q7 */
    const char *sql_planet =
        "SELECT planet_id, name, type, owner_id, owner_type FROM planets WHERE planet_id = {1};";
    char sql_planet_converted[256];
    sql_build(db, sql_planet, sql_planet_converted, sizeof(sql_planet_converted));

    db_error_t err;
    if (db_query(db, sql_planet_converted, (db_bind_t[]){ db_bind_i64(planet_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_cmds_get_planet_stock(db_t *db, int32_t planet_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q8 */
    const char *sql_stock =
        "SELECT es.commodity_code, c.commodities_id, es.quantity "
        "FROM entity_stock es "
        "JOIN commodities c ON es.commodity_code = c.code "
        "WHERE es.entity_type = 'planet' AND es.entity_id = {1};";
    char sql_stock_converted[384];
    sql_build(db, sql_stock, sql_stock_converted, sizeof(sql_stock_converted));

    db_error_t err;
    if (db_query(db, sql_stock_converted, (db_bind_t[]){ db_bind_i64(planet_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_cmds_get_port_stock(db_t *db, int32_t port_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q10 */
    const char *sql_stock =
        "SELECT es.commodity_code, c.commodities_id, es.quantity "
        "FROM entity_stock es "
        "JOIN commodities c ON es.commodity_code = c.code "
        "WHERE es.entity_type = 'port' AND es.entity_id = {1};";
    char sql_stock_converted[384];
    sql_build(db, sql_stock, sql_stock_converted, sizeof(sql_stock_converted));

    db_error_t err;
    if (db_query(db, sql_stock_converted, (db_bind_t[]){ db_bind_i64(port_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_cmds_get_port_info(db_t *db, int32_t port_id, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q9 */
    const char *sql_port = "SELECT port_id, name, size FROM ports WHERE port_id = {1};";
    char sql_port_converted[256];
    sql_build(db, sql_port, sql_port_converted, sizeof(sql_port_converted));

    db_error_t err;
    if (db_query(db, sql_port_converted, (db_bind_t[]){ db_bind_i64(port_id) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}
