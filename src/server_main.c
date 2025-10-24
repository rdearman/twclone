#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>		/* for pthread_mutex_t */
#include <sqlite3.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <jansson.h>
#include <inttypes.h>
/* local includes */
#include "server_loop.h"
#include "server_config.h"
#include "s2s_keyring.h"
#include "s2s_transport.h"
#include "server_engine.h"
#include "server_s2s.h"
#include "config.h"
#include "database.h"
#include "server_bigbang.h"
#include "db_player_settings.h"


static pid_t g_engine_pid = -1;
static int g_engine_shutdown_fd = -1;
static int s2s_listen_fd = -1;
static int s2s_conn_fd = -1;
/// 
static s2s_conn_t *g_s2s_conn = NULL;
static pthread_t g_s2s_thr;
static volatile int g_s2s_run = 0;

volatile sig_atomic_t g_running = 1;	// global stop flag the loop can read
static volatile sig_atomic_t g_saw_signal = 0;

static void
on_signal (int sig)
{
  (void) sig;
  g_saw_signal = 1;
  g_running = 0;		// tell server_loop to exit
}

static void
install_signal_handlers (void)
{
  struct sigaction sa;
  memset (&sa, 0, sizeof (sa));
  sa.sa_handler = on_signal;
  sigemptyset (&sa.sa_mask);
  sa.sa_flags = 0;		// no SA_RESTART -> poll/select will EINTR
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
  signal (SIGPIPE, SIG_IGN);
}

/////////////////////////////////
static void
build_capabilities (void)
{
  if (g_capabilities)
    json_decref (g_capabilities);
  g_capabilities = json_object ();

  json_t *limits = json_object ();
  json_object_set_new (limits, "max_bulk", json_integer (100));
  json_object_set_new (limits, "max_page_size", json_integer (50));
  json_object_set_new (limits, "max_beacon_len", json_integer (256));
  json_object_set_new (g_capabilities, "limits", limits);

  json_t *features = json_object ();
  json_object_set_new (features, "auth", json_true ());
  json_object_set_new (features, "warp", json_true ());
  json_object_set_new (features, "sector.describe", json_true ());
  json_object_set_new (features, "trade.buy", json_true ());
  json_object_set_new (features, "server_autopilot", json_false ());
  json_object_set_new (g_capabilities, "features", features);

  json_object_set_new (g_capabilities, "version",
		       json_string ("1.0.0-alpha"));
}


static void
log_s2s_metrics (const char *who)
{
  uint64_t sent = 0, recv = 0, auth_fail = 0, too_big = 0;
  s2s_get_counters (&sent, &recv, &auth_fail, &too_big);
  fprintf (stderr, "[%s] s2s metrics: sent=%" PRIu64 " recv=%" PRIu64
	   " auth_fail=%" PRIu64 " too_big=%" PRIu64 "\n",
	   who, sent, recv, auth_fail, too_big);
}

static void *
s2s_control_thread (void *arg)
{
  (void) arg;
  g_s2s_run = 1;
  while (g_s2s_run)
    {
      json_t *msg = NULL;
      int rc = s2s_recv_json (g_s2s_conn, &msg, 1000);	// 1s tick; lets us notice shutdowns
      if (rc == S2S_OK && msg)
	{
	  const char *type =
	    json_string_value (json_object_get (msg, "type"));
	  if (type && strcmp (type, "s2s.health.ack") == 0)
	    {
	      // optional: read payload, surface metrics
	    }
	  else if (type && strcmp (type, "s2s.error") == 0)
	    {
	      json_t *pl = json_object_get (msg, "payload");
	      const char *reason =
		pl ? json_string_value (json_object_get (pl, "reason")) :
		NULL;
	      fprintf (stderr, "[server] s2s.error%s%s\n", reason ? ": " : "",
		       reason ? reason : "");
	    }
	  else
	    {
	      fprintf (stderr, "[server] s2s: unknown type '%s'\n",
		       type ? type : "(null)");
	    }
	  json_decref (msg);
	  continue;
	}
      // errors / idle
      if (rc == S2S_E_TIMEOUT)
	continue;		// benign idle
      if (rc == S2S_E_CLOSED)
	break;			// peer closed
      if (rc == S2S_E_AUTH_BAD || rc == S2S_E_AUTH_REQUIRED)
	{
	  fprintf (stderr, "[server] s2s auth failure; closing\n");
	  break;
	}
      if (rc == S2S_E_TOOLARGE)
	{
	  fprintf (stderr, "[server] s2s oversized frame; closing\n");
	  break;
	}
      if (rc == S2S_E_IO)
	{
	  fprintf (stderr, "[server] s2s IO error; closing\n");
	  break;
	}
    }
  return NULL;
}


