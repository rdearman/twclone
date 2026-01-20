#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_ports.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int db_ports_upsert_stock(db_t *db, const char *entity_type, int entity_id, const char *commodity_code, int quantity, int64_t ts) {
    db_error_t err;
    int64_t rows = 0;

    /* 1. Try Update first */
    const char *q_upd = "UPDATE entity_stock SET quantity = {1}, last_updated_ts = {2} WHERE entity_type = {3} AND entity_id = {4} AND commodity_code = {5};";
    char sql_upd[1024]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_i32(quantity), db_bind_i64(ts), db_bind_text(entity_type), db_bind_i32(entity_id), db_bind_text(commodity_code) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 5, &rows, &err) && rows > 0) return 0;


    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) VALUES ({1}, {2}, {3}, {4}, 0, {5});";
    char sql_ins[1024]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    db_bind_t ins_params[] = { db_bind_text(entity_type), db_bind_i32(entity_id), db_bind_text(commodity_code), db_bind_i32(quantity), db_bind_i64(ts) };
    if (!db_exec(db, sql_ins, ins_params, 5, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 5, &err)) return 0;
        }
        return err.code;
    }

    return 0;
}

int db_ports_get_buy_eligibility(db_t *db, int port_id, const char *commodity_code, int *quantity, int *max_capacity) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q2 */
    const char *q2 = "SELECT es.quantity, p.size * 1000 AS max_capacity FROM ports p JOIN entity_stock es ON p.port_id = es.entity_id WHERE es.entity_type = 'port' AND es.commodity_code = {2} AND p.port_id = {1} LIMIT 1;";
    char sql[1024]; sql_build(db, q2, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(port_id), db_bind_text(commodity_code) }, 2, &res, &err) && db_res_step(res, &err)) {
        *quantity = db_res_col_int(res, 0, &err);
        *max_capacity = db_res_col_int(res, 1, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_stock_quantity(db_t *db, const char *entity_type, int entity_id, const char *commodity_code, int *quantity) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q3 */
    const char *q3 = "SELECT quantity FROM entity_stock WHERE entity_type = {1} AND entity_id = {2} AND commodity_code = {3};";
    char sql[512]; sql_build(db, q3, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(entity_type), db_bind_i32(entity_id), db_bind_text(commodity_code) }, 3, &res, &err) && db_res_step(res, &err)) {
        *quantity = db_res_col_int(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return ERR_DB_NOT_FOUND;
}

int db_ports_get_commodity_code(db_t *db, const char *commodity, char **code) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q4 */
    const char *q4 = "SELECT code FROM commodities WHERE UPPER(code) = UPPER({1}) LIMIT 1;";
    char sql[512]; sql_build(db, q4, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(commodity) }, 1, &res, &err) && db_res_step(res, &err)) {
        const char *canonical = db_res_col_text(res, 0, &err);
        *code = canonical ? strdup(canonical) : NULL;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_price_info(db_t *db, int port_id, const char *commodity_code, int *base_price, int *quantity, int *max_capacity, int *techlevel, double *elasticity, double *volatility_factor) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q5 */
    const char *q5 = "SELECT c.base_price, c.volatility,        es.quantity,        p.size * 1000 AS max_capacity,        p.techlevel,        ec.price_elasticity,        ec.volatility_factor FROM commodities c JOIN ports p ON p.port_id = {1} JOIN entity_stock es ON p.port_id = es.entity_id AND es.entity_type = 'port' AND es.commodity_code = c.code JOIN economy_curve ec ON p.economy_curve_id = ec.economy_curve_id WHERE c.code = {2} LIMIT 1;";
    char sql[1024]; sql_build(db, q5, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(port_id), db_bind_text(commodity_code) }, 2, &res, &err) && db_res_step(res, &err)) {
        if (base_price) *base_price = db_res_col_int(res, 0, &err);
        if (quantity) *quantity = db_res_col_int(res, 2, &err);
        if (max_capacity) *max_capacity = db_res_col_int(res, 3, &err);
        if (techlevel) *techlevel = db_res_col_int(res, 4, &err);
        if (elasticity) *elasticity = db_res_col_double(res, 5, &err);
        if (volatility_factor) *volatility_factor = db_res_col_double(res, 6, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_illegal_status(db_t *db, const char *commodity_code, bool *illegal) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q6 */
    const char *q6 = "SELECT illegal FROM commodities WHERE code = {1} LIMIT 1";
    char sql[256]; sql_build(db, q6, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(commodity_code) }, 1, &res, &err) && db_res_step(res, &err)) {
        *illegal = (db_res_col_int(res, 0, &err) != 0);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_port_sector(db_t *db, int port_id, int *sector_id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q7 */
    const char *q7 = "SELECT sector_id FROM ports WHERE port_id = {1} LIMIT 1";
    char sql[256]; sql_build(db, q7, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(port_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *sector_id = db_res_col_int(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_market_move_info(db_t *db, int port_id, const char *commodity_code, int *current_quantity, int *max_capacity) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q8 */
    const char *q8 = "SELECT es.quantity, p.size * 1000 AS max_capacity FROM ports p LEFT JOIN entity_stock es   ON p.port_id = es.entity_id  AND es.entity_type = 'port'  AND es.commodity_code = {2} WHERE p.port_id = {1};";
    char sql[1024]; sql_build(db, q8, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(port_id), db_bind_text(commodity_code) }, 2, &res, &err) && db_res_step(res, &err)) {
        if (db_res_col_is_null(res, 0)) *current_quantity = 0;
        else *current_quantity = db_res_col_int(res, 0, &err);
        *max_capacity = db_res_col_int(res, 1, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_trade_history(db_t *db, int player_id, int limit, json_t **history) {
    db_res_t *res = NULL;
    db_error_t err;
    *history = json_array();
    /* SQL_VERBATIM: Q9 */
    const char *q9 = "SELECT timestamp, trade_log_id as id, port_id, commodity, units, price_per_unit, action FROM trade_log WHERE player_id = {1} ORDER BY timestamp DESC, trade_log_id DESC LIMIT {2};";
    char sql[1024]; sql_build(db, q9, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i32(limit) }, 2, &res, &err)) {
        while (db_res_step(res, &err)) {
            json_t *row = json_object();
            json_object_set_new(row, "timestamp", json_integer(db_res_col_i64(res, 0, &err)));
            json_object_set_new(row, "id", json_integer(db_res_col_i64(res, 1, &err)));
            json_object_set_new(row, "port_id", json_integer(db_res_col_i32(res, 2, &err)));
            const char *cmdty = db_res_col_text(res, 3, &err);
            json_object_set_new(row, "commodity", json_string(cmdty ? cmdty : ""));
            json_object_set_new(row, "units", json_integer(db_res_col_i32(res, 4, &err)));
            json_object_set_new(row, "price_per_unit", json_real(db_res_col_double(res, 5, &err)));
            const char *action = db_res_col_text(res, 6, &err);
            json_object_set_new(row, "action", json_string(action ? action : ""));
            json_array_append_new(*history, row);
        }
        db_res_finalize(res);
        return (err.code == 0) ? 0 : -1;
    }
    return -1;
}

int db_ports_get_trade_history_cursor(db_t *db, int player_id, int limit, int64_t ts, int64_t id, json_t **history) {
    db_res_t *res = NULL;
    db_error_t err;
    *history = json_array();
    /* SQL_VERBATIM: Q10 */
    const char *q10 = "SELECT timestamp, trade_log_id as id, port_id, commodity, units, price_per_unit, action FROM trade_log WHERE player_id = {1} AND (timestamp < {3} OR (timestamp = {3} AND trade_log_id < {4})) ORDER BY timestamp DESC, trade_log_id DESC LIMIT {2};";
    char sql[1024]; sql_build(db, q10, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i32(limit), db_bind_i64(ts), db_bind_i64(id) }, 4, &res, &err)) {
        while (db_res_step(res, &err)) {
            json_t *row = json_object();
            json_object_set_new(row, "timestamp", json_integer(db_res_col_i64(res, 0, &err)));
            json_object_set_new(row, "id", json_integer(db_res_col_i64(res, 1, &err)));
            json_object_set_new(row, "port_id", json_integer(db_res_col_i32(res, 2, &err)));
            const char *cmdty = db_res_col_text(res, 3, &err);
            json_object_set_new(row, "commodity", json_string(cmdty ? cmdty : ""));
            json_object_set_new(row, "units", json_integer(db_res_col_i32(res, 4, &err)));
            json_object_set_new(row, "price_per_unit", json_real(db_res_col_double(res, 5, &err)));
            const char *action = db_res_col_text(res, 6, &err);
            json_object_set_new(row, "action", json_string(action ? action : ""));
            json_array_append_new(*history, row);
        }
        db_res_finalize(res);
        return (err.code == 0) ? 0 : -1;
    }
    return -1;
}

int db_ports_set_ported_status(db_t *db, int ship_id, int port_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q11 */
    const char *q11 = "UPDATE ships SET ported = {1}, onplanet = FALSE WHERE ship_id = {2};";
    char sql[512]; sql_build(db, q11, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_bool(port_id > 0), db_bind_i32(ship_id) }, 2, &err)) return 0;
    return -1;
}

