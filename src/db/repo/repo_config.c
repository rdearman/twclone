#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_config.h"
#include "db/sql_driver.h"
#include <stdio.h>
#include <string.h>

int repo_config_get_all(db_t *db, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q1 */
    const char *sql = "SELECT key, value, type FROM config;";
    db_error_t err;
    if (db_query(db, sql, NULL, 0, out_res, &err)) {
        return 0;
    }
    return err.code;
}

int repo_config_get_value(db_t *db, const char *key, char *out_val, size_t out_sz)
{
    /* SQL_VERBATIM: Q3 */
    const char *sql = "SELECT value FROM config WHERE key = {1} LIMIT 1;";
    db_res_t *res = NULL;
    db_error_t err;
    if (db_query(db, sql, (db_bind_t[]){ db_bind_text(key) }, 1, &res, &err)) {
        if (db_res_step(res, &err)) {
            const char *val = db_res_col_text(res, 0, &err);
            if (val) {
                strncpy(out_val, val, out_sz - 1);
                out_val[out_sz-1] = '\0';
                db_res_finalize(res);
                return 0;
            }
        }
        db_res_finalize(res);
    }
    return -1;
}

int repo_config_set_value(db_t *db, const char *key, const char *val)
{
    db_error_t err;
    /* SQL_VERBATIM: Q_CFG_SET */
    const char *sql = "UPDATE config SET value={2} WHERE key={1};";
    
    char sql_buf[512];
    sql_build(db, sql, sql_buf, sizeof(sql_buf));
    
    if (!db_exec(db, sql_buf, (db_bind_t[]){ db_bind_text(key), db_bind_text(val) }, 2, &err)) {
        return err.code;
    }
    return 0;
}

int repo_config_get_default_s2s_key(db_t *db, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q2 */
    const char *sql = "SELECT key_id,key_b64 FROM s2s_keys WHERE active = TRUE AND is_default_tx = TRUE LIMIT 1";
    db_error_t err;
    if (db_query(db, sql, NULL, 0, out_res, &err)) {
        return 0;
    }
    return err.code;
}