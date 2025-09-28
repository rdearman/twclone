#include "server_combat.h"
#include "server_envelope.h"	// send_enveloped_ok/error/refused
#include "errors.h"
#include "config.h"
#include <jansson.h>

/* --- common helpers --- */
static inline int
require_auth (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id > 0)
    return 1;
  send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
  return 0;
}

static inline int
niy (client_ctx_t *ctx, json_t *root, const char *which)
{
  (void) which;
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented");
  return 0;
}

/* ---------- combat.attack ---------- */
int
cmd_combat_attack (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse target, resolve sector, dmg calc, state updates, events
  return niy (ctx, root, "combat.attack");
}

/* ---------- combat.deploy_fighters ---------- */
int
cmd_combat_deploy_fighters (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse amount, reduce ship fighters, spawn sector entities
  return niy (ctx, root, "combat.deploy_fighters");
}

/* ---------- combat.lay_mines ---------- */
int
cmd_combat_lay_mines (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse amount, lay mines in sector grid
  return niy (ctx, root, "combat.lay_mines");
}

/* ---------- combat.sweep_mines ---------- */
int
cmd_combat_sweep_mines (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse sweep strength, clear mines, apply risk to ship
  return niy (ctx, root, "combat.sweep_mines");
}

/* ---------- combat.status ---------- */
int
cmd_combat_status (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: return sector combat snapshot (entities, mines, fighters, cooldowns)
  return niy (ctx, root, "combat.status");
}
