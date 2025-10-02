#ifndef SERVER_S2S_H
#define SERVER_S2S_H

#include <jansson.h>
#include "common.h"		// client_ctx_t
#include <pthread.h>
#include "s2s_transport.h"
#include "server_envelope.h"
#include "schemas.h"

/* Start/stop the control thread that reads envs from `conn` and dispatches. */
int server_s2s_start (s2s_conn_t * conn, pthread_t * out_thr,
		      volatile sig_atomic_t * running_flag);
void server_s2s_stop (pthread_t thr);

/* If you need to unit-test dispatch directly: */
int server_s2s_dispatch (s2s_conn_t * c, json_t * env);

#ifdef __cplusplus
extern "C"
{
#endif

/* Server-to-server commands */
  int cmd_s2s_planet_genesis (client_ctx_t * ctx, json_t * root);	// "s2s.planet.genesis"
  int cmd_s2s_planet_transfer (client_ctx_t * ctx, json_t * root);	// "s2s.planet.transfer"
  int cmd_s2s_player_migrate (client_ctx_t * ctx, json_t * root);	// "s2s.player.migrate"
  int cmd_s2s_port_restock (client_ctx_t * ctx, json_t * root);	// "s2s.port.restock"
  int cmd_s2s_event_relay (client_ctx_t * ctx, json_t * root);	// "s2s.event.relay"
  int cmd_s2s_replication_heartbeat (client_ctx_t * ctx, json_t * root);	// "s2s.replication.heartbeat"


#ifdef __cplusplus
}
#endif

#endif				/* SERVER_S2S_H */
