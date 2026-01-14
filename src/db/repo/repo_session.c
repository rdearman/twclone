#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_session.h"
#include "db/sql_driver.h"
#include <stdio.h>

int repo_session_lookup(db_t *db, const char *token, int32_t *out_player_id, int64_t *out_expires)
{
    /* SQL_VERBATIM: Q1 */
    const char *sql = "SELECT player_id, expires FROM sessions WHERE token = {1} LIMIT 1;";
    char sql_converted[256];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_text(token) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            *out_player_id = (int32_t)db_res_col_i64(res, 0, &err);
            *out_expires = db_res_col_i64(res, 1, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1;
    }
    return err.code;
}

int repo_session_get_unseen_notices(db_t *db, int32_t max_rows, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q2 */
    static const char *sql =
        "SELECT system_notice_id, created_at, title, body, severity, expires_at "
        "FROM system_notice "
        "WHERE system_notice_id NOT IN (" 
        "    SELECT notice_id FROM notice_seen WHERE player_id = 0"
        ") "
        "ORDER BY created_at ASC, system_notice_id ASC "
        "LIMIT {1};";

    char sql_converted[512];
    sql_build(db, sql, sql_converted, sizeof(sql_converted));

    db_error_t err;
    if (db_query(db, sql_converted, (db_bind_t[]){ db_bind_i32(max_rows) }, 1, out_res, &err)) {
        return 0;
    }
    return err.code;
}
