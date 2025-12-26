/* src/server_stardock.c */
#include <jansson.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>

/* local includes */
#include "server_stardock.h"
#include "common.h"
#include "database.h"
#include "game_db.h"
#include "database_cmd.h"
#include "server_players.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_ports.h"
#include "server_ships.h"
#include "server_cmds.h"
#include "server_corporation.h"
#include "server_loop.h"
#include "server_config.h"
#include "server_log.h"
#include "server_cron.h"
#include "server_communication.h"
#include "db/db_api.h"


int
cmd_hardware_list (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_hardware_buy (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_shipyard_list (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_shipyard_upgrade (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
tavern_settings_load (void)
{
  return 0;
}


int
cmd_tavern_lottery_buy_ticket (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_lottery_status (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_deadpool_place_bet (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_dice_play (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_graffiti_post (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_highstakes_play (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_loan_pay (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_loan_take (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_raffle_buy_ticket (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_round_buy (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_rumour_get_hint (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_trader_buy_password (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_tavern_barcharts_get_prices_summary (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}


int
cmd_sys_cron_planet_tick_once (client_ctx_t *ctx, json_t *root)
{
  (void)ctx; (void)root; return 0;
}