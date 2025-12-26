#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>            /* for pthread_mutex_t */
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <time.h>
#include <jansson.h>
#include <inttypes.h>
#include <strings.h>
/* local includes */
#include "server_loop.h"
#include "server_config.h"
#include "s2s_keyring.h"
#include "s2s_transport.h"
#include "server_engine.h"
#include "server_s2s.h"

#include "game_db.h"  // Include the new game_db header
#include "db_player_settings.h"
#include "server_log.h" // Explicitly include server_log.h
#include "sysop_interaction.h" // Explicitly include sysop_interaction.h
#include "server_cron.h"
static pid_t g_engine_pid = -1;
static int g_engine_shutdown_fd = -1;
static int s2s_listen_fd = -1;

static s2s_conn_t *g_s2s_conn = NULL;
static pthread_t g_s2s_thr;
static volatile int g_s2s_run = 0;
volatile sig_atomic_t g_running = 1;    // global stop flag the loop can read
static volatile sig_atomic_t g_saw_signal = 0;


static void
on_signal (int sig)
{
  (void) sig;
  g_saw_signal = 1;
  g_running = 0;                // tell server_loop to exit
}


static void
install_signal_handlers (void)
{
  struct sigaction sa;
  memset (&sa, 0, sizeof (sa));
  sa.sa_handler = on_signal;
  sigemptyset (&sa.sa_mask);
  sa.sa_flags = 0;              // no SA_RESTART -> poll/select will EINTR
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);
  signal (SIGPIPE, SIG_IGN);
}


/////////////////////////////////
static void
build_capabilities (void)
{
  if (g_capabilities)
    {
      json_decref (g_capabilities);
    }
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


/*
   static void
   log_s2s_metrics (const char *who)
   {
   uint64_t sent = 0, recv = 0, auth_fail = 0, too_big = 0;
   s2s_get_counters (&sent, &recv, &auth_fail, &too_big);
   LOGI ("[%s] s2s metrics: sent=%" PRIu64 " recv=%" PRIu64
        " auth_fail=%" PRIu64 " too_big=%" PRIu64 "\n",
        who, sent, recv, auth_fail, too_big);

   //  LOGE( "[%s] s2s metrics: sent=%" PRIu64 " recv=%" PRIu64
   //       " auth_fail=%" PRIu64 " too_big=%" PRIu64 "\n",
   //       who, sent, recv, auth_fail, too_big);
   }
 */
static void *
s2s_control_thread (void *arg)
{
  (void) arg;
  g_s2s_run = 1;
  while (g_s2s_run)
    {
      json_t *msg = NULL;
      int rc = s2s_recv_json (g_s2s_conn, &msg, 1000);  // 1s tick; lets us notice shutdowns


      if (rc == S2S_OK && msg)
        {
          const char *type =
            json_string_value (json_object_get (msg, "type"));


          if (type && strcasecmp (type, "s2s.health.ack") == 0)
            {
              // optional: read payload, surface metrics
            }
          else if (type && strcasecmp (type, "s2s.error") == 0)
            {
              json_t *pl = json_object_get (msg, "payload");
              const char *reason =
                pl ? json_string_value (json_object_get (pl, "reason")) :
                NULL;


              LOGE ("s2s.error%s%s\n", reason ? ": " : "",
                    reason ? reason : "");
              //              LOGE( "[server] s2s.error%s%s\n", reason ? ": " : "",
              //       reason ? reason : "");
            }
          else
            {
              LOGE (" s2s: unknown type '%s'\n", type ? type : "(null)");
              //              LOGE( "[server] s2s: unknown type '%s'\n",
              //       type ? type : "(null)");
            }
          json_decref (msg);
          continue;
        }
      // errors / idle
      if (rc == S2S_E_TIMEOUT)
        {
          continue;             // benign idle
        }
      if (rc == S2S_E_CLOSED)
        {
          break;                // peer closed
        }
      if (rc == S2S_E_AUTH_BAD || rc == S2S_E_AUTH_REQUIRED)
        {
          LOGE ("s2s auth failure; closing\n");
          //      LOGE( "[server] s2s auth failure; closing\n");
          break;
        }
      if (rc == S2S_E_TOOLARGE)
        {
          LOGE ("s2s oversized frame; closing\n");
          //      LOGE( "[server] s2s oversized frame; closing\n");
          break;
        }
      if (rc == S2S_E_IO)
        {
          LOGE ("s2s IO error; closing\n");
          //      LOGE( "[server] s2s IO error; closing\n");
          break;
        }
    }
  return NULL;
}


//////////////////////


/*
   static int
   s2s_accept_once (int lfd)
   {
   struct sockaddr_in peer;
   socklen_t slen = sizeof (peer);
   return accept (lfd, (struct sockaddr *) &peer, &slen);
   }
 */


/*
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
 */


/*
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
 */


/////////////////////////////


/* Convenience: send a NUL-terminated C string. Returns 0 on success, -1 on error. */


/*
   static int
   send_cstr (int fd, const char *s)
   {
   return send_all (fd, s);
   }
 */


////////
//static int s2s_listen_fd = -1, s2s_conn_fd = -1;
static int
s2s_listen (void)
{
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
  struct sockaddr_in addr = { 0 };


  addr.sin_family = AF_INET;
  addr.sin_port = htons (g_cfg.s2s.tcp_port);
  inet_pton (AF_INET, "127.0.0.1", &addr.sin_addr);
  if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) < 0)
    {
      return -1;
    }
  if (listen (fd, 1) < 0)
    {
      return -1;
    }
  return fd;
}


