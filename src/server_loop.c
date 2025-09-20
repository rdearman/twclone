#include <stdatomic.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jansson.h>		/* -ljansson */
#include "server_cmds.h"
#include <sqlite3.h>
#include "database.h"

#define LISTEN_PORT 1234
#define BUF_SIZE    8192

/* forward declaration to avoid implicit extern */
static void send_all_json (int fd, json_t * obj);
int db_player_info_json(int player_id, json_t **out);

typedef struct
{
  int fd;
  volatile sig_atomic_t *running;
  struct sockaddr_in peer;
  uint64_t cid;			/* connection id assigned on accept */
    int player_id;   /* 0 = not authenticated */
    int sector_id;   /* current sector; default to 1 until loaded */
} client_ctx_t;

static _Atomic uint64_t g_conn_seq = 0;
/* global (file-scope) counter for server message ids */
static _Atomic uint64_t g_msg_seq = 0;


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

/* ------------------------ JSON reply helpers ------------------------ */

/* RFC3339 UTC "YYYY-MM-DDThh:mm:ssZ" (seconds precision) */
static void
iso8601_utc (char out[32])
{
  time_t now = time (NULL);
  struct tm tm;
  gmtime_r (&now, &tm);
  strftime (out, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

/* server-generated message id "srv-<seq>" */
static void
next_msg_id (char out[32])
{
  uint64_t n = atomic_fetch_add (&g_msg_seq, 1) + 1;
  snprintf (out, 32, "srv-%" PRIu64, n);
}

/* Build base envelope: id, ts, optional reply_to */
static json_t *
make_base_envelope (json_t *req /* may be NULL */ )
{
  char ts[32], mid[32];
  iso8601_utc (ts);
  next_msg_id (mid);

  json_t *env = json_object ();
  json_object_set_new (env, "id", json_string (mid));
  json_object_set_new (env, "ts", json_string (ts));

  if (req && json_is_object (req))
    {
      json_t *rid = json_object_get (req, "id");
      if (rid && json_is_string (rid))
	{
	  json_object_set (env, "reply_to", rid);	/* borrow */
	}
    }
  return env;			/* caller owns */
}

/* Send {"status":"error","type":"error","error":{code,message},...} (+data=null) */
static void
send_enveloped_error (int fd, json_t *req, int code, const char *message)
{
  json_t *env = make_base_envelope (req);
  json_object_set_new (env, "status", json_string ("error"));
  json_object_set_new (env, "type", json_string ("error"));

  json_t *err = json_object ();
  json_object_set_new (err, "code", json_integer (code));
  json_object_set_new (err, "message", json_string (message));
  json_object_set_new (env, "error", err);	/* env owns err */
  json_object_set_new (env, "data", json_null ());

  send_all_json (fd, env);
  json_decref (env);
}

/* Send {"status":"ok","type":<type>,"data":<data>,"error":null} */
static void
send_enveloped_ok (int fd, json_t *req, const char *type,
		   json_t *data /* may be NULL */ )
{
  json_t *env = make_base_envelope (req);
  json_object_set_new (env, "status", json_string ("ok"));
  json_object_set_new (env, "type", json_string (type ? type : "ok"));
  if (data)
    json_object_set (env, "data", data);
  else
    json_object_set_new (env, "data", json_object ());
  json_object_set_new (env, "error", json_null ());

  send_all_json (fd, env);
  json_decref (env);
}

static void
send_all_json (int fd, json_t *obj)
{
  char *s = json_dumps (obj, JSON_COMPACT);
  if (s)
    {
      (void) send_all (fd, s, strlen (s));
      (void) send_all (fd, "\n", 1);
      free (s);
    }
}

static void
send_error_json (int fd, int code, const char *msg)
{
  json_t *o = json_object ();
  json_object_set_new (o, "status", json_string ("error"));
  json_object_set_new (o, "code", json_integer (code));
  json_object_set_new (o, "error", json_string (msg));
  send_all_json (fd, o);
  json_decref (o);
}

static void
send_ok_json (int fd, json_t *data /* may be NULL */ )
{
  json_t *o = json_object ();
  json_object_set_new (o, "status", json_string ("OK"));
  if (data)
    json_object_set (o, "data", data);	/* borrowed */
  send_all_json (fd, o);
  json_decref (o);
}

/* ------------------------ message processor (stub dispatcher) ------------------------ */
/* Single responsibility: receive one parsed JSON object and produce one reply. */

static void
process_message (client_ctx_t *ctx, json_t *root)
{
  json_t *cmd = json_object_get (root, "command");
  json_t *evt = json_object_get (root, "event");

  if (ctx->player_id == 0 && ctx->sector_id <= 0) {
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

  const char *c = json_string_value (cmd);

  if (strcmp (c, "login") == 0 || strcmp (c, "auth.login") == 0)
    {
      /* NEW: pull fields from the "data" object, not the root */
      json_t *jdata = json_object_get (root, "data");
      const char *name = NULL, *pass = NULL;

      if (json_is_object (jdata))
	{
	  /* Accept both "user_name" (new) and "player_name" (legacy) */
	  json_t *jname = json_object_get (jdata, "user_name");
	  if (!json_is_string (jname))
	    jname = json_object_get (jdata, "player_name");
	  json_t *jpass = json_object_get (jdata, "password");

	  name = json_is_string (jname) ? json_string_value (jname) : NULL;
	  pass = json_is_string (jpass) ? json_string_value (jpass) : NULL;
	}

      if (!name || !pass)
	{
	  send_enveloped_error (ctx->fd, root, AUTH_ERR_BAD_REQUEST,
				"Missing required field");
	}
      else
	{
	  int player_id = 0;
	  int rc = play_login (name, pass, &player_id);
	  if (rc == AUTH_OK)
	    {
	      /* Optional: mark session on ctx */
	      /* ctx->player_id = player_id; ctx->is_logged_in = 1; */

	      json_t *data = json_pack ("{s:i}", "player_id", player_id);
	      /* Mark connection as authenticated */
	      ctx->player_id = player_id;
	      /* Safe default until you load from DB */
	      if (ctx->sector_id <= 0) ctx->sector_id = 1;
	      send_enveloped_ok (ctx->fd, root, "auth.session", data);
	      json_decref (data);
	    }
	  else if (rc == AUTH_ERR_INVALID_CRED)
	    {
	      send_enveloped_error (ctx->fd, root, AUTH_ERR_INVALID_CRED,
				    "Invalid credentials");
	    }
	  else
	    {
	      send_enveloped_error (ctx->fd, root, AUTH_ERR_DB,
				    "Database error");
	    }
	}
    }
  else if (strcmp (c, "user.create") == 0 || strcmp (c, "new.user") == 0)
    {
      json_t *jdata = json_object_get (root, "data");
      const char *name = NULL, *pass = NULL;
      if (json_is_object (jdata))
	{
	  json_t *jname = json_object_get (jdata, "user_name");
	  if (!json_is_string (jname))
	    jname = json_object_get (jdata, "player_name");
	  json_t *jpass = json_object_get (jdata, "password");
	  name = json_is_string (jname) ? json_string_value (jname) : NULL;
	  pass = json_is_string (jpass) ? json_string_value (jpass) : NULL;
	}

      if (!name || !pass)
	{
	  send_enveloped_error (ctx->fd, root, AUTH_ERR_BAD_REQUEST,
				"Missing required field");
	}
      else
	{
	  int player_id = 0;
	  int rc = user_create (name, pass, &player_id);
	  if (rc == AUTH_OK)
	    {
	      json_t *data = json_pack ("{s:i}", "player_id", player_id);
	      send_enveloped_ok (ctx->fd, root, "user.created", data);
	      json_decref (data);
	    }
	  else if (rc == AUTH_ERR_NAME_TAKEN)
	    {
	      send_enveloped_error (ctx->fd, root, AUTH_ERR_NAME_TAKEN,
				    "Username already exists");
	    }
	  else
	    {
	      send_enveloped_error (ctx->fd, root, AUTH_ERR_DB,
				    "Database error");
	    }
	}
    }
  else if (strcmp (c, "player.my_info") == 0)
    {
      if (ctx->player_id <= 0)
	{
	  send_enveloped_error (ctx->fd, root, 1401, "Not authenticated");
	}
      else
	{
	  json_t *info = NULL;
	  int rc = db_player_info_json (ctx->player_id, &info);
	  if (rc == SQLITE_OK && info)
	    {
	      send_enveloped_ok (ctx->fd, root, "player.info", info);
	      json_decref (info);
	    }
	  else
	    {
	      send_enveloped_error (ctx->fd, root, 1500, "Database error");
	    }
	}
    }

  else if (strcmp (c, "SHIPINFO") == 0)
    {
      json_t *sn = json_object_get (root, "ship_num");
      if (!json_is_integer (sn))
	{
	  send_enveloped_error (ctx->fd, root, 1301,
				"Missing required field");
	}
      else
	{
	  json_t *data = json_object ();
	  send_enveloped_ok (ctx->fd, root, "ship.info", data);
	  json_decref (data);
	}
    }
  else if (strcmp (c, "DESCRIPTION") == 0)
    {
      json_t *data = json_object ();
      send_enveloped_ok (ctx->fd, root, "system.description", data);
      json_decref (data);
    }
  else if (strcmp (c, "MYINFO") == 0)
    {
      json_t *data = json_object ();
      send_enveloped_ok (ctx->fd, root, "player.info", data);
      json_decref (data);
    }
  else if (strcmp (c, "ONLINE") == 0)
    {
      json_t *data = json_object ();
      send_enveloped_ok (ctx->fd, root, "player.list_online", data);
      json_decref (data);
    }
  else if (strcmp (c, "QUIT") == 0)
    {
      json_t *data = json_object ();
      send_enveloped_ok (ctx->fd, root, "system.goodbye", data);
      json_decref (data);
    }
  else
    {
      /* Unknown command -> 1101 Not implemented (per catalogue) */
      send_enveloped_error (ctx->fd, root, 1101, "Not implemented");
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
  fprintf (stderr, "Server loop starting...\n");

#ifdef SIGPIPE
  signal (SIGPIPE, SIG_IGN);	/* don’t die on write to closed socket */
#endif

  int listen_fd = make_listen_socket (LISTEN_PORT);
  if (listen_fd < 0)
    {
      fprintf (stderr, "Server loop exiting due to listen socket error.\n");
      return -1;
    }
  fprintf (stderr, "Listening on 0.0.0.0:%d\n", LISTEN_PORT);

  struct pollfd pfd = {.fd = listen_fd,.events = POLLIN,.revents = 0 };

  pthread_attr_t attr;
  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);

  while (*running)
    {
      int rc = poll (&pfd, 1, 1000);	/* 1s tick re-checks *running */
      if (rc < 0)
	{
	  if (errno == EINTR)
	    continue;
	  perror ("poll");
	  break;
	}
      if (rc == 0)
	continue;

      if (pfd.revents & POLLIN)
	{
	  client_ctx_t *ctx = calloc (1, sizeof (*ctx));
	  if (!ctx)
	    {
	      fprintf (stderr, "malloc failed\n");
	      continue;
	    }

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
	  fprintf (stderr, "Client connected: %s:%u (fd=%d)\n",
		   ip, (unsigned) ntohs (ctx->peer.sin_port), cfd);

	  // after filling ctx->fd, ctx->running, ctx->peer, and assigning ctx->cid
	  pthread_t th;
	  int prc = pthread_create (&th, &attr, connection_thread, ctx);
	  if (prc == 0)
	    {
	      fprintf (stderr,
		       "[cid=%" PRIu64 "] thread created (pthread=%lu)\n",
		       ctx->cid, (unsigned long) th);
	    }
	  else
	    {
	      fprintf (stderr, "pthread_create: %s\n", strerror (prc));
	      close (cfd);
	      free (ctx);
	    }

	}
    }

  pthread_attr_destroy (&attr);
  close (listen_fd);
  fprintf (stderr, "Server loop exiting...\n");
  return 0;
}
