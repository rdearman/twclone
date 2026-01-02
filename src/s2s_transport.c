#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <netinet/tcp.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <inttypes.h>
#include "s2s_transport.h"
#include "server_log.h"
#include "server_config.h"
#ifdef TCP_NODELAY


static int
set_nodelay (int fd)
{
  int y = 1;
  return setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &y, sizeof (y));
}


#else


static int
set_nodelay (int fd)
{
  (void) fd;
  return 0;
}


#endif
struct s2s_conn
{
  int fd;
  s2s_role_t role;
};


/* --- simple counters --- */
static struct
{
  uint64_t sent_ok, recv_ok, auth_fail, toolarge;
} g_ctr;


void
s2s_get_counters (uint64_t *a, uint64_t *b, uint64_t *c, uint64_t *d)
{
  if (a)
    {
      *a = g_ctr.sent_ok;
    }
  if (b)
    {
      *b = g_ctr.recv_ok;
    }
  if (c)
    {
      *c = g_ctr.auth_fail;
    }
  if (d)
    {
      *d = g_ctr.toolarge;
    }
}


/* --- keyring --- */
s2s_key_t g_keys[8];
size_t g_key_count = 0;
// s2s_transport.c
#include <arpa/inet.h>


void
s2s_debug_dump_conn (const char *who, s2s_conn_t *c)
{
  if (!c || c->fd < 0)
    {
      LOGI ("[%s] conn=NULL\n", who);
      //      // fprintf (stderr, "[%s] conn=NULL\n", who);
      return;
    }
  struct sockaddr_in la, ra;
  socklen_t sl = sizeof (la), sr = sizeof (ra);


  getsockname (c->fd, (struct sockaddr *) &la, &sl);
  getpeername (c->fd, (struct sockaddr *) &ra, &sr);
  char lip[32], rip[32];


  inet_ntop (AF_INET, &la.sin_addr, lip, sizeof (lip));
  inet_ntop (AF_INET, &ra.sin_addr, rip, sizeof (rip));
  LOGI ("[%s] fd=%d local=%s:%u peer=%s:%u\n", who, c->fd,
        lip, (unsigned) ntohs (la.sin_port), rip,
        (unsigned) ntohs (ra.sin_port));
  //  // fprintf (stderr, "[%s] fd=%d local=%s:%u peer=%s:%u\n", who, c->fd,
  //       lip, (unsigned) ntohs (la.sin_port), rip,
  //       (unsigned) ntohs (ra.sin_port));
}


void
s2s_set_keyring (const s2s_key_t *keys, size_t n)
{
  if (!keys || n == 0)
    {
      g_key_count = 0;
      return;
    }
  if (n > 8)
    {
      n = 8;
    }
  memcpy (g_keys, keys, n * sizeof (s2s_key_t));
  g_key_count = n;
}


static const s2s_key_t *
find_key (const char *key_id)
{
  for (size_t i = 0; i < g_key_count; i++)
    {
      if (strncmp (g_keys[i].key_id, key_id, sizeof (g_keys[i].key_id)) == 0)
        {
          return &g_keys[i];
        }
    }
  return NULL;
}


/* --- small utils --- */


// static int set_nodelay(int fd) { int y=1; return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &y, sizeof(y)); }
static int
set_cloexec (int fd)
{
  return fcntl (fd, F_SETFD, FD_CLOEXEC);
}


static int
poll_wait (int fd, short events, int timeout_ms)
{
  struct pollfd p = {.fd = fd,.events = events,.revents = 0 };
  for (;;)
    {
      int rc = poll (&p, 1, timeout_ms);


      if (rc == 0)
        {
          return S2S_E_TIMEOUT;
        }
      if (rc < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          return S2S_E_IO;
        }
      if (p.revents & (POLLERR | POLLHUP | POLLNVAL))
        {
          return S2S_E_CLOSED;
        }
      if (p.revents & events)
        {
          return S2S_OK;
        }
    }
}


static int
read_n (int fd, void *buf, size_t n, int timeout_ms)
{
  uint8_t *p = (uint8_t *) buf;
  size_t off = 0;
  while (off < n)
    {
      int rc = poll_wait (fd, POLLIN, timeout_ms);


      if (rc != S2S_OK)
        {
          return rc;
        }
      ssize_t k = recv (fd, p + off, n - off, 0);


      if (k == 0)
        {
          return S2S_E_CLOSED;
        }
      if (k < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          return S2S_E_IO;
        }
      off += (size_t) k;
    }
  return S2S_OK;
}


