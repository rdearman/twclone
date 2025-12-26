#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h> // For floor and fabs
#include "database_cmd.h" // For h_player_apply_progress
#include "server_players.h" // For h_player_apply_progress
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
/* local includes */
#include "s2s_keyring.h"
#include "s2s_transport.h"
#include "database.h"
#include "server_envelope.h"
#include "s2s_transport.h"
#include "engine_consumer.h"
#include "server_engine.h"
#include "database.h"
#include "server_loop.h"        // Assuming this contains functions to communicate with clients
#include "server_universe.h"
#include "server_log.h"
#include "server_cron.h"
#include "common.h"
#include "server_config.h"
#include "server_clusters.h"
#include "globals.h"        // For g_xp_align config
#include "database_cmd.h"   // For h_player_apply_progress, db_player_get_alignment, h_get_cluster_alignment_band
#include "server_stardock.h"


/* handlers (implemented below) */
/* static int sweeper_engine_deadletter_retry (sqlite3 * db, int64_t now_ms); */
// Define the interval for the main game loop ticks in seconds
#define GAME_TICK_INTERVAL_SEC 60
#define MAX_RETRIES 3
#define MAX_BACKLOG_DAYS 30     // Cap for how many days of interest can be applied retroactively
static const int64_t CRON_PERIOD_MS = 500;      /* run every 0.5s */
static const int CRON_BATCH_LIMIT = 8;  /* max tasks per runner tick */
static const int64_t CRON_LOCK_STALE_MS = 120000;       /* reclaim after 2 min */


int h_daily_bank_interest_tick (db_t *db, int64_t now_s);
static int engine_notice_ttl_sweep (db_t *db, int64_t now_ms);
static int sweeper_engine_deadletter_retry (db_t *db, int64_t now_ms);
int cron_limpet_ttl_cleanup (db_t *db, int64_t now_s);


static inline uint64_t
monotonic_millis (void)
{
  struct timespec ts;
  clock_gettime (CLOCK_MONOTONIC, &ts);
  return (uint64_t) ts.tv_sec * 1000ULL + (uint64_t) ts.tv_nsec / 1000000ULL;
}


/* ---- Cron: registry ---- */
typedef int (*cron_handler_fn) (db_t *db, int64_t now_s);
typedef struct
{
  const char *name;             /* matches cron_tasks.name */
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
  {"cluster_black_market", cluster_black_market_step},
  {"daily_stock_price_recalculation", h_daily_stock_price_recalculation},
  {"port_economy", h_port_economy_tick},
  {"shield_regen", h_shield_regen_tick},
  {"system_notice_ttl", engine_notice_ttl_sweep},
  {"deadletter_retry", sweeper_engine_deadletter_retry},
  {NULL,NULL} /* required terminator */
};


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
    {
      return now_s + 60;
    }
  if (strncmp (schedule, "every:", 6) == 0)
    {
      const char *p = schedule + 6;
      char *end = NULL;
      long n = strtol (p, &end, 10);


      if (n <= 0)
        {
          return now_s + 60;
        }
      if (end && (*end == 's' || *end == 'S'))
        {
          return now_s + n;
        }
      if (end && (*end == 'm' || *end == 'M'))
        {
          return now_s + n * 60L;
        }
      return now_s + n;         /* default seconds */
    }
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
          g.tm_isdst = 0;       // Essential: Force no DST offset for UTC calculations
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
              target += 86400;  // Add exactly 24 hours (safe for UTC)
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
  .priority_types_csv = "s2s.broadcast.sweep,player.login,player.trade.v1",
  .consumer_key = "game_engine"
};


void
engine_tick (db_t *db)
{
  eng_consumer_metrics_t m;
  if (engine_consume_tick (db, &G_CFG, &m) == 0)
    {
      LOGE ("events: processed=%d quarantined=%d last_id=%lld lag=%lld\n",
            m.processed, m.quarantined, m.last_event_id, m.lag);
      // LOGE(
      //             "[engine] events: processed=%d quarantined=%d last_id=%lld lag=%lld\n",
      //     m.processed, m.quarantined, m.last_event_id, m.lag);
    }
}


/* Returns a new env (caller must json_decref(result)). */
json_t *
engine_build_command_push (const char *cmd_type,
                           const char *idem_key,
                           json_t *payload_obj,
                           /* owned by caller */
                           const char *correlation_id,
                           /* may be NULL */
                           int priority)        /* <=0 -> default 100 */
{
  if (!cmd_type || !idem_key || !payload_obj)
    {
      return NULL;
    }
  if (priority <= 0)
    {
      priority = 100;
    }
  json_t *pl = json_object ();


  json_object_set_new (pl, "cmd_type", json_string (cmd_type));
  json_object_set_new (pl, "idem_key", json_string (idem_key));
  json_object_set (pl, "payload", payload_obj);
  json_object_set_new (pl, "priority", json_integer (priority));


  if (correlation_id)
    {
      json_object_set_new (pl, "correlation_id", json_string (correlation_id));
    }
  json_t *env = s2s_make_env ("s2s.command.push", "engine", "server", pl);


  json_decref (pl);
  return env;
}


