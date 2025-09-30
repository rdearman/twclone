#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <jansson.h>
#include <time.h>
#include <string.h>
#include "server_envelope.h"
#include "schemas.h"
#include "server_config.h"
#include <string.h>
#include <jansson.h>
#include "server_envelope.h"   // declares send_enveloped_ok/error (fd, req, type, data)
#include "server_config.h"     // for client_ctx_t (struct) and other shared types


// --- Weak fallback so linking succeeds even if server_loop.c doesn't define it ---
// If a strong version exists in server_loop.c, the linker will prefer that one.
__attribute__((weak))
void loop_get_supported_commands(const cmd_desc_t **out_tbl, size_t *out_n) {
  static const cmd_desc_t k_supported_cmds[] = {
    // --- session / system ---
    { "session.hello",          "Handshake / hello" },
    { "session.ping",           "Ping" },
    { "session.goodbye",        "Client disconnect" },

    { "system.schema_list",     "List schema namespaces" },
    { "system.describe_schema", "Describe commands in a schema" },
    { "system.capabilities",    "Feature flags, schemas, counts" },
    { "system.cmd_list",        "Flat list of all commands" },

    // --- auth ---
    { "auth.login",             "Authenticate" },
    { "auth.logout",            "Log out" },
    { "auth.mfa",               "Second-factor code" },
    { "auth.register",          "Create a new player" },

    // --- players / ship ---
    { "players.me",             "Current player info" },
    { "players.online",         "List online players" },
    { "players.refresh",        "Refresh player state" },
    { "ship.info",              "Ship information" },

    // --- sector / movement ---
    { "sector.info",            "Describe current sector" },
    { "sector.set_beacon",      "Set or clear sector beacon" },
    { "move.warp",              "Warp to sector" },
    { "move.scan",              "Scan adjacent sectors" },
    { "move.pathfind",          "Find path between sectors" },
    { "move.force_move",        "Admin: force-move a ship" },

    // --- trade ---
    { "trade.port_info",        "Port prices/stock in sector" },
    { "trade.buy",              "Buy from port" },
    { "trade.sell",             "Sell to port" },
  };

  if (out_tbl) *out_tbl = k_supported_cmds;
  if (out_n)   *out_n   = sizeof(k_supported_cmds) / sizeof(k_supported_cmds[0]);
}


// Provided by server_loop.c
extern void loop_get_supported_commands(const cmd_desc_t **out_tbl, size_t *out_n);

// ---- Helpers ----
static void get_cmd_table(const cmd_desc_t **out_tbl, size_t *out_n) {
  loop_get_supported_commands(out_tbl, out_n);
}

static int have_cmd(const char *needle) {
  const cmd_desc_t *tbl = NULL; size_t n = 0;
  get_cmd_table(&tbl, &n);
  for (size_t i = 0; i < n; i++) {
    if (tbl[i].name && strcmp(tbl[i].name, needle) == 0) return 1;
  }
  return 0;
}

static json_t *collect_schema_names(void) {
  const cmd_desc_t *tbl = NULL; size_t n = 0;
  get_cmd_table(&tbl, &n);

  // use an object as a set of schema prefixes
  json_t *set = json_object();
  for (size_t i = 0; i < n; i++) {
    const char *cmd = tbl[i].name;
    if (!cmd) continue;
    const char *dot = strchr(cmd, '.');
    if (!dot || dot == cmd) continue;
    size_t len = (size_t)(dot - cmd);
    if (len == 0) continue;

    char buf[64];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, cmd, len); buf[len] = '\0';

    json_object_set_new(set, buf, json_true());
  }

  // convert set → array
  json_t *arr = json_array();
  void *it = json_object_iter(set);
  while (it) {
    const char *key = json_object_iter_key(it);
    json_array_append_new(arr, json_string(key));
    it = json_object_iter_next(set, it);
  }
  json_decref(set);
  return arr;
}

// ----------------- Handlers -----------------

