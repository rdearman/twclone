#ifndef SCHEMAS_H
#define SCHEMAS_H
#include <jansson.h>

/* Build the system.capabilities payload */
json_t *capabilities_build (void);

/* Return a JSON Schema object for a key ("auth.login", "move.warp", "trade.buy", "envelope") or NULL */
json_t *schema_get (const char *key);

/* Return an array of available schema keys (strings) */
json_t *schema_keys (void);

#endif
