#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <time.h>
// local includes
#include "server_communication.h"
#include "server_envelope.h"    // send_enveloped_ok/error/refused
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "database.h"
#include "server_loop.h"
#include "db_player_settings.h"
#include "server_envelope.h"
#include "server_log.h"
enum
{ MAX_SUBSCRIPTIONS_PER_PLAYER = 64 };

/* ---- topic validation ----
   Accept either exact catalogue items or simple suffix wildcard "prefix.*".
   Adjust ALLOWED list as you add more feeds. */
/* Add alongside your existing ALLOWED_TOPICS */
static const char *ALLOWED_TOPICS[] = {
  "system.notice",
  "system.events",
  "broadcast.global",           /* NEW: global ephemeral broadcasts */
  "sector.*",
  "corp.*",                     /* NEW: corporation-targeted */
  "player.*",                   /* NEW: player-targeted */
  "chat.global",
  "corp.mail",
  "corp.log",
  "iss.move",
  "iss.warp",
  "npc.*",                      /* generic NPC surface: move/warp/spawn/attack/destroy */
  "nav.*",                      /* nav events per contract */
  "chat.*",                     /* show full chat namespace in catalog */
  "engine.tick",                /* exact event in contract */
  "sector.notice",              /* exact sector-scoped shout */
  NULL
};


/* Accept only explicit public topics + selected wildcard domains (exclude npc.*) */
static int
is_allowed_topic (const char *t)
{
  static const char *const allowed[] = {
    "system.notice",
    "sector.*",
    "sector.player_entered",
    "chat.sector",
    "chat.global",
    "combat.*",
    "trade.*",
    "iss.move",
    "iss.warp",
    "engine.tick",
    "sector.notice",
    NULL
  };
  if (!t)
    {
      return 0;
    }
  for (int i = 0; allowed[i]; ++i)
    {
      if (strcasecmp (t, allowed[i]) == 0)
        {
          return 1;
        }
    }
  const char *dot = strchr (t, '.');


  if (!dot || strcasecmp (dot + 1, "*") != 0)
    {
      return 0;
    }
  static const char *const public_domains[] = {
    "system", "sector", "chat", "combat", "trade", "nav", "iss", "corp",
    "player", NULL
  };
  size_t n = (size_t) (dot - t);
  char dom[32];


  if (n >= sizeof dom)
    {
      return 0;
    }
  memcpy (dom, t, n);
  dom[n] = '\0';
  for (int i = 0; public_domains[i]; ++i)
    {
      if (strcasecmp (dom, public_domains[i]) == 0)
        {
          return 1;
        }
    }
  return 0;
}


extern void attach_rate_limit_meta (json_t *env, client_ctx_t *ctx);
extern void rl_tick (client_ctx_t *ctx);
extern void send_all_json (int fd, json_t *obj);
extern json_t *db_notice_list_unseen_for_player (int player_id);
extern int db_notice_mark_seen (int notice_id, int player_id);
static void broadcast_system_notice (int notice_id,
                                     const char *title,
                                     const char *body,
                                     const char *severity,
                                     time_t created_at, time_t expires_at);


/* ---------------- sys.notice.create ----------------
 * Admin/SysOp-only. Create a persistent notice and push to online users.
 * JSON:
 *   { "title": str, "body": str, "severity": "info|warn|error",
 *     "expires_at": int|null }   // unix seconds (UTC) or null for open-ended
 */
int
cmd_sys_notice_create (client_ctx_t *ctx, json_t *root)
{
  /* TODO: gate with permissions (SysOp) if you have a helper like ensure_is_sysop(ctx) */
  json_t *data = json_object_get (root, "data");
  const char *title = json_string_value (json_object_get (data, "title"));
  const char *body = json_string_value (json_object_get (data, "body"));
  const char *sev = json_string_value (json_object_get (data, "severity"));
  json_t *exp = json_object_get (data, "expires_at");
  if (!title || !body)
    {
      send_enveloped_error (ctx->fd, root, 400,
                            "title and body are required");
      return 0;
    }
  if (!sev
      || (strcasecmp (sev, "info") && strcasecmp (sev, "warn")
          && strcasecmp (sev, "error")))
    {
      sev = "info";
    }
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  int rc, notice_id = 0;
  time_t now = time (NULL);
  // Calculate 24 hours from 'now' as the default
  int active_until_time = now + (24 * 60 * 60);


  /* INSERT into system_notice (id autogen or explicit), per your schema */
  /* system_notice(id, created_at, title, body, severity, expires_at) */
  /* created_at/expires_at use unix seconds. *//* :contentReference[oaicite:4]{index=4} */
  rc = sqlite3_prepare_v2 (db,
                           "INSERT INTO system_notice (created_at, title, body, severity, expires_at) "
                           "VALUES (?1, ?2, ?3, ?4, ?5);",
                           -1,
                           &st,
                           NULL);
  if (rc != SQLITE_OK)
    {
      goto sql_err;
    }
  sqlite3_bind_int64 (st, 1, (sqlite3_int64) now);
  sqlite3_bind_text (st, 2, title, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 3, body, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 4, sev, -1, SQLITE_TRANSIENT);
  if (exp && json_is_integer (exp))
    {
      sqlite3_bind_int64 (st, 5, (sqlite3_int64) json_integer_value (exp));
    }
  else
    {
      sqlite3_bind_null (st, 5);
    }
  rc = sqlite3_step (st);
  if (rc != SQLITE_DONE)
    {
      goto sql_err;
    }
  sqlite3_finalize (st);
  st = NULL;
  notice_id = (int) sqlite3_last_insert_rowid (db);
  /* Immediate push to online users (your helper composes the "system.notice") */
  broadcast_system_notice (notice_id, title, body, sev, now,
                           active_until_time);
  /* Build reply */
  json_t *resp = json_pack ("{s:i, s:s, s:s, s:s}",
                            "id", notice_id, "title", title, "body", body,
                            "severity", sev);


  /* echo expires_at if present */
  if (exp && json_is_integer (exp))
    {
      json_object_set (resp, "expires_at", exp);
    }
  send_enveloped_ok (ctx->fd, root, "announce.created", resp);
  return 0;
sql_err:
  if (st)
    {
      sqlite3_finalize (st);
    }
  LOGE ("sys.notice.create SQL error: %s", sqlite3_errmsg (db));
  send_enveloped_error (ctx->fd, root, 500, "db error");
  return 1;
}


