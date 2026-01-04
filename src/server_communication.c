/* src/server_communication.c */
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

/* local includes */
#include "server_communication.h"
#include "server_envelope.h"
#include "server_players.h"
#include "errors.h"
#include "config.h"
#include "game_db.h"
#include "server_cmds.h"
#include "database.h"
#include "server_loop.h"
#include "db_player_settings.h"
#include "server_log.h"
#include "database_cmd.h"
#include "db/db_api.h"
#include "db/sql_driver.h"


enum
{ MAX_SUBSCRIPTIONS_PER_PLAYER = 64 };


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


/* ---- topic validation ----
   Accept either exact catalogue items or simple suffix wildcard "prefix.*".
   Adjust ALLOWED list as you add more feeds. */
static const char *ALLOWED_TOPICS[] = {
  "system.notice",
  "system.events",
  "broadcast.global",
  "sector.*",
  "corp.*",
  "player.*",
  "chat.global",
  "corp.mail",
  "corp.log",
  "iss.move",
  "iss.warp",
  "npc.*",
  "nav.*",
  "chat.*",
  "engine.tick",
  "sector.notice",
  NULL
};


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
      if (matches_pattern (t, allowed[i]))
        {
          return 1;
        }
    }
  return 0;
}


extern void attach_rate_limit_meta (json_t *env, client_ctx_t *ctx);
extern void rl_tick (client_ctx_t *ctx);
extern void send_all_json (int fd, json_t *obj);

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


static void
broadcast_system_notice (int notice_id,
                         const char *title,
                         const char *body,
                         const char *severity,
                         time_t created_at, time_t expires_at)
{
  json_t *data = json_object ();
  json_object_set_new (data, "id", json_integer (notice_id));
  json_object_set_new (data, "title", json_string (title ? title : ""));
  json_object_set_new (data, "body", json_string (body ? body : ""));
  json_object_set_new (data, "severity",
                       json_string (severity ? severity : "info"));
  json_object_set_new (data, "created_at", json_integer ((int) created_at));
  json_object_set_new (data, "expires_at", json_integer ((int) expires_at));
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
      send_all_json (m->ctx->fd, env);
      json_decref (env);
    }
  json_decref (data);
}


int
cmd_sys_notice_create (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0 ||
      auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Permission denied", NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  const char *title = json_string_value (json_object_get (data, "title"));
  const char *body = json_string_value (json_object_get (data, "body"));
  const char *sev = json_string_value (json_object_get (data, "severity"));
  json_t *exp = json_object_get (data, "expires_at");


  if (!title || !body)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "title and body are required");
      return 0;
    }
  if (!sev
      || (strcasecmp (sev, "info") && strcasecmp (sev, "warn")
          && strcasecmp (sev, "error")))
    {
      sev = "info";
    }

  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  int64_t notice_id = 0;
  time_t now = time (NULL);
  db_error_t err;

  char sql[256];
  sql_build(db, 
    "INSERT INTO system_notice (created_at, title, body, severity, expires_at) "
    "VALUES ({1}, {2}, {3}, {4}, {5});",
    sql, sizeof(sql));

  db_bind_t params[5];


  params[0] = db_bind_i64 ((int64_t)now);
  params[1] = db_bind_text (title);
  params[2] = db_bind_text (body);
  params[3] = db_bind_text (sev);
  if (exp && json_is_integer (exp))
    {
      params[4] = db_bind_i64 (json_integer_value (exp));
    }
  else
    {
      params[4] = db_bind_null ();
    }

  if (!db_exec_insert_id (db, sql, params, 5, &notice_id, &err))
    {
      LOGE ("sys.notice.create SQL error: %s", err.message);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  /* Immediate push to online users */
  broadcast_system_notice ((int)notice_id, title, body, sev, now,
                           now + (24 * 60 * 60));

  /* Build reply */
  json_t *resp = json_object ();


  json_object_set_new (resp, "id", json_integer (notice_id));
  json_object_set_new (resp, "title", json_string (title));
  json_object_set_new (resp, "body", json_string (body));
  json_object_set_new (resp, "severity", json_string (sev));

  if (exp && json_is_integer (exp))
    {
      json_object_set (resp, "expires_at", exp);
    }
  send_response_ok_take (ctx, root, "announce.created", &resp);
  return 0;
}


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

  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  db_res_t *res = NULL;
  db_error_t err;
  json_t *items = NULL;
  json_t *resp = NULL;
  int rc = 0;

  const char *now_expr = sql_now_timestamptz(db);
  if (!now_expr)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  char sql[512];
  char sql_tmpl[512];
  snprintf(sql_tmpl, sizeof(sql_tmpl),
    "SELECT n.system_notice_id, n.title, n.body, n.severity, n.created_at, n.expires_at, s.seen_at "
    "FROM system_notice n "
    "LEFT JOIN notice_seen s ON s.notice_id = n.system_notice_id AND s.player_id = {1} "
    "WHERE ({2} = 1 OR n.expires_at IS NULL OR n.expires_at > %s) "
    "ORDER BY n.created_at DESC LIMIT {3};",
    now_expr);
  sql_build(db, sql_tmpl, sql, sizeof(sql));

  db_bind_t params[3];


  params[0] = db_bind_i32 (ctx->player_id);
  params[1] = db_bind_i32 (include_expired ? 1 : 0);
  params[2] = db_bind_i32 (limit);

  if (!db_query (db, sql, params, 3, &res, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      rc = 0;
      goto cleanup;
    }

  items = json_array ();


  while (db_res_step (res, &err))
    {
      int id = db_res_col_i32 (res, 0, &err);
      const char *t = db_res_col_text (res, 1, &err);
      const char *b = db_res_col_text (res, 2, &err);
      const char *sv = db_res_col_text (res, 3, &err);
      int64_t created = db_res_col_i64 (res, 4, &err);
      int64_t expires = db_res_col_is_null (res, 5) ? 0 : db_res_col_i64 (res,
                                                                          5,
                                                                          &err);
      int64_t seen_at = db_res_col_is_null (res, 6) ? 0 : db_res_col_i64 (res,
                                                                          6,
                                                                          &err);

      json_t *row = json_object ();


      json_object_set_new (row, "id", json_integer (id));
      json_object_set_new (row, "title", json_string (t ? t : ""));
      json_object_set_new (row, "body", json_string (b ? b : ""));
      json_object_set_new (row, "severity", json_string (sv ? sv : "info"));
      json_object_set_new (row, "created_at", json_integer (created));

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

  resp = json_object ();
  json_object_set_new (resp, "items", items);
  items = NULL;
  send_response_ok_take (ctx, root, "notice.list_v1", &resp);
  resp = NULL;
  rc = 0;

cleanup:
  if (res)
    db_res_finalize (res);
  if (items)
    json_decref (items);
  if (resp)
    json_decref (resp);
  return rc;
}


int
cmd_notice_ack (client_ctx_t *ctx, json_t *root)
{
  json_t *data = json_object_get (root, "data");
  int id = (int) json_integer_value (json_object_get (data, "id"));
  if (id <= 0)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "id required");
      return 0;
    }

  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  time_t now = time (NULL);
  db_error_t err;

  char sql[256];
  sql_build(db,
    "INSERT INTO notice_seen (notice_id, player_id, seen_at) "
    "VALUES ({1}, {2}, {3}) "
    "ON CONFLICT(notice_id, player_id) DO UPDATE SET seen_at = excluded.seen_at;",
    sql, sizeof(sql));

  db_bind_t params[3];


  params[0] = db_bind_i32 (id);
  params[1] = db_bind_i32 (ctx->player_id);
  params[2] = db_bind_i64 ((int64_t)now);

  if (!db_exec (db, sql, params, 3, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  /* Optional: return the updated row */
  json_t *resp = json_object ();


  json_object_set_new (resp, "id", json_integer (id));
  json_object_set_new (resp, "seen_at", json_integer (now));

  send_response_ok_take (ctx, root, "notice.acknowledged", &resp);
  return 0;
}


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
server_broadcast_event (const char *type, json_t *data)
{
  if (!type || !data)
    {
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      return -1;
    }
  struct bc_ctx bc = {.event_type = type,.data = data,.deliveries = 0 };
  int rc = db_for_each_subscriber (db, type, bc_cb, &bc);


  return (rc == 0) ? bc.deliveries : rc;
}


int
server_broadcast_to_sector (int sid, const char *name, json_t *payload)
{
  if (!name || !payload || sid <= 0)
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
        sid);
      return -1;
    }
  // This function acts as a wrapper to comm_publish_sector_event, ensuring sector-specific filtering
  comm_publish_sector_event (sid, name, payload_copy);
  return 0;                     // Success
}


void
comm_broadcast_message (comm_scope_t scope,
                        long long id,
                        const char *msg,
                        json_t *extra)
{
  if (!msg || !*msg)
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
        snprintf (topic, sizeof (topic), "sector.%lld", id);
        break;
      case COMM_SCOPE_CORP:
        snprintf (topic, sizeof (topic), "corp.%lld", id);
        break;
      case COMM_SCOPE_PLAYER:
        snprintf (topic, sizeof (topic), "player.%lld", id);
        break;
      default:
        if (extra)
          {
            json_decref (extra);
          }
        return;
    }
  /* Prebuild common data object; clone per recipient if needed */
  json_t *base = json_object ();


  json_object_set_new (base, "message", json_string (msg));
  json_object_set_new (base, "scope",
                       json_string ((scope == COMM_SCOPE_GLOBAL) ? "global" :
                                    (scope ==
                                     COMM_SCOPE_SECTOR) ? "sector" :
                                    (scope ==
                                     COMM_SCOPE_CORP) ? "corp" : "player"));
  json_object_set_new (base, "scope_id",
                       json_integer ((json_int_t) id));


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
      json_t *v = NULL;
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


void
comm_publish_sector_event (int sid, const char *name, json_t *data)
{
  if (sid <= 0 || !name || !*name)
    {
      if (data)
        {
          json_decref (data);
        }
      return;
    }


  db_t *db = game_db_get_handle ();


  if (!db)
    {
      if (data)
        {
          json_decref (data);
        }
      return;
    }


  /* Build concrete topic once (e.g., "sector.42") */
  char topic[64];


  snprintf (topic, sizeof (topic), "sector.%d", sid);


  struct bc_ctx bc = {.event_type = name,.data = data,
                      .deliveries = 0};


  /* db_for_each_subscriber finds players subscribed to 'topic' (exact or wildcard) */
  db_for_each_subscriber (db, topic, bc_cb, &bc);


  json_decref (data);
}


void
push_unseen_notices_for_player (client_ctx_t *ctx, int pid)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return;
    }
  json_t *arr = db_notice_list_unseen_for_player (db, pid);


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
      json_t *data = json_object ();


      json_object_set_new (data, "id", json_integer (id));
      json_object_set_new (data, "title", json_string (ttl ? ttl : ""));
      json_object_set_new (data, "body", json_string (bod ? bod : ""));
      json_object_set_new (data, "severity",
                           json_string (sev ? sev : "info"));
      json_object_set_new (data, "created_at", json_integer (created));
      json_object_set_new (data, "expires_at", json_integer (expires));


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
    }
  json_decref (arr);
}


int
cmd_admin_notice_create (client_ctx_t *ctx, json_t *root)
{
  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_response_error (ctx, root, REF_NOT_IN_SECTOR, "Bad request");
      return 1;
    }
  const char *title = json_string_value (json_object_get (data, "title"));
  const char *body = json_string_value (json_object_get (data, "body"));
  const char *sev = json_string_value (json_object_get (data, "severity"));
  int expires_at =
    (int) json_integer_value (json_object_get (data, "expires_at"));


  if (!title || !body)
    {
      send_response_error (ctx, root, REF_NOT_IN_SECTOR,
                           "Missing title/body");
      return 1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "DB unavailable");
      return 1;
    }
  int id =
    db_notice_create (db, title, body, sev ? sev : "info", (time_t) expires_at);


  if (id < 0)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "DB error");
      return 1;
    }
  /* Broadcast to all online sessions */
  time_t now = time (NULL);


  broadcast_system_notice (id, title, body, sev ? sev : "info", now,
                           (time_t) expires_at);
  json_t *ok = json_object ();


  json_object_set_new (ok, "notice_id", json_integer (id));


  send_response_ok_take (ctx, root, "admin.notice.created_v1", &ok);
  return 0;
}


int
cmd_notice_dismiss (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_IMPLEMENTED,
                                   "Auth required", NULL);
      return 1;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, REF_NOT_IN_SECTOR, "Bad request");
      return 1;
    }
  int notice_id =
    (int) json_integer_value (json_object_get (data, "notice_id"));


  if (notice_id <= 0)
    {
      send_response_error (ctx, root, REF_NOT_IN_SECTOR, "Missing notice_id");
      return 1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "DB unavailable");
      return 1;
    }
  if (db_notice_mark_seen (db, notice_id, ctx->player_id) != 0)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND, "DB error");
      return 1;
    }
  json_t *ok = json_object ();


  json_object_set_new (ok, "notice_id", json_integer (notice_id));


  send_response_ok_take (ctx, root, "notice.dismissed_v1", &ok);
  return 0;
}


