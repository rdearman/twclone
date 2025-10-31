#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
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
#include <sqlite3.h>
/* local includes */
#include "s2s_keyring.h"
#include "s2s_transport.h"
#include "database.h"
#include "server_envelope.h"
#include "s2s_transport.h"
#include "engine_consumer.h"
#include "server_engine.h"
#include "database.h"
#include "server_loop.h"	// Assuming this contains functions to communicate with clients
#include "server_universe.h"
#include "server_log.h"
#include "server_cron.h"
#include "common.h"

/* handlers (implemented below) */
static int sweeper_system_notice_ttl (sqlite3 * db, int64_t now_ms);
static int sweeper_engine_deadletter_retry (sqlite3 * db, int64_t now_ms);
static int sweeper_engine_commands_timeout (sqlite3 * db, int64_t now_ms);

// Define the interval for the main game loop ticks in seconds
#define GAME_TICK_INTERVAL_SEC 60

static inline uint64_t
monotonic_millis (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}

/* ---- Cron: registry ---- */
typedef int (*cron_handler_fn) (sqlite3 * db, int64_t now_s);

typedef struct
{
  const char *name;		/* matches cron_tasks.name */
  cron_handler_fn fn;
} CronHandler;

/* handlers you declared */
static int sweeper_broadcast_ttl_cleanup (sqlite3 * db, int64_t now_s);

/* registry */

static const CronHandler CRON_REGISTRY[] = {
  {"traps_process", h_traps_process},
  {"npc_step", h_npc_step},
  {"autouncloak_sweeper", h_autouncloak_sweeper},
  {"fedspace_cleanup", h_fedspace_cleanup},
  {"broadcast_ttl_cleanup", h_broadcast_ttl_cleanup},
  {"planet_growth", h_planet_growth},
  {"daily_turn_reset", h_daily_turn_reset},
  {"terra_replenish", h_terra_replenish},
  {"port_reprice", h_port_reprice},
  {"port_price_drift", h_port_price_drift},
  {"news_collator", h_news_collator},
  {NULL, NULL}
};

/* Lookup by task name (e.g., "fedspace_cleanup"). */
cron_handler_fn
cron_find (const char *name)
{
  for (size_t i = 0; CRON_REGISTRY[i].name; ++i)
    {
      if (strcmp (CRON_REGISTRY[i].name, name) == 0)
	{
	  return CRON_REGISTRY[i].fn;
	}
    }
  return NULL;
}


/* ---- Cron framework (schema: cron_tasks uses schedule + next_due_at) ---- */
/* Schema: cron_tasks(id, name, schedule, last_run_at, next_due_at, enabled, payload) */

/* Parse schedule -> next due (seconds since epoch)
   Supports: every:Ns | every:Nm | daily@HH:MMZ (UTC) */
static int64_t
cron_next_due_from (int64_t now_s, const char *schedule)
{
  if (!schedule || !*schedule)
    return now_s + 60;

  if (strncmp (schedule, "every:", 6) == 0)
    {
      const char *p = schedule + 6;
      char *end = NULL;
      long n = strtol (p, &end, 10);
      if (n <= 0)
	return now_s + 60;
      if (end && (*end == 's' || *end == 'S'))
	return now_s + n;
      if (end && (*end == 'm' || *end == 'M'))
	return now_s + n * 60L;
      return now_s + n;		/* default seconds */
    }

  if (strncmp (schedule, "daily@", 6) == 0)
    {
      int HH = 0, MM = 0;
      if (sscanf (schedule + 6, "%2d:%2dZ", &HH, &MM) == 2)
	{
	  /* compute next UTC wall-clock occurrence >= now */
	  time_t t = (time_t) now_s;
	  struct tm g;
	  gmtime_r (&t, &g);
	  int64_t today =
	    now_s - (g.tm_hour * 3600 + g.tm_min * 60 + g.tm_sec);
	  int64_t target = today + HH * 3600 + MM * 60;
	  return (now_s <= target) ? target : (target + 86400);
	}
    }

  /* unknown → run in 60s to avoid lock-up */
  return now_s + 60;
}


/* TTL for announcements visible to all players (older schema):
   system_notice(id, created_at, title, body, severity, expires_at) */
static int
sweeper_broadcast_ttl_cleanup (sqlite3 *db, int64_t now_s)
{
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db,
			       "DELETE FROM system_notice "
			       "WHERE expires_at IS NOT NULL AND expires_at < ?1 "
			       "LIMIT 500;", -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;
  sqlite3_bind_int64 (st, 1, now_s);
  sqlite3_step (st);
  sqlite3_finalize (st);
  return 0;
}


