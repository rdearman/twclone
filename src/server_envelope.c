#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <fcntl.h>
/* local includes */
#include "server_envelope.h"
#include "schemas.h"
#include "server_config.h"
#include "server_log.h"
#include "s2s_transport.h"
#include "common.h"             /* now_iso8601, strip_ansi */
int toss;


void
send_response_ok_borrow (client_ctx_t *ctx,
                         json_t *req,
                         const char *type,
                         const json_t *data)
{
  /* Borrow contract: callee will take its own ref if it needs one */
  json_t *payload = data ? json_incref ((json_t *)data) : NULL;
  /* Reuse the take API so there is one implementation */
  send_response_ok_take (ctx, req, type, &payload);
  /* payload is NULL now */
}


void
send_response_ok_take (client_ctx_t *ctx,
                       json_t *req,
                       const char *type,
                       json_t **pdata)
{
  json_t *data = (pdata ? *pdata : NULL);
  if (pdata)
    {
      *pdata = NULL;
    }

  if (ctx && ctx->captured_envelopes)
    {
      json_t *resp = json_object ();


      /* ... build envelope ... */

      if (data)
        {
          json_object_set_new (resp, "data", data); /* steal */
        }
      else
        {
          json_object_set_new (resp, "data", json_null ());
        }

      /* ... */
      json_array_append_new (ctx->captured_envelopes, resp);
      return;
    }

  /* send_enveloped_ok must follow the same rule: it STEALS 'data' if non-NULL */
  send_enveloped_ok (ctx->fd, req, type, data);
  if (data)
    {
      json_decref (data); /* FIX: Manually fulfill the "steal" contract */
    }
}


/* Recursively strip ANSI from all JSON strings in 'node'. */
static void
sanitize_json_strings (json_t *node)
{
  if (!node)
    {
      return;
    }
  if (json_is_string (node))
    {
      const char *s = json_string_value (node);


      if (!s)
        {
          return;
        }
      char buf[4096];


      strip_ansi (buf, s, sizeof (buf));
      if (strcmp (buf, s) != 0)
        {
          (void) json_string_set (node, buf);
        }
      return;
    }
  if (json_is_array (node))
    {
      size_t i; json_t *it;


      json_array_foreach (node, i, it) sanitize_json_strings (it);
      return;
    }
  if (json_is_object (node))
    {
      void *iter = json_object_iter (node);


      while (iter)
        {
          sanitize_json_strings (json_object_iter_value (iter));
          iter = json_object_iter_next (node, iter);
        }
      return;
    }
}


// Provided by server_loop.c
extern void loop_get_supported_commands (const cmd_desc_t **out_tbl,
                                         size_t *out_n);


static void
get_cmd_table (const cmd_desc_t **out_tbl, size_t *out_n)
{
  loop_get_supported_commands (out_tbl, out_n);
}


static int
have_cmd (const char *needle)
{
  const cmd_desc_t *tbl = NULL;
  size_t n = 0;
  get_cmd_table (&tbl, &n);
  for (size_t i = 0; i < n; i++)
    {
      if (tbl[i].name && strcmp (tbl[i].name, needle) == 0)
        {
          return 1;
        }
    }
  return 0;
}


static json_t *
collect_schema_names (void)
{
  const cmd_desc_t *tbl = NULL;
  size_t n = 0;
  get_cmd_table (&tbl, &n);
  json_t *set = json_object ();


  for (size_t i = 0; i < n; i++)
    {
      const char *cmd = tbl[i].name;


      if (!cmd)
        {
          continue;
        }
      const char *dot = strchr (cmd, '.');


      if (!dot || dot == cmd)
        {
          continue;
        }
      size_t len = (size_t) (dot - cmd);
      char buf[64];


      if (len >= sizeof (buf))
        {
          len = sizeof (buf) - 1;
        }
      memcpy (buf, cmd, len);
      buf[len] = '\0';
      json_object_set_new (set, buf, json_true ());
    }
  json_t *arr = json_array ();
  void *it = json_object_iter (set);


  while (it)
    {
      json_array_append_new (arr, json_string (json_object_iter_key (it)));
      it = json_object_iter_next (set, it);
    }
  json_decref (set);
  return arr;
}


