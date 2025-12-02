#ifndef SERVER_PORTS_H
#define SERVER_PORTS_H
#include <jansson.h>
#include "common.h"             // client_ctx_t
// If you centralised send_enveloped_* in a header:
#include "server_envelope.h"
#include "server_rules.h"
#ifdef __cplusplus
extern "C"
{
#endif
typedef struct TradeLine
{
  char *commodity;
  int amount;
  int unit_price;
  long long line_cost;
  bool is_illegal; // Added for illegal trade logic
} TradeLine;
/* Port info / status */
int cmd_trade_port_info (client_ctx_t *ctx, json_t *root);
/* Trading */
int cmd_trade_buy (client_ctx_t *ctx, json_t *root);
int cmd_trade_sell (client_ctx_t *ctx, json_t *root);
int cmd_trade_offer (client_ctx_t *ctx, json_t *root);
int cmd_trade_accept (client_ctx_t *ctx, json_t *root);
int cmd_trade_cancel (client_ctx_t *ctx, json_t *root);
int cmd_trade_history (client_ctx_t *ctx, json_t *root);
/* Optional if your loop implements them */
int cmd_trade_quote (client_ctx_t *ctx, json_t *root);
int cmd_trade_jettison (client_ctx_t *ctx, json_t *root);
int cmd_dock_status (client_ctx_t *ctx, json_t *root);
int cmd_trade_port_info (client_ctx_t *ctx, json_t *root);
int cmd_port_rob (client_ctx_t *ctx, json_t *root);
//int player_credits (int player_id);
//int cargo_space_free (int player_id);
int port_is_open (int port_id, const char *commodity);
int h_get_ship_cargo_and_holds (sqlite3 *db, int ship_id, int *ore,
                                int *organics, int *equipment, int *holds,
                                int *colonists, int *slaves, int *weapons,
                                int *drugs);
int h_update_port_stock (sqlite3 *db, int port_id, const char *commodity,
                         int delta, int *new_qty_out);
int h_calculate_port_buy_price (sqlite3 *db, int port_id,
                                const char *commodity);
int h_calculate_port_sell_price (sqlite3 *db, int port_id,
                                 const char *commodity);
int h_port_build_inventory (sqlite3 *db,
                            int port_id,
                            int player_id,
                            json_t **out_array);
bool h_illegal_visible (int player_align_band_id,
                        int cluster_align_band_id,
                        const commodity_t *comm);
bool h_illegal_trade_allowed (int player_align_band_id,
                              int cluster_align_band_id,
                              const commodity_t *comm);
// Forward declaration for local helper
// void free_trade_lines (struct TradeLine *lines, size_t n);
void free_trade_lines (TradeLine *lines, size_t n);
#ifdef __cplusplus
}
#endif
#endif                          /* SERVER_PORTS_H */