int db_ports_get_ported_status(db_t *db, int ship_id, int *port_id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q12 */
    const char *q12 = "SELECT ported FROM ships WHERE ship_id = {1}";
    char sql[256]; sql_build(db, q12, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(ship_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *port_id = db_res_col_bool(res, 0, &err) ? 1 : 0;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_header_by_id(db_t *db, int port_id, json_t **port_obj, int *port_size) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q14 */
    const char *q14 = "SELECT port_id, number, name, sector_id, size, techlevel, petty_cash, type FROM ports WHERE port_id = {1} LIMIT 1;";
    char sql[512]; sql_build(db, q14, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(port_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *port_obj = json_object();
        json_object_set_new(*port_obj, "id", json_integer(db_res_col_i32(res, 0, &err)));
        json_object_set_new(*port_obj, "number", json_integer(db_res_col_i32(res, 1, &err)));
        const char *name = db_res_col_text(res, 2, &err);
        json_object_set_new(*port_obj, "name", json_string(name ? name : ""));
        json_object_set_new(*port_obj, "sector", json_integer(db_res_col_i32(res, 3, &err)));
        int size = db_res_col_i32(res, 4, &err);
        json_object_set_new(*port_obj, "size", json_integer(size));
        if (port_size) *port_size = size;
        json_object_set_new(*port_obj, "techlevel", json_integer(db_res_col_i32(res, 5, &err)));
        json_object_set_new(*port_obj, "petty_cash", json_integer(db_res_col_i32(res, 6, &err)));
        json_object_set_new(*port_obj, "type", json_integer(db_res_col_i32(res, 7, &err)));
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_header_by_sector(db_t *db, int sector_id, json_t **port_obj, int *port_size) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q15 */
    const char *q15 = "SELECT port_id, number, name, sector_id, size, techlevel, petty_cash, type FROM ports WHERE sector_id = {1} LIMIT 1;";
    char sql[512]; sql_build(db, q15, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *port_obj = json_object();
        json_object_set_new(*port_obj, "id", json_integer(db_res_col_i32(res, 0, &err)));
        json_object_set_new(*port_obj, "number", json_integer(db_res_col_i32(res, 1, &err)));
        const char *name = db_res_col_text(res, 2, &err);
        json_object_set_new(*port_obj, "name", json_string(name ? name : ""));
        json_object_set_new(*port_obj, "sector", json_integer(db_res_col_i32(res, 3, &err)));
        int size = db_res_col_i32(res, 4, &err);
        json_object_set_new(*port_obj, "size", json_integer(size));
        if (port_size) *port_size = size;
        json_object_set_new(*port_obj, "techlevel", json_integer(db_res_col_i32(res, 5, &err)));
        json_object_set_new(*port_obj, "petty_cash", json_integer(db_res_col_i32(res, 6, &err)));
        json_object_set_new(*port_obj, "type", json_integer(db_res_col_i32(res, 7, &err)));
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_commodities(db_t *db, int port_id, json_t **commodities_array) {
    db_res_t *res = NULL;
    db_error_t err;
    *commodities_array = json_array();
    /* SQL_VERBATIM: Q16 */
    const char *q16 = "SELECT es.commodity_code, es.quantity, es.price, c.illegal FROM entity_stock es JOIN commodities c ON es.commodity_code = c.code WHERE es.entity_type = 'port' AND es.entity_id = {1};";
    char sql[1024]; sql_build(db, q16, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(port_id) }, 1, &res, &err)) {
        while (db_res_step(res, &err)) {
            json_t *obj = json_object();
            const char *code = db_res_col_text(res, 0, &err);
            json_object_set_new(obj, "code", json_string(code ? code : ""));
            json_object_set_new(obj, "quantity", json_integer(db_res_col_i32(res, 1, &err)));
            json_object_set_new(obj, "price", json_integer(db_res_col_i32(res, 2, &err)));
            json_object_set_new(obj, "illegal", json_integer(db_res_col_i32(res, 3, &err)));
            json_array_append_new(*commodities_array, obj);
        }
        db_res_finalize(res);
        return (err.code == 0) ? 0 : -1;
    }
    return -1;
}

int db_ports_get_robbery_config(db_t *db, int *threshold, int *xp_per_hold, int *cred_per_xp, double *chance_base, int *turn_cost, double *good_bonus, double *pro_delta, double *evil_cluster_bonus, double *good_penalty_mult, int *ttl_days) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q17 */
    const char *q17 = "SELECT robbery_evil_threshold, robbery_xp_per_hold, robbery_credits_per_xp, robbery_bust_chance_base, robbery_turn_cost, good_guy_bust_bonus, pro_criminal_bust_delta, evil_cluster_bust_bonus, good_align_penalty_mult, robbery_real_bust_ttl_days FROM law_enforcement WHERE law_enforcement_id=1;";
    if (db_query(db, q17, NULL, 0, &res, &err) && db_res_step(res, &err)) {
        if (threshold) *threshold = (int)db_res_col_i64(res, 0, &err);
        if (xp_per_hold) *xp_per_hold = (int)db_res_col_i64(res, 1, &err);
        if (cred_per_xp) *cred_per_xp = (int)db_res_col_i64(res, 2, &err);
        if (chance_base) *chance_base = db_res_col_double(res, 3, &err);
        if (turn_cost) *turn_cost = (int)db_res_col_i64(res, 4, &err);
        if (good_bonus) *good_bonus = db_res_col_double(res, 5, &err);
        if (pro_delta) *pro_delta = db_res_col_double(res, 6, &err);
        if (evil_cluster_bonus) *evil_cluster_bonus = db_res_col_double(res, 7, &err);
        if (good_penalty_mult) *good_penalty_mult = db_res_col_double(res, 8, &err);
        if (ttl_days) *ttl_days = (int)db_res_col_i64(res, 9, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_check_active_bust(db_t *db, int port_id, int player_id, bool *active) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q18 */
    const char *q18 = "SELECT 1 FROM port_busts WHERE port_id={1} AND player_id={2} AND active=TRUE";
    char sql[512]; sql_build(db, q18, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(port_id), db_bind_i64(player_id) }, 2, &res, &err)) {
        *active = db_res_step(res, &err);
        db_res_finalize(res);
        return 0;
    }
    return -1;
}

int db_ports_get_cluster_id(db_t *db, int sector_id, int *cluster_id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q19 */
    const char *q19 = "SELECT cluster_id FROM cluster_sectors WHERE sector_id={1}";
    char sql[512]; sql_build(db, q19, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(sector_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *cluster_id = (int)db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_check_cluster_ban(db_t *db, int cluster_id, int player_id, bool *banned) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q20 */
    const char *q20 = "SELECT banned FROM cluster_player_status WHERE cluster_id={1} AND player_id={2}";
    char sql[512]; sql_build(db, q20, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(cluster_id), db_bind_i64(player_id) }, 2, &res, &err) && db_res_step(res, &err)) {
        *banned = (db_res_col_i64(res, 0, &err) == 1);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_player_xp(db_t *db, int player_id, int *xp) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q21 */
    const char *q21 = "SELECT experience FROM players WHERE player_id={1}";
    char sql[512]; sql_build(db, q21, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *xp = (int)db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_last_rob(db_t *db, int player_id, int *last_port, int64_t *last_ts, bool *success) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q22 */
    const char *q22 = "SELECT port_id, last_attempt_at, was_success FROM player_last_rob WHERE player_id={1}";
    char sql[512]; sql_build(db, q22, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(player_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *last_port = (int)db_res_col_i64(res, 0, &err);
        *last_ts = db_res_col_i64(res, 1, &err);
        *success = (db_res_col_i64(res, 2, &err) != 0);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_insert_fake_bust(db_t *db, int port_id, int player_id) {
    db_error_t err;
    int64_t rows = 0;
    int64_t now_ts = time(NULL);

    /* 1. Try Update first */
    const char *q_upd = "UPDATE port_busts SET last_bust_at={1}, bust_type='fake', active=TRUE WHERE port_id={2} AND player_id={3};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_timestamp_text(now_ts), db_bind_i32(port_id), db_bind_i32(player_id) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 3, &rows, &err) && rows > 0) return 0;


    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO port_busts (port_id, player_id, last_bust_at, bust_type, active) VALUES ({1}, {2}, {3}, 'fake', 1);";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    db_bind_t ins_params[] = { db_bind_i32(port_id), db_bind_i32(player_id), db_bind_timestamp_text(now_ts) };
    if (!db_exec(db, sql_ins, ins_params, 3, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 3, &err)) return 0;
        }
        return err.code;
    }

    return 0;
}

int db_ports_update_last_rob_attempt(db_t *db, int player_id, int port_id) {
    db_error_t err;
    int64_t rows = 0;
    int64_t now_ts = time(NULL);

    /* 1. Try Update first */
    const char *q_upd = "UPDATE player_last_rob SET port_id={1}, last_attempt_at={2}, was_success=FALSE WHERE player_id={3};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_i32(port_id), db_bind_timestamp_text(now_ts), db_bind_i32(player_id) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 3, &rows, &err) && rows > 0) return 0;


    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO player_last_rob (player_id, port_id, last_attempt_at, was_success) VALUES ({1}, {2}, {3}, 0);";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    db_bind_t ins_params[] = { db_bind_i32(player_id), db_bind_i32(port_id), db_bind_timestamp_text(now_ts) };
    if (!db_exec(db, sql_ins, ins_params, 3, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 3, &err)) return 0;
        }
        return err.code;
    }

    return 0;
}

int db_ports_get_cash(db_t *db, int port_id, int64_t *cash) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q25 */
    const char *q25 = "SELECT petty_cash FROM ports WHERE port_id={1}";
    char sql[256]; sql_build(db, q25, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(port_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *cash = db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_update_cash(db_t *db, int port_id, int64_t loot) {
    db_error_t err;
    /* SQL_VERBATIM: Q26 */
    const char *q26 = "UPDATE ports SET petty_cash = petty_cash - {1} WHERE port_id={2}";
    char sql[512]; sql_build(db, q26, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i64(loot), db_bind_i64(port_id) }, 2, &err)) return 0;
    return -1;
}

int db_ports_increase_suspicion(db_t *db, int cluster_id, int player_id, int susp_inc) {
    db_error_t err;
    int64_t rows = 0;

    /* 1. Try Update first */
    const char *q_upd = "UPDATE cluster_player_status SET suspicion = suspicion + {1} WHERE cluster_id = {2} AND player_id = {3};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_i32(susp_inc), db_bind_i32(cluster_id), db_bind_i32(player_id) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 3, &rows, &err) && rows > 0) return 0;


    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO cluster_player_status (cluster_id, player_id, suspicion) VALUES ({1}, {2}, {3});";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    db_bind_t ins_params[] = { db_bind_i32(cluster_id), db_bind_i32(player_id), db_bind_i32(susp_inc) };
    if (!db_exec(db, sql_ins, ins_params, 3, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 3, &err)) return 0;
        }
        return err.code;
    }

    return 0;
}

int db_ports_update_last_rob_success(db_t *db, int player_id, int port_id) {
    db_error_t err;
    int64_t rows = 0;
    int64_t now_ts = time(NULL);

    /* 1. Try Update first */
    const char *q_upd = "UPDATE player_last_rob SET port_id={1}, last_attempt_at={2}, was_success=TRUE WHERE player_id={3};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_i32(port_id), db_bind_timestamp_text(now_ts), db_bind_i32(player_id) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 3, &rows, &err) && rows > 0) return 0;


    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO player_last_rob (player_id, port_id, last_attempt_at, was_success) VALUES ({1}, {2}, {3}, 1);";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    db_bind_t ins_params[] = { db_bind_i32(player_id), db_bind_i32(port_id), db_bind_timestamp_text(now_ts) };
    if (!db_exec(db, sql_ins, ins_params, 3, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 3, &err)) return 0;
        }
        return err.code;
    }

    return 0;
}

