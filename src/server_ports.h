/* src/server_ports.h */
#ifndef SERVER_PORTS_H
#define SERVER_PORTS_H
#include <jansson.h>
#include <stdbool.h>
#include "common.h"
#include "db/db_api.h"


typedef struct {
  char *commodity;
  int amount;          /* used by sell */
  int quantity;        /* used by buy */
  int unit_price;
  long long line_cost;
} TradeLine;


int cmd_trade_port_info (db_t *db,client_ctx_t *ctx, json_t *root);
int cmd_trade_buy (db_t *db,client_ctx_t *ctx, json_t *root);
int cmd_trade_sell (db_t *db,client_ctx_t *ctx, json_t *root);
int cmd_trade_history (db_t *db,client_ctx_t *ctx, json_t *root);
int cmd_dock_status (db_t *db,client_ctx_t *ctx, json_t *root);
int cmd_port_rob (db_t *db,client_ctx_t *ctx, json_t *root);
int cmd_trade_jettison (db_t *db,client_ctx_t *ctx, json_t *root);
int cmd_trade_quote (db_t *db,client_ctx_t *ctx, json_t *root);

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

int
h_update_entity_stock (db_t *db,
                       const char *entity_type,
                       int entity_id,
                       const char *commodity_code,
                       int quantity_delta, int *new_quantity_out);


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

int parse_trade_lines (json_t *jitems, TradeLine **out_lines, size_t *out_n);


#endif
