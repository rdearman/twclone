/* src/server_ports.h */
#ifndef SERVER_PORTS_H
#define SERVER_PORTS_H
#include <jansson.h>
#include <stdbool.h>
#include "common.h"
#include "db/db_api.h"

int cmd_trade_port_info (client_ctx_t *ctx, json_t *root);
int cmd_trade_buy (client_ctx_t *ctx, json_t *root);
int cmd_trade_sell (client_ctx_t *ctx, json_t *root);
int cmd_trade_history (client_ctx_t *ctx, json_t *root);
int cmd_dock_status (client_ctx_t *ctx, json_t *root);
int cmd_port_rob (client_ctx_t *ctx, json_t *root);
int cmd_trade_jettison (client_ctx_t *ctx, json_t *root);
int cmd_trade_quote (client_ctx_t *ctx, json_t *root);

int h_get_ship_cargo_and_holds (db_t *db,
                                int ship_id,
                                int *ore,
                                int *org,
                                int *eq,
                                int *col,
                                int *slv,
                                int *wpn,
                                int *drg,
                                int *holds);
int h_update_port_stock (db_t *db,
                         int port_id,
                         const char *commodity,
                         int delta,
                         int *new_qty);
int h_update_entity_stock (db_t *db,
                           const char *type,
                           int id,
                           const char *code,
                           int delta,
                           int *new_qty);
int h_get_port_commodity_details (db_t *db,
                                  int port_id,
                                  const char *code,
                                  int *qty,
                                  int *price,
                                  bool *buy,
                                  bool *sell);
int h_entity_calculate_buy_price (db_t *db,
                                  const char *type,
                                  int id,
                                  const char *code);
int h_entity_calculate_sell_price (db_t *db,
                                   const char *type,
                                   int id,
                                   const char *code);
int h_market_move_port_stock (db_t *db, int port_id, const char *code,
                              int delta);
int h_calculate_port_buy_price (db_t *db, int port_id, const char *commodity);
int h_calculate_port_sell_price (db_t *db, int port_id, const char *commodity);

#endif