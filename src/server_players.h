#ifndef SERVER_PLAYERS_H
#define SERVER_PLAYERS_H
#pragma once

#include "common.h"     // send_enveloped_*, json_t
#include "types.h"      // client_ctx_t (same struct used in server_loop.c)

#ifdef __cplusplus
extern "C" {
#endif

int cmd_player_my_info    (client_ctx_t* ctx, json_t* root);
int cmd_player_list_online(client_ctx_t* ctx, json_t* root);

#ifdef __cplusplus
}
#endif

#endif /* SERVER_PLAYERS_H */