/* ---- shared helpers (module-local) ---- */
static inline int
require_auth (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id > 0)
    {
      return 1;
    }
  send_response_refused_steal (ctx,
                               root,
                               ERR_SECTOR_NOT_FOUND,
                               "Not authenticated", NULL);
  return 0;
}


static inline int
niy (client_ctx_t *ctx, json_t *root, const char *which)
{
  char buf[256];
  snprintf (buf, sizeof (buf), "Not implemented: %s", which);
  send_response_error (ctx, root, ERR_NOT_IMPLEMENTED, buf);
  return 0;
}


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


int
cmd_mail_send (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    {
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_SCHEMA, "Invalid request schema");
      return 0;
    }
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
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD,
                           "Missing required field: to/to_id and body");
      return 0;
    }
  /* Resolve recipient by name if needed */
  db_error_t err;


  if (to_id <= 0 && to_name)
    {
      db_res_t *res = NULL;
      char sql[256];
      sql_build(db,
        "SELECT id FROM players WHERE lower(name) = lower({1}) LIMIT 1;",
        sql, sizeof(sql));


      if (db_query (db,
                    sql,
                    (db_bind_t[]){ db_bind_text (to_name) },
                    1,
                    &res,
                    &err))
        {
          if (db_res_step (res, &err))
            {
              to_id = db_res_col_i32 (res, 0, &err);
            }
          db_res_finalize (res);
        }
      if (to_id <= 0)
        {
          send_response_error (ctx, root, 1900, "Recipient not found");
          return 0;
        }
    }
  /* Check if recipient has blocked the sender */
  {
    db_res_t *res = NULL;
    char sql[256];
    sql_build(db,
      "SELECT 1 FROM player_block WHERE blocker_id={1} AND blocked_id={2} LIMIT 1;",
      sql, sizeof(sql));
    db_bind_t p[] = { db_bind_i32 (to_id), db_bind_i32 (ctx->player_id) };
    int blocked = 0;


    if (db_query (db, sql, p, 2, &res, &err))
      {
        if (db_res_step (res, &err))
          {
            blocked = 1;
          }
        db_res_finalize (res);
      }
    if (blocked)
      {
        send_response_error (ctx,
                             root,
                             ERR_HARDWARE_NOT_AVAILABLE, "Muted or blocked");
        return 0;
      }
  }
  /* Idempotency */
  int64_t mail_id = 0;


  if (idem && *idem)
    {
      db_res_t *chk_res = NULL;
      char sql[256];
      sql_build(db,
        "SELECT mail_id FROM mail WHERE idempotency_key={1} AND recipient_id={2} LIMIT 1;",
        sql, sizeof(sql));
      db_bind_t p[] = { db_bind_text (idem), db_bind_i32 (to_id) };


      if (db_query (db, sql, p, 2, &chk_res, &err))
        {
          if (db_res_step (chk_res, &err))
            {
              mail_id = db_res_col_i64 (chk_res, 0, &err);
            }
          db_res_finalize (chk_res);
        }
    }
  /* Insert if not already present */
  if (mail_id == 0)
    {
      char sql[256];
      sql_build(db,
        "INSERT INTO mail(sender_id, recipient_id, subject, body, idempotency_key) "
        "VALUES({1},{2},{3},{4},{5});",
        sql, sizeof(sql));
      db_bind_t p[5];


      p[0] = db_bind_i32 (ctx->player_id);
      p[1] = db_bind_i32 (to_id);
      p[2] = subject ? db_bind_text (subject) : db_bind_null ();
      p[3] = db_bind_text (body);
      p[4] = (idem && *idem) ? db_bind_text (idem) : db_bind_null ();

      if (!db_exec_insert_id (db, sql, p, 5, &mail_id, &err))
        {
          /* Re-check idempotency in case of race */
          if (idem && *idem)
            {
              db_res_t *chk_res = NULL;
              char sql_chk[256];
              sql_build(db,
                "SELECT mail_id FROM mail WHERE idempotency_key={1} AND recipient_id={2} LIMIT 1;",
                sql_chk, sizeof(sql_chk));
              db_bind_t p_chk[] = { db_bind_text (idem), db_bind_i32 (to_id) };


              if (db_query (db, sql_chk, p_chk, 2, &chk_res, &err))
                {
                  if (db_res_step (chk_res, &err))
                    {
                      mail_id = db_res_col_i64 (chk_res, 0, &err);
                    }
                  db_res_finalize (chk_res);
                }
            }
          if (mail_id == 0)
            {
              send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
              return 0;
            }
        }
    }
  /* Respond */
  json_t *resp = json_object ();


  json_object_set_new (resp, "id", json_integer (mail_id));
  send_response_ok_take (ctx, root, "mail.sent", &resp);
  return 0;
}


