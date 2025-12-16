// server_envelope.h
#ifndef SERVER_ENVELOPE_H
#define SERVER_ENVELOPE_H
#include <jansson.h>
#include "s2s_transport.h"      // for s2s_conn_t
#include "common.h"
void iso8601_utc (char out[32]);        // if you use timestamps in envelopes
const char *next_msg_id (void); // if you auto-number messages
/* Ownership contract:
 * send_response_ok / send_enveloped_ok TAKE OWNERSHIP of payload.
 * Caller must NOT json_decref(payload) after calling.
 */
void send_enveloped_ok (int fd, json_t *req, const char *type,
                        json_t *data);
void send_enveloped_error (int fd, json_t *req, int code, const char *msg);
void send_enveloped_refused (int fd, json_t *req, int code, const char *msg,
                             json_t *data_opt);

/* Context-aware wrappers that support capture/bulk execution */
/* Note: 'data' is consumed (json_decref called internally). Do not decref 'data' after calling send_response_ok. */
void send_response_ok (client_ctx_t *ctx, json_t *req, const char *type, json_t *data);
void send_response_error (client_ctx_t *ctx, json_t *req, int code, const char *msg);
void send_response_refused (client_ctx_t *ctx, json_t *req, int code, const char *msg, json_t *data_opt);

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