static int
engine_demo_push (s2s_conn_t *c)
{
  /* build the command payload */
  json_t *cmdpl = json_object ();
  json_object_set_new (cmdpl, "cmd_type", json_string ("notice.publish"));
  json_object_set_new (cmdpl, "idem_key", json_string ("player:42:hello:001")); // keep identical to test idempotency
  json_object_set_new (cmdpl, "correlation_id", json_string ("demo-001"));
  json_object_set_new (cmdpl, "priority", json_integer (100));


  json_t *payload = json_object ();


  json_object_set_new (payload, "scope", json_string ("player"));
  json_object_set_new (payload, "player_id", json_integer (42));
  json_object_set_new (payload, "message", json_string ("Hello captain!"));
  json_object_set_new (payload, "ttl_seconds", json_integer (3600));
  json_object_set_new (cmdpl, "payload", payload);
  json_t *env = s2s_make_env ("s2s.command.push", "engine", "server", cmdpl);


  json_decref (cmdpl);
  int rc = s2s_send_env (c, env, 3000);


  json_decref (env);
  LOGI ("command.push send rc=%d\n", rc);
  // LOGE( "[engine] command.push send rc=%d\n", rc);
  if (rc != 0)
    {
      return rc;
    }
  json_t *resp = NULL;


  rc = s2s_recv_env (c, &resp, 3000);
  LOGI ("command.push recv rc=%d\n", rc);
  // LOGE( "[engine] command.push recv rc=%d\n", rc);
  if (rc == 0 && resp)
    {
      const char *ty = s2s_env_type (resp);


      if (ty && strcasecmp (ty, "s2s.ack") == 0)
        {
          json_t *ackpl = s2s_env_payload (resp);
          json_t *dup = json_object_get (ackpl, "duplicate");


          LOGW ("ack duplicate=%s\n",
                (dup && json_is_true (dup)) ? "true" : "false");
          // LOGE( "[engine] ack duplicate=%s\n",
          // (dup && json_is_true (dup)) ? "true" : "false");
        }
      else
        {
          LOGE ("got non-ack (%s)\n", ty ? ty : "null");
          // LOGE( "[engine] got non-ack (%s)\n", ty ? ty : "null");
        }
      json_decref (resp);
    }
  return rc;
}


// Helper to calculate illegal alignment delta (from design brief)
static int
h_compute_illegal_alignment_delta (int player_alignment,
                                   int cluster_align_band_id,
                                   double value)
{
  db_t *db = db_get_handle ();  // Get a DB handle
  int player_align_band_id = 0;
  int player_band_is_good = 0;
  int player_band_is_evil = 0;
  db_alignment_band_for_value (db,
                               player_alignment,
                               &player_align_band_id,
                               NULL,
                               NULL,
                               &player_band_is_good,
                               &player_band_is_evil,
                               NULL,
                               NULL);
  int cluster_band_is_good = 0;
  int cluster_band_is_evil = 0;
  // For cluster, we have the band ID, so query alignment_band directly
  db_error_t err;


  db_error_clear (&err);
  db_res_t *res = NULL;
  const char *sql =
    "SELECT is_good, is_evil FROM alignment_band WHERE id = $1;";

  db_bind_t params[1] = { db_bind_i32 (cluster_align_band_id) };


  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          cluster_band_is_good = (int) db_res_col_i32 (res, 0, &err);
          cluster_band_is_evil = (int) db_res_col_i32 (res, 1, &err);
        }
      db_res_finalize (res);
    }

  int base_penalty = floor (value / g_xp_align.illegal_base_align_divisor);


  if (base_penalty < 1)
    {
      base_penalty = 1;
    }
  double factor = 1.0;


  if (player_band_is_evil)
    {
      factor *= g_xp_align.illegal_align_factor_evil;                               // Already evil; small incremental penalty
    }
  else if (player_band_is_good)
    {
      factor *= g_xp_align.illegal_align_factor_good;                                // Good doing bad: harsher
    }
  if (cluster_band_is_good)
    {
      factor *= 1.5;                          // Good cluster makes it harsher (hardcoded for now, could be config)
    }
  else if (cluster_band_is_evil)
    {
      factor *= 0.5;                              // Evil cluster makes it less harsh (hardcoded for now, could be config)
    }
  int align_delta = -(int)floor (base_penalty * factor);


  if (align_delta == 0)
    {
      align_delta = -1;                     // Ensure there's always a penalty
    }
  return align_delta;
}


