#ifndef REPO_SHIPTYPES_H
#define REPO_SHIPTYPES_H

#include "db/db_api.h"

/*
 * Ship Type Repository Layer
 * 
 * Phase 4: Data-Driven Ship Types
 * Centralizes all shiptype-related DB operations, particularly restriction checking.
 */

typedef struct {
    int restriction_id;
    char check_type[20];       /* CEO, ALIGNMENT_MIN, ALIGNMENT_MAX, SCORE_MIN, CUSTOM */
    char check_value[255];     /* Flexible value storage */
    char description[512];     /* Human-readable: "Must have alignment > 200" */
    int enabled;
} shiptype_restriction_t;

typedef struct {
    int alignment;
    int commission_id;
    long long score;
    int player_id;
    int is_ceo;                /* Set by validation function */
} player_info_t;

/*
 * repo_shiptypes_get_restrictions()
 * Fetch all restrictions for a given ship type.
 * Returns result set; caller must finalize.
 * Returns 0 on success, -1 on error.
 */
int repo_shiptypes_get_restrictions(db_t *db, int shiptypes_id, db_res_t **out_res);

/*
 * repo_shiptypes_validate_eligibility()
 * Check if player can purchase/use a given ship type.
 * Evaluates all restrictions for that type.
 * Returns 0 if eligible, non-zero if restricted.
 * 
 * player_info_t must be populated with player stats before calling.
 */
int repo_shiptypes_validate_eligibility(db_t *db, int shiptypes_id, const player_info_t *player_info);

/*
 * repo_shiptypes_get_restriction_desc()
 * Fetch human-readable description of why ship type is restricted.
 * Returns malloc'd string; caller must free.
 * Returns NULL if type is unrestricted or on error.
 */
char *repo_shiptypes_get_restriction_desc(db_t *db, int shiptypes_id, const player_info_t *player_info);

#endif /* REPO_SHIPTYPES_H */
