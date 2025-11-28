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
#include "server_envelope.h"
#include "server_config.h"
#include "s2s_transport.h"
#include "common.h"		/* now_iso8601, strip_ansi */

int toss;

/* Recursively strip ANSI from all JSON strings in 'node'. */
static void
sanitize_json_strings (json_t *node)
{
  if (!node)
    return;

  if (json_is_string (node))
    {
      const char *s = json_string_value (node);
      if (!s)
	return;
      char buf[4096];
      strip_ansi (buf, s, sizeof (buf));
      if (strcmp (buf, s) != 0)
	{
	  (void) json_string_set (node, buf);	/* json_t* is fine here */
	}
      return;
    }

  if (json_is_array (node))
    {
      size_t i;
      json_t *it;
      json_array_foreach (node, i, it)
      {
	sanitize_json_strings (it);
      }
      return;
    }

  if (json_is_object (node))
    {
      void *iter = json_object_iter (node);
      while (iter)
	{
	  json_t *v = json_object_iter_value (iter);	/* ← only iter */
	  sanitize_json_strings (v);
	  iter = json_object_iter_next (node, iter);	/* ← object + iter */
	}
      return;
    }
}



// --- Weak fallback so linking succeeds even if server_loop.c doesn't define it ---
// If a strong version exists in server_loop.c, the linker will prefer that one.
__attribute__((weak))
     void loop_get_supported_commands (const cmd_desc_t **out_tbl,
				       size_t *out_n)
{
  static const cmd_desc_t k_supported_cmds[] = {
    // --- session / system ---
    {"session.hello", "Handshake / hello"},
    {"session.ping", "Ping"},
    {"session.disconnect", "Client disconnect"},

    {"system.schema_list", "List schema namespaces"},
    {"system.describe_schema", "Describe commands in a schema"},
    {"system.capabilities", "Feature flags, schemas, counts"},
    {"system.cmd_list", "Flat list of all commands"},

    // --- auth ---
    {"auth.login", "Authenticate"},
    {"auth.logout", "Log out"},
    {"auth.mfa.totp.verify", "Second-factor code"},
    {"auth.register", "Create a new player"},

    // --- players / ship ---
    {"player.my_info", "Current player info"},
    {"player.list_online", "List online players"},
    {"ship.info", "Ship information"},

    // --- sector / movement ---
    {"sector.info", "Describe current sector"},
    {"sector.set_beacon", "Set or clear sector beacon"},
    {"move.warp", "Warp to sector"},
    {"move.scan", "Scan adjacent sectors"},
    {"move.pathfind", "Find path between sectors"},

    // --- INSERT THESE ENTRIES (Alphabetically) ---
    {"trade.accept", "Accept player trade offer"},
    {"trade.buy", "Buy commodity from port"},
    {"trade.cancel", "Cancel player trade offer"},
    {"trade.history", "View trade history (paginated)"},
    {"trade.jettison", "Jettison cargo"},
    {"trade.offer", "Create player trade offer"},
    {"trade.port_info", "Port info (alias to sector.info/port)"},
    {"trade.quote", "Get buy/sell quote"},
    {"trade.sell", "Sell commodity to port"},
    {"trade.port_info", "Port prices/stock in sector"},
  };

  if (out_tbl)
    *out_tbl = k_supported_cmds;
  if (out_n)
    *out_n = sizeof (k_supported_cmds) / sizeof (k_supported_cmds[0]);
}


// Provided by server_loop.c
extern void loop_get_supported_commands (const cmd_desc_t ** out_tbl,
					 size_t *out_n);

// ---- Helpers ----
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
	return 1;
    }
  return 0;
}

static json_t *
collect_schema_names (void)
{
  const cmd_desc_t *tbl = NULL;
  size_t n = 0;
  get_cmd_table (&tbl, &n);

  // use an object as a set of schema prefixes
  json_t *set = json_object ();
  for (size_t i = 0; i < n; i++)
    {
      const char *cmd = tbl[i].name;
      if (!cmd)
	continue;
      const char *dot = strchr (cmd, '.');
      if (!dot || dot == cmd)
	continue;
      size_t len = (size_t) (dot - cmd);
      if (len == 0)
	continue;

      char buf[64];
      if (len >= sizeof (buf))
	len = sizeof (buf) - 1;
      memcpy (buf, cmd, len);
      buf[len] = '\0';

      json_object_set_new (set, buf, json_true ());
    }

  // convert set → array
  json_t *arr = json_array ();
  void *it = json_object_iter (set);
  while (it)
    {
      const char *key = json_object_iter_key (it);
      json_array_append_new (arr, json_string (key));
      it = json_object_iter_next (set, it);
    }
  json_decref (set);
  return arr;
}