// If these exist elsewhere, keep them; otherwise these prototypes silence warnings
int universe_init (void);
void universe_shutdown (void);
int load_config (void);
int load_eng_config (void);
static volatile sig_atomic_t running = 1;


/* forward decl: your bigbang entry point (adjust name/signature if different) */
int bigbang (void);             /* if your function is named differently, change this */


/*-------------------  Bigbang ---------------------------------*/


// Return first column of first row as int, or -1 on error
static int
get_scalar_int (const char *sql)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      return -1;
    }

  db_res_t *res = NULL;
  db_error_t err;
  int v = -1;


  if (db_query (db, sql, NULL, 0, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          v = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }
  return v;
}


// Decide if we need to run bigbang on this DB
static int
needs_bigbang (void)
{
  db_t *db = game_db_get_handle ();
  if (db_backend (db) == DB_BACKEND_SQLITE)
    {
      // Primary flag: PRAGMA user_version (SQLite only)
      int uv = get_scalar_int ("PRAGMA user_version");


      if (uv > 0)
        {
          return 0;                 // already seeded
        }
    }

  // Belt-and-braces: look at contents in case user_version wasn't set (or for Postgres)
  int sectors = get_scalar_int ("SELECT COUNT(*) FROM sectors");
  int warps = get_scalar_int ("SELECT COUNT(*) FROM sector_warps");
  int ports = get_scalar_int ("SELECT COUNT(*) FROM ports");


  if (sectors <= 10)
    {
      return 1;                 // only the 10 Fedspace rows exist
    }
  if (warps == 0)
    {
      return 1;
    }
  if (ports == 0)
    {
      return 1;
    }
  return 0;
}