// system.cmd_list  →  { type:"system.cmd_list", data:{ commands:[{cmd,summary?},...], count:n } }
int cmd_system_cmd_list(client_ctx_t *ctx, json_t *root) {
  (void)ctx; // used via ctx->fd below
  const cmd_desc_t *tbl = NULL; size_t n = 0;
  get_cmd_table(&tbl, &n);

  json_t *arr = json_array();
  for (size_t i = 0; i < n; i++) {
    if (!tbl[i].name) continue;
    json_t *o = json_object();
    json_object_set_new(o, "cmd", json_string(tbl[i].name));
    if (tbl[i].summary && tbl[i].summary[0])
      json_object_set_new(o, "summary", json_string(tbl[i].summary));
    json_array_append_new(arr, o);
  }

  json_t *data = json_pack("{s:o, s:i}", "commands", arr, "count", (int)n);
  send_enveloped_ok(ctx->fd, root, "system.cmd_list", data);
  return 0;
}

// system.describe_schema {name} → { type:"system.describe_schema", data:{ name, messages:[...] } }
int cmd_system_describe_schema(client_ctx_t *ctx, json_t *root) {
  const char *name = NULL;
  json_t *d = json_object_get(root, "data");
  if (d) name = json_string_value(json_object_get(d, "name"));
  if (!name || !*name) {
    send_enveloped_error(ctx->fd, root, 1103, "Missing 'name'");
    return 0;
  }

  size_t plen = strlen(name);
  const cmd_desc_t *tbl = NULL; size_t n = 0;
  get_cmd_table(&tbl, &n);

  json_t *msgs = json_array();
  for (size_t i = 0; i < n; i++) {
    const char *cmd = tbl[i].name;
    if (!cmd) continue;
    if (strncmp(cmd, name, plen) == 0 && cmd[plen] == '.') {
      json_t *o = json_object();
      json_object_set_new(o, "cmd", json_string(cmd));
      if (tbl[i].summary && tbl[i].summary[0])
        json_object_set_new(o, "summary", json_string(tbl[i].summary));
      json_array_append_new(msgs, o);
    }
  }

  json_t *out = json_pack("{s:s, s:o}", "name", name, "messages", msgs);
  send_enveloped_ok(ctx->fd, root, "system.describe_schema", out);
  return 0;
}

// system.schema_list → { type:"system.schema_list", data:{ available:[...] } }
int cmd_system_schema_list(client_ctx_t *ctx, json_t *root) {
  json_t *arr = collect_schema_names();
  json_t *data = json_pack("{s:o}", "available", arr);
  send_enveloped_ok(ctx->fd, root, "system.schema_list", data);
  return 0;
}

