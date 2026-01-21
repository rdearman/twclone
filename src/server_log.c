#include "server_log.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

/* Backend selector */
typedef enum
{ BACKEND_NONE = 0, BACKEND_FILE, BACKEND_SYSLOG } backend_t;
static backend_t g_backend = BACKEND_NONE;
// static int g_level_max = LOG_INFO;   /* LOG_DEBUG .. LOG_EMERG */
static int g_level_max = LOG_ERR;	/* LOG_DEBUG .. LOG_EMERG */
static char g_prefix[32] = "";
static int g_echo_stderr = 0;	/* 0/1 */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
/* FILE backend state */
static int g_fd = -1;		/* open file descriptor */
static char g_path[512] = "";	/* remembered for reopen() */


/* ---- helpers ---- */
static int
priority_allows (int pri)
{
  /* syslog numerics: higher number = more verbose; LOG_DEBUG=7 .. LOG_EMERG=0 */
  return pri <= g_level_max;
}


static int
write_all (int fd, const char *buf, size_t len)
{
  size_t off = 0;
  while (off < len)
    {
      ssize_t w = write (fd, buf + off, len - off);


      if (w > 0)
	{
	  off += (size_t) w;
	  continue;
	}
      if (w < 0 && errno == EINTR)
	{
	  continue;
	}
      return -1;
    }
  return 0;
}


static void
now_iso8601 (char *dst, size_t n)
{
  struct timespec ts;
  clock_gettime (CLOCK_REALTIME, &ts);
  struct tm tm;


  localtime_r (&ts.tv_sec, &tm);
  strftime (dst, n, "%Y-%m-%d %H:%M:%S", &tm);
}


/* ---- initialisers ---- */
void
server_log_set_level (int max_level)
{
  if (max_level < LOG_EMERG)
    {
      max_level = LOG_EMERG;
    }
  if (max_level > LOG_DEBUG)
    {
      max_level = LOG_DEBUG;
    }
  pthread_mutex_lock (&g_lock);
  g_level_max = max_level;
  pthread_mutex_unlock (&g_lock);
}


void
server_log_set_prefix (const char *prefix)
{
  pthread_mutex_lock (&g_lock);
  if (prefix && *prefix)
    {
      snprintf (g_prefix, sizeof g_prefix, "%s", prefix);
      g_prefix[sizeof (g_prefix) - 1] = 0;
    }
  else
    {
      g_prefix[0] = 0;
    }
  pthread_mutex_unlock (&g_lock);
}


/* FILE backend */
void
server_log_init_file (const char *filepath,
		      const char *prefix, int echo_stderr, int max_level)
{
  pthread_mutex_lock (&g_lock);
  if (g_fd != -1)
    {
      close (g_fd);
      g_fd = -1;
    }
  g_backend = BACKEND_FILE;
  g_echo_stderr = echo_stderr ? 1 : 0;
  g_level_max = (max_level < LOG_EMERG) ? LOG_EMERG :
    (max_level > LOG_DEBUG) ? LOG_DEBUG : max_level;
  if (prefix && *prefix)
    {
      snprintf (g_prefix, sizeof g_prefix, "%s", prefix);
      g_prefix[sizeof (g_prefix) - 1] = 0;
    }
  else
    {
      g_prefix[0] = 0;
    }
  if (filepath && *filepath)
    {
      snprintf (g_path, sizeof g_path, "%s", filepath);
      g_path[sizeof (g_path) - 1] = 0;
    }
  else
    {
      snprintf (g_path, sizeof g_path, "%s", "./twclone.log");
    }
  /* open with O_APPEND for atomic multi-process appends; create if missing */
  int fd = open (g_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);


  if (fd < 0)
    {
      /* fall back to stderr-only if the path is not writable */
      g_fd = -1;
      g_backend = BACKEND_NONE;	/* so we don't pretend we have a file */
    }
  else
    {
      g_fd = fd;
    }
  pthread_mutex_unlock (&g_lock);
}


