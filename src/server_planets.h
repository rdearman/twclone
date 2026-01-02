/* src/server_planets.h */
#ifndef SERVER_PLANETS_H
#define SERVER_PLANETS_H
#include <jansson.h>
#include <stdbool.h>
#include "common.h"
#include "db/db_api.h"

typedef struct {
  int id;
  int owner_id;
  int owner_type_is_corp;
  char *owner_type;
} planet_t;

int cmd_planet_info (client_ctx_t *ctx, json_t *root);
int cmd_planet_rename (client_ctx_t *ctx, json_t *root);
int cmd_planet_land (client_ctx_t *ctx, json_t *root);
int cmd_planet_launch (client_ctx_t *ctx, json_t *root);
int cmd_planet_transfer_ownership (client_ctx_t *ctx, json_t *root);
int cmd_planet_harvest (client_ctx_t *ctx, json_t *root);
int cmd_planet_deposit (client_ctx_t *ctx, json_t *root);
int cmd_planet_withdraw (client_ctx_t *ctx, json_t *root);
int cmd_planet_genesis_create (client_ctx_t *ctx, json_t *root);
int cmd_planet_market_sell (client_ctx_t *ctx, json_t *root);
int cmd_planet_market_buy_order (client_ctx_t *ctx, json_t *root);
int cmd_planet_transwarp (client_ctx_t *ctx, json_t *root);
int cmd_planet_colonists_set (client_ctx_t *ctx, json_t *root);
int cmd_planet_colonists_get (client_ctx_t *ctx, json_t *root);

int h_planet_check_trade_legality (db_t *db,
                                   int pid,
                                   int player_id,
                                   const char *code,
                                   bool buy);
int h_get_planet_owner_info (db_t *db, int pid, planet_t *p);
int h_market_move_planet_stock (db_t *db, int pid, const char *code, int delta);
int h_get_commodity_id_by_code (db_t *db, const char *code);

#endif