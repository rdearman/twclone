#ifndef REPO_PORTTYPES_H
#define REPO_PORTTYPES_H

#include "db/db_api.h"
#include <stdbool.h>

typedef struct {
  int porttype_id;
  char code[64];
  char description[256];
  bool can_buy;
  bool can_sell;
  bool is_stardock;
  bool is_black_market;
} porttype_t;

/**
 * Get port type by numeric ID
 * Returns TRUE if found, FALSE otherwise
 */
bool repo_porttypes_get_by_id(db_t *db, int porttype_id, porttype_t *out_porttype);

/**
 * Get port type by code string
 * Returns TRUE if found, FALSE otherwise
 */
bool repo_porttypes_get_by_code(db_t *db, const char *code, porttype_t *out_porttype);

/**
 * Check if port type is a stardock
 * Returns TRUE if port is stardock, FALSE otherwise
 */
bool repo_porttypes_is_stardock(db_t *db, int porttype_id);

/**
 * Check if port type is a black market
 * Returns TRUE if port is black market, FALSE otherwise
 */
bool repo_porttypes_is_black_market(db_t *db, int porttype_id);

/**
 * Get capabilities (can_buy, can_sell) for a port type
 * out_can_buy and out_can_sell can be NULL if not needed
 * Returns TRUE if porttype found, FALSE otherwise
 */
bool repo_porttypes_get_capabilities(db_t *db, int porttype_id, 
                                      bool *out_can_buy, bool *out_can_sell);

#endif // REPO_PORTTYPES_H
