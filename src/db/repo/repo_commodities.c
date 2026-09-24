/**
 * repo_commodities.c - Data-driven commodity queries (Phase 2)
 *
 * Implements DB-driven access to commodity metadata and eligibility rules.
 * Replaces hardcoded commodity lists (e.g., CASE statements).
 *
 * Strategy:
 * - Query port_trade table to determine which commodities a port trades
 * - Query planet_goods table to determine which commodities a planet accepts
 * - Check commodities.illegal flag to filter by alignment
 */

#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_commodities.h"
#include "db/sql_driver.h"
#include "errors.h"
#include <stdlib.h>
#include <string.h>

/**
 * Helper: Check alignment against legality.
 * Returns true if player is allowed to trade this commodity.
 *
 * Rules:
 *   - Legal commodities: allowed for any alignment
 *   - Illegal commodities: allowed for evil alignment (< 0), blocked for good alignment (>= 0)
 */
static inline bool is_commodity_allowed_for_alignment(bool is_illegal, int alignment) {
    if (!is_illegal) return true;  /* Legal: always allowed */
    return alignment < 0;           /* Illegal: only for evil alignment */
}

int repo_commodities_list_tradeable_at_port(
    db_t *db,
    int port_id,
    int alignment,
    char ***out_codes,
    int *out_count,
    bool **out_buy_flags,
    bool **out_sell_flags
) {
    if (!db || !out_codes || !out_count || !out_buy_flags || !out_sell_flags) {
        return ERR_INVALID_ARG;
    }

    db_res_t *res = NULL;
    db_error_t err;
    db_error_clear(&err);

    /* Query: Get all commodities this port trades (buy or sell) + legality flag */
    const char *q_template =
        "SELECT DISTINCT pt.commodity, c.illegal, "
        "       MAX(CASE WHEN pt.mode = 'buy' THEN 1 ELSE 0 END) AS buys, "
        "       MAX(CASE WHEN pt.mode = 'sell' THEN 1 ELSE 0 END) AS sells "
        "FROM port_trade pt "
        "JOIN commodities c ON c.code = pt.commodity "
        "WHERE pt.port_id = {1} "
        "GROUP BY pt.commodity, c.illegal "
        "ORDER BY pt.commodity;";

    char sql[1024];
    sql_build(db, q_template, sql, sizeof(sql));

    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i64(port_id) }, 1, &res, &err)) {
        return ERR_DB_QUERY_FAILED;
    }

    /* Allocate initial capacity */
    int capacity = 10;
    int idx = 0;
    *out_codes = (char **)malloc(capacity * sizeof(char *));
    *out_buy_flags = (bool *)malloc(capacity * sizeof(bool));
    *out_sell_flags = (bool *)malloc(capacity * sizeof(bool));

    if (!*out_codes || !*out_buy_flags || !*out_sell_flags) {
        free(*out_codes);
        free(*out_buy_flags);
        free(*out_sell_flags);
        if (res) db_res_finalize(res);
        return ERR_NOMEM;
    }

    /* Iterate and collect matching commodities */
    while (res && db_res_step(res, &err)) {
        const char *commodity_code = db_res_col_text(res, 0, &err);
        bool is_illegal = (db_res_col_int(res, 1, &err) != 0);
        bool buys = (db_res_col_int(res, 2, &err) != 0);
        bool sells = (db_res_col_int(res, 3, &err) != 0);

        /* Check alignment filter */
        if (!is_commodity_allowed_for_alignment(is_illegal, alignment)) {
            continue;
        }

        /* Resize if needed */
        if (idx >= capacity) {
            capacity *= 2;
            char **new_codes = (char **)realloc(*out_codes, capacity * sizeof(char *));
            bool *new_buys = (bool *)realloc(*out_buy_flags, capacity * sizeof(bool));
            bool *new_sells = (bool *)realloc(*out_sell_flags, capacity * sizeof(bool));

            if (!new_codes || !new_buys || !new_sells) {
                db_res_finalize(res);
                for (int i = 0; i < idx; i++) {
                    free((*out_codes)[i]);
                }
                free(*out_codes);
                free(*out_buy_flags);
                free(*out_sell_flags);
                return ERR_NOMEM;
            }

            *out_codes = new_codes;
            *out_buy_flags = new_buys;
            *out_sell_flags = new_sells;
        }

        (*out_codes)[idx] = (char *)malloc(4);  /* 3 chars + null terminator */
        if (!(*out_codes)[idx]) {
            db_res_finalize(res);
            for (int i = 0; i < idx; i++) {
                free((*out_codes)[i]);
            }
            free(*out_codes);
            free(*out_buy_flags);
            free(*out_sell_flags);
            return ERR_NOMEM;
        }

        strncpy((*out_codes)[idx], commodity_code, 3);
        (*out_codes)[idx][3] = '\0';
        (*out_buy_flags)[idx] = buys;
        (*out_sell_flags)[idx] = sells;
        idx++;
    }

    if (res) db_res_finalize(res);
    *out_count = idx;
    return 0;
}

