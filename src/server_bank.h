#ifndef SERVER_BANK_H
#define SERVER_BANK_H
#pragma once
#include "common.h"             // send_enveloped_*, json_t
#include "globals.h"
#include <jansson.h>            /* -ljansson */
#include <stdbool.h>


int cmd_bank_balance (client_ctx_t *ctx, json_t *root);
int cmd_bank_deposit (client_ctx_t *ctx, json_t *root);
int cmd_bank_transfer (client_ctx_t *ctx, json_t *root);
int cmd_bank_withdraw (client_ctx_t *ctx, json_t *root);
int cmd_bank_history (client_ctx_t *ctx, json_t *root);
int cmd_bank_leaderboard (client_ctx_t *ctx, json_t *root);

int cmd_fine_list (client_ctx_t *ctx, json_t *root);
int cmd_fine_pay (client_ctx_t *ctx, json_t *root);
int h_player_bank_balance_add (db_t *db, int player_id, long long delta,
			       long long *new_balance_out);

  
#endif
