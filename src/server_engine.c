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
#include <strings.h>
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
#include "server_config.h"
#include "server_clusters.h"

/* handlers (implemented below) */
/* static int sweeper_engine_deadletter_retry (sqlite3 * db, int64_t now_ms); */

// Define the interval for the main game loop ticks in seconds
#define GAME_TICK_INTERVAL_SEC 60
#define MAX_RETRIES 3
#define MAX_BACKLOG_DAYS 30	// Cap for how many days of interest can be applied retroactively

static const int64_t CRON_PERIOD_MS = 500;	/* run every 0.5s */
static const int CRON_BATCH_LIMIT = 8;	/* max tasks per runner tick */
static const int64_t CRON_LOCK_STALE_MS = 120000;	/* reclaim after 2 min */

int h_daily_bank_interest_tick (sqlite3 * db, int64_t now_s);
static int engine_notice_ttl_sweep (sqlite3 * db, int64_t now_ms);
static int sweeper_engine_deadletter_retry (sqlite3 * db, int64_t now_ms);
int cron_limpet_ttl_cleanup (sqlite3 * db, int64_t now_s);

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


/* registry */

static const CronHandler CRON_REGISTRY[] = {
  {"daily_turn_reset", h_reset_turns_for_player},
  {"fedspace_cleanup", h_fedspace_cleanup},
  {"autouncloak_sweeper", h_autouncloak_sweeper},
  {"terra_replenish", h_terra_replenish},
  {"planet_growth", h_planet_growth},
  {"broadcast_ttl_cleanup", h_broadcast_ttl_cleanup},
  {"traps_process", h_traps_process},
  {"npc_step", h_npc_step},
  {"daily_market_settlement", h_daily_market_settlement},
  {"daily_news_compiler", h_daily_news_compiler},
  {"cleanup_old_news", h_cleanup_old_news},
  {"limpet_ttl_cleanup", cron_limpet_ttl_cleanup},
  {"daily_bank_interest_tick", h_daily_bank_interest_tick},
  {"daily_lottery_draw", h_daily_lottery_draw},
  {"deadpool_resolution_cron", h_deadpool_resolution_cron},
  {"tavern_notice_expiry_cron", h_tavern_notice_expiry_cron},
  {"loan_shark_interest_cron", h_loan_shark_interest_cron},
  {"daily_corp_tax", h_daily_corp_tax},
  {"dividend_payout", h_dividend_payout},
  {"cluster_economy", cluster_economy_step},
  {"daily_stock_price_recalculation", h_daily_stock_price_recalculation},
  {"traps_process", h_traps_process},
  {"npc_step", h_npc_step},
  {"autouncloak_sweeper", h_autouncloak_sweeper},
  {"fedspace_cleanup", h_fedspace_cleanup},
  {"broadcast_ttl_cleanup", h_broadcast_ttl_cleanup},
  {"system_notice_ttl", engine_notice_ttl_sweep},
  {"deadletter_retry", sweeper_engine_deadletter_retry}
};



/* static const CronHandler CRON_REGISTRY[] = { */
  /* {"traps_process", h_traps_process}, */
  /* {"npc_step", h_npc_step}, */
  /* {"autouncloak_sweeper", h_autouncloak_sweeper}, */
  /* {"fedspace_cleanup", h_fedspace_cleanup}, */
  /* {"broadcast_ttl_cleanup", h_broadcast_ttl_cleanup}, */
  /* {"planet_growth", h_planet_growth}, */
  /* {"daily_turn_reset", h_daily_turn_reset}, */
  /* {"terra_replenish", h_terra_replenish}, */
  /* {"daily_market_settlement", h_daily_market_settlement}, */
  /* {"daily_news_compiler", h_daily_news_compiler}, */
  /* {"cleanup_old_news", h_cleanup_old_news}, */
  /* {"system_notice_ttl", engine_notice_ttl_sweep}, */
  /* {"deadletter_retry", sweeper_engine_deadletter_retry} */
/* }; */