int repo_commodities_list_planet_transferable(
    db_t *db,
    int planet_id,
    int alignment,
    char ***out_codes,
    int *out_count
) {
    if (!db || !out_codes || !out_count) {
        return ERR_INVALID_ARG;
    }

    db_res_t *res = NULL;
    db_error_t err;
    db_error_clear(&err);

    /* Query: Get all commodities this planet accepts (from planet_goods) + legality flag */
    const char *q_template =
        "SELECT pg.commodity, c.illegal "
        "FROM planet_goods pg "
        "JOIN commodities c ON c.code = pg.commodity "
        "WHERE pg.planet_id = {1} "
        "ORDER BY pg.commodity;";

    char sql[1024];
    sql_build(db, q_template, sql, sizeof(sql));

    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i64(planet_id) }, 1, &res, &err)) {
        return ERR_DB_QUERY_FAILED;
    }

    /* Allocate initial capacity */
    int capacity = 10;
    int idx = 0;
    *out_codes = (char **)malloc(capacity * sizeof(char *));
    if (!*out_codes) {
        if (res) db_res_finalize(res);
        return ERR_NOMEM;
    }

    /* Iterate and collect matching commodities */
    while (res && db_res_step(res, &err)) {
        const char *commodity_code = db_res_col_text(res, 0, &err);
        bool is_illegal = (db_res_col_int(res, 1, &err) != 0);

        /* Check alignment filter */
        if (!is_commodity_allowed_for_alignment(is_illegal, alignment)) {
            continue;
        }

        /* Resize if needed */
        if (idx >= capacity) {
            capacity *= 2;
            char **new_codes = (char **)realloc(*out_codes, capacity * sizeof(char *));
            if (!new_codes) {
                db_res_finalize(res);
                for (int i = 0; i < idx; i++) {
                    free((*out_codes)[i]);
                }
                free(*out_codes);
                return ERR_NOMEM;
            }
            *out_codes = new_codes;
        }

        (*out_codes)[idx] = (char *)malloc(4);
        if (!(*out_codes)[idx]) {
            db_res_finalize(res);
            for (int i = 0; i < idx; i++) {
                free((*out_codes)[i]);
            }
            free(*out_codes);
            return ERR_NOMEM;
        }

        strncpy((*out_codes)[idx], commodity_code, 3);
        (*out_codes)[idx][3] = '\0';
        idx++;
    }

    if (res) db_res_finalize(res);
    *out_count = idx;
    return 0;
}

int repo_commodities_is_legal(
    db_t *db,
    const char *commodity_code,
    bool *out_is_legal
) {
    if (!db || !commodity_code || !out_is_legal) {
        return ERR_INVALID_ARG;
    }

    db_res_t *res = NULL;
    db_error_t err;
    db_error_clear(&err);

    const char *q_template = "SELECT illegal FROM commodities WHERE code = {1};";

    char sql[256];
    sql_build(db, q_template, sql, sizeof(sql));

    if (!db_query(db, sql, (db_bind_t[]){ db_bind_text(commodity_code) }, 1, &res, &err)) {
        return ERR_DB_QUERY_FAILED;
    }

    if (!res || !db_res_step(res, &err)) {
        if (res) db_res_finalize(res);
        return ERR_DB_NOT_FOUND;
    }

    *out_is_legal = (db_res_col_int(res, 0, &err) == 0);
    db_res_finalize(res);
    return 0;
}

/**
 * Get the per-ship maximum hold limit for a commodity.
 *
 * Parameters:
 *   db: Database handle
 *   commodity_code: 3-char commodity code
 *   out_max: Output maximum holds (-1 if unlimited/NULL in DB)
 *
 * Returns:
 *   0 on success
 *   ERR_DB_NOT_FOUND if commodity does not exist
 *   ERR_* on error
 */
int repo_commodities_get_max_holds_per_ship(
    db_t *db,
    const char *commodity_code,
    int *out_max
) {
    if (!db || !commodity_code || !out_max) {
        return ERR_INVALID_ARG;
    }

    db_res_t *res = NULL;
    db_error_t err;
    db_error_clear(&err);

    const char *q_template = "SELECT max_holds_per_ship FROM commodities WHERE code = {1};";

    char sql[256];
    sql_build(db, q_template, sql, sizeof(sql));

    if (!db_query(db, sql, (db_bind_t[]){ db_bind_text(commodity_code) }, 1, &res, &err)) {
        return ERR_DB_QUERY_FAILED;
    }

    if (!res || !db_res_step(res, &err)) {
        if (res) db_res_finalize(res);
        return ERR_DB_NOT_FOUND;
    }

    /* Check if result is NULL; if so, return -1 (unlimited) */
    if (db_res_col_is_null(res, 0)) {
        *out_max = -1;
    } else {
        *out_max = (int)db_res_col_i32(res, 0, &err);
    }

    db_res_finalize(res);
    return 0;
}