/* Run TTL cleanup for system_notice + notice_seen.
   now_ts: seconds since epoch (use time(NULL)). */
static int
engine_notice_ttl_sweep (sqlite3 *db, int64_t now_ts)
{
  int rc = SQLITE_OK;
  char *errmsg = NULL;

  /* v1 retention windows (seconds) */
  const int64_t TTL_INFO = 7 * 24 * 3600;
  const int64_t TTL_WARN = 14 * 24 * 3600;
  const int64_t TTL_ERROR = 30 * 24 * 3600;

  rc = sqlite3_exec (db, "BEGIN IMMEDIATE", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      if (errmsg)
	sqlite3_free (errmsg);
      return rc;
    }

  /* 1) Hard-expired rows: expires_at < now */
  {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db,
			    "DELETE FROM system_notice WHERE expires_at IS NOT NULL AND expires_at < ?1;",
			    -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_int64 (st, 1, now_ts);
	sqlite3_step (st);
      }
    if (st)
      sqlite3_finalize (st);
  }

  /* 2) Severity-based windows for rows without explicit expires_at */
  {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db,
			    "DELETE FROM system_notice "
			    "WHERE expires_at IS NULL AND severity='info'  AND created_at < (?1 - ?2);",
			    -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_int64 (st, 1, now_ts);
	sqlite3_bind_int64 (st, 2, TTL_INFO);
	sqlite3_step (st);
      }
    if (st)
      sqlite3_finalize (st);

    if (sqlite3_prepare_v2 (db,
			    "DELETE FROM system_notice "
			    "WHERE expires_at IS NULL AND severity='warn'  AND created_at < (?1 - ?2);",
			    -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_int64 (st, 1, now_ts);
	sqlite3_bind_int64 (st, 2, TTL_WARN);
	sqlite3_step (st);
      }
    if (st)
      sqlite3_finalize (st);

    if (sqlite3_prepare_v2 (db,
			    "DELETE FROM system_notice "
			    "WHERE expires_at IS NULL AND severity='error' AND created_at < (?1 - ?2);",
			    -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_bind_int64 (st, 1, now_ts);
	sqlite3_bind_int64 (st, 2, TTL_ERROR);
	sqlite3_step (st);
      }
    if (st)
      sqlite3_finalize (st);
  }

  /* 3) Keep last 1,000 by id (optional, simple and safe) */
  {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db,
			    "WITH keep AS (SELECT id FROM system_notice ORDER BY id DESC LIMIT 1000) "
			    "DELETE FROM system_notice WHERE id NOT IN (SELECT id FROM keep);",
			    -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_step (st);
      }
    if (st)
      sqlite3_finalize (st);
  }

  /* 4) Remove orphaned notice_seen rows */
  {
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2 (db,
			    "DELETE FROM notice_seen WHERE notice_id NOT IN (SELECT id FROM system_notice);",
			    -1, &st, NULL) == SQLITE_OK)
      {
	sqlite3_step (st);
      }
    if (st)
      sqlite3_finalize (st);
  }

  rc = sqlite3_exec (db, "COMMIT", NULL, NULL, &errmsg);
  if (rc != SQLITE_OK)
    {
      if (errmsg)
	sqlite3_free (errmsg);
      return rc;
    }
  return SQLITE_OK;
}

//////////////////////////

static eng_consumer_cfg_t G_CFG = {
  .batch_size = 200,
  .backlog_prio_threshold = 5000,
  .priority_types_csv = "s2s.broadcast.sweep,player.login",
  .consumer_key = "game_engine"
};

void
engine_tick (sqlite3 *db)
{
  eng_consumer_metrics_t m;
  if (engine_consume_tick (db, &G_CFG, &m) == SQLITE_OK)
    {
      LOGE ("events: processed=%d quarantined=%d last_id=%lld lag=%lld\n",
	    m.processed, m.quarantined, m.last_event_id, m.lag);
      // fprintf (stderr,
      //       "[engine] events: processed=%d quarantined=%d last_id=%lld lag=%lld\n",
      //       m.processed, m.quarantined, m.last_event_id, m.lag);
    }
}


