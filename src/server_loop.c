#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jansson.h>		/* -ljansson */
#include <stdbool.h>
#include <sqlite3.h>
/* local includes */
#include "database.h"
#include "schemas.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"


#define LISTEN_PORT 1234
#define BUF_SIZE    8192
#define RULE_REFUSE(_code,_msg,_hint_json) \
    do { send_enveloped_refused(ctx->fd, root, (_code), (_msg), (_hint_json)); goto trade_buy_done; } while (0)

#define RULE_ERROR(_code,_msg) \
    do { send_enveloped_error(ctx->fd, root, (_code), (_msg)); goto trade_buy_done; } while (0)


typedef struct
{
  int fd;
  volatile sig_atomic_t *running;
  struct sockaddr_in peer;
  uint64_t cid;
  int player_id;
  int sector_id;

  /* --- rate limit hints --- */
  time_t rl_window_start;	/* epoch sec when current window began */
  int rl_count;			/* responses sent in this window */
  int rl_limit;			/* max responses per window */
  int rl_window_sec;		/* window length in seconds */
} client_ctx_t;

static _Atomic uint64_t g_conn_seq = 0;
/* global (file-scope) counter for server message ids */
static _Atomic uint64_t g_msg_seq = 0;
static __thread client_ctx_t *g_ctx_for_send = NULL;
/* forward declaration to avoid implicit extern */
static void send_all_json (int fd, json_t * obj);
int db_player_info_json (int player_id, json_t ** out);
/* rate-limit helper prototypes (defined later) */
static void attach_rate_limit_meta (json_t * env, client_ctx_t * ctx);
static void rl_tick (client_ctx_t * ctx);
int db_sector_basic_json (int sector_id, json_t ** out_obj);
int db_adjacent_sectors_json (int sector_id, json_t ** out_array);
int db_ports_at_sector_json (int sector_id, json_t ** out_array);
int db_players_at_sector_json (int sector_id, json_t ** out_array);
int db_beacons_at_sector_json (int sector_id, json_t ** out_array);
int db_planets_at_sector_json (int sector_id, json_t ** out_array);
int db_player_set_sector (int player_id, int sector_id);
int db_player_get_sector (int player_id, int *out_sector);
static void handle_sector_info (int fd, json_t * root, int sector_id,
				int player_id);
static void send_enveloped_ok (int fd, json_t * root, const char *type,
			       json_t * data);
static void handle_sector_set_beacon (client_ctx_t *ctx, json_t *root);



/* Provided elsewhere; needed here for correct prototype */
// void send_enveloped_error (int fd, json_t *root, int code, const char *msg);

/* ----------------------------- */
/* Helpers for session endpoints */
/* ----------------------------- */

// Define the handler function
static void
handle_system_capabilities (int fd, json_t *root)
{
  send_enveloped_ok (fd, root, "system.capabilities",
		     json_incref (g_capabilities));
}

static json_t *
make_session_hello_payload (int is_authed, int player_id, int sector_id)
{
  return json_pack ("{s:s s:i s:o s:o s:o}",
		    "protocol_version", "1.0",
		    "server_time_unix", (int) time (NULL),
		    "authenticated", is_authed ? json_true () : json_false (),
		    "player_id", (is_authed
				  && player_id >
				  0) ? json_integer (player_id) :
		    json_null (), "current_sector", (is_authed
						     && sector_id >
						     0) ?
		    json_integer (sector_id) : json_null ());
}