static int
write_n (int fd, const void *buf, size_t n, int timeout_ms)
{
  const uint8_t *p = (const uint8_t *) buf;
  size_t off = 0;
  while (off < n)
    {
      int rc = poll_wait (fd, POLLOUT, timeout_ms);


      if (rc != S2S_OK)
        {
          return rc;
        }
      ssize_t k = send (fd, p + off, n - off, 0);


      if (k <= 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          return S2S_E_IO;
        }
      off += (size_t) k;
    }
  return S2S_OK;
}


/* --- HMAC helpers (SHA-256, hex) --- */
static int
hmac_sha256_hex (const uint8_t *key, size_t keylen,
                 const uint8_t *msg, size_t msglen, char out_hex[65])
{
  unsigned int maclen = 0;
  unsigned char mac[EVP_MAX_MD_SIZE];
  if (!HMAC (EVP_sha256 (), key, (int) keylen, msg, msglen, mac, &maclen))
    {
      return -1;
    }
  static const char *hexd = "0123456789abcdef";


  for (unsigned i = 0; i < maclen; i++)
    {
      out_hex[2 * i] = hexd[mac[i] >> 4];
      out_hex[2 * i + 1] = hexd[mac[i] & 0xF];
    }
  out_hex[2 * maclen] = '\0';
  return (int) (2 * maclen);
}


/* --- Public API --- */
s2s_conn_t *
s2s_tcp_server_listen (const char *host, uint16_t port, int *out_listen_fd)
{
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    {
      return NULL;
    }
  int yes = 1;


  setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof (yes));
  struct sockaddr_in a = { 0 };


  a.sin_family = AF_INET;
  a.sin_port = htons (port);
  inet_pton (AF_INET, host, &a.sin_addr);
  if (bind (fd, (struct sockaddr *) &a, sizeof (a)) < 0 || listen (fd, 8) < 0)
    {
      close (fd);
      return NULL;
    }
  set_cloexec (fd);
  if (out_listen_fd)
    {
      *out_listen_fd = fd;
    }
  /* Return a listener "conn" if you prefer, but here we just pass back fd */
  return NULL;
}


s2s_conn_t *
s2s_tcp_server_accept (int listen_fd, int timeout_ms)
{
  int rc = poll_wait (listen_fd, POLLIN, timeout_ms);
  if (rc != S2S_OK)
    {
      return NULL;
    }
  struct sockaddr_in peer;
  socklen_t sl = sizeof (peer);
  int fd = accept (listen_fd, (struct sockaddr *) &peer, &sl);


  if (fd < 0)
    {
      return NULL;
    }
  set_cloexec (fd);
  set_nodelay (fd);
  s2s_conn_t *c = calloc (1, sizeof (*c));
  if (!c)
    {
      close (fd);
      return NULL;
    }
  c->fd = fd;
  c->role = S2S_ROLE_SERVER;
  return c;
}


s2s_conn_t *
s2s_tcp_client_connect (const char *host, uint16_t port, int total_timeout_ms)
{
  int fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    {
      return NULL;
    }
  set_cloexec (fd);
  struct sockaddr_in a = { 0 };


  a.sin_family = AF_INET;
  a.sin_port = htons (port);
  inet_pton (AF_INET, host, &a.sin_addr);
  /* bounded backoff */
  int elapsed = 0, backoff = S2S_BACKOFF_MIN_MS;


  while (elapsed < total_timeout_ms)
    {
      if (connect (fd, (struct sockaddr *) &a, sizeof (a)) == 0)
        {
          set_nodelay (fd);
          s2s_conn_t *c = calloc (1, sizeof (*c));
          if (!c)
            {
              close (fd);
              return NULL;
            }
          c->fd = fd;
          c->role = S2S_ROLE_CLIENT;
          return c;
        }
      usleep (backoff * 1000);
      elapsed += backoff;
      backoff =
        (backoff * 2 > S2S_BACKOFF_MAX_MS) ? S2S_BACKOFF_MAX_MS : backoff * 2;
    }
  close (fd);
  return NULL;
}


void
s2s_close (s2s_conn_t *c)
{
  if (!c)
    {
      return;
    }
  if (c->fd >= 0)
    {
      shutdown (c->fd, SHUT_RDWR);
      close (c->fd);
    }
  free (c);
}


/* Attach or verify HMAC {key_id, sig} inside the JSON object */
static int
ensure_hmac_on_send (json_t *obj)
{
  if (g_key_count == 0)
    {
      return S2S_E_AUTH_REQUIRED;
    }
  const s2s_key_t *k = &g_keys[0];      /* single-key for now; extend to choose by key_id */
  /* Serialize without auth fields first */
  json_t *copy = json_deep_copy (obj);


  json_object_del (copy, "sig");
  json_object_del (copy, "key_id");
  char *payload = json_dumps (copy, JSON_COMPACT);


  json_decref (copy);
  if (!payload)
    {
      return S2S_E_BADJSON;
    }
  char hex[65];


  if (hmac_sha256_hex
        (k->key, k->key_len, (uint8_t *) payload, strlen (payload), hex) < 0)
    {
      free (payload);
      return S2S_E_IO;
    }
  json_object_set_new (obj, "key_id", json_string (k->key_id));
  json_object_set_new (obj, "sig", json_string (hex));
  free (payload);
  return S2S_OK;
}


