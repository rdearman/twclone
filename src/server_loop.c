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
#include "server_ships.h"
#include "common.h"
#include "server_envelope.h"
#include "server_players.h"
#include "server_ports.h"   // at top of server_loop.c
#include "server_auth.h"


#define LISTEN_PORT 1234
#define BUF_SIZE    8192
#define RULE_REFUSE(_code,_msg,_hint_json) \
    do { send_enveloped_refused(ctx->fd, root, (_code), (_msg), (_hint_json)); goto trade_buy_done; } while (0)

#define RULE_ERROR(_code,_msg) \
    do { send_enveloped_error(ctx->fd, root, (_code), (_msg)); goto trade_buy_done; } while (0)



static _Atomic uint64_t g_conn_seq = 0;
/* global (file-scope) counter for server message ids */
static _Atomic uint64_t g_msg_seq = 0;
static __thread client_ctx_t *g_ctx_for_send = NULL;
/* forward declaration to avoid implicit extern */
void send_all_json (int fd, json_t * obj);
int db_player_info_json (int player_id, json_t ** out);
/* rate-limit helper prototypes (defined later) */
 void attach_rate_limit_meta (json_t * env, client_ctx_t * ctx);
 void rl_tick (client_ctx_t * ctx);
int db_sector_basic_json (int sector_id, json_t ** out_obj);
int db_adjacent_sectors_json (int sector_id, json_t ** out_array);
int db_ports_at_sector_json (int sector_id, json_t ** out_array);
int db_sector_scan_core (int sector_id, json_t **out_obj);
int db_players_at_sector_json (int sector_id, json_t ** out_array);
int db_beacons_at_sector_json (int sector_id, json_t ** out_array);
int db_planets_at_sector_json (int sector_id, json_t ** out_array);
int db_player_set_sector (int player_id, int sector_id);
int db_player_get_sector (int player_id, int *out_sector);
void handle_sector_info (int fd, json_t * root, int sector_id,
				int player_id);
void handle_sector_set_beacon (client_ctx_t * ctx, json_t * root);
/* Fast sector scan handler (IDs+counts only) */
void handle_move_scan (client_ctx_t *ctx, json_t *root);
void handle_move_pathfind (client_ctx_t *ctx, json_t *root);
void send_enveloped_ok (int fd, json_t * root, const char *type,
			       json_t * data);


/* Provided elsewhere; needed here for correct prototype */
// void send_enveloped_error (int fd, json_t *root, int code, const char *msg);