int db_ports_insert_real_bust(db_t *db, int port_id, int player_id) {
    db_error_t err;
    int64_t rows = 0;
    int64_t now_ts = time(NULL);

    /* 1. Try Update first */
    const char *q_upd = "UPDATE port_busts SET last_bust_at={1}, bust_type='real', active=TRUE WHERE port_id={2} AND player_id={3};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_timestamp_text(now_ts), db_bind_i32(port_id), db_bind_i32(player_id) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 3, &rows, &err) && rows > 0) return 0;


    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO port_busts (port_id, player_id, last_bust_at, bust_type, active) VALUES ({1}, {2}, {3}, 'real', 1);";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    db_bind_t ins_params[] = { db_bind_i32(port_id), db_bind_i32(player_id), db_bind_timestamp_text(now_ts) };
    if (!db_exec(db, sql_ins, ins_params, 3, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 3, &err)) return 0;
        }
        return err.code;
    }

    return 0;
}

int db_ports_update_cluster_bust(db_t *db, int cluster_id, int player_id, int susp_inc) {
    db_error_t err;
    int64_t rows = 0;
    int64_t now_ts = time(NULL);

    /* 1. Try Update first */
    const char *q_upd = "UPDATE cluster_player_status SET suspicion = suspicion + {1}, bust_count = bust_count + 1, last_bust_at = {2} WHERE cluster_id = {3} AND player_id = {4};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_i32(susp_inc), db_bind_timestamp_text(now_ts), db_bind_i32(cluster_id), db_bind_i32(player_id) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 4, &rows, &err) && rows > 0) return 0;


    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO cluster_player_status (cluster_id, player_id, suspicion, bust_count, last_bust_at) VALUES ({1}, {2}, {3}, 1, {4});";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    db_bind_t ins_params[] = { db_bind_i32(cluster_id), db_bind_i32(player_id), db_bind_i32(susp_inc), db_bind_timestamp_text(now_ts) };
    if (!db_exec(db, sql_ins, ins_params, 4, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 4, &err)) return 0;
        }
        return err.code;
    }

    return 0;
}

