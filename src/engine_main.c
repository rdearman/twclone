// server_engine.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <jansson.h>
#include <inttypes.h>
/* local includes */
#include "engine_main.h"
#include "database.h"
#include "s2s_transport.h"   // s2s_conn_t, s2s_recv_json, s2s_send_json
#include "s2s_transport.h"
#include "s2s_keyring.h"
#include "s2s_transport.h"
#include "database.h"

/////////////////////////


static void log_s2s_metrics(const char *who) {
  uint64_t sent=0, recv=0, auth_fail=0, too_big=0;
  s2s_get_counters(&sent, &recv, &auth_fail, &too_big);
  fprintf(stderr, "[%s] s2s metrics: sent=%" PRIu64 " recv=%" PRIu64
                  " auth_fail=%" PRIu64 " too_big=%" PRIu64 "\n",
          who, sent, recv, auth_fail, too_big);
}


static void engine_s2s_drain_once(s2s_conn_t *conn) {
  json_t *msg = NULL;
  int rc = s2s_recv_json(conn, &msg, 0);   // 0ms => non-blocking
  if (rc != S2S_OK || !msg) return;

  const char *type = json_string_value(json_object_get(msg, "type"));
  if (type && strcmp(type, "s2s.engine.shutdown") == 0) {
    // Begin graceful shutdown: set your running flag false, close down
    fprintf(stderr, "[engine] received shutdown command\n");
    // flip your engine running flag here…
  } else if (type && strcmp(type, "s2s.health.check") == 0) {
    // Optional: allow server to ping any time
    time_t now = time(NULL);
    static time_t start_ts; if (!start_ts) start_ts = now;
    json_t *ack = json_pack("{s:i,s:s,s:s,s:I,s:o}",
                            "v",1,"type","s2s.health.ack","id","rt-ack",
                            "ts",(json_int_t)now,
                            "payload", json_pack("{s:s,s:s,s:I}",
                              "role","engine","version","0.1",
                              "uptime_s",(json_int_t)(now-start_ts)));
    s2s_send_json(conn, ack, 5000);
    json_decref(ack);
  } else {
    // Unknown → send s2s.error
    json_t *err = json_pack("{s:i,s:s,s:s,s:I,s:o}",
                            "v",1,"type","s2s.error","id","unknown",
                            "ts",(json_int_t)time(NULL),
                            "payload", json_pack("{s:s}", "reason","unknown_type"));
    s2s_send_json(conn, err, 5000);
    json_decref(err);
  }

  json_decref(msg);
}

/* Send the entire buffer (blocking). Returns 0 on success, -1 on error. */
static int
send_all (int fd, const void *buf, size_t len)
{
  const char *p = (const char *) buf;
  size_t off = 0;
  while (off < len)
    {
      ssize_t n = send (fd, p + off, len - off, 0);
      if (n > 0)
	{
	  off += (size_t) n;
	  continue;
	}
      if (n < 0 && (errno == EINTR))
	continue;		// interrupted -> retry
      return -1;		// EPIPE/ECONNRESET/etc.
    }
  return 0;
}

/* Convenience: send a NUL-terminated C string. Returns 0 on success, -1 on error. */
static int
send_cstr (int fd, const char *s)
{
  return send_all (fd, s, strlen (s));
}

/*
 * Read a single line (ending with '\n') into buf (cap includes NUL).
 * Returns number of bytes stored (excluding NUL), or -1 on error/EOF before any byte.
 * The returned string has the trailing '\n' removed and also trims a preceding '\r' (CRLF).
 */
static int
recv_line (int fd, char *buf, size_t cap)
{
  if (cap == 0)
    return -1;
  size_t off = 0;

  for (;;)
    {
      char c;
      ssize_t n = recv (fd, &c, 1, 0);
      if (n == 0)
	{			// peer closed
	  if (off == 0)
	    return -1;		// EOF with no data
	  break;		// return what we have
	}
      if (n < 0)
	{
	  if (errno == EINTR)
	    continue;		// interrupted -> retry
	  return -1;
	}

      if (c == '\n')
	{
	  // Trim optional '\r' before '\n'
	  if (off > 0 && buf[off - 1] == '\r')
	    off--;
	  break;
	}

      if (off + 1 < cap)
	{
	  buf[off++] = c;
	}
      else
	{
	  // Buffer full; terminate and return what we have so far.
	  break;
	}
    }

  buf[off] = '\0';
  return (int) off;
}





// Keep the legacy thread entry (unused after forking, but preserved)
#define GAME_TICK_INTERVAL_SEC 60

/* ----- Forked engine process implementation ----- */


static int
s2s_connect_4321 (void)
{
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr = { 0 };
  addr.sin_family = AF_INET;
  addr.sin_port = htons (4321);
  inet_pton (AF_INET, "127.0.0.1", &addr.sin_addr);
  // retry a few times in case accept isn't up yet
  for (int i = 0; i < 20; i++)
    {
      if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == 0)
	return fd;
      usleep (50 * 1000);
    }
  close (fd);
  return -1;
}

