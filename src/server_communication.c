#include "server_communication.h"
#include "server_envelope.h"	// send_enveloped_ok/error/refused
#include "errors.h"
#include "config.h"
#include <jansson.h>
#include "server_cmds.h"
#include <string.h>

// forward-declared somewhere in this module already:
static const char *ALLOWED_TOPICS[];

extern void attach_rate_limit_meta (json_t * env, client_ctx_t * ctx);
extern void rl_tick (client_ctx_t * ctx);
extern void send_all_json (int fd, json_t * obj);
extern json_t *db_notice_list_unseen_for_player (int player_id);
extern int db_notice_mark_seen (int notice_id, int player_id);



typedef struct sub_node
{
  char *topic;
  struct sub_node *next;
} sub_node_t;

typedef struct sub_map
{
  client_ctx_t *ctx;
  sub_node_t *head;
  struct sub_map *next;
} sub_map_t;


static sub_map_t *g_submaps = NULL;


/* find or create the per-ctx map node */
static sub_map_t *
submap_get (client_ctx_t *ctx, int create_if_missing)
{
  for (sub_map_t * m = g_submaps; m; m = m->next)
    if (m->ctx == ctx)
      return m;
  if (!create_if_missing)
    return NULL;
  sub_map_t *m = (sub_map_t *) calloc (1, sizeof *m);
  if (!m)
    return NULL;
  m->ctx = ctx;
  m->head = NULL;
  m->next = g_submaps;
  g_submaps = m;
  return m;
}

static int
matches_pattern (const char *topic, const char *pattern)
{
  const char *star = strchr (pattern, '*');
  if (!star)
    return strcmp (topic, pattern) == 0;	/* exact */
  /* suffix wildcard: "prefix.*" */
  size_t plen = (size_t) (star - pattern);
  return strncmp (topic, pattern, plen) == 0;
}

static int
is_valid_topic (const char *topic)
{
  if (!topic || !*topic)
    return 0;
  for (const char **p = ALLOWED_TOPICS; *p; ++p)
    if (matches_pattern (topic, *p))
      return 1;
  return 0;
}


/* ===================== broadcast ===================== */

void
comm_broadcast_message (comm_scope_t scope, long long scope_id,
			const char *message, json_t *extra)
{
  if (!message || !*message)
    {
      if (extra)
	json_decref (extra);
      return;
    }

  char topic[64] = { 0 };
  switch (scope)
    {
    case COMM_SCOPE_GLOBAL:
      snprintf (topic, sizeof (topic), "broadcast.global");
      break;
    case COMM_SCOPE_SECTOR:
      snprintf (topic, sizeof (topic), "sector.%lld", scope_id);
      break;
    case COMM_SCOPE_CORP:
      snprintf (topic, sizeof (topic), "corp.%lld", scope_id);
      break;
    case COMM_SCOPE_PLAYER:
      snprintf (topic, sizeof (topic), "player.%lld", scope_id);
      break;
    default:
      if (extra)
	json_decref (extra);
      return;
    }

  /* Prebuild common data object; clone per recipient if needed */
  json_t *base = json_pack ("{s:s, s:s, s:I}",
			    "message", message,
			    "scope", (scope == COMM_SCOPE_GLOBAL) ? "global" :
			    (scope == COMM_SCOPE_SECTOR) ? "sector" :
			    (scope == COMM_SCOPE_CORP) ? "corp" : "player",
			    "scope_id", (json_int_t) scope_id);
  if (!base)
    {
      if (extra)
	json_decref (extra);
      return;
    }
  if (extra)
    {
      /* merge: extra wins on key collisions */
      const char *k;
      json_t *v;
      void *it = json_object_iter (extra);
      while (it)
	{
	  k = json_object_iter_key (it);
	  v = json_object_iter_value (it);
	  json_object_set (base, k, v);
	  it = json_object_iter_next (extra, it);
	}
      json_decref (extra);
    }

  /* Fan-out to subscribers of the computed topic */
  for (sub_map_t * m = g_submaps; m; m = m->next)
    {
      int deliver = 0;
      for (sub_node_t * n = m->head; n; n = n->next)
	{
	  if (matches_pattern (topic, n->topic))
	    {
	      deliver = 1;
	      break;
	    }
	}
      if (!deliver)
	continue;

      json_t *env = json_object ();
      if (!env)
	continue;

      json_t *data = json_deep_copy (base);
      json_object_set_new (env, "status", json_string ("ok"));
      json_object_set_new (env, "type", json_string ("broadcast.message_v1"));
      json_object_set_new (env, "data", data);

      json_t *meta = json_object ();
      if (meta)
	{
	  json_object_set_new (meta, "topic", json_string (topic));
	  json_object_set_new (env, "meta", meta);
	}

      attach_rate_limit_meta (env, m->ctx);
      rl_tick (m->ctx);
      send_all_json (m->ctx->fd, env);
      json_decref (env);
    }

  json_decref (base);
}

