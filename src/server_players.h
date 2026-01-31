/* src/server_players.h */
#ifndef SERVER_PLAYERS_H
#define SERVER_PLAYERS_H
#include <jansson.h>
#include <stdbool.h>
#include "common.h"
#include "db/db_api.h"

int cmd_player_set_settings (client_ctx_t * ctx, json_t * root);
int cmd_player_get_settings (client_ctx_t * ctx, json_t * root);
int cmd_player_set_prefs (client_ctx_t * ctx, json_t * root);
int cmd_player_get_prefs (client_ctx_t * ctx, json_t * root);
int cmd_player_set_bookmarks (client_ctx_t * ctx, json_t * root);
int cmd_player_get_bookmarks (client_ctx_t * ctx, json_t * root);
int cmd_player_set_avoids (client_ctx_t * ctx, json_t * root);
int cmd_player_get_avoids (client_ctx_t * ctx, json_t * root);
int cmd_player_get_notes (client_ctx_t * ctx, json_t * root);
int cmd_player_set_note (client_ctx_t * ctx, json_t * root);
int cmd_player_delete_note (client_ctx_t * ctx, json_t * root);
int cmd_player_my_info (client_ctx_t * ctx, json_t * root);
int cmd_player_list_online (client_ctx_t * ctx, json_t * root);
int cmd_player_rankings (client_ctx_t * ctx, json_t * root);
int cmd_player_computer_recommend_routes (client_ctx_t * ctx, json_t * root);

void cmd_nav_bookmark_add (client_ctx_t * ctx, json_t * root);
void cmd_nav_bookmark_remove (client_ctx_t * ctx, json_t * root);
void cmd_nav_bookmark_list (client_ctx_t * ctx, json_t * root);
void cmd_nav_avoid_add (client_ctx_t * ctx, json_t * root);
void cmd_nav_avoid_remove (client_ctx_t * ctx, json_t * root);
void cmd_nav_avoid_list (client_ctx_t * ctx, json_t * root);

int h_decloak_ship (db_t * db, int ship_id);
int h_get_active_ship_id (db_t * db, int player_id);
int h_get_player_sector (db_t * db, int player_id);
int h_deduct_ship_credits (db_t * db, int player_id, int amount,
			   int *new_balance);
int h_get_credits (db_t * db,
		   const char *owner_type,
		   int owner_id, long long *credits_out);
int h_add_credits (db_t * db,
		   const char *owner_type,
		   int owner_id,
		   long long amount,
		   const char *tx_type,
		   const char *tx_group_id, long long *new_balance_out);
int h_deduct_credits (db_t * db,
		      const char *owner_type,
		      int owner_id,
		      long long amount,
		      const char *tx_type,
		      const char *tx_group_id, long long *new_balance_out);
int h_deduct_player_petty_cash (db_t * db,
				int player_id,
				long long amount, long long *new_balance_out);
int h_deduct_ship_credits (db_t * db, int player_id, int amount,
			   int *new_balance);
int h_add_player_petty_cash (db_t * db, int player_id, long long amount,
			     long long *new_balance_out);
int h_deduct_player_petty_cash_unlocked (db_t * db, int player_id,
					 long long amount,
					 long long *new_balance_out);
int h_add_player_petty_cash_unlocked (db_t * db, int player_id,
				      long long amount,
				      long long *new_balance_out);
int h_get_player_petty_cash (db_t * db, int player_id,
			     long long *out_credits);
int h_send_message_to_player (db_t * db, int player_id, int sender_id,
			      const char *subject, const char *message);
int h_player_apply_progress (db_t * db, int player_id, long long delta_xp,
			     int delta_align, const char *reason);
int h_player_build_title_payload (db_t * db, int player_id,
				  json_t ** out_json);
int h_get_cargo_space_free (db_t * db, int player_id, int *free_out);
int h_player_is_npc (db_t * db, int player_id);
int spawn_starter_ship (db_t * db, int player_id, int sector_id);
int destroy_ship_and_handle_side_effects (client_ctx_t * ctx, int player_id);

TurnConsumeResult h_consume_player_turn (db_t * db,
					 client_ctx_t * ctx,
					 int turns_to_consume);
int handle_turn_consumption_error (client_ctx_t * ctx,
				   TurnConsumeResult consume_result,
				   const char *cmd,
				   json_t * root, json_t * meta_data);

int h_player_petty_cash_add (db_t * db, int player_id, long long delta,
			     long long *new_balance_out);

#include "server_auth.h"

int auth_player_get_type (int player_id);

#endif