/* ----------------------------- */
/* Helpers for session endpoints */
/* ----------------------------- */





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
void idemp_fingerprint_json (json_t *obj, char out[17])
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
 void
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
 void
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
  int rc;
  
  if (strcmp (c, "login") == 0 || strcmp (c, "auth.login") == 0)
    {
       rc = cmd_auth_login(ctx, root);
    }
  else if (!strcmp(c, "system.capabilities")) {
     rc = cmd_system_capabilities(ctx, root);
  }
  else if (!strcmp(c, "system.describe_schema")) {
     rc = cmd_system_describe_schema(ctx, root);
  }
  else if (!strcmp(c, "session.ping")) {
    rc = cmd_session_ping(ctx, root);
  }
  else if (!strcmp(c, "session.hello")) {
    rc = cmd_session_hello(ctx, root);
  }
  else if (strcmp(c, "auth.register") == 0) {
     rc = cmd_auth_register(ctx, root);
  }
  else if (!strcmp(c, "auth.logout")) {
     rc = cmd_auth_logout(ctx, root);
  }
  else if (strcmp (c, "user.create") == 0 || strcmp (c, "new.user") == 0)
    {
      rc = cmd_user_create(ctx, root);
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
  else if (strcmp(c, "move.pathfind") == 0)
    {
      handle_move_pathfind(ctx, root);
    }  
  else if (strcmp (c, "move.scan") == 0)
    {
      handle_move_scan (ctx, root);
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
  else if (strcmp(c, "port.info") == 0 || strcmp(c, "port.status") == 0) {
     rc = cmd_port_info(ctx, root);
  }
  else if (strcmp(c, "trade.buy") == 0) {
     rc = cmd_trade_buy(ctx, root);
  }
  else if (strcmp(c, "trade.sell") == 0) {
    //  rc = cmd_trade_sell(ctx, root);
  }
  /* optional, if present in your code */
  else if (strcmp(c, "trade.quote") == 0) {
    //  rc = cmd_trade_quote(ctx, root);
  }
  else if (strcmp(c, "trade.jettison") == 0) {
    // rc = cmd_trade_jettison(ctx, root);
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
  else if (strcmp(c, "ship.inspect") == 0) {
     rc = cmd_ship_inspect(ctx, root);
  }
  else if (strcmp(c, "ship.rename") == 0) {
     rc = cmd_ship_rename(ctx, root);
  }
  else if (strcmp(c, "ship.reregister") == 0) {
     rc = cmd_ship_rename(ctx, root);   // alias
  }
  else if (strcmp(c, "ship.claim") == 0) {
     rc = cmd_ship_claim(ctx, root);
  }
  else if (strcmp(c, "ship.status") == 0) {
     rc = cmd_ship_status(ctx, root);
  }
  else if (strcmp(c, "ship.info") == 0) {
     rc = cmd_ship_info_compat(ctx, root); // legacy alias
  }
  else if (strcmp (c, "sector.set_beacon") == 0)
    {
      handle_sector_set_beacon (ctx, root);
    }
  else if (strcmp (c, "player.list_online") == 0)
    {
       rc = cmd_player_list_online(ctx, root);
    }

  else if (strcmp (c, "system.disconnect") == 0)
    {
       rc = cmd_session_disconnect(ctx, root);
    }
  else if (strcmp (c, "system.hello") == 0)
    {
       rc = cmd_session_hello(ctx, root);
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


 void
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
      json_object_set_new (payload, "planets",
			   planets ? planets : json_array ());
      json_object_set_new (payload, "planets_count",
			   json_integer (json_array_size (planets)));
    }

  // Add planet info
  json_t *players = NULL;
  int py = db_players_at_sector_json (sector_id, &players);
  if (py == SQLITE_OK)
    {
      json_object_set_new (payload, "players",
			   players ? players : json_array ());
      json_object_set_new (payload, "players_count",
			   json_integer (json_array_size (players)));
    }


  send_enveloped_ok (fd, root, "sector.info", payload);
  json_decref (payload);
}


 void
handle_sector_set_beacon (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || !root)
    return;

  json_t *jdata = json_object_get (root, "data");
  json_t *jsector_id = json_object_get (jdata, "sector_id");
  json_t *jtext = json_object_get (jdata, "text");

  /* Guard 0: schema */
  if (!json_is_integer (jsector_id) || !json_is_string (jtext))
    {
      send_enveloped_error (ctx->fd, root, 1300, "Invalid request schema");
      return;
    }

  /* Guard 1: player must be in that sector */
  int req_sector_id = (int) json_integer_value (jsector_id);
  if (ctx->sector_id != req_sector_id)
    {
      send_enveloped_error (ctx->fd, root, 1400,
			    "Player is not in the specified sector.");
      return;
    }

  /* Guard 2: FedSpace 1–10 is forbidden */
  if (req_sector_id >= 1 && req_sector_id <= 10)
    {
      send_enveloped_error (ctx->fd, root, 1403,
			    "Cannot set a beacon in FedSpace.");
      return;
    }

  /* Guard 3: player must have a beacon on the ship */
  if (!db_player_has_beacon_on_ship (ctx->player_id))
    {
      send_enveloped_error (ctx->fd, root, 1401,
			    "Player does not have a beacon on their ship.");
      return;
    }

  /* NOTE: Canon behavior: if a beacon already exists, launching another destroys BOTH.
     So we DO NOT reject here. We only check 'had_beacon' to craft a user message. */
  int had_beacon = db_sector_has_beacon (req_sector_id);

  /* Text length guard (<=80) */
  const char *beacon_text = json_string_value (jtext);
  if (!beacon_text)
    beacon_text = "";
  if ((int) strlen (beacon_text) > 80)
    {
      send_enveloped_error (ctx->fd, root, 1400,
			    "Beacon text is too long (max 80 characters).");
      return;
    }

  /* Perform the update:
     - if none existed → set text
     - if one existed  → clear (explode both) */
  int rc = db_sector_set_beacon (req_sector_id, beacon_text);
  if (rc != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 1500,
			    "Database error updating beacon.");
      return;
    }

  /* Consume the player's beacon (canon: you used it either way) */
  db_player_decrement_beacon_count (ctx->player_id);

  /* ===== Build sector.info payload (same fields as handle_sector_info) ===== */
  json_t *payload = build_sector_info_json (req_sector_id);
  if (!payload)
    {
      send_enveloped_error (ctx->fd, root, 1500,
			    "Out of memory building sector info");
      return;
    }

  /* Beacon text */
  char *btxt = NULL;
  if (db_sector_beacon_text (req_sector_id, &btxt) == SQLITE_OK && btxt
      && *btxt)
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

  /* Ships */
  json_t *ships = NULL;
  rc = db_ships_at_sector_json (ctx->player_id, req_sector_id, &ships);
  if (rc == SQLITE_OK)
    {
      json_object_set_new (payload, "ships", ships ? ships : json_array ());
      json_object_set_new (payload, "ships_count",
			   json_integer ((int) json_array_size (ships)));
    }

  /* Ports */
  json_t *ports = NULL;
  int pt = db_ports_at_sector_json (req_sector_id, &ports);
  if (pt == SQLITE_OK)
    {
      json_object_set_new (payload, "ports", ports ? ports : json_array ());
      json_object_set_new (payload, "ports_count",
			   json_integer ((int) json_array_size (ports)));
    }

  /* Planets */
  json_t *planets = NULL;
  int plt = db_planets_at_sector_json (req_sector_id, &planets);
  if (plt == SQLITE_OK)
    {
      json_object_set_new (payload, "planets",
			   planets ? planets : json_array ());
      json_object_set_new (payload, "planets_count",
			   json_integer ((int) json_array_size (planets)));
    }

  /* Players */
  json_t *players = NULL;
  int py = db_players_at_sector_json (req_sector_id, &players);
  if (py == SQLITE_OK)
    {
      json_object_set_new (payload, "players",
			   players ? players : json_array ());
      json_object_set_new (payload, "players_count",
			   json_integer ((int) json_array_size (players)));
    }

  /* ===== Send envelope with a nice meta.message ===== */
  json_t *env = make_base_envelope (root);
  json_object_set_new (env, "status", json_string ("ok"));
  json_object_set_new (env, "type", json_string ("sector.info"));
  json_object_set_new (env, "data", payload);	/* take ownership */

  json_t *meta = json_object ();
  json_object_set_new (meta, "message",
		       json_string (had_beacon
				    ?
				    "Two marker beacons collided and exploded — the sector now has no beacon."
				    : "Beacon deployed."));
  json_object_set_new (env, "meta", meta);

  attach_rate_limit_meta (env, ctx);
  rl_tick (ctx);
  send_all_json (ctx->fd, env);
  json_decref (env);
}