// h_player_progress_from_event_payload: Processes an engine event payload to apply XP/Alignment changes.
int
h_player_progress_from_event_payload (json_t *ev_payload)
{
  if (!ev_payload || !json_is_object (ev_payload))
    {
      LOGE ("h_player_progress_from_event_payload: Invalid event payload.");
      return -1;
    }
  db_t *db = db_get_handle ();


  if (!db)
    {
      LOGE ("h_player_progress_from_event_payload: Failed to get DB handle.");
      return -1;
    }
  json_t *j_player_id = json_object_get (ev_payload, "player_id");
  json_t *j_event_type = json_object_get (ev_payload, "type");  // Event type from engine_events


  if (!json_is_integer (j_player_id) || !json_is_string (j_event_type))
    {
      LOGE (
        "h_player_progress_from_event_payload: Missing player_id or event type in payload.");
      return -1;
    }
  int player_id = json_integer_value (j_player_id);
  const char *event_type = json_string_value (j_event_type);
  long long xp_delta = 0;
  int align_delta = 0;
  const char *reason = NULL;   // For logging


  if (strcasecmp (event_type, "player.trade.v1") == 0)
    {
      json_t *j_credits_delta = json_object_get (ev_payload, "credits_delta");
      json_t *j_is_illegal = json_object_get (ev_payload, "is_illegal");
      json_t *j_sector_id = json_object_get (ev_payload, "sector_id");


      if (!json_is_integer (j_credits_delta) ||
          !json_is_boolean (j_is_illegal) || !json_is_integer (j_sector_id))
        {
          LOGE (
            "h_player_progress_from_event_payload: Missing trade details in player.trade.v1 payload.");
          return -1;
        }
      long long credits_delta = json_integer_value (j_credits_delta);
      bool is_illegal = json_is_true (j_is_illegal);
      int sector_id = json_integer_value (j_sector_id);


      if (credits_delta > 0)     // XP only for selling/profit
        {
          xp_delta = floor ((double)credits_delta / g_xp_align.trade_xp_ratio);
        }
      if (is_illegal)
        {
          // Calculate alignment penalty for illegal trade
          int player_alignment = 0;


          if (db_player_get_alignment (db, player_id,
                                       &player_alignment) != 0)
            {
              LOGE (
                "h_player_progress_from_event_payload: Failed to get player %d alignment for illegal trade.",
                player_id);
              return -1;
            }
          int cluster_align_band_id;


          h_get_cluster_alignment_band (db, sector_id, &cluster_align_band_id);
          align_delta = h_compute_illegal_alignment_delta (player_alignment,
                                                           cluster_align_band_id,
                                                           fabs (
                                                             (double)
                                                             credits_delta));
        }
      reason = "trade";
    }
  // TODO: Add more event types (player.destroy_ship.v1, etc.) here
  return h_player_apply_progress (db, player_id, xp_delta, align_delta, reason);
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
  // LOGE( "[%s] s2s metrics: sent=%" PRIu64 " recv=%" PRIu64
  //       " auth_fail=%" PRIu64 " too_big=%" PRIu64 "\n",
  //      who, sent, recv, auth_fail, too_big);
}


static void
engine_s2s_drain_once (s2s_conn_t *conn)
{
  json_t *msg = NULL;
  int rc = s2s_recv_json (conn, &msg, 0);       // 0ms => non-blocking
  if (rc != S2S_OK || !msg)
    {
      return;
    }
  const char *type = json_string_value (json_object_get (msg, "type"));


  if (type && strcasecmp (type, "s2s.engine.shutdown") == 0)
    {
      // Begin graceful shutdown: set your running flag false, close down
      LOGI ("received shutdown command\n");
      // // LOGE( "[engine] received shutdown command\n");
      // flip your engine running flag here…
    }
  else if (type && strcasecmp (type, "s2s.health.check") == 0)
    {
      // Optional: allow server to ping any time
      time_t now = time (NULL);
      static time_t start_ts;


      if (!start_ts)
        {
          start_ts = now;
        }
      json_t *ack = json_object ();


      json_object_set_new (ack, "v", json_integer (1));
      json_object_set_new (ack, "type", json_string ("s2s.health.ack"));
      json_object_set_new (ack, "id", json_string ("rt-ack"));
      json_object_set_new (ack, "ts", json_integer ((json_int_t) now));


      json_t *payload = json_object ();


      json_object_set_new (payload, "role", json_string ("engine"));
      json_object_set_new (payload, "version", json_string ("0.1"));
      json_object_set_new (payload, "uptime_s",
                           json_integer ((json_int_t) (now - start_ts)));
      json_object_set_new (ack, "payload", payload);


      s2s_send_json (conn, ack, 5000);
      json_decref (ack);
    }
  else
    {
      // Unknown → send s2s.error
      json_t *err = json_object ();


      json_object_set_new (err, "v", json_integer (1));
      json_object_set_new (err, "type", json_string ("s2s.error"));
      json_object_set_new (err, "id", json_string ("unknown"));
      json_object_set_new (err, "ts", json_integer ((json_int_t) time (NULL)));


      json_t *payload = json_object ();


      json_object_set_new (payload, "reason", json_string ("unknown_type"));
      json_object_set_new (err, "payload", payload);


      s2s_send_json (conn, err, 5000);
      json_decref (err);
    }
  json_decref (msg);
}