/* Returns a new env (caller must json_decref(result)). */
json_t *
engine_build_command_push (const char *cmd_type, const char *idem_key, json_t *payload_obj,	/* owned by caller */
			   const char *correlation_id,	/* may be NULL */
			   int priority)	/* <=0 -> default 100 */
{
  if (!cmd_type || !idem_key || !payload_obj)
    return NULL;
  if (priority <= 0)
    priority = 100;

  json_t *pl = json_pack ("{s:s,s:s,s:o,s:i}",
			  "cmd_type", cmd_type,
			  "idem_key", idem_key,
			  "payload", payload_obj,
			  "priority", priority);
  if (correlation_id)
    json_object_set_new (pl, "correlation_id", json_string (correlation_id));

  json_t *env = s2s_make_env ("s2s.command.push", "engine", "server", pl);
  json_decref (pl);
  return env;
}




static int
engine_demo_push (s2s_conn_t *c)
{
  /* build the command payload */
  json_t *cmdpl = json_pack ("{s:s,s:s,s:s,s:i,s:o}",
			     "cmd_type", "notice.publish",
			     "idem_key", "player:42:hello:001",	// keep identical to test idempotency
			     "correlation_id", "demo-001",
			     "priority", 100,
			     "payload", json_pack ("{s:s,s:i,s:s,s:i}",
						   "scope", "player",
						   "player_id", 42,
						   "message",
						   "Hello captain!",
						   "ttl_seconds", 3600));

  json_t *env = s2s_make_env ("s2s.command.push", "engine", "server", cmdpl);
  json_decref (cmdpl);

  int rc = s2s_send_env (c, env, 3000);
  json_decref (env);
  LOGI ("command.push send rc=%d\n", rc);
  //  fprintf (stderr, "[engine] command.push send rc=%d\n", rc);
  if (rc != 0)
    return rc;

  json_t *resp = NULL;
  rc = s2s_recv_env (c, &resp, 3000);
  LOGI ("command.push recv rc=%d\n", rc);
  //  fprintf (stderr, "[engine] command.push recv rc=%d\n", rc);
  if (rc == 0 && resp)
    {
      const char *ty = s2s_env_type (resp);
      if (ty && strcmp (ty, "s2s.ack") == 0)
	{
	  json_t *ackpl = s2s_env_payload (resp);
	  json_t *dup = json_object_get (ackpl, "duplicate");
	  LOGW ("ack duplicate=%s\n",
		(dup && json_is_true (dup)) ? "true" : "false");
	  //      fprintf (stderr, "[engine] ack duplicate=%s\n",
	  //       (dup && json_is_true (dup)) ? "true" : "false");
	}
      else
	{
	  LOGE ("got non-ack (%s)\n", ty ? ty : "null");
	  //      fprintf (stderr, "[engine] got non-ack (%s)\n", ty ? ty : "null");
	}
      json_decref (resp);
    }
  return rc;
}



/////////////////////////
static int
engine_send_notice_publish (s2s_conn_t *c, int player_id, const char *msg,
			    int ttl_seconds)
{
  json_t *pl = json_pack ("{s:s,s:s,s:s,s:i,s:o}",
			  "cmd_type", "notice.publish",
			  "idem_key", "player:42:msg:abc",	/* TODO: make this unique per message */
			  "correlation_id", "engine-demo-1",
			  "priority", 100,
			  "payload", json_pack ("{s:s,s:i,s:s}",
						"scope", "player",
						"player_id", player_id,
						"message", msg));
  json_t *env = s2s_make_env ("s2s.command.push", "engine", "server", pl);
  json_decref (pl);

  int rc = s2s_send_env (c, env, 3000);
  json_decref (env);
  if (rc != 0)
    return rc;

  json_t *resp = NULL;
  rc = s2s_recv_env (c, &resp, 3000);
  if (rc == 0 && resp)
    {
      // log ack/error minimally
      const char *ty = s2s_env_type (resp);
      if (ty && strcmp (ty, "s2s.ack") == 0)
	LOGI ("command.push ack\n");
      //        fprintf (stderr, "[engine] command.push ack\n");
      else
	LOGE ("command.push err\n");
      //fprintf (stderr, "[engine] command.push err\n");
      json_decref (resp);
    }
  return rc;
}