//////////////////////
static int
s2s_accept_once (int lfd)
{
  struct sockaddr_in peer;
  socklen_t slen = sizeof (peer);
  return accept (lfd, (struct sockaddr *) &peer, &slen);
}

static int
send_all (int fd, const char *s)
{
  size_t n = strlen (s), off = 0;
  while (off < n)
    {
      ssize_t k = write (fd, s + off, n - off);
      if (k <= 0)
	return -1;
      off += k;
    }
  return 0;
}

static int
recv_line (int fd, char *buf, size_t cap)
{
  size_t off = 0;
  while (off + 1 < cap)
    {
      char c;
      ssize_t k = read (fd, &c, 1);
      if (k <= 0)
	return -1;
      buf[off++] = c;
      if (c == '\n')
	break;
    }
  buf[off] = '\0';
  return (int) off;
}

/////////////////////////////


/* Convenience: send a NUL-terminated C string. Returns 0 on success, -1 on error. */
static int
send_cstr (int fd, const char *s)
{
  return send_all (fd, s);
}

////////

//static int s2s_listen_fd = -1, s2s_conn_fd = -1;

static int
s2s_listen_4321 (void)
{
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
  struct sockaddr_in addr = { 0 };
  addr.sin_family = AF_INET;
  addr.sin_port = htons (4321);
  inet_pton (AF_INET, "127.0.0.1", &addr.sin_addr);
  if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0)
    return -1;
  if (listen (fd, 1) < 0)
    return -1;
  return fd;
}


// If these exist elsewhere, keep them; otherwise these prototypes silence warnings
int universe_init (void);
void universe_shutdown (void);
int load_config (void);
int load_eng_config (void);

static volatile sig_atomic_t running = 1;

/* forward decl: your bigbang entry point (adjust name/signature if different) */
int bigbang (void);		/* if your function is named differently, change this */


/*-------------------  Bigbang ---------------------------------*/

// Return first column of first row as int, or -1 on error
static int
get_scalar_int (const char *sql)
{
  sqlite3 *dbh = db_get_handle ();
  if (!dbh)
    return -1;
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL) != SQLITE_OK)
    return -1;
  int rc = sqlite3_step (st);
  int v = (rc == SQLITE_ROW) ? sqlite3_column_int (st, 0) : -1;
  sqlite3_finalize (st);
  return v;
}

// Decide if we need to run bigbang on this DB
static int
needs_bigbang (void)
{
  // Primary flag: PRAGMA user_version
  int uv = get_scalar_int ("PRAGMA user_version");
  if (uv > 0)
    return 0;			// already seeded

  // Belt-and-braces: look at contents in case user_version wasn't set
  int sectors = get_scalar_int ("SELECT COUNT(*) FROM sectors");
  int warps = get_scalar_int ("SELECT COUNT(*) FROM sector_warps");
  int ports = get_scalar_int ("SELECT COUNT(*) FROM ports");

  if (sectors <= 10)
    return 1;			// only the 10 Fedspace rows exist
  if (warps == 0)
    return 1;
  if (ports == 0)
    return 1;

  return 0;
}