/* --- executor: broadcast.create → INSERT INTO system_notice --- */
static int
exec_broadcast_create (db_t *db, json_t *payload, const char *idem_key,
                       int64_t *out_notice_id)
{
  (void) idem_key;              /* idempotency handled at engine_commands layer */
  const char *title = NULL, *body = NULL, *severity = "info";
  int64_t expires_at = 0;


  if (!json_is_object (payload))
    {
      return -1;
    }
  json_t *jt = json_object_get (payload, "title");
  json_t *jb = json_object_get (payload, "body");
  json_t *js = json_object_get (payload, "severity");
  json_t *je = json_object_get (payload, "expires_at");


  if (jt && json_is_string (jt))
    {
      title = json_string_value (jt);
    }
  if (jb && json_is_string (jb))
    {
      body = json_string_value (jb);
    }
  if (js && json_is_string (js))
    {
      severity = json_string_value (js);
    }
  if (je && json_is_integer (je))
    {
      expires_at = (int64_t) json_integer_value (je);
    }
  if (!title || !body)
    {
      return -1;
    }

  int64_t now_s = (int64_t)time (NULL);
  const char *sql =
    "INSERT INTO system_notice(created_at, title, body, severity, expires_at) "
    "VALUES($1, $2, $3, $4, $5);";

  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[5];


  params[0] = db_bind_i64 (now_s);
  params[1] = db_bind_text (title);
  params[2] = db_bind_text (body);
  params[3] = db_bind_text (severity ? severity : "info");

  if (expires_at > 0)
    {
      params[4] = db_bind_i64 (expires_at);
    }
  else
    {
      params[4] = db_bind_null ();
    }

  int64_t new_id = 0;


  if (!db_exec_insert_id (db, sql, params, 5, &new_id, &err))
    {
      return -1;
    }

  if (out_notice_id)
    {
      *out_notice_id = new_id;
    }
  return 0;
}


static int
exec_notice_publish (db_t *db, json_t *payload, const char *idem_key,
                     int64_t *out_notice_id)
{
  (void) idem_key;              /* idempotency handled at engine_commands layer */
  const char *scope = NULL, *message = NULL;
  int player_id = 0;


  if (!json_is_object (payload))
    {
      return -1;
    }
  json_t *js = json_object_get (payload, "scope");
  json_t *jp = json_object_get (payload, "player_id");
  json_t *jm = json_object_get (payload, "message");


  if (js && json_is_string (js))
    {
      scope = json_string_value (js);
    }
  if (jp && json_is_integer (jp))
    {
      player_id = (int) json_integer_value (jp);
    }
  if (jm && json_is_string (jm))
    {
      message = json_string_value (jm);
    }
  if (!scope || !message || player_id == 0)
    {
      return -1;
    }
  // For now, hardcode severity and expires_at, as they are not in the payload
  const char *severity = "info";
  int64_t expires_at = 0;       // No expiration

  int64_t now_s = (int64_t)time (NULL);

  const char *sql =
    "INSERT INTO system_notice(created_at, scope, player_id, title, body, severity, expires_at) "
    "VALUES($1, $2, $3, 'Notice', $4, $5, $6);";

  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[6];


  params[0] = db_bind_i64 (now_s);
  params[1] = db_bind_text (scope);
  params[2] = db_bind_i32 (player_id);
  params[3] = db_bind_text (message);
  params[4] = db_bind_text (severity);

  if (expires_at > 0)
    {
      params[5] = db_bind_i64 (expires_at);
    }
  else
    {
      params[5] = db_bind_null ();
    }

  int64_t new_id = 0;


  if (!db_exec_insert_id (db, sql, params, 6, &new_id, &err))
    {
      return -1;
    }

  if (out_notice_id)
    {
      *out_notice_id = new_id;
    }
  return 0;
}


/* --- tick: pull ready commands and execute --- */
static int
server_commands_tick (db_t *db, int max_rows)
{
  if (max_rows <= 0 || max_rows > 100)
    {
      max_rows = 16;
    }
  int processed = 0;
  int64_t now_s = (int64_t)time (NULL);

  const char *sql_select =
    "SELECT id, type, payload, idem_key "
    "FROM engine_commands "
    "WHERE status='ready' AND due_at <= $1 "
    "ORDER BY priority ASC, due_at ASC, id ASC "
    "LIMIT $2;";

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[2];


  params[0] = db_bind_i64 (now_s);
  params[1] = db_bind_i32 (max_rows);

  if (!db_query (db, sql_select, params, 2, &res, &err))
    {
      return 0;
    }

  while (db_res_step (res, &err))
    {
      int64_t cmd_id = db_res_col_i64 (res, 0, &err);
      const char *tmp_type = db_res_col_text (res, 1, &err);
      const char *tmp_payload = db_res_col_text (res, 2, &err);
      const char *tmp_idem = db_res_col_text (res, 3, &err);

      char *type = tmp_type ? strdup (tmp_type) : NULL;
      char *payload_json = tmp_payload ? strdup (tmp_payload) : NULL;
      char *idem_key = tmp_idem ? strdup (tmp_idem) : NULL;

      /* mark running */
      const char *sql_running =
        "UPDATE engine_commands SET status='running', started_at=$1 WHERE id=$2;";

      db_bind_t run_params[2];


      run_params[0] = db_bind_i64 (now_s);
      run_params[1] = db_bind_i64 (cmd_id);

      db_exec (db, sql_running, run_params, 2, &err);

      int ok = -1;
      json_error_t jerr;
      json_t *pl = payload_json ? json_loads (payload_json, 0, &jerr) : NULL;


      if (type && strcasecmp (type, "broadcast.create") == 0)
        {
          int64_t notice_id = 0;


          ok =
            (exec_broadcast_create (db, pl, idem_key, &notice_id) ==
             0) ? 0 : -1;
        }
      else if (type && strcasecmp (type, "notice.publish") == 0)
        {
          int64_t notice_id = 0;


          ok =
            (exec_notice_publish (db, pl, idem_key, &notice_id) ==
             0) ? 0 : -1;
        }
      else
        {
          /* unknown command type → mark error */
          ok = -1;
        }
      if (pl)
        {
          json_decref (pl);
        }

      free (type);
      free (payload_json);
      free (idem_key);

      /* finalize status */
      if (ok == 0)
        {
          const char *sql_done =
            "UPDATE engine_commands SET status='done', finished_at=$1 WHERE id=$2;";
          db_bind_t done_params[2] = { db_bind_i64 (now_s),
                                       db_bind_i64 (cmd_id) };


          db_exec (db, sql_done, done_params, 2, &err);
        }
      else
        {
          const char *sql_err =
            "UPDATE engine_commands SET status='error', attempts=attempts+1, finished_at=$1 WHERE id=$2;";
          db_bind_t err_params[2] = { db_bind_i64 (now_s),
                                      db_bind_i64 (cmd_id) };


          db_exec (db, sql_err, err_params, 2, &err);
        }
      processed++;
    }
  db_res_finalize (res);
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

  // 1. Close the stale handle inherited from the parent
  db_handle_close_and_reset ();

  // 2. Get a fresh, new handle (idempotent open)
  db_t *db_handle = db_get_handle ();


  if (db_handle == NULL)
    {
      LOGE ("The fresh open failed.");
      return 1;
    }
  // CRITICAL FIX: Reload configuration in the child process
  if (!load_eng_config ())
    {
      LOGE ("[engine] FATAL: Failed to load engine configuration.\n");
      return 1;
    }
  // Initialize tavern settings (load from DB) for engine cron jobs
  if (tavern_settings_load () != 0)
    {
      LOGW ("[engine] Failed to load tavern settings. Using defaults.");
    }

  int server_port_unused = 0; // Not needed by engine, but db_load_ports requires it
  int s2s_port = 0;


  if (db_load_ports (&server_port_unused, &s2s_port) != 0)
    {
      LOGW (
        "[engine] Could not load ports from database in child, using defaults.");
      // If s2s_port is critical and not loaded, we might need a FATAL here
      // For now, allow it to proceed with defaults if any are set in g_cfg
    }
  else
    {
      g_cfg.s2s.tcp_port = s2s_port;   // Ensure g_cfg is updated with the loaded s2s_port
      LOGI ("[engine] Loaded s2s port from database: %d", g_cfg.s2s.tcp_port);
    }
  LOGI ("[engine] child up. pid=%d\n", getpid ());
  //printf ("[engine] child up. pid=%d\n", getpid ());
  const int tick_ms = CRON_PERIOD_MS;   // quick tick; we can fetch from DB config later
  struct pollfd pfd = {.fd = shutdown_fd,.events = POLLIN };


  LOGI ("[engine] loading s2s key ...\n");
  // LOGE( "[engine] loading s2s key ...\n");
  if (s2s_install_default_key (db_handle) != 0)
    {
      LOGE ("[engine] FATAL: S2S key missing/invalid.\n");
      // LOGE( "[engine] FATAL: S2S key missing/invalid.\n");
      return 1;
    }
  LOGI ("[engine] connecting to 127.0.0.1:%d ...\n", g_cfg.s2s.tcp_port);
  s2s_conn_t *conn =
    s2s_tcp_client_connect ("127.0.0.1", g_cfg.s2s.tcp_port, 5000);


  if (!conn)
    {
      LOGE ("[engine] connect failed\n");
      // LOGE( "[engine] connect failed\n");
      return 1;
    }
  s2s_debug_dump_conn ("engine", conn); // optional debug
  /* Engine initiates: send hello, then expect ack */
  time_t now = time (NULL);
  json_t *hello = json_object ();


  json_object_set_new (hello, "v", json_integer (1));
  json_object_set_new (hello, "type", json_string ("s2s.health.hello"));
  json_object_set_new (hello, "id", json_string ("boot-1"));
  json_object_set_new (hello, "ts", json_integer ((json_int_t) now));


  json_t *payload = json_object ();


  json_object_set_new (payload, "role", json_string ("engine"));
  json_object_set_new (payload, "version", json_string ("0.1"));
  json_object_set_new (hello, "payload", payload);


  int rc_send = s2s_send_json (conn, hello, 5000);


  json_decref (hello);
  LOGI ("[engine] hello send rc=%d\n", rc_send);
  // LOGE( "[engine] hello send rc=%d\n", rc);
  json_t *msg = NULL;


  int rc_recv = s2s_recv_json (conn, &msg, 5000);


  LOGI ("[engine] first frame rc=%d\n", rc_recv);
  // LOGE( "[engine] first frame rc=%d\n", rc);
  if (rc_recv == S2S_OK && msg)
    {
      const char *type = json_string_value (json_object_get (msg, "type"));


      if (type && strcmp (type, "s2s.health.ack") == 0)
        {
          LOGI ("[engine] Ping test complete\n");
        }
      json_decref (msg);
    }
  LOGI ("[engine] Running Smoke Test\n");
  (void) engine_demo_push (conn);
  LOGI ("[engine] Running Smoke Test\n");
  static time_t last_metrics = 0;
  static time_t last_cmd_tick_ms = 0;
  /* One-time ISS bootstrap */
  int iss_ok = iss_init_once ();        // 1 if ISS+Stardock found, else 0


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
      // Sleep until next tick or until shutdown pipe changes
      int rc_poll = poll (&pfd, 1, tick_ms);


      if (rc_poll > 0)
        {
          // either data or EOF on the pipe means: time to exit
          char buf[8];
          ssize_t n = read (shutdown_fd, buf, sizeof (buf));


          (void) n;             // We don't care what's read; any activity/EOF = shutdown
          LOGI ("[engine] shutdown signal received.\n");
          break;
        }
      if (rc_poll == 0)
        {
          db_error_t err;


          db_error_clear (&err);
          /* ---- Stale cron lock sweeper ---- */
          {
            int64_t stale_threshold_ms =
              monotonic_millis () - CRON_LOCK_STALE_MS;

            const char *sql_reclaim =
              "DELETE FROM locks WHERE owner='server' AND until_ms < $1;";
            db_bind_t params[1] = { db_bind_i64 (stale_threshold_ms) };


            db_exec (db_handle, sql_reclaim, params, 1, &err);
            // Ignore reclaimed count for now, or assume success implies cleanup
          }
          /* ---- bounded cron runner (inline) ---- */
          const int LIMIT = CRON_BATCH_LIMIT;
          uint64_t now_s = (int64_t) time (NULL);

          const char *sql_pick =
            "SELECT id, name, schedule FROM cron_tasks "
            "WHERE enabled=1 "
            "  AND (next_due_at IS NULL OR next_due_at <= $1) "
            "ORDER BY next_due_at ASC "
            "LIMIT $2;";


          typedef struct {
            int64_t id;
            char *name;
            char *schedule;
          } task_t;

          task_t tasks[CRON_BATCH_LIMIT];
          int task_count = 0;

          db_res_t *res = NULL;
          db_bind_t params[2];


          params[0] = db_bind_i64 (now_s);
          params[1] = db_bind_i32 (LIMIT);

          if (db_query (db_handle, sql_pick, params, 2, &res, &err))
            {
              while (db_res_step (res, &err) && task_count < LIMIT)
                {
                  tasks[task_count].id = db_res_col_i64 (res, 0, &err);
                  const char *nm = db_res_col_text (res, 1, &err);
                  const char *sch = db_res_col_text (res, 2, &err);


                  tasks[task_count].name = nm ? strdup (nm) : NULL;
                  tasks[task_count].schedule = sch ? strdup (sch) : NULL;
                  task_count++;
                }
              db_res_finalize (res);
            }

          for (int i = 0; i < task_count; i++)
            {
              int64_t id = tasks[i].id;
              const char *nm = tasks[i].name;
              const char *sch = tasks[i].schedule;

              /* run handler if registered */
              cron_handler_fn fn = cron_find (nm);


              if (fn)
                {
                  fn (db_handle, now_s);
                }

              /* reschedule deterministically */
              int64_t next_due = cron_next_due_from (now_s, sch);
              const char *sql_upd =
                "UPDATE cron_tasks "
                "SET last_run_at=$2, next_due_at=$3 "
                "WHERE id=$1;";

              db_bind_t up_params[3];


              up_params[0] = db_bind_i64 (id);
              up_params[1] = db_bind_i64 (now_s);
              up_params[2] = db_bind_i64 (next_due);

              db_exec (db_handle, sql_upd, up_params, 3, &err);

              free (tasks[i].name);
              free (tasks[i].schedule);
            }
          /* (keep anything else you want here; short, bounded work only) */
        }
      else if (rc_poll < 0 && errno != EINTR)
        {
          LOGE ("[engine] poll error: %s\n", strerror (errno));
          break;
        }
    }
  db_close ();
  LOGI ("[engine] child exiting cleanly.\n");
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
    {
      *out_pid = pid;
    }
  if (out_shutdown_fd)
    {
      *out_shutdown_fd = pipefd[1];
    }
  LOGI ("[engine] pid=%d\n", (int) pid);
  //  printf ("[engine] pid=%d\n", (int) pid);
  return 0;
}