int
cmd_system_cmd_list (client_ctx_t *ctx, json_t *root)
{
  const cmd_desc_t *tbl = NULL;
  size_t n = 0;
  get_cmd_table (&tbl, &n);
  json_t *arr = json_array ();


  for (size_t i = 0; i < n; i++)
    {
      if (!tbl[i].name)
        {
          continue;
        }
      json_t *o = json_object ();


      json_object_set_new (o, "cmd", json_string (tbl[i].name));
      if (tbl[i].summary && tbl[i].summary[0])
        {
          json_object_set_new (o, "summary", json_string (tbl[i].summary));
        }
      json_array_append_new (arr, o);
    }

  json_t *data = json_object ();


  json_object_set_new (data, "commands", arr); /* FIX: Use _new to steal ownership */
  json_object_set_new (data, "count", json_integer ((int) n));

  send_response_ok_take (ctx, root, "system.cmd_list", &data);
  return 0;
}


int
cmd_system_describe_schema (client_ctx_t *ctx, json_t *root)
{
  const char *name = NULL;
  const char *schema_type = NULL;
  json_t *d = json_object_get (root, "data");
  if (d)
    {
      name = json_string_value (json_object_get (d, "name"));
      schema_type = json_string_value (json_object_get (d, "type"));
    }
  if (!name || !*name)
    {
      send_response_error (ctx, root, ERR_MAINTENANCE_MODE, "Missing 'name'");
      return 0;
    }
  if (!have_cmd (name))
    {
      send_response_error (ctx, root, REF_NOT_IN_SECTOR, "Unknown command");
      return 0;
    }
  if (!schema_type || (strcasecmp (schema_type,
                                   "command") != 0 && strcasecmp (schema_type,
                                                                  "event") !=
                       0))
    {
      schema_type = "command";
    }

  //LOGD ("Debug: Fetching schema for '%s'", name);
  JSON_AUTO schema_obj = schema_get (name);


  if (!schema_obj)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "Schema not found");
      return 0;
    }
  JSON_AUTO response_data = json_object ();


  json_object_set_new (response_data, "name", json_string (name));
  json_object_set_new (response_data, "type", json_string (schema_type));
  json_object_set_new (response_data, "schema", json_incref (schema_obj));

  send_response_ok_take (ctx, root, "system.schema", &response_data);
  return 0;
}


int
cmd_system_schema_list (client_ctx_t *ctx, json_t *root)
{
  json_t *data = json_object ();
  json_object_set_new (data, "available", collect_schema_names ());
  send_response_ok_take (ctx, root, "system.schema_list", &data);
  return 0;
}


static int
send_all (int fd, const char *buf, size_t len)
{
  size_t off = 0;
  while (off < len)
    {
      ssize_t n = send (fd, buf + off, len - off, MSG_NOSIGNAL);


      if (n < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          return -1;
        }
      off += (size_t) n;
    }
  return 0;
}


const char *
next_msg_id (void)
{
  static __thread char buf[32];
  static unsigned long long seq = 0;
  unsigned long long n = __sync_add_and_fetch (&seq, 1ULL);
  snprintf (buf, sizeof (buf), "srv-%llu", n);
  return buf;
}


