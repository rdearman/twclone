#ifndef SERVER_SHIPS_H
#define SERVER_SHIPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <jansson.h>      // for json_t

/* Pull in the definition of client_ctx_t.
   If client_ctx_t lives somewhere else, include that header instead. */
#include "server_loop.h"  // adjust if client_ctx_t is defined in another header

/* Exposed handlers implemented in server_ships.c */
void handle_move_pathfind(client_ctx_t *ctx, json_t *root);

#ifdef __cplusplus
}
#endif
#endif /* SERVER_SHIPS_H */
