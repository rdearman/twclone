#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
#include "server_ports.h"	// at top of server_loop.c
#include "server_auth.h"
#include "server_s2s.h"
#include "server_universe.h"
#include "server_config.h"
#include "server_communication.h"
#include "server_players.h"
#include "server_planets.h"
#include "server_citadel.h"
#include "server_combat.h"
#include "server_bulk.h"
#include "server_news.h"
#include "server_log.h"


#ifndef streq
#define streq(a,b) (strcasecmp(json_string_value((a)), (b))==0)
#endif
#define LISTEN_PORT 1234
#define BUF_SIZE    8192
#define RULE_REFUSE(_code,_msg,_hint_json) \
    do { send_enveloped_refused(ctx->fd, root, (_code), (_msg), (_hint_json)); goto trade_buy_done; } while (0)

#define RULE_ERROR(_code,_msg) \
    do { send_enveloped_error(ctx->fd, root, (_code), (_msg)); goto trade_buy_done; } while (0)

// static _Atomic uint64_t g_conn_seq = 0;
/* global (file-scope) counter for server message ids */
// static _Atomic uint64_t g_msg_seq = 0;
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
int db_sector_scan_core (int sector_id, json_t ** out_obj);
int db_players_at_sector_json (int sector_id, json_t ** out_array);
int db_beacons_at_sector_json (int sector_id, json_t ** out_array);
int db_planets_at_sector_json (int sector_id, json_t ** out_array);
int db_player_set_sector (int player_id, int sector_id);
int db_player_get_sector (int player_id, int *out_sector);
void handle_sector_info (int fd, json_t * root, int sector_id, int player_id);
void handle_sector_set_beacon (client_ctx_t * ctx, json_t * root);
/* Fast sector scan handler (IDs+counts only) */
void handle_move_scan (client_ctx_t * ctx, json_t * root);
void handle_move_pathfind (client_ctx_t * ctx, json_t * root);
void send_enveloped_ok (int fd, json_t * root, const char *type,
			json_t * data);
json_t *build_sector_info_json (int sector_id);
// static int64_t g_next_notice_ttl_sweep = 0;



static const cmd_desc_t k_supported_cmds_fallback[] = {
  // --- Session / System ---
  {"session.hello", "Handshake / hello"},
  {"session.ping", "Ping"},
  {"session.goodbye", "Client disconnect"},

  {"system.schema_list", "List schema namespaces"},
  {"system.describe_schema", "Describe commands in a schema"},
  {"system.capabilities", "Feature flags, schemas, counts"},
  {"system.cmd_list", "Flat list of all commands"},

  // --- Auth ---
  {"auth.login", "Authenticate"},
  {"auth.logout", "Log out"},
  {"auth.mfa", "Second-factor code"},
  {"auth.register", "Create a new player"},

  // --- Players / Ship ---
  {"players.me", "Current player info"},
  {"players.online", "List online players"},
  {"players.refresh", "Refresh player state"},
  {"ship.info", "Ship information"},

  // --- Sector / Movement ---
  {"sector.info", "Describe current sector"},
  {"sector.set_beacon", "Set or clear sector beacon"},

  {"move.warp", "Warp to sector"},
  {"move.scan", "Scan adjacent sectors"},
  {"move.pathfind", "Find path between sectors"},
  {"move.force_move", "Admin: force-move a ship"},

  // --- Trade / Commerce (Extended) ---
  {"trade.port_info", "Port prices/stock in sector"},
  {"trade.buy", "Buy commodity from port"},
  {"trade.sell", "Sell commodity to port"},
  {"trade.quote", "Get a quote for a trade action (Optional)"},
  {"trade.offer", "Create a private player-to-player trade offer"},
  {"trade.accept", "Accept a private trade offer"},
  {"trade.cancel", "Cancel a pending trade offer"},
  {"trade.history", "View recent trade transactions"},
  {"trade.jettison", "Dump cargo into space (Optional)"},

  // --- News ---
  {"news.read", "Get the daily news feed"},

  // --- Combat ---
  {"fighters.recall", "Recall deployed fighters"},
  {"combat.deploy_mines", "Deploy mines"},
  {"mines.recall", "Recall deployed mines"},
};

