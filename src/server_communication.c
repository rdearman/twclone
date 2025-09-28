#include "server_communication.h"
#include "server_envelope.h"	// send_enveloped_ok/error/refused
#include "errors.h"
#include "config.h"
#include <jansson.h>

/* ---- shared helpers (module-local) ---- */
static inline int
require_auth (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id > 0)
    return 1;
  send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
  return 0;
}

static inline int
niy (client_ctx_t *ctx, json_t *root, const char *which)
{
  (void) which;			// keep for future logging if desired
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented");
  return 0;
}

/* ===================== chat.* ===================== */

int
cmd_chat_send (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {channel?, text}, persist/deliver
  return niy (ctx, root, "chat.send");
}

int
cmd_chat_broadcast (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: admin/mod ACL, broadcast message
  return niy (ctx, root, "chat.broadcast");
}

int
cmd_chat_history (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {channel?, before?, limit?}, return messages
  return niy (ctx, root, "chat.history");
}

/* ===================== mail.* ===================== */

int
cmd_mail_send (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {to, subject?, body}, enqueue/persist
  return niy (ctx, root, "mail.send");
}

int
cmd_mail_inbox (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: list inbox for ctx->player_id
  return niy (ctx, root, "mail.inbox");
}

int
cmd_mail_read (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {mail_id}, return message payload
  return niy (ctx, root, "mail.read");
}

int
cmd_mail_delete (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {mail_id}, delete/mark
  return niy (ctx, root, "mail.delete");
}

/* ================== subscribe.* =================== */

int
cmd_subscribe_add (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {topic}, add subscription
  return niy (ctx, root, "subscribe.add");
}

int
cmd_subscribe_remove (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: parse {topic}, remove subscription
  return niy (ctx, root, "subscribe.remove");
}

int
cmd_subscribe_list (client_ctx_t *ctx, json_t *root)
{
  if (!require_auth (ctx, root))
    return 0;
  // TODO: return list of current subscriptions
  return niy (ctx, root, "subscribe.list");
}

/* ================== admin.* =================== */


static inline int
require_admin (client_ctx_t *ctx, json_t *root)
{
  // TODO: replace with real ACL; for now require auth at least
  if (ctx->player_id > 0)
    return 1;
  send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
  return 0;
}

int
cmd_admin_notice (client_ctx_t *ctx, json_t *root)
{
  if (!require_admin (ctx, root))
    return 0;
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented: admin.notice");
  return 0;
}

int
cmd_admin_shutdown_warning (client_ctx_t *ctx, json_t *root)
{
  if (!require_admin (ctx, root))
    return 0;
  send_enveloped_error (ctx->fd, root, 1101,
			"Not implemented: admin.shutdown_warning");
  return 0;
}
