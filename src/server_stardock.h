#ifndef SERVER_STARDOCK_H
#define SERVER_STARDOCK_H
#include <jansson.h>
#include "common.h"             // For client_ctx_t
// Location Types
#define LOCATION_STARDOCK "STARDOCK"
#define LOCATION_CLASS0 "CLASS0"
#define LOCATION_OTHER "OTHER"
// Port Types
#define PORT_TYPE_STARDOCK 9
#define PORT_TYPE_CLASS0 0
// Hardware Categories
#define HW_CATEGORY_FIGHTER "FIGHTER"
#define HW_CATEGORY_SHIELD "SHIELD"
#define HW_CATEGORY_HOLD "HOLD"
#define HW_CATEGORY_SPECIAL "SPECIAL"
#define HW_CATEGORY_MODULE "MODULE"
// Specific Hardware Item Codes
#define HW_ITEM_GENESIS "GENESIS"
#define HW_ITEM_DETONATOR "DETONATOR"
#define HW_ITEM_PROBE "PROBE"
#define HW_ITEM_CLOAK "CLOAK"
#define HW_ITEM_TWARP "TWARP"
#define HW_ITEM_PSCANNER "PSCANNER"
#define HW_ITEM_LSCANNER "LSCANNER"
// General Hardware Constants
#define HW_MAX_PER_SHIP_DEFAULT -1
#define HW_MIN_QUANTITY 1
// Helper for stringification of macros
#define QUOTE(name) #name
// Function prototypes for shipyard commands
int cmd_shipyard_list (client_ctx_t *ctx, json_t *root);
int cmd_shipyard_upgrade (client_ctx_t *ctx, json_t *root);
// Function prototypes for shipyard commands
int cmd_shipyard_list (client_ctx_t *ctx, json_t *root);
int cmd_shipyard_upgrade (client_ctx_t *ctx, json_t *root);
// Function prototypes for hardware-related commands
int cmd_hardware_list (client_ctx_t *ctx, json_t *root);
int cmd_hardware_buy (client_ctx_t *ctx, json_t *root);
// Function prototypes for Tavern commands
int cmd_tavern_lottery_buy_ticket (client_ctx_t *ctx, json_t *root);
int cmd_tavern_lottery_status (client_ctx_t *ctx, json_t *root);
int cmd_tavern_deadpool_place_bet (client_ctx_t *ctx, json_t *root);
int cmd_tavern_dice_play (client_ctx_t *ctx, json_t *root);
int cmd_tavern_highstakes_play (client_ctx_t *ctx, json_t *root);
int cmd_tavern_raffle_buy_ticket (client_ctx_t *ctx, json_t *root);
int cmd_tavern_trader_buy_password (client_ctx_t *ctx, json_t *root);
int cmd_tavern_graffiti_post (client_ctx_t *ctx, json_t *root);
int cmd_tavern_round_buy (client_ctx_t *ctx, json_t *root);
int cmd_tavern_loan_take (client_ctx_t *ctx, json_t *root);
int cmd_tavern_loan_pay (client_ctx_t *ctx, json_t *root);
int cmd_tavern_rumour_get_hint (client_ctx_t *ctx, json_t *root);
int cmd_tavern_barcharts_get_prices_summary (client_ctx_t *ctx,
                                             json_t *root);
// Tavern Settings
struct tavern_settings
{
  int max_bet_per_transaction;
  int daily_max_wager;
  int enable_dynamic_wager_limit;
  int graffiti_max_posts;
  int notice_expires_days;
  int buy_round_cost;
  int buy_round_alignment_gain;
  int loan_shark_enabled;
};
extern struct tavern_settings g_tavern_cfg;
int tavern_settings_load (void);
// Helper function declarations
bool get_player_loan (sqlite3 *db, int player_id, long long *principal,
                      int *interest_rate, int *due_date, int *is_defaulted);
int apply_loan_interest (sqlite3 *db, int player_id,
                         long long current_principal, int interest_rate_bp);
bool check_loan_default (sqlite3 *db, int player_id, int current_time);
#endif // SERVER_STARDOCK_H
