#include "server_s2s.h"
#include "server_envelope.h"	// send_enveloped_ok/error/refused
#include "errors.h"
#include "config.h"
#include <jansson.h>
#include <string.h>

/* --------- S2S auth gate (customize later) ---------
   Looks for meta.s2s_token or meta.api_key on the request and
   compares with a server-side key (e.g., from config).
   For now it just NIY-refuses if missing; replace with real check.
*/
static int
require_s2s (json_t *root, const char **why_out)
{
  if (why_out)
    *why_out = NULL;

  json_t *meta = json_object_get (root, "meta");
  const char *tok = NULL, *api_key = NULL;
  if (json_is_object (meta))
    {
      json_t *jtok = json_object_get (meta, "s2s_token");
      if (json_is_string (jtok))
	tok = json_string_value (jtok);
      json_t *jkey = json_object_get (meta, "api_key");
      if (json_is_string (jkey))
	api_key = json_string_value (jkey);
    }

  // TODO: compare tok/api_key to configured secret.
  if (!tok && !api_key)
    {
      if (why_out)
	*why_out = "Missing S2S credentials";
      return 0;
    }
  return 1;
}

static inline int
niy (client_ctx_t *ctx, json_t *root, const char *which)
{
  (void) which;
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented");
  return 0;
}

/* ---------- s2s.planet.genesis ---------- */
int
cmd_s2s_planet_genesis (client_ctx_t *ctx, json_t *root)
{
  const char *why = NULL;
  if (!require_s2s (root, &why))
    {
      send_enveloped_refused (ctx->fd, root, 1401, why ? why : "Unauthorized",
			      NULL);
      return 0;
    }
  // TODO: parse sector_id, seed, owner, call DB, broadcast
  return niy (ctx, root, "s2s.planet.genesis");
}

/* ---------- s2s.planet.transfer ---------- */
int
cmd_s2s_planet_transfer (client_ctx_t *ctx, json_t *root)
{
  const char *why = NULL;
  if (!require_s2s (root, &why))
    {
      send_enveloped_refused (ctx->fd, root, 1401, why ? why : "Unauthorized",
			      NULL);
      return 0;
    }
  // TODO: parse planet_id, from_server, to_server, handoff metadata
  return niy (ctx, root, "s2s.planet.transfer");
}

/* ---------- s2s.player.migrate ---------- */
int
cmd_s2s_player_migrate (client_ctx_t *ctx, json_t *root)
{
  const char *why = NULL;
  if (!require_s2s (root, &why))
    {
      send_enveloped_refused (ctx->fd, root, 1401, why ? why : "Unauthorized",
			      NULL);
      return 0;
    }
  // TODO: parse player_id, snapshot blob, import/export
  return niy (ctx, root, "s2s.player.migrate");
}

/* ---------- s2s.port.restock ---------- */
int
cmd_s2s_port_restock (client_ctx_t *ctx, json_t *root)
{
  const char *why = NULL;
  if (!require_s2s (root, &why))
    {
      send_enveloped_refused (ctx->fd, root, 1401, why ? why : "Unauthorized",
			      NULL);
      return 0;
    }
  // TODO: parse sector_id/port_id, quantities, pricing policy
  return niy (ctx, root, "s2s.port.restock");
}

/* ---------- s2s.event.relay ---------- */
int
cmd_s2s_event_relay (client_ctx_t *ctx, json_t *root)
{
  const char *why = NULL;
  if (!require_s2s (root, &why))
    {
      send_enveloped_refused (ctx->fd, root, 1401, why ? why : "Unauthorized",
			      NULL);
      return 0;
    }
  // TODO: parse event type/payload, fanout to connected players/servers
  return niy (ctx, root, "s2s.event.relay");
}

/* ---------- s2s.replication.heartbeat ---------- */
int
cmd_s2s_replication_heartbeat (client_ctx_t *ctx, json_t *root)
{
  const char *why = NULL;
  if (!require_s2s (root, &why))
    {
      send_enveloped_refused (ctx->fd, root, 1401, why ? why : "Unauthorized",
			      NULL);
      return 0;
    }
  // TODO: update replication status; reply with ack + version/lsn
  json_t *payload = json_pack ("{s:s}", "status", "ok");
  send_enveloped_ok (ctx->fd, root, "s2s.heartbeat", payload);
  json_decref (payload);
  return 0;
}