/////////////////////////
static void
log_s2s_metrics (const char *who)
{
  uint64_t sent = 0, recv = 0, auth_fail = 0, too_big = 0;
  s2s_get_counters (&sent, &recv, &auth_fail, &too_big);
  LOGI ("[%s] s2s metrics: sent=%" PRIu64 " recv=%" PRIu64
	" auth_fail=%" PRIu64 " too_big=%" PRIu64 "\n",
	who, sent, recv, auth_fail, too_big);

  //fprintf (stderr, "[%s] s2s metrics: sent=%" PRIu64 " recv=%" PRIu64
  //       " auth_fail=%" PRIu64 " too_big=%" PRIu64 "\n",
  //       who, sent, recv, auth_fail, too_big);
}


static void
engine_s2s_drain_once (s2s_conn_t *conn)
{
  json_t *msg = NULL;
  int rc = s2s_recv_json (conn, &msg, 0);	// 0ms => non-blocking
  if (rc != S2S_OK || !msg)
    return;

  const char *type = json_string_value (json_object_get (msg, "type"));
  if (type && strcmp (type, "s2s.engine.shutdown") == 0)
    {
      // Begin graceful shutdown: set your running flag false, close down
      LOGI ("received shutdown command\n");
      // fprintf (stderr, "[engine] received shutdown command\n");
      // flip your engine running flag here…
    }
  else if (type && strcmp (type, "s2s.health.check") == 0)
    {
      // Optional: allow server to ping any time
      time_t now = time (NULL);
      static time_t start_ts;
      if (!start_ts)
	start_ts = now;
      json_t *ack = json_pack ("{s:i,s:s,s:s,s:I,s:o}",
			       "v", 1, "type", "s2s.health.ack", "id",
			       "rt-ack",
			       "ts", (json_int_t) now,
			       "payload", json_pack ("{s:s,s:s,s:I}",
						     "role", "engine",
						     "version", "0.1",
						     "uptime_s",
						     (json_int_t) (now -
								   start_ts)));
      s2s_send_json (conn, ack, 5000);
      json_decref (ack);
    }
  else
    {
      // Unknown → send s2s.error
      json_t *err = json_pack ("{s:i,s:s,s:s,s:I,s:o}",
			       "v", 1, "type", "s2s.error", "id", "unknown",
			       "ts", (json_int_t) time (NULL),
			       "payload", json_pack ("{s:s}", "reason",
						     "unknown_type"));
      s2s_send_json (conn, err, 5000);
      json_decref (err);
    }

  json_decref (msg);
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


/* --- executor: broadcast.create → INSERT INTO system_notice --- */
static int
exec_broadcast_create (sqlite3 *db, json_t *payload, const char *idem_key,
		       int64_t *out_notice_id)
{
  (void) idem_key;		/* idempotency handled at engine_commands layer */
  const char *title = NULL, *body = NULL, *severity = "info";
  int64_t expires_at = 0;

  if (!json_is_object (payload))
    return SQLITE_MISMATCH;
  json_t *jt = json_object_get (payload, "title");
  json_t *jb = json_object_get (payload, "body");
  json_t *js = json_object_get (payload, "severity");
  json_t *je = json_object_get (payload, "expires_at");
  if (jt && json_is_string (jt))
    title = json_string_value (jt);
  if (jb && json_is_string (jb))
    body = json_string_value (jb);
  if (js && json_is_string (js))
    severity = json_string_value (js);
  if (je && json_is_integer (je))
    expires_at = (int64_t) json_integer_value (je);
  if (!title || !body)
    return SQLITE_MISUSE;

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db,
			       "INSERT INTO system_notice(id, created_at, title, body, severity, expires_at) "
			       "VALUES(NULL, strftime('%s','now'), ?1, ?2, ?3, ?4);",
			       -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, title, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 2, body, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 3, severity ? severity : "info", -1,
		     SQLITE_TRANSIENT);
  if (expires_at > 0)
    sqlite3_bind_int64 (st, 4, expires_at);
  else
    sqlite3_bind_null (st, 4);

  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    return SQLITE_ERROR;

  if (out_notice_id)
    *out_notice_id = (int64_t) sqlite3_last_insert_rowid (db);
  return SQLITE_OK;
}