//////////////////////////////////////////////////
int
main (void)
{
  srand ((unsigned) time (NULL));       // Seed random number generator once at program start
  int rc = 1;                   // Initialize rc to 1 (failure)


  g_running = 1;
  server_log_init_file ("./twclone.log", "[server]", 0, LOG_DEBUG);
  LOGI ("starting up");
  sysop_start ();

  /* 0.0) Bootstrap Config (DB Connection) */
  if (load_bootstrap_config ("bigbang.json") != 0)
    {
      // Try looking in bin/ just in case we are at root
      if (load_bootstrap_config ("bin/bigbang.json") != 0)
        {
          LOGW (
            "Failed to load bigbang.json (bootstrap config). Using defaults/ENV if available.");
        }
    }

  /* 0) DB: open (create schema/defaults if missing) */
  if (game_db_init () != 0)
    {
      LOGW ("Failed to init DB.\n");
      //      LOGE( "Failed to init DB.\n");
      return EXIT_FAILURE;
    }

  if (needs_bigbang ())
    {
      LOGW ("Universe appears empty - running Big Bang...");
      // Big Bang might still use raw SQLite internally in server_bigbang.c
      // We haven't refactored server_bigbang.c yet, so we assume it handles itself
      // or we accept that it might fail if we are strictly on Postgres right now.
      // But server_bigbang.c is next in the list.
      if (universe_init () != 0)
        {
          return EXIT_FAILURE;
        }
    }

  // normal startup
  if (!load_eng_config ()) // This still calls original SQLite db funcs
    {
      return 2;
    }

  // initalise the player settings if all the other DB stuff is done.
  db_player_settings_init (game_db_get_handle ());
  cron_register_builtins ();
  /* 0.1) Capabilities (restored) */
  build_capabilities ();        /* rebuilds g_capabilities */
  /* 0.2) Signals (restored) */
  install_signal_handlers ();   /* restores Ctrl-C / SIGTERM behavior */
  atexit (schema_shutdown);
  /* 1) S2S keyring (must be before we bring up TCP) */
  LOGI ("loading s2s key ...\n");
  //  LOGE( " loading s2s key ...\n");
  if (s2s_install_default_key (game_db_get_handle ()) != 0)
    {
      LOGW (" FATAL: S2S key missing/invalid\n");
      //  LOGE( " FATAL: S2S key missing/invalid\n");
      return EXIT_FAILURE;
    }
  /* 2) S2S listener for engine control link */
  s2s_listen_fd = s2s_listen ();
  if (s2s_listen_fd < 0)
    {
      LOGW ("listen on s2s port %d failed\n", g_cfg.s2s.tcp_port);
      return EXIT_FAILURE;
    }
  /* NEW: prevent child from inheriting this fd on exec */
  int fl = fcntl (s2s_listen_fd, F_GETFD, 0);


  if (fl != -1)
    {
      fcntl (s2s_listen_fd, F_SETFD, fl | FD_CLOEXEC);
    }
  LOGW ("s2s listen on 127.0.0.1:%d\n", g_cfg.s2s.tcp_port);
  /* 3) Fork the engine (child connects back to the s2s port) */
  LOGW (" forking engine…\n");
  //  LOGE( " forking engine…\n");
  if (engine_spawn (&g_engine_pid, &g_engine_shutdown_fd) != 0)
    {
      LOGW (" engine fork failed\n");
      // LOGE( " engine fork failed\n");
      return EXIT_FAILURE;
    }
  LOGW (" engine forked. pid=%d\n", g_engine_pid);
  //  LOGE( " engine forked. pid=%d\n", g_engine_pid);
  /* 4) Accept one engine connection, complete JSON handshake: recv hello → send ack */
  LOGW (" accepting engine…\n");
  //  LOGE( " accepting engine…\n");
  s2s_conn_t *conn = s2s_tcp_server_accept (s2s_listen_fd, 5000);


  if (!conn)
    {
      LOGW (" accept failed\n");
      //  LOGE( " accept failed\n");
      goto shutdown_and_exit;
    }
  LOGW (" accepted s2s\n");
  //  LOGE( " accepted s2s\n");
  s2s_debug_dump_conn ("server", conn);
  /* Receive engine hello first */
  json_t *msg = NULL;


  rc = s2s_recv_json (conn, &msg, 5000);
  LOGW (" first frame rc=%d\n", rc);
  //  LOGE( " first frame rc=%d\n", rc);
  if (rc == S2S_OK && msg)
    {
      const char *type = json_string_value (json_object_get (msg, "type"));


      if (type && strcasecmp (type, "s2s.health.hello") == 0)
        {
          LOGW (" accepted hello\n");
          //      LOGE( " accepted hello\n");
          time_t now = time (NULL);
          json_t *ack = json_object ();


          json_object_set_new (ack, "v", json_integer (1));
          json_object_set_new (ack, "type", json_string ("s2s.health.ack"));
          json_object_set_new (ack, "id", json_string ("boot-ack"));
          json_object_set_new (ack, "ts", json_integer ((json_int_t) now));


          json_t *payload = json_object ();


          json_object_set_new (payload, "status", json_string ("ok"));
          json_object_set_new (ack, "payload", payload);
          int rc2 = s2s_send_json (conn, ack, 5000);


          LOGW (" ack send rc=%d\n", rc2);
          //      LOGE( " ack send rc=%d\n", rc2);
          json_decref (ack);
          LOGW (" Return Ping\n");
          //      LOGE( " Return Ping\n");
          if (server_s2s_start (conn, &g_s2s_thr, &g_running) != 0)
            {
              LOGW (" failed to start s2s control thread\n");
              //LOGE(
              //               " failed to start s2s control thread\n");
            }
        }
      else
        {
          LOGW (" unexpected type on first frame: %s\n",
                type ? type : "(null)");
          //      LOGE( " unexpected type on first frame: %s\n",
          //       type ? type : "(null)");
        }
      json_decref (msg);
    }
  else
    {
      LOGW (" no hello from engine (rc=%d)\n", rc);
      //      LOGE( " no hello from engine (rc=%d)\n", rc);
      /* continue anyway; control thread can handle retries later */
    }
  /* (Single engine) optionally close the listener now */
  /* close (s2s_listen_fd); s2s_listen_fd = -1; */
  /* 5) Park conn and start S2S control thread AFTER handshake */
  g_s2s_conn = conn;
  g_s2s_run = 1;
  pthread_create (&g_s2s_thr, NULL, s2s_control_thread, NULL);
  /* 6) Run the server loop (unchanged behavior/logs) */
  LOGW ("Server loop starting...\n");
  //  LOGE( "Server loop starting...\n");
  rc = server_loop (&g_running);
  LOGW ("Server loop exiting...\n");
  //  LOGE( "Server loop exiting...\n");

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
    }                           // unblocks thread
  pthread_join (g_s2s_thr, NULL);
  if (s2s_listen_fd >= 0)
    {
      close (s2s_listen_fd);
      s2s_listen_fd = -1;
    }
