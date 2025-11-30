#include "server_bulk.h"
#include "server_envelope.h"
int
cmd_bulk_execute (client_ctx_t *ctx, json_t *root)
{
  (void) ctx;
  (void) root;
  send_enveloped_error (ctx->fd, root, 1101, "Not implemented: bulk.execute");
  return 0;
}

