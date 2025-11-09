// server_planets.h
#ifndef SERVER_PLANETS_H
#define SERVER_PLANETS_H
#include "globals.h"
#include <jansson.h>
#include "common.h"

int cmd_planet_genesis (client_ctx_t * ctx, json_t * root);
int cmd_planet_info (client_ctx_t * ctx, json_t * root);
int cmd_planet_rename (client_ctx_t * ctx, json_t * root);
int cmd_planet_land (client_ctx_t * ctx, json_t * root);
int cmd_planet_launch (client_ctx_t * ctx, json_t * root);
int cmd_planet_transfer_ownership (client_ctx_t * ctx, json_t * root);
int cmd_planet_harvest (client_ctx_t * ctx, json_t * root);
int cmd_planet_deposit (client_ctx_t * ctx, json_t * root);
int cmd_planet_withdraw (client_ctx_t * ctx, json_t * root);
int h_update_planet_stock (sqlite3 *db, int planet_id, const char *commodity, int delta, int *new_qty_out);


#endif
