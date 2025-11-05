#ifndef SERVER_PORTS_H
#define SERVER_PORTS_H

#include <jansson.h>
#include "common.h"		// client_ctx_t
// If you centralised send_enveloped_* in a header:
#include "server_envelope.h"
#include "server_rules.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* Port info / status */
  int cmd_port_info (client_ctx_t * ctx, json_t * root);

/* Trading */
  int cmd_trade_buy (client_ctx_t * ctx, json_t * root);
  int cmd_trade_sell (client_ctx_t * ctx, json_t * root);
  int cmd_trade_offer (client_ctx_t * ctx, json_t * root);
  int cmd_trade_accept (client_ctx_t * ctx, json_t * root);
  int cmd_trade_cancel (client_ctx_t * ctx, json_t * root);
  int cmd_trade_history (client_ctx_t * ctx, json_t * root);

/* Optional if your loop implements them */
  int cmd_trade_quote (client_ctx_t * ctx, json_t * root);
  int cmd_trade_jettison (client_ctx_t * ctx, json_t * root);
  int cmd_trade_port_info (client_ctx_t * ctx, json_t * root);

  //int player_credits (int player_id);
  //int cargo_space_free (int player_id);
  int port_is_open (int port_id, const char *commodity);
  extern double h_calculate_trade_price(int port_id, const char *commodity, int quantity);

#ifdef __cplusplus
}
#endif

#endif				/* SERVER_PORTS_H */
