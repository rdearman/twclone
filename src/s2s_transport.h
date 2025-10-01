#ifndef S2S_TRANSPORT_H
#define S2S_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>
#include <jansson.h>


/* --- Config constants --- */
#define S2S_MAX_FRAME          (64 * 1024)  /* 64 KiB hard cap */
#define S2S_DEFAULT_TIMEOUT_MS 3000
#define S2S_BACKOFF_MIN_MS     100
#define S2S_BACKOFF_MAX_MS     5000

/* Opaque connection */
typedef struct s2s_conn s2s_conn_t;

/* Key ring item for HMAC */
typedef struct {
  char     key_id[32];   /* short ID string */
  uint8_t  key[64];      /* HMAC key bytes */
  size_t   key_len;      /* bytes used from key[] */
} s2s_key_t;

/* Error codes (negative) */
enum {
  S2S_OK                    = 0,
  S2S_E_TIMEOUT             = -1,
  S2S_E_CLOSED              = -2,
  S2S_E_IO                  = -3,
  S2S_E_TOOLARGE            = -4,
  S2S_E_BADJSON             = -5,
  S2S_E_AUTH_REQUIRED       = -6,
  S2S_E_AUTH_BAD            = -7
};

/* Connection role */
typedef enum { S2S_ROLE_SERVER=1, S2S_ROLE_CLIENT=2 } s2s_role_t;

/* --- Lifecycle --- */
s2s_conn_t* s2s_tcp_server_listen(const char *host, uint16_t port, int *out_listen_fd);
s2s_conn_t* s2s_tcp_server_accept(int listen_fd, int timeout_ms);
/* Client connect with bounded backoff; returns connected conn or NULL */
s2s_conn_t* s2s_tcp_client_connect(const char *host, uint16_t port,
                                   int total_timeout_ms);

/* Close (idempotent) */
void s2s_close(s2s_conn_t *c);

/* Install a key ring (required for TCP). keys may be NULL/0 to clear. */
void s2s_set_keyring(const s2s_key_t *keys, size_t n);

/* --- Framed JSON send/recv (HMAC enforced on TCP) --- */
/* Will attach {"key_id":..., "sig":...} if not present. */
int s2s_send_json(s2s_conn_t *c, json_t *obj, int timeout_ms);

/* On success, *out takes ownership (refcount +1). */
int s2s_recv_json(s2s_conn_t *c, json_t **out, int timeout_ms);

/* Metrics hooks (optional; return cached counters) */
void s2s_get_counters(uint64_t *sent_ok, uint64_t *recv_ok,
                      uint64_t *auth_fail, uint64_t *toolarge);

void s2s_debug_dump_conn(const char *who, s2s_conn_t *c);

#endif
