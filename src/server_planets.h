// server_planets.h
#ifndef SERVER_PLANETS_H
#define SERVER_PLANETS_H
#include "globals.h"
#include <jansson.h>
#include "common.h"
#define GENESIS_ENABLED           1     // 1 to enable, 0 to disable
#define GENESIS_BLOCK_AT_CAP      0     // 1 to hard-block if max_per_sector reached, 0 to allow overfill
#define GENESIS_NAVHAZ_DELTA      0     // Change in navhaz when a planet is created
// Planet class weights (used for weighted random selection)
#define GENESIS_CLASS_WEIGHT_M    10
#define GENESIS_CLASS_WEIGHT_K    10
#define GENESIS_CLASS_WEIGHT_O    10
#define GENESIS_CLASS_WEIGHT_L    10
#define GENESIS_CLASS_WEIGHT_C    10
#define GENESIS_CLASS_WEIGHT_H    10
#define GENESIS_CLASS_WEIGHT_U    5
int cmd_planet_genesis (client_ctx_t *ctx, json_t *root);
int cmd_planet_info (client_ctx_t *ctx, json_t *root);
int cmd_planet_rename (client_ctx_t *ctx, json_t *root);
int cmd_planet_land (client_ctx_t *ctx, json_t *root);
int cmd_planet_launch (client_ctx_t *ctx, json_t *root);
int cmd_planet_transfer_ownership (client_ctx_t *ctx, json_t *root);
int cmd_planet_harvest (client_ctx_t *ctx, json_t *root);
int cmd_planet_deposit (client_ctx_t *ctx, json_t *root);
int cmd_planet_withdraw (client_ctx_t *ctx, json_t *root);
int cmd_planet_genesis_create (client_ctx_t *ctx, json_t *root);
#endif
