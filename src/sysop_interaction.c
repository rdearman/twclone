#include "sysop_interaction.h"
#include "server_log.h"
#include "server_sysop.h"
#include "server_communication.h"
#include "server_auth.h"
#include "game_db.h"
#include "errors.h"
#include <pthread.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>


/* ================== small helpers ================== */
static void
trim (char *s)
{
  size_t n = strlen (s);
  while (n
	 && (s[n - 1] == '\n' || s[n - 1] == '\r'
	     || isspace ((unsigned char) s[n - 1])))
    {
      s[--n] = 0;
    }
  size_t i = 0;


  while (s[i] && isspace ((unsigned char) s[i]))
    {
      i++;
    }
  if (i)
    {
      memmove (s, s + i, strlen (s + i) + 1);
    }
}


static void
ts_utc (char *buf, size_t n)
{
  time_t t = time (NULL);
  struct tm tm;
  gmtime_r (&t, &tm);
  strftime (buf, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}


static int
next_id (void)
{
  static int x = 1;
  return __sync_add_and_fetch (&x, 1);
}


/* ================== envelope output ==================
   Matches your SYSOP reply shape so this can later be exposed
   over TCP without changing handlers.                       */
static void
reply_ok (const char *type, const char *fmt_data_json)
{
  char ts[32];
  ts_utc (ts, sizeof ts);
  printf
    ("{\"id\":\"srv-%d\",\"reply_to\":null,\"ts\":\"%s\",\"status\":\"ok\",\"type\":\"%s\",\"data\":%s,\"error\":null}\n",
     next_id (), ts, type, fmt_data_json ? fmt_data_json : "{}");
  fflush (stdout);
}


static void
reply_refused (int code, const char *msg)
{
  char ts[32];
  ts_utc (ts, sizeof ts);
  printf
    ("{\"id\":\"srv-%d\",\"reply_to\":null,\"ts\":\"%s\",\"status\":\"refused\",\"type\":null,"
     "\"data\":{},\"error\":{\"code\":%d,\"message\":\"%s\"}}\n",
     next_id (), ts, code, msg ? msg : "Refused");
  fflush (stdout);
}


/*
   static void
   reply_error (const char *msg)
   {
   (void)msg;
   }
 */


/* ================== handlers (MVP) ================== */

typedef int (*sysop_handler_fn) (client_ctx_t * ctx, json_t * root);

static void
sysop_local_call (sysop_handler_fn handler, json_t * data)
{
  client_ctx_t ctx;
  memset (&ctx, 0, sizeof (ctx));
  ctx.player_id = 0;		/* System actor */
  ctx.fd = -1;

  json_t *root = json_object ();
  json_object_set_new (root, "command", json_string ("local.sysop"));
  if (data)
    {
      json_object_set_new (root, "data", data);
    }
  else
    {
      json_object_set_new (root, "data", json_object ());
    }

  /* Capture responses is tricky because handlers use send_response_ok_take.
     For local console, we'll let them print to stdout/stderr or we can
     improve send_response logic to detect fd=-1.
     Wait, send_response_ok_take calls send_all_json if fd >= 0.
     If fd < 0, it doesn't send but might leak or just do nothing.
     Let's look at server_envelope.c. */

  handler (&ctx, root);
  json_decref (root);
}

/* sysop.dashboard.get -> sysop.dashboard_v1 (stub; wire real counters later) */
static void
h_dashboard_get (void)
{
  reply_ok ("sysop.dashboard_v1",
	    "{\"server\":{\"version\":\"dev\",\"time\":\"\",\"uptime_s\":0},"
	    "\"links\":{\"engine\":{\"status\":\"up\",\"last_hello\":null,"
	    "\"counters\":{\"sent\":0,\"recv\":0,\"auth_fail\":0,\"too_big\":0}}},"
	    "\"rates\":{\"rpc_per_min\":0,\"refusals_per_min\":0,\"errors_per_min\":0},"
	    "\"notices\":[],\"audit_tail\":[]}");
}

/* sysop.players.search q=<term> -> sysop.players_v1 (stub) */
static void
h_players_search (const char *q)
{
  if (!q)
    {
      reply_refused (1301, "Missing query");
      return;
    }
  json_t *data = json_object ();
  json_object_set_new (data, "query", json_string (q));
  sysop_local_call (cmd_sysop_players_search, data);
}

/* sysop.universe.summary -> sysop.universe.summary_v1 (stub) */
static void
h_universe_summary (void)
{
  sysop_local_call (cmd_sysop_universe_summary, NULL);
}

/* sysop.engine_status.get -> sysop.engine_status_v1 (stub) */
static void
h_engine_status (void)
{
  sysop_local_call (cmd_sysop_engine_status_get, NULL);
}


/* sysop.logs.tail -> sysop.audit_tail_v1 (stub; later: tail your logfile) */
static void
h_logs_tail (void)
{
  sysop_local_call (cmd_sysop_logs_tail, NULL);
}

static void
h_logs_clear (void)
{
  sysop_local_call (cmd_sysop_logs_clear, NULL);
}

/* Phase 1: Config */
static void h_config_list(void) {
    sysop_local_call(cmd_sysop_config_list, NULL);
}

static void h_config_get(const char *key) {
    if (!key) { reply_refused(1301, "Missing key"); return; }
    json_t *data = json_object();
    json_object_set_new(data, "key", json_string(key));
    sysop_local_call(cmd_sysop_config_get, data);
}

static void h_config_set(const char *key, const char *val) {
    if (!key || !val) { reply_refused(1301, "Missing key/value"); return; }
    json_t *data = json_object();
    json_object_set_new(data, "key", json_string(key));
    json_object_set_new(data, "value", json_string(val));
    json_object_set_new(data, "confirm", json_true());
    sysop_local_call(cmd_sysop_config_set, data);
}

/* Phase 2: Player Ops */
static void h_player_get(int id) {
    json_t *data = json_object();
    json_object_set_new(data, "player_id", json_integer(id));
    sysop_local_call(cmd_sysop_player_get, data);
}

static void h_player_kick(int id, const char *reason) {
    json_t *data = json_object();
    json_object_set_new(data, "player_id", json_integer(id));
    json_object_set_new(data, "reason", json_string(reason ? reason : "SysOp kick"));
    sysop_local_call(cmd_sysop_player_kick, data);
}

static void h_player_sessions(int id) {
    json_t *data = json_object();
    json_object_set_new(data, "player_id", json_integer(id));
    sysop_local_call(cmd_sysop_player_sessions_get, data);
}

/* Phase 3: Jobs */
static void h_job_list(void) {
    sysop_local_call(cmd_sysop_jobs_list, NULL);
}

static void h_job_get(int id) {
    json_t *data = json_object();
    json_object_set_new(data, "job_id", json_integer(id));
    sysop_local_call(cmd_sysop_jobs_get, data);
}

static void h_job_cancel(int id) {
    json_t *data = json_object();
    json_object_set_new(data, "job_id", json_integer(id));
    sysop_local_call(cmd_sysop_jobs_cancel, data);
}

/* Phase 4: Messaging */
static void h_broadcast(const char *msg) {
    if (!msg) { reply_refused(1301, "Missing message"); return; }
    json_t *data = json_object();
    json_object_set_new(data, "message", json_string(msg));
    sysop_local_call(cmd_sysop_broadcast_send, data);
}

static void h_notice_create(const char *title, const char *body) {
    if (!title || !body) { reply_refused(1301, "Missing title/body"); return; }
    json_t *data = json_object();
    json_object_set_new(data, "title", json_string(title));
    json_object_set_new(data, "body", json_string(body));
    sysop_local_call(cmd_sysop_notice_create, data);
}

/* help text (human-friendly) */
static void
h_help (void)
{
  puts ("Commands:\n"
	"  dashboard               -> sysop.dashboard.get\n"
	"  config list             -> sysop.config.list\n"
	"  config get <key>        -> sysop.config.get\n"
	"  config set <key> <val>  -> sysop.config.set\n"
	"  players search <q>      -> sysop.players.search\n"
	"  player info <id>        -> sysop.player.get\n"
	"  player kick <id> [r]    -> sysop.player.kick\n"
	"  player sessions <id>    -> sysop.player.sessions.get\n"
	"  universe summary        -> sysop.universe.summary\n"
	"  engine status           -> sysop.engine_status.get\n"
	"  job list                -> sysop.jobs.list\n"
	"  job info <id>           -> sysop.jobs.get\n"
	"  job cancel <id>         -> sysop.jobs.cancel\n"
	"  broadcast <msg>         -> sysop.broadcast.send\n"
	"  notice <title> | <body> -> sysop.notice.create\n"
	"  logs tail               -> sysop.logs.tail\n"
	"  logs clear              -> sysop.logs.clear\n"
	"  level <ERR|INFO|DEBUG>\n"
	"  quit\n" "Shortcuts: g d/p/u/e/l, ?, :, /");
  fflush (stdout);
}


/* log level */
static void
h_level (const char *lvl)
{
  if (!lvl)
    {
      reply_refused (1403, "Missing level");
      return;
    }
  if (!strcasecmp (lvl, "ERR"))
    {
      server_log_set_level (LOG_ERR);
    }
  else if (!strcasecmp (lvl, "INFO"))
    {
      server_log_set_level (LOG_INFO);
    }
  else if (!strcasecmp (lvl, "DEBUG"))
    {
      server_log_set_level (LOG_DEBUG);
    }
  else
    {
      reply_refused (1403, "Bad level");
      return;
    }
  reply_ok ("sysop.ack_v1", "{\"ok\":true}");
}


#include <poll.h>
#include <errno.h>

/* ================== parser & REPL ================== */
static pthread_t g_thr;
static volatile int g_run = 0;
static volatile sig_atomic_t *g_running_ptr = NULL;


void
sysop_dispatch_line (char *line)
{
  trim (line);
  if (!*line)
    {
      return;
    }
  /* Navigation aliases */
  if (!strcmp (line, "g d") || !strcmp (line, "dashboard"))
    {
      h_dashboard_get ();
      return;
    }
  if (!strcmp (line, "g p"))
    {
      puts ("Tip: players search <q>");
      fflush (stdout);
      return;
    }
  if (!strcmp (line, "g u") || !strcmp (line, "universe summary"))
    {
      h_universe_summary ();
      return;
    }
  if (!strcmp (line, "g e") || !strcmp (line, "engine status"))
    {
      h_engine_status ();
      return;
    }
  if (!strcmp (line, "g l") || !strcmp (line, "logs tail"))
    {
      h_logs_tail ();
      return;
    }
  if (!strcmp (line, "logs clear"))
    {
      h_logs_clear ();
      return;
    }
  if (!strcmp (line, "?") || !strcmp (line, "help"))
    {
      h_help ();
      return;
    }
  if (!strcmp (line, "q") || !strcmp (line, ":q") || !strcmp (line, "quit"))
    {
      g_run = 0;
      if (g_running_ptr)
	{
	  *g_running_ptr = 0;
	}
      return;
    }
  /* Palette verbs */
  if (!strncmp (line, "config list", 11))
    {
      h_config_list ();
      return;
    }
  if (!strncmp (line, "config get", 10))
    {
      char *s = line + 10;
      while (*s && isspace (*s)) s++;
      h_config_get (*s ? s : NULL);
      return;
    }
  if (!strncmp (line, "config set", 10))
    {
      char *s = line + 10;
      while (*s && isspace (*s)) s++;
      char *key = s;
      while (*s && !isspace (*s)) s++;
      if (*s) { *s = 0; s++; }
      while (*s && isspace (*s)) s++;
      h_config_set (key, *s ? s : NULL);
      return;
    }
  if (!strncmp (line, "players search", 14) || !strncmp (line, "player search", 13))
    {
      char *s = strstr(line, "search") + 6;
      while (*s && isspace ((unsigned char) *s))
	{
	  s++;
	}
      h_players_search (*s ? s : NULL);
      return;
    }
  if (!strncmp (line, "player info", 11))
    {
      char *s = line + 11;
      while (*s && isspace (*s)) s++;
      if (isdigit(*s)) h_player_get (atoi (s));
      else reply_refused (1301, "Usage: player info <id>");
      return;
    }
  if (!strncmp (line, "player kick", 11))
    {
      char *s = line + 11;
      while (*s && isspace (*s)) s++;
      char *id_s = s;
      while (*s && !isspace (*s)) s++;
      if (*s) { *s = 0; s++; }
      while (*s && isspace (*s)) s++;
      if (isdigit(*id_s)) h_player_kick (atoi (id_s), *s ? s : NULL);
      else reply_refused (1301, "Usage: player kick <id> [reason]");
      return;
    }
  if (!strncmp (line, "player sessions", 15))
    {
      char *s = line + 15;
      while (*s && isspace (*s)) s++;
      if (isdigit(*s)) h_player_sessions (atoi (s));
      else reply_refused (1301, "Usage: player sessions <id>");
      return;
    }
  if (!strncmp (line, "job list", 8))
    {
      h_job_list ();
      return;
    }
  if (!strncmp (line, "job info", 8))
    {
      char *s = line + 8;
      while (*s && isspace (*s)) s++;
      if (isdigit(*s)) h_job_get (atoll (s));
      else reply_refused (1301, "Usage: job info <id>");
      return;
    }
  if (!strncmp (line, "job cancel", 10))
    {
      char *s = line + 10;
      while (*s && isspace (*s)) s++;
      if (isdigit(*s)) h_job_cancel (atoll (s));
      else reply_refused (1301, "Usage: job cancel <id>");
      return;
    }
  if (!strncmp (line, "broadcast", 9))
    {
      char *s = line + 9;
      while (*s && isspace (*s)) s++;
      h_broadcast (*s ? s : NULL);
      return;
    }
  if (!strncmp (line, "notice", 6))
    {
      char *s = line + 6;
      while (*s && isspace (*s)) s++;
      char *title = s;
      char *body = strchr (s, '|');
      if (body) { *body = 0; body++; while (*body && isspace (*body)) body++; }
      h_notice_create (title, body);
      return;
    }
  if (!strncmp (line, "level", 5))
    {
      char *s = line + 5;


      while (*s && isspace ((unsigned char) *s))
	{
	  s++;
	}
      h_level (*s ? s : NULL);
      return;
    }
  /* Spec command names directly */
  if (!strcmp (line, "sysop.dashboard.get"))
    {
      h_dashboard_get ();
      return;
    }
  if (!strcmp (line, "sysop.universe.summary"))
    {
      h_universe_summary ();
      return;
    }
  if (!strcmp (line, "sysop.engine_status.get"))
    {
      h_engine_status ();
      return;
    }
  if (!strcmp (line, "sysop.logs.tail"))
    {
      h_logs_tail ();
      return;
    }
  reply_refused (1403, "Unknown command");
}


static void *
repl (void *arg)
{
  (void) arg;
  char *line = NULL;

  while (g_run && (!g_running_ptr || *g_running_ptr))
    {
      line = readline ("sysop +> ");
      if (!line)
	{
	  /* EOF */
	  break;
	}

      if (*line)
	{
	  add_history (line);
	  sysop_dispatch_line (line);
	}
      free (line);
    }
  return NULL;
}


void
sysop_start (volatile sig_atomic_t *running_flag)
{
  g_run = 1;
  g_running_ptr = running_flag;
  pthread_create (&g_thr, NULL, repl, NULL);
}


void
sysop_stop (void)
{
  /* Always try to join if we ever started */
  g_run = 0;
  pthread_join (g_thr, NULL);
}