/* --- tick: pull ready commands and execute --- */
static int
server_commands_tick (sqlite3 *db, int max_rows)
{
  if (max_rows <= 0 || max_rows > 100)
    max_rows = 16;
  int processed = 0;

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db,
			       "SELECT id, type, payload, idem_key "
			       "FROM engine_commands "
			       "WHERE status='ready' AND due_at <= strftime('%s','now') "
			       "ORDER BY priority ASC, due_at ASC, id ASC "
			       "LIMIT ?1;",
			       -1, &st, NULL);
  if (rc != SQLITE_OK)
    return 0;
  sqlite3_bind_int (st, 1, max_rows);

  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int64_t cmd_id = sqlite3_column_int64 (st, 0);
      const char *type = (const char *) sqlite3_column_text (st, 1);
      const char *payload_json = (const char *) sqlite3_column_text (st, 2);
      const char *idem_key = (const char *) sqlite3_column_text (st, 3);

      /* mark running */
      sqlite3_stmt *upr = NULL;
      if (sqlite3_prepare_v2 (db,
			      "UPDATE engine_commands SET status='running', started_at=strftime('%s','now') WHERE id=?1;",
			      -1, &upr, NULL) == SQLITE_OK)
	{
	  sqlite3_bind_int64 (upr, 1, cmd_id);
	  sqlite3_step (upr);
	}
      if (upr)
	sqlite3_finalize (upr);

      int ok = -1;
      json_error_t jerr;
      json_t *pl = payload_json ? json_loads (payload_json, 0, &jerr) : NULL;

      if (type && strcmp (type, "broadcast.create") == 0)
	{
	  int64_t notice_id = 0;
	  ok =
	    (exec_broadcast_create (db, pl, idem_key, &notice_id) ==
	     SQLITE_OK) ? 0 : -1;
	}
      else
	{
	  /* unknown command type → mark error */
	  ok = -1;
	}

      if (pl)
	json_decref (pl);

      /* finalize status */
      sqlite3_stmt *upf = NULL;
      if (ok == 0)
	{
	  if (sqlite3_prepare_v2 (db,
				  "UPDATE engine_commands SET status='done', finished_at=strftime('%s','now') WHERE id=?1;",
				  -1, &upf, NULL) == SQLITE_OK)
	    {
	      sqlite3_bind_int64 (upf, 1, cmd_id);
	      sqlite3_step (upf);
	    }
	}
      else
	{
	  if (sqlite3_prepare_v2 (db,
				  "UPDATE engine_commands SET status='error', attempts=attempts+1, finished_at=strftime('%s','now') WHERE id=?1;",
				  -1, &upf, NULL) == SQLITE_OK)
	    {
	      sqlite3_bind_int64 (upf, 1, cmd_id);
	      sqlite3_step (upf);
	    }
	}
      if (upf)
	sqlite3_finalize (upf);
      processed++;
    }
  sqlite3_finalize (st);
  return processed;
}



/////////////////////////////////////////////////////////////////////////////
//////////////   MAIN ENGINE LOOP
/////////////////////////////////////////////////////////////////////////////