// ----------------- Handlers -----------------

// system.cmd_list  →  { type:"system.cmd_list", data:{ commands:[{cmd,summary?},...], count:n } }
int
cmd_system_cmd_list (client_ctx_t *ctx, json_t *root)
{
  (void) ctx;			// used via ctx->fd below
  const cmd_desc_t *tbl = NULL;
  size_t n = 0;
  get_cmd_table (&tbl, &n);

  json_t *arr = json_array ();
  for (size_t i = 0; i < n; i++)
    {
      if (!tbl[i].name)
	continue;
      json_t *o = json_object ();
      json_object_set_new (o, "cmd", json_string (tbl[i].name));
      if (tbl[i].summary && tbl[i].summary[0])
	json_object_set_new (o, "summary", json_string (tbl[i].summary));
      json_array_append_new (arr, o);
    }

  json_t *data = json_pack ("{s:o, s:i}", "commands", arr, "count", (int) n);
  send_enveloped_ok (ctx->fd, root, "system.cmd_list", data);
  return 0;
}

// system.describe_schema {name, type} → { type:"system.schema", data:{ name, type, schema } }
int
cmd_system_describe_schema (client_ctx_t *ctx, json_t *root)
{
  const char *name = NULL;
  const char *schema_type = NULL;	// "command" or "event"
  json_t *d = json_object_get (root, "data");
  if (d)
    {
      name = json_string_value (json_object_get (d, "name"));
      schema_type = json_string_value (json_object_get (d, "type"));	// Assuming client sends "type"
    }

  if (!name || !*name)
    {
      send_enveloped_error (ctx->fd, root, 1103,
			    "Missing 'name' in data payload");
      return 0;
    }
  // NEW: Check if command exists before trying to get schema
  if (!have_cmd (name))
    {
      send_enveloped_error (ctx->fd, root, 1400, "Unknown command");
      return 0;
    }
  // Default to "command" if type is not provided or recognized
  if (!schema_type
      || (strcasecmp (schema_type, "command") != 0
	  && strcasecmp (schema_type, "event") != 0))
    {
      schema_type = "command";
    }

  // 1. Retrieve the actual schema for the requested command/event
  json_t *schema_obj = schema_get (name);
  if (!schema_obj)
    {
      // If schema_get returns NULL, it means the schema is not found/implemented
      send_enveloped_error (ctx->fd, root, 1104,
			    "Schema not found or not implemented");
      return 0;
    }

  // 2. Construct the correct 'data' payload for the response
  json_t *response_data = json_object ();
  json_object_set_new (response_data, "name", json_string (name));
  json_object_set_new (response_data, "type", json_string (schema_type));
  json_object_set_new (response_data, "schema", schema_obj);	// schema_obj is a new reference from schema_get()

  // 3. Send with correct type "system.schema"
  send_enveloped_ok (ctx->fd, root, "system.schema", response_data);

  // response_data now owns schema_obj, so no separate decref for schema_obj here.
  // send_enveloped_ok will decref response_data (and thus schema_obj)
  return 0;
}

// system.schema_list → { type:"system.schema_list", data:{ available:[...] } }
int
cmd_system_schema_list (client_ctx_t *ctx, json_t *root)
{
  json_t *arr = collect_schema_names ();
  json_t *data = json_pack ("{s:o}", "available", arr);
  send_enveloped_ok (ctx->fd, root, "system.schema_list", data);
  return 0;
}


/////////////////////////

static int
send_all (int fd, const char *buf, size_t len)
{
  size_t off = 0;
  while (off < len)
    {
      ssize_t n = write (fd, buf + off, len - off);
      if (n < 0)
	{
	  if (errno == EINTR)
	    continue;
	  if (errno == EAGAIN || errno == EWOULDBLOCK)
	    {
	      // caller can decide whether to retry later; treat as partial send success
	      return -1;
	    }
	  return -1;
	}
      off += (size_t) n;
    }
  return 0;
}


