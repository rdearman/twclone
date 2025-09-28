#ifndef SERVER_CITADEL_H
#define SERVER_CITADEL_H

#include <jansson.h>
#include "common.h"		// client_ctx_t

#ifdef __cplusplus
extern "C"
{
#endif

// Citadel-related commands (expand as your protocol grows)
  int cmd_citadel_build (client_ctx_t * ctx, json_t * root);	// "citadel.build"
  int cmd_citadel_upgrade (client_ctx_t * ctx, json_t * root);	// "citadel.upgrade"
// Optional, if you decide to expose it:
// int cmd_citadel_info    (client_ctx_t *ctx, json_t *root);  // "citadel.info"

#ifdef __cplusplus
}
#endif

#endif				/* SERVER_CITADEL_H */
