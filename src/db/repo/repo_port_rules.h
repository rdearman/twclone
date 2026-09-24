#ifndef REPO_PORT_RULES_H
#define REPO_PORT_RULES_H

#include "db_api.h"
#include <stdbool.h>

/* Struct for port commodity rule */
typedef struct {
    int porttype_commodity_rule_id;
    int porttype_id;
    char *commodity_code;
    bool can_buy;
    bool can_sell;
    int base_price_mul;         /* 100 = 1.0x */
    int qty_max_mul;            /* 100 = 1.0x */
    bool *allow_illegal_override; /* NULL if not set */
    int *min_alignment;         /* NULL if not set */
    int *max_alignment;         /* NULL if not set */
    char *notes;
} porttype_commodity_rule_t;

/* Struct for port type rules */
typedef struct {
    int porttype_rule_id;
    int porttype_id;
    bool allow_illegal;
    int *min_alignment;         /* NULL if not set */
    int *max_alignment;         /* NULL if not set */
    char *notes;
} porttype_rule_t;

/* Struct for cluster modifiers */
typedef struct {
    int porttype_cluster_modifier_id;
    int porttype_id;
    int cluster_id;
    int price_mul;              /* 100 = 1.0x */
    int contraband_price_mul;   /* 100 = 1.0x */
    char *notes;
} porttype_cluster_modifier_t;

/**
 * Get commodity rule for a port type
 * Returns 0 on success (found), -1 if not found, other on error
 */
int repo_port_commodity_rule_get(db_t *db, int porttype_id,
                                  const char *commodity_code,
                                  porttype_commodity_rule_t *out_rule,
                                  bool *out_found);

/**
 * Get rules for a port type
 * Returns 0 on success, -1 if not found, other on error
 */
int repo_port_rules_get(db_t *db, int porttype_id,
                        porttype_rule_t *out_rules,
                        bool *out_found);

/**
 * Get cluster modifier for porttype
 * Returns 0 on success (found), -1 if not found, other on error
 */
int repo_port_cluster_modifier_get(db_t *db, int porttype_id,
                                    int cluster_id,
                                    porttype_cluster_modifier_t *out_mod,
                                    bool *out_found);

/**
 * Calculate effective price multiplier (combined porttype + cluster)
 * Uses integer math: 100 = 1.0x, 150 = 1.5x, etc.
 * Returns multiplier or 100 if not found/error
 */
int repo_port_effective_price_mul(db_t *db, int porttype_id,
                                   int cluster_id);

#endif /* REPO_PORT_RULES_H */
