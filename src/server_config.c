#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <jansson.h>
#include <ctype.h>
#include <string.h>
#include <jansson.h>
#include <sqlite3.h>
/* local inlcudes */
#include "server_config.h"
#include "database.h"
#include "server_loop.h"
#include "errors.h"
#include "config.h"
#include "schemas.h"
#include "server_envelope.h"
#include "server_config.h"
#include "server_log.h"

server_config_t g_cfg;
json_t *g_capabilities = NULL;

/* Provided by your DB module; MUST be defined there (no 'static') */
sqlite3 *g_db = NULL;

/* --------- static helpers (not visible to linker) --------- */

static void
set_defaults (void)
{
  memset (&g_cfg, 0, sizeof g_cfg);

  g_cfg.engine.tick_ms = 50;
  g_cfg.engine.daily_align_sec = 0;

  g_cfg.batching.event_batch = 128;
  g_cfg.batching.command_batch = 64;
  g_cfg.batching.broadcast_batch = 128;

  g_cfg.priorities.default_command_weight = 100;
  g_cfg.priorities.default_event_weight = 100;

  snprintf (g_cfg.s2s.transport, sizeof g_cfg.s2s.transport, "tcp");
  snprintf (g_cfg.s2s.tcp_host, sizeof g_cfg.s2s.tcp_host, "127.0.0.1");
  g_cfg.s2s.tcp_port = 4321;
  g_cfg.s2s.frame_size_limit = 2 * 1024 * 1024;

  g_cfg.safety.connect_ms = 1500;
  g_cfg.safety.handshake_ms = 1500;
  g_cfg.safety.rpc_ms = 5000;
  g_cfg.safety.backoff_initial_ms = 100;
  g_cfg.safety.backoff_max_ms = 2000;
  g_cfg.safety.backoff_factor = 2.0;

  g_cfg.secrets.key_id[0] = '\0';
  g_cfg.secrets.key_len = 0;
}



static int
validate_cfg (void)
{
  if (g_cfg.engine.tick_ms < 1)
    {
      fprintf (stderr, "ERROR config: engine.tick_ms must be >= 1 (got %d)\n",
	       g_cfg.engine.tick_ms);
      return 0;
    }
  if (strcasecmp (g_cfg.s2s.transport, "uds") != 0
      && strcasecmp (g_cfg.s2s.transport, "tcp") != 0)
    {
      fprintf (stderr,
	       "ERROR config: s2s.transport must be one of [uds|tcp] (got \"%s\")\n",
	       g_cfg.s2s.transport);
      return 0;
    }
  if (!strcasecmp (g_cfg.s2s.transport, "uds") && g_cfg.s2s.uds_path[0] == '\0')
    {
      fprintf (stderr,
	       "ERROR config: s2s.uds_path required when s2s.transport=uds\n");
      return 0;
    }
  if (!strcasecmp (g_cfg.s2s.transport, "tcp") &&
      (g_cfg.s2s.tcp_host[0] == '\0' || g_cfg.s2s.tcp_port <= 0))
    {
      fprintf (stderr,
	       "ERROR config: s2s.tcp_host/tcp_port required when s2s.transport=tcp\n");
      return 0;
    }
  if (g_cfg.s2s.frame_size_limit > 8 * 1024 * 1024)
    {
      fprintf (stderr,
	       "ERROR config: s2s.frame_size_limit exceeds 8 MiB (%d)\n",
	       g_cfg.s2s.frame_size_limit);
      return 0;
    }
  return 1;
}

/* --------- exported API --------- */


void
print_effective_config_redacted (void)
{
  printf ("INFO config: effective (secrets redacted)\n");
  printf ("{\"engine\":{\"tick_ms\":%d,\"daily_align_sec\":%d},",
	  g_cfg.engine.tick_ms, g_cfg.engine.daily_align_sec);

  printf
    ("\"batching\":{\"event_batch\":%d,\"command_batch\":%d,\"broadcast_batch\":%d},",
     g_cfg.batching.event_batch, g_cfg.batching.command_batch,
     g_cfg.batching.broadcast_batch);

  printf
    ("\"priorities\":{\"default_command_weight\":%d,\"default_event_weight\":%d},",
     g_cfg.priorities.default_command_weight,
     g_cfg.priorities.default_event_weight);

  printf
    ("\"s2s\":{\"transport\":\"%s\",\"uds_path\":\"%s\",\"tcp_host\":\"%s\",\"tcp_port\":%d,\"frame_size_limit\":%d},",
     g_cfg.s2s.transport, g_cfg.s2s.uds_path, g_cfg.s2s.tcp_host,
     g_cfg.s2s.tcp_port, g_cfg.s2s.frame_size_limit);

  printf ("\"safety\":{\"connect_ms\":%d,\"handshake_ms\":%d,\"rpc_ms\":%d,"
	  "\"backoff_initial_ms\":%d,\"backoff_max_ms\":%d,\"backoff_factor\":%.2f},",
	  g_cfg.safety.connect_ms, g_cfg.safety.handshake_ms,
	  g_cfg.safety.rpc_ms, g_cfg.safety.backoff_initial_ms,
	  g_cfg.safety.backoff_max_ms, g_cfg.safety.backoff_factor);

  printf ("\"secrets\":{\"key_id\":\"%s\",\"hmac\":\"********\"}}\n",
	  g_cfg.secrets.key_id[0] ? "********" : "");
}