// Weak fallback: satisfies server_envelope.o at link time.
// If server_loop.c defines a strong version, it will override this.
__attribute__((weak))
     void
     loop_get_supported_commands (const cmd_desc_t **out_tbl, size_t *out_n)
{
  if (out_tbl)
    *out_tbl = k_supported_cmds_fallback;
  if (out_n)
    *out_n =
      sizeof (k_supported_cmds_fallback) /
      sizeof (k_supported_cmds_fallback[0]);
}




static inline uint64_t
monotonic_millis (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}


/* ===== Client registry for broadcasts  ===== */


/* Broadcast a system.notice to everyone (uses subscription infra).
   Data is BORROWED here; we incref for the call. */
static void
server_broadcast_to_all_online (json_t *data)
{
  server_broadcast_event ("system.notice", json_incref (data));
  json_decref (data);
}

/* Sector-scoped, ephemeral event to subscribers of sector.* / sector.{id}.
   NOTE: comm_publish_sector_event STEALS a ref to 'data'. */
/*
static void
server_broadcast_to_sector (int sector_id, const char *event_type,
			    json_t *data)
{
  comm_publish_sector_event (sector_id, event_type, data);	// steals 'data'
}
*/



static int
broadcast_sweep_once (sqlite3 *db, int max_rows)
{
  static const char *SQL_SEL =
    "SELECT id, created_at, title, body, severity, expires_at "
    "FROM system_notice "
    "WHERE id NOT IN (SELECT DISTINCT notice_id FROM notice_seen WHERE player_id=0) "
    "ORDER BY created_at ASC, id ASC LIMIT ?1;";

  sqlite3_stmt *st = NULL;
  int processed = 0;

  if (max_rows <= 0 || max_rows > 1000)
    max_rows = 64;

  if (sqlite3_prepare_v2 (db, SQL_SEL, -1, &st, NULL) != SQLITE_OK)
    {
      return 0;
    }
  sqlite3_bind_int (st, 1, max_rows);

  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int id = sqlite3_column_int (st, 0);
      int created_at = sqlite3_column_int (st, 1);
      const char *title = (const char *) sqlite3_column_text (st, 2);
      const char *body = (const char *) sqlite3_column_text (st, 3);
      const char *severity = (const char *) sqlite3_column_text (st, 4);
      int expires_at =
	(sqlite3_column_type (st, 5) ==
	 SQLITE_NULL) ? 0 : sqlite3_column_int (st, 5);

      json_t *data = json_pack ("{s:i, s:i, s:s, s:s, s:s, s:O}",
				"id", id,
				"ts", created_at,
				"title", title ? title : "",
				"body", body ? body : "",
				"severity", severity ? severity : "info",
				"expires_at",
				expires_at ? json_integer (expires_at) :
				json_null ());

      server_broadcast_to_all_online (json_incref (data));	/* fan-out live */
      json_decref (data);

      /* mark-as-published sentinel (player_id=0) to avoid re-sending */
      sqlite3_stmt *st2 = NULL;
      if (sqlite3_prepare_v2 (db,
			      "INSERT OR IGNORE INTO notice_seen(notice_id, player_id, seen_at) "
			      "VALUES(?1, 0, strftime('%s','now'));",
			      -1, &st2, NULL) == SQLITE_OK)
	{
	  sqlite3_bind_int (st2, 1, id);
	  (void) sqlite3_step (st2);
	}
      if (st2)
	sqlite3_finalize (st2);

      processed++;
    }

  sqlite3_finalize (st);
  return processed;
}


/* ===== Client registry for broadcasts (#195) ===== */

typedef struct client_node_s
{
  client_ctx_t *ctx;
  struct client_node_s *next;
} client_node_t;

static client_node_t *g_clients = NULL;
static pthread_mutex_t g_clients_mu = PTHREAD_MUTEX_INITIALIZER;

void
server_register_client (client_ctx_t *ctx)
{
  pthread_mutex_lock (&g_clients_mu);
  client_node_t *n = (client_node_t *) calloc (1, sizeof (*n));
  if (n)
    {
      n->ctx = ctx;
      n->next = g_clients;
      g_clients = n;
    }
  pthread_mutex_unlock (&g_clients_mu);
}