/* Optional syslog backend (unchanged semantics if you keep it) */
#include <syslog.h>


void
server_log_init (const char *prog_tag,
		 const char *prefix,
		 int facility, int echo_stderr, int max_level)
{
  pthread_mutex_lock (&g_lock);
  g_backend = BACKEND_SYSLOG;
  g_echo_stderr = echo_stderr ? 1 : 0;
  g_level_max = (max_level < LOG_EMERG) ? LOG_EMERG :
    (max_level > LOG_DEBUG) ? LOG_DEBUG : max_level;
  if (prefix && *prefix)
    {
      snprintf (g_prefix, sizeof g_prefix, "%s", prefix);
      g_prefix[sizeof (g_prefix) - 1] = 0;
    }
  else
    {
      g_prefix[0] = 0;
    }
  int flags = LOG_PID | LOG_NDELAY;


  if (g_echo_stderr)
    {
      flags |= LOG_PERROR;
    }
  if (!prog_tag)
    {
      prog_tag = "twclone";
    }
  if (!facility)
    {
      facility = LOG_LOCAL0;
    }
  openlog (prog_tag, flags, facility);
  pthread_mutex_unlock (&g_lock);
}


void
server_log_reopen (void)
{
  pthread_mutex_lock (&g_lock);
  if (g_backend == BACKEND_FILE && g_path[0])
    {
      int fd = open (g_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);


      if (fd >= 0)
	{
	  if (g_fd != -1)
	    {
	      close (g_fd);
	    }
	  g_fd = fd;
	}
    }
  pthread_mutex_unlock (&g_lock);
}


void
server_log_close (void)
{
  pthread_mutex_lock (&g_lock);
  if (g_backend == BACKEND_FILE && g_fd != -1)
    {
      close (g_fd);
      g_fd = -1;
    }
  if (g_backend == BACKEND_SYSLOG)
    {
      closelog ();
    }
  g_backend = BACKEND_NONE;
  pthread_mutex_unlock (&g_lock);
}


const char *
server_log_get_path (void)
{
  return g_path;
}


/* ---- emitters ---- */
void
server_log_vprintf (int priority, const char *fmt, va_list ap)
{
  pthread_mutex_lock (&g_lock);
  const int allowed = priority_allows (priority);
  const int echo = g_echo_stderr;
  const backend_t be = g_backend;
  const int fd = g_fd;
  char prefix[64];


  prefix[0] = 0;
  if (g_prefix[0])
    {
      snprintf (prefix, sizeof prefix, "%s ", g_prefix);
    }
  pthread_mutex_unlock (&g_lock);
  if (!allowed)
    {
      return;
    }
  /* Build one line with timestamp + prefix + message */
  char when[32];


  now_iso8601 (when, sizeof when);
  char msgbuf[1400];


  vsnprintf (msgbuf, sizeof msgbuf, fmt ? fmt : "", ap);
  char line[1600];
  int n = snprintf (line, sizeof line, "%s %s%s",
		    when, prefix, msgbuf);


  if (n < 0)
    {
      return;
    }
  if (n >= (int) sizeof (line))
    {
      n = (int) sizeof (line) - 1;
    }
  /* Ensure newline for readability */
  if (n == 0 || line[n - 1] != '\n')
    {
      if (n < (int) sizeof (line) - 1)
	{
	  line[n++] = '\n';
	  line[n] = 0;
	}
    }
  if (be == BACKEND_FILE && fd >= 0)
    {
      (void) write_all (fd, line, (size_t) n);
    }
  else if (be == BACKEND_SYSLOG)
    {
      /* syslog already timestamps; keep prefix inside message */
      syslog (priority, "%s%s", prefix, msgbuf);
    }
  if (echo)
    {
      (void) write_all (STDERR_FILENO, line, (size_t) n);
    }
}


void
server_log_printf (int priority, const char *fmt, ...)
{
  va_list ap;
  va_start (ap, fmt);
  server_log_vprintf (priority, fmt, ap);
  va_end (ap);
}
