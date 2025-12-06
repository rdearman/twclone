#ifndef SERVER_PLAYERS_H
#define SERVER_PLAYERS_H
#pragma once
#include "common.h"             // send_enveloped_*, json_t
#include "globals.h"
#include <jansson.h>            /* -ljansson */
#include <stdbool.h>
#include <sqlite3.h>
#ifdef __cplusplus
extern "C"
{
#endif
int cmd_player_set_settings (client_ctx_t *ctx, json_t *root);
int cmd_player_get_settings (client_ctx_t *ctx, json_t *root);
int cmd_player_set_prefs (client_ctx_t *ctx, json_t *root);
int cmd_player_get_prefs (client_ctx_t *ctx, json_t *root);
int cmd_player_set_topics (client_ctx_t *ctx, json_t *root);
int cmd_player_get_topics (client_ctx_t *ctx, json_t *root);
int cmd_player_set_bookmarks (client_ctx_t *ctx, json_t *root);
int cmd_player_get_bookmarks (client_ctx_t *ctx, json_t *root);
int cmd_player_set_avoids (client_ctx_t *ctx, json_t *root);
int cmd_player_get_avoids (client_ctx_t *ctx, json_t *root);
int cmd_player_get_notes (client_ctx_t *ctx, json_t *root);
int cmd_player_my_info (client_ctx_t *ctx, json_t *root);
int cmd_player_list_online (client_ctx_t *ctx, json_t *root);
int cmd_player_rankings (client_ctx_t *ctx, json_t *root);
void cmd_nav_bookmark_add (client_ctx_t *ctx, json_t *root);
void cmd_nav_bookmark_remove (client_ctx_t *ctx, json_t *root);
void cmd_nav_bookmark_list (client_ctx_t *ctx, json_t *root);
void cmd_nav_avoid_add (client_ctx_t *ctx, json_t *root);
void cmd_nav_avoid_remove (client_ctx_t *ctx, json_t *root);
void cmd_nav_avoid_list (client_ctx_t *ctx, json_t *root);
int h_decloak_ship (sqlite3 *db, int ship_id);
int h_get_active_ship_id (sqlite3 *db, int player_id);
int h_get_player_sector (int player_id);
int h_deduct_ship_credits (struct sqlite3 *db, int player_id, int amount,
                           int *new_balance);
int h_update_ship_cargo (sqlite3 *db, int player_id, const char *commodity,
                         int delta, int *new_qty_out);
int h_get_credits (sqlite3 *db, const char *owner_type, int owner_id,
                   long long *credits_out);
int h_add_credits (sqlite3 *db, const char *owner_type, int owner_id,
                   long long amount, const char *tx_type,
                   const char *tx_group_id, long long *new_balance_out);
int h_deduct_credits (sqlite3 *db, const char *owner_type, int owner_id,
                      long long amount, const char *tx_type,
                      const char *tx_group_id, long long *new_balance_out);
int h_deduct_player_petty_cash (sqlite3 *db, int player_id,
                                long long amount,
                                long long *new_balance_out);
int h_add_player_petty_cash (sqlite3 *db, int player_id, long long amount,
                             long long *new_balance_out);
// Unlocked versions for use within existing transactions
int h_deduct_player_petty_cash_unlocked (sqlite3 *db, int player_id,
                                         long long amount,
                                         long long *new_balance_out);
int h_add_player_petty_cash_unlocked (sqlite3 *db, int player_id,
                                      long long amount,
                                      long long *new_balance_out);
int h_get_player_petty_cash (sqlite3 *db, int player_id,
                             long long *credits_out);
int cmd_get_news (client_ctx_t *ctx, json_t *root);
int cmd_bank_balance (client_ctx_t *ctx, json_t *root);
int cmd_bank_deposit (client_ctx_t *ctx, json_t *root);
int cmd_bank_transfer (client_ctx_t *ctx, json_t *root);
int cmd_bank_withdraw (client_ctx_t *ctx, json_t *root);
int cmd_bank_history (client_ctx_t *ctx, json_t *root);
int cmd_bank_leaderboard (client_ctx_t *ctx, json_t *root);
int h_get_player_bank_account_id (sqlite3 *db, int player_id);
TurnConsumeResult h_consume_player_turn (sqlite3 *db_conn,
                                         client_ctx_t *ctx);
int handle_turn_consumption_error (client_ctx_t *ctx,
                                   TurnConsumeResult consume_result,
                                   const char *cmd, json_t *root,
                                   json_t *meta_data);
/* Convenience wrappers used across the server */
int player_credits (client_ctx_t *ctx);
int cargo_space_free (client_ctx_t *ctx);
// Function to destroy a ship and handle its side effects
int destroy_ship_and_handle_side_effects (client_ctx_t *ctx,
                                          int player_id);
// New function for Big Sleep respawn
int spawn_starter_ship (sqlite3 *db, int player_id, int sector_id);
/* Player Types */
#define PLAYER_TYPE_NPC     1
#define PLAYER_TYPE_HUMAN   2
#define PLAYER_TYPE_SYSOP   3
int auth_player_get_type (int player_id);
int h_send_message_to_player (int player_id,
                              int sender_id,
                              const char *subject,
                              const char *message);
int h_player_apply_progress (sqlite3 *db,
                             int player_id,
                             long long delta_xp,
                             int delta_align,
                             const char *reason);
int h_player_apply_progress (sqlite3 *db,
                             int player_id,
                             long long delta_xp,
                             int delta_align,
                             const char *reason);
int h_player_build_title_payload (sqlite3 *db,
                                  int player_id,
                                  json_t **out_json);
int h_get_cargo_space_free (sqlite3 *db, int player_id, int *free_out);
#ifdef __cplusplus
}
#endif
#endif                          /* SERVER_PLAYERS_H */