int
cmd_mail_inbox (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }
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
  char SQL[512];
  sql_build(db,
    "SELECT m.mail_id, m.thread_id, m.sender_id, p.name, m.subject, m.sent_at, m.read_at "
    "FROM mail m JOIN players p ON m.sender_id = p.player_id "
    "WHERE m.recipient_id={1} AND m.deleted=0 AND m.archived=0 "
    "  AND ({2}=0 OR m.mail_id<{2}) " "ORDER BY m.mail_id DESC " "LIMIT {3};",
    SQL, sizeof(SQL));

  db_res_t *res = NULL;
  db_error_t err;
  db_bind_t params[3];


  params[0] = db_bind_i32 (ctx->player_id);
  params[1] = db_bind_i32 (after_id);
  params[2] = db_bind_i32 (limit);

  if (!db_query (db, SQL, params, 3, &res, &err))
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  json_t *items = json_array ();
  int last_id = 0;


  while (db_res_step (res, &err))
    {
      int id = db_res_col_i32 (res, 0, &err);
      int thread_id = db_res_col_is_null (res, 1) ? 0 : db_res_col_i32 (res,
                                                                        1,
                                                                        &err);
      int sender_id = db_res_col_i32 (res, 2, &err);
      const char *sender_name = db_res_col_text (res, 3, &err);
      const char *subject = db_res_col_text (res, 4, &err);
      const char *sent_at = db_res_col_text (res, 5, &err);
      const char *read_at = db_res_col_is_null (res,
                                                6) ? NULL :
                            db_res_col_text (res,
                                             6,
                                             &err);

      json_t *row = json_object ();


      json_object_set_new (row, "id", json_integer (id));
      json_object_set_new (row, "thread_id", json_integer (thread_id));
      json_object_set_new (row, "sender_id", json_integer (sender_id));
      json_object_set_new (row, "sender_name",
                           json_string (sender_name ? sender_name : ""));
      json_object_set_new (row, "subject",
                           json_string (subject ? subject : ""));
      json_object_set_new (row, "sent_at",
                           json_string (sent_at ? sent_at : ""));


      if (read_at)
        {
          json_object_set_new (row, "read_at", json_string (read_at));
        }
      json_array_append_new (items, row);
      last_id = id;
    }
  db_res_finalize (res);

  json_t *resp = json_object ();


  json_object_set (resp, "items", items);


  if (json_array_size (items) == (size_t) limit)
    {
      json_object_set_new (resp, "next_after_id", json_integer (last_id));
    }
  send_response_ok_take (ctx, root, "mail.inbox_v1", &resp);
  return 0;
}


