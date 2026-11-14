#ifndef SERVER_FEEDBACK_H
#define SERVER_FEEDBACK_H
#include "common.h"
#include <jansson.h>
int cmd_feedback_submit(client_ctx_t *ctx, json_t *root);
int cmd_feedback_preview(client_ctx_t *ctx, json_t *root);
int cmd_feedback_status(client_ctx_t *ctx, json_t *root);
int cmd_feedback_diagnostics(client_ctx_t *ctx, json_t *root);
#endif
