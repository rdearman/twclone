#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_warp.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int repo_warp_get_high_degree_sector(db_t *db, int *sector_id_out) {
    db_res_t *st = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q1 */
    const char *q = "SELECT from_sector, COUNT(to_sector) AS out_degree "
                  "FROM sector_warps "
                  "GROUP BY from_sector "
                  "HAVING COUNT(to_sector) > 3 " "ORDER BY RANDOM() LIMIT 1;";
    if (db_query (db, q, NULL, 0, &st, &err) && db_res_step(st, &err)) {
        *sector_id_out = db_res_col_i32(st, 0, &err);
        db_res_finalize(st);
        return 0;
    }
    if (st) db_res_finalize(st);
    return err.code ? err.code : -1;
}

int repo_warp_get_tunnel_end(db_t *db, int start_sector, int *end_sector_out) {
    db_res_t *st = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q2 */
    const char *q = "SELECT to_sector FROM sector_warps WHERE from_sector = {1};";
    char sql_converted[256]; sql_build(db, q, sql_converted, sizeof(sql_converted));
    if (db_query (db, sql_converted, (db_bind_t[]){ db_bind_i64(start_sector) }, 1, &st, &err) && db_res_step(st, &err)) {
        *end_sector_out = db_res_col_i32(st, 0, &err);
        db_res_finalize(st);
        return 0;
    }
    if (st) db_res_finalize(st);
    return err.code ? err.code : -1;
}

int repo_warp_get_tunnels(db_t *db, int *start_sector1_out, int *start_sector2_out) {
    db_res_t *st = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q3 */
    const char *q_find_tunnels =
    "SELECT from_sector FROM (SELECT from_sector, COUNT(to_sector) AS out_degree FROM sector_warps GROUP BY from_sector) AS t WHERE out_degree = 1 ORDER BY RANDOM() LIMIT 2;";
    if (db_query (db, q_find_tunnels, NULL, 0, &st, &err)) {
        if (db_res_step (st, &err)) {
            *start_sector1_out = db_res_col_i32 (st, 0, &err);
        }
        if (db_res_step (st, &err)) {
            *start_sector2_out = db_res_col_i32 (st, 0, &err);
        }
        db_res_finalize(st);
        return 0;
    }
    return err.code ? err.code : -1;
}

int repo_warp_delete_warp(db_t *db, int from_sector, int to_sector) {
    db_error_t err;
    /* SQL_VERBATIM: Q4 */
    const char *sql_delete_warp = "DELETE FROM sector_warps WHERE from_sector = {1} AND to_sector = {2};";
    char sql_converted[256]; sql_build(db, sql_delete_warp, sql_converted, sizeof(sql_converted));
    if (!db_exec (db, sql_converted, (db_bind_t[]){ db_bind_i64(from_sector), db_bind_i64(to_sector) }, 2, &err)) return err.code;
    return 0;
}

int repo_warp_insert_warp(db_t *db, int from_sector, int to_sector) {
    db_error_t err;
    /* SQL_VERBATIM: Q5 */
    const char *sql_insert_warp = "INSERT INTO sector_warps (from_sector, to_sector) VALUES ({1}, {2});";
    char sql_converted[256]; sql_build(db, sql_insert_warp, sql_converted, sizeof(sql_converted));
    if (!db_exec (db, sql_converted, (db_bind_t[]){ db_bind_i64(from_sector), db_bind_i64(to_sector) }, 2, &err)) return err.code;
    return 0;
}

int repo_warp_get_bidirectional_warp(db_t *db, int *from_out, int *to_out) {
    db_res_t *st = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q6 */
    const char *q_find_bidirectional = "SELECT a, b FROM v_bidirectional_warps ORDER BY RANDOM() LIMIT 1;";
    if (db_query (db, q_find_bidirectional, NULL, 0, &st, &err) && db_res_step(st, &err)) {
        *from_out = db_res_col_i32(st, 0, &err);
        *to_out = db_res_col_i32(st, 1, &err);
        db_res_finalize(st);
        return 0;
    }
    if (st) db_res_finalize(st);
    return err.code ? err.code : -1;
}

int repo_warp_get_low_degree_sector(db_t *db, int *sector_id_out) {
    db_res_t *st = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q8 */
    const char *q_find_low_degree =
    "SELECT from_sector FROM (SELECT from_sector, COUNT(to_sector) AS out_degree "
    "FROM sector_warps GROUP BY from_sector) AS t "
    "WHERE out_degree = 1 ORDER BY RANDOM() LIMIT 1;";
    if (db_query (db, q_find_low_degree, NULL, 0, &st, &err) && db_res_step(st, &err)) {
        *sector_id_out = db_res_col_i32(st, 0, &err);
        db_res_finalize(st);
        return 0;
    }
    if (st) db_res_finalize(st);
    return err.code ? err.code : -1;
}

int repo_warp_delete_warps_from_sector(db_t *db, int from_sector) {
    db_error_t err;
    /* SQL_VERBATIM: Q9 */
    const char *sql_delete = "DELETE FROM sector_warps WHERE from_sector = {1};";
    char sql_converted[256]; sql_build(db, sql_delete, sql_converted, sizeof(sql_converted));
    if (!db_exec (db, sql_converted, (db_bind_t[]){ db_bind_i64(from_sector) }, 1, &err)) return err.code;
    return 0;
}