/* Simple process-local counter for message ids. Thread-safe enough via GCC builtin. */
const char *
next_msg_id (void)
{
  static __thread char buf[32];
  static unsigned long long seq = 0;
#if defined(__GNUC__) || defined(__clang__)
  unsigned long long n = __sync_add_and_fetch (&seq, 1ULL);
#else
  // fallback (not thread-safe)
  unsigned long long n = ++seq;
#endif
  // "srv-<number>"
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
  char ts[32];
  iso8601_utc (ts);

  json_t *meta = json_object ();
  json_object_set_new (meta, "id", json_string (next_msg_id ()));
  json_object_set_new (meta, "ts", json_string (ts));

  json_t *env = json_object ();
  json_object_set_new (env, "type", json_string (type));
  json_object_set_new (env, "meta", meta);

  // If request has meta.id, echo it as in_reply_to (best-effort)
  const char *in_reply_to = NULL;
  if (req && json_is_object (req))
    {
      json_t *m = json_object_get (req, "meta");
      if (json_is_object (m))
	{
	  json_t *rid = json_object_get (m, "id");
	  if (json_is_string (rid))
	    in_reply_to = json_string_value (rid);
	}
    }
  if (in_reply_to)
    json_object_set_new (env, "in_reply_to", json_string (in_reply_to));

  return env;
}