void
server_unregister_client (client_ctx_t *ctx)
{
  pthread_mutex_lock (&g_clients_mu);
  client_node_t **pp = &g_clients;
  while (*pp)
    {
      if ((*pp)->ctx == ctx)
	{
	  client_node_t *dead = *pp;
	  *pp = (*pp)->next;
	  free (dead);
	  break;
	}
      pp = &((*pp)->next);
    }
  pthread_mutex_unlock (&g_clients_mu);
}

/* Deliver an envelope (type+data) to any online socket for player_id. */
int
server_deliver_to_player (int player_id, const char *event_type, json_t *data)
{
  int delivered = 0;

  pthread_mutex_lock (&g_clients_mu);
  for (client_node_t * n = g_clients; n; n = n->next)
    {
      client_ctx_t *c = n->ctx;
      if (!c)
	continue;
      if (c->player_id == player_id && c->fd >= 0)
	{
	  /* send_enveloped_ok does its own timestamp/meta/sanitize. */
	  send_enveloped_ok (c->fd, NULL, event_type, data);
	  delivered++;
	}
    }
  pthread_mutex_unlock (&g_clients_mu);

  return (delivered > 0) ? 0 : -1;
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
void
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

/*
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
*/


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
  int rc = 0;

/* ---------- AUTH / USER ---------- */
  if (!strcasecmp (c, "login") || !strcasecmp (c, "auth.login"))
    {
      rc = cmd_auth_login (ctx, root);
      if (rc)
	{
	  push_unseen_notices_for_player (ctx, ctx->player_id);
	}
    }
  else if (!strcasecmp (c, "auth.register"))
    {
      rc = cmd_auth_register (ctx, root);
    }
  else if (!strcasecmp (c, "auth.logout"))
    {
      rc = cmd_auth_logout (ctx, root);
    }
  // use auth.register
  /* else if (!strcmp (c, "user.create") || !strcmp (c, "new.user")) */
  /*   { */
  /*     rc = cmd_user_create (ctx, root); */
  /*   } */
  else if (!strcasecmp (c, "auth.refresh"))
    {
      rc = cmd_auth_refresh (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "auth.mfa.totp.verify"))
    {
      rc = cmd_auth_mfa_totp_verify (ctx, root);	/* NIY stub */
    }

/* ---------- SYSTEM / SESSION ---------- */
  else if (!strcasecmp (c, "system.capabilities"))
    {
      rc = cmd_system_capabilities (ctx, root);
    }
  else if (!strcasecmp (c, "system.describe_schema"))
    {
      rc = cmd_system_describe_schema (ctx, root);
    }
  else if ((!strcasecmp (c, "session.ping")) || (!strcasecmp (c, "session.hello")) || (!strcasecmp (c, "system.hello")))
    {
      rc = cmd_system_hello (ctx, root); 
    }
  else if (!strcasecmp (c, "session.disconnect")
	   || !strcasecmp (c, "system.disconnect"))
    {
      rc = cmd_session_disconnect (ctx, root);	/* NIY stub */
    }
  else if (streq (cmd, "system.cmd_list"))
    {
      cmd_system_cmd_list (ctx, root);
    }
  else if (streq (cmd, "system.describe_schema"))
    {
      cmd_system_describe_schema (ctx, root);
    }
  // (If not already present:)
  else if (streq (cmd, "system.schema_list"))
    {
      cmd_system_schema_list (ctx, root);
    }
  else if (streq (cmd, "system.capabilities"))
    {
      cmd_system_capabilities (ctx, root);
    }
  else if (streq (cmd, "sys.test_news_cron"))
    {
      rc = cmd_sys_test_news_cron (ctx, root);
    }
  else if (streq (cmd, "sys.raw_sql_exec"))
    {
      rc = cmd_sys_raw_sql_exec (ctx, root);
    }


/* ---------- PLAYER ---------- */
  else if (streq (cmd, "player.get_settings"))
    {
      rc = cmd_player_get_settings (ctx, root);
    }
  else if (streq (cmd, "bank.deposit"))
    {
      rc = cmd_bank_deposit (ctx, root);
    }
  else if (streq (cmd, "bank.transfer"))
    {
      rc = cmd_bank_transfer (ctx, root);
    }
  else if (streq (cmd, "bank.withdraw"))
    {
      rc = cmd_bank_withdraw (ctx, root);
    }

  else if (streq (cmd, "player.my_info"))
    {
      cmd_player_my_info (ctx, root);
    }
  else if (streq (cmd, "player.list_online"))
    {
      cmd_player_list_online (ctx, root);
    }
  else if (streq (cmd, "player.get_settings"))
    {
      cmd_player_get_settings (ctx, root);
    }
  else if (streq (cmd, "player.set_settings"))
    {
      cmd_player_set_settings (ctx, root);
    }

  else if (streq (cmd, "player.get_prefs"))
    {
      cmd_player_get_prefs (ctx, root);
    }
  else if (streq (cmd, "player.set_prefs"))
    {
      cmd_player_set_prefs (ctx, root);
    }

  else if (streq (cmd, "player.get_topics") ||
	   streq (cmd, "player.get_subscriptions"))
    {
      cmd_player_get_topics (ctx, root);
    }
  else if (streq (cmd, "player.set_topics") ||
	   streq (cmd, "player.set_subscriptions"))
    {
      cmd_player_set_topics (ctx, root);
    }

  else if (streq (cmd, "player.get_bookmarks") ||
	   streq (cmd, "nav.bookmark.list"))
    {
      cmd_player_get_bookmarks (ctx, root);
    }
  else if (streq (cmd, "player.set_bookmarks") ||
	   streq (cmd, "nav.bookmark.set") ||
	   streq (cmd, "nav.bookmark.add") ||
	   streq (cmd, "nav.bookmark.remove"))
    {
      cmd_player_set_bookmarks (ctx, root);
    }

  else if (streq (cmd, "nav.avoid.add"))
    {
      cmd_nav_avoid_add (ctx, root);
    }
  else if (streq (cmd, "nav.avoid.remove"))
    {
      cmd_nav_avoid_remove (ctx, root);
    }
  else if (streq (cmd, "nav.avoid.list"))
    {
      cmd_nav_avoid_list (ctx, root);
    }

  else if (streq (cmd, "player.get_avoids") || streq (cmd, "nav.avoid.list"))
    {
      cmd_player_get_avoids (ctx, root);
    }
  else if (streq (cmd, "player.set_avoids") ||
	   streq (cmd, "nav.avoid.set") || streq (cmd, "nav.avoid.add"))
    {
      cmd_player_set_avoids (ctx, root);
    }

  else if (streq (cmd, "player.get_notes") || streq (cmd, "notes.list"))
    {
      cmd_player_get_notes (ctx, root);
    }

  else if (streq (cmd, "player.get_settings"))
    {
      cmd_player_get_settings (ctx, root);
    }
  else if (streq (cmd, "player.set_settings"))
    {
      cmd_player_set_settings (ctx, root);
    }

  else if (streq (cmd, "player.get_prefs"))
    {
      cmd_player_get_prefs (ctx, root);
    }
  else if (streq (cmd, "player.set_prefs"))
    {
      cmd_player_set_prefs (ctx, root);
    }

  else if (streq (cmd, "player.get_topics")
	   || streq (cmd, "player.get_subscriptions"))
    {
      cmd_player_get_topics (ctx, root);
    }
  else if (streq (cmd, "player.set_topics")
	   || streq (cmd, "player.set_subscriptions"))
    {
      cmd_player_set_topics (ctx, root);
    }

  else if (streq (cmd, "player.get_bookmarks")
	   || streq (cmd, "nav.bookmark.list"))
    {
      cmd_player_get_bookmarks (ctx, root);
    }
  else if (streq (cmd, "player.set_bookmarks") ||
	   streq (cmd, "nav.bookmark.set") ||
	   streq (cmd, "nav.bookmark.add") ||
	   streq (cmd, "nav.bookmark.remove"))
    {
      cmd_player_set_bookmarks (ctx, root);
    }

  else if (streq (cmd, "player.get_avoids") || streq (cmd, "nav.avoid.list"))
    {
      cmd_player_get_avoids (ctx, root);
    }
  else if (streq (cmd, "player.set_avoids") ||
	   streq (cmd, "nav.avoid.set") ||
	   streq (cmd, "nav.avoid.add") || streq (cmd, "nav.avoid.remove"))
    {
      cmd_player_set_avoids (ctx, root);
    }

  else if (streq (cmd, "player.get_notes") || streq (cmd, "notes.list"))
    {
      cmd_player_get_notes (ctx, root);
    }

/* ---------- SHIP ---------- */
  else if (!strcasecmp (c, "ship.inspect"))
    {
      rc = cmd_ship_inspect (ctx, root);
    }
  else if (!strcasecmp (c, "ship.rename") || !strcasecmp (c, "ship.reregister"))
    {
      rc = cmd_ship_rename (ctx, root);
    }
  else if (!strcasecmp (c, "ship.claim"))
    {
      rc = cmd_ship_claim (ctx, root);
    }
  else if (!strcasecmp (c, "ship.status"))
    {
      rc = cmd_ship_status (ctx, root);
    }
  else if (!strcasecmp (c, "ship.info"))
    {
      rc = cmd_ship_info_compat (ctx, root);	/* legacy alias */
    }
  else if (!strcasecmp (c, "ship.transfer_cargo"))
    {
      rc = cmd_ship_transfer_cargo (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "ship.jettison"))
    {
      rc = cmd_ship_jettison (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "ship.upgrade"))
    {
      rc = cmd_ship_upgrade (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "ship.repair"))
    {
      rc = cmd_ship_repair (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "ship.self_destruct"))
    {
      rc = cmd_ship_self_destruct (ctx, root);	/* NIY stub */
    }

/* ---------- PORTS / TRADE ---------- */
  else if (!strcasecmp (c, "port.info") || !strcasecmp (c, "port.status")
	   || !strcasecmp (c, "trade.port_info") || !strcasecmp (c, "port.describe"))
    {
      rc = cmd_trade_port_info (ctx, root);
    }
  else if (!strcasecmp (c, "trade.buy"))
    {
      rc = cmd_trade_buy (ctx, root);
    }
  else if (!strcasecmp (c, "trade.sell"))
    {
      rc = cmd_trade_sell (ctx, root);	/* NIY stub (or real) */
    }
  else if (!strcasecmp (c, "trade.quote"))
    {
      rc = cmd_trade_quote (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "trade.jettison"))
    {
      rc = cmd_ship_jettison (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "trade.offer"))
    {
      rc = cmd_trade_offer (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "trade.accept"))
    {
      rc = cmd_trade_accept (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "trade.cancel"))
    {
      rc = cmd_trade_cancel (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "trade.history"))
    {
      rc = cmd_trade_history (ctx, root);	/* NIY stub */
    }

/* ---------- UNIVERSE / SECTOR / MOVE ---------- */
  else if (!strcasecmp (c, "move.describe_sector") || !strcasecmp (c, "sector.info"))
    {
      rc = cmd_move_describe_sector (ctx, root);	/* NIY or real */
    }
  else if (!strcasecmp (c, "move.scan"))
    {
      cmd_move_scan (ctx, root);	/* NIY or real */
    }
  else if (!strcasecmp (c, "move.warp"))
    {
      rc = cmd_move_warp (ctx, root);	/* NIY or real */
    }
  else if (!strcasecmp (c, "move.pathfind"))
    {
      rc = cmd_move_pathfind (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "move.autopilot.start"))
    {
      rc = cmd_move_autopilot_start (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "move.autopilot.stop"))
    {
      rc = cmd_move_autopilot_stop (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "move.autopilot.status"))
    {
      rc = cmd_move_autopilot_status (ctx, root);
    }
  else if (!strcasecmp (c, "sector.search"))
    {
      rc = cmd_sector_search (ctx, root);
    }
  else if (!strcasecmp (c, "sector.set_beacon"))
    {
      rc = cmd_sector_set_beacon (ctx, root);
    }
  else if (!strcasecmp (c, "sector.scan.density"))
    {
      cmd_sector_scan_density (ctx, root); 
    }
  else if (!strcasecmp (c, "sector.scan"))
    {
      cmd_sector_scan (ctx, root);	
    }

/* ---------- PLANETS / CITADEL ---------- */
  else if (!strcasecmp (c, "planet.genesis"))
    {
      rc = cmd_planet_genesis (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "planet.info"))
    {
      rc = cmd_planet_info (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "planet.rename"))
    {
      rc = cmd_planet_rename (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "planet.land"))
    {
      rc = cmd_planet_land (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "planet.launch"))
    {
      rc = cmd_planet_launch (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "planet.transfer_ownership"))
    {
      rc = cmd_planet_transfer_ownership (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "planet.harvest"))
    {
      rc = cmd_planet_harvest (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "planet.deposit"))
    {
      rc = cmd_planet_deposit (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "planet.withdraw"))
    {
      rc = cmd_planet_withdraw (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "citadel.build"))
    {
      rc = cmd_citadel_build (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "citadel.upgrade"))
    {
      rc = cmd_citadel_upgrade (ctx, root);	/* NIY stub */
    }

/* ---------- COMBAT ---------- */
  else if (!strcasecmp (c, "combat.attack"))
    {
      rc = cmd_combat_attack (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "combat.deploy_fighters"))
    {
      rc = cmd_combat_deploy_fighters (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "combat.lay_mines"))
    {
      rc = cmd_combat_lay_mines (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "combat.sweep_mines"))
    {
      rc = cmd_combat_sweep_mines (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "combat.status"))
    {
      rc = cmd_combat_status (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "deploy.fighters.list"))
    {
      rc = cmd_deploy_fighters_list (ctx, root);        /* list deployed fighters */
    }
  else if (!strcasecmp (c, "deploy.mines.list"))
    {
      rc = cmd_deploy_mines_list (ctx, root);           /* list deployed mines */
    }  
  else if (!strcasecmp (c, "fighters.recall"))
    {
      rc = cmd_fighters_recall (ctx, root);             /* recall deployed fighters */
    }
  else if (!strcasecmp (c, "combat.deploy_mines"))
    {
      rc = cmd_combat_deploy_mines (ctx, root);
    }
  else if (!strcasecmp (c, "mines.recall"))
    {
      rc = cmd_mines_recall (ctx, root);
    }

/* ---------- CHAT ---------- */
  else if (!strcasecmp (c, "chat.send"))
    {
      rc = cmd_chat_send (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "chat.broadcast"))
    {
      rc = cmd_chat_broadcast (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "chat.history"))
    {
      rc = cmd_chat_history (ctx, root);	/* NIY stub */
    }

/* ---------- MAIL/Announcements ---------- */
  else if (!strcasecmp (c, "mail.send"))
    {
      rc = cmd_mail_send (ctx, root);
    }
  else if (!strcasecmp (c, "mail.inbox"))
    {
      rc = cmd_mail_inbox (ctx, root);
    }
  else if (!strcasecmp (c, "mail.read"))
    {
      rc = cmd_mail_read (ctx, root);
    }
  else if (!strcasecmp (c, "mail.delete"))
    {
      rc = cmd_mail_delete (ctx, root);
    }
  else if (strcasecmp (c, "sys.notice.create") == 0)
    {
      rc = cmd_sys_notice_create (ctx, root);
    }
  else if (strcasecmp (c, "notice.list") == 0)
    {
      rc = cmd_notice_list (ctx, root);
    }
  else if (strcasecmp (c, "notice.ack") == 0)
    {
      rc = cmd_notice_ack (ctx, root);
    }
/* ---------- NEWS ---------- */
  else if (strcasecmp (c, "news.get_feed") == 0)
    {
      rc = cmd_news_get_feed (ctx, root);
    }
  else if (strcasecmp (c, "news.mark_feed_read") == 0)
    {
      rc = cmd_news_mark_feed_read (ctx, root);
    }

/* ---------- SUBSCRIBE ---------- */
  else if (!strcasecmp (c, "subscribe.add"))
    {
      rc = cmd_subscribe_add (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "subscribe.remove"))
    {
      rc = cmd_subscribe_remove (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "subscribe.list"))
    {
      rc = cmd_subscribe_list (ctx, root);	/* NIY stub */
    }
  else if (strcasecmp (c, "subscribe.catalog") == 0)
    {
      cmd_subscribe_catalog (ctx, root);
    }

/* ---------- BULK ---------- */
  else if (!strcasecmp (c, "bulk.execute"))
    {
      rc = cmd_bulk_execute (ctx, root);	/* NIY stub */
    }

/* ---------- ADMIN ---------- */
  else if (!strcasecmp (c, "admin.notice"))
    {
      rc = cmd_admin_notice (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "admin.shutdown_warning"))
    {
      rc = cmd_admin_shutdown_warning (ctx, root);	/* NIY stub */
    }

  /* ---------- S2S ---------- */
  else if (!strcasecmp (c, "s2s.planet.genesis"))
    {
      rc = cmd_s2s_planet_genesis (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "s2s.planet.transfer"))
    {
      rc = cmd_s2s_planet_transfer (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "s2s.player.migrate"))
    {
      rc = cmd_s2s_player_migrate (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "s2s.port.restock"))
    {
      rc = cmd_s2s_port_restock (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "s2s.event.relay"))
    {
      rc = cmd_s2s_event_relay (ctx, root);	/* NIY stub */
    }
  else if (!strcasecmp (c, "s2s.replication.heartbeat"))
    {
      rc = cmd_s2s_replication_heartbeat (ctx, root);	/* NIY stub */
    }

/* ---------- FALLBACK ---------- */
  else
    {
      send_enveloped_error (ctx->fd, root, 1400, "Unknown command");
      rc = 0;
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
  LOGI ("Server loop starting...\n");
  //  fprintf (stderr, "Server loop starting...\n");

#ifdef SIGPIPE
  signal (SIGPIPE, SIG_IGN);	/* don’t die on write to closed socket */
#endif

  int listen_fd = make_listen_socket (LISTEN_PORT);
  if (listen_fd < 0)
    {
      LOGE ("Server loop exiting due to listen socket error.\n");
      //      fprintf (stderr, "Server loop exiting due to listen socket error.\n");
      return -1;
    }
  LOGI ("Listening on 0.0.0.0:%d\n", LISTEN_PORT);
  //  fprintf (stderr, "Listening on 0.0.0.0:%d\n", LISTEN_PORT);

  struct pollfd pfd = {.fd = listen_fd,.events = POLLIN,.revents = 0 };

  pthread_attr_t attr;
  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);

  while (*running)
    {
      int rc = poll (&pfd, 1, 100);	/* was 1000; 100ms gives us a ~10Hz tick */
      // int rc = poll (&pfd, 1, 1000); /* 1s tick re-checks *running */

      /* === Broadcast pump tick (every ~500ms) === */
      {
	static uint64_t last_broadcast_ms = 0;
	uint64_t now_ms = monotonic_millis ();

	if (now_ms - last_broadcast_ms >= 500)
	  {
	    (void) broadcast_sweep_once (db_get_handle (), 64);
	    last_broadcast_ms = now_ms;
	  }
      }

      if (rc < 0)
	{
	  if (errno == EINTR)
	    continue;
	  perror ("poll");
	  break;
	}

      if (rc == 0)
	{
	  /* timeout only: we still ran the pump above; just loop again */
	  continue;
	}

      if (pfd.revents & POLLIN)
	{
	  client_ctx_t *ctx = calloc (1, sizeof (*ctx));
	  if (!ctx)
	    {
	      LOGE ("malloc failed\n");
	      //              fprintf (stderr, "malloc failed\n");
	      continue;
	    }
	  server_register_client (ctx);

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
	  LOGI ("Client connected: %s:%u (fd=%d)\n",
		ip, (unsigned) ntohs (ctx->peer.sin_port), cfd);

	  //      fprintf (stderr, "Client connected: %s:%u (fd=%d)\n",
	  //       ip, (unsigned) ntohs (ctx->peer.sin_port), cfd);

	  // after filling ctx->fd, ctx->running, ctx->peer, and assigning ctx->cid
	  pthread_t th;
	  int prc = pthread_create (&th, &attr, connection_thread, ctx);
	  if (prc == 0)
	    {
	      LOGI ("[cid=%" PRIu64 "] thread created (pthread=%lu)\n",
		    ctx->cid, (unsigned long) th);

	      //              fprintf (stderr,
	      //       "[cid=%" PRIu64 "] thread created (pthread=%lu)\n",
	      //       ctx->cid, (unsigned long) th);
	    }
	  else
	    {
	      LOGE ("pthread_create: %s\n", strerror (prc));
	      //              fprintf (stderr, "pthread_create: %s\n", strerror (prc));
	      close (cfd);
	      free (ctx);
	    }

	}
    }

  // server_unregister_client(ctx);
  pthread_attr_destroy (&attr);
  close (listen_fd);
  LOGI ("Server loop exiting...\n");
  //  fprintf (stderr, "Server loop exiting...\n");
  return 0;
}