static int
engine_main_loop (int shutdown_fd)
{

  sqlite3 *db_handle = db_get_handle();
  if (s2s_install_default_key(db_handle) != 0) {
    fprintf(stderr, "[server] FATAL: S2S key missing/invalid.\n");
    return 1; // or exit(1)
  }

  printf ("[engine] child up. pid=%d\n", getpid ());

  const int tick_ms = 500;	// quick tick; we can fetch from DB config later
  struct pollfd pfd = {.fd = shutdown_fd,.events = POLLIN };

  fprintf (stderr, "[engine] loading s2s key ...\n");
  if (s2s_install_default_key(db_handle) != 0)
    {
    fprintf(stderr, "[engine] FATAL: S2S key missing/invalid.\n");
    return 1;
  }

  fprintf(stderr, "[engine] connecting to 127.0.0.1:4321 ...\n");
  s2s_conn_t *conn = s2s_tcp_client_connect("127.0.0.1", 4321, 5000);
  if (!conn) {
    fprintf(stderr, "[engine] connect failed\n");
    return 1;
  }
  s2s_debug_dump_conn("engine", conn);  // optional debug

  /* Engine initiates: send hello, then expect ack */
  time_t now = time(NULL);
  json_t *hello = json_pack("{s:i,s:s,s:s,s:I,s:o}",
			    "v",1,
			    "type","s2s.health.hello",
			    "id","boot-1",
			    "ts",(json_int_t)now,
			    "payload", json_pack("{s:s,s:s}",
						 "role","engine","version","0.1"));
  int rc = s2s_send_json(conn, hello, 5000);
  json_decref(hello);
  fprintf(stderr, "[engine] hello send rc=%d\n", rc);

  json_t *msg = NULL;
  rc = s2s_recv_json(conn, &msg, 5000);
  fprintf(stderr, "[engine] first frame rc=%d\n", rc);
  if (rc == S2S_OK && msg) {
    const char *type = json_string_value(json_object_get(msg, "type"));
    if (type && strcmp(type, "s2s.health.ack") == 0) {
      fprintf(stderr, "[engine] Ping test complete\n");
    }
    json_decref(msg);
  }

  

  static time_t last_metrics = 0;  
  for (;;)
    {
      engine_s2s_drain_once(conn);

      time_t now = time(NULL);
      if (now - last_metrics >= 3600)
	{
	log_s2s_metrics("engine");
	last_metrics = now;
      }

      // Sleep until next tick or until shutdown pipe changes
      int rc = poll (&pfd, 1, tick_ms);
      if (rc > 0)
	{
	  // either data or EOF on the pipe means: time to exit
	  char buf[8];
	  ssize_t n = read (shutdown_fd, buf, sizeof (buf));
	  (void) n;		// We don't care what's read; any activity/EOF = shutdown
	  printf ("[engine] shutdown signal received.\n");
	  break;
	}
      if (rc == 0)
	{
	  // timeout -> do one bounded tick of work
	  // (placeholders now; wire real sweepers per ENGINE.md)
	  // - consume events in ASC batches
	  // - run due cron_tasks
	  // - NPC step, TTL sweepers, etc.
	  // - update engine_offset watermark
	  // Keep each unit short and idempotent.
	  // …
	  // For now, just a heartbeat:
	  static time_t last = 0;
	  time_t now = time (NULL);
	  if (now != last)
	    {;
	    }			//printf("[engine] tick @ %ld\n", (long)now); last = now; }
	}
      else if (rc < 0 && errno != EINTR)
	{
	  fprintf (stderr, "[engine] poll error: %s\n", strerror (errno));
	  break;
	}
    }

  db_close ();
  printf ("[engine] child exiting cleanly.\n");
  return 0;
}

int
engine_spawn (pid_t *out_pid, int *out_shutdown_fd)
{
  int pipefd[2];
  if (pipe (pipefd) != 0)
    {
      perror ("pipe");
      return -1;
    }

  pid_t pid = fork ();
  if (pid < 0)
    {
      perror ("fork");
      close (pipefd[0]);
      close (pipefd[1]);
      return -1;
    }

  if (pid == 0)
    {
      /* --- CHILD PROCESS: engine --- */

      /* Close the write end; child only reads shutdown pipe */
      close (pipefd[1]);

      /* Run the engine and exit the process (never return to server main) */
      int ec = engine_main_loop (pipefd[0]);
      _exit (ec == 0 ? 0 : 1);
    }

  /* --- PARENT PROCESS: server --- */

  /* Parent keeps write end (shutdown signal), closes read end */
  close (pipefd[0]);

  if (out_pid)          *out_pid = pid;
  if (out_shutdown_fd)  *out_shutdown_fd = pipefd[1];

  printf ("[engine] pid=%d\n", (int)pid);
  return 0;
}




int
engine_request_shutdown (int shutdown_fd)
{
  // Closing the parent's write-end causes EOF in the child -> graceful exit
  if (shutdown_fd >= 0)
    close (shutdown_fd);
  return 0;
}

int
engine_wait (pid_t pid, int timeout_ms)
{
  // Simple timed waitpid loop
  const int step_ms = 50;
  int elapsed = 0;
  for (;;)
    {
      int status = 0;
      pid_t r = waitpid (pid, &status, WNOHANG);
      if (r == pid)
	return 0;		// reaped
      if (r < 0)
	return -1;		// error
      if (timeout_ms >= 0 && elapsed >= timeout_ms)
	return 1;		// still running
      usleep (step_ms * 1000);
      elapsed += step_ms;
    }
}
