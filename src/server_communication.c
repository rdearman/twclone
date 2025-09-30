#include "server_communication.h"
#include "server_envelope.h"	// send_enveloped_ok/error/refused
#include "errors.h"
#include "config.h"
#include <jansson.h>
#include "server_cmds.h"
#include <string.h>

// forward-declared somewhere in this module already:
static const char *ALLOWED_TOPICS[];

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


/* ---- per-connection store (module local) ---- */
typedef struct sub_node {
  char *topic;
  struct sub_node *next;
} sub_node_t;

typedef struct sub_map {
  client_ctx_t *ctx;
  sub_node_t *head;
  struct sub_map *next;
} sub_map_t;

static sub_map_t *g_submaps = NULL;

/* find or create the per-ctx map node */
static sub_map_t *
submap_get (client_ctx_t *ctx, int create_if_missing)
{
  for (sub_map_t *m = g_submaps; m; m = m->next)
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
sub_contains (sub_node_t *head, const char *topic)
{
  for (sub_node_t *n = head; n; n = n->next)
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
static const char *ALLOWED_TOPICS[] = {
  "sector.*",          /* any sector-scoped feed, e.g., sector.42, sector.17 */
  "system.notice",     /* generic system notifications */
  "system.events",     /* raw system_events stream */
  "corp.mail",         /* corp mail stream (server enforces membership) */
  "corp.log",          /* corp activity log */
  "chat.global",       /* global chat/subspace mirror */
  NULL
};

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
    ? json_string_value (json_object_get (data, "topic"))
    : NULL;

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

int
cmd_subscribe_remove (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;

  json_t *data = json_object_get (root, "data");
  const char *topic = data && json_is_object (data)
    ? json_string_value (json_object_get (data, "topic"))
    : NULL;

  if (!topic || !*topic)
    {
      send_enveloped_refused (ctx->fd, root, 1402, "Missing 'topic'", NULL);
      return 0;
    }

  sub_map_t *m = submap_get (ctx, 0);
  int removed = (m ? sub_remove (m, topic) : 0);

  json_t *resp = json_pack ("{s:s, s:b}", "topic", topic, "removed", removed ? 1 : 0);
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
      for (sub_node_t *n = m->head; n; n = n->next)
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
  if (strcmp(pattern, "sector.*") == 0)      return "All sector-scoped events; use sector.{id} pattern, e.g., sector.42";
  if (strcmp(pattern, "system.notice") == 0) return "Server notices & maintenance";
  if (strcmp(pattern, "system.events") == 0) return "Raw system events stream (structured)";
  if (strcmp(pattern, "corp.mail") == 0)     return "Corporation mail (membership required)";
  if (strcmp(pattern, "corp.log") == 0)      return "Corporation activity log (membership required)";
  if (strcmp(pattern, "chat.global") == 0)   return "Global chat/subspace mirror";
  return "";
}

/* Build an example for wildcard topics (e.g., "sector.*" -> "sector.42") */
static json_t *
catalog_topics_json (void)
{
  json_t *arr = json_array();
  if (!arr) return NULL;

  for (size_t i = 0; ALLOWED_TOPICS[i]; ++i)
    {
      const char *pat = ALLOWED_TOPICS[i];
      int is_wc = (strlen(pat) >= 2 && pat[strlen(pat)-1] == '*' && pat[strlen(pat)-2] == '.');

      json_t *obj = json_object();
      if (!obj) { json_decref(arr); return NULL; }

      json_object_set_new(obj, "pattern", json_string(pat));
      json_object_set_new(obj, "kind",    json_string(is_wc ? "wildcard" : "exact"));

      const char *desc = topic_desc(pat);
      if (desc && *desc) json_object_set_new(obj, "desc", json_string(desc));

      if (is_wc)
        {
          /* make example: replace trailing ".*" with ".42" */
          size_t n = strlen(pat);
          char example[128];
          if (n < sizeof(example))
            {
              memcpy(example, pat, n - 1);    // copy up to '*'
              example[n - 1] = '4';
              example[n]     = '2';
              example[n + 1] = '\0';
              json_object_set_new(obj, "example", json_string(example)); // e.g., "sector.42"
            }
        }

      json_array_append_new(arr, obj);
    }

  return arr;
}

/* subscribe.catalog → returns "subscribe.catalog_v1"
   data: { "topics": [ {pattern, kind, desc?, example?}, ... ] } */
int
cmd_subscribe_catalog (client_ctx_t *ctx, json_t *root)
{
  (void)root;

  json_t *topics = catalog_topics_json();
  if (!topics)  send_enveloped_error(ctx->fd, root, 1500, "Allocation failure");

  json_t *data = json_pack("{s:O}", "topics", topics);
  if (!data) { json_decref(topics);  send_enveloped_error(ctx->fd,root, 1500, "Allocation failure"); }
  send_enveloped_ok(ctx->fd, root, "subscribe.catalog_v1", data);
  json_decref(data);
  return 0;
}