/* -------------- notice.list ----------------
 * Return active notices and player seen state.
 * Optional input: { "include_expired": bool, "limit": int }
 */
int
cmd_notice_list (client_ctx_t *ctx, json_t *root)
{
  json_t *data = json_object_get (root, "data");
  int include_expired =
    json_is_true (json_object_get (data, "include_expired"));
  int limit = 100;
  if (data)
    {
      json_t *jlim = json_object_get (data, "limit");


      if (jlim && json_is_integer (jlim))
        {
          limit = (int) json_integer_value (jlim);
        }
    }
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  /* Active = expires_at IS NULL OR expires_at > now */
  /* Join with notice_seen to surface seen_at for this player. *//* :contentReference[oaicite:5]{index=5} */
  const char *SQL =
    "SELECT n.id, n.title, n.body, n.severity, n.created_at, n.expires_at, s.seen_at "
    "FROM system_notice n "
    "LEFT JOIN notice_seen s ON s.notice_id = n.id AND s.player_id = ?1 "
    "WHERE (?2 OR n.expires_at IS NULL OR n.expires_at > strftime('%s','now')) "
    "ORDER BY n.created_at DESC " "LIMIT ?3;";


  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
    {
      if (st)
        {
          sqlite3_finalize (st);
        }
      send_enveloped_error (ctx->fd, root, 500, "db error");
      return 0;
    }
  sqlite3_bind_int64 (st, 1, (sqlite3_int64) ctx->player_id);
  sqlite3_bind_int (st, 2, include_expired ? 1 : 0);
  sqlite3_bind_int (st, 3, limit);
  json_t *items = json_array ();


  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int id = sqlite3_column_int (st, 0);
      const unsigned char *t = sqlite3_column_text (st, 1);
      const unsigned char *b = sqlite3_column_text (st, 2);
      const unsigned char *sv = sqlite3_column_text (st, 3);
      sqlite3_int64 created = sqlite3_column_int64 (st, 4);
      sqlite3_int64 expires = sqlite3_column_type (st,
                                                   5) ==
                              SQLITE_NULL ? 0 : sqlite3_column_int64 (st,
                                                                      5);
      sqlite3_int64 seen_at = sqlite3_column_type (st,
                                                   6) ==
                              SQLITE_NULL ? 0 : sqlite3_column_int64 (st,
                                                                      6);
      json_t *row = json_pack ("{s:i, s:s, s:s, s:s, s:I}",
                               "id", id,
                               "title", t ? (const char *) t : "",
                               "body", b ? (const char *) b : "",
                               "severity", sv ? (const char *) sv : "info",
                               "created_at", (json_int_t) created);


      if (expires > 0)
        {
          json_object_set_new (row, "expires_at", json_integer (expires));
        }
      if (seen_at > 0)
        {
          json_object_set_new (row, "seen_at", json_integer (seen_at));
        }
      json_array_append_new (items, row);
    }
  sqlite3_finalize (st);
  json_t *resp = json_pack ("{s:o}", "items", items);


  send_enveloped_ok (ctx->fd, root, "notice.list_v1", resp);
  return 0;
}


/* -------------- notice.ack ----------------
 * Mark a notice as acknowledged (or simply 'seen') for this player.
 * JSON: { "id": int }
 */
int
cmd_notice_ack (client_ctx_t *ctx, json_t *root)
{
  json_t *data = json_object_get (root, "data");
  int id = (int) json_integer_value (json_object_get (data, "id"));
  if (id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "id required");
      return 1;
    }
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  time_t now = time (NULL);


  /* INSERT OR REPLACE into notice_seen (notice_id, player_id, seen_at) *//* :contentReference[oaicite:6]{index=6} */
  if (sqlite3_prepare_v2 (db,
                          "INSERT OR REPLACE INTO notice_seen (notice_id, player_id, seen_at) "
                          "VALUES (?1, ?2, ?3);",
                          -1,
                          &st,
                          NULL) != SQLITE_OK)
    {
      if (st)
        {
          sqlite3_finalize (st);
        }
      send_enveloped_error (ctx->fd, root, 500, "db error");
      return 1;
    }
  sqlite3_bind_int (st, 1, id);
  sqlite3_bind_int64 (st, 2, (sqlite3_int64) ctx->player_id);
  sqlite3_bind_int64 (st, 3, (sqlite3_int64) now);
  int rc = sqlite3_step (st);


  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      send_enveloped_error (ctx->fd, root, 500, "db error");
      return 1;
    }
  /* Optional: return the updated row */
  json_t *resp =
    json_pack ("{s:i, s:I}", "id", id, "seen_at", (json_int_t) now);


  send_enveloped_ok (ctx->fd, root, "notice.acknowledged", resp);
  return 0;
}