int
engine_request_shutdown (int shutdown_fd)
{
  // Closing the parent's write-end causes EOF in the child -> graceful exit
  if (shutdown_fd >= 0)
    {
      close (shutdown_fd);
    }
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
        {
          return 0;             // reaped
        }
      if (r < 0)
        {
          return -1;            // error
        }
      if (timeout_ms >= 0 && elapsed >= timeout_ms)
        {
          return 1;             // still running
        }
      usleep (step_ms * 1000);
      elapsed += step_ms;
    }
}


/* configurable knobs */
static int
engine_notice_ttl_sweep (db_t *db, int64_t now_ms)
{
  int64_t now_s = now_ms / 1000;
  /* delete ephemerals whose ttl expired; bounded batch */
  const char *sql =
    "DELETE FROM system_notice "
    "WHERE expires_at IS NOT NULL AND expires_at <= $1 "
    // LIMIT is not standard in DELETE in all SQLs (e.g. Postgres DELETE doesn't support LIMIT directly without CTID tricks)
    // But standard Postgres driver might support it or we ignore it?
    // SQLite supports DELETE ... LIMIT if compiled with it.
    // To be safe and generic, maybe just DELETE all expired?
    // Or use a subquery for ID?
    // "DELETE FROM system_notice WHERE id IN (SELECT id FROM system_notice WHERE ... LIMIT 500)"
    // This is safer for Postgres.
    "AND id IN (SELECT id FROM system_notice WHERE expires_at IS NOT NULL AND expires_at <= $1 LIMIT 500);";

  // Wait, binding $1 twice?
  // db_api supports reusing bind params if the backend does.
  // SQLite supports ?1 used multiple times. Postgres supports $1 used multiple times.
  // So "AND expires_at <= $1 ... WHERE ... expires_at <= $1" works.

  // Actually, simplest is just "DELETE ... WHERE ... <= $1".
  // If we want batching, we need LIMIT.
  // Using subquery with LIMIT is the standard cross-DB way for batch delete.

  db_error_t err;
  db_error_clear (&err);

  db_bind_t params[1] = { db_bind_i64 (now_s) };


  // Note: if id is not primary key or unique, this might be slow, but it is standard.
  // system_notice usually has id PK.

  if (!db_exec (db, sql, params, 1, &err))
    {
      return 1;
    }
  return 0;
}


