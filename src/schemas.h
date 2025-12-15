#ifndef SCHEMAS_H
#define SCHEMAS_H
#include <jansson.h>

/**
 * @brief Build the system.capabilities payload.
 */
json_t *capabilities_build (void);

/**
 * @brief (C2S) Return a JSON Schema object for a client-facing key.
 *
 * @param key The schema key (e.g., "auth.login", "move.warp").
 * @return A new json_t* reference to the schema, or NULL if not found.
 */
json_t *schema_get (const char *key);

/**
 * @brief (C2S) Return an array of all available client-facing schema keys.
 *
 * @return A new json_t* reference to an array of strings.
 */
json_t *schema_keys (void);

/**
 * @brief (C2S) Validate a client payload against its registered JSON Schema.
 *
 * @param type The command key (e.g., "auth.login").
 * @param payload The JSON object payload from the client.
 * @param why A pointer to a char* that will be set to an error message on failure.
 * @return 0 on success, -1 on failure.
 */
int schema_validate_payload (const char *type, json_t *payload, char **why);

/**
 * @brief (S2S) Manually validate an inter-server (s2s) payload.
 *
 * @param type The S2S command key (e.g., "s2s.health.check").
 * @param payload The JSON object payload from the other server.
 * @param why A pointer to a char* that will be set to an error message on failure.
 * @return 0 on success, -1 on failure.
 */
int s2s_validate_payload (const char *type, json_t *payload, char **why);
#endif
