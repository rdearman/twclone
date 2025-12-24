#include "sysop_interaction.h"
#include "server_log.h"
#include <pthread.h>
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
  (
    "{\"id\":\"srv-%d\",\"reply_to\":null,\"ts\":\"%s\",\"status\":\"ok\",\"type\":\"%s\",\"data\":%s,\"error\":null}\n",
    next_id (),
    ts,
    type,
    fmt_data_json ? fmt_data_json : "{}");
  fflush (stdout);
}


static void
reply_refused (int code, const char *msg)
{
  char ts[32];
  ts_utc (ts, sizeof ts);
  printf
  (
    "{\"id\":\"srv-%d\",\"reply_to\":null,\"ts\":\"%s\",\"status\":\"refused\",\"type\":null,"
    "\"data\":{},\"error\":{\"code\":%d,\"message\":\"%s\"}}\n",
    next_id (),
    ts,
    code,
    msg ? msg : "Refused");
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
  (void) q;
  reply_ok ("sysop.players_v1",
            "{\"items\":[],\"page\":1,\"page_size\":20,\"total\":0}");
}


/* sysop.universe.summary -> sysop.universe.summary_v1 (stub) */
static void
h_universe_summary (void)
{
  reply_ok ("sysop.universe.summary_v1",
            "{\"world\":{\"sectors\":0,\"warps\":0,\"ports\":0,\"planets\":0,\"players\":0,\"ships\":0},"
            "\"stardock\":null,\"hotspots\":{}}");
}


/* sysop.engine_status.get -> sysop.engine_status_v1 (stub) */
static void
h_engine_status (void)
{
  reply_ok ("sysop.engine_status_v1",
            "{\"link\":{\"status\":\"up\",\"last_hello\":null},"
            "\"counters\":{\"sent\":0,\"recv\":0,\"auth_fail\":0,\"too_big\":0}}");
}


/* sysop.logs.tail -> sysop.audit_tail_v1 (stub; later: tail your logfile) */
static void
h_logs_tail (void)
{
  reply_ok ("sysop.audit_tail_v1", "{\"items\":[],\"last_id\":0}");
}


/* help text (human-friendly) */
static void
h_help (void)
{
  puts ("Commands:\n"
        "  dashboard               -> sysop.dashboard.get\n"
        "  players search <q>      -> sysop.players.search\n"
        "  universe summary        -> sysop.universe.summary\n"
        "  engine status           -> sysop.engine_status.get\n"
        "  logs tail               -> sysop.logs.tail\n"
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


/* ================== parser & REPL ================== */
static pthread_t g_thr;
static int g_run = 0;


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
  if (!strcmp (line, "?") || !strcmp (line, "help"))
    {
      h_help ();
      return;
    }
  if (!strcmp (line, "q") || !strcmp (line, ":q") || !strcmp (line, "quit"))
    {
      g_run = 0;
      return;
    }
  /* Palette verbs */
  if (!strncmp (line, "players search", 14))
    {
      char *s = line + 14;


      while (*s && isspace ((unsigned char) *s))
        {
          s++;
        }
      h_players_search (*s ? s : NULL);
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
  size_t cap = 0;


  while (g_run)
    {
      fputs ("sysop +> ", stdout);
      fflush (stdout);
      ssize_t n = getline (&line, &cap, stdin);


      if (n < 0)
        {
          break;
        }
      sysop_dispatch_line (line);
    }
  free (line);
  return NULL;
}


void
sysop_start (void)
{
  g_run = 1;
  pthread_create (&g_thr, NULL, repl, NULL);
  pthread_detach (g_thr);
}


void
sysop_stop (void)
{
  g_run = 0;
}

