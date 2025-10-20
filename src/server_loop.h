#ifndef SERVER_LOOP_H
#define SERVER_LOOP_H
#pragma once
#include <signal.h>
#include "database.h"		/* for db_handle, etc. */
// #include "universe.h"                /* for sector/planet structures if needed */
//#include "player_interaction.h"
#include "common.h"

/* Single, canonical declaration */
int server_loop (volatile sig_atomic_t * running);
void attach_rate_limit_meta (json_t * env, client_ctx_t * ctx);
void rl_tick (client_ctx_t * ctx);


/* ---- client registry / delivery (used by broadcast path) ---- */
void server_register_client(client_ctx_t *ctx);
void server_unregister_client(client_ctx_t *ctx);

/* Returns 0 if something was delivered; -1 if no online client for player_id.
   Does NOT steal 'data'. */
int server_deliver_to_player(int player_id, const char *event_type, json_t *data);

#endif /* SERVER_LOOP_H */
