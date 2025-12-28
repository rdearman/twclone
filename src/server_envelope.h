// server_envelope.h
#ifndef SERVER_ENVELOPE_H
#define SERVER_ENVELOPE_H
#include <jansson.h>

#include "common.h" // For client_ctx_t

// Defined in server_loop.c, used by send functions to access thread's context
extern __thread client_ctx_t *g_ctx_for_send;



static inline void
jsonp_cleanup (json_t **p)
{
  if (p && *p)
    {
      json_decref (*p);
    }
}


#define JSON_AUTO __attribute__ ((cleanup (jsonp_cleanup))) json_t *
#include "s2s_transport.h"      // for s2s_conn_t
#include "common.h"
void iso8601_utc (char out[32]);        // if you use timestamps in envelopes
const char *next_msg_id (void); // if you auto-number messages


void send_response_ok_borrow (client_ctx_t *ctx,
                              json_t *req,
                              const char *type, const json_t *data);
void send_response_ok_take (client_ctx_t *ctx,
                            json_t *req, const char *type, json_t **pdata);

/* ergonomics */
#define SEND_OK_BORROW(ctx, req, type, data) send_response_ok_borrow ((ctx), \
                                                                      (req), \
                                                                      (type), \
                                                                      (data))
#define SEND_OK_TAKE(ctx, req, type, pdata)  send_response_ok_take ((ctx), \
                                                                    (req), \
                                                                    (type), \
                                                                    (pdata))

/* Make the old ambiguous API illegal everywhere */
#ifdef send_response_ok
#undef send_response_ok
#endif
#define send_response_ok( \
          ...) DO_NOT_USE_send_response_ok__use_send_response_ok_borrow_or_take \
          (__VA_ARGS__)

#ifdef send_response_ok_steal
#undef send_response_ok_steal
#endif
#define send_response_ok_steal( \
          ...) \
        DO_NOT_USE_send_response_ok_steal__use_send_response_ok_take_and_ptr ( \
          __VA_ARGS__)


void send_enveloped_ok (int fd, json_t *req, const char *type,
                        json_t *data);
void send_enveloped_error (int fd, json_t *req, int code,
                           const char *message);
void send_enveloped_refused (int fd, json_t *req, int code, const char *msg,
                             json_t *data_opt);

/* Context-aware wrappers */
void send_response_error (client_ctx_t *ctx,
                          json_t *req, int code, const char *msg);
void send_response_refused (client_ctx_t *ctx,
                            json_t *req,
                            int code, const char *msg, json_t *data_opt);


static inline void
send_response_refused_steal (client_ctx_t *ctx,
                             json_t *req,
                             int code, const char *msg, json_t *data_opt)
{
  send_response_refused (ctx, req, code, msg, data_opt);
  if (data_opt)
    {
      json_decref (data_opt);
    }
}


json_t *s2s_make_env (const char *type, const char *src, const char *dst,
                      json_t *payload);
json_t *s2s_make_ack (const char *src, const char *dst, const char *ack_of,
                      json_t *payload);
json_t *s2s_make_error (const char *src, const char *dst, const char *ack_of,
                        const char *code, const char *message,
                        json_t *details);
/* Parsing helpers */
const char *s2s_env_type (json_t *env);
const char *s2s_env_id (json_t *env);
json_t *s2s_env_payload (json_t *env);  /* borrowed ref */
/* Minimal envelope validation */
int s2s_env_validate_min (json_t *env, char **why);     /* 0 ok, else <0, *why malloc'd */
/* Thin wrappers over transport */
int s2s_send_env (s2s_conn_t *c, json_t *env, int timeout_ms);
int s2s_recv_env (s2s_conn_t *c, json_t **out_env, int timeout_ms);
json_t *make_base_envelope (json_t *req, const char *type);
int cmd_system_schema_list (client_ctx_t *ctx, json_t *root);
int cmd_system_cmd_list (client_ctx_t *ctx, json_t *root);
void send_error_json (int fd, int code, const char *msg);
void send_all_json (int fd, json_t *obj);
int j_get_integer (json_t *root, const char *path, int *result);
#endif