static int
engine_main_loop (int shutdown_fd)
{

  server_log_init_file ("./twclone.log", "[engine]", 0, LOG_INFO);
  LOGI ("engine boot pid=%d", getpid ());

  // 1. *** CRITICAL NEW STEP: Shut down and re-initialize SQLite ***
  //    This discards all inherited global VFS and memory state.
  sqlite3_shutdown ();
  int rc = sqlite3_initialize ();
  if (rc != SQLITE_OK)
    {
      LOGE ("FATAL: SQLite re-initialization failed! rc=%d", rc);
      return 1;
    }

  // 2. Close the stale handle inherited from the parent (which you already added)
  db_handle_close_and_reset ();

  // 3. Get a fresh, new handle (which you already fixed to be idempotent)
  sqlite3 *db_handle = db_get_handle ();
  if (db_handle == NULL)
    {
      // This LOGE is no longer strictly necessary if db_get_handle() logs the error,
      // but it confirms the fresh open failed.
      LOGE ("The fresh open failed.");
      return 1;
    }



  if (s2s_install_default_key (db_handle) != 0)
    {
      LOGW ("[server] FATAL: S2S key missing/invalid.\n");
      //fprintf (stderr, "[server] FATAL: S2S key missing/invalid.\n");
      return 1;			// or exit(1)
    }
  LOGI ("[engine] child up. pid=%d\n", getpid ());
  //printf ("[engine] child up. pid=%d\n", getpid ());

  const int tick_ms = 500;	// quick tick; we can fetch from DB config later
  struct pollfd pfd = {.fd = shutdown_fd,.events = POLLIN };

  LOGI ("[engine] loading s2s key ...\n");
  //fprintf (stderr, "[engine] loading s2s key ...\n");
  if (s2s_install_default_key (db_handle) != 0)
    {
      LOGE ("[engine] FATAL: S2S key missing/invalid.\n");
      //  fprintf (stderr, "[engine] FATAL: S2S key missing/invalid.\n");
      return 1;
    }
  LOGI ("[engine] connecting to 127.0.0.1:4321 ...\n");
  //fprintf (stderr, "[engine] connecting to 127.0.0.1:4321 ...\n");
  s2s_conn_t *conn = s2s_tcp_client_connect ("127.0.0.1", 4321, 5000);
  if (!conn)
    {
      LOGE ("[engine] connect failed\n");
      //  fprintf (stderr, "[engine] connect failed\n");
      return 1;
    }
  s2s_debug_dump_conn ("engine", conn);	// optional debug

  /* Engine initiates: send hello, then expect ack */
  time_t now = time (NULL);
  json_t *hello = json_pack ("{s:i,s:s,s:s,s:I,s:o}",
			     "v", 1,
			     "type", "s2s.health.hello",
			     "id", "boot-1",
			     "ts", (json_int_t) now,
			     "payload", json_pack ("{s:s,s:s}",
						   "role", "engine",
						   "version", "0.1"));
  rc = s2s_send_json (conn, hello, 5000);
  json_decref (hello);
  LOGI ("[engine] hello send rc=%d\n", rc);
  //fprintf (stderr, "[engine] hello send rc=%d\n", rc);

  json_t *msg = NULL;
  rc = s2s_recv_json (conn, &msg, 5000);
  LOGI ("[engine] first frame rc=%d\n", rc);
  //fprintf (stderr, "[engine] first frame rc=%d\n", rc);
  if (rc == S2S_OK && msg)
    {
      const char *type = json_string_value (json_object_get (msg, "type"));
      if (type && strcmp (type, "s2s.health.ack") == 0)
	{
	  LOGI ("[engine] Ping test complete\n");
	  //      fprintf (stderr, "[engine] Ping test complete\n");
	}
      json_decref (msg);
    }

  LOGI ("[engine] Running Smoke Test\n");
  //  fprintf (stderr, "[engine] Running Smoke Test\n");
  (void) engine_demo_push (conn);

  static time_t last_metrics = 0;
  static time_t last_cmd_tick_ms = 0;
  /* One-time ISS bootstrap */
  int iss_ok = iss_init_once ();	// 1 if ISS+Stardock found, else 0
  fer_attach_db (db_handle);
  int fer_ok = fer_init_once ();


  for (;;)
    {
      engine_s2s_drain_once (conn);

      uint64_t now_ms = monotonic_millis ();

      if (now_ms - last_cmd_tick_ms >= 250)
	{
	  (void) server_commands_tick (db_get_handle (), 16);
	  last_cmd_tick_ms = now_ms;
	}

      time_t now = time (NULL);
      if (now - last_metrics >= 3600)
	{
	  log_s2s_metrics ("engine");
	  last_metrics = now;
	  engine_tick (db_handle);
	}

      /* file-scope static, near other engine globals */
      static int64_t g_next_notice_ttl_sweep = 0;

      /* inside the engine’s main loop / tick */
      {
	int64_t now_ts = (int64_t) time (NULL);

	if (g_next_notice_ttl_sweep == 0)
	  {
	    /* schedule first run ~5 minutes from start */
	    g_next_notice_ttl_sweep = now_ts + 300;
	  }

	if (now_ts >= g_next_notice_ttl_sweep)
	  {
	    (void) engine_notice_ttl_sweep (db_get_handle (), now_ts);
	    g_next_notice_ttl_sweep = now_ts + 24 * 3600;	/* run daily */
	  }
      }

      // Sleep until next tick or until shutdown pipe changes
      int rc = poll (&pfd, 1, tick_ms);
      if (rc > 0)
	{
	  // either data or EOF on the pipe means: time to exit
	  char buf[8];
	  ssize_t n = read (shutdown_fd, buf, sizeof (buf));
	  (void) n;		// We don't care what's read; any activity/EOF = shutdown
	  LOGI ("[engine] shutdown signal received.\n");
	  //printf ("[engine] shutdown signal received.\n");
	  break;
	}

      if (rc == 0)
	{
	  /* ---- bounded cron runner (inline) ---- */
	  const int LIMIT = 8;
	  int64_t now_s = (int64_t) time (NULL);

	  sqlite3_stmt *pick = NULL;
	  if (sqlite3_prepare_v2 (db_handle,
				  "SELECT id, name, schedule FROM cron_tasks "
				  "WHERE enabled=1 AND next_due_at <= ?1 "
				  "ORDER BY next_due_at ASC "
				  "LIMIT ?2;", -1, &pick, NULL) == SQLITE_OK)
	    {

	      sqlite3_bind_int64 (pick, 1, now_s);
	      sqlite3_bind_int (pick, 2, LIMIT);

	      while (sqlite3_step (pick) == SQLITE_ROW)
		{
		  int64_t id = sqlite3_column_int64 (pick, 0);
		  const char *nm =
		    (const char *) sqlite3_column_text (pick, 1);
		  const char *sch =
		    (const char *) sqlite3_column_text (pick, 2);

		  /* run handler if registered */
		  int task_rc = 0;
		  cron_handler_fn fn = cron_find (nm);
		  LOGI ("cron: starting handler '%s'", nm);
		  if (fn)
		    task_rc = fn (db_handle, now_s);

		  /* reschedule deterministically */
		  int64_t next_due = cron_next_due_from (now_s, sch);

		  sqlite3_stmt *upd = NULL;
		  if (sqlite3_prepare_v2 (db_handle,
					  "UPDATE cron_tasks "
					  "SET last_run_at=?2, next_due_at=?3 "
					  "WHERE id=?1;", -1, &upd,
					  NULL) == SQLITE_OK)
		    {
		      sqlite3_bind_int64 (upd, 1, id);
		      sqlite3_bind_int64 (upd, 2, now_s);
		      sqlite3_bind_int64 (upd, 3, next_due);
		      sqlite3_step (upd);
		      sqlite3_finalize (upd);
		    }

		  (void) task_rc;	/* optional: log rc */
		}
	      sqlite3_finalize (pick);
	    }

	  /* (keep anything else you want here; short, bounded work only) */
	}
      else if (rc < 0 && errno != EINTR)
	{
	  LOGE ("[engine] poll error: %s\n", strerror (errno));
	  //      fprintf (stderr, "[engine] poll error: %s\n", strerror (errno));
	  break;
	}
    }

  db_close ();
  LOGI ("[engine] child exiting cleanly.\n");
  //  printf ("[engine] child exiting cleanly.\n");
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

  if (out_pid)
    *out_pid = pid;
  if (out_shutdown_fd)
    *out_shutdown_fd = pipefd[1];
  LOGI ("[engine] pid=%d\n", (int) pid);
  //  printf ("[engine] pid=%d\n", (int) pid);
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


/* configurable knobs */
static const int64_t CRON_PERIOD_MS = 500;	/* run every 0.5s */
static const int CRON_BATCH_LIMIT = 8;	/* max tasks per runner tick */
static const int64_t CRON_LOCK_STALE_MS = 120000;	/* reclaim after 2 min */

/* helpers to find handler */
static cron_handler_fn
cron_lookup (const char *name)
{
  for (int i = 0; CRON_REGISTRY[i].name; ++i)
    if (strcmp (CRON_REGISTRY[i].name, name) == 0)
      return CRON_REGISTRY[i].fn;
  return NULL;
}

/* one runner tick: claim due tasks, run, reschedule */
static void
run_cron_batch (sqlite3 *db, int64_t now_ms)
{
  sqlite3_stmt *st = NULL;

  /* reclaim stale locks (best-effort) */
  sqlite3_exec (db, "UPDATE cron_tasks " "SET locked=0, lock_owner=NULL " "WHERE locked=1 AND (strftime('%s','now')*1000 - lock_ts_ms) > ?1;", NULL, NULL, NULL);	/* you can bind via prepared stmt if you like */

  /* pick due & unlocked tasks (bounded) */
  if (sqlite3_prepare_v2 (db,
			  "SELECT id, task_name, interval_ms "
			  "FROM cron_tasks "
			  "WHERE enabled=1 AND locked=0 AND next_due_ms <= ?1 "
			  "ORDER BY priority DESC, next_due_ms ASC "
			  "LIMIT ?2;", -1, &st, NULL) != SQLITE_OK)
    return;

  sqlite3_bind_int64 (st, 1, now_ms);
  sqlite3_bind_int (st, 2, CRON_BATCH_LIMIT);

  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int64_t id = sqlite3_column_int64 (st, 0);
      const char *nm = (const char *) sqlite3_column_text (st, 1);
      int64_t interval = sqlite3_column_int64 (st, 2);

      /* try to lock */
      sqlite3_stmt *lk = NULL;
      if (sqlite3_prepare_v2 (db,
			      "UPDATE cron_tasks SET locked=1, lock_owner='engine', lock_ts_ms=?2 "
			      "WHERE id=?1 AND locked=0;", -1, &lk,
			      NULL) == SQLITE_OK)
	{
	  sqlite3_bind_int64 (lk, 1, id);
	  sqlite3_bind_int64 (lk, 2, now_ms);
	  sqlite3_step (lk);
	  sqlite3_finalize (lk);
	}

      /* verify lock (race-safe) */
      sqlite3_stmt *chk = NULL;
      int have_lock = 0;
      if (sqlite3_prepare_v2 (db,
			      "SELECT locked FROM cron_tasks WHERE id=?1;",
			      -1, &chk, NULL) == SQLITE_OK)
	{
	  sqlite3_bind_int64 (chk, 1, id);
	  if (sqlite3_step (chk) == SQLITE_ROW)
	    have_lock = sqlite3_column_int (chk, 0);
	  sqlite3_finalize (chk);
	}
      if (!have_lock)
	continue;

      /* run the handler */
      int rc = -1;
      cron_handler_fn fn = cron_lookup (nm);
      if (fn)
	{
	  rc = fn (db, now_ms);
	}

      /* reschedule (idempotent) */
      sqlite3_stmt *upd = NULL;
      if (sqlite3_prepare_v2 (db,
			      "UPDATE cron_tasks SET "
			      "  locked=0, lock_owner=NULL, "
			      "  last_rc=?2, last_run_ms=?3, "
			      "  next_due_ms = CASE WHEN ?2=0 THEN (?3 + interval_ms) ELSE (?3 + interval_ms) END "
			      "WHERE id=?1;", -1, &upd, NULL) == SQLITE_OK)
	{
	  sqlite3_bind_int64 (upd, 1, id);
	  sqlite3_bind_int (upd, 2, rc == 0 ? 0 : rc);	/* 0=ok, else error code */
	  sqlite3_bind_int64 (upd, 3, now_ms);
	  sqlite3_step (upd);
	  sqlite3_finalize (upd);
	}
    }
  sqlite3_finalize (st);
}