shutdown_and_exit:
  /* 1. Request Graceful Shutdown via Pipe */
  if (g_engine_shutdown_fd >= 0)
    {
      engine_request_shutdown (g_engine_shutdown_fd);   /* close pipe -> child POLLIN/EOF */
      g_engine_shutdown_fd = -1;
    }
  /* 2. Wait for Engine to Exit, escalating to SIGKILL if stuck */
  if (g_engine_pid > 0)
    {
      LOGW ("Waiting for engine (pid=%d) to exit...\n", g_engine_pid);
      int loops = 0;
      int status = 0;
      int reaped = 0;


      // Wait up to 2 seconds (20 * 100ms)
      while (loops < 20)
        {
          pid_t r = waitpid (g_engine_pid, &status, WNOHANG);


          if (r == g_engine_pid)
            {
              reaped = 1;
              break;
            }
          usleep (100000);      // 100ms
          loops++;
        }
      // If not reaped, send SIGTERM
      if (!reaped)
        {
          LOGW ("Engine unresponsive. Sending SIGTERM.\n");
          kill (g_engine_pid, SIGTERM);
          loops = 0;
          while (loops < 10)    // Wait another 1 second
            {
              pid_t r = waitpid (g_engine_pid, &status, WNOHANG);


              if (r == g_engine_pid)
                {
                  reaped = 1;
                  break;
                }
              usleep (100000);
              loops++;
            }
        }
      // If STILL not reaped, send SIGKILL (Nuclear option)
      if (!reaped)
        {
          LOGW ("Engine stuck. Sending SIGKILL.\n");
          kill (g_engine_pid, SIGKILL);
          waitpid (g_engine_pid, &status, 0);   // Blocking wait for SIGKILL result
        }
      LOGW ("Engine reaped.\n");
      g_engine_pid = -1;
    }
  /* 3. Capabilities cleanup */
  if (g_capabilities)
    {
      json_decref (g_capabilities);
      g_capabilities = NULL;
    }
  sysop_stop ();
  /* 4. FIX: Close Main Thread DB Connection */
  game_db_close ();
  server_log_close ();
  return (rc == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

