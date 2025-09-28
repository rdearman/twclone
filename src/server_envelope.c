#include <jansson.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <jansson.h>
#include <time.h>
#include <string.h>
#include "server_envelope.h"
#include "schemas.h"


static int
send_all (int fd, const char *buf, size_t len)
{
  size_t off = 0;
  while (off < len)
    {
      ssize_t n = write (fd, buf + off, len - off);
      if (n < 0)
	{
	  if (errno == EINTR)
	    continue;
	  if (errno == EAGAIN || errno == EWOULDBLOCK)
	    {
	      // caller can decide whether to retry later; treat as partial send success
	      return -1;
	    }
	  return -1;
	}
      off += (size_t) n;
    }
  return 0;
}


/* Simple process-local counter for message ids. Thread-safe enough via GCC builtin. */
const char *
next_msg_id (void)
{
  static __thread char buf[32];
  static unsigned long long seq = 0;
#if defined(__GNUC__) || defined(__clang__)
  unsigned long long n = __sync_add_and_fetch (&seq, 1ULL);
#else
  // fallback (not thread-safe)
  unsigned long long n = ++seq;
#endif
  // "srv-<number>"
  snprintf (buf, sizeof (buf), "srv-%llu", n);
  return buf;
}

/* /\* server-generated message id "srv-<seq>" *\/ */
/* void */
/* next_msg_id (char out[32]) */
/* { */
/*   uint64_t n = atomic_fetch_add (&g_msg_seq, 1) + 1; */
/*   snprintf (out, 32, "srv-%" PRIu64, n); */
/* } */

/* RFC3339 UTC "YYYY-MM-DDThh:mm:ssZ" (seconds precision) */
void
iso8601_utc (char out[32])
{
  time_t now = time (NULL);
  struct tm tm;
  gmtime_r (&now, &tm);
  strftime (out, 32, "%Y-%m-%dT%H:%M:%SZ", &tm);
}


/* Build base envelope: id, ts, optional reply_to */
/*  json_t * */
/* make_base_envelope (json_t *req /\* may be NULL *\/ ) */
/* { */
/*   char ts[32], mid[32]; */
/*   iso8601_utc (ts); */
/*   next_msg_id (mid); */

/*   json_t *env = json_object (); */
/*   json_object_set_new (env, "id", json_string (mid)); */
/*   json_object_set_new (env, "ts", json_string (ts)); */

/*   if (req && json_is_object (req)) */
/*     { */
/*       json_t *rid = json_object_get (req, "id"); */
/*       if (rid && json_is_string (rid)) */
/* 	{ */
/* 	  json_object_set (env, "reply_to", rid);	/\* borrow *\/ */
/* 	} */
/*     } */
/*   return env;			/\* caller owns *\/ */
/* } */

json_t *
make_base_envelope (json_t *req, const char *type)
{
  char ts[32];
  iso8601_utc (ts);

  json_t *meta = json_object ();
  json_object_set_new (meta, "id", json_string (next_msg_id ()));
  json_object_set_new (meta, "ts", json_string (ts));

  json_t *env = json_object ();
  json_object_set_new (env, "type", json_string (type));
  json_object_set_new (env, "meta", meta);

  // If request has meta.id, echo it as in_reply_to (best-effort)
  const char *in_reply_to = NULL;
  if (req && json_is_object (req))
    {
      json_t *m = json_object_get (req, "meta");
      if (json_is_object (m))
	{
	  json_t *rid = json_object_get (m, "id");
	  if (json_is_string (rid))
	    in_reply_to = json_string_value (rid);
	}
    }
  if (in_reply_to)
    json_object_set_new (env, "in_reply_to", json_string (in_reply_to));

  return env;
}


/* /\* Send {"status":"error","type":"error","error":{code,message},...} (+data=null) *\/ */
/*  void */
/* send_enveloped_refused (int fd, json_t *root, int code, const char *msg, */
/* 			json_t *data_opt) */
/* { */
/*   json_t *env = json_object (); */
/*   json_object_set_new (env, "id", json_string ("srv-refuse")); */
/*   json_object_set (env, "reply_to", json_object_get (root, "id")); */
/*   char ts[32]; */
/*   iso8601_utc (ts); */
/*   json_object_set_new (env, "ts", json_string (ts)); */
/*   json_object_set_new (env, "status", json_string ("refused")); */
/*   json_object_set_new (env, "type", json_string ("error")); */
/*   json_t *err = */
/*     json_pack ("{s:i, s:s}", "code", code, "message", msg ? msg : ""); */
/*   json_object_set_new (env, "error", err); */
/*   json_object_set (env, "data", data_opt ? data_opt : json_null ()); */

/*   attach_rate_limit_meta (env, g_ctx_for_send); */
/*   rl_tick (g_ctx_for_send); */

/*   send_all_json (fd, env); */
/*   json_decref (env); */
/* } */

/* void */
/* send_enveloped_ok (int fd, json_t *req, const char *type, json_t *data) */
/* { */
/*   json_t *resp = json_object(); */

/*   // id/correlation */
/*   json_object_set_new(resp, "id", json_string("srv-ok")); */
/*   const char *req_id = NULL; */
/*   if (req && json_is_object(req)) { */
/*     json_t *jid = json_object_get(req, "id"); */
/*     if (json_is_string(jid)) req_id = json_string_value(jid); */
/*   } */
/*   if (req_id) json_object_set_new(resp, "reply_to", json_string(req_id)); */