static int
resolve_current_sector_from_info (json_t *info_obj, int fallback)
{
  if (!json_is_object (info_obj))
    return fallback;

  /* Preferred flat field */
  json_t *j = json_object_get (info_obj, "current_sector");
  if (json_is_integer (j))
    return (int) json_integer_value (j);

  /* Common alternates */
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



/* ------------------------ idempotency helpers  ------------------------ */


/* FNV-1a 64-bit */
static uint64_t
fnv1a64 (const unsigned char *s, size_t n)
{
  uint64_t h = 1469598103934665603ULL;
  for (size_t i = 0; i < n; ++i)
    {
      h ^= (uint64_t) s[i];
      h *= 1099511628211ULL;
    }
  return h;
}

static void
hex64 (uint64_t v, char out[17])
{
  static const char hexd[] = "0123456789abcdef";
  for (int i = 15; i >= 0; --i)
    {
      out[i] = hexd[v & 0xF];
      v >>= 4;
    }
  out[16] = '\0';
}

/* Canonicalize a JSON object (sorted keys, compact) then FNV-1a */
static void
idemp_fingerprint_json (json_t *obj, char out[17])
{
  char *s = json_dumps (obj, JSON_COMPACT | JSON_SORT_KEYS);
  if (!s)
    {
      strcpy (out, "0");
      return;
    }
  uint64_t h = fnv1a64 ((const unsigned char *) s, strlen (s));
  free (s);
  hex64 (h, out);
}



/* ------------------------ socket helpers ------------------------ */

static int
set_reuseaddr (int fd)
{
  int yes = 1;
  return setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
}

static int
make_listen_socket (uint16_t port)
{
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    {
      perror ("socket");
      return -1;
    }

  if (set_reuseaddr (fd) < 0)
    {
      perror ("setsockopt(SO_REUSEADDR)");
      close (fd);
      return -1;
    }

  struct sockaddr_in sa;
  memset (&sa, 0, sizeof (sa));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = htonl (INADDR_ANY);
  sa.sin_port = htons (port);

  if (bind (fd, (struct sockaddr *) &sa, sizeof (sa)) < 0)
    {
      perror ("bind");
      close (fd);
      return -1;
    }
  if (listen (fd, 128) < 0)
    {
      perror ("listen");
      close (fd);
      return -1;
    }
  return fd;
}

static int
send_all (int fd, const void *buf, size_t n)
{
  const char *p = (const char *) buf;
  size_t off = 0;
  while (off < n)
    {
      ssize_t w = write (fd, p + off, n - off);
      if (w < 0)
	{
	  if (errno == EINTR)
	    continue;
	  return -1;
	}
      off += (size_t) w;
    }
  return 0;
}

/* ------------------------ JSON reply helpers ------------------------ */

/* RFC3339 UTC "YYYY-MM-DDThh:mm:ssZ" (seconds precision) */
static void
iso8601_utc (char out[32])
{
  time_t now = time (NULL);
  struct tm tm;
  gmtime_r (&now, &tm);
  strftime (out, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

/* server-generated message id "srv-<seq>" */
static void
next_msg_id (char out[32])
{
  uint64_t n = atomic_fetch_add (&g_msg_seq, 1) + 1;
  snprintf (out, 32, "srv-%" PRIu64, n);
}

/* Build base envelope: id, ts, optional reply_to */
static json_t *
make_base_envelope (json_t *req /* may be NULL */ )
{
  char ts[32], mid[32];
  iso8601_utc (ts);
  next_msg_id (mid);

  json_t *env = json_object ();
  json_object_set_new (env, "id", json_string (mid));
  json_object_set_new (env, "ts", json_string (ts));

  if (req && json_is_object (req))
    {
      json_t *rid = json_object_get (req, "id");
      if (rid && json_is_string (rid))
	{
	  json_object_set (env, "reply_to", rid);	/* borrow */
	}
    }
  return env;			/* caller owns */
}

/* Send {"status":"error","type":"error","error":{code,message},...} (+data=null) */
static void
send_enveloped_refused (int fd, json_t *root, int code, const char *msg,
			json_t *data_opt)
{
  json_t *env = json_object ();
  json_object_set_new (env, "id", json_string ("srv-refuse"));
  json_object_set (env, "reply_to", json_object_get (root, "id"));
  char ts[32];
  iso8601_utc (ts);
  json_object_set_new (env, "ts", json_string (ts));
  json_object_set_new (env, "status", json_string ("refused"));
  json_object_set_new (env, "type", json_string ("error"));
  json_t *err =
    json_pack ("{s:i, s:s}", "code", code, "message", msg ? msg : "");
  json_object_set_new (env, "error", err);
  json_object_set (env, "data", data_opt ? data_opt : json_null ());

  attach_rate_limit_meta (env, g_ctx_for_send);
  rl_tick (g_ctx_for_send);

  send_all_json (fd, env);
  json_decref (env);
}


/* Send {"status":"ok","type":<type>,"data":<data>,"error":null} */
static void
send_enveloped_ok (int fd, json_t *root, const char *type, json_t *data)
{
  json_t *env = json_object ();
  json_object_set_new (env, "id", json_string ("srv-ok"));
  json_object_set (env, "reply_to", json_object_get (root, "id"));
  char ts[32];
  iso8601_utc (ts);
  json_object_set_new (env, "ts", json_string (ts));
  json_object_set_new (env, "status", json_string ("ok"));
  json_object_set_new (env, "type", json_string (type));
  json_object_set_new (env, "data", data ? data : json_null ());
  json_object_set_new (env, "error", json_null ());

  /* NEW: attach rate-limit meta and tick */
  //  attach_rate_limit_meta(env, ctx);   /* <-- add this line (requires ctx in scope; see note) */
  //  rl_tick(ctx);
  attach_rate_limit_meta (env, g_ctx_for_send);
  rl_tick (g_ctx_for_send);

  send_all_json (fd, env);
  json_decref (env);
}

/* Send {"status":"error","type":"error","error":{code,message}, "data": null } */
static void
send_enveloped_error (int fd, json_t *root, int code, const char *msg)
{
  json_t *env = json_object ();
  json_object_set_new (env, "id", json_string ("srv-err"));

  if (root && json_is_object (root))
    json_object_set (env, "reply_to", json_object_get (root, "id"));
  else
    json_object_set_new (env, "reply_to", json_null ());

  char ts[32];
  iso8601_utc (ts);
  json_object_set_new (env, "ts", json_string (ts));

  json_object_set_new (env, "status", json_string ("error"));
  json_object_set_new (env, "type", json_string ("error"));

  json_t *err =
    json_pack ("{s:i, s:s}", "code", code, "message", msg ? msg : "");
  json_object_set_new (env, "error", err);
  json_object_set_new (env, "data", json_null ());

  /* rate-limit hints (uses thread-local set in process_message) */
  attach_rate_limit_meta (env, g_ctx_for_send);
  rl_tick (g_ctx_for_send);

  send_all_json (fd, env);
  json_decref (env);
}


static void
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

static void
send_error_json (int fd, int code, const char *msg)
{
  json_t *o = json_object ();
  json_object_set_new (o, "status", json_string ("error"));
  json_object_set_new (o, "code", json_integer (code));
  json_object_set_new (o, "error", json_string (msg));
  send_all_json (fd, o);
  json_decref (o);
}

static void
send_ok_json (int fd, json_t *data /* may be NULL */ )
{
  json_t *o = json_object ();
  json_object_set_new (o, "status", json_string ("OK"));
  if (data)
    json_object_set (o, "data", data);	/* borrowed */
  send_all_json (fd, o);
  json_decref (o);
}



/* Build a minimal, stable JSON for fingerprinting (cmd + data subset) */
static json_t *
build_trade_buy_fp_obj (const char *cmd, json_t *jdata)
{
  /* Expect: port_id (int), commodity (str), quantity (int).
     Ignore meta and unrelated keys. */
  json_t *fp = json_object ();
  json_object_set_new (fp, "command", json_string (cmd));

  int port_id = 0, qty = 0;
  const char *commodity = NULL;
  json_t *jport = json_object_get (jdata, "port_id");
  json_t *jcomm = json_object_get (jdata, "commodity");
  json_t *jqty = json_object_get (jdata, "quantity");
  if (json_is_integer (jport))
    port_id = (int) json_integer_value (jport);
  if (json_is_integer (jqty))
    qty = (int) json_integer_value (jqty);
  if (json_is_string (jcomm))
    commodity = json_string_value (jcomm);

  json_object_set_new (fp, "port_id", json_integer (port_id));
  json_object_set_new (fp, "quantity", json_integer (qty));
  json_object_set_new (fp, "commodity",
		       json_string (commodity ? commodity : ""));
  return fp;			/* caller must json_decref */
}


/* Return REFUSED(1402) if no link; otherwise OK.
   Later: SELECT 1 FROM warps WHERE from=? AND to=?; */
static decision_t
validate_warp_rule (int from_sector, int to_sector)
{
  if (to_sector <= 0)
    return err (ERR_BAD_REQUEST, "Missing required field");
  if (to_sector == 9999)
    return refused (REF_NO_WARP_LINK, "No warp link");	/* your test case */
  /* TODO: if (no_row_in_warps_table) return refused(REF_NO_WARP_LINK, "No warp link"); */
  return ok ();
}

/* Example snapshot lookups (stub values for now). Later, read from DB. */
static int
player_credits (int player_id)
{
  return 1000;
}

static int
cargo_space_free (int player_id)
{
  return 50;
}

static int
port_is_open (int port_id, const char *commodity)
{
  return 1;
}				/* 0=closed */

static decision_t
validate_trade_buy_rule (int player_id, int port_id, const char *commodity,
			 int qty)
{
  if (!commodity || port_id <= 0 || qty <= 0)
    return err (ERR_BAD_REQUEST, "Missing required field");

  if (!port_is_open (port_id, commodity))
    return refused (REF_PORT_CLOSED, "Port is closed");

  /* Example rule checks */
  int price_per = 10;		/* stub */
  long long cost = (long long) price_per * qty;
  if (player_credits (player_id) < cost)
    return refused (REF_NOT_ENOUGH_CREDITS, "Not enough credits");

  if (cargo_space_free (player_id) < qty)
    return refused (REF_NOT_ENOUGH_HOLDS, "Not enough cargo holds");

  return ok ();
}

/* ------------------------ rate limit helpers ------------------------ */

/* Roll the window and increment count for this response */
static void
rl_tick (client_ctx_t *ctx)
{
  if (!ctx)
    return;
  time_t now = time (NULL);
  if (ctx->rl_limit <= 0)
    {
      ctx->rl_limit = 60;
    }				/* safety */
  if (ctx->rl_window_sec <= 0)
    {
      ctx->rl_window_sec = 60;
    }
  if (now - ctx->rl_window_start >= ctx->rl_window_sec)
    {
      ctx->rl_window_start = now;
      ctx->rl_count = 0;
    }
  ctx->rl_count++;
}

/* Build {limit, remaining, reset} */
static json_t *
rl_build_meta (const client_ctx_t *ctx)
{
  if (!ctx)
    return json_null ();
  time_t now = time (NULL);
  int reset = (int) (ctx->rl_window_start + ctx->rl_window_sec - now);
  if (reset < 0)
    reset = 0;
  int remaining = ctx->rl_limit - ctx->rl_count;
  if (remaining < 0)
    remaining = 0;
  return json_pack ("{s:i,s:i,s:i}", "limit", ctx->rl_limit, "remaining",
		    remaining, "reset", reset);
}

/* Ensure env.meta exists and add meta.rate_limit */
static void
attach_rate_limit_meta (json_t *env, client_ctx_t *ctx)
{
  if (!env || !ctx)
    return;
  json_t *meta = json_object_get (env, "meta");
  if (!json_is_object (meta))
    {
      meta = json_object ();
      json_object_set_new (env, "meta", meta);
    }
  json_t *rl = rl_build_meta (ctx);
  json_object_set_new (meta, "rate_limit", rl);
}



/* /\* Build a full sector snapshot for sector.info *\/ */
static json_t *
build_sector_info_json (int sector_id)
{
  json_t *root = json_object ();
  if (!root)
    return NULL;

  /* Basic info (id/name) */
  json_t *basic = NULL;
  if (db_sector_basic_json (sector_id, &basic) == SQLITE_OK && basic)
    {
      json_t *sid = json_object_get (basic, "sector_id");
      json_t *name = json_object_get (basic, "name");
      if (sid)
	json_object_set (root, "sector_id", sid);
      if (name)
	json_object_set (root, "name", name);
      json_decref (basic);
    }
  else
    {
      json_object_set_new (root, "sector_id", json_integer (sector_id));
    }

  /* Adjacent warps */
  json_t *adj = NULL;
  if (db_adjacent_sectors_json (sector_id, &adj) == SQLITE_OK && adj)
    {
      json_object_set_new (root, "adjacent", adj);
      json_object_set_new (root, "adjacent_count",
			   json_integer ((int) json_array_size (adj)));
    }
  else
    {
      json_object_set_new (root, "adjacent", json_array ());
      json_object_set_new (root, "adjacent_count", json_integer (0));
    }

/* Ports */
  json_t *ports = NULL;
  if (db_ports_at_sector_json (sector_id, &ports) == SQLITE_OK && ports)
    {
      json_object_set_new (root, "ports", ports);
      json_object_set_new (root, "has_port",
			   json_array_size (ports) >
			   0 ? json_true () : json_false ());
    }
  else
    {
      json_object_set_new (root, "ports", json_array ());
      json_object_set_new (root, "has_port", json_false ());
    }

  /* Players */
  json_t *players = NULL;
  if (db_players_at_sector_json (sector_id, &players) == SQLITE_OK && players)
    {
      json_object_set_new (root, "players", players);
      json_object_set_new (root, "players_count",
			   json_integer ((int) json_array_size (players)));
    }
  else
    {
      json_object_set_new (root, "players", json_array ());
      json_object_set_new (root, "players_count", json_integer (0));
    }

  /* Beacons (always include array) */
  json_t *beacons = NULL;
  if (db_beacons_at_sector_json (sector_id, &beacons) == SQLITE_OK && beacons)
    {
      json_object_set_new (root, "beacons", beacons);
      json_object_set_new (root, "beacons_count",
			   json_integer ((int) json_array_size (beacons)));
    }
  else
    {
      json_object_set_new (root, "beacons", json_array ());
      json_object_set_new (root, "beacons_count", json_integer (0));
    }

  /* Planets */
  json_t *planets = NULL;
  if (db_planets_at_sector_json (sector_id, &planets) == SQLITE_OK && planets)
    {
      json_object_set_new (root, "planets", planets);	/* takes ownership */
      json_object_set_new (root, "has_planet",
			   json_array_size (planets) >
			   0 ? json_true () : json_false ());
      json_object_set_new (root, "planets_count",
			   json_integer ((int) json_array_size (planets)));
    }
  else
    {
      json_object_set_new (root, "planets", json_array ());
      json_object_set_new (root, "has_planet", json_false ());
      json_object_set_new (root, "planets_count", json_integer (0));
    }



  /* Beacons (always include array) */
  // json_t *beacons = NULL;
  if (db_beacons_at_sector_json (sector_id, &beacons) == SQLITE_OK && beacons)
    {
      json_object_set_new (root, "beacons", beacons);
      json_object_set_new (root, "beacons_count",
			   json_integer ((int) json_array_size (beacons)));
    }
  else
    {
      json_object_set_new (root, "beacons", json_array ());
      json_object_set_new (root, "beacons_count", json_integer (0));
    }

  return root;
}





/* ------------------------ message processor (stub dispatcher) ------------------------ */
/* Single responsibility: receive one parsed JSON object and produce one reply. */

static void
process_message (client_ctx_t *ctx, json_t *root)
{
  /* Make ctx visible to send helpers for rate-limit meta */
  g_ctx_for_send = ctx;
  json_t *cmd = json_object_get (root, "command");
  json_t *evt = json_object_get (root, "event");

  /* Auto-auth from meta.session_token (transport-agnostic clients) */
  json_t *jmeta = json_object_get (root, "meta");
  const char *session_token = NULL;
  if (json_is_object (jmeta))
    {
      json_t *jtok = json_object_get (jmeta, "session_token");
      if (json_is_string (jtok))
	session_token = json_string_value (jtok);
    }
  if (ctx->player_id == 0 && session_token)
    {
      int pid = 0;
      long long exp = 0;
      int rc = db_session_lookup (session_token, &pid, &exp);
      if (rc == SQLITE_OK && pid > 0)
	{
	  ctx->player_id = pid;
	  if (ctx->sector_id <= 0)
	    ctx->sector_id = 1;	/* or load from DB */
	}
      /* If invalid/expired, we silently ignore; individual commands can refuse with 1401 */
    }

  if (ctx->player_id == 0 && ctx->sector_id <= 0)
    {
      ctx->sector_id = 1;
    }

  if (!(cmd && json_is_string (cmd)) && !(evt && json_is_string (evt)))
    {
      send_enveloped_error (ctx->fd, root, 1300, "Invalid request schema");
      return;
    }

  if (evt && json_is_string (evt))
    {
      send_enveloped_error (ctx->fd, root, 1300, "Invalid request schema");
      return;
    }

  /* Rate-limit defaults: 60 responses / 60 seconds */
  ctx->rl_limit = 60;
  ctx->rl_window_sec = 60;
  ctx->rl_window_start = time (NULL);
  ctx->rl_count = 0;

  const char *c = json_string_value (cmd);

  /* ----------------------- */
  /* Session ping / handshake */
  /* ----------------------- */

  // Check for the new command before others
  if (strcmp (c, "system.capabilities") == 0)
    {
      handle_system_capabilities (ctx->fd, root);
    }
  else if (strcmp (c, "session.ping") == 0)
    {
      /* Echo back whatever is in data (or {}) */
      json_t *jdata = json_object_get (root, "data");
      json_t *echo =
	json_is_object (jdata) ? json_incref (jdata) : json_object ();
      send_enveloped_ok (ctx->fd, root, "session.pong", echo);
      json_decref (echo);
      return;
    }
  else if (strcmp (c, "session.hello") == 0)
    {
      int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 0;

      if (ctx->player_id > 0)
	{
	  json_t *info = NULL;
	  int rc = db_player_info_json (ctx->player_id, &info);
	  if (rc == SQLITE_OK && info)
	    {
	      sector_id = resolve_current_sector_from_info (info, sector_id);
	      json_decref (info);
	    }
	}

      json_t *payload = make_session_hello_payload ((ctx->player_id > 0),
						    ctx->player_id,
						    (sector_id >
						     0) ? sector_id : 0);
      send_enveloped_ok (ctx->fd, root, "session.hello", payload);
      json_decref (payload);
      return;
    }

  if (strcmp (c, "login") == 0 || strcmp (c, "auth.login") == 0)
    {
      /* NEW: pull fields from the "data" object, not the root */
      json_t *jdata = json_object_get (root, "data");
      const char *name = NULL, *pass = NULL;

      if (json_is_object (jdata))
	{
	  /* Accept both "user_name" (new) and "player_name" (legacy) */
	  json_t *jname = json_object_get (jdata, "user_name");
	  if (!json_is_string (jname))
	    jname = json_object_get (jdata, "player_name");
	  json_t *jpass = json_object_get (jdata, "password");

	  name = json_is_string (jname) ? json_string_value (jname) : NULL;
	  pass = json_is_string (jpass) ? json_string_value (jpass) : NULL;
	}

      if (!name || !pass)
	{
	  send_enveloped_error (ctx->fd, root, AUTH_ERR_BAD_REQUEST,
				"Missing required field");
	}
      else
	{
	  int player_id = 0;
	  int rc = play_login (name, pass, &player_id);
	  if (rc == AUTH_OK)
	    {
	      /* Read canonical sector from DB; fall back to 1 if missing/NULL */
	      int sector_id = 0;
	      if (db_player_get_sector (player_id, &sector_id) != SQLITE_OK
		  || sector_id <= 0)
		sector_id = 1;

	      /* Mark connection as authenticated and sync session state */
	      ctx->player_id = player_id;
	      ctx->sector_id = sector_id;	/* <-- set unconditionally on login */

	      /* Reply with session info, including current_sector */
	      json_t *data = json_pack ("{s:i, s:i}",
					"player_id", player_id,
					"current_sector", sector_id);
	      if (!data)
		{
		  send_enveloped_error (ctx->fd, root, 1500, "Out of memory");
		  return;
		}
	      send_enveloped_ok (ctx->fd, root, "auth.session", data);
	      json_decref (data);
	    }
	  else if (rc == AUTH_ERR_INVALID_CRED)
	    {
	      send_enveloped_error (ctx->fd, root, AUTH_ERR_INVALID_CRED,
				    "Invalid credentials");
	    }
	  else
	    {
	      send_enveloped_error (ctx->fd, root, AUTH_ERR_DB,
				    "Database error");
	    }
	}
    }
  else if (strcmp (c, "user.create") == 0 || strcmp (c, "new.user") == 0)
    {
      json_t *jdata = json_object_get (root, "data");
      const char *name = NULL, *pass = NULL;
      if (json_is_object (jdata))
	{
	  json_t *jname = json_object_get (jdata, "user_name");
	  if (!json_is_string (jname))
	    jname = json_object_get (jdata, "player_name");
	  json_t *jpass = json_object_get (jdata, "password");
	  name = json_is_string (jname) ? json_string_value (jname) : NULL;
	  pass = json_is_string (jpass) ? json_string_value (jpass) : NULL;
	}

      if (!name || !pass)
	{
	  send_enveloped_error (ctx->fd, root, AUTH_ERR_BAD_REQUEST,
				"Missing required field");
	}
      else
	{
	  int player_id = 0;
	  int rc = user_create (name, pass, &player_id);
	  if (rc == AUTH_OK)
	    {
	      json_t *data = json_pack ("{s:i}", "player_id", player_id);
	      send_enveloped_ok (ctx->fd, root, "user.created", data);
	      json_decref (data);
	    }
	  else if (rc == AUTH_ERR_NAME_TAKEN)
	    {
	      send_enveloped_error (ctx->fd, root, AUTH_ERR_NAME_TAKEN,
				    "Username already exists");
	    }
	  else
	    {
	      send_enveloped_error (ctx->fd, root, AUTH_ERR_DB,
				    "Database error");
	    }
	}
    }
  else if (strcmp (c, "player.my_info") == 0)
    {
      if (ctx->player_id <= 0)
	{
	  send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated",
				  NULL);
	}
      else
	{
	  json_t *pinfo = NULL;
	  int prc = db_player_info_json (ctx->player_id, &pinfo);
	  if (prc != SQLITE_OK || !pinfo)
	    {
	      send_enveloped_error (ctx->fd, root, 1503, "Database error");
	      return;
	    }
	  send_enveloped_ok (ctx->fd, root, "player.info", pinfo);
	  json_decref (pinfo);
	}
    }
  else if (strcmp (c, "move.describe_sector") == 0
	   || strcmp (c, "sector.info") == 0)
    {
      int sector_id = ctx->sector_id > 0 ? ctx->sector_id : 0;
      json_t *jdata = json_object_get (root, "data");
      json_t *jsec =
	json_is_object (jdata) ? json_object_get (jdata, "sector_id") : NULL;
      if (json_is_integer (jsec))
	sector_id = (int) json_integer_value (jsec);
      if (sector_id <= 0)
	sector_id = 1;

      handle_sector_info (ctx->fd, root, sector_id, ctx->player_id);
    }
  else if (strcmp (c, "trade.buy") == 0)
    {
      json_t *jdata = json_object_get (root, "data");
      if (!json_is_object (jdata))
	{
	  send_enveloped_error (ctx->fd, root, 1301,
				"Missing required field");
	}
      else
	{
	  /* Extract fields */
	  json_t *jport = json_object_get (jdata, "port_id");
	  json_t *jcomm = json_object_get (jdata, "commodity");
	  json_t *jqty = json_object_get (jdata, "quantity");
	  const char *commodity =
	    json_is_string (jcomm) ? json_string_value (jcomm) : NULL;
	  int port_id =
	    json_is_integer (jport) ? (int) json_integer_value (jport) : 0;
	  int qty =
	    json_is_integer (jqty) ? (int) json_integer_value (jqty) : 0;

	  if (!commodity || port_id <= 0 || qty <= 0)
	    {
	      RULE_ERROR (ERR_BAD_REQUEST, "Missing required field");
	      //              send_enveloped_error (ctx->fd, root, 1301,
	      //                            "Missing required field");
	      /* no idempotency processing if invalid */
	    }
	  else
	    {
	      /* Pull idempotency key if present */
	      const char *idem_key = NULL;
	      json_t *jmeta = json_object_get (root, "meta");
	      if (json_is_object (jmeta))
		{
		  json_t *jk = json_object_get (jmeta, "idempotency_key");
		  if (json_is_string (jk))
		    idem_key = json_string_value (jk);
		}

	      /* Build fingerprint */
	      char fp[17];
	      fp[0] = 0;
	      json_t *fpobj = build_trade_buy_fp_obj (c, jdata);
	      idemp_fingerprint_json (fpobj, fp);
	      json_decref (fpobj);

	      if (idem_key && *idem_key)
		{
		  /* Try to begin idempotent op */
		  int rc = db_idemp_try_begin (idem_key, c, fp);
		  if (rc == SQLITE_CONSTRAINT)
		    {
		      /* Existing key: fetch */
		      char *ecmd = NULL, *efp = NULL, *erst = NULL;
		      if (db_idemp_fetch (idem_key, &ecmd, &efp, &erst) ==
			  SQLITE_OK)
			{
			  int fp_match = (efp && strcmp (efp, fp) == 0);
			  if (!fp_match)
			    {
			      /* Key reused with different payload */
			      send_enveloped_error (ctx->fd, root, 1105,
						    "Duplicate request (idempotency key reused)");
			    }
			  else if (erst)
			    {
			      /* Replay stored response exactly */
			      json_error_t jerr;
			      json_t *env = json_loads (erst, 0, &jerr);
			      if (env)
				{
				  send_all_json (ctx->fd, env);
				  json_decref (env);
				}
			      else
				{
				  /* Corrupt stored response; treat as server error */
				  send_enveloped_error (ctx->fd, root, 1500,
							"Idempotency replay error");
				}
			    }
			  else
			    {
			      /* Record exists but no stored response (in-flight/crash before store).
			         For now, treat as duplicate; later you could block/wait or retry op safely. */
			      send_enveloped_error (ctx->fd, root, 1105,
						    "Duplicate request (pending)");
			    }
			  free (ecmd);
			  free (efp);
			  free (erst);
			  /* Done */
			  goto done_trade_buy;
			}
		      else
			{
			  /* Couldn’t fetch; treat as server error */
			  send_enveloped_error (ctx->fd, root, 1500,
						"Database error");
			  goto done_trade_buy;
			}
		    }
		  else if (rc != SQLITE_OK)
		    {
		      send_enveloped_error (ctx->fd, root, 1500,
					    "Database error");
		      goto done_trade_buy;
		    }
		  /* If SQLITE_OK, we “own” this key now and should execute then store. */
		}

	      /* === Perform the actual operation (your existing stub) === */
	      json_t *data = json_pack ("{s:i, s:s, s:i}",
					"port_id", port_id,
					"commodity", commodity,
					"quantity", qty);

	      /* Build the final envelope so we can persist exactly what we send */
	      json_t *env = json_object ();
	      json_object_set_new (env, "id", json_string ("srv-trade"));
	      json_object_set (env, "reply_to", json_object_get (root, "id"));
	      char ts[32];
	      iso8601_utc (ts);
	      json_object_set_new (env, "ts", json_string (ts));
	      json_object_set_new (env, "status", json_string ("ok"));
	    trade_buy_done:
	      ;
	      json_object_set_new (env, "type",
				   json_string ("trade.accepted"));
	      json_object_set_new (env, "data", data);
	      json_object_set_new (env, "error", json_null ());

	      /* Optional meta: signal replay=false on first-run */
	      json_t *meta = json_object ();
	      if (idem_key && *idem_key)
		{
		  json_object_set_new (meta, "idempotent_replay",
				       json_false ());
		  json_object_set_new (meta, "idempotency_key",
				       json_string (idem_key));
		}
	      if (json_object_size (meta) > 0)
		json_object_set_new (env, "meta", meta);
	      else
		json_decref (meta);

	      /* If we’re idempotent, store the envelope JSON BEFORE sending */
	      if (idem_key && *idem_key)
		{
		  char *env_json =
		    json_dumps (env, JSON_COMPACT | JSON_SORT_KEYS);
		  if (!env_json
		      || db_idemp_store_response (idem_key,
						  env_json) != SQLITE_OK)
		    {
		      if (env_json)
			free (env_json);
		      json_decref (env);
		      send_enveloped_error (ctx->fd, root, 1500,
					    "Database error");
		      goto done_trade_buy;
		    }
		  free (env_json);
		}

	      /* Send */
	      send_all_json (ctx->fd, env);
	      json_decref (env);

	    done_trade_buy:
	      (void) 0;
	    }
	}
    }
  else if (strcmp (c, "move.warp") == 0)
    {
      json_t *jdata = json_object_get (root, "data");
      int to = 0;
      if (json_is_object (jdata))
	{
	  json_t *jto = json_object_get (jdata, "to_sector_id");
	  if (json_is_integer (jto))
	    to = (int) json_integer_value (jto);
	}

      decision_t d = validate_warp_rule (ctx->sector_id, to);
      if (d.status == DEC_ERROR)
	{
	  send_enveloped_error (ctx->fd, root, d.code, d.message);
	}
      else if (d.status == DEC_REFUSED)
	{
	  send_enveloped_refused (ctx->fd, root, d.code, d.message, NULL);
	}
      else
	{
	  int from = ctx->sector_id;

	  /* Persist new sector for this player */
	  int prc = db_player_set_sector (ctx->player_id, to);
	  if (prc != SQLITE_OK)
	    {
	      send_enveloped_error (ctx->fd, root, 1502,
				    "Failed to persist player sector");
	      return;
	    }

	  /* Update session state */
	  ctx->sector_id = to;

	  /* Reply (include current_sector for clients) */
	  json_t *data = json_pack ("{s:i, s:i, s:i, s:i}",
				    "player_id", ctx->player_id,
				    "from_sector_id", from,
				    "to_sector_id", to,
				    "current_sector", to);
	  if (!data)
	    {
	      send_enveloped_error (ctx->fd, root, 1500, "Out of memory");
	      return;
	    }
	  send_enveloped_ok (ctx->fd, root, "move.result", data);
	  json_decref (data);
	}
    }
  else if (strcmp (json_string_value (cmd), "port_info") == 0)
    {
      json_t *data_in = json_object_get (root, "data");
      int port_id = -1;

      // We can allow either `{"port_id":...}` or just assume the first port
      // in the sector, as the client will do.
      if (data_in)
	{
	  json_t *j_pid = json_object_get (data_in, "port_id");
	  if (json_is_integer (j_pid))
	    {
	      port_id = json_integer_value (j_pid);
	    }
	}

      if (port_id == -1)
	{
	  // Fallback to finding the port in the current sector
	  json_t *ports_at_sector = NULL;
	  db_ports_at_sector_json (ctx->sector_id, &ports_at_sector);
	  if (ports_at_sector && json_array_size (ports_at_sector) > 0)
	    {
	      json_t *first_port = json_array_get (ports_at_sector, 0);
	      json_t *first_port_id_json = json_object_get (first_port, "id");
	      if (first_port_id_json)
		{
		  port_id = json_integer_value (first_port_id_json);
		}
	    }
	  json_decref (ports_at_sector);
	}

      if (port_id == -1)
	{
	  // Still no port found.
	  send_enveloped_refused (ctx->fd, root, ERR_BAD_STATE,
				  "No port found in this sector.", NULL);
	  goto trade_port_info_done;
	}

      json_t *port_info = NULL;
      int rc = db_port_info_json (port_id, &port_info);
      if (rc != SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, ERR_DATABASE,
				"Could not fetch port info.");
	  goto trade_port_info_done;
	}

      send_enveloped_ok (ctx->fd, root, "trade.port_info", port_info);
      json_decref (port_info);

    trade_port_info_done:
      json_decref (root);
    }

  else if (strcmp (c, "ship.info") == 0 || strcmp (c, "ship.status") == 0)
    {
      if (ctx->player_id <= 0)
	{
	  send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated",
				  NULL);
	  //send_enveloped_error (ctx->fd, root, 1401, "Not authenticated");
	}
      else
	{
	  json_t *info = NULL;
	  int rc = db_player_info_json (ctx->player_id, &info);
	  if (rc == SQLITE_OK && info)
	    {
	      /* Build a ship-only view from the player_info payload */
	      json_t *data = json_pack ("{s:i, s:i, s:s, s:i, s:s, s:i, s:i}",
					"ship_number",
					json_integer_value (json_object_get
							    (info,
							     "ship_number")),
					"ship_id",
					json_integer_value (json_object_get
							    (info,
							     "ship_id")),
					"ship_name",
					json_string_value (json_object_get
							   (info,
							    "ship_name")),
					"ship_type_id",
					json_integer_value (json_object_get
							    (info,
							     "ship_type_id")),
					"ship_type_name",
					json_string_value (json_object_get
							   (info,
							    "ship_type_name")),
					"ship_holds",
					json_integer_value (json_object_get
							    (info,
							     "ship_holds")),
					"ship_fighters",
					json_integer_value (json_object_get
							    (info,
							     "ship_fighters")));
	      /* json_decref (info); */
	      /* send_enveloped_ok (ctx->fd, root, "ship.info", data); */
	      /* json_decref (data); */
	      json_decref (info);

	      json_t *env = make_base_envelope (root);
	      json_object_set_new (env, "status", json_string ("ok"));
	      json_object_set_new (env, "type", json_string ("ship.info"));
	      json_object_set_new (env, "data", data);	// takes ownership of data

	      json_t *meta = json_object ();
	      if (strcmp (c, "ship.info") == 0)
		{
		  json_object_set_new (meta, "deprecated", json_true ());
		}
	      if (json_object_size (meta) > 0)
		{
		  json_object_set_new (env, "meta", meta);
		}
	      else
		{
		  json_decref (meta);
		}

	      attach_rate_limit_meta (env, ctx);
	      rl_tick (ctx);
	      send_all_json (ctx->fd, env);
	      json_decref (env);

	    }
	  else
	    {
	      send_enveloped_error (ctx->fd, root, 1500, "Database error");
	    }
	}
    }
  else if (strcmp (c, "sector.set_beacon") == 0)
    {
      handle_sector_set_beacon (ctx, root);
    }
  else if (strcmp (c, "player.list_online") == 0)
    {
      /*Until you maintain a global connection registry,
         return the current player only (it unblocks clients and is safe).
         You can upgrade later to a real list. */

      json_t *arr = json_array ();
      if (ctx->player_id > 0)
	{
	  json_array_append_new (arr,
				 json_pack ("{s:i}", "player_id",
					    ctx->player_id));
	}
      json_t *data = json_pack ("{s:o}", "players", arr);
      send_enveloped_ok (ctx->fd, root, "player.list_online", data);
      json_decref (data);
    }

  else if (strcmp (c, "system.disconnect") == 0)
    {
      json_t *data = json_pack ("{s:s}", "message", "Goodbye");
      send_enveloped_ok (ctx->fd, root, "system.goodbye", data);
      json_decref (data);

      shutdown (ctx->fd, SHUT_RDWR);
      close (ctx->fd);
      return;			/* or break your per-connection loop appropriately */
    }
  else if (strcmp (c, "system.hello") == 0)
    {
      json_t *data = capabilities_build ();	/* never NULL in our stub */
      if (!data)
	{
	  send_enveloped_error (ctx->fd, root, 1102, "Service unavailable");
	}
      else
	{
	  send_enveloped_ok (ctx->fd, root, "system.capabilities", data);
	  json_decref (data);
	}
    }
  else if (strcmp (c, "system.describe_schema") == 0)
    {
      json_t *jdata = json_object_get (root, "data");
      const char *key = NULL;
      if (json_is_object (jdata))
	{
	  json_t *jkey = json_object_get (jdata, "key");
	  if (json_is_string (jkey))
	    key = json_string_value (jkey);
	}

      if (!key)
	{
	  /* Return the list of keys we have */
	  json_t *data = json_pack ("{s:o}", "available", schema_keys ());
	  send_enveloped_ok (ctx->fd, root, "system.schema_list", data);
	  json_decref (data);
	}
      else
	{
	  json_t *schema = schema_get (key);
	  if (!schema)
	    {
	      send_enveloped_error (ctx->fd, root, 1306, "Schema not found");
	    }
	  else
	    {
	      json_t *data =
		json_pack ("{s:s, s:o}", "key", key, "schema", schema);
	      send_enveloped_ok (ctx->fd, root, "system.schema", data);
	      json_decref (schema);
	      json_decref (data);
	    }
	}
    }
  else if (strcmp (c, "auth.register") == 0)
    {
      json_t *jdata = json_object_get (root, "data");
      const char *name = NULL, *pass = NULL;
      if (json_is_object (jdata))
	{
	  json_t *jn = json_object_get (jdata, "user_name");
	  json_t *jp = json_object_get (jdata, "password");
	  if (!json_is_string (jn))
	    jn = json_object_get (jdata, "player_name");
	  name = json_is_string (jn) ? json_string_value (jn) : NULL;
	  pass = json_is_string (jp) ? json_string_value (jp) : NULL;
	}
      if (!name || !pass)
	{
	  send_enveloped_error (ctx->fd, root, 1301,
				"Missing required field");
	}
      else
	{
	  int player_id = 0;
	  int rc = user_create (name, pass, &player_id);
	  if (rc == AUTH_OK)
	    {
	      /* issue a session token (24h = 86400s) */
	      char tok[65];
	      if (db_session_create (player_id, 86400, tok) != SQLITE_OK)
		{
		  send_enveloped_error (ctx->fd, root, 1500,
					"Database error");
		}
	      else
		{
		  ctx->player_id = player_id;
		  if (ctx->sector_id <= 0)
		    ctx->sector_id = 1;
		  json_t *data =
		    json_pack ("{s:i, s:s}", "player_id", player_id,
			       "session_token", tok);
		  send_enveloped_ok (ctx->fd, root, "auth.session", data);
		  json_decref (data);
		}
	    }
	  else if (rc == AUTH_ERR_NAME_TAKEN)
	    {
	      send_enveloped_error (ctx->fd, root, AUTH_ERR_NAME_TAKEN,
				    "Username already exists");
	    }
	  else
	    {
	      send_enveloped_error (ctx->fd, root, AUTH_ERR_DB,
				    "Database error");
	    }
	}
    }
  else if (strcmp (c, "auth.refresh") == 0)
    {
      /* Try data.session_token */
      json_t *jdata = json_object_get (root, "data");
      const char *tok = NULL;
      if (json_is_object (jdata))
	{
	  json_t *jt = json_object_get (jdata, "session_token");
	  if (json_is_string (jt))
	    tok = json_string_value (jt);
	}

      /* Try meta.session_token */
      if (!tok)
	{
	  json_t *jmeta = json_object_get (root, "meta");
	  if (json_is_object (jmeta))
	    {
	      json_t *jt = json_object_get (jmeta, "session_token");
	      if (json_is_string (jt))
		tok = json_string_value (jt);
	    }
	}

      /* If still no token, fall back to the connection’s logged-in player */
      if (!tok && ctx->player_id > 0)
	{
	  char newtok[65];
	  if (db_session_create (ctx->player_id, 86400, newtok) != SQLITE_OK)
	    {
	      send_enveloped_error (ctx->fd, root, 1500, "Database error");
	    }
	  else
	    {
	      json_t *data =
		json_pack ("{s:i, s:s}", "player_id", ctx->player_id,
			   "session_token", newtok);
	      send_enveloped_ok (ctx->fd, root, "auth.session", data);
	      json_decref (data);
	    }
	}
      else if (tok)
	{
	  /* Rotate provided token */
	  char newtok[65];
	  int pid = 0;
	  int rc = db_session_refresh (tok, 86400, newtok, &pid);
	  if (rc == SQLITE_OK)
	    {
	      ctx->player_id = pid;
	      if (ctx->sector_id <= 0)
		ctx->sector_id = 1;
	      json_t *data =
		json_pack ("{s:i, s:s}", "player_id", pid, "session_token",
			   newtok);
	      send_enveloped_ok (ctx->fd, root, "auth.session", data);
	      json_decref (data);
	    }
	  else
	    {
	      send_enveloped_error (ctx->fd, root, 1401,
				    "Invalid or expired session");
	    }
	}
      else
	{
	  /* Not logged in and no token supplied */
	  send_enveloped_error (ctx->fd, root, 1301,
				"Missing required field");
	}
    }

  else if (strcmp (c, "auth.logout") == 0)
    {
      json_t *jdata = json_object_get (root, "data");
      const char *tok = NULL;
      if (json_is_object (jdata))
	{
	  json_t *jt = json_object_get (jdata, "session_token");
	  if (json_is_string (jt))
	    tok = json_string_value (jt);
	}
      /* If no token provided, log out the connection; if token given, revoke it. */
      if (tok)
	(void) db_session_revoke (tok);
      ctx->player_id = 0;	/* drop connection auth state */

      json_t *data = json_pack ("{s:s}", "message", "Logged out");
      send_enveloped_ok (ctx->fd, root, "auth.logged_out", data);
      json_decref (data);
    }
  else
    {
      /* Unknown command -> 1101 Not implemented (per catalogue) */
      send_enveloped_error (ctx->fd, root, 1101, "Not implemented");
    }
}



/* ------------------------ per-connection loop (thread body) ------------------------ */

static void *
connection_thread (void *arg)
{
  client_ctx_t *ctx = (client_ctx_t *) arg;
  int fd = ctx->fd;

  /* Per-thread initialisation (DB/session/etc.) goes here */
  /* ctx->db = thread_db_open(); */

  /* Make recv interruptible via timeout so we can stop promptly */
  struct timeval tv = {.tv_sec = 1,.tv_usec = 0 };
  setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof (tv));

  char buf[BUF_SIZE];
  size_t have = 0;

  for (;;)
    {
      if (!*ctx->running)
	break;

      ssize_t n = recv (fd, buf + have, sizeof (buf) - have, 0);
      if (n > 0)
	{
	  have += (size_t) n;

	  /* Process complete lines (newline-terminated frames) */
	  size_t start = 0;
	  for (size_t i = 0; i < have; ++i)
	    {
	      if (buf[i] == '\n')
		{
		  /* Trim optional CR */
		  size_t linelen = i - start;
		  const char *line = buf + start;
		  while (linelen && line[linelen - 1] == '\r')
		    linelen--;

		  /* Parse and dispatch */
		  json_error_t jerr;
		  json_t *root = json_loadb (line, linelen, 0, &jerr);

		  if (!root || !json_is_object (root))
		    {
		      send_enveloped_error (fd, NULL, 1300,
					    "Invalid request schema");
		    }
		  else
		    {
		      process_message (ctx, root);
		    }

		  start = i + 1;
		}
	    }
	  /* Shift any partial line to front */
	  if (start > 0)
	    {
	      memmove (buf, buf + start, have - start);
	      have -= start;
	    }
	  /* Overflow without newline → guard & reset */
	  if (have == sizeof (buf))
	    {
	      send_error_json (fd, 1300, "invalid request schema");
	      have = 0;
	    }
	}
      else if (n == 0)
	{
	  /* peer closed */
	  break;
	}
      else
	{
	  if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
	    continue;
	  /* hard error */
	  break;
	}
    }

  /* Per-thread teardown */
  /* thread_db_close(ctx->db); */
  close (fd);
  free (ctx);
  return NULL;
}