int
cmd_mail_read (client_ctx_t *ctx,
               json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_SCHEMA, "Invalid request schema");
      return 0;
    }
  int id = (int) json_integer_value (json_object_get (data, "id"));


  if (id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD, "Missing required field: id");
      return 0;
    }
  LOGI ("cmd_mail_read: reading mail id %d for player %d", id,
        ctx->player_id);
  /* Load and verify ownership */
  char SEL[512];
  sql_build(db,
    "SELECT m.mail_id, m.thread_id, m.sender_id, p.name, m.subject, m.body, m.sent_at, m.read_at "
    "FROM mail m JOIN players p ON m.sender_id = p.player_id WHERE m.mail_id={1} AND m.recipient_id={2} AND m.deleted=0;",
    SEL, sizeof(SEL));

  db_res_t *res = NULL;
  db_error_t err;
  db_bind_t params[2];


  params[0] = db_bind_i32 (id);
  params[1] = db_bind_i32 (ctx->player_id);

  if (!db_query (db, SEL, params, 2, &res, &err))
    {
      LOGE ("cmd_mail_read: prepare failed: %s", err.message);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  if (!db_res_step (res, &err))
    {
      LOGI ("cmd_mail_read: mail not found or not owner");
      db_res_finalize (res);
      send_response_error (ctx,
                           root,
                           1900, "Recipient not found or message not yours");
      return 0;
    }

  LOGI ("cmd_mail_read: mail found, processing...");
  int thread_id = db_res_col_is_null (res, 1) ? 0 : db_res_col_i32 (res, 1,
                                                                    &err);
  int sender_id = db_res_col_i32 (res, 2, &err);
  const char *tmp_sender_name = db_res_col_text (res, 3, &err);
  const char *tmp_subject = db_res_col_text (res, 4, &err);
  const char *tmp_body = db_res_col_text (res, 5, &err);
  const char *tmp_sent_at = db_res_col_text (res, 6, &err);
  const char *tmp_read_at = db_res_col_is_null (res,
                                                7) ? NULL :
                            db_res_col_text (res,
                                             7,
                                             &err);

  char *sender_name = strdup (tmp_sender_name ? tmp_sender_name : "");
  char *subject = strdup (tmp_subject ? tmp_subject : "");
  char *body = strdup (tmp_body ? tmp_body : "");
  char *sent_at = strdup (tmp_sent_at ? tmp_sent_at : "");
  char *read_at = tmp_read_at ? strdup (tmp_read_at) : NULL;


  db_res_finalize (res);

  /* Mark read if needed */
  if (!read_at)
    {
      time_t now = time (NULL);
      char iso[32];


      strftime (iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", gmtime (&now));

      char up_sql[256];
      sql_build(db, "UPDATE mail SET read_at={1} WHERE mail_id={2};",
        up_sql, sizeof(up_sql));
      db_bind_t up_params[2];


      up_params[0] = db_bind_text (iso);
      up_params[1] = db_bind_i32 (id);
      db_exec (db, up_sql, up_params, 2, &err);
    }

  json_t *resp = json_object ();


  json_object_set_new (resp, "id", json_integer (id));
  json_object_set_new (resp, "thread_id", json_integer (thread_id));
  json_object_set_new (resp, "sender_id", json_integer (sender_id));
  json_object_set_new (resp, "sender_name",
                       json_string (sender_name));
  json_object_set_new (resp, "subject", json_string (subject));
  json_object_set_new (resp, "body", json_string (body));
  json_object_set_new (resp, "sent_at", json_string (sent_at));


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
  send_response_ok_take (ctx, root, "mail.read_v1", &resp);

  free (sender_name);
  free (subject);
  free (body);
  free (sent_at);
  if (read_at)
    {
      free (read_at);
    }
  return 0;
}


int
cmd_mail_delete (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *ids = data ? json_object_get (data, "ids") : NULL;


  if (!ids || !json_is_array (ids))
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_SCHEMA,
                           "Invalid request schema: ids[] required");
      return 0;
    }
  /* Build a parameterised IN (...) safely (<= 200 ids) */
  size_t n = json_array_size (ids);


  if (n == 0)
    {
      json_t *res = json_object ();


      json_object_set_new (res, "count", json_integer (0));
      send_response_ok_take (ctx, root, "mail.deleted", &res);
      return 0;
    }
  if (n > 200)
    {
      send_response_error (ctx,
                           root,
                           ERR_TOO_MANY_BULK_ITEMS,
                           "Too many bulk items");
      return 0;
    }
  /* Create: UPDATE mail SET deleted=1 WHERE recipient_id={1} AND mail_id IN ({2},{3},...) */
  char sql[4096];
  char *p = sql;


  p +=
    snprintf (p, sizeof (sql),
              "UPDATE mail SET deleted=1 WHERE recipient_id={1} AND mail_id IN (");
  for (size_t i = 0; i < n; i++)
    {
      p +=
        snprintf (p, (size_t) (sql + sizeof (sql) - p),
                  (i ? ",{%zu}" : "{%zu}"), i + 2);
    }
  p += snprintf (p, (size_t) (sql + sizeof (sql) - p), ");");

  db_bind_t *params = malloc ((n + 1) * sizeof (db_bind_t));


  if (!params)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "out of memory");
      return 0;
    }

  params[0] = db_bind_i32 (ctx->player_id);
  for (size_t i = 0; i < n; i++)
    {
      params[i +
             1] = db_bind_i32 ((int) json_integer_value (json_array_get (ids,
                                                                         i)));
    }

  int64_t rows_affected = 0;
  db_error_t err;


  if (!db_exec_rows_affected (db, sql, params, n + 1, &rows_affected, &err))
    {
      free (params);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }
  free (params);

  json_t *resp = json_object ();


  json_object_set_new (resp, "count", json_integer (rows_affected));
  send_response_ok_take (ctx, root, "mail.deleted", &resp);
  return 0;
}


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


