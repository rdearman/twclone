#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_engine_consumer.h"
#include "db/sql_driver.h"
#include <stdio.h>
#include <string.h>

int repo_engine_load_watermark(db_t *db, const char *key, long long *last_id, long long *last_ts)
{
    char epoch_expr[256];
    if (sql_ts_to_epoch_expr(db, "last_event_ts", epoch_expr, sizeof(epoch_expr)) != 0) {
        return -1;
    }

    /* SQL_VERBATIM: Q1 */
    char sql_template[512];
    snprintf(sql_template, sizeof(sql_template),
             "SELECT last_event_id, %s FROM engine_offset WHERE key={1};",
             epoch_expr);

    char sql[512];
    sql_build(db, sql_template, sql, sizeof(sql));

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(key) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (last_id) *last_id = db_res_col_i64(res, 0, &err);
            if (last_ts) *last_ts = db_res_col_i64(res, 1, &err);
        } else {
            if (last_id) *last_id = 0;
            if (last_ts) *last_ts = 0;
        }
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_engine_save_watermark(db_t *db, const char *key, long long last_id, long long last_ts)
{
    db_error_t err;
    int64_t rows = 0;
    db_bind_t params[] = { db_bind_text(key), db_bind_i64(last_id), db_bind_timestamp_text(last_ts) };

    /* 1. Try Update first */
    const char *q_upd = "UPDATE engine_offset SET last_event_id={2}, last_event_ts={3} WHERE key={1};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    if (db_exec_rows_affected(db, sql_upd, params, 3, &rows, &err) && rows > 0) return 0;

    /* 2. Try Insert */
    const char *q_ins = "INSERT INTO engine_offset(key, last_event_id, last_event_ts) VALUES({1}, {2}, {3});";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, params, 3, &err)) {
        if (err.code == ERR_DB_CONSTRAINT) {
            /* 3. Concurrent write - retry Update once */
            if (db_exec(db, sql_upd, params, 3, &err)) return 0;
        }
        return err.code;
    }
    return 0;
}

int repo_engine_fetch_max_event_id(db_t *db, long long *max_id)
{
    /* SQL_VERBATIM: Q3 */
    const char *sql = "SELECT COALESCE(MAX(engine_events_id),0) FROM engine_events;";
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, NULL, 0, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (max_id) *max_id = db_res_col_i64(res, 0, &err);
        }
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_engine_quarantine(db_t *db, int64_t id, int64_t ts, const char *type, const char *payload, const char *err_msg, int now_s)
{
    db_error_t err;
    int64_t rows = 0;
    db_bind_t params[] = {
        db_bind_i64(id),
        db_bind_timestamp_text(ts),
        db_bind_text(type ? type : ""),
        db_bind_text(payload ? payload : ""),
        db_bind_text(err_msg ? err_msg : "unknown error"),
        db_bind_timestamp_text((int64_t)now_s)
    };

    /* 1. Try Update first */
    const char *q_upd = "UPDATE engine_events_deadletter SET error={5}, moved_at={6} WHERE engine_events_deadletter_id={1};";
    char sql_upd[512]; sql_build(db, q_upd, sql_upd, sizeof(sql_upd));
    if (db_exec_rows_affected(db, sql_upd, params, 6, &rows, &err) && rows > 0) return 0;

    /* 2. Try Insert */
    const char *q_ins = "INSERT INTO engine_events_deadletter(engine_events_deadletter_id, ts, type, payload, error, moved_at) "
                        "VALUES({1}, {2}, {3}, {4}, {5}, {6});";
    char sql_ins[512]; sql_build(db, q_ins, sql_ins, sizeof(sql_ins));
    if (!db_exec(db, sql_ins, params, 6, &err)) {
        if (err.code == ERR_DB_CONSTRAINT) {
            /* 3. Concurrent write - retry Update once */
            if (db_exec(db, sql_upd, params, 6, &err)) return 0;
        }
        return err.code;
    }
    return 0;
}

int repo_engine_get_ship_id(db_t *db, int32_t player_id, int32_t *ship_id_out)
{
    /* SQL_VERBATIM: Q5 */
    char sql_get_ship_id[512];
    sql_build(db,
              "SELECT ship_id FROM players WHERE player_id = {1};",
              sql_get_ship_id, sizeof(sql_get_ship_id));
    db_bind_t params[] = { db_bind_i64(player_id) };

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_get_ship_id, params, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            if (ship_id_out) *ship_id_out = db_res_col_i32(res, 0, &err);
        }
        db_res_finalize(res);
        return 0;
    }
    return err.code;
}

int repo_engine_get_ship_name(db_t *db, int32_t ship_id, char *name_out, size_t name_sz)
{
    /* SQL_VERBATIM: Q6 */
    char sql_get_ship_name[512];
    sql_build(db,
              "SELECT name FROM ships WHERE ship_id = {1};",
              sql_get_ship_name, sizeof(sql_get_ship_name));
    db_bind_t params[] = { db_bind_i64(ship_id) };

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_get_ship_name, params, 1, &res, &err)) {
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

int repo_engine_fetch_events(db_t *db, int64_t last_id, int priority_only, const char *prio_json, int limit, db_res_t **out_res)
{
    char expanded_sql[1024];
    if (db_backend(db) == DB_BACKEND_POSTGRES) {
        /* SQL_VERBATIM: Q7 (PG) */
        char expanded_sql_tmpl[1024];
        snprintf(expanded_sql_tmpl,
                 sizeof(expanded_sql_tmpl),
                 "SELECT engine_events_id as id, ts, type, actor_player_id, sector_id, payload "
                 "FROM engine_events "
                 "WHERE engine_events_id > {1} "
                 "  AND ({2} = FALSE OR type IN (SELECT trim(json_array_elements_text({3})))) "
                 "ORDER BY engine_events_id ASC LIMIT {4};");
        sql_build(db, expanded_sql_tmpl, expanded_sql, sizeof(expanded_sql));
    } else {
        /* SQL_VERBATIM: Q7 (SQLite) */
        char expanded_sql_tmpl[1024];
        snprintf(expanded_sql_tmpl,
                 sizeof(expanded_sql_tmpl),
                 "SELECT engine_events_id as id, ts, type, actor_player_id, sector_id, payload "
                 "FROM engine_events "
                 "WHERE engine_events_id > {1} "
                 "  AND ({2} = FALSE OR type IN (SELECT trim(value) FROM json_each({3}))) "
                 "ORDER BY engine_events_id ASC LIMIT {4};");
        sql_build(db, expanded_sql_tmpl, expanded_sql, sizeof(expanded_sql));
    }

    db_bind_t params[] = {
        db_bind_i64(last_id),
        db_bind_bool(priority_only),
        db_bind_json(prio_json),
        db_bind_i64(limit)
    };

    db_error_t err;
    if (db_query(db, expanded_sql, params, 4, out_res, &err)) {
        return 0;
    }
    return err.code;
}