/* ---------- sector event publisher ---------- */

void
comm_publish_sector_event (int sector_id, const char *event_name,
			   json_t *data)
{
  if (sector_id <= 0 || !event_name || !*event_name)
    {
      if (data)
	json_decref (data);
      return;
    }

  /* Build concrete topic once (e.g., "sector.42") */
  char topic[64];
  snprintf (topic, sizeof (topic), "sector.%d", sector_id);

  /* Walk all connections; deliver to any that subscribed to sector.* or sector.{id} */
  for (sub_map_t * m = g_submaps; m; m = m->next)
    {
      int deliver = 0;
      for (sub_node_t * n = m->head; n; n = n->next)
	{
	  /* pattern is subscriber’s topic; subject is our concrete topic */
	  if (matches_pattern (topic, n->topic))
	    {
	      deliver = 1;
	      break;
	    }
	}
      if (!deliver)
	continue;

      /* Envelope: event + data */
      json_t *env = json_object ();
      if (!env)
	continue;

      /* NOTE: we move 'data' into the envelope; if multiple recipients, clone */
      json_t *payload = data;
      if (m->next)		/* more recipients ahead? clone so each gets its own */
	payload = data ? json_deep_copy (data) : json_object ();

      json_object_set_new (env, "id", json_string ("evt"));	/* simple id */
      // json_object_set_new(env, "ts", json_string(current_rfc3339()));  
      json_object_set_new (env, "event", json_string (event_name));
      json_object_set_new (env, "data", payload);

      /* Optional: include topic for client-side routing convenience */
      json_t *meta = json_object ();
      if (meta)
	{
	  json_object_set_new (meta, "topic", json_string (topic));
	  json_object_set_new (env, "meta", meta);
	}

      /* Your usual send path + rate-limit hints, if desired */
      attach_rate_limit_meta (env, m->ctx);
      rl_tick (m->ctx);
      send_all_json (m->ctx->fd, env);
      json_decref (env);
    }

  /* Original 'data' consumed (moved or deep-copied), drop our ref */
  if (data)
    json_decref (data);
}



/* Mandatory broadcast (ignores subscriptions) */
static void
broadcast_system_notice (int notice_id,
			 const char *title,
			 const char *body,
			 const char *severity,
			 time_t created_at, time_t expires_at)
{
  json_t *data = json_pack ("{s:i, s:s, s:s, s:s, s:i, s:i}",
			    "id", notice_id,
			    "title", title ? title : "",
			    "body", body ? body : "",
			    "severity", severity ? severity : "info",
			    "created_at", (int) created_at,
			    "expires_at", (int) expires_at);

  if (!data)
    return;

  for (sub_map_t * m = g_submaps; m; m = m->next)
    {
      json_t *env = json_object ();
      if (!env)
	continue;

      json_object_set_new (env, "status", json_string ("ok"));
      json_object_set_new (env, "type", json_string ("system.notice_v1"));
      json_object_set (env, "data", data);	/* shared; incref below */

      json_t *meta = json_object ();
      if (meta)
	{
	  json_object_set_new (meta, "topic", json_string ("system.notice"));
	  json_object_set_new (meta, "mandatory", json_true ());
	  json_object_set_new (meta, "persistent", json_true ());
	  json_object_set_new (env, "meta", meta);
	}

      attach_rate_limit_meta (env, m->ctx);
      rl_tick (m->ctx);

      json_incref (data);
      send_all_json (m->ctx->fd, env);
      json_decref (env);
    }

  json_decref (data);
}


