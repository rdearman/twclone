#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_clusters.h"
#include "db/sql_driver.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int repo_clusters_get_sector_count(db_t *db, int *count_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q1 */
    if (db_query (db, "SELECT COUNT(*) FROM sectors", NULL, 0, &res, &err)) {
        if (db_res_step (res, &err)) {
            *count_out = db_res_col_i32 (res, 0, &err);
        }
        db_res_finalize (res);
        return 0;
    }
    return err.code;
}

int repo_clusters_get_cluster_for_sector(db_t *db, int sector_id, int *cluster_id_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q2 */
    const char *q2 = "SELECT cluster_id FROM cluster_sectors WHERE sector_id = {1}";
    char sql[256]; sql_build(db, q2, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) {
        if (db_res_step (res, &err)) {
            *cluster_id_out = db_res_col_i32 (res, 0, &err);
        } else {
            *cluster_id_out = 0;
        }
        db_res_finalize (res);
        return 0;
    }
    return err.code;
}

int repo_clusters_create(db_t *db, const char *name, const char *role, const char *kind, int center_sector, int alignment, int law_severity, int *cluster_id_out) {
    db_error_t err;
    int64_t new_id = 0;
    /* SQL_VERBATIM: Q3 */
    const char *q3 = "INSERT INTO clusters (name, role, kind, center_sector, alignment, law_severity) VALUES ({1}, {2}, {3}, {4}, {5}, {6})";
    char sql[512]; sql_build(db, q3, sql, sizeof(sql));
    if (db_exec_insert_id (db, sql, (db_bind_t[]){ db_bind_text(name), db_bind_text(role), db_bind_text(kind), db_bind_i32(center_sector), db_bind_i32(alignment), db_bind_i32(law_severity) }, 6, "clusters_id", &new_id, &err)) {
        *cluster_id_out = (int)new_id;
        return 0;
    }
    return err.code;
}

int repo_clusters_add_sector(db_t *db, int cluster_id, int sector_id) {
    db_error_t err;
    const char *conflict_clause = sql_insert_ignore_clause(db);
    if (!conflict_clause) return -1;
    char sql_template[256];
    /* SQL_VERBATIM: Q4 */
    snprintf(sql_template, sizeof(sql_template),
        "INSERT INTO cluster_sectors (cluster_id, sector_id) VALUES ({1}, {2}) %s",
        conflict_clause);
    char sql[256]; sql_build(db, sql_template, sql, sizeof(sql));
    if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i32(cluster_id), db_bind_i32(sector_id) }, 2, &err)) return err.code;
    return 0;
}

db_res_t* repo_clusters_get_warps(db_t *db, int from_sector, db_error_t *err) {
    /* SQL_VERBATIM: Q5 */
    const char *q5 = "SELECT to_sector FROM sector_warps WHERE from_sector = {1}";
    char sql[512]; sql_build(db, q5, sql, sizeof(sql));
    db_res_t *res = NULL;
    db_query(db, sql, (db_bind_t[]){ db_bind_i32(from_sector) }, 1, &res, err);
    return res;
}

int repo_clusters_check_sector_in_any_cluster(db_t *db, int sector_id, int *exists_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q6 */
    const char *q6 = "SELECT 1 FROM cluster_sectors WHERE sector_id = {1}";
    char sql[256]; sql_build(db, q6, sql, sizeof(sql));
    *exists_out = 0;
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) {
        if (db_res_step (res, &err)) {
            *exists_out = 1;
        }
        db_res_finalize (res);
        return 0;
    }
    return err.code;
}

int repo_clusters_is_initialized(db_t *db, int *inited_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q7 */
    if (db_query (db, "SELECT 1 FROM clusters LIMIT 1", NULL, 0, &res, &err)) {
        if (db_res_step (res, &err)) {
            *inited_out = 1;
        } else {
            *inited_out = 0;
        }
        db_res_finalize (res);
        return 0;
    }
    return err.code;
}

int repo_clusters_get_planet_sector(db_t *db, int num, int *sector_out) {
    db_res_t *res = NULL;
    db_error_t err;
    char sql[256];
    if (num == 2) {
        /* SQL_VERBATIM: Q8 */
        snprintf(sql, sizeof(sql), "SELECT sector_id FROM planets WHERE num=2");
    } else {
        /* SQL_VERBATIM: Q9 */
        snprintf(sql, sizeof(sql), "SELECT sector_id FROM planets WHERE num=3");
    }
    if (db_query (db, sql, NULL, 0, &res, &err)) {
        if (db_res_step (res, &err)) {
            *sector_out = db_res_col_i32 (res, 0, &err);
        } else {
            *sector_out = 0;
        }
        db_res_finalize (res);
        return 0;
    }
    return err.code;
}

