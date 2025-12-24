#ifndef SERVER_CORPORATION_H
#define SERVER_CORPORATION_H
#include <jansson.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdint.h>
#include "database.h"
#include "server_loop.h"
// Helper functions
int h_get_player_corp_role (sqlite3 *db, int player_id, int corp_id,
                            char *role_buffer, size_t buffer_size);
int h_corp_is_publicly_traded (sqlite3 *db, int corp_id,
                               bool *is_publicly_traded);
int h_get_corp_credit_rating (sqlite3 *db, int corp_id, int *rating);
int h_get_corp_bank_account_id (sqlite3 *db, int corp_id);
int h_get_player_corp_id (sqlite3 *db, int player_id);
int h_get_corp_stock_id (sqlite3 *db, int corp_id, int *out_stock_id);
int h_get_stock_info (sqlite3 *db, int stock_id, char **out_ticker,
                      int *out_corp_id, int *out_total_shares,
                      int *out_par_value, int *out_current_price,
                      long long *out_last_dividend_ts);
int h_update_player_shares (sqlite3 *db, int player_id, int stock_id,
                            int quantity_change);

/* Returns 1 if player is the active CEO of some corporation, 0 otherwise.
 * If out_corp_id is non-NULL, it will be set to that corporation id.
 */
int h_is_player_corp_ceo (sqlite3 *db, int player_id, int *out_corp_id);
/* RPC: corp.transfer_ceo */
int cmd_corp_transfer_ceo (client_ctx_t *ctx, json_t *root);
// Command handlers
int cmd_corp_create (client_ctx_t *ctx, json_t *root);
int cmd_corp_dissolve (client_ctx_t *ctx, json_t *root);
int cmd_corp_invite (client_ctx_t *ctx, json_t *root);
int cmd_corp_kick (client_ctx_t *ctx, json_t *root);
int cmd_corp_join (client_ctx_t *ctx, json_t *root);
int cmd_corp_leave (client_ctx_t *ctx, json_t *root);
int cmd_corp_list (client_ctx_t *ctx, json_t *root);
int cmd_corp_roster (client_ctx_t *ctx, json_t *root);
int cmd_corp_balance (client_ctx_t *ctx, json_t *root);
int cmd_corp_statement (client_ctx_t *ctx, json_t *root);
int cmd_corp_status (client_ctx_t *ctx, json_t *root);
int cmd_corp_deposit (client_ctx_t *ctx, json_t *root);
int cmd_corp_withdraw (client_ctx_t *ctx, json_t *root);
int cmd_stock (client_ctx_t *ctx, json_t *root);
int cmd_stock_ipo_register (client_ctx_t *ctx, json_t *root);
int cmd_stock_buy (client_ctx_t *ctx, json_t *root);
int cmd_stock_dividend_set (client_ctx_t *ctx, json_t *root);
// Cron handler
int h_daily_corp_tax (sqlite3 *db, int64_t now_s);
int h_dividend_payout (sqlite3 *db, int64_t now_s);
#endif // SERVER_CORPORATION_H