// Run bigbang once; mark DB as seeded so we never do it again
static int
run_bigbang_if_needed (void)
{
  if (!needs_bigbang ())
    return 0;

  sqlite3 *dbh = db_get_handle ();
  if (!dbh)
    {
      fprintf (stderr, "BIGBANG: DB handle unavailable.\n");
      return -1;
    }

  fprintf (stderr, "BIGBANG: Universe appears empty — seeding now...\n");
  if (bigbang () != 0)
    {
      fprintf (stderr, "BIGBANG: Failed.\n");
      return -1;
    }

}

//////////////////////////////////////////////////
int
main (void)
{
  int rc;
  g_running = 1;

  /* 0) DB: open (create schema/defaults if missing) */
  if (db_init () != 0)
    {
      fprintf (stderr, "Failed to init DB.\n");
      return EXIT_FAILURE;
    }

  /* Optional: keep a sanity check/log, but don't gate db_init() on it. */
  (void) load_config ();

  if (universe_init () != 0)
    {
      fprintf (stderr, "Failed to init universe.\n");
      db_close ();
      return EXIT_FAILURE;
    }

  if (run_bigbang_if_needed () != 0)
    {
      return EXIT_FAILURE;	// or your project’s error path
    }


  // normal startup
  if (!load_eng_config ())
    return 2;

  // initalise the player settings if all the other DB stuff is done. 
  db_player_settings_init (db_get_handle ());


  /* 0.1) Capabilities (restored) */
  build_capabilities ();	/* rebuilds g_capabilities */

  /* 0.2) Signals (restored) */
  install_signal_handlers ();	/* restores Ctrl-C / SIGTERM behavior */


  /* 1) S2S keyring (must be before we bring up TCP) */
  fprintf (stderr, "[server] loading s2s key ...\n");
  if (s2s_install_default_key (db_get_handle ()) != 0)
    {
      fprintf (stderr, "[server] FATAL: S2S key missing/invalid\n");
      return EXIT_FAILURE;
    }

  /* 2) S2S listener on 127.0.0.1:4321 for engine control link */
  s2s_listen_fd = s2s_listen_4321 ();
  if (s2s_listen_fd < 0)
    {
      fprintf (stderr, "[server] listen on 4321 failed\n");
      return EXIT_FAILURE;
    }
  /* NEW: prevent child from inheriting this fd on exec */
  int fl = fcntl (s2s_listen_fd, F_GETFD, 0);
  if (fl != -1)
    {
      fcntl (s2s_listen_fd, F_SETFD, fl | FD_CLOEXEC);
    }
  fprintf (stderr, "[server] s2s listen on 127.0.0.1:4321\n");


  /* 3) Fork the engine (child connects back to 4321) */
  fprintf (stderr, "[server] forking engine…\n");
  if (engine_spawn (&g_engine_pid, &g_engine_shutdown_fd) != 0)
    {
      fprintf (stderr, "[server] engine fork failed\n");
      return EXIT_FAILURE;
    }
  fprintf (stderr, "[server] engine forked. pid=%d\n", g_engine_pid);

  /* 4) Accept one engine connection, complete JSON handshake: recv hello → send ack */
  fprintf (stderr, "[server] accepting engine…\n");
  s2s_conn_t *conn = s2s_tcp_server_accept (s2s_listen_fd, 5000);
  if (!conn)
    {
      fprintf (stderr, "[server] accept failed\n");
      goto shutdown_and_exit;
    }
  fprintf (stderr, "[server] accepted s2s\n");
  s2s_debug_dump_conn ("server", conn);

  /* Receive engine hello first */
  json_t *msg = NULL;
  rc = s2s_recv_json (conn, &msg, 5000);
  fprintf (stderr, "[server] first frame rc=%d\n", rc);
  if (rc == S2S_OK && msg)
    {
      const char *type = json_string_value (json_object_get (msg, "type"));
      if (type && strcmp (type, "s2s.health.hello") == 0)
	{
	  fprintf (stderr, "[server] accepted hello\n");

	  time_t now = time (NULL);
	  json_t *ack = json_pack ("{s:i,s:s,s:s,s:I,s:o}",
				   "v", 1,
				   "type", "s2s.health.ack",
				   "id", "boot-ack",
				   "ts", (json_int_t) now,
				   "payload", json_pack ("{s:s}", "status",
							 "ok"));
	  int rc2 = s2s_send_json (conn, ack, 5000);
	  fprintf (stderr, "[server] ack send rc=%d\n", rc2);
	  json_decref (ack);

	  fprintf (stderr, "[server] Return Ping\n");
	  if (server_s2s_start (conn, &g_s2s_thr, &g_running) != 0)
	    {
	      fprintf (stderr,
		       "[server] failed to start s2s control thread\n");
	    }
	}
      else
	{
	  fprintf (stderr, "[server] unexpected type on first frame: %s\n",
		   type ? type : "(null)");
	}
      json_decref (msg);
    }
  else
    {
      fprintf (stderr, "[server] no hello from engine (rc=%d)\n", rc);
      /* continue anyway; control thread can handle retries later */
    }

  /* (Single engine) optionally close the listener now */
  /* close (s2s_listen_fd); s2s_listen_fd = -1; */

  /* 5) Park conn and start S2S control thread AFTER handshake */
  g_s2s_conn = conn;
  g_s2s_run = 1;
  pthread_create (&g_s2s_thr, NULL, s2s_control_thread, NULL);

  /* 6) Run the server loop (unchanged behavior/logs) */
  fprintf (stderr, "Server loop starting...\n");
  rc = server_loop (&g_running);
  fprintf (stderr, "Server loop exiting...\n");

  /* 7) Teardown in the right order:
     - stop control thread (s2s_close unblocks recv)
     - close listener
     - request engine shutdown & reap
   */
  g_s2s_run = 0;
  if (g_s2s_conn)
    {
      s2s_close (g_s2s_conn);
      g_s2s_conn = NULL;
    }				// unblocks thread
  pthread_join (g_s2s_thr, NULL);

  if (s2s_listen_fd >= 0)
    {
      close (s2s_listen_fd);
      s2s_listen_fd = -1;
    }

shutdown_and_exit:

  /* Ask engine to shut down and reap it (preserves your previous logic) */
  if (g_engine_shutdown_fd >= 0)
    {
      engine_request_shutdown (g_engine_shutdown_fd);	/* close pipe -> child exits */
      g_engine_shutdown_fd = -1;
    }
  if (g_engine_pid > 0)
    {
      int waited = engine_wait (g_engine_pid, 3000);	/* wait up to 3s */
      if (waited == 1)
	{
	  fprintf (stderr,
		   "[server] engine still running; sending SIGTERM.\n");
	  kill (g_engine_pid, SIGTERM);
	  (void) engine_wait (g_engine_pid, 2000);
	}
      g_engine_pid = -1;
    }

  /* 8) Capabilities cleanup */
  if (g_capabilities)
    {
      json_decref (g_capabilities);
      g_capabilities = NULL;
    }

  /* if (conn) */
  /*   { */
  /*     s2s_close (conn); */
  /*     conn = NULL; */
  /*   } */
  /* server_s2s_stop (g_s2s_thr); */
  /* g_s2s_thr = 0; */

  return (rc == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}


/* Returns 1 if we can open twconfig.db and read at least one row from config; else 0. */
int
load_config (void)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *stmt = NULL;
  int rc;

  rc = sqlite3_open (DEFAULT_DB_NAME, &db);
  if (rc != SQLITE_OK)
    {
      /* fprintf(stderr, "sqlite3_open: %s\n", sqlite3_errmsg(db)); */
      if (db)
	sqlite3_close (db);
      return 0;
    }

  rc =
    sqlite3_prepare_v2 (db, "SELECT 1 FROM config LIMIT 1;", -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      /* fprintf(stderr, "sqlite3_prepare_v2: %s\n", sqlite3_errmsg(db)); */
      sqlite3_close (db);
      return 0;
    }

  rc = sqlite3_step (stmt);
  /* SQLITE_ROW means at least one row exists */
  int ok = (rc == SQLITE_ROW) ? 1 : 0;

  sqlite3_finalize (stmt);
  sqlite3_close (db);
  return ok;
}
