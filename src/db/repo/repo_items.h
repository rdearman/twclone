#ifndef REPO_ITEMS_H
#define REPO_ITEMS_H

#include "db/db_api.h"
#include <stdbool.h>

typedef struct {
  int item_id;
  char code[128];
  char name[256];
  char category[64];
  bool is_illegal;
  int min_alignment;
  int max_alignment;
  bool can_buy;
  bool can_sell;
} item_t;

/**
 * Get item by code
 * Returns TRUE if found, FALSE otherwise
 */
bool repo_items_get_by_code(db_t *db, const char *code, item_t *out_item);

/**
 * Get item by ID
 * Returns TRUE if found, FALSE otherwise
 */
bool repo_items_get_by_id(db_t *db, int item_id, item_t *out_item);

/**
 * Check if item is available at a port type
 * Sets out_can_buy and out_can_sell if not NULL
 * Returns TRUE if available, FALSE otherwise
 */
bool repo_items_is_available_at_porttype(db_t *db, int item_id, 
                                          int porttype_id,
                                          bool *out_can_buy, 
                                          bool *out_can_sell);

/**
 * Check item legality for player
 * Returns NULL if allowed, else error code string
 * player_alignment: player's current alignment
 * cluster_alignment: cluster's base alignment (if applicable)
 */
int repo_items_validate_legality(db_t *db, int item_id, 
                                  int player_alignment,
                                  int cluster_alignment);

/**
 * Check item alignment requirements
 * Returns 0 if alignment meets requirements, error code otherwise
 */
int repo_items_validate_alignment(db_t *db, int item_id, int player_alignment);

#endif // REPO_ITEMS_H
