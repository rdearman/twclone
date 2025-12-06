#ifndef SERVER_LOOP_H
#define SERVER_LOOP_H
#pragma once
#include <signal.h>
#include "database.h"           /* for db_handle, etc. */
// #include "universe.h"                /* for sector/planet structures if needed */
//#include "player_interaction.h"
#include "common.h"
/* Single, canonical declaration */
int server_loop (volatile sig_atomic_t *running);
void attach_rate_limit_meta (json_t *env, client_ctx_t *ctx);
void rl_tick (client_ctx_t *ctx);
/* ---- client registry / delivery (used by broadcast path) ---- */
void server_register_client (client_ctx_t *ctx);
void server_unregister_client (client_ctx_t *ctx);
typedef struct client_node_s
{
  client_ctx_t *ctx;
  struct client_node_s *next;
} client_node_t;
extern client_node_t *g_clients;
extern pthread_mutex_t g_clients_mu;


/* Returns 0 if something was delivered; -1 if no online client for player_id.
   Does NOT steal 'data'. */
int server_deliver_to_player (int player_id, const char *event_type,
                              json_t *data);
void idemp_fingerprint_json (json_t *obj, char out[17]);
#endif /* SERVER_LOOP_H */