static void
apply_db (sqlite3 *db)
{
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db, "SELECT key,value FROM app_config", -1, &st, NULL) != SQLITE_OK)
    {
      LOGI("[config] no app_config table found, using defaults (%s)\n", sqlite3_errmsg(db));
      return;
    }
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      const char *k = (const char *) sqlite3_column_text (st, 0);
      const char *v = (const char *) sqlite3_column_text (st, 1);
      // map k -> g_cfg fields; parse ints/doubles where needed
      if (!strcasecmp (k, "engine.tick_ms"))
	g_cfg.engine.tick_ms = atoi (v);
      else if (!strcasecmp (k, "engine.daily_align_sec"))
	g_cfg.engine.daily_align_sec = atoi (v);

      else if (!strcasecmp (k, "batch.event_batch"))
	g_cfg.batching.event_batch = atoi (v);
      else if (!strcasecmp (k, "batch.command_batch"))
	g_cfg.batching.command_batch = atoi (v);
      else if (!strcasecmp (k, "batch.broadcast_batch"))
	g_cfg.batching.broadcast_batch = atoi (v);

      else if (!strcasecmp (k, "prio.default_command_weight"))
	g_cfg.priorities.default_command_weight = atoi (v);
      else if (!strcasecmp (k, "prio.default_event_weight"))
	g_cfg.priorities.default_event_weight = atoi (v);

      else if (!strcasecmp (k, "s2s.transport"))
	snprintf (g_cfg.s2s.transport, sizeof g_cfg.s2s.transport, "%s", v);
      else if (!strcasecmp (k, "s2s.uds_path"))
	snprintf (g_cfg.s2s.uds_path, sizeof g_cfg.s2s.uds_path, "%s", v);
      else if (!strcasecmp (k, "s2s.tcp_host"))
	snprintf (g_cfg.s2s.tcp_host, sizeof g_cfg.s2s.tcp_host, "%s", v);
      else if (!strcasecmp (k, "s2s.tcp_port"))
	g_cfg.s2s.tcp_port = atoi (v);
      else if (!strcasecmp (k, "s2s.frame_size_limit"))
	g_cfg.s2s.frame_size_limit = atoi (v);

      else if (!strcasecmp (k, "safety.connect_ms"))
	g_cfg.safety.connect_ms = atoi (v);
      else if (!strcasecmp (k, "safety.handshake_ms"))
	g_cfg.safety.handshake_ms = atoi (v);
      else if (!strcasecmp (k, "safety.rpc_ms"))
	g_cfg.safety.rpc_ms = atoi (v);
      else if (!strcasecmp (k, "safety.backoff_initial_ms"))
	g_cfg.safety.backoff_initial_ms = atoi (v);
      else if (!strcasecmp (k, "safety.backoff_max_ms"))
	g_cfg.safety.backoff_max_ms = atoi (v);
      else if (!strcasecmp (k, "safety.backoff_factor"))
	g_cfg.safety.backoff_factor = atof (v);
    }
  sqlite3_finalize (st);

  // Secrets: pick active key (redacted in print)
  sqlite3_stmt *ks = NULL;
  if (sqlite3_prepare_v2 (db,
			  "SELECT key_id,key_b64 FROM s2s_keys WHERE active=1 AND is_default_tx=1 LIMIT 1",
			  -1, &ks, NULL) == SQLITE_OK
      && sqlite3_step (ks) == SQLITE_ROW)
    {
      const char *kid = (const char *) sqlite3_column_text (ks, 0);
//       const char *b64 = (const char *) sqlite3_column_text (ks, 1);
      snprintf (g_cfg.secrets.key_id, sizeof g_cfg.secrets.key_id, "%s",
		kid ? kid : "");
      // decode b64 → g_cfg.secrets.key / key_len (or keep it where your transport already expects it)
    }
  sqlite3_finalize (ks);
}

static void
apply_env (void)
{				/* same names as before: TW_ENGINE_TICK_MS, etc. */
}

int
load_eng_config (void)
{
  set_defaults ();

  // ensure DB is open/initialised here (you already do schema creation)
  extern sqlite3 *g_db;
  if (g_db)
    apply_db (g_db);

  apply_env ();			// ENV overrides DB
  return validate_cfg ();
}

void send_enveloped_ok (int fd, json_t * root, const char *type,
			json_t * data);

// exported by server_loop.c
void loop_get_supported_commands (const cmd_desc_t ** out_tbl, size_t *out_n);


