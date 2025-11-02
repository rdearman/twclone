#ifndef SERVER_COMBAT_H
#define SERVER_COMBAT_H

#include <jansson.h>
#include "common.h"		// client_ctx_t

#ifdef __cplusplus
extern "C"
{
#endif

  int cmd_combat_attack (client_ctx_t * ctx, json_t * root);	// "combat.attack"
  int cmd_combat_deploy_fighters (client_ctx_t * ctx, json_t * root);	// "combat.deploy_fighters"
  int cmd_combat_lay_mines (client_ctx_t * ctx, json_t * root);	// "combat.lay_mines"
  int cmd_combat_sweep_mines (client_ctx_t * ctx, json_t * root);	// "combat.sweep_mines"
  int cmd_combat_status (client_ctx_t * ctx, json_t * root);	// "combat.status"
  int cmd_deploy_fighters_list (client_ctx_t *ctx, json_t *root);
  int cmd_deploy_mines_list (client_ctx_t *ctx, json_t *root);




#ifdef __cplusplus
}
#endif

#endif				/* SERVER_COMBAT_H */