static int
verify_hmac_on_recv (json_t *obj)
{
  if (g_key_count == 0)
    {
      return S2S_E_AUTH_REQUIRED;
    }
  json_t *kid = json_object_get (obj, "key_id");
  json_t *sig = json_object_get (obj, "sig");


  if (!kid || !sig || !json_is_string (kid) || !json_is_string (sig))
    {
      return S2S_E_AUTH_BAD;
    }
  const char *key_id = json_string_value (kid);
  const char *sig_hex = json_string_value (sig);
  const s2s_key_t *k = find_key (key_id);


  if (!k)
    {
      return S2S_E_AUTH_BAD;
    }
  /* recompute over a copy without auth fields */
  json_t *copy = json_deep_copy (obj);


  json_object_del (copy, "sig");
  json_object_del (copy, "key_id");
  char *payload = json_dumps (copy, JSON_COMPACT);


  json_decref (copy);
  if (!payload)
    {
      return S2S_E_BADJSON;
    }
  char hex[65];
  int ok = 0;


  if (hmac_sha256_hex
        (k->key, k->key_len, (uint8_t *) payload, strlen (payload), hex) >= 0)
    {
      ok = (strncmp (hex, sig_hex, 64) == 0);
    }
  free (payload);
  return ok ? S2S_OK : S2S_E_AUTH_BAD;
}


/* --- framed send/recv --- */
int
s2s_send_json (s2s_conn_t *c, json_t *obj, int timeout_ms)
{
  if (!c || c->fd < 0 || !obj)
    {
      return S2S_E_IO;
    }
  /* TCP requires HMAC */
  int rc = ensure_hmac_on_send (obj);


  if (rc != S2S_OK)
    {
      return rc;
    }
  char *payload = json_dumps (obj, JSON_COMPACT);


  if (!payload)
    {
      return S2S_E_BADJSON;
    }
  size_t len = strlen (payload);


  if (len > g_cfg.s2s.frame_size_limit)
    {
      LOGE ("s2s_send_json: frame too large (%zu > %d)",
            len,
            g_cfg.s2s.frame_size_limit);
      free (payload);
      g_ctr.toolarge++;
      return S2S_E_TOOLARGE;
    }
  uint32_t be = htonl ((uint32_t) len);


  rc =
    write_n (c->fd, &be, sizeof (be),
             timeout_ms > 0 ? timeout_ms : S2S_DEFAULT_TIMEOUT_MS);
  if (rc == S2S_OK)
    {
      rc =
        write_n (c->fd, payload, len,
                 timeout_ms > 0 ? timeout_ms : S2S_DEFAULT_TIMEOUT_MS);
    }
  free (payload);
  if (rc == S2S_OK)
    {
      g_ctr.sent_ok++;
    }
  return rc;
}


int
s2s_recv_json (s2s_conn_t *c, json_t **out, int timeout_ms)
{
  if (!c || c->fd < 0 || !out)
    {
      return S2S_E_IO;
    }
  uint32_t be = 0;
  int rc = read_n (c->fd, &be, sizeof (be),
                   timeout_ms > 0 ? timeout_ms : S2S_DEFAULT_TIMEOUT_MS);


  if (rc != S2S_OK)
    {
      return rc;
    }
  uint32_t len = ntohl (be);


  if (len == 0 || len > g_cfg.s2s.frame_size_limit)
    {
      LOGE ("s2s_recv_json: invalid/large frame length (%u, limit=%d)",
            len,
            g_cfg.s2s.frame_size_limit);
      g_ctr.toolarge++;
      return S2S_E_TOOLARGE;
    }
  char *buf = malloc (len + 1);


  if (!buf)
    {
      return S2S_E_IO;
    }
  rc =
    read_n (c->fd, buf, len,
            timeout_ms > 0 ? timeout_ms : S2S_DEFAULT_TIMEOUT_MS);
  if (rc != S2S_OK)
    {
      free (buf);
      return rc;
    }
  buf[len] = '\0';
  json_error_t jerr;
  json_t *obj = json_loads (buf, 0, &jerr);


  free (buf);
  if (!obj)
    {
      return S2S_E_BADJSON;
    }
  rc = verify_hmac_on_recv (obj);
  if (rc != S2S_OK)
    {
      g_ctr.auth_fail++;
      json_decref (obj);
      return rc;
    }
  *out = obj;
  g_ctr.recv_ok++;
  return S2S_OK;
}

