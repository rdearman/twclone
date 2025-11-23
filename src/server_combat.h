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
  int cmd_deploy_fighters_list (client_ctx_t * ctx, json_t * root);
  int cmd_deploy_mines_list (client_ctx_t * ctx, json_t * root);
  int cmd_fighters_recall (client_ctx_t * ctx, json_t * root);
  int cmd_combat_deploy_mines (client_ctx_t * ctx, json_t * root);
  int cmd_mines_recall (client_ctx_t * ctx, json_t * root);

  bool armid_stack_is_hostile (const sector_asset_t * mine_asset,
			       int ship_player_id, int ship_corp_id);
  bool armid_stack_is_active (const sector_asset_t * row, time_t now);
  void apply_armid_damage_to_ship (ship_t * ship, int total_damage,
				   armid_damage_breakdown_t * b);

  int apply_armid_mines_on_entry (client_ctx_t * ctx, int new_sector_id,
				  armid_encounter_t * out_enc);

  typedef struct
  {
    int total_mines;		// all mine types
    int armid_mines;
    int limpet_mines;
  } sector_mine_counts_t;

  int get_sector_mine_counts (int sector_id, sector_mine_counts_t * out);

#ifdef __cplusplus
}
#endif

#endif				/* SERVER_COMBAT_H */