int db_ports_ban_player_in_cluster(db_t *db, int cluster_id, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q31 */
    const char *q31 = "UPDATE cluster_player_status SET banned=1 WHERE cluster_id={1} AND player_id={2} AND wanted_level >= 3";
    char sql[512]; sql_build(db, q31, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i64(cluster_id), db_bind_i64(player_id) }, 2, &err)) return 0;
    return -1;
}

int db_ports_update_last_rob_fail(db_t *db, int player_id, int port_id) {
    db_error_t err;
    int64_t rows = 0;
    int64_t now_ts = time(NULL);

    /* 1. Try Update first */
    const char *q_upd = "UPDATE player_last_rob SET port_id={1}, last_attempt_at={2}, was_success=FALSE WHERE player_id={3};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    db_bind_t upd_params[] = { db_bind_i32(port_id), db_bind_timestamp_text(now_ts), db_bind_i32(player_id) };
    if (db_exec_rows_affected(db, sql_upd, upd_params, 3, &rows, &err) && rows > 0) return 0;


    /* 2. Try Insert if update affected 0 rows */
    const char *q_ins = "INSERT INTO player_last_rob (player_id, port_id, last_attempt_at, was_success) VALUES ({1}, {2}, {3}, 0);";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    db_bind_t ins_params[] = { db_bind_i32(player_id), db_bind_i32(port_id), db_bind_timestamp_text(now_ts) };
    if (!db_exec(db, sql_ins, ins_params, 3, &err)) {
        /* 3. If Insert failed due to constraint (concurrent write), retry Update once */
        if (err.code == ERR_DB_CONSTRAINT) {
            if (db_exec(db, sql_upd, upd_params, 3, &err)) return 0;
        }
        return err.code;
    }

    return 0;
}