/* tiny local helpers (no new headers) */
static int
is_ascii_printable (const char *s)
{
  if (!s)
    {
      return 0;
    }
  for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
      if (*p < 0x20 || *p > 0x7E)
        {
          return 0;
        }
    }
  return 1;
}


static int
len_leq (const char *s, size_t m)
{
  return s && strlen (s) <= m;
}


static int
send_to_player (int player_id, const char *event_type, json_t *data)
{
  // delegate to server_loop’s registry-based delivery
  return server_deliver_to_player (player_id, event_type, data);
}


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


/* /\* find or create the per-ctx map node *\/ */


/* static sub_map_t * */


/* submap_get (client_ctx_t *ctx, int create_if_missing) */


/* { */


/*   for (sub_map_t * m = g_submaps; m; m = m->next) */


/*     if (m->ctx == ctx) */


/*       return m; */


/*   if (!create_if_missing) */


/*     return NULL; */


/*   sub_map_t *m = (sub_map_t *) calloc (1, sizeof *m); */


/*   if (!m) */


/*     return NULL; */


/*   m->ctx = ctx; */


/*   m->head = NULL; */


/*   m->next = g_submaps; */


/*   g_submaps = m; */


/*   return m; */


/* } */
static int
matches_pattern (const char *topic, const char *pattern)
{
  const char *star = strchr (pattern, '*');
  if (!star)
    {
      return strcasecmp (topic, pattern) == 0;  /* exact */
    }
  /* suffix wildcard: "prefix.*" */
  size_t plen = (size_t) (star - pattern);


  return strncmp (topic, pattern, plen) == 0;
}


/* static int */
/* is_valid_topic (const char *topic) */
/* { */
/*   if (!topic || !*topic) */
/*     return 0; */
/*   for (const char **p = ALLOWED_TOPICS; *p; ++p) */
/*     if (matches_pattern (topic, *p)) */
/*       return 1; */
/*   return 0; */
/* } */
struct bc_ctx
{
  const char *event_type;
  json_t *data;
  int deliveries;
};


static int
bc_cb (int player_id, void *arg)
{
  struct bc_ctx *bc = (struct bc_ctx *) arg;
  if (send_to_player (player_id, bc->event_type, json_incref (bc->data)) == 0)
    {
      bc->deliveries++;
    }
  return 0;                     // continue
}


int
server_broadcast_event (const char *event_type, json_t *data)
{
  if (!event_type || !data)
    {
      return -1;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return -1;
    }
  struct bc_ctx bc = {.event_type = event_type,.data = data,.deliveries = 0 };
  int rc = db_for_each_subscriber (db, event_type, bc_cb, &bc);


  // You can log bc.deliveries if you want metrics later (#197)
  return (rc == 0) ? bc.deliveries : rc;
}


// Broadcast an event (type + payload) to all currently-connected players
// in a specific sector. Payload is borrowed (not stolen).
int
server_broadcast_to_sector (int sector_id, const char *event_name,
                            json_t *payload)
{
  if (!event_name || !payload || sector_id <= 0)
    {
      return -1;
    }
  // Create a copy of the payload for each recipient as comm_publish_sector_event consumes the reference
  json_t *payload_copy = json_deep_copy (payload);


  if (!payload_copy)
    {
      LOGE
      (
        "server_broadcast_to_sector: Failed to deep copy payload for sector %d.",
        sector_id);
      return -1;
    }
  // This function acts as a wrapper to comm_publish_sector_event, ensuring sector-specific filtering
  comm_publish_sector_event (sector_id, event_name, payload_copy);
  return 0;                     // Success
}