int
cmd_subscribe_add (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "auth required");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA,
                           "data must be object");
      return 0;
    }
  json_t *v = json_object_get (data, "topic");


  if (!json_is_string (v))
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD,
                           "missing field: topic");
      return 0;
    }
  const char *topic = json_string_value (v);


  if (!is_ascii_printable (topic) || !len_leq (topic, 64)
      || !is_allowed_topic (topic))
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "invalid topic");
      return 0;
    }
  const char *filter_json = NULL;


  v = json_object_get (data, "filter_json");
  if (v)
    {
      if (!json_is_string (v))
        {
          send_response_error (ctx,
                               root,
                               ERR_INVALID_ARG, "filter_json must be string");
          return 0;
        }
      filter_json = json_string_value (v);
      /* sanity-parse filter JSON so we don't store garbage */
      json_error_t jerr;
      json_t *probe = json_loads (filter_json, 0, &jerr);


      if (!probe)
        {
          send_response_error (ctx,
                               root,
                               ERR_INVALID_ARG,
                               "filter_json is not valid JSON");
          return 0;
        }
      json_decref (probe);
    }
  /* Cap check */
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  db_res_t *res = NULL;
  db_error_t err;
  char sql[256];
  sql_build(db,
    "SELECT COUNT(*) FROM subscriptions WHERE player_id={1} AND enabled=1;",
    sql, sizeof(sql));
  int have = 0;


  if (db_query (db,
                sql,
                (db_bind_t[]){ db_bind_i32 (ctx->player_id) },
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          have = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }
  else
    {
      send_response_error (ctx, root, ERR_UNKNOWN, "db error");
      return 0;
    }

  if (have >= MAX_SUBSCRIPTIONS_PER_PLAYER)
    {
      json_t *meta = json_object ();


      json_object_set_new (meta, "max",
                           json_integer (MAX_SUBSCRIPTIONS_PER_PLAYER));
      json_object_set_new (meta, "have", json_integer (have));


      send_response_refused_steal (ctx,
                                   root,
                                   ERR_LIMIT_EXCEEDED,
                                   "too many subscriptions", meta);
      return 0;
    }
  /* Upsert subscription */
  int rc = db_subscribe_upsert (db, ctx->player_id, topic, filter_json,
                                0 /*locked */ );


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_UNKNOWN, "db error");
      return 0;
    }
  json_t *resp = json_object ();


  json_object_set_new (resp, "topic", json_string (topic));


  send_response_ok_take (ctx, root, "subscribe.added", &resp);
  return 0;
}


int
cmd_subscribe_remove (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "auth required");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA,
                           "data must be object");
      return 0;
    }
  json_t *v = json_object_get (data, "topic");


  if (!json_is_string (v))
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD,
                           "missing field: topic");
      return 0;
    }
  const char *topic = json_string_value (v);


  if (!is_ascii_printable (topic) || !len_leq (topic, 64)
      || !is_allowed_topic (topic))
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "invalid topic");
      return 0;
    }

  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  int was_locked = 0;
  int rc = db_subscribe_disable (db, ctx->player_id, topic, &was_locked);


  if (rc == +1 || was_locked)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   1456 /* REF_SAFE_ZONE_ONLY used in org */,
                                   "subscription locked by policy",
                                   NULL);
      return 0;
    }
  if (rc != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_USER_NOT_FOUND, "subscription not found");
      return 0;
    }
  json_t *resp = json_object ();


  json_object_set_new (resp, "topic", json_string (topic));


  send_response_ok_take (ctx, root, "subscribe.removed", &resp);
  return 0;
}