int db_ports_lookup_idemp(db_t *db, const char *key, int player_id, int sector_id, char **req_json, char **resp_json) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q33 */
    const char *q33 = "SELECT request_json, response_json FROM trade_idempotency WHERE key = {1} AND player_id = {2} AND sector_id = {3};";
    char sql[1024]; sql_build(db, q33, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(key), db_bind_i64(player_id), db_bind_i64(sector_id) }, 3, &res, &err) && db_res_step(res, &err)) {
        const char *req = db_res_col_text(res, 0, &err);
        const char *resp = db_res_col_text(res, 1, &err);
        if (req_json) *req_json = req ? strdup(req) : NULL;
        if (resp_json) *resp_json = resp ? strdup(resp) : NULL;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_check_port_id(db_t *db, int port_id, bool *exists) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q34 */
    const char *q34 = "SELECT port_id FROM ports WHERE port_id = {1} LIMIT 1;";
    char sql[256]; sql_build(db, q34, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(port_id) }, 1, &res, &err)) {
        *exists = db_res_step(res, &err);
        db_res_finalize(res);
        return 0;
    }
    return -1;
}

int db_ports_check_port_sector(db_t *db, int sector_id, int *port_id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q35 */
    const char *q35 = "SELECT port_id FROM ports WHERE sector_id = {1} LIMIT 1;";
    char sql[256]; sql_build(db, q35, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i64(sector_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *port_id = (int)db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_log_trade_sell(db_t *db, int player_id, int port_id, int sector_id, const char *commodity, int amount, int buy_price) {
    db_error_t err;
    const char *now_ts = sql_now_expr(db);
    /* SQL_VERBATIM: Q36 */
    const char *q36 = "INSERT INTO trade_log (player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) VALUES ({1}, {2}, {3}, {4}, {5}, {6}, 'sell', %s);";
    char sql_tmpl[512], sql[512]; snprintf(sql_tmpl, sizeof(sql_tmpl), q36, now_ts);
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i32(port_id), db_bind_i32(sector_id), db_bind_text(commodity), db_bind_i32(amount), db_bind_i32(buy_price) }, 6, &err)) return 0;
    return -1;
}