static int
sweeper_engine_deadletter_retry (db_t *db, int64_t now_ms)
{
  int64_t now_s = now_ms / 1000;
  int retried_count = 0;
  int final_rc = 0;
  // Select error commands ready for retry
  const char *sql_select_deadletters =
    "SELECT id, attempts FROM engine_commands WHERE status='error' AND attempts < $1 LIMIT 500;";

  db_res_t *res = NULL;
  db_error_t err;
  db_error_clear (&err);

  db_bind_t params[1] = { db_bind_i32 (MAX_RETRIES) };


  if (!db_query (db, sql_select_deadletters, params, 1, &res, &err))
    {
      LOGE
      (
        "sweeper_engine_deadletter_retry: Failed to select: %s",
        err.message);
      return -1;
    }

  while (db_res_step (res, &err))
    {
      int64_t cmd_id = db_res_col_i64 (res, 0, &err);

      const char *sql_update_deadletter =
        "UPDATE engine_commands SET status='ready', due_at=$1 + (attempts * 60) WHERE id=$2;";

      db_bind_t up_params[2];


      up_params[0] = db_bind_i64 (now_s);
      up_params[1] = db_bind_i64 (cmd_id);

      if (!db_exec (db, sql_update_deadletter, up_params, 2, &err))
        {
          LOGE
            ("sweeper_engine_deadletter_retry: Failed to update command %ld: %s",
            cmd_id,
            err.message);
          final_rc = -1;
          break;
        }
      retried_count++;
    }

  db_res_finalize (res);

  if (retried_count > 0)
    {
      LOGI
        ("sweeper_engine_deadletter_retry: Retried %d deadletter commands.",
        retried_count);
    }

  return final_rc;
}