/* ===================== broadcast ===================== */
void
comm_broadcast_message (comm_scope_t scope, long long scope_id,
                        const char *message, json_t *extra)
{
  if (!message || !*message)
    {
      if (extra)
        {
          json_decref (extra);
        }
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
          {
            json_decref (extra);
          }
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
        {
          json_decref (extra);
        }
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
  for (sub_map_t *m = g_submaps; m; m = m->next)
    {
      int deliver = 0;


      for (sub_node_t *n = m->head; n; n = n->next)
        {
          if (matches_pattern (topic, n->topic))
            {
              deliver = 1;
              break;
            }
        }
      if (!deliver)
        {
          continue;
        }
      json_t *env = json_object ();


      if (!env)
        {
          continue;
        }
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
        {
          json_decref (data);
        }
      return;
    }
  /* Build concrete topic once (e.g., "sector.42") */
  char topic[64];


  snprintf (topic, sizeof (topic), "sector.%d", sector_id);
  /* Walk all connections; deliver to any that subscribed to sector.* or sector.{id} */
  for (sub_map_t *m = g_submaps; m; m = m->next)
    {
      int deliver = 0;


      for (sub_node_t *n = m->head; n; n = n->next)
        {
          /* pattern is subscriber’s topic; subject is our concrete topic */
          if (matches_pattern (topic, n->topic))
            {
              deliver = 1;
              break;
            }
        }
      if (!deliver)
        {
          continue;
        }
      /* Envelope: event + data */
      json_t *env = json_object ();


      if (!env)
        {
          continue;
        }
      /* NOTE: we move 'data' into the envelope; if multiple recipients, clone */
      json_t *payload = data;


      if (m->next)              /* more recipients ahead? clone so each gets its own */
        {
          payload = data ? json_deep_copy (data) : json_object ();
        }
      json_object_set_new (env, "id", json_string ("evt"));     /* simple id */
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
    {
      json_decref (data);
    }
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
    {
      return;
    }
  for (sub_map_t *m = g_submaps; m; m = m->next)
    {
      json_t *env = json_object ();


      if (!env)
        {
          continue;
        }
      json_object_set_new (env, "status", json_string ("ok"));
      json_object_set_new (env, "type", json_string ("system.notice_v1"));
      json_object_set (env, "data", data);      /* shared; incref below */
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
      // json_incref (data); // Removed to fix leak
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
    {
      return;
    }
  size_t i, n = json_array_size (arr);


  for (i = 0; i < n; i++)
    {
      json_t *it = json_array_get (arr, i);


      if (!it || !json_is_object (it))
        {
          continue;
        }
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
        {
          continue;
        }
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
        {
          json_object_set_new (env, "data", data);
        }
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
    {
      return 1;
    }
  send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
  return 0;
}


static inline int
niy (client_ctx_t *ctx, json_t *root, const char *which)
{
  (void) which;                 // keep for future logging if desired
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented");
  return 0;
}


/* ===================== chat.* ===================== */
int
cmd_chat_send (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  // TODO: parse {channel?, text}, persist/deliver
  return niy (ctx, root, "chat.send");
}


int
cmd_chat_broadcast (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  // TODO: admin/mod ACL, broadcast message
  return niy (ctx, root, "chat.broadcast");
}


int
cmd_chat_history (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  // TODO: parse {channel?, before?, limit?}, return messages
  return niy (ctx, root, "chat.history");
}


/* /\* ===================== mail.* ===================== *\/ */
int
cmd_mail_send (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_enveloped_error (ctx->fd, root, 1300, "Invalid request schema");
      return 0;
    }                           /* 1300 */
  /* Parse inputs */
  const char *to_name = NULL, *subject = NULL, *body = NULL, *idem = NULL;
  int to_id = 0;
  json_t *j_to_id = json_object_get (data, "to_id");


  if (j_to_id && json_is_integer (j_to_id))
    {
      to_id = (int) json_integer_value (j_to_id);
    }
  json_t *j_to = json_object_get (data, "to");


  if (j_to && json_is_string (j_to))
    {
      to_name = json_string_value (j_to);
    }
  json_t *j_subject = json_object_get (data, "subject");


  if (j_subject && json_is_string (j_subject))
    {
      subject = json_string_value (j_subject);
    }
  json_t *j_body = json_object_get (data, "body");


  if (j_body && json_is_string (j_body))
    {
      body = json_string_value (j_body);
    }
  json_t *j_idem = json_object_get (data, "idempotency_key");


  if (j_idem && json_is_string (j_idem))
    {
      idem = json_string_value (j_idem);
    }
  /* Basic validation */
  if ((!to_name && to_id <= 0) || !body)
    {
      send_enveloped_error (ctx->fd,
                            root,
                            1301,
                            "Missing required field: to/to_id and body");                       /* 1301 */
      return 0;
    }
  /* Resolve recipient by name if needed (players.name exists) */
  if (to_id <= 0 && to_name)
    {
      sqlite3_stmt *st = NULL;


      if (sqlite3_prepare_v2
            (db,
            "SELECT id FROM players WHERE name = ?1 COLLATE NOCASE LIMIT 1;",
            -1, &st, NULL) != SQLITE_OK)
        {
          if (st)
            {
              sqlite3_finalize (st);
            }
          send_enveloped_error (ctx->fd, root, 500, "db error");
          return 0;
        }
      sqlite3_bind_text (st, 1, to_name, -1, SQLITE_TRANSIENT);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          to_id = sqlite3_column_int (st, 0);
        }
      sqlite3_finalize (st);
      if (to_id <= 0)
        {
          send_enveloped_error (ctx->fd, root, 1900, "Recipient not found");
          return 0;
        }                       /* 1900 */
    }
  /* Check if recipient has blocked the sender (player_block) */
  {
    sqlite3_stmt *st = NULL;


    if (sqlite3_prepare_v2 (db,
                            "SELECT 1 FROM player_block WHERE blocker_id=?1 AND blocked_id=?2 LIMIT 1;",
                            -1,
                            &st,
                            NULL) != SQLITE_OK)
      {
        if (st)
          {
            sqlite3_finalize (st);
          }
        send_enveloped_error (ctx->fd, root, 500, "db error");
        return 0;
      }
    sqlite3_bind_int (st, 1, to_id);
    sqlite3_bind_int (st, 2, ctx->player_id);
    int blocked = (sqlite3_step (st) == SQLITE_ROW);


    sqlite3_finalize (st);
    if (blocked)
      {
        send_enveloped_error (ctx->fd, root, 1901, "Muted or blocked");
        return 0;
      }                         /* 1901 */
  }
  /* Idempotency: if idem_key+recipient exists, return existing id (mail has unique index) */
  int mail_id = 0;


  if (idem && *idem)
    {
      sqlite3_stmt *chk = NULL;


      if (sqlite3_prepare_v2 (db,
                              "SELECT id FROM mail WHERE idempotency_key=?1 AND recipient_id=?2 LIMIT 1;",
                              -1,
                              &chk,
                              NULL) == SQLITE_OK)
        {
          sqlite3_bind_text (chk, 1, idem, -1, SQLITE_TRANSIENT);
          sqlite3_bind_int (chk, 2, to_id);
          if (sqlite3_step (chk) == SQLITE_ROW)
            {
              mail_id = sqlite3_column_int (chk, 0);
            }
        }
      if (chk)
        {
          sqlite3_finalize (chk);
        }
    }
  /* Insert if not already present */
  if (mail_id == 0)
    {
      sqlite3_stmt *ins = NULL;


      if (sqlite3_prepare_v2 (db,
                              "INSERT INTO mail(sender_id, recipient_id, subject, body, idempotency_key) "
                              "VALUES(?1,?2,?3,?4,?5);",
                              -1,
                              &ins,
                              NULL) != SQLITE_OK)
        {
          if (ins)
            {
              sqlite3_finalize (ins);
            }
          send_enveloped_error (ctx->fd, root, 500, "db error");
          return 0;
        }
      sqlite3_bind_int (ins, 1, ctx->player_id);
      sqlite3_bind_int (ins, 2, to_id);
      if (subject)
        {
          sqlite3_bind_text (ins, 3, subject, -1, SQLITE_TRANSIENT);
        }
      else
        {
          sqlite3_bind_null (ins, 3);
        }
      sqlite3_bind_text (ins, 4, body, -1, SQLITE_TRANSIENT);
      if (idem && *idem)
        {
          sqlite3_bind_text (ins, 5, idem, -1, SQLITE_TRANSIENT);
        }
      else
        {
          sqlite3_bind_null (ins, 5);
        }
      if (sqlite3_step (ins) != SQLITE_DONE)
        {
          /* Unique constraint on (idempotency_key, recipient_id) would hit here on replay */
          sqlite3_finalize (ins);
          /* Try to fetch id in case of constraint race */
          if (idem && *idem)
            {
              sqlite3_stmt *chk = NULL;


              if (sqlite3_prepare_v2 (db,
                                      "SELECT id FROM mail WHERE idempotency_key=?1 AND recipient_id=?2 LIMIT 1;",
                                      -1,
                                      &chk,
                                      NULL) == SQLITE_OK)
                {
                  sqlite3_bind_text (chk, 1, idem, -1, SQLITE_TRANSIENT);
                  sqlite3_bind_int (chk, 2, to_id);
                  if (sqlite3_step (chk) == SQLITE_ROW)
                    {
                      mail_id = sqlite3_column_int (chk, 0);
                    }
                }
              if (chk)
                {
                  sqlite3_finalize (chk);
                }
            }
          if (mail_id == 0)
            {
              send_enveloped_error (ctx->fd, root, 500, "db error");
              return 0;
            }
        }
      else
        {
          mail_id = (int) sqlite3_last_insert_rowid (db);
          sqlite3_finalize (ins);
        }
    }
  /* Respond */
  json_t *resp = json_pack ("{s:i}", "id", mail_id);


  send_enveloped_ok (ctx->fd, root, "mail.sent", resp);
  return 0;
}


/* ==================== mail.inbox ==================== */


/* Input:  { "limit": 50?, "after_id": int? } (optional cursor)
 * Output: type "mail.inbox_v1" { items:[{id,thread_id,sender_id,subject,sent_at,read_at}], next_after_id? }
 */
int
cmd_mail_inbox (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");
  int limit = 50;
  int after_id = 0;
  if (data)
    {
      json_t *jlim = json_object_get (data, "limit");


      if (jlim && json_is_integer (jlim))
        {
          limit = (int) json_integer_value (jlim);
        }
      json_t *jaft = json_object_get (data, "after_id");


      if (jaft && json_is_integer (jaft))
        {
          after_id = (int) json_integer_value (jaft);
        }
    }
  if (limit <= 0 || limit > 200)
    {
      limit = 50;
    }
  const char *SQL =
    "SELECT m.id, m.thread_id, m.sender_id, p.name, m.subject, m.sent_at, m.read_at "
    "FROM mail m JOIN players p ON m.sender_id = p.id "
    "WHERE m.recipient_id=?1 AND m.deleted=0 AND m.archived=0 "
    "  AND (?2=0 OR m.id<?2) " "ORDER BY m.id DESC " "LIMIT ?3;";                                                                                                                                                                                                                       /* uses idx_mail_inbox */
  sqlite3_stmt *st = NULL;


  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
    {
      if (st)
        {
          sqlite3_finalize (st);
        }
      send_enveloped_error (ctx->fd, root, 500, "db error");
      return 0;
    }
  sqlite3_bind_int64 (st, 1, (sqlite3_int64) ctx->player_id);
  sqlite3_bind_int (st, 2, after_id);
  sqlite3_bind_int (st, 3, limit);
  json_t *items = json_array ();
  int last_id = 0;


  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int id = sqlite3_column_int (st, 0);
      int thread_id = sqlite3_column_type (st,
                                           1) ==
                      SQLITE_NULL ? 0 : sqlite3_column_int (st,
                                                            1);
      int sender_id = sqlite3_column_int (st, 2);
      char *sender_name = strdup ((const char *) sqlite3_column_text (st, 3));
      char *subject = strdup ((const char *) sqlite3_column_text (st, 4));
      char *sent_at = strdup ((const char *) sqlite3_column_text (st, 5));
      char *read_at = sqlite3_column_type (st,
                                           6) ==
                      SQLITE_NULL ? NULL : strdup ((const char *)
                                                   sqlite3_column_text (st, 6));
      json_t *row = json_pack ("{s:i, s:i, s:i, s:s, s:s, s:s}",
                               "id", id,
                               "thread_id", thread_id,
                               "sender_id", sender_id,
                               "sender_name", sender_name ? sender_name : "",
                               "subject", subject ? subject : "",
                               "sent_at", sent_at ? sent_at : "");


      if (read_at)
        {
          json_object_set_new (row, "read_at", json_string (read_at));
        }
      json_array_append_new (items, row);
      last_id = id;
      free (sender_name);
      free (subject);
      free (sent_at);
      free (read_at);
    }
  sqlite3_finalize (st);
  json_t *resp = json_pack ("{s:o}", "items", items);


  if (json_array_size (items) == (size_t) limit)
    {
      json_object_set_new (resp, "next_after_id", json_integer (last_id));
    }
  send_enveloped_ok (ctx->fd, root, "mail.inbox_v1", resp);
  return 0;
}


/* ==================== mail.read ==================== */


/* Input:  { "id": int }
 * Output: type "mail.read_v1" { id, thread_id, sender_id, subject, body, sent_at, read_at }
 * Side-effect: set read_at=now for the recipient (if not already set).
 */
int
cmd_mail_read (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");
  if (!data)
    {
      send_enveloped_error (ctx->fd, root, 1300, "Invalid request schema");
      return 0;
    }
  int id = (int) json_integer_value (json_object_get (data, "id"));


  if (id <= 0)
    {
      send_enveloped_error (ctx->fd, root, 1301,
                            "Missing required field: id");
      return 0;
    }
  LOGI ("cmd_mail_read: reading mail id %d for player %d", id,
        ctx->player_id);
  /* Load and verify ownership */
  const char *SEL =
    "SELECT m.id, m.thread_id, m.sender_id, p.name, m.subject, m.body, m.sent_at, m.read_at "
    "FROM mail m JOIN players p ON m.sender_id = p.id WHERE m.id=?1 AND m.recipient_id=?2 AND m.deleted=0;";
  sqlite3_stmt *st = NULL;


  if (sqlite3_prepare_v2 (db, SEL, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("cmd_mail_read: prepare failed: %s", sqlite3_errmsg (db));
      if (st)
        {
          sqlite3_finalize (st);
        }
      send_enveloped_error (ctx->fd, root, 500, "db error");
      return 0;
    }
  sqlite3_bind_int (st, 1, id);
  sqlite3_bind_int64 (st, 2, (sqlite3_int64) ctx->player_id);
  if (sqlite3_step (st) != SQLITE_ROW)
    {
      LOGI ("cmd_mail_read: mail not found or not owner");
      sqlite3_finalize (st);
      send_enveloped_error (ctx->fd, root, 1900,
                            "Recipient not found or message not yours");
      return 0;
    }
  LOGI ("cmd_mail_read: mail found, processing...");
  int thread_id =
    sqlite3_column_type (st, 1) == SQLITE_NULL ? 0 : sqlite3_column_int (st,
                                                                         1);
  int sender_id = sqlite3_column_int (st, 2);
  char *sender_name = strdup ((const char *) sqlite3_column_text (st, 3));
  char *subject = strdup ((const char *) sqlite3_column_text (st, 4));
  char *body = strdup ((const char *) sqlite3_column_text (st, 5));
  char *sent_at = strdup ((const char *) sqlite3_column_text (st, 6));
  char *read_at = sqlite3_column_type (st,
                                       7) ==
                  SQLITE_NULL ? NULL :
                  strdup ((const char *) sqlite3_column_text (st, 7));


  sqlite3_finalize (st);
  /* Mark read if needed */
  if (!read_at)
    {
      sqlite3_stmt *up = NULL;


      if (sqlite3_prepare_v2
            (db,
            "UPDATE mail SET read_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE id=?1;",
            -1,
            &up,
            NULL) == SQLITE_OK)
        {
          sqlite3_bind_int (up, 1, id);
          (void) sqlite3_step (up);
        }
      if (up)
        {
          sqlite3_finalize (up);
        }
    }
  json_t *resp = json_pack ("{s:i,s:i,s:i,s:s,s:s,s:s,s:s}",
                            "id", id,
                            "thread_id", thread_id,
                            "sender_id", sender_id,
                            "sender_name", sender_name ? sender_name : "",
                            "subject", subject ? subject : "",
                            "body", body ? body : "",
                            "sent_at", sent_at ? sent_at : "");


  if (read_at)
    {
      json_object_set_new (resp, "read_at", json_string (read_at));
    }
  else
    {
      /* echo now-ish for UX; server truth is in DB */
      time_t now = time (NULL);
      char iso[32];


      strftime (iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", gmtime (&now));
      json_object_set_new (resp, "read_at", json_string (iso));
    }
  send_enveloped_ok (ctx->fd, root, "mail.read_v1", resp);
  free (sender_name);
  free (subject);
  free (body);
  free (sent_at);
  free (read_at);
  return 0;
}


/* ==================== mail.delete ==================== */


/* Input:  { "ids":[int,...] }  (soft delete; only own messages) */


/* Output: type "mail.deleted" { count:int } */
int
cmd_mail_delete (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");
  json_t *ids = data ? json_object_get (data, "ids") : NULL;
  if (!ids || !json_is_array (ids))
    {
      send_enveloped_error (ctx->fd,
                            root,
                            1300,
                            "Invalid request schema: ids[] required");                          /* :contentReference[oaicite:5]{index=5} */
      return 0;
    }
  /* Build a parameterised IN (...) safely (<= 200 ids) */
  size_t n = json_array_size (ids);


  if (n == 0)
    {
      send_enveloped_ok (ctx->fd, root, "mail.deleted",
                         json_pack ("{s:i}", "count", 0));
    }
  if (n > 200)
    {
      send_enveloped_error (ctx->fd, root, 1305, "Too many bulk items"); /* 1305 *//* :contentReference[oaicite:6]{index=6} */
    }
  /* Create: UPDATE mail SET deleted=1 WHERE recipient_id=? AND id IN (?,?,...) */
  char sql[1024];
  char *p = sql;


  p +=
    snprintf (p, sizeof (sql),
              "UPDATE mail SET deleted=1 WHERE recipient_id=?1 AND id IN (");
  for (size_t i = 0; i < n; i++)
    {
      p +=
        snprintf (p, (size_t) (sql + sizeof (sql) - p),
                  (i ? ",?%zu" : "?%zu"), i + 2);
    }
  p += snprintf (p, (size_t) (sql + sizeof (sql) - p), ");");
  sqlite3_stmt *st = NULL;


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      if (st)
        {
          sqlite3_finalize (st);
        }
      send_enveloped_error (ctx->fd, root, 500, "db error");
      return 0;
    }
  sqlite3_bind_int64 (st, 1, (sqlite3_int64) ctx->player_id);
  for (size_t i = 0; i < n; i++)
    {
      sqlite3_bind_int (st, (int) i + 2,
                        (int) json_integer_value (json_array_get (ids, i)));
    }
  int rc = sqlite3_step (st);
  int changes = sqlite3_changes (db);


  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      send_enveloped_error (ctx->fd, root, 500, "db error");
      return 0;
    }
  json_t *resp = json_pack ("{s:i}", "count", changes);


  send_enveloped_ok (ctx->fd, root, "mail.deleted", resp);
  return 0;
}


/////////////////////////////////////////////////////////


/* static int */


/* sub_contains (sub_node_t *head, const char *topic) */


/* { */


/*   for (sub_node_t * n = head; n; n = n->next) */


/*     if (strcmp (n->topic, topic) == 0) */


/*       return 1; */


/*   return 0; */


/* } */


/* static int */


/* sub_add (sub_map_t *m, const char *topic) */


/* { */


/*   if (sub_contains (m->head, topic)) */


/*     return 0;			/\* already present -> idempotent add *\/ */


/*   sub_node_t *n = (sub_node_t *) calloc (1, sizeof *n); */


/*   if (!n) */


/*     return -1; */


/*   n->topic = strdup (topic); */


/*   if (!n->topic) */


/*     { */


/*       free (n); */


/*       return -1; */


/*     } */


/*   n->next = m->head; */


/*   m->head = n; */


/*   return 1;			/\* added *\/ */


/* } */


/* static int */


/* sub_remove (sub_map_t *m, const char *topic) */


/* { */


/*   sub_node_t **pp = &m->head; */


/*   for (; *pp; pp = &(*pp)->next) */


/*     { */


/*       if (strcmp ((*pp)->topic, topic) == 0) */


/*      { */


/*        sub_node_t *dead = *pp; */


/*        *pp = dead->next; */


/*        free (dead->topic); */


/*        free (dead); */


/*        return 1;		/\* removed *\/ */


/*      } */


/*     } */


/*   return 0;			/\* not found *\/ */


/* } */


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


/* ---- new command handlers ---- */
int
cmd_subscribe_add (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
                            "auth required");
      return -1;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
                            "data must be object");
      return -1;
    }
  json_t *v = json_object_get (data, "topic");


  if (!json_is_string (v))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
                            "missing field: topic");
      return -1;
    }
  const char *topic = json_string_value (v);


  if (!is_ascii_printable (topic) || !len_leq (topic, 64)
      || !is_allowed_topic (topic))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
                            "invalid topic");
      return -1;
    }
  const char *filter_json = NULL;


  v = json_object_get (data, "filter_json");
  if (v)
    {
      if (!json_is_string (v))
        {
          send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
                                "filter_json must be string");
          return -1;
        }
      filter_json = json_string_value (v);
      /* sanity-parse filter JSON so we don't store garbage */
      json_error_t jerr;
      json_t *probe = json_loads (filter_json, 0, &jerr);


      if (!probe)
        {
          send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
                                "filter_json is not valid JSON");
          return -1;
        }
      json_decref (probe);
    }
  /* Cap check */
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;


  if (sqlite3_prepare_v2
        (db,
        "SELECT COUNT(*) FROM subscriptions WHERE player_id=?1 AND enabled=1;",
        -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return -1;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);
  int have = 0;


  if (sqlite3_step (st) == SQLITE_ROW)
    {
      have = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  if (have >= MAX_SUBSCRIPTIONS_PER_PLAYER)
    {
      json_t *meta =
        json_pack ("{s:i,s:i}", "max", MAX_SUBSCRIPTIONS_PER_PLAYER, "have",
                   have);


      send_enveloped_refused (ctx->fd, root, ERR_LIMIT_EXCEEDED,
                              "too many subscriptions", meta);
      return -1;
    }
  /* Upsert subscription */
  int rc = db_subscribe_upsert (ctx->player_id, topic, filter_json,
                                0 /*locked */ );


  if (rc != 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return -1;
    }
  json_t *resp = json_pack ("{s:s}", "topic", topic);


  send_enveloped_ok (ctx->fd, root, "subscribe.added", resp);
  json_decref (resp);
  return 0;
}


int
cmd_subscribe_remove (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
                            "auth required");
      return -1;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
                            "data must be object");
      return -1;
    }
  json_t *v = json_object_get (data, "topic");


  if (!json_is_string (v))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
                            "missing field: topic");
      return -1;
    }
  const char *topic = json_string_value (v);


  if (!is_ascii_printable (topic) || !len_leq (topic, 64)
      || !is_allowed_topic (topic))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
                            "invalid topic");
      return -1;
    }
  int was_locked = 0;
  int rc = db_subscribe_disable (ctx->player_id, topic, &was_locked);


  if (rc == +1 || was_locked)
    {
      send_enveloped_refused (ctx->fd, root, REF_SAFE_ZONE_ONLY
                              /* or REF_LOCKED if you prefer */,
                              "subscription locked by policy", NULL);
      return -1;
    }
  if (rc != 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_USER_NOT_FOUND,
                            "subscription not found");
      return -1;
    }
  json_t *resp = json_pack ("{s:s}", "topic", topic);


  send_enveloped_ok (ctx->fd, root, "subscribe.removed", resp);
  json_decref (resp);
  return 0;
}