int db_ports_insert_idemp_sell(db_t *db, const char *key, int player_id, int sector_id, const char *req_json, const char *resp_json) {
    db_error_t err;
    const char *now_ts = sql_now_expr(db);
    /* SQL_VERBATIM: Q37 */
    const char *q37 = "INSERT INTO trade_idempotency (key, player_id, sector_id, request_json, response_json, created_at) VALUES ({1}, {2}, {3}, {4}, {5}, %s);";
    char sql_tmpl[1024], sql[1024]; snprintf(sql_tmpl, sizeof(sql_tmpl), q37, now_ts);
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_text(key), db_bind_i64(player_id), db_bind_i64(sector_id), req_json ? db_bind_text(req_json) : db_bind_null(), resp_json ? db_bind_text(resp_json) : db_bind_null() }, 5, &err)) return 0;
    return -1;
}

int db_ports_lookup_idemp_race(db_t *db, const char *key, int player_id, int sector_id, char **req_json, char **resp_json) {
    return db_ports_lookup_idemp(db, key, player_id, sector_id, req_json, resp_json); /* Verbatim Q38 == Q33 */
}

int db_ports_lookup_idemp_buy(db_t *db, const char *key, int player_id, int sector_id, char **req_json, char **resp_json) {
    return db_ports_lookup_idemp(db, key, player_id, sector_id, req_json, resp_json); /* Verbatim Q39 == Q33 */
}