/*
static int
resolve_current_sector_from_info (json_t *info_obj, int fallback)
{
  if (!json_is_object (info_obj))
    return fallback;

  // Preferred flat field
  json_t *j = json_object_get (info_obj, "current_sector");
  if (json_is_integer (j))
    return (int) json_integer_value (j);

  // Common alternates
  json_t *ship = json_object_get (info_obj, "ship");
  if (json_is_object (ship))
    {
      j = json_object_get (ship, "sector_id");
      if (json_is_integer (j))
	return (int) json_integer_value (j);
    }
  json_t *player = json_object_get (info_obj, "player");
  if (json_is_object (player))
    {
      j = json_object_get (player, "sector_id");
      if (json_is_integer (j))
	return (int) json_integer_value (j);
    }
  return fallback;
}
*/

/////////////////////////// NEW 

static json_t *
make_session_hello_payload (int is_authed, int player_id, int sector_id)
{
  json_t *payload = json_object ();
  json_object_set_new (payload, "protocol_version", json_string ("1.0"));
  json_object_set_new (payload, "server_time_unix",
		       json_integer ((json_int_t) time (NULL)));
  json_object_set_new (payload, "authenticated",
		       is_authed ? json_true () : json_false ());
  if (is_authed)
    {
      json_object_set_new (payload, "player_id", json_integer (player_id));
      json_object_set_new (payload, "current_sector",
			   sector_id >
			   0 ? json_integer (sector_id) : json_null ());
    }
  else
    {
      json_object_set_new (payload, "player_id", json_null ());
      json_object_set_new (payload, "current_sector", json_null ());
    }

  /* NEW: ISO-8601 UTC timestamp for clients that prefer strings */
  char iso[32];
  iso8601_utc (iso);
  json_object_set_new (payload, "server_time", json_string (iso));
  return payload;
}


/* ---------- system.hello ---------- */
int
cmd_system_hello (client_ctx_t *ctx, json_t *root)
{
  // alias to session.hello
  return cmd_session_hello (ctx, root);
}

//////////////////

// Define the handler function
int
cmd_system_capabilities (client_ctx_t *ctx, json_t *root)
{
  send_enveloped_ok (ctx->fd, root, "system.capabilities",
		     json_incref (g_capabilities));
  return 0;
}


/* /\* ---------- system.describe_schema (optional) ---------- *\/ */
/* int */
/* cmd_system_describe_schema (client_ctx_t *ctx, json_t *root) */
/* { */
/*   json_t *jdata = json_object_get (root, "data"); */
/*   const char *key = NULL; */
/*   if (json_is_object (jdata)) */
/*     { */
/*       json_t *jkey = json_object_get (jdata, "key"); */
/*       if (json_is_string (jkey)) */
/* 	key = json_string_value (jkey); */
/*     } */

/*   if (!key) */
/*     { */
/*       /\* Return the list of keys we have *\/ */
/*       json_t *data = json_pack ("{s:o}", "available", schema_keys ()); */
/*       send_enveloped_ok (ctx->fd, root, "system.schema_list", data); */
/*       json_decref (data); */
/*     } */
/*   else */
/*     { */
/*       json_t *schema = schema_get (key); */
/*       if (!schema) */
/* 	{ */
/* 	  send_enveloped_error (ctx->fd, root, 1306, "Schema not found"); */
/* 	} */
/*       else */
/* 	{ */
/* 	  json_t *data = */
/* 	    json_pack ("{s:s, s:o}", "key", key, "schema", schema); */
/* 	  send_enveloped_ok (ctx->fd, root, "system.schema", data); */
/* 	  json_decref (schema); */
/* 	  json_decref (data); */
/* 	} */
/*     } */

/*   return 0; */
/* } */

/* ---------- session.ping ---------- */
int
cmd_session_ping (client_ctx_t *ctx, json_t *root)
{
  /* Echo back whatever is in data (or {}) */
  json_t *jdata = json_object_get (root, "data");
  json_t *echo =
    json_is_object (jdata) ? json_incref (jdata) : json_object ();
  send_enveloped_ok (ctx->fd, root, "session.pong", echo);
  json_decref (echo);
  return 0;
}


int
cmd_session_hello (client_ctx_t *ctx, json_t *root)
{
//   const char *req_id = json_string_value (json_object_get (root, "id"));

  json_t *payload = make_session_hello_payload ((ctx->player_id > 0),
						ctx->player_id,
						ctx->sector_id);

  // Use ONE helper that builds a proper envelope including reply_to + status.
  // If your send_enveloped_ok doesn't add reply_to, fix it (next section).
  send_enveloped_ok (ctx->fd, root, "session.hello", payload);

  json_decref (payload);
  return 0;			// IMPORTANT: do not send another frame after this
}

int
cmd_session_disconnect (client_ctx_t *ctx, json_t *root)
{
  json_t *data = json_pack ("{s:s}", "message", "Goodbye");
  send_enveloped_ok (ctx->fd, root, "system.goodbye", data);
  json_decref (data);

  shutdown (ctx->fd, SHUT_RDWR);
  close (ctx->fd);
  return 0;			/* or break your per-connection loop appropriately */
}