int
cmd_subscribe_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
                            "auth required");
      return -1;
    }
  sqlite3_stmt *it = NULL;


  if (db_subscribe_list (ctx->player_id, &it) != 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return -1;
    }
  json_t *items = json_array ();


  while (sqlite3_step (it) == SQLITE_ROW)
    {
      const char *topic = (const char *) sqlite3_column_text (it, 0);
      int locked = sqlite3_column_int (it, 1);
      int enabled = sqlite3_column_int (it, 2);
      const char *deliv = (const char *) sqlite3_column_text (it, 3);
      const char *flt = (const char *) sqlite3_column_text (it, 4);
      json_t *row = json_pack ("{s:s,s:i,s:i,s:s,s:O?}",
                               "topic", topic ? topic : "",
                               "locked", locked,
                               "enabled", enabled,
                               "delivery", deliv ? deliv : "push",
                               "filter", flt ? json_loads (flt, 0,
                                                           NULL) : NULL);


      json_array_append_new (items, row);
    }
  sqlite3_finalize (it);
  json_t *resp = json_pack ("{s:O}", "items", items);


  send_enveloped_ok (ctx->fd, root, "subscribe.list", resp);
  json_decref (resp);
  return 0;
}


/* ---- command handlers ---- */


/* /\* Guard: refuse removing a locked subscription *\/ */


