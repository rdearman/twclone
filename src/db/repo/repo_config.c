#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_config.h"
#include <stdio.h>

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

int repo_config_get_default_s2s_key(db_t *db, db_res_t **out_res)
{
    /* SQL_VERBATIM: Q2 */
    const char *sql = "SELECT key_id,key_b64 FROM s2s_keys WHERE active=1 AND is_default_tx=1 LIMIT 1";
    db_error_t err;
    if (db_query(db, sql, NULL, 0, out_res, &err)) {
        return 0;
    }
    return err.code;
}