int
cmd_subscribe_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "auth required");
      return 0;
    }

  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "db error");
      return 0;
    }

  db_res_t *res = NULL;
  db_error_t err;
  char sql[256];
  sql_build(db,
    "SELECT event_type, locked, enabled, delivery, filter_json FROM subscriptions WHERE player_id = {1};",
    sql, sizeof(sql));


  if (!db_query (db,
                 sql,
                 (db_bind_t[]){ db_bind_i32 (ctx->player_id) },
                 1,
                 &res,
                 &err))
    {
      send_response_error (ctx, root, ERR_UNKNOWN, "db error");
      return 0;
    }

  json_t *items = json_array ();


  while (db_res_step (res, &err))
    {
      const char *tmp_topic = db_res_col_text (res, 0, &err);
      int locked = db_res_col_i32 (res, 1, &err);
      int enabled = db_res_col_i32 (res, 2, &err);
      const char *tmp_deliv = db_res_col_text (res, 3, &err);
      const char *tmp_flt = db_res_col_is_null (res,
                                                4) ? NULL :
                            db_res_col_text (res,
                                             4,
                                             &err);

      json_t *row = json_object ();


      json_object_set_new (row,
                           "topic", json_string (tmp_topic ? tmp_topic : ""));
      json_object_set_new (row, "locked", json_integer (locked));
      json_object_set_new (row, "enabled", json_integer (enabled));
      json_object_set_new (row, "delivery",
                           json_string (tmp_deliv ? tmp_deliv : "push"));
      if (tmp_flt)
        {
          json_t *filter_obj = json_loads (tmp_flt, 0, NULL);


          if (filter_obj)
            {
              json_object_set_new (row, "filter", filter_obj);
            }
        }
      json_array_append_new (items, row);
    }
  db_res_finalize (res);

  json_t *resp = json_object ();


  json_object_set_new (resp, "items", items);
  send_response_ok_take (ctx, root, "subscribe.list", &resp);
  return 0;
}


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


int
cmd_subscribe_catalog (client_ctx_t *ctx, json_t *root)
{
  json_t *topics = catalog_topics_json ();


  if (!topics)
    {
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                           "Allocation failure");
      return 0;
    }
  json_t *data = json_object ();


  json_object_set_new (data, "topics", topics);


  if (!data)
    {
      json_decref (topics);
      send_response_error (ctx, root, ERR_PLANET_NOT_FOUND,
                           "Allocation failure");
      return 0;
    }
  send_response_ok_take (ctx, root, "subscribe.catalog_v1", &data);
  return 0;
}


static int
require_admin (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0 ||
      auth_player_get_type (ctx->player_id) != PLAYER_TYPE_SYSOP)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Permission denied", NULL);
      return 0;
    }
  return 1;
}


int
cmd_admin_notice (client_ctx_t *ctx, json_t *root)
{
  if (!require_admin (ctx, root))
    {
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Missing or invalid data for admin.notice");
      return 0;
    }

  const char *title = json_string_value (json_object_get (data, "title"));
  const char *body = json_string_value (json_object_get (data, "body"));
  const char *severity =
    json_string_value (json_object_get (data, "severity"));
  json_int_t ttl_seconds_json = 0;
  json_t *ttl_json = json_object_get (data, "ttl_seconds");


  if (ttl_json && json_is_integer (ttl_json))
    {
      ttl_seconds_json = json_integer_value (ttl_json);
    }

  if (!title || !body)
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD,
                           "Title and body are required for admin.notice");
      return 0;
    }

  broadcast_system_notice (0, title, body, severity ? severity : "info",
                           time (NULL), ttl_seconds_json);
  send_response_ok_take (ctx, root, "admin.notice", NULL);
  return 0;
}


int
cmd_admin_shutdown_warning (client_ctx_t *ctx, json_t *root)
{
  if (!require_admin (ctx, root))
    {
      return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!data || !json_is_object (data))
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Missing or invalid data for admin.shutdown_warning");
      return 0;
    }

  const char *body = json_string_value (json_object_get (data, "body"));
  json_int_t countdown_seconds_json = 0;
  json_t *countdown_json = json_object_get (data, "countdown_seconds");


  if (countdown_json && json_is_integer (countdown_json))
    {
      countdown_seconds_json = json_integer_value (countdown_json);
    }

  if (!body || countdown_seconds_json <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD,
                           "Body and countdown_seconds (positive integer) are required for admin.shutdown_warning");
      return 0;
    }

  // Use a fixed title and critical severity for shutdown warnings
  broadcast_system_notice (0,
                           "SERVER SHUTDOWN IMMINENT",
                           body,
                           "critical", time (NULL), countdown_seconds_json);
  send_response_ok_take (ctx, root, "admin.shutdown_warning", NULL);
  return 0;
}