/* static int */


/* is_locked_subscription (sqlite3 *db, int player_id, const char *topic) */


/* { */


/*   static const char *SQL = */


/*     "SELECT locked FROM subscriptions WHERE player_id=? AND event_type=? LIMIT 1"; */


/*   sqlite3_stmt *st = NULL; */


/*   int locked = 0; */


/*   if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK) */


/*     return 0; */


/*   sqlite3_bind_int (st, 1, player_id); */


/*   sqlite3_bind_text (st, 2, topic, -1, SQLITE_STATIC); */


/*   if (sqlite3_step (st) == SQLITE_ROW) */


/*     { */


/*       locked = sqlite3_column_int (st, 0); */


/*     } */


/*   sqlite3_finalize (st); */


/*   return locked ? 1 : 0; */


/* } */


/* ================== admin.* =================== */
static inline int
require_admin (client_ctx_t *ctx, json_t *root)
{
  // TODO: replace with real ACL; for now require auth at least
  if (ctx->player_id > 0)
    {
      return 1;
    }
  send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
  return 0;
}


int
cmd_admin_notice (client_ctx_t *ctx, json_t *root)
{
  if (!require_admin (ctx, root))
    {
      return 0;
    }
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented: admin.notice");
  return 0;
}


int
cmd_admin_shutdown_warning (client_ctx_t *ctx, json_t *root)
{
  if (!require_admin (ctx, root))
    {
      return 0;
    }
  send_enveloped_error (ctx->fd, root, 1101,
                        "Not implemented: admin.shutdown_warning");
  return 0;
}