void
push_unseen_notices_for_player (client_ctx_t *ctx, int player_id)
{
  json_t *arr = db_notice_list_unseen_for_player (player_id);
  if (!arr)
    return;

  size_t i, n = json_array_size (arr);
  for (i = 0; i < n; i++)
    {
      json_t *it = json_array_get (arr, i);
      if (!it || !json_is_object (it))
	continue;

      int id = (int) json_integer_value (json_object_get (it, "id"));
      const char *ttl = json_string_value (json_object_get (it, "title"));
      const char *bod = json_string_value (json_object_get (it, "body"));
      const char *sev = json_string_value (json_object_get (it, "severity"));
      int created =
	(int) json_integer_value (json_object_get (it, "created_at"));
      int expires =
	(int) json_integer_value (json_object_get (it, "expires_at"));

      /* Send a single envelope to this ctx only */
      json_t *env = json_object ();
      if (!env)
	continue;

      json_object_set_new (env, "status", json_string ("ok"));
      json_object_set_new (env, "type", json_string ("system.notice_v1"));

      json_t *data = json_pack ("{s:i, s:s, s:s, s:s, s:i, s:i}",
				"id", id,
				"title", ttl ? ttl : "",
				"body", bod ? bod : "",
				"severity", sev ? sev : "info",
				"created_at", created,
				"expires_at", expires);
      if (data)
	json_object_set_new (env, "data", data);

      json_t *meta = json_object ();
      if (meta)
	{
	  json_object_set_new (meta, "topic", json_string ("system.notice"));
	  json_object_set_new (meta, "mandatory", json_true ());
	  json_object_set_new (meta, "persistent", json_true ());
	  json_object_set_new (env, "meta", meta);
	}

      attach_rate_limit_meta (env, ctx);
      rl_tick (ctx);
      send_all_json (ctx->fd, env);
      json_decref (env);

      /* Optional: auto-mark as seen on delivery, or do it when client calls notice.dismiss */
      /* db_notice_mark_seen(id, player_id); */
    }

  json_decref (arr);
}


/* { cmd:"admin.notice.create", data:{ title, body, severity?, expires_at? } } */
int
cmd_admin_notice_create (client_ctx_t *ctx, json_t *root)
{
  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 1400, "Bad request");
      return 1;
    }

  const char *title = json_string_value (json_object_get (data, "title"));
  const char *body = json_string_value (json_object_get (data, "body"));
  const char *sev = json_string_value (json_object_get (data, "severity"));
  int expires_at =
    (int) json_integer_value (json_object_get (data, "expires_at"));

  if (!title || !body)
    {
      send_enveloped_error (ctx->fd, root, 1400, "Missing title/body");
      return 1;
    }

  int id =
    db_notice_create (title, body, sev ? sev : "info", (time_t) expires_at);
  if (id < 0)
    {
      send_enveloped_error (ctx->fd, root, 1500, "DB error");
      return 1;
    }

  /* Broadcast to all online sessions */
  time_t now = time (NULL);
  broadcast_system_notice (id, title, body, sev ? sev : "info", now,
			   (time_t) expires_at);

  json_t *ok = json_pack ("{s:i}", "notice_id", id);
  send_enveloped_ok (ctx->fd, root, "admin.notice.created_v1", ok);
  json_decref (ok);
  return 0;
}


/* { cmd:"notice.dismiss", data:{ notice_id } } */
int
cmd_notice_dismiss (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1101, "Auth required", NULL);
      return 1;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, 1400, "Bad request");
      return 1;
    }

  int notice_id =
    (int) json_integer_value (json_object_get (data, "notice_id"));
  if (notice_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 1400, "Missing notice_id");
      return 1;
    }

  if (db_notice_mark_seen (notice_id, ctx->player_id) != 0)
    {
      send_enveloped_error (ctx->fd, root, 1500, "DB error");
      return 1;
    }

  json_t *ok = json_pack ("{s:i}", "notice_id", notice_id);
  send_enveloped_ok (ctx->fd, root, "notice.dismissed_v1", ok);
  json_decref (ok);
  return 0;
}


////////////////////////////////////////////////////////////////

/* ---- shared helpers (module-local) ---- */
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
  (void) which;			// keep for future logging if desired
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented");
  return 0;
}

/* ===================== chat.* ===================== */

int
cmd_chat_send (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {channel?, text}, persist/deliver
  return niy (ctx, root, "chat.send");
}

int
cmd_chat_broadcast (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: admin/mod ACL, broadcast message
  return niy (ctx, root, "chat.broadcast");
}

int
cmd_chat_history (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {channel?, before?, limit?}, return messages
  return niy (ctx, root, "chat.history");
}

/* ===================== mail.* ===================== */

int
cmd_mail_send (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {to, subject?, body}, enqueue/persist
  return niy (ctx, root, "mail.send");
}

int
cmd_mail_inbox (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: list inbox for ctx->player_id
  return niy (ctx, root, "mail.inbox");
}

int
cmd_mail_read (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {mail_id}, return message payload
  return niy (ctx, root, "mail.read");
}

int
cmd_mail_delete (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {mail_id}, delete/mark
  return niy (ctx, root, "mail.delete");
}

/* ================== subscribe.* =================== */




static int
sub_contains (sub_node_t *head, const char *topic)
{
  for (sub_node_t * n = head; n; n = n->next)
    if (strcmp (n->topic, topic) == 0)
      return 1;
  return 0;
}

