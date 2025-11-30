#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <stdlib.h>
/* local includes */
#include "database.h"
#include "server_s2s.h"
#include "server_envelope.h"    // send_enveloped_ok/error/refused
#include "errors.h"
#include "config.h"
#include "database.h"
#include "server_envelope.h"
#include "s2s_transport.h"
#include "schemas.h"
#include <time.h>
static int
handle_command_push (s2s_conn_t *c, json_t *env)
{
  json_t *pl = s2s_env_payload (env);
  const char *cmd_type = json_string_value (json_object_get (pl, "cmd_type"));
  const char *idem_key = json_string_value (json_object_get (pl, "idem_key"));
  json_t *cmd_payload = json_object_get (pl, "payload");
  /* now it's safe to log */
  fprintf (stderr, "[server] handle_command_push: %s %s\n",
           cmd_type ? cmd_type : "(null)", idem_key ? idem_key : "(null)");
  int cmd_id = 0, duplicate = 0, due_at = (int) time (NULL);
  int rcdb = db_commands_accept (cmd_type, idem_key, cmd_payload,
                                 &cmd_id, &duplicate, &due_at);
  if (rcdb != 0)
    {
      json_t *err = s2s_make_error ("server", "engine", s2s_env_id (env),
                                    "internal_error", "db error", NULL);
      s2s_send_env (c, err, 2000);
      json_decref (err);
      return 0;
    }
  json_t *ackpl = json_pack ("{s:b,s:b,s:i,s:s,s:i}",
                             "accepted", 1,
                             "duplicate", duplicate,
                             "cmd_id", cmd_id,
                             "status", "ready",
                             "due_at", due_at);
  json_t *ack = s2s_make_ack ("server", "engine", s2s_env_id (env), ackpl);
  json_decref (ackpl);
  int rcsend = s2s_send_env (c, ack, 2000);
  json_decref (ack);
  return rcsend;
}


/* ------------ handlers (add more types as you implement) ------------ */
static int
handle_broadcast_sweep (s2s_conn_t *c, json_t *env)
{
  json_t *pl = s2s_env_payload (env);
  json_t *since_js = json_object_get (pl, "since_ts");
  long long since_ts = (since_js && json_is_integer (since_js))
    ? (long long) json_integer_value (since_js) : 0;
  // For now, just ACK; wire real logic later.
  json_t *ackpl =
    json_pack ("{s:b,s:I}", "ok", 1, "since_ts", (json_int_t) since_ts);
  json_t *ack = s2s_make_ack ("server", "engine", s2s_env_id (env), ackpl);
  json_decref (ackpl);
  int rc = s2s_send_env (c, ack, 2000);
  json_decref (ack);
  return rc;
}


static int
handle_health_check (s2s_conn_t *c, json_t *env)
{
  time_t now = time (NULL);
  json_t *pl = json_pack ("{s:s,s:s,s:I}",
                          "role", "server",
                          "version", "0.1.0",
                          "uptime_s", (json_int_t) (now));      /* TODO: subtract start ts */
  json_t *ack = s2s_make_ack ("server", "engine", s2s_env_id (env), pl);
  json_decref (pl);
  int rc = s2s_send_env (c, ack, 2000);
  json_decref (ack);
  return rc;
}


/* ------------ dispatcher ------------ */
int
server_s2s_dispatch (s2s_conn_t *c, json_t *env)
{
  const char *type = s2s_env_type (env);
  if (!type)
    {
      return 0;
    }
  /* payload validation (lightweight, per catalogue) */
  json_t *payload = s2s_env_payload (env);
  char *why = NULL;
  if (schema_validate_payload (type, payload, &why) != 0)
    {
      json_t *err = s2s_make_error ("server", "engine", s2s_env_id (env),
                                    "bad_request",
                                    (why ? why : "invalid payload"), NULL);
      free (why);
      s2s_send_env (c, err, 2000);
      json_decref (err);
      return 0;
    }
  if (strcasecmp (type, "s2s.broadcast.sweep") == 0)
    {
      return handle_broadcast_sweep (c, env);
    }
  if (strcasecmp (type, "s2s.health.check") == 0)
    {
      return handle_health_check (c, env);
    }
  if (strcasecmp (type, "s2s.command.push") == 0)
    {
      return handle_command_push (c, env);
    }
  json_t *err = s2s_make_error ("server", "engine", s2s_env_id (env),
                                "unsupported_type", type, NULL);
  s2s_send_env (c, err, 2000);
  json_decref (err);
  return 0;
}


/* ------------ control thread ------------ */
typedef struct
{
  s2s_conn_t *conn;
  volatile sig_atomic_t *running_flag;
} s2s_thr_ctx_t;
static void *
s2s_control_thread_fn (void *arg)
{
  s2s_thr_ctx_t *ctx = (s2s_thr_ctx_t *) arg;
  s2s_conn_t *c = ctx->conn;
  while (*(ctx->running_flag))
    {
      json_t *env = NULL;
      int rc = s2s_recv_env (c, &env, 1000);    /* 1s poll */
      if (rc == 0 && env)
        {
          server_s2s_dispatch (c, env);
          json_decref (env);
          continue;
        }
      if (rc == S2S_E_CLOSED)
        {
          break;                /* peer closed */
        }
      if (rc == S2S_E_TIMEOUT)
        {
          continue;             /* idle */
        }
      if (rc == S2S_E_AUTH_REQUIRED || rc == S2S_E_AUTH_BAD
          || rc == S2S_E_TOOLARGE)
        {
          break;                /* hard errors: exit thread */
        }
      /* Other I/O errors: log and continue/retry */
    }
  free (ctx);
  return NULL;
}


int
server_s2s_start (s2s_conn_t *conn, pthread_t *out_thr,
                  volatile sig_atomic_t *running_flag)
{
  if (!conn || !out_thr || !running_flag)
    {
      return -1;
    }
  s2s_thr_ctx_t *ctx = (s2s_thr_ctx_t *) calloc (1, sizeof (*ctx));
  if (!ctx)
    {
      return -1;
    }
  ctx->conn = conn;
  ctx->running_flag = running_flag;
  return pthread_create (out_thr, NULL, s2s_control_thread_fn, ctx);
}


void
server_s2s_stop (pthread_t thr)
{
  if (thr)
    {
      pthread_join (thr, NULL);
    }
}


///////////////////////////////////////////
/* --------- S2S auth gate (customize later) ---------
   Looks for meta.s2s_token or meta.api_key on the request and
   compares with a server-side key (e.g., from config).
   For now it just NIY-refuses if missing; replace with real check.
 */
static int
require_s2s (json_t *root, const char **why_out)
{
  if (why_out)
    {
      *why_out = NULL;
    }
  json_t *meta = json_object_get (root, "meta");
  const char *tok = NULL, *api_key = NULL;
  if (json_is_object (meta))
    {
      json_t *jtok = json_object_get (meta, "s2s_token");
      if (json_is_string (jtok))
        {
          tok = json_string_value (jtok);
        }
      json_t *jkey = json_object_get (meta, "api_key");
      if (json_is_string (jkey))
        {
          api_key = json_string_value (jkey);
        }
    }
  // TODO: compare tok/api_key to configured secret.
  if (!tok && !api_key)
    {
      if (why_out)
        {
          *why_out = "Missing S2S credentials";
        }
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

