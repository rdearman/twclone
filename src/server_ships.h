#ifndef SERVER_SHIPS_H
#define SERVER_SHIPS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <jansson.h>		// for json_t
#include "server_loop.h"	// for ctx_t definition used in the function bodies
#include "common.h"		// adjust if client_ctx_t is defined in another header
#include "errors.h"

/* Exposed handlers implemented in server_ships.c */
  void handle_move_pathfind (client_ctx_t * ctx, json_t * root);
  typedef struct ctx ctx_t;;	// or: typedef struct server_ctx ctx_t;
  int cmd_ship_inspect (client_ctx_t * ctx, json_t * root);
  int cmd_ship_rename (client_ctx_t * ctx, json_t * root);
  int cmd_ship_claim (client_ctx_t * ctx, json_t * root);
  int cmd_ship_status (client_ctx_t * ctx, json_t * root);
  int cmd_ship_info_compat (client_ctx_t * ctx, json_t * root);
  int cmd_ship_transfer_cargo (client_ctx_t * ctx, json_t * root);
  int cmd_ship_jettison (client_ctx_t * ctx, json_t * root);
  int cmd_ship_upgrade (client_ctx_t * ctx, json_t * root);
  int cmd_ship_repair (client_ctx_t * ctx, json_t * root);
  int cmd_ship_self_destruct (client_ctx_t *ctx, json_t *root);



#ifdef __cplusplus
}
#endif
#endif				/* SERVER_SHIPS_H */