static int
sub_add (sub_map_t *m, const char *topic)
{
  if (sub_contains (m->head, topic))
    return 0;			/* already present -> idempotent add */
  sub_node_t *n = (sub_node_t *) calloc (1, sizeof *n);
  if (!n)
    return -1;
  n->topic = strdup (topic);
  if (!n->topic)
    {
      free (n);
      return -1;
    }
  n->next = m->head;
  m->head = n;
  return 1;			/* added */
}

static int
sub_remove (sub_map_t *m, const char *topic)
{
  sub_node_t **pp = &m->head;
  for (; *pp; pp = &(*pp)->next)
    {
      if (strcmp ((*pp)->topic, topic) == 0)
	{
	  sub_node_t *dead = *pp;
	  *pp = dead->next;
	  free (dead->topic);
	  free (dead);
	  return 1;		/* removed */
	}
    }
  return 0;			/* not found */
}

/* ---- topic validation ----
   Accept either exact catalogue items or simple suffix wildcard "prefix.*".
   Adjust ALLOWED list as you add more feeds. */
/* Add alongside your existing ALLOWED_TOPICS */
static const char *ALLOWED_TOPICS[] = {
  "system.notice",
  "system.events",
  "broadcast.global",		/* NEW: global ephemeral broadcasts */
  "sector.*",
  "corp.*",			/* NEW: corporation-targeted */
  "player.*",			/* NEW: player-targeted */
  "chat.global",
  "corp.mail",
  "corp.log",
  NULL
};



/* ---- public cleanup hook ---- */
void
comm_clear_subscriptions (client_ctx_t *ctx)
{
  sub_map_t **pp = &g_submaps;
  for (; *pp; pp = &(*pp)->next)
    {
      if ((*pp)->ctx == ctx)
	{
	  sub_map_t *dead = *pp;
	  *pp = dead->next;
	  sub_node_t *n = dead->head;
	  while (n)
	    {
	      sub_node_t *next = n->next;
	      free (n->topic);
	      free (n);
	      n = next;
	    }
	  free (dead);
	  return;
	}
    }
}

/* ---- command handlers ---- */

int
cmd_subscribe_add (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;

  json_t *data = json_object_get (root, "data");
  const char *topic = data && json_is_object (data)
    ? json_string_value (json_object_get (data, "topic")) : NULL;

  if (!topic || !*topic)
    {
      send_enveloped_refused (ctx->fd, root, 1402, "Missing 'topic'", NULL);
      return 0;
    }
  if (!is_valid_topic (topic))
    {
      /* catalogue code: invalid topic */
      send_enveloped_refused (ctx->fd, root, 1403, "Invalid topic", NULL);
      return 0;
    }

  sub_map_t *m = submap_get (ctx, 1);
  if (!m)
    {
      send_enveloped_error (ctx->fd, root, 1102, "Out of memory");
      return 0;
    }

  int rc = sub_add (m, topic);
  if (rc < 0)
    {
      send_enveloped_error (ctx->fd, root, 1102, "Out of memory");
      return 0;
    }

  json_t *resp = json_pack ("{s:s, s:b}", "topic", topic, "added",
			    rc > 0 ? 1 : 0);
  /* type token can be anything you standardise on; using subscribe.ack_v1 */
  send_enveloped_ok (ctx->fd, root, "subscribe.ack_v1", resp);
  json_decref (resp);
  return 0;
}

/* Guard: refuse removing a locked subscription */
static int
is_locked_subscription (sqlite3 *db, int player_id, const char *topic)
{
  static const char *SQL =
    "SELECT locked FROM subscriptions WHERE player_id=? AND event_type=? LIMIT 1";
  sqlite3_stmt *st = NULL;
  int locked = 0;
  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
    return 0;
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_bind_text (st, 2, topic, -1, SQLITE_STATIC);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      locked = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  return locked ? 1 : 0;
}



int
cmd_subscribe_remove (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;

  json_t *data = json_object_get (root, "data");
  const char *topic = data && json_is_object (data)
    ? json_string_value (json_object_get (data, "topic")) : NULL;

  if (!topic || !*topic)
    {
      send_enveloped_refused (ctx->fd, root, 1402, "Missing 'topic'", NULL);
      return 0;
    }

  sqlite3 *db = db_get_handle ();
  if (is_locked_subscription (db, ctx->player_id, topic))
    {
      send_enveloped_refused (ctx->fd, root, 1405, "Topic is locked", NULL);
      return 0;
    }

  sub_map_t *m = submap_get (ctx, 0);
  int removed = (m ? sub_remove (m, topic) : 0);

  json_t *resp =
    json_pack ("{s:s, s:b}", "topic", topic, "removed", removed ? 1 : 0);
  send_enveloped_ok (ctx->fd, root, "subscribe.ack_v1", resp);
  json_decref (resp);
  return 0;
}