/* Optional: friendly descriptions without changing ALLOWED_TOPICS */
static const char *
topic_desc (const char *pattern)
{
  if (strcasecmp (pattern, "sector.*") == 0)
    {
      return
        "All sector-scoped events; use sector.{id} pattern, e.g., sector.42";
    }
  if (strcasecmp (pattern, "system.notice") == 0)
    {
      return "Server notices & maintenance";
    }
  if (strcasecmp (pattern, "system.events") == 0)
    {
      return "Raw system events stream (structured)";
    }
  if (strcasecmp (pattern, "corp.mail") == 0)
    {
      return "Corporation mail (membership required)";
    }
  if (strcasecmp (pattern, "corp.log") == 0)
    {
      return "Corporation activity log (membership required)";
    }
  if (strcasecmp (pattern, "chat.global") == 0)
    {
      return "Global chat/subspace mirror";
    }
  return "";
}


/* Build an example for wildcard topics (e.g., "sector.*" -> "sector.42") */
static json_t *
catalog_topics_json (void)
{
  json_t *arr = json_array ();
  if (!arr)
    {
      return NULL;
    }
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
        {
          json_object_set_new (obj, "desc", json_string (desc));
        }
      if (is_wc)
        {
          /* make example: replace trailing ".*" with ".42" */
          size_t n = strlen (pat);
          char example[128];


          if (n < sizeof (example))
            {
              memcpy (example, pat, n - 1);     // copy up to '*'
              example[n - 1] = '4';
              example[n] = '2';
              example[n + 1] = '\0';
              json_object_set_new (obj, "example", json_string (example));      // e.g., "sector.42"
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
    {
      send_enveloped_error (ctx->fd, root, 1500, "Allocation failure");
    }
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