void
send_all_json (int fd, json_t *obj)
{
  char *s = json_dumps (obj, JSON_COMPACT);
  if (s)
    {
      (void) send_all (fd, s, strlen (s));
      (void) send_all (fd, "\n", 1);
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
send_ok_json (int fd, json_t *data /* may be NULL */ )
{
  json_t *o = json_object ();
  json_object_set_new (o, "status", json_string ("OK"));
  if (data)
    json_object_set (o, "data", data);	/* borrowed */
  send_all_json (fd, o);
  json_decref (o);
}


/* ---- tiny helper: default rate-limit meta (no external ctx needed) ---- */
static json_t *
make_default_meta (void)
{
  json_t *rate = json_object ();
  json_object_set_new (rate, "limit", json_integer (60));
  json_object_set_new (rate, "remaining", json_integer (60));
  json_object_set_new (rate, "reset", json_integer (60));	// seconds

  json_t *meta = json_object ();
  json_object_set_new (meta, "rate_limit", rate);
  return meta;			// caller owns
}

/* -------------------- OK -------------------- */
void
send_enveloped_ok (int fd, json_t *req, const char *type, json_t *data)
{
  json_t *resp = json_object ();

  // ids
  json_object_set_new (resp, "id", json_string ("srv-ok"));
  const char *req_id = NULL;
  if (req && json_is_object (req))
    {
      json_t *jid = json_object_get (req, "id");
      if (json_is_string (jid))
	req_id = json_string_value (jid);
    }
  if (req_id)
    json_object_set_new (resp, "reply_to", json_string (req_id));

  // timestamp (ISO 8601, UTC)
  char tsbuf[32];
  iso8601_utc (tsbuf);
  json_object_set_new (resp, "ts", json_string (tsbuf));

  // status/type
  json_object_set_new (resp, "status", json_string ("ok"));
  json_object_set_new (resp, "type", json_string (type ? type : "ok"));

  // payload + error=null
  if (data)
    json_object_set_new (resp, "data", data);
  else
    json_object_set_new (resp, "data", json_null ());
  json_object_set_new (resp, "error", json_null ());

  // meta: default rate-limit info
  json_object_set_new (resp, "meta", make_default_meta ());

  // ✨ sanitize all strings (strip ANSI) before sending
  if (type && strcmp (type, "system.schema") != 0)
    {
      sanitize_json_strings (resp);
    }

  /* // write one line */
  /* char *s = json_dumps (resp, JSON_COMPACT); */
  /* // int toss; */
  /* if (s) */
  /*   { */
  /*     size_t len = strlen (s); */
  /*     if (len) */
  /* 	toss = write (fd, s, len); */
  /*     toss = write (fd, "\n", 1); */
  /*     free (s); */
  /*   } */
  /* json_decref (resp); */

// write one line
  char *s = json_dumps (resp, JSON_COMPACT);
  
  if (s)
    {
      size_t len = strlen (s);
      if (len)
        {
          // REPLACE write() with send() + MSG_NOSIGNAL
          // toss = write (fd, s, len);
          send (fd, s, len, MSG_NOSIGNAL); 
        }
      
      // REPLACE write() with send() + MSG_NOSIGNAL
      // toss = write (fd, "\n", 1);
      send (fd, "\n", 1, MSG_NOSIGNAL);

      free (s);
    }
  json_decref (resp);

}

/* -------------------- ERROR -------------------- */
void
send_enveloped_error (int fd, json_t *req, int code, const char *message)
{
  json_t *resp = json_object ();

  json_object_set_new (resp, "id", json_string ("srv-err"));
  const char *req_id = NULL;
  if (req && json_is_object (req))
    {
      json_t *jid = json_object_get (req, "id");
      if (json_is_string (jid))
	req_id = json_string_value (jid);
    }
  if (req_id)
    json_object_set_new (resp, "reply_to", json_string (req_id));

  char tsbuf[32];
  iso8601_utc (tsbuf);
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

  // ✨ sanitize all strings (strip ANSI) before sending
  sanitize_json_strings (resp);

  //  int toss;
  char *s = json_dumps (resp, JSON_COMPACT);
  if (s)
    {
      size_t len = strlen (s);
      if (len)
	toss = write (fd, s, len);
      toss = write (fd, "\n", 1);
      free (s);
    }
  json_decref (resp);
}


/* -------------------- REFUSED -------------------- */
void
send_enveloped_refused (int fd, json_t *req, int code, const char *msg,
			json_t *data_opt)
{
  json_t *resp = json_object ();

  json_object_set_new (resp, "id", json_string ("srv-refuse"));
  const char *req_id = NULL;
  if (req && json_is_object (req))
    {
      json_t *jid = json_object_get (req, "id");
      if (json_is_string (jid))
	req_id = json_string_value (jid);
    }
  if (req_id)
    json_object_set_new (resp, "reply_to", json_string (req_id));

  char tsbuf[32];
  iso8601_utc (tsbuf);
  json_object_set_new (resp, "ts", json_string (tsbuf));
  json_object_set_new (resp, "status", json_string ("refused"));
  json_object_set_new (resp, "type", json_string ("error"));	// legacy shape

  json_t *err = json_pack ("{s:i, s:s}",
			   "code", code,
			   "message", msg ? msg : "");
  if (data_opt)
    json_object_set (err, "data", data_opt);	// borrow
  json_object_set_new (resp, "error", err);

  json_object_set_new (resp, "data", json_null ());
  json_object_set_new (resp, "meta", make_default_meta ());

  // ✨ sanitize all strings (strip ANSI) before sending
  sanitize_json_strings (resp);


  char *s = json_dumps (resp, JSON_COMPACT);
  if (s)
    {
      size_t len = strlen (s);
      if (len)
	toss = write (fd, s, len);
      toss = write (fd, "\n", 1);
      free (s);
    }
  json_decref (resp);
}


////////////////////////   S2S SECTION //////////////////////////////

// server_envelope.c

/* ---------- tiny helpers ---------- */

static int
urand_bytes (void *buf, size_t n)
{
  int fd = open ("/dev/urandom", O_RDONLY);
  if (fd < 0)
    return -1;
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
    return NULL;
  /* Set version (4) and variant (10) bits */
  b[6] = (unsigned char) ((b[6] & 0x0F) | 0x40);
  b[8] = (unsigned char) ((b[8] & 0x3F) | 0x80);

  static const char *hex = "0123456789abcdef";
  char *s = (char *) malloc (37);
  if (!s)
    return NULL;
  int p = 0;
  for (int i = 0; i < 16; ++i)
    {
      s[p++] = hex[(b[i] >> 4) & 0xF];
      s[p++] = hex[b[i] & 0xF];
      if (i == 3 || i == 5 || i == 7 || i == 9)
	s[p++] = '-';
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
    return NULL;

  char *id = gen_uuid_v4 ();
  if (!id)
    return NULL;

  time_t now = time (NULL);
  json_t *pl = borrow_or_empty_obj (payload);

  /* NOTE: No "auth" here. Transport layer will add it before sending. */
  json_t *env = json_pack ("{s:i, s:s, s:s, s:I, s:s, s:s, s:o}",
			   "v", 1,
			   "type", type,
			   "id", id,
			   "ts", (json_int_t) now,
			   "src", src,
			   "dst", dst,
			   "payload", pl);

  json_decref (pl);
  free (id);
  return env;			/* caller must json_decref */
}

json_t *
s2s_make_ack (const char *src, const char *dst, const char *ack_of,
	      json_t *payload)
{
  if (!src || !*src || !dst || !*dst || !ack_of || !*ack_of)
    return NULL;

  char *id = gen_uuid_v4 ();
  if (!id)
    return NULL;

  time_t now = time (NULL);
  json_t *pl = borrow_or_empty_obj (payload);

  json_t *env = json_pack ("{s:i, s:s, s:s, s:I, s:s, s:s, s:s, s:o}",
			   "v", 1,
			   "type", "s2s.ack",
			   "id", id,
			   "ts", (json_int_t) now,
			   "src", src,
			   "dst", dst,
			   "ack_of", ack_of,
			   "payload", pl);

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
    return NULL;

  char *id = gen_uuid_v4 ();
  if (!id)
    return NULL;

  time_t now = time (NULL);
  json_t *det = details ? json_incref (details), details : json_object ();

  json_t *err = json_pack ("{s:s, s:s, s:o}",
			   "code", code,
			   "message", message ? message : "",
			   "details", det);

  json_decref (det);

  json_t *env = json_pack ("{s:i, s:s, s:s, s:I, s:s, s:s, s:s, s:o, s:o}",
			   "v", 1,
			   "type", "s2s.error",
			   "id", id,
			   "ts", (json_int_t) now,
			   "src", src,
			   "dst", dst,
			   "ack_of", ack_of,
			   "error", err,
			   "payload", json_object ()	/* keep payload an object for uniformity */
    );

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
    memcpy (p, m, n);
  return p;
}

int
s2s_env_validate_min (json_t *env, char **why)
{
  if (why)
    *why = NULL;
  if (!env || !json_is_object (env))
    {
      if (why)
	*why = dupmsg ("envelope not an object");
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
	*why = dupmsg ("v!=1");
      return -2;
    }
  if (!json_is_string (ty) || json_string_length (ty) == 0)
    {
      if (why)
	*why = dupmsg ("type missing");
      return -3;
    }
  if (!json_is_string (id) || json_string_length (id) == 0)
    {
      if (why)
	*why = dupmsg ("id missing");
      return -4;
    }
  if (!json_is_integer (ts) || json_integer_value (ts) <= 0)
    {
      if (why)
	*why = dupmsg ("ts invalid");
      return -5;
    }
  if (!json_is_string (src) || json_string_length (src) == 0)
    {
      if (why)
	*why = dupmsg ("src missing");
      return -6;
    }
  if (!json_is_string (dst) || json_string_length (dst) == 0)
    {
      if (why)
	*why = dupmsg ("dst missing");
      return -7;
    }
  if (!json_is_object (pl))
    {
      if (why)
	*why = dupmsg ("payload not object");
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
    *out_env = NULL;
  json_t *root = NULL;
  int rc = s2s_recv_json (c, &root, timeout_ms);
  if (rc != 0)
    return rc;

  char *why = NULL;
  int vrc = s2s_env_validate_min (root, &why);
  if (vrc != 0)
    {
      if (why)
	{
	  fprintf (stderr, "[s2s] envelope min-validate failed: %s\n", why);
	  free (why);
	}
      json_decref (root);
      return -1;
    }
  if (out_env)
    *out_env = root;
  else
    json_decref (root);
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
      fprintf (stderr, "j_get_integer: Invalid input parameters.\n");
      return -1;
    }

  // Make a mutable copy of the path for strtok_r to work on.
  char *path_copy = strdup (path);
  if (!path_copy)
    {
      perror ("j_get_integer: strdup failed");
      return -1;		// Allocation failure
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
	  fprintf (stderr,
		   "j_get_integer: Path segment '%s' expected an object, but found something else or NULL.\n",
		   token);
	  free (path_copy);
	  return -1;
	}

      // 2. Find the child node for the current token
      json_t *next = json_object_get (current, token);

      if (!next)
	{
	  fprintf (stderr,
		   "j_get_integer: Key '%s' not found at this level.\n",
		   token);
	  free (path_copy);
	  return -1;
	}

      // 3. Check if this is the last token in the path
      if (saveptr == NULL || *saveptr == '\0')
	{
	  // We are on the final key: check type and extract value
	  if (json_is_integer (next))
	    {
	      *result = (int) json_integer_value (next);
	      free (path_copy);
	      return 0;		// Success!
	    }
	  else
	    {
	      fprintf (stderr,
		       "j_get_integer: Final key '%s' found, but value is not an integer.\n",
		       token);
	      free (path_copy);
	      return -1;	// Wrong type
	    }
	}

      // 4. Not the final token, move to the next level for traversal
      current = next;
    }

  // Should only be reached if the path was malformed (e.g., ended in a dot)
  free (path_copy);
  fprintf (stderr,
	   "j_get_integer: Path traversal completed without finding a final value.\n");
  return -1;
}