/* ------------------------ accept loop (spawns thread per client) ------------------------ */

int
server_loop (volatile sig_atomic_t *running)
{
  fprintf (stderr, "Server loop starting...\n");

#ifdef SIGPIPE
  signal (SIGPIPE, SIG_IGN);	/* don’t die on write to closed socket */
#endif

  int listen_fd = make_listen_socket (LISTEN_PORT);
  if (listen_fd < 0)
    {
      fprintf (stderr, "Server loop exiting due to listen socket error.\n");
      return -1;
    }
  fprintf (stderr, "Listening on 0.0.0.0:%d\n", LISTEN_PORT);

  struct pollfd pfd = {.fd = listen_fd,.events = POLLIN,.revents = 0 };

  pthread_attr_t attr;
  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);

  while (*running)
    {
      int rc = poll (&pfd, 1, 1000);	/* 1s tick re-checks *running */
      if (rc < 0)
	{
	  if (errno == EINTR)
	    continue;
	  perror ("poll");
	  break;
	}
      if (rc == 0)
	continue;

      if (pfd.revents & POLLIN)
	{
	  client_ctx_t *ctx = calloc (1, sizeof (*ctx));
	  if (!ctx)
	    {
	      fprintf (stderr, "malloc failed\n");
	      continue;
	    }

	  socklen_t sl = sizeof (ctx->peer);
	  int cfd = accept (listen_fd, (struct sockaddr *) &ctx->peer, &sl);
	  if (cfd < 0)
	    {
	      free (ctx);
	      if (errno == EINTR)
		continue;
	      if (errno == EAGAIN || errno == EWOULDBLOCK)
		continue;
	      perror ("accept");
	      continue;
	    }

	  ctx->fd = cfd;
	  ctx->running = running;

	  char ip[INET_ADDRSTRLEN];
	  inet_ntop (AF_INET, &ctx->peer.sin_addr, ip, sizeof (ip));
	  fprintf (stderr, "Client connected: %s:%u (fd=%d)\n",
		   ip, (unsigned) ntohs (ctx->peer.sin_port), cfd);

	  // after filling ctx->fd, ctx->running, ctx->peer, and assigning ctx->cid
	  pthread_t th;
	  int prc = pthread_create (&th, &attr, connection_thread, ctx);
	  if (prc == 0)
	    {
	      fprintf (stderr,
		       "[cid=%" PRIu64 "] thread created (pthread=%lu)\n",
		       ctx->cid, (unsigned long) th);
	    }
	  else
	    {
	      fprintf (stderr, "pthread_create: %s\n", strerror (prc));
	      close (cfd);
	      free (ctx);
	    }

	}
    }

  pthread_attr_destroy (&attr);
  close (listen_fd);
  fprintf (stderr, "Server loop exiting...\n");
  return 0;
}