void
iso8601_utc (char out[32])
{
  time_t now = time (NULL);
  struct tm tm;
  gmtime_r (&now, &tm);
  strftime (out, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
}


json_t *
make_base_envelope (json_t *req, const char *type)
{
  char ts[32]; iso8601_utc (ts);
  json_t *meta = json_object ();
  json_object_set_new (meta, "id", json_string (next_msg_id ()));
  json_object_set_new (meta, "ts", json_string (ts));
  json_t *env = json_object ();


  json_object_set_new (env, "type", json_string (type));
  json_object_set_new (env, "meta", meta);
  if (req && json_is_object (req))
    {
      json_t *m = json_object_get (req, "meta");


      if (json_is_object (m))
        {
          json_t *rid = json_object_get (m, "id");


          if (json_is_string (rid))
            {
              json_object_set_new (env, "in_reply_to",
                                   json_string (json_string_value (rid)));
            }
        }
    }
  return env;
}


void
send_all_json (int fd, json_t *obj)
{
  if (g_ctx_for_send) {
    g_ctx_for_send->responses_sent++;
  }
  char *s = json_dumps (obj, JSON_COMPACT);
  if (s)
    {
      (void) send_all (fd, s, strlen (s)); (void) send_all (fd, "\n", 1);
      free (s);
    }
}


void
send_error_json (int fd, int code, const char *msg)
{
  json_t *o = json_object ();
  json_object_set_new (o, "status", json_string ("error"));
  json_object_set_new (o, "code", json_integer (code));
  json_object_set_new (o, "error", json_string (msg));
  send_all_json (fd, o);
  json_decref (o);
}


void
send_ok_json (int fd, json_t *data)
{
  json_t *o = json_object ();
  json_object_set_new (o, "status", json_string ("OK"));
  if (data)
    {
      json_object_set (o, "data", data);
    }
  send_all_json (fd, o);
  json_decref (o);
}


static json_t *
make_default_meta (void)
{
  json_t *rate = json_object ();
  json_object_set_new (rate, "limit", json_integer (60));
  json_object_set_new (rate, "remaining", json_integer (60));
  json_object_set_new (rate, "reset", json_integer (60));
  json_t *meta = json_object ();


  json_object_set_new (meta, "rate_limit", rate);
  return meta;
}


/* send_enveloped_ok NOW BORROWS data */
void
send_enveloped_ok (int fd, json_t *req, const char *type, json_t *data)
{
  json_t *resp = json_object ();
  json_object_set_new (resp, "id", json_string ("srv-ok"));
  if (req && json_is_object (req))
    {
      json_t *jid = json_object_get (req, "id");


      if (json_is_string (jid))
        {
          json_object_set_new (resp, "reply_to",
                               json_string (json_string_value (jid)));
        }
    }
  char tsbuf[32]; iso8601_utc (tsbuf);


  json_object_set_new (resp, "ts", json_string (tsbuf));
  json_object_set_new (resp, "status", json_string ("ok"));
  json_object_set_new (resp, "type", json_string (type ? type : "ok"));
  if (data)
    {
      json_object_set (resp, "data", data);
    }
  else
    {
      json_object_set_new (resp, "data", json_null ());
    }
  json_object_set_new (resp, "error", json_null ());
  json_object_set_new (resp, "meta", make_default_meta ());
  if (type && strcmp (type, "system.schema") != 0)
    {
      sanitize_json_strings (resp);
    }
  char *s = json_dumps (resp, JSON_COMPACT);


  if (s)
    {
      send (fd, s, strlen (s), MSG_NOSIGNAL); send (fd, "\n", 1, MSG_NOSIGNAL);
      free (s);
    }
  json_decref (resp);
}


void
send_enveloped_error (int fd,
                      json_t *req,
                      int code,
                      const char *message)
{
  json_t *resp = json_object ();
  json_object_set_new (resp, "id", json_string ("srv-err"));
  if (req && json_is_object (req))
    {
      json_t *jid = json_object_get (req, "id");


      if (json_is_string (jid))
        {
          json_object_set_new (resp, "reply_to",
                               json_string (json_string_value (jid)));
        }
    }
  char tsbuf[32]; iso8601_utc (tsbuf);


  json_object_set_new (resp, "ts", json_string (tsbuf));
  json_object_set_new (resp, "status", json_string ("error"));
  json_object_set_new (resp, "type", json_string ("error"));
  json_t *err = json_object ();


  json_object_set_new (err, "code", json_integer (code));
  json_object_set_new (err, "message",
                       json_string (message ? message : "Error"));
  json_object_set_new (resp, "error", err);
  json_object_set_new (resp, "data", json_null ());
  json_object_set_new (resp, "meta", make_default_meta ());
  sanitize_json_strings (resp);
  char *s = json_dumps (resp, JSON_COMPACT);


  if (s)
    {
      send (fd, s, strlen (s), MSG_NOSIGNAL); send (fd, "\n", 1, MSG_NOSIGNAL);
      free (s);
    }
  json_decref (resp);
}


void
send_enveloped_refused (int fd,
                        json_t *req,
                        int code,
                        const char *msg,
                        json_t *data_opt)
{
  json_t *resp = json_object ();
  json_object_set_new (resp, "id", json_string ("srv-refuse"));
  if (req && json_is_object (req))
    {
      json_t *jid = json_object_get (req, "id");


      if (json_is_string (jid))
        {
          json_object_set_new (resp, "reply_to",
                               json_string (json_string_value (jid)));
        }
    }
  char tsbuf[32]; iso8601_utc (tsbuf);


  json_object_set_new (resp, "ts", json_string (tsbuf));
  json_object_set_new (resp, "status", json_string ("refused"));
  json_object_set_new (resp, "type", json_string ("error"));
  json_t *err = json_object ();


  json_object_set_new (err, "code", json_integer (code));
  json_object_set_new (err, "message", json_string (msg ? msg : ""));


  if (data_opt)
    {
      json_object_set (err, "data", data_opt);
    }
  json_object_set_new (resp, "error", err);
  json_object_set_new (resp, "data", json_null ());
  json_object_set_new (resp, "meta", make_default_meta ());
  sanitize_json_strings (resp);
  char *s = json_dumps (resp, JSON_COMPACT);


  if (s)
    {
      send (fd, s, strlen (s), MSG_NOSIGNAL); send (fd, "\n", 1, MSG_NOSIGNAL);
      free (s);
    }
  json_decref (resp);
}


void
send_response_ok (client_ctx_t *ctx, json_t *req, const char *type,
                  json_t *data)
{
  if (ctx && ctx->captured_envelopes)
    {
      json_t *resp = json_object ();


      if (data)
        {
          json_object_set (resp, "data", data);
        }
      else
        {
          json_object_set_new (resp, "data", json_null ());
        }
      if (req && json_is_object (req))
        {
          json_t *jid = json_object_get (req, "id");


          if (json_is_string (jid))
            {
              json_object_set_new (resp, "reply_to",
                                   json_string (json_string_value (jid)));
            }
        }
      char tsbuf[32]; iso8601_utc (tsbuf);


      json_object_set_new (resp, "ts", json_string (tsbuf));
      json_object_set_new (resp, "status", json_string ("ok"));
      json_object_set_new (resp, "type", json_string (type ? type : "ok"));
      json_object_set_new (resp, "error", json_null ());
      json_object_set_new (resp, "meta", make_default_meta ());
      if (type && strcmp (type, "system.schema") != 0)
        {
          sanitize_json_strings (resp);
        }
      json_array_append_new (ctx->captured_envelopes, resp);
    }
  else
    {
      send_enveloped_ok (ctx->fd, req, type, data);
    }
}


void
send_response_error (client_ctx_t *ctx, json_t *req, int code, const char *msg)
{
  if (ctx && ctx->captured_envelopes)
    {
      json_t *resp = json_object ();


      if (req && json_is_object (req))
        {
          json_t *jid = json_object_get (req, "id");


          if (json_is_string (jid))
            {
              json_object_set_new (resp, "reply_to",
                                   json_string (json_string_value (jid)));
            }
        }
      char tsbuf[32]; iso8601_utc (tsbuf);


      json_object_set_new (resp, "ts", json_string (tsbuf));
      json_object_set_new (resp, "status", json_string ("error"));
      json_object_set_new (resp, "type", json_string ("error"));
      json_t *err = json_object ();


      json_object_set_new (err, "code", json_integer (code));
      json_object_set_new (err, "message", json_string (msg ? msg : ""));


      json_object_set_new (resp, "error", err);
      json_object_set_new (resp, "data", json_null ());
      json_object_set_new (resp, "meta", make_default_meta ());
      sanitize_json_strings (resp);
      json_array_append_new (ctx->captured_envelopes, resp);
    }
  else
    {
      send_enveloped_error (ctx->fd, req, code, msg);
    }
}


void
send_response_refused (client_ctx_t *ctx,
                       json_t *req,
                       int code,
                       const char *msg,
                       json_t *data_opt)
{
  if (ctx && ctx->captured_envelopes)
    {
      json_t *resp = json_object ();


      if (req && json_is_object (req))
        {
          json_t *jid = json_object_get (req, "id");


          if (json_is_string (jid))
            {
              json_object_set_new (resp, "reply_to",
                                   json_string (json_string_value (jid)));
            }
        }
      char tsbuf[32]; iso8601_utc (tsbuf);


      json_object_set_new (resp, "ts", json_string (tsbuf));
      json_object_set_new (resp, "status", json_string ("refused"));
      json_object_set_new (resp, "type", json_string ("error"));
      json_t *err = json_object ();


      json_object_set_new (err, "code", json_integer (code));
      json_object_set_new (err, "message", json_string (msg ? msg : ""));


      if (data_opt)
        {
          json_object_set (err, "data", data_opt);
        }
      json_object_set_new (resp, "error", err);
      json_object_set_new (resp, "data", json_null ());
      json_object_set_new (resp, "meta", make_default_meta ());
      sanitize_json_strings (resp);
      json_array_append_new (ctx->captured_envelopes, resp);
    }
  else
    {
      send_enveloped_refused (ctx->fd, req, code, msg, data_opt);
    }
}


////////////////////////   S2S SECTION //////////////////////////////


// server_envelope.c


/* ---------- tiny helpers ---------- */
static int
urand_bytes (void *buf, size_t n)
{
  int fd = open ("/dev/urandom", O_RDONLY);
  if (fd < 0)
    {
      return -1;
    }
  size_t off = 0;


  while (off < n)
    {
      ssize_t r = read (fd, (char *) buf + off, n - off);


      if (r <= 0)
        {
          close (fd);
          return -1;
        }
      off += (size_t) r;
    }
  close (fd);
  return 0;
}


/* UUID v4 (lowercase, canonical 36 chars). Caller must free(). */
static char *
gen_uuid_v4 (void)
{
  unsigned char b[16];
  if (urand_bytes (b, sizeof b) != 0)
    {
      return NULL;
    }
  /* Set version (4) and variant (10) bits */
  b[6] = (unsigned char) ((b[6] & 0x0F) | 0x40);
  b[8] = (unsigned char) ((b[8] & 0x3F) | 0x80);
  static const char *hex = "0123456789abcdef";
  char *s = (char *) malloc (37);


  if (!s)
    {
      return NULL;
    }
  int p = 0;


  for (int i = 0; i < 16; ++i)
    {
      s[p++] = hex[(b[i] >> 4) & 0xF];
      s[p++] = hex[b[i] & 0xF];
      if (i == 3 || i == 5 || i == 7 || i == 9)
        {
          s[p++] = '-';
        }
    }
  s[p] = '\0';
  return s;
}


/* Borrow-or-empty: returns a NEW ref you must decref (never NULL). */
static json_t *
borrow_or_empty_obj (json_t *maybe_obj)
{
  if (maybe_obj && json_is_object (maybe_obj))
    {
      json_incref (maybe_obj);
      return maybe_obj;
    }
  return json_object ();
}


/* ---------- envelope builders (public) ---------- */
json_t *
s2s_make_env (const char *type, const char *src, const char *dst,
              json_t *payload)
{
  if (!type || !*type || !src || !*src || !dst || !*dst)
    {
      return NULL;
    }
  char *id = gen_uuid_v4 ();


  if (!id)
    {
      return NULL;
    }
  time_t now = time (NULL);
  json_t *pl = borrow_or_empty_obj (payload);
  /* NOTE: No "auth" here. Transport layer will add it before sending. */
  json_t *env = json_object ();


  json_object_set_new (env, "v", json_integer (1));
  json_object_set_new (env, "type", json_string (type));
  json_object_set_new (env, "id", json_string (id));
  json_object_set_new (env, "ts", json_integer ((json_int_t) now));
  json_object_set_new (env, "src", json_string (src));
  json_object_set_new (env, "dst", json_string (dst));
  json_object_set (env, "payload", pl);


  json_decref (pl);
  free (id);
  return env;                   /* caller must json_decref */
}


json_t *
s2s_make_ack (const char *src, const char *dst, const char *ack_of,
              json_t *payload)
{
  if (!src || !*src || !dst || !*dst || !ack_of || !*ack_of)
    {
      return NULL;
    }
  char *id = gen_uuid_v4 ();


  if (!id)
    {
      return NULL;
    }
  time_t now = time (NULL);
  json_t *pl = borrow_or_empty_obj (payload);
  json_t *env = json_object ();


  json_object_set_new (env, "v", json_integer (1));
  json_object_set_new (env, "type", json_string ("s2s.ack"));
  json_object_set_new (env, "id", json_string (id));
  json_object_set_new (env, "ts", json_integer ((json_int_t) now));
  json_object_set_new (env, "src", json_string (src));
  json_object_set_new (env, "dst", json_string (dst));
  json_object_set_new (env, "ack_of", json_string (ack_of));
  json_object_set (env, "payload", pl);


  json_decref (pl);
  free (id);
  return env;
}


json_t *
s2s_make_error (const char *src, const char *dst,
                const char *ack_of, const char *code,
                const char *message, json_t *details /* NULL ok */ )
{
  if (!src || !*src || !dst || !*dst || !ack_of || !*ack_of || !code
      || !*code)
    {
      return NULL;
    }
  char *id = gen_uuid_v4 ();


  if (!id)
    {
      return NULL;
    }
  time_t now = time (NULL);
  json_t *det = details ? json_incref (details), details : json_object ();
  json_t *err = json_object ();


  json_object_set_new (err, "code", json_string (code));
  json_object_set_new (err, "message", json_string (message ? message : ""));
  json_object_set (err, "details", det);


  json_decref (det);
  json_t *env = json_object ();


  json_object_set_new (env, "v", json_integer (1));
  json_object_set_new (env, "type", json_string ("s2s.error"));
  json_object_set_new (env, "id", json_string (id));
  json_object_set_new (env, "ts", json_integer ((json_int_t) now));
  json_object_set_new (env, "src", json_string (src));
  json_object_set_new (env, "dst", json_string (dst));
  json_object_set_new (env, "ack_of", json_string (ack_of));
  json_object_set (env, "error", err);
  json_object_set_new (env, "payload", json_object ());    /* keep payload an object for uniformity */


  json_decref (err);
  free (id);
  return env;
}


/* --------- light parsing helpers --------- */
const char *
s2s_env_type (json_t *env)
{
  json_t *x = json_object_get (env, "type");
  return json_is_string (x) ? json_string_value (x) : NULL;
}


const char *
s2s_env_id (json_t *env)
{
  json_t *x = json_object_get (env, "id");
  return json_is_string (x) ? json_string_value (x) : NULL;
}


/* Borrowed ref; do NOT decref the returned pointer. */
json_t *
s2s_env_payload (json_t *env)
{
  json_t *x = json_object_get (env, "payload");
  return json_is_object (x) ? x : NULL;
}


/* --------- minimal envelope validation ---------
 * Checks presence & basic types for v/type/id/ts/src/dst/payload.
 * Returns 0 on OK; nonzero on error and sets *why to a malloc'd message.
 * Caller must free(*why) if non-NULL.
 */
static char *
dupmsg (const char *m)
{
  size_t n = strlen (m) + 1;
  char *p = (char *) malloc (n);
  if (p)
    {
      memcpy (p, m, n);
    }
  return p;
}


int
s2s_env_validate_min (json_t *env, char **why)
{
  if (why)
    {
      *why = NULL;
    }
  if (!env || !json_is_object (env))
    {
      if (why)
        {
          *why = dupmsg ("envelope not an object");
        }
      return -1;
    }
  json_t *v = json_object_get (env, "v");
  json_t *ty = json_object_get (env, "type");
  json_t *id = json_object_get (env, "id");
  json_t *ts = json_object_get (env, "ts");
  json_t *src = json_object_get (env, "src");
  json_t *dst = json_object_get (env, "dst");
  json_t *pl = json_object_get (env, "payload");


  if (!json_is_integer (v) || json_integer_value (v) != 1)
    {
      if (why)
        {
          *why = dupmsg ("v!=1");
        }
      return -2;
    }
  if (!json_is_string (ty) || json_string_length (ty) == 0)
    {
      if (why)
        {
          *why = dupmsg ("type missing");
        }
      return -3;
    }
  if (!json_is_string (id) || json_string_length (id) == 0)
    {
      if (why)
        {
          *why = dupmsg ("id missing");
        }
      return -4;
    }
  if (!json_is_integer (ts) || json_integer_value (ts) <= 0)
    {
      if (why)
        {
          *why = dupmsg ("ts invalid");
        }
      return -5;
    }
  if (!json_is_string (src) || json_string_length (src) == 0)
    {
      if (why)
        {
          *why = dupmsg ("src missing");
        }
      return -6;
    }
  if (!json_is_string (dst) || json_string_length (dst) == 0)
    {
      if (why)
        {
          *why = dupmsg ("dst missing");
        }
      return -7;
    }
  if (!json_is_object (pl))
    {
      if (why)
        {
          *why = dupmsg ("payload not object");
        }
      return -8;
    }
  return 0;
}


/* --------- thin wrappers over transport --------- */
int
s2s_send_env (s2s_conn_t *c, json_t *env, int timeout_ms)
{
  /* DO NOT add auth here; transport will inject + sign. */
  return s2s_send_json (c, env, timeout_ms);
}


int
s2s_recv_env (s2s_conn_t *c, json_t **out_env, int timeout_ms)
{
  if (out_env)
    {
      *out_env = NULL;
    }
  json_t *root = NULL;
  int rc = s2s_recv_json (c, &root, timeout_ms);


  if (rc != 0)
    {
      return rc;
    }
  char *why = NULL;
  int vrc = s2s_env_validate_min (root, &why);


  if (vrc != 0)
    {
      if (why)
        {
          //fprintf (stderr, "[s2s] envelope min-validate failed: %s\n", why);
          free (why);
        }
      json_decref (root);
      return -1;
    }
  if (out_env)
    {
      *out_env = root;
    }
  else
    {
      json_decref (root);
    }
  return 0;
}


/**
 * @brief Safely retrieves an integer from a JSON object/array using a dot-separated path.
 *
 * This function is robust against missing keys or incorrect types at any level of the path.
 *
 * @param root The root JSON object (command envelope).
 * @param path The dot-separated path to the integer (e.g., "data.confirmation").
 * @param result Pointer to store the extracted integer value.
 * @return 0 on success, -1 on failure (path not found, wrong type, or invalid structure).
 */
int
j_get_integer (json_t *root, const char *path, int *result)
{
  if (!root || !path || !result || *path == '\0')
    {
      LOGE ( "j_get_integer: Invalid input parameters.\n");
      return -1;
    }
  // Make a mutable copy of the path for strtok_r to work on.
  char *path_copy = strdup (path);


  if (!path_copy)
    {
      perror ("j_get_integer: strdup failed");
      return -1;                // Allocation failure
    }
  char *saveptr;
  char *token;
  json_t *current = root;


  // Use strtok_r to safely tokenize the path by '.'
  for (token = strtok_r (path_copy, ".", &saveptr);
       token != NULL; token = strtok_r (NULL, ".", &saveptr))
    {
      // 1. Ensure the current node is a JSON object before looking up a key
      if (!current || !json_is_object (current))
        {
          LOGE (
            "j_get_integer: Path segment '%s' expected an object, but found something else or NULL.\n",
            token);
          free (path_copy);
          return -1;
        }
      // 2. Find the child node for the current token
      json_t *next = json_object_get (current, token);


      if (!next)
        {
          LOGD ( "j_get_integer: Key '%s' not found at this level.\n",
                 token);
          free (path_copy);
          return -1;
        }
      // 3. Check if this is the final token in the path
      if (saveptr == NULL || *saveptr == '\0')
        {
          // We are on the final key: check type and extract value
          if (json_is_integer (next))
            {
              *result = (int) json_integer_value (next);
              free (path_copy);
              return 0;         // Success!
            }
          else
            {
              LOGE (
                "j_get_integer: Final key '%s' found, but value is not an integer.\n",
                token);
              free (path_copy);
              return -1;        // Wrong type
            }
        }
      // 4. Not the final token, move to the next level for traversal
      current = next;
    }
  // Should only be reached if the path was malformed (e.g., ended in a dot)
  free (path_copy);
  LOGE (
    "j_get_integer: Path traversal completed without finding a final value.\n");
  return -1;
}