/*   // timestamp */
/*   char tsbuf[32]; iso8601_utc(tsbuf); */
/*   json_object_set_new(resp, "ts", json_string(tsbuf)); */

/*   // status/type */
/*   json_object_set_new(resp, "status", json_string("ok")); */
/*   json_object_set_new(resp, "type", json_string(type ? type : "ok")); */

/*   // data (attach payload; steals ref if _set_new) */
/*   if (data) json_object_set_new(resp, "data", data); */
/*   else      json_object_set_new(resp, "data", json_null()); */

/*   // error is null on ok */
/*   json_object_set_new(resp, "error", json_null()); */

/*   // meta.rate_limit (inline default so we don't need ctx here) */
/*   json_t *rate = json_object(); */
/*   json_object_set_new(rate, "limit",     json_integer(60)); */
/*   json_object_set_new(rate, "remaining", json_integer(60)); */
/*   json_object_set_new(rate, "reset",     json_integer(60)); // seconds */

/*   json_t *meta = json_object(); */
/*   json_object_set_new(meta, "rate_limit", rate); */
/*   json_object_set_new(resp, "meta", meta); */

/*   // write one line to socket */
/*   char *s = json_dumps(resp, JSON_COMPACT); */
/*   if (s) { */
/*     size_t len = strlen(s); */
/*     if (len) (void)write(fd, s, len); */
/*     (void)write(fd, "\n", 1); */
/*     free(s); */
/*   } */
/*   json_decref(resp); */
/* } */

/* void */
/* send_enveloped_error (int fd, json_t *req, int code, const char *message) */
/* { */
/*   json_t *resp = json_object(); */

/*   json_object_set_new(resp, "id", json_string("srv-err")); */
/*   const char *req_id = NULL; */
/*   if (req && json_is_object(req)) { */
/*     json_t *jid = json_object_get(req, "id"); */
/*     if (json_is_string(jid)) req_id = json_string_value(jid); */
/*   } */
/*   if (req_id) json_object_set_new(resp, "reply_to", json_string(req_id)); */

/*   char tsbuf[32]; iso8601_utc(tsbuf); */
/*   json_object_set_new(resp, "ts", json_string(tsbuf)); */
/*   json_object_set_new(resp, "status", json_string("error")); */
/*   json_object_set_new(resp, "type", json_string("error")); */

/*   json_t *err = json_object(); */
/*   json_object_set_new(err, "code", json_integer(code)); */
/*   json_object_set_new(err, "message", json_string(message ? message : "Error")); */
/*   json_object_set_new(resp, "error", err); */

/*   json_object_set_new(resp, "data", json_null()); */

/*   // meta.rate_limit default */
/*   json_t *rate = json_object(); */
/*   json_object_set_new(rate, "limit",     json_integer(60)); */
/*   json_object_set_new(rate, "remaining", json_integer(60)); */
/*   json_object_set_new(rate, "reset",     json_integer(60)); */

/*   json_t *meta = json_object(); */
/*   json_object_set_new(meta, "rate_limit", rate); */
/*   json_object_set_new(resp, "meta", meta); */

/*   char *s = json_dumps(resp, JSON_COMPACT); */
/*   if (s) { */
/*     size_t len = strlen(s); */
/*     if (len) (void)write(fd, s, len); */
/*     (void)write(fd, "\n", 1); */
/*     free(s); */
/*   } */
/*   json_decref(resp); */
/* } */



void
send_enveloped_refused (int fd, json_t *req, int code, const char *msg,
			json_t *data_opt)
{
  json_t *err =
    json_pack ("{s:i s:s}", "code", code, "message", msg ? msg : "");
  if (data_opt)
    json_object_set (err, "data", data_opt);	// borrow

  json_t *env = make_base_envelope (req, "refused");
  json_object_set_new (env, "error", err);

  char *s = json_dumps (env, JSON_COMPACT);
  size_t len = s ? strlen (s) : 0;
  if (len)
    (void) send_all (fd, s, len);
  if (s)
    free (s);
  json_decref (env);
}

void
send_enveloped_error (int fd, json_t *req, int code, const char *msg)
{
  json_t *err =
    json_pack ("{s:i s:s}", "code", code, "message", msg ? msg : "");
  json_t *env = make_base_envelope (req, "error");
  json_object_set_new (env, "error", err);

  char *s = json_dumps (env, JSON_COMPACT);
  size_t len = s ? strlen (s) : 0;
  if (len)
    (void) send_all (fd, s, len);
  if (s)
    free (s);
  json_decref (env);
}

void
send_enveloped_ok (int fd, json_t *req, const char *type, json_t *data)
{
  json_t *env = make_base_envelope (req, type ? type : "ok");
  if (data)
    json_object_set_new (env, "data", data);	// takes ownership

  char *s = json_dumps (env, JSON_COMPACT);
  size_t len = s ? strlen (s) : 0;
  if (len)
    (void) send_all (fd, s, len);
  if (s)
    free (s);
  json_decref (env);
}

void
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

void
send_error_json (int fd, int code, const char *msg)
{
  json_t *o = json_object ();
  json_object_set_new (o, "status", json_string ("error"));
  json_object_set_new (o, "code", json_integer (code));
  json_object_set_new (o, "error", json_string (msg));
  send_all_json (fd, o);
  json_decref (o);
}

void
send_ok_json (int fd, json_t *data /* may be NULL */ )
{
  json_t *o = json_object ();
  json_object_set_new (o, "status", json_string ("OK"));
  if (data)
    json_object_set (o, "data", data);	/* borrowed */
  send_all_json (fd, o);
  json_decref (o);
}