static void
handle_sector_info (int fd, json_t *root, int sector_id, int player_id)
{
  json_t *payload = build_sector_info_json (sector_id);
  if (!payload)
    {
      send_enveloped_error (fd, root, 1500,
			    "Out of memory building sector info");
      return;
    }

  // Add beacon info
  char *btxt = NULL;
  if (db_sector_beacon_text (sector_id, &btxt) == SQLITE_OK && btxt && *btxt)
    {
      json_object_set_new (payload, "beacon", json_string (btxt));
      json_object_set_new (payload, "has_beacon", json_true ());
    }
  else
    {
      json_object_set_new (payload, "beacon", json_null ());
      json_object_set_new (payload, "has_beacon", json_false ());
    }
  free (btxt);

  // Add ships info
  json_t *ships = NULL;
  int rc = db_ships_at_sector_json (player_id, sector_id, &ships);
  if (rc == SQLITE_OK)
    {
      json_object_set_new (payload, "ships", ships ? ships : json_array ());
      json_object_set_new (payload, "ships_count",
			   json_integer (json_array_size (ships)));
    }

  // Add port info
  json_t *ports = NULL;
  int pt = db_ports_at_sector_json (sector_id, &ports);
  if (pt == SQLITE_OK)
    {
      json_object_set_new (payload, "ports", ports ? ports : json_array ());
      json_object_set_new (payload, "ports_count",
			   json_integer (json_array_size (ports)));
    }
 
  // Add planet info
  json_t *planets = NULL;
  int plt = db_planets_at_sector_json (sector_id, &planets);
  if (plt == SQLITE_OK)
    {
      json_object_set_new (payload, "planets", planets ? planets : json_array ());
      json_object_set_new (payload, "planets_count",
			   json_integer (json_array_size (planets)));
    }

  // Add planet info
  json_t *players = NULL;
  int py = db_players_at_sector_json (sector_id, &players);
  if (py == SQLITE_OK)
    {
      json_object_set_new (payload, "players", players ? players : json_array ());
      json_object_set_new (payload, "players_count",
			   json_integer (json_array_size (players)));
    }


  
  send_enveloped_ok (fd, root, "sector.info", payload);
  json_decref (payload);
}