static int
sweeper_system_notice_ttl (sqlite3 *db, int64_t now_ms)
{
  (void) now_ms;
  /* delete ephemerals whose ttl expired; bounded batch */
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db,
			  "DELETE FROM system_notice "
			  "WHERE ephemeral=1 AND (ts + ttl_seconds) < strftime('%s','now') "
			  "LIMIT 500;", -1, &st, NULL) != SQLITE_OK)
    return 1;
  sqlite3_step (st);
  sqlite3_finalize (st);
  return 0;
}

static int
sweeper_engine_commands_timeout (sqlite3 *db, int64_t now_ms)
{
  (void) now_ms;
  /* deadletter stuck commands older than N seconds; bounded */
  const int TIMEOUT_S = 60;	/* adjust */
  sqlite3_stmt *st = NULL;

  /* move timed-out commands to deadletter with cause=timeout */
  if (sqlite3_prepare_v2 (db,
			  "WITH stuck AS ( "
			  "  SELECT id, type, sector_id, payload "
			  "  FROM engine_commands "
			  "  WHERE status='pending' "
			  "    AND created_at < strftime('%s','now') - ?1 "
			  "  LIMIT 200 "
			  ") "
			  "INSERT INTO engine_events_deadletter(type, sector_id, payload, reason, retry_at, attempts, terminal) "
			  "SELECT type, sector_id, payload, 'timeout', strftime('%s','now') + 60, 0, 0 FROM stuck; ",
			  -1, &st, NULL) != SQLITE_OK)
    return 4;
  sqlite3_bind_int (st, 1, TIMEOUT_S);
  sqlite3_step (st);
  sqlite3_finalize (st);

  /* mark them deadlettered */
  if (sqlite3_prepare_v2 (db,
			  "UPDATE engine_commands "
			  "SET status='deadlettered' "
			  "WHERE status='pending' "
			  "  AND created_at < strftime('%s','now') - ?1;", -1,
			  &st, NULL) != SQLITE_OK)
    return 5;
  sqlite3_bind_int (st, 1, TIMEOUT_S);
  sqlite3_step (st);
  sqlite3_finalize (st);

  return 0;
}