int repo_clusters_get_clustered_count(db_t *db, int *count_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q10 */
    if (db_query (db, "SELECT COUNT(*) FROM cluster_sectors", NULL, 0, &res, &err)) {
        if (db_res_step (res, &err)) {
            *count_out = db_res_col_i32 (res, 0, &err);
        }
        db_res_finalize (res);
        return 0;
    }
    return err.code;
}

int repo_clusters_pick_random_unclustered_sector(db_t *db, int *sector_id_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q11 */
    const char *q11 = "SELECT sector_id FROM sectors WHERE sector_id > 10 AND sector_id NOT IN (SELECT sector_id FROM cluster_sectors) ORDER BY RANDOM() LIMIT 1";
    if (db_query (db, q11, NULL, 0, &res, &err)) {
        if (db_res_step (res, &err)) {
            *sector_id_out = db_res_col_i32 (res, 0, &err);
        } else {
            *sector_id_out = 0;
        }
        db_res_finalize (res);
        return 0;
    }
    return err.code;
}

db_res_t* repo_clusters_get_all(db_t *db, db_error_t *err) {
    db_res_t *res = NULL;
    /* SQL_VERBATIM: Q12 */
    db_query(db, "SELECT clusters_id, name FROM clusters", NULL, 0, &res, err);
    return res;
}

int repo_clusters_get_avg_price(db_t *db, int cluster_id, const char *commodity, double *avg_price_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q13 */
    const char *q13 = "SELECT AVG(price) FROM port_trade pt JOIN ports p ON p.port_id = pt.port_id JOIN cluster_sectors cs ON cs.sector_id = p.sector_id WHERE cs.cluster_id = {1} AND pt.commodity = {2}";
    char sql[1024]; sql_build(db, q13, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(cluster_id), db_bind_text(commodity) }, 2, &res, &err)) {
        if (db_res_step (res, &err)) {
            *avg_price_out = db_res_col_double (res, 0, &err);
        }
        db_res_finalize (res);
        return 0;
    }
    return err.code;
}

int repo_clusters_update_commodity_index(db_t *db, int cluster_id, const char *commodity, int mid_price) {
    db_error_t err;
    const char *conflict_fmt = sql_conflict_target_fmt(db);
    if (!conflict_fmt) return -1;
    char conflict_clause[128];
    snprintf(conflict_clause, sizeof(conflict_clause), conflict_fmt, "cluster_id, commodity_code");
    char sql_template[512];
    /* SQL_VERBATIM: Q14 */
    snprintf(sql_template, sizeof(sql_template),
        "INSERT INTO cluster_commodity_index (cluster_id, commodity_code, mid_price, last_updated) VALUES ({1}, {2}, {3}, CURRENT_TIMESTAMP) "
        "%s UPDATE SET mid_price=excluded.mid_price, last_updated=CURRENT_TIMESTAMP",
        conflict_clause);
    char sql[512]; sql_build(db, sql_template, sql, sizeof(sql));
    if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i32(cluster_id), db_bind_text(commodity), db_bind_i32(mid_price) }, 3, &err)) return err.code;
    return 0;
}

int repo_clusters_drift_port_prices(db_t *db, int mid_price, const char *commodity, int cluster_id) {
    db_error_t err;
    /* SQL_VERBATIM: Q15 */
    const char *q15 = "UPDATE port_trade SET price = CAST(price + 0.1 * ({1} - price) AS INTEGER) WHERE commodity = {2} AND port_id IN ( SELECT p.port_id FROM ports p JOIN cluster_sectors cs ON cs.sector_id = p.sector_id WHERE cs.cluster_id = {3} )";
    char sql[1024]; sql_build(db, q15, sql, sizeof(sql));
    if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i32(mid_price), db_bind_text(commodity), db_bind_i32(cluster_id) }, 3, &err)) return err.code;
    return 0;
}

int repo_clusters_get_player_banned(db_t *db, int cluster_id, int player_id, int *banned_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q16 */
    const char *q16 = "SELECT banned FROM cluster_player_status WHERE cluster_id = {1} AND player_id = {2}";
    char sql[512]; sql_build(db, q16, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(cluster_id), db_bind_i32(player_id) }, 2, &res, &err)) {
        if (db_res_step (res, &err)) {
            *banned_out = db_res_col_i32 (res, 0, &err);
        } else {
            *banned_out = 0;
        }
        db_res_finalize (res);
        return 0;
    }
    return err.code;
}

