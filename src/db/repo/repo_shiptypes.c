#include "repo_shiptypes.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*
 * Phase 4: Data-Driven Ship Types Repository
 * 
 * Implements DB-driven ship type eligibility checking.
 * Replaces hardcoded "Corporate Flagship" and other type-specific logic.
 */

int repo_shiptypes_get_restrictions(db_t *db, int shiptypes_id, db_res_t **out_res)
{
    if (!db || shiptypes_id <= 0 || !out_res) {
        return -1;
    }

    db_error_t err;
    const char *sql_template = "SELECT restriction_id, check_type, check_value, description FROM shiptype_restrictions WHERE shiptypes_id = {1} AND enabled = TRUE ORDER BY restriction_id;";
    char sql[512];
    sql_build(db, sql_template, sql, sizeof(sql));

    if (!db_query(db, sql, (db_bind_t[]){ db_bind_i64(shiptypes_id) }, 1, out_res, &err)) {
        return -1;
    }
    return 0;
}

int repo_shiptypes_validate_eligibility(db_t *db, int shiptypes_id, const player_info_t *player_info)
{
    if (!db || shiptypes_id <= 0 || !player_info) {
        return -1;
    }

    db_error_t err;
    db_res_t *res = NULL;

    if (repo_shiptypes_get_restrictions(db, shiptypes_id, &res) != 0) {
        return -1;
    }

    int eligible = 1;
    while (db_res_step(res, &err)) {
        const char *check_type = db_res_col_text(res, 1, &err);
        const char *check_value = db_res_col_text(res, 2, &err);

        if (!check_type) {
            continue;
        }

        /* CEO restriction */
        if (strcmp(check_type, "CEO") == 0) {
            if (!player_info->is_ceo) {
                eligible = 0;
                break;
            }
        }
        /* Alignment minimum */
        else if (strcmp(check_type, "ALIGNMENT_MIN") == 0) {
            if (check_value) {
                int required_alignment = atoi(check_value);
                if (player_info->alignment < required_alignment) {
                    eligible = 0;
                    break;
                }
            }
        }
        /* Alignment maximum */
        else if (strcmp(check_type, "ALIGNMENT_MAX") == 0) {
            if (check_value) {
                int required_alignment = atoi(check_value);
                if (player_info->alignment > required_alignment) {
                    eligible = 0;
                    break;
                }
            }
        }
        /* Score minimum */
        else if (strcmp(check_type, "SCORE_MIN") == 0) {
            if (check_value) {
                long long required_score = atoll(check_value);
                if (player_info->score < required_score) {
                    eligible = 0;
                    break;
                }
            }
        }
        /* CUSTOM type - reserved for future use */
        else if (strcmp(check_type, "CUSTOM") == 0) {
            /* Reserved: can be extended by sysop hooks */
            continue;
        }
    }

    db_res_finalize(res);
    return eligible ? 0 : 1;  /* Return 0 if eligible, 1 if restricted */
}

char *repo_shiptypes_get_restriction_desc(db_t *db, int shiptypes_id, const player_info_t *player_info)
{
    if (!db || shiptypes_id <= 0 || !player_info) {
        return NULL;
    }

    db_error_t err;
    db_res_t *res = NULL;

    if (repo_shiptypes_get_restrictions(db, shiptypes_id, &res) != 0) {
        return NULL;
    }

    char *reason = NULL;
    while (db_res_step(res, &err)) {
        const char *check_type = db_res_col_text(res, 1, &err);
        const char *check_value = db_res_col_text(res, 2, &err);
        const char *description = db_res_col_text(res, 3, &err);

        if (!check_type) {
            continue;
        }

        int restricted = 0;

        /* Check each restriction type */
        if (strcmp(check_type, "CEO") == 0) {
            if (!player_info->is_ceo) {
                restricted = 1;
            }
        }
        else if (strcmp(check_type, "ALIGNMENT_MIN") == 0) {
            if (check_value) {
                int required_alignment = atoi(check_value);
                if (player_info->alignment < required_alignment) {
                    restricted = 1;
                }
            }
        }
        else if (strcmp(check_type, "ALIGNMENT_MAX") == 0) {
            if (check_value) {
                int required_alignment = atoi(check_value);
                if (player_info->alignment > required_alignment) {
                    restricted = 1;
                }
            }
        }
        else if (strcmp(check_type, "SCORE_MIN") == 0) {
            if (check_value) {
                long long required_score = atoll(check_value);
                if (player_info->score < required_score) {
                    restricted = 1;
                }
            }
        }

        if (restricted && description) {
            reason = malloc(strlen(description) + 1);
            if (reason) {
                strcpy(reason, description);
            }
            break;
        }
    }

    db_res_finalize(res);
    return reason;  /* Caller must free */
}