int db_ports_check_port_id_buy(db_t *db, int port_id, int *found_id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q40 */
    const char *q40 = "SELECT port_id FROM ports WHERE port_id = {1} LIMIT 1;";
    char sql[256]; sql_build(db, q40, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(port_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *found_id = (int)db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_check_port_sector_buy(db_t *db, int sector_id, int *found_id) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q41 */
    const char *q41 = "SELECT port_id FROM ports WHERE sector_id = {1} LIMIT 1;";
    char sql[256]; sql_build(db, q41, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err) && db_res_step(res, &err)) {
        *found_id = (int)db_res_col_i64(res, 0, &err);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_log_trade_buy(db_t *db, int player_id, int port_id, int sector_id, const char *commodity, int amount, int unit_price) {
    db_error_t err;
    const char *now_ts = sql_now_expr(db);
    /* SQL_VERBATIM: Q42 */
    const char *q42 = "INSERT INTO trade_log (player_id, port_id, sector_id, commodity, units, price_per_unit, action, timestamp) VALUES ({1},{2},{3},{4},{5},{6},'buy',%s)";
    char sql_tmpl[512], sql[512]; snprintf(sql_tmpl, sizeof(sql_tmpl), q42, now_ts);
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id), db_bind_i32(port_id), db_bind_i32(sector_id), db_bind_text(commodity), db_bind_i32(amount), db_bind_i32(unit_price) }, 6, &err)) return 0;
    return -1;
}