int repo_clusters_get_player_suspicion_wanted(db_t *db, int cluster_id, int player_id, int *suspicion_out, int *wanted_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q17 */
    const char *q17 = "SELECT suspicion, wanted_level FROM cluster_player_status WHERE cluster_id = {1} AND player_id = {2}";
    char sql[512]; sql_build(db, q17, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(cluster_id), db_bind_i32(player_id) }, 2, &res, &err)) {
        if (db_res_step (res, &err)) {
            *suspicion_out = db_res_col_i32 (res, 0, &err);
            *wanted_out = db_res_col_i32 (res, 1, &err);
        } else {
            *suspicion_out = 0;
            *wanted_out = 0;
        }
        db_res_finalize (res);
        return 0;
    }
    return err.code;
}

int repo_clusters_upsert_player_status(db_t *db, int cluster_id, int player_id, int susp_inc, int busted) {
    db_error_t err;
    const char *conflict_fmt = sql_conflict_target_fmt(db);
    if (!conflict_fmt) return -1;
    char conflict_clause[128];
    snprintf(conflict_clause, sizeof(conflict_clause), conflict_fmt, "cluster_id, player_id");
    char sql_template[1024];
    /* SQL_VERBATIM: Q18 */
    snprintf(sql_template, sizeof(sql_template),
        "INSERT INTO cluster_player_status (cluster_id, player_id, suspicion, bust_count, last_bust_at) "
        "VALUES ({1}, {2}, {3}, {4}, CASE WHEN {5}=1 THEN CURRENT_TIMESTAMP ELSE NULL END) "
        "%s UPDATE SET "
        "suspicion = suspicion + {6}, "
        "bust_count = bust_count + {7}, "
        "last_bust_at = CASE WHEN {8}=1 THEN CURRENT_TIMESTAMP ELSE last_bust_at END;",
        conflict_clause);
    char sql[1024]; sql_build(db, sql_template, sql, sizeof(sql));
    if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i32(cluster_id), db_bind_i32(player_id), db_bind_i32(susp_inc), db_bind_i32(busted ? 1 : 0), db_bind_i32(busted), db_bind_i32(susp_inc), db_bind_i32(busted ? 1 : 0), db_bind_i32(busted) }, 8, &err)) return err.code;
    return 0;
}

db_res_t* repo_clusters_get_all_ports(db_t *db, db_error_t *err) {
    db_res_t *res = NULL;
    /* SQL_VERBATIM: Q19 */
    db_query(db, "SELECT port_id, sector_id FROM ports", NULL, 0, &res, err);
    return res;
}

int repo_clusters_get_alignment(db_t *db, int sector_id, int *alignment_out) {
    db_res_t *res = NULL;
    db_error_t err;
    /* SQL_VERBATIM: Q20 */
    const char *q20 = "SELECT c.alignment FROM clusters c JOIN cluster_sectors cs ON cs.cluster_id = c.clusters_id WHERE cs.sector_id = {1} LIMIT 1";
    char sql[512]; sql_build(db, q20, sql, sizeof(sql));
    if (db_query (db, sql, (db_bind_t[]){ db_bind_i32(sector_id) }, 1, &res, &err)) {
        if (db_res_step (res, &err)) {
            *alignment_out = db_res_col_i32 (res, 0, &err);
        } else {
            *alignment_out = 0;
        }
        db_res_finalize (res);
        return 0;
    }
    return err.code;
}

int repo_clusters_upsert_port_stock(db_t *db, int port_id, const char *commodity, int quantity, int64_t now_s) {
    db_error_t err;
    const char *conflict_fmt = sql_conflict_target_fmt(db);
    if (!conflict_fmt) return -1;
    char conflict_clause[128];
    snprintf(conflict_clause, sizeof(conflict_clause), conflict_fmt, "entity_type, entity_id, commodity_code");
    char sql_template[512];
    /* SQL_VERBATIM: Q21 */
    snprintf(sql_template, sizeof(sql_template),
        "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "
        "VALUES ('port', {1}, {2}, {3}, 0, {4}) "
        "%s UPDATE SET quantity = excluded.quantity, last_updated_ts = excluded.last_updated_ts;",
        conflict_clause);
    char sql[512]; sql_build(db, sql_template, sql, sizeof(sql));
    if (!db_exec (db, sql, (db_bind_t[]){ db_bind_i32(port_id), db_bind_text(commodity), db_bind_i32(quantity), db_bind_i64(now_s) }, 4, &err)) return err.code;
    return 0;
}