// In server_loop.c
static void
handle_sector_set_beacon (client_ctx_t *ctx, json_t *root)
{
  json_t *jdata = json_object_get (root, "data");
  json_t *jsector_id = json_object_get (jdata, "sector_id");
  json_t *jtext = json_object_get (jdata, "text");

  // Guard 1: Check if the player is in the specified sector
  int req_sector_id = json_integer_value (jsector_id);
  if (ctx->sector_id != req_sector_id)
    {
      send_enveloped_error (ctx->fd, root, 1400,
			    "Player is not in the specified sector.");
      return;
    }

  // Guard 2: Check if the sector is a FedSpace sector (1-10)
  if (req_sector_id >= 1 && req_sector_id <= 10)
    {
      send_enveloped_error (ctx->fd, root, 1403,
			    "Cannot set a beacon in FedSpace.");
      return;
    }

  // Guard 3: Check if the player has a beacon on their ship
  // (This requires a new DB function or a check against player inventory/ship state)
  // For this example, let's assume a function `db_player_has_beacon_on_ship` exists.
  // If you need to implement this, let me know.
  if (!db_player_has_beacon_on_ship (ctx->player_id))
    {
      send_enveloped_error (ctx->fd, root, 1401,
			    "Player does not have a beacon on their ship.");
      return;
    }

  // Guard 4: Check if a beacon already exists in the sector
  if (db_sector_has_beacon (req_sector_id))
    {
      send_enveloped_error (ctx->fd, root, 1402,
			    "A beacon already exists in this sector.");
      return;
    }

  // Trim and bound text length (e.g., 0-80 chars)
  const char *beacon_text = json_string_value (jtext);
  if (strlen (beacon_text) > 80)
    {
      send_enveloped_error (ctx->fd, root, 1400,
			    "Beacon text is too long (max 80 characters).");
      return;
    }

  // All guards passed, perform the update
  int rc = db_sector_set_beacon (req_sector_id, beacon_text);
  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 1500,
			    "Database error updating beacon.");
      return;
    }

  // Decrement player's beacon count
  db_player_decrement_beacon_count (ctx->player_id);

  // Return the new sector state
  handle_sector_info (ctx->fd, root, req_sector_id, ctx->player_id);
}