// system.capabilities → { type:"system.capabilities", data:{ protocol_version, features, schemas, command_count } }
int cmd_system_capabilities(client_ctx_t *ctx, json_t *root) {
  const cmd_desc_t *tbl = NULL; size_t n = 0;
  get_cmd_table(&tbl, &n);

  json_t *features = json_pack("{"
    "s:b, s:b, s:b, s:b, s:b"
  "}",
    "move_warp",       have_cmd("move.warp"),
    "move_scan",       have_cmd("move.scan"),
    "trade_buy",       have_cmd("trade.buy"),
    "trade_sell",      have_cmd("trade.sell"),
    "sector_describe", have_cmd("sector.describe")
  );

  json_t *schemas = collect_schema_names();

  json_t *data = json_pack("{s:s, s:o, s:o, s:i}",
    "protocol_version", "1.0",
    "features", features,
    "schemas",  schemas,
    "command_count", (int)n
  );
  send_enveloped_ok(ctx->fd, root, "system.capabilities", data);
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

/* /\* server-generated message id "srv-<seq>" *\/ */
/* void */
/* next_msg_id (char out[32]) */
/* { */
/*   uint64_t n = atomic_fetch_add (&g_msg_seq, 1) + 1; */
/*   snprintf (out, 32, "srv-%" PRIu64, n); */
/* } */

/* RFC3339 UTC "YYYY-MM-DDThh:mm:ssZ" (seconds precision) */
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
static json_t *make_default_meta(void) {
  json_t *rate = json_object();
  json_object_set_new(rate, "limit",     json_integer(60));
  json_object_set_new(rate, "remaining", json_integer(60));
  json_object_set_new(rate, "reset",     json_integer(60)); // seconds

  json_t *meta = json_object();
  json_object_set_new(meta, "rate_limit", rate);
  return meta; // caller owns
}

/* -------------------- OK -------------------- */
void
send_enveloped_ok (int fd, json_t *req, const char *type, json_t *data)
{
  json_t *resp = json_object();

  // ids
  json_object_set_new(resp, "id", json_string("srv-ok"));
  const char *req_id = NULL;
  if (req && json_is_object(req)) {
    json_t *jid = json_object_get(req, "id");
    if (json_is_string(jid)) req_id = json_string_value(jid);
  }
  if (req_id) json_object_set_new(resp, "reply_to", json_string(req_id));

  // timestamp
  char tsbuf[32]; iso8601_utc(tsbuf);
  json_object_set_new(resp, "ts", json_string(tsbuf));

  // status/type
  json_object_set_new(resp, "status", json_string("ok"));
  json_object_set_new(resp, "type",   json_string(type ? type : "ok"));

  // payload + error=null
  if (data) json_object_set_new(resp, "data", data); else json_object_set_new(resp, "data", json_null());
  json_object_set_new(resp, "error", json_null());

  // meta: default rate-limit info
  json_object_set_new(resp, "meta", make_default_meta());

  // write one line
  char *s = json_dumps(resp, JSON_COMPACT);
  if (s) {
    size_t len = strlen(s);
    if (len) (void)write(fd, s, len);
    (void)write(fd, "\n", 1);
    free(s);
  }
  json_decref(resp);
}

/* -------------------- ERROR -------------------- */
void
send_enveloped_error (int fd, json_t *req, int code, const char *message)
{
  json_t *resp = json_object();

  json_object_set_new(resp, "id", json_string("srv-err"));
  const char *req_id = NULL;
  if (req && json_is_object(req)) {
    json_t *jid = json_object_get(req, "id");
    if (json_is_string(jid)) req_id = json_string_value(jid);
  }
  if (req_id) json_object_set_new(resp, "reply_to", json_string(req_id));

  char tsbuf[32]; iso8601_utc(tsbuf);
  json_object_set_new(resp, "ts", json_string(tsbuf));
  json_object_set_new(resp, "status", json_string("error"));
  json_object_set_new(resp, "type",   json_string("error"));

  json_t *err = json_object();
  json_object_set_new(err, "code",    json_integer(code));
  json_object_set_new(err, "message", json_string(message ? message : "Error"));
  json_object_set_new(resp, "error",  err);

  json_object_set_new(resp, "data", json_null());
  json_object_set_new(resp, "meta", make_default_meta());

  char *s = json_dumps(resp, JSON_COMPACT);
  if (s) {
    size_t len = strlen(s);
    if (len) (void)write(fd, s, len);
    (void)write(fd, "\n", 1);
    free(s);
  }
  json_decref(resp);
}

/* -------------------- REFUSED -------------------- */
void
send_enveloped_refused (int fd, json_t *req, int code, const char *msg, json_t *data_opt)
{
  json_t *resp = json_object();

  json_object_set_new(resp, "id", json_string("srv-refuse"));
  const char *req_id = NULL;
  if (req && json_is_object(req)) {
    json_t *jid = json_object_get(req, "id");
    if (json_is_string(jid)) req_id = json_string_value(jid);
  }
  if (req_id) json_object_set_new(resp, "reply_to", json_string(req_id));

  char tsbuf[32]; iso8601_utc(tsbuf);
  json_object_set_new(resp, "ts", json_string(tsbuf));
  json_object_set_new(resp, "status", json_string("refused"));
  json_object_set_new(resp, "type",   json_string("error")); // legacy shape

  json_t *err = json_pack("{s:i, s:s}", "code", code, "message", msg ? msg : "");
  if (data_opt) json_object_set(err, "data", data_opt); // borrow
  json_object_set_new(resp, "error", err);

  json_object_set_new(resp, "data", json_null());
  json_object_set_new(resp, "meta", make_default_meta());

  char *s = json_dumps(resp, JSON_COMPACT);
  if (s) {
    size_t len = strlen(s);
    if (len) (void)write(fd, s, len);
    (void)write(fd, "\n", 1);
    free(s);
  }
  json_decref(resp);
}