int
cmd_subscribe_list (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;

  sub_map_t *m = submap_get (ctx, 0);

  json_t *arr = json_array ();
  if (m)
    {
      /* return in deterministic order (insertion order not guaranteed here);
         it's fine for now; sort server-side later if needed */
      for (sub_node_t * n = m->head; n; n = n->next)
	json_array_append_new (arr, json_string (n->topic));
    }

  json_t *resp = json_pack ("{s:o}", "topics", arr);
  send_enveloped_ok (ctx->fd, root, "subscribe.list_v1", resp);
  json_decref (resp);
  return 0;
}


/* ================== admin.* =================== */


static inline int
require_admin (client_ctx_t *ctx, json_t *root)
{
  // TODO: replace with real ACL; for now require auth at least
  if (ctx->player_id > 0)
    return 1;
  send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
  return 0;
}

int
cmd_admin_notice (client_ctx_t *ctx, json_t *root)
{
  if (!require_admin (ctx, root))
    return 0;
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented: admin.notice");
  return 0;
}

int
cmd_admin_shutdown_warning (client_ctx_t *ctx, json_t *root)
{
  if (!require_admin (ctx, root))
    return 0;
  send_enveloped_error (ctx->fd, root, 1101,
			"Not implemented: admin.shutdown_warning");
  return 0;
}


/* Optional: friendly descriptions without changing ALLOWED_TOPICS */
static const char *
topic_desc (const char *pattern)
{
  if (strcmp (pattern, "sector.*") == 0)
    return
      "All sector-scoped events; use sector.{id} pattern, e.g., sector.42";
  if (strcmp (pattern, "system.notice") == 0)
    return "Server notices & maintenance";
  if (strcmp (pattern, "system.events") == 0)
    return "Raw system events stream (structured)";
  if (strcmp (pattern, "corp.mail") == 0)
    return "Corporation mail (membership required)";
  if (strcmp (pattern, "corp.log") == 0)
    return "Corporation activity log (membership required)";
  if (strcmp (pattern, "chat.global") == 0)
    return "Global chat/subspace mirror";
  return "";
}

/* Build an example for wildcard topics (e.g., "sector.*" -> "sector.42") */
static json_t *
catalog_topics_json (void)
{
  json_t *arr = json_array ();
  if (!arr)
    return NULL;

  for (size_t i = 0; ALLOWED_TOPICS[i]; ++i)
    {
      const char *pat = ALLOWED_TOPICS[i];
      int is_wc = (strlen (pat) >= 2 && pat[strlen (pat) - 1] == '*'
		   && pat[strlen (pat) - 2] == '.');

      json_t *obj = json_object ();
      if (!obj)
	{
	  json_decref (arr);
	  return NULL;
	}

      json_object_set_new (obj, "pattern", json_string (pat));
      json_object_set_new (obj, "kind",
			   json_string (is_wc ? "wildcard" : "exact"));

      const char *desc = topic_desc (pat);
      if (desc && *desc)
	json_object_set_new (obj, "desc", json_string (desc));

      if (is_wc)
	{
	  /* make example: replace trailing ".*" with ".42" */
	  size_t n = strlen (pat);
	  char example[128];
	  if (n < sizeof (example))
	    {
	      memcpy (example, pat, n - 1);	// copy up to '*'
	      example[n - 1] = '4';
	      example[n] = '2';
	      example[n + 1] = '\0';
	      json_object_set_new (obj, "example", json_string (example));	// e.g., "sector.42"
	    }
	}

      json_array_append_new (arr, obj);
    }

  return arr;
}

/* subscribe.catalog → returns "subscribe.catalog_v1"
   data: { "topics": [ {pattern, kind, desc?, example?}, ... ] } */
int
cmd_subscribe_catalog (client_ctx_t *ctx, json_t *root)
{
  (void) root;

  json_t *topics = catalog_topics_json ();
  if (!topics)
    send_enveloped_error (ctx->fd, root, 1500, "Allocation failure");

  json_t *data = json_pack ("{s:O}", "topics", topics);
  if (!data)
    {
      json_decref (topics);
      send_enveloped_error (ctx->fd, root, 1500, "Allocation failure");
    }
  send_enveloped_ok (ctx->fd, root, "subscribe.catalog_v1", data);
  json_decref (data);
  return 0;
}
