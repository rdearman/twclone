#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_main.h"
#include <stdio.h>

int repo_main_get_sector_count(db_t *db, int32_t *out_count)
{
    /* SQL_VERBATIM: Q1 */
    const char *sql = "SELECT COUNT(*) FROM sectors";
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, NULL, 0, &res, &err)) {
        if (db_res_step(res, &err)) {
            *out_count = (int32_t)db_res_col_i64(res, 0, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1;
    }
    return err.code;
}

int repo_main_get_warp_count(db_t *db, int32_t *out_count)
{
    /* SQL_VERBATIM: Q2 */
    const char *sql = "SELECT COUNT(*) FROM sector_warps";
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, NULL, 0, &res, &err)) {
        if (db_res_step(res, &err)) {
            *out_count = (int32_t)db_res_col_i64(res, 0, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1;
    }
    return err.code;
}

int repo_main_get_port_count(db_t *db, int32_t *out_count)
{
    /* SQL_VERBATIM: Q3 */
    const char *sql = "SELECT COUNT(*) FROM ports";
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, NULL, 0, &res, &err)) {
        if (db_res_step(res, &err)) {
            *out_count = (int32_t)db_res_col_i64(res, 0, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1;
    }
    return err.code;
}
