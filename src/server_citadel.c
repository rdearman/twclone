#include "server_citadel.h"
#include "server_envelope.h"	// send_enveloped_ok/error/refused
#include "errors.h"
#include "config.h"
#include <jansson.h>

/* Not Implemented helper */
static inline int
niy (client_ctx_t *ctx, json_t *root, const char *which)
{
  (void) which;			// keep for future logging if you want
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented");
  return 0;
}

/* Require auth before touching citadel features */
static inline int
require_auth (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id > 0)
    return 1;
  send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
  return 0;
}

/* ---------- citadel.build ---------- */
int
cmd_citadel_build (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;

  // TODO: parse data, validate resources, create/upgrade citadel
  return niy (ctx, root, "citadel.build");
}

/* ---------- citadel.upgrade ---------- */
int
cmd_citadel_upgrade (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;

  // TODO: parse target level, costs, apply DB changes
  return niy (ctx, root, "citadel.upgrade");
}

/* // Optional: citadel.info
int cmd_citadel_info(client_ctx_t *ctx, json_t *root) {
  if (!require_auth(ctx, root)) return 0;
  return niy(ctx, root, "citadel.info");
}
*/
