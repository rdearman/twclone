#ifndef SERVER_PORTS_H
#define SERVER_PORTS_H

#include <jansson.h>
#include "common.h"        // client_ctx_t
// If you centralised send_enveloped_* in a header:
#include "server_envelope.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Port info / status */
int cmd_port_info      (client_ctx_t *ctx, json_t *root);

/* Trading */
int cmd_trade_buy      (client_ctx_t *ctx, json_t *root);
int cmd_trade_sell     (client_ctx_t *ctx, json_t *root);

/* Optional if your loop implements them */
//int cmd_trade_quote    (client_ctx_t *ctx, json_t *root);
//int cmd_trade_jettison (client_ctx_t *ctx, json_t *root);




  
#ifdef __cplusplus
}
#endif

#endif /* SERVER_PORTS_H */
