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
void attach_rate_limit_meta (json_t *env, client_ctx_t *ctx);
void rl_tick (client_ctx_t *ctx);


#endif /* SERVER_LOOP_H */