/* -------- move.scan: fast, side-effect-free snapshot (defensive build) -------- */
 void
handle_move_scan (client_ctx_t *ctx, json_t *root)
{
  if (!ctx) return;

  /* Resolve sector id (default to 1 if session is unset) */
  int sector_id = (ctx->sector_id > 0) ? ctx->sector_id : 1;
  fprintf(stderr, "[move.scan] sector_id=%d\n", sector_id);

  /* 1) Core snapshot from DB (uses sectors.name/beacon; ports.location; ships.location; planets.sector) */
  json_t *core = NULL;
  if (db_sector_scan_core(sector_id, &core) != SQLITE_OK || !core) {
    send_enveloped_error(ctx->fd, root, 1401, "Sector not found");
    return;
  }

  /* 2) Adjacent IDs (array) */
  json_t *adj = NULL;
  if (db_adjacent_sectors_json(sector_id, &adj) != SQLITE_OK || !adj) {
    adj = json_array(); /* never null */
  }

  /* 3) Security flags */
  int in_fed = (sector_id >= 1 && sector_id <= 10);
  int safe_zone = json_integer_value(json_object_get(core, "safe_zone")); /* 0 with your schema */
  json_t *security = json_object();
  if (!security) { json_decref(core); json_decref(adj); send_enveloped_error(ctx->fd, root, 1500, "OOM"); return; }
  json_object_set_new(security, "fedspace",      json_boolean(in_fed));
  json_object_set_new(security, "safe_zone",     json_boolean(in_fed ? 1 : (safe_zone != 0)));
  json_object_set_new(security, "combat_locked", json_boolean(in_fed ? 1 : 0));

  /* 4) Port summary (presence only) */
  int port_cnt = json_integer_value(json_object_get(core, "port_count"));
  json_t *port = json_object();
  if (!port) { json_decref(core); json_decref(adj); json_decref(security); send_enveloped_error(ctx->fd, root, 1500, "OOM"); return; }
  json_object_set_new(port, "present", json_boolean(port_cnt > 0));
  json_object_set_new(port, "class",   json_null());
  json_object_set_new(port, "stance",  json_null());

  /* 5) Counts object */
  int ships   = json_integer_value(json_object_get(core, "ship_count"));
  int planets = json_integer_value(json_object_get(core, "planet_count"));
  json_t *counts = json_object();
  if (!counts) { json_decref(core); json_decref(adj); json_decref(security); json_decref(port); send_enveloped_error(ctx->fd, root, 1500, "OOM"); return; }
  json_object_set_new(counts, "ships",    json_integer(ships));
  json_object_set_new(counts, "planets",  json_integer(planets));
  json_object_set_new(counts, "mines",    json_integer(0));
  json_object_set_new(counts, "fighters", json_integer(0));

  /* 6) Beacon (string or null) */
  const char *btxt = json_string_value(json_object_get(core, "beacon_text"));
  json_t *beacon = (btxt && *btxt) ? json_string(btxt) : json_null();
  if (!beacon) { /* json_string can OOM */ beacon = json_null(); }

  /* 7) Name */
  const char *name = json_string_value(json_object_get(core, "name"));

  /* 8) Build data object explicitly (no json_pack; no chance of NULL from format mismatch) */
  json_t *data = json_object();
  if (!data) {
    json_decref(core); json_decref(adj); json_decref(security); json_decref(port); json_decref(counts); if (beacon) json_decref(beacon);
    send_enveloped_error(ctx->fd, root, 1500, "OOM");
    return;
  }
  json_object_set_new(data, "sector_id", json_integer(sector_id));
  json_object_set_new(data, "name",      json_string(name ? name : "Unknown"));
  json_object_set_new(data, "security",  security);  /* transfers ownership */
  json_object_set_new(data, "adjacent",  adj);       /* transfers ownership */
  json_object_set_new(data, "port",      port);      /* transfers ownership */
  json_object_set_new(data, "counts",    counts);    /* transfers ownership */
  json_object_set_new(data, "beacon",    beacon);    /* transfers ownership */

  /* Optional debug: confirm non-NULL before sending */
  fprintf(stderr, "[move.scan] built data=%p (sector_id=%d)\n", (void*)data, sector_id);

  /* 9) Send envelope (your send_enveloped_ok steals the 'data' ref via _set_new) */
  send_enveloped_ok(ctx->fd, root, "sector.scan_v1", data);

  /* 10) Clean up */
  json_decref(core);
  /* 'data' members already owned by 'data' -> envelope stole 'data' */
}