// Cron handler to clean up expired Limpet mines
int
cron_limpet_ttl_cleanup (db_t *db, int64_t now_s)
{
  if (!g_cfg.mines.limpet.enabled)
    {
      return 0;                 // Limpet mines disabled, no cleanup needed
    }
  if (g_cfg.mines.limpet.limpet_ttl_days <= 0)
    {
      LOGW
        ("limpet_ttl_days is not set or zero. Skipping Limpet TTL cleanup.");
      return 0;                 // No TTL is set, so no cleanup needed
    }
  if (!try_lock (db, "limpet_ttl_cleanup", now_s))
    {
      return 0;
    }
  LOGD ("limpet_ttl_cleanup: Starting Limpet mine TTL cleanup.");

  long long expiry_threshold_s =
    now_s - ((long long) g_cfg.mines.limpet.limpet_ttl_days * 24 * 3600);
  const char *sql_delete_expired =
    "DELETE FROM sector_assets "
    "WHERE asset_type = $1 AND deployed_at <= $2;";


  db_error_t err;


  db_error_clear (&err);

  db_bind_t params[2];


  params[0] = db_bind_i32 (ASSET_LIMPET_MINE);
  params[1] = db_bind_i64 (expiry_threshold_s);

  if (!db_exec (db, sql_delete_expired, params, 2, &err))
    {
      LOGE ("limpet_ttl_cleanup: Failed to delete: %s", err.message);
      return -1;
    }

  LOGI ("limpet_ttl_cleanup: Expired Limpet mines cleanup completed.");
  return 0;
}


int
h_daily_bank_interest_tick (db_t *db, int64_t now_s)
{
  if (!try_lock (db, "daily_bank_interest_tick", now_s))
    {
      return 0;
    }
  LOGI ("daily_bank_interest_tick: Starting daily bank interest accrual.");

  const char *sql_select_accounts =
    "SELECT id, owner_type, owner_id, balance, interest_rate_bp, last_interest_tick "
    "FROM bank_accounts WHERE is_active = 1 AND interest_rate_bp > 0;";

  db_res_t *res = NULL;
  db_error_t err;


  db_error_clear (&err);

  if (!db_query (db, sql_select_accounts, NULL, 0, &res, &err))
    {
      LOGE
      (
        "daily_bank_interest_tick: Failed to select accounts: %s",
        err.message);
      unlock (db, "daily_bank_interest_tick");
      return -1;
    }

  int current_epoch_day = get_utc_epoch_day (now_s);
  int processed_accounts = 0;
  long long min_balance_for_interest =
    h_get_config_int_unlocked (db, "bank_min_balance_for_interest", 0);
  long long max_daily_per_account =
    h_get_config_int_unlocked (db, "bank_max_daily_interest_per_account",
                               9223372036854775807LL);


  while (db_res_step (res, &err))
    {
      int account_id = (int) db_res_col_i32 (res, 0, &err);
      const char *owner_type = db_res_col_text (res, 1, &err);
      int owner_id = (int) db_res_col_i32 (res, 2, &err);
      long long balance = db_res_col_i64 (res, 3, &err);
      int interest_rate_bp = (int) db_res_col_i32 (res, 4, &err);
      int last_interest_tick = (int) db_res_col_i32 (res, 5, &err);


      if (balance < min_balance_for_interest)
        {
          continue;
        }
      int days_to_accrue = current_epoch_day - last_interest_tick;


      if (days_to_accrue <= 0)
        {
          continue;
        }
      if (days_to_accrue > MAX_BACKLOG_DAYS)
        {
          days_to_accrue = MAX_BACKLOG_DAYS;
        }
      char tx_group_id[33];


      h_generate_hex_uuid (tx_group_id, sizeof (tx_group_id));
      for (int i = 0; i < days_to_accrue; ++i)
        {
          if (balance <= 0)
            {
              break;
            }
          long long daily_interest =
            (balance * interest_rate_bp) / (10000 * 365);


          if (daily_interest > max_daily_per_account)
            {
              daily_interest = max_daily_per_account;
            }
          if (daily_interest > 0)
            {
              int add_rc =
                h_add_credits_unlocked (db, account_id, daily_interest,
                                        "INTEREST", tx_group_id, &balance);


              if (add_rc != 0)
                {
                  LOGE
                  (
                    "daily_bank_interest_tick: Failed to add interest to account %d (owner %s:%d)",
                    account_id,
                    owner_type ? owner_type : "?",
                    owner_id);
                  db_res_finalize (res);
                  unlock (db, "daily_bank_interest_tick");
                  return -1;
                }
            }
        }
      const char *sql_update_tick =
        "UPDATE bank_accounts SET last_interest_tick = $1 WHERE id = $2;";

      db_bind_t up_params[2];


      up_params[0] = db_bind_i32 (current_epoch_day);
      up_params[1] = db_bind_i32 (account_id);

      if (!db_exec (db, sql_update_tick, up_params, 2, &err))
        {
          LOGE
          (
            "daily_bank_interest_tick: Failed to update last_interest_tick for account %d: %s",
            account_id,
            err.message);
          db_res_finalize (res);
          unlock (db, "daily_bank_interest_tick");
          return -1;
        }
      processed_accounts++;
    }

  db_res_finalize (res);

  LOGI
  (
    "daily_bank_interest_tick: Successfully processed interest for %d accounts.",
    processed_accounts);
  unlock (db, "daily_bank_interest_tick");
  return 0;
}