/* Lookup by task name (e.g., "fedspace_cleanup"). */
cron_handler_fn
cron_find (const char *name)
{
  for (size_t i = 0; CRON_REGISTRY[i].name; ++i)
    {
      if (strcasecmp (CRON_REGISTRY[i].name, name) == 0)
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

  /* dogdy, will sometimes not run after midnight */
  /* if (strncmp (schedule, "daily@", 6) == 0) */
  /*   { */
  /*     int HH = 0, MM = 0; */
  /*     if (sscanf (schedule + 6, "%2d:%2dZ", &HH, &MM) == 2) */
  /*    { */
  /*      /\* compute next UTC wall-clock occurrence >= now *\/ */
  /*      time_t t = (time_t) now_s; */
  /*      struct tm g; */
  /*      gmtime_r (&t, &g); */
  /*      int64_t today = */
  /*        now_s - (g.tm_hour * 3600 + g.tm_min * 60 + g.tm_sec); */
  /*      int64_t target = today + HH * 3600 + MM * 60; */
  /*      return (now_s <= target) ? target : (target + 86400); */
  /*    } */
  /*   } */

  if (strncmp (schedule, "daily@", 6) == 0)
    {
      int HH = 0, MM = 0;
      if (sscanf (schedule + 6, "%2d:%2dZ", &HH, &MM) == 2)
	{
	  /* 1. Get current broken-down time in UTC */
	  time_t t_now = (time_t) now_s;
	  struct tm g;

	  // Use gmtime_r for thread safety
	  gmtime_r (&t_now, &g);

	  /* 2. Set the target time components for "Today" */
	  // We overwrite the current H:M:S with the scheduled H:M:S
	  g.tm_hour = HH;
	  g.tm_min = MM;
	  g.tm_sec = 0;
	  g.tm_isdst = 0;	// Essential: Force no DST offset for UTC calculations

	  /* 3. Convert back to linear time_t */
	  // timegm is the inverse of gmtime. It handles normalization perfectly.
	  // It calculates the timestamp for Today @ HH:MM:00 UTC.
	  time_t target = timegm (&g);

	  /* 4. Comparison Logic (Fixing Issue #2) */
	  // If the calculated target is in the past OR is exactly now,
	  // we must schedule it for tomorrow.
	  //
	  // Previous Bug: (now_s <= target) allowed tasks to reschedule 
	  // for the current second, potentially causing double-execution.
	  if (target <= t_now)
	    {
	      target += 86400;	// Add exactly 24 hours (safe for UTC)
	    }

	  return (int64_t) target;
	}
    }

  /* unknown → run in 60s to avoid lock-up */
  return now_s + 60;
}


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
      if (ty && strcasecmp (ty, "s2s.ack") == 0)
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
  if (type && strcasecmp (type, "s2s.engine.shutdown") == 0)
    {
      // Begin graceful shutdown: set your running flag false, close down
      LOGI ("received shutdown command\n");
      // fprintf (stderr, "[engine] received shutdown command\n");
      // flip your engine running flag here…
    }
  else if (type && strcasecmp (type, "s2s.health.check") == 0)
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

static int
exec_notice_publish (sqlite3 *db, json_t *payload, const char *idem_key,
		     int64_t *out_notice_id)
{
  (void) idem_key;		/* idempotency handled at engine_commands layer */
  const char *scope = NULL, *message = NULL;
  int player_id = 0;

  if (!json_is_object (payload))
    return SQLITE_MISMATCH;
  json_t *js = json_object_get (payload, "scope");
  json_t *jp = json_object_get (payload, "player_id");
  json_t *jm = json_object_get (payload, "message");

  if (js && json_is_string (js))
    scope = json_string_value (js);
  if (jp && json_is_integer (jp))
    player_id = (int) json_integer_value (jp);
  if (jm && json_is_string (jm))
    message = json_string_value (jm);

  if (!scope || !message || player_id == 0)
    return SQLITE_MISUSE;

  // For now, hardcode severity and expires_at, as they are not in the payload
  const char *severity = "info";
  int64_t expires_at = 0;	// No expiration

  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db,
			       "INSERT INTO system_notice(created_at, scope, player_id, title, body, severity, expires_at) "
			       "VALUES(strftime('%s','now'), ?1, ?2, 'Notice', ?3, ?4, ?5);",
			       -1, &st, NULL);
  if (rc != SQLITE_OK)
    return rc;

  sqlite3_bind_text (st, 1, scope, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, player_id);
  sqlite3_bind_text (st, 3, message, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 4, severity, -1, SQLITE_TRANSIENT);
  if (expires_at > 0)
    sqlite3_bind_int64 (st, 5, expires_at);
  else
    sqlite3_bind_null (st, 5);

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

      if (type && strcasecmp (type, "broadcast.create") == 0)
	{
	  int64_t notice_id = 0;
	  ok =
	    (exec_broadcast_create (db, pl, idem_key, &notice_id) ==
	     SQLITE_OK) ? 0 : -1;
	}
      else if (type && strcasecmp (type, "notice.publish") == 0)
	{
	  int64_t notice_id = 0;
	  ok =
	    (exec_notice_publish (db, pl, idem_key, &notice_id) ==
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

  server_log_init_file ("./twclone.log", "[engine]", 0, LOG_DEBUG);
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

  const int tick_ms = CRON_PERIOD_MS;	// quick tick; we can fetch from DB config later
  struct pollfd pfd = {.fd = shutdown_fd,.events = POLLIN };

  LOGI ("[engine] loading s2s key ...\n");
  //fprintf (stderr, "[engine] loading s2s key ...\n");
  if (s2s_install_default_key (db_handle) != 0)
    {
      LOGE ("[engine] FATAL: S2S key missing/invalid.\n");
      //  fprintf (stderr, "[engine] FATAL: S2S key missing/invalid.\n");
      return 1;
    }
  LOGI ("[engine] connecting to 127.0.0.1:%d ...\n", g_cfg.s2s.tcp_port);
  s2s_conn_t *conn =
    s2s_tcp_client_connect ("127.0.0.1", g_cfg.s2s.tcp_port, 5000);
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

      if (iss_ok)
	{
	  iss_tick (now_ms);
	}
      if (fer_ok)
	{
	  fer_tick (now_ms);
	}

      if (now_ms - last_cmd_tick_ms >= CRON_PERIOD_MS)
	{
	  (void) server_commands_tick (db_get_handle (), CRON_BATCH_LIMIT);
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
      /* static int64_t g_next_notice_ttl_sweep = 0; */

      /* inside the engine’s main loop / tick */
      /* {
         int64_t now_ts = (int64_t) time (NULL);

         if (g_next_notice_ttl_sweep == 0)
         {
         // schedule first run ~5 minutes from start
         g_next_notice_ttl_sweep = now_ts + 300;
         }

         if (now_ts >= g_next_notice_ttl_sweep)
         {
         (void) engine_notice_ttl_sweep (db_get_handle (), now_ts);
         g_next_notice_ttl_sweep = now_ts + 24 * 3600;  // run daily
         }
         } */

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
	  /* ---- Stale cron lock sweeper ---- */
	  {
	    int64_t stale_threshold_ms =
	      monotonic_millis () - CRON_LOCK_STALE_MS;
	    sqlite3_stmt *reclaim = NULL;
	    if (sqlite3_prepare_v2
		(db_handle,
		 "DELETE FROM locks WHERE owner='server' AND until_ms < ?1;",
		 -1, &reclaim, NULL) == SQLITE_OK)
	      {
		sqlite3_bind_int64 (reclaim, 1, stale_threshold_ms);
		sqlite3_step (reclaim);
		int reclaimed = sqlite3_changes (db_handle);
		if (reclaimed > 0)
		  {
		    LOGI ("cron: reclaimed %d stale lock(s)", reclaimed);
		  }
	      }
	    sqlite3_finalize (reclaim);
	  }

	  /* ---- bounded cron runner (inline) ---- */
	  const int LIMIT = CRON_BATCH_LIMIT;
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

static int
engine_notice_ttl_sweep (sqlite3 *db, int64_t now_ms)
{
  (void) now_ms;
  /* delete ephemerals whose ttl expired; bounded batch */
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db,
			  "DELETE FROM system_notice "
			  "WHERE expires_at IS NOT NULL AND expires_at <= now "
			  "LIMIT 500;", -1, &st, NULL) != SQLITE_OK)
    return 1;
  sqlite3_step (st);
  sqlite3_finalize (st);
  return 0;
}

static int
sweeper_engine_deadletter_retry (sqlite3 *db, int64_t now_ms)
{
  (void) now_ms;		// now_ms is already implicit in the query logic (strftime('%s','now'))

  int retried_count = 0;

  sqlite3_stmt *st = NULL;
  sqlite3_stmt *update_st = NULL;
  int rc = SQLITE_OK;
  int final_rc = SQLITE_OK;

  // Select error commands ready for retry
  const char *sql_select_deadletters =
    "SELECT id, attempts FROM engine_commands WHERE status='error' AND attempts < ?1 LIMIT 500;";
  rc = sqlite3_prepare_v2 (db, sql_select_deadletters, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
	("sweeper_engine_deadletter_retry: Failed to prepare select statement: %s",
	 sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, MAX_RETRIES);

  const char *sql_update_deadletter =
    "UPDATE engine_commands SET status='ready', due_at=strftime('%s','now') + (attempts * 60) WHERE id=?1;";
  rc = sqlite3_prepare_v2 (db, sql_update_deadletter, -1, &update_st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
	("sweeper_engine_deadletter_retry: Failed to prepare update statement: %s",
	 sqlite3_errmsg (db));
      final_rc = rc;
      goto cleanup;
    }

  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int64_t cmd_id = sqlite3_column_int64 (st, 0);

      sqlite3_bind_int64 (update_st, 1, cmd_id);
      rc = sqlite3_step (update_st);
      if (rc != SQLITE_DONE)
	{
	  LOGE
	    ("sweeper_engine_deadletter_retry: Failed to update command %ld: %s",
	     cmd_id, sqlite3_errmsg (db));
	  final_rc = rc;
	  goto cleanup;
	}
      sqlite3_reset (update_st);	// Reset for next iteration
      retried_count++;
    }

  if (retried_count > 0)
    {
      LOGI
	("sweeper_engine_deadletter_retry: Retried %d deadletter commands.",
	 retried_count);
    }

cleanup:
  if (st)
    sqlite3_finalize (st);
  if (update_st)
    sqlite3_finalize (update_st);

  return final_rc;
}


// Cron handler to clean up expired Limpet mines
int
cron_limpet_ttl_cleanup (sqlite3 *db, int64_t now_s)
{
  if (!g_cfg.mines.limpet.enabled)
    {
      return 0;			// Limpet mines disabled, no cleanup needed
    }
  if (g_cfg.mines.limpet.limpet_ttl_days <= 0)
    {
      LOGW
	("limpet_ttl_days is not set or zero. Skipping Limpet TTL cleanup.");
      return 0;			// No TTL is set, so no cleanup needed
    }

  if (!try_lock (db, "limpet_ttl_cleanup", now_s))
    return 0;

  LOGI ("limpet_ttl_cleanup: Starting Limpet mine TTL cleanup.");

  int rc = begin (db);
  if (rc)
    {
      unlock (db, "limpet_ttl_cleanup");
      return rc;
    }

  sqlite3_stmt *st = NULL;
  int removed_count = 0;

  // Calculate the expiry timestamp: deployed_at + (limpet_ttl_days * seconds_in_day)
  // Assuming deployed_at is UNIX epoch.
  long long expiry_threshold_s =
    now_s - ((long long) g_cfg.mines.limpet.limpet_ttl_days * 24 * 3600);

  const char *sql_delete_expired =
    "DELETE FROM sector_assets "
    "WHERE asset_type = ?1 AND deployed_at <= ?2;";

  rc = sqlite3_prepare_v2 (db, sql_delete_expired, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("limpet_ttl_cleanup: Failed to prepare delete statement: %s",
	    sqlite3_errmsg (db));
      rollback (db);
      unlock (db, "limpet_ttl_cleanup");
      return rc;
    }

  sqlite3_bind_int (st, 1, ASSET_LIMPET_MINE);
  sqlite3_bind_int64 (st, 2, expiry_threshold_s);

  rc = sqlite3_step (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("limpet_ttl_cleanup: Failed to execute delete statement: %s",
	    sqlite3_errmsg (db));
      rollback (db);
      unlock (db, "limpet_ttl_cleanup");
      sqlite3_finalize (st);
      return rc;
    }
  removed_count = sqlite3_changes (db);
  sqlite3_finalize (st);

  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("limpet_ttl_cleanup: Commit failed: %s", sqlite3_errmsg (db));
      rollback (db);		// Attempt to rollback if commit fails
      unlock (db, "limpet_ttl_cleanup");
      return rc;
    }

  if (removed_count > 0)
    {
      LOGI ("limpet_ttl_cleanup: Removed %d expired Limpet mines.",
	    removed_count);
    }
  else
    {
      LOGD ("limpet_ttl_cleanup: No expired Limpet mines found.");
    }

  unlock (db, "limpet_ttl_cleanup");
  return SQLITE_OK;
}


int
h_daily_bank_interest_tick (sqlite3 *db, int64_t now_s)
{
  if (!try_lock (db, "daily_bank_interest_tick", now_s))
    return 0;

  LOGI ("daily_bank_interest_tick: Starting daily bank interest accrual.");

  int rc = begin (db);
  if (rc)
    {
      unlock (db, "daily_bank_interest_tick");
      return rc;
    }

  sqlite3_stmt *st = NULL;
  const char *sql_select_accounts =
    "SELECT id, owner_type, owner_id, balance, interest_rate_bp, last_interest_tick "
    "FROM bank_accounts WHERE is_active = 1 AND interest_rate_bp > 0;";

  rc = sqlite3_prepare_v2 (db, sql_select_accounts, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
	("daily_bank_interest_tick: Failed to prepare select accounts statement: %s",
	 sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }

  int current_epoch_day = get_utc_epoch_day (now_s);
  int processed_accounts = 0;

  long long min_balance_for_interest =
    h_get_config_int_unlocked (db, "bank_min_balance_for_interest", 0);
  long long max_daily_per_account =
    h_get_config_int_unlocked (db, "bank_max_daily_interest_per_account",
			       9223372036854775807LL);

  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int account_id = sqlite3_column_int (st, 0);
      const char *owner_type = (const char *) sqlite3_column_text (st, 1);
      int owner_id = sqlite3_column_int (st, 2);
      long long balance = sqlite3_column_int64 (st, 3);
      int interest_rate_bp = sqlite3_column_int (st, 4);
      int last_interest_tick = sqlite3_column_int (st, 5);

      if (balance < min_balance_for_interest)
	{
	  continue;		// Skip accounts below minimum balance
	}

      int days_to_accrue = current_epoch_day - last_interest_tick;
      if (days_to_accrue <= 0)
	{
	  continue;		// Already processed for today or future tick
	}
      if (days_to_accrue > MAX_BACKLOG_DAYS)
	{
	  days_to_accrue = MAX_BACKLOG_DAYS;	// Cap the backlog
	}

      char tx_group_id[33];
      h_generate_hex_uuid (tx_group_id, sizeof (tx_group_id));

      for (int i = 0; i < days_to_accrue; ++i)
	{
	  if (balance <= 0)
	    break;		// No interest on zero or negative balance

	  // interest = floor( balance * interest_rate_bp / (10000 * 365) )
	  long long daily_interest =
	    (balance * interest_rate_bp) / (10000 * 365);

	  if (daily_interest > max_daily_per_account)
	    {
	      daily_interest = max_daily_per_account;
	    }

	  if (daily_interest > 0)
	    {
	      // Use h_add_credits_unlocked to record interest and update balance
	      int add_rc =
		h_add_credits_unlocked (db, account_id, daily_interest,
					"INTEREST", tx_group_id, &balance);
	      if (add_rc != SQLITE_OK)
		{
		  LOGE
		    ("daily_bank_interest_tick: Failed to add interest to account %d (owner %s:%d): %s",
		     account_id, owner_type, owner_id, sqlite3_errmsg (db));
		  // Continue to next account or abort? Aborting for now.
		  goto rollback_and_unlock;
		}
	    }
	}

      // Update last_interest_tick in bank_accounts
      sqlite3_stmt *update_tick_st = NULL;
      const char *sql_update_tick =
	"UPDATE bank_accounts SET last_interest_tick = ? WHERE id = ?;";
      int update_rc =
	sqlite3_prepare_v2 (db, sql_update_tick, -1, &update_tick_st, NULL);
      if (update_rc != SQLITE_OK)
	{
	  LOGE
	    ("daily_bank_interest_tick: Failed to prepare update last_interest_tick statement: %s",
	     sqlite3_errmsg (db));
	  goto rollback_and_unlock;
	}
      sqlite3_bind_int (update_tick_st, 1, current_epoch_day);
      sqlite3_bind_int (update_tick_st, 2, account_id);
      update_rc = sqlite3_step (update_tick_st);
      sqlite3_finalize (update_tick_st);
      if (update_rc != SQLITE_DONE)
	{
	  LOGE
	    ("daily_bank_interest_tick: Failed to update last_interest_tick for account %d: %s",
	     account_id, sqlite3_errmsg (db));
	  goto rollback_and_unlock;
	}
      processed_accounts++;
    }

  if (rc != SQLITE_DONE)
    {
      LOGE
	("daily_bank_interest_tick: Error stepping through bank_accounts: %s",
	 sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }

  sqlite3_finalize (st);
  st = NULL;

  rc = commit (db);
  if (rc != SQLITE_OK)
    {
      LOGE ("daily_bank_interest_tick: commit failed: %s",
	    sqlite3_errmsg (db));
      goto rollback_and_unlock;
    }

  LOGI
    ("daily_bank_interest_tick: Successfully processed interest for %d accounts.",
     processed_accounts);
  unlock (db, "daily_bank_interest_tick");
  return SQLITE_OK;

rollback_and_unlock:
  if (st)
    sqlite3_finalize (st);
  rollback (db);
  unlock (db, "daily_bank_interest_tick");
  return rc;
}
