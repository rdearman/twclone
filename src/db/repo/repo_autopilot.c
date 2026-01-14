#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_autopilot.h"
#include "db/sql_driver.h"
#include <stdio.h>
#include <string.h>

int repo_autopilot_get_max_sector_id(db_t *db, int32_t *out_max_id)
{
    /* SQL_VERBATIM: Q1 */
    const char *sql = "SELECT MAX(sector_id) FROM sectors;";
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, NULL, 0, &res, &err)) {
        if (db_res_step(res, &err)) {
            *out_max_id = (int32_t)db_res_col_i64(res, 0, &err);
            db_res_finalize(res);
            return 0;
        }
        db_res_finalize(res);
        return 1;
    }
    return err.code;
}

int repo_autopilot_get_warp_count(db_t *db, int32_t *out_count)
{
    /* SQL_VERBATIM: Q2 */
    const char *sql = "SELECT COUNT(*) FROM sector_warps;";
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

int repo_autopilot_get_all_warps(db_t *db, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q3 */
    const char *sql = "SELECT from_sector, to_sector FROM sector_warps;";
    db_error_t err;
    if (db_query(db, sql, NULL, 0, out_res, &err)) {
        return 0;
    }
    return err.code;
}