int db_ports_apply_alignment_hit(db_t *db, int player_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q43 */
    const char *q43 = "UPDATE players SET alignment = alignment - 10 WHERE player_id = {1};";
    char sql[256]; sql_build(db, q43, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_i32(player_id) }, 1, &err)) return 0;
    return -1;
}

int db_ports_insert_idemp_buy(db_t *db, const char *key, int player_id, int sector_id, const char *req_json, const char *resp_json) {
    db_error_t err;
    const char *now_ts = sql_now_expr(db);
    /* SQL_VERBATIM: Q44 */
    const char *q44 = "INSERT INTO trade_idempotency (key, player_id, sector_id, request_json, response_json, created_at) VALUES ({1},{2},{3},{4},{5},%s);";
    char sql_tmpl[1024], sql[1024]; snprintf(sql_tmpl, sizeof(sql_tmpl), q44, now_ts);
    sql_build(db, sql_tmpl, sql, sizeof(sql));
    if (db_exec(db, sql, (db_bind_t[]){ db_bind_text(key), db_bind_i32(player_id), db_bind_i32(sector_id), db_bind_text(req_json), db_bind_text(resp_json) }, 5, &err)) return 0;
    return -1;
}

int db_ports_lookup_idemp_buy_race(db_t *db, const char *key, int player_id, int sector_id, char **req_json, char **resp_json) {
    return db_ports_lookup_idemp(db, key, player_id, sector_id, req_json, resp_json); /* Verbatim Q45 == Q33 */
}

int db_ports_replay_idemp_buy(db_t *db, const char *key, int player_id, int sector_id, char **resp_json) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q46 */
    const char *q46 = "SELECT response_json FROM trade_idempotency WHERE key = {1} AND player_id = {2} AND sector_id = {3};";
    char sql[1024]; sql_build(db, q46, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(key), db_bind_i32(player_id), db_bind_i32(sector_id) }, 3, &res, &err) && db_res_step(res, &err)) {
        const char *resp = db_res_col_text(res, 0, &err);
        if (resp_json) *resp_json = resp ? strdup(resp) : NULL;
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return -1;
}

int db_ports_get_commodity_details(db_t *db, int port_id, const char *commodity_code, int *quantity, int *max_capacity, bool *buys, bool *sells) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q47 */
    const char *q47 = "SELECT es.quantity, p.size * 1000 AS max_capacity,        (CASE WHEN c.code IN ('ORE', 'ORG', 'EQU') THEN 1 ELSE 0 END) AS buys_commodity,        (CASE WHEN c.code IN ('ORE', 'ORG', 'EQU') THEN 1 ELSE 0 END) AS sells_commodity FROM ports p LEFT JOIN entity_stock es   ON p.port_id = es.entity_id  AND es.entity_type = 'port'  AND es.commodity_code = {2} JOIN commodities c ON c.code = {2} WHERE p.port_id = {1};";
    char sql[1024]; sql_build(db, q47, sql, sizeof(sql));
    if (db_query(db, sql, (db_bind_t[]){ db_bind_i32(port_id), db_bind_text(commodity_code) }, 2, &res, &err) && db_res_step(res, &err)) {
        if (quantity) {
            if (db_res_col_is_null(res, 0)) *quantity = 0;
            else *quantity = db_res_col_int(res, 0, &err);
        }
        if (max_capacity) *max_capacity = db_res_col_int(res, 1, &err);
        if (buys) *buys = (db_res_col_int(res, 2, &err) != 0);
        if (sells) *sells = (db_res_col_int(res, 3, &err) != 0);
        db_res_finalize(res);
        return 0;
    }
    if (res) db_res_finalize(res);
    return ERR_DB_NOT_FOUND;
}
