#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <jansson.h>

#include "common.h"
#include "db/db_api.h"
#include "repo_universe.h"
#include "server_players.h"
#include "server_universe.h"

static int g_max_rc;
static int g_max_sector;
static int g_drive_rc;
static int g_has_drive;
static int g_fighter_rc;
static int g_has_fighter;
static int g_origin_sector;
static int g_distance_rc;
static int g_distance;
static int64_t g_fuel_available;
static int g_cargo_rc;
static int64_t g_fuel_consumed;
static int g_fuel_debits;
static int g_turn_debits;
static int g_sector_updates;
static int g_last_response_code;
static int g_success_responses;

db_t *game_db_get_handle(void) { return (db_t *) 1; }

bool json_get_int_flexible(json_t *obj, const char *key, int *out)
{
  json_t *value = json_object_get(obj, key);
  if (!json_is_integer(value)) return false;
  *out = (int) json_integer_value(value);
  return true;
}

int repo_universe_get_max_sector_id(db_t *db, int *out)
{
  (void) db;
  *out = g_max_sector;
  return g_max_rc;
}

int h_get_active_ship_id(db_t *db, int player_id)
{
  (void) db; (void) player_id;
  return 99;
}

int repo_universe_check_transwarp(db_t *db, int ship_id, int *enabled)
{
  (void) db; (void) ship_id;
  *enabled = g_has_drive;
  return g_drive_rc;
}

int repo_universe_lock_sector_fighter(db_t *db, int sector_id, int *found)
{
  (void) db; (void) sector_id;
  *found = g_has_fighter;
  return g_fighter_rc;
}

int repo_universe_lock_player_ship_sector(db_t *db, int player_id,
                                          int expected_ship_id, int *sector)
{
  (void) db;
  assert(player_id == 7 && expected_ship_id == 99);
  *sector = g_origin_sector;
  return 0;
}

int repo_universe_get_transwarp_distance(db_t *db, int from, int to,
                                         int *distance)
{
  (void) db;
  assert(from == g_origin_sector && to == 20);
  *distance = g_distance;
  return g_distance_rc;
}

int repo_cargo_lock_ships_for_update(db_t *db, int first, int second)
{
  (void) db;
  assert(first == 99 && second == 99);
  return g_cargo_rc;
}

int repo_cargo_get(db_t *db, int ship_id, const char *commodity, int64_t *qty)
{
  (void) db;
  assert(ship_id == 99 && strcmp(commodity, "ORE") == 0);
  *qty = g_fuel_available;
  return g_cargo_rc;
}

int repo_cargo_consume_locked(db_t *db, int ship_id, const char *commodity,
                              int64_t quantity)
{
  (void) db;
  assert(ship_id == 99 && strcmp(commodity, "ORE") == 0);
  assert(quantity <= g_fuel_available);
  g_fuel_consumed = quantity;
  g_fuel_debits++;
  return 0;
}

bool db_tx_begin(db_t *db, db_tx_flags_t flags, db_error_t *err)
{
  (void) db; (void) flags; (void) err;
  return true;
}

bool db_tx_commit(db_t *db, db_error_t *err)
{
  (void) db; (void) err;
  return true;
}

bool db_tx_rollback(db_t *db, db_error_t *err)
{
  (void) db; (void) err;
  return true;
}

int repo_universe_move_player_and_ship(db_t *db, int player_id,
                                       int expected_ship_id, int sector_id)
{
  (void) db; (void) player_id; (void) expected_ship_id; (void) sector_id;
  g_sector_updates++;
  return 0;
}

TurnConsumeResult h_consume_player_turn(db_t *db, client_ctx_t *ctx, int turns)
{
  (void) db; (void) ctx;
  assert(turns == 1);
  g_turn_debits++;
  return TURN_CONSUME_SUCCESS;
}

int handle_turn_consumption_error(client_ctx_t *ctx,
                                  TurnConsumeResult result,
                                  const char *cmd, json_t *root,
                                  json_t *meta)
{
  (void) ctx; (void) result; (void) cmd; (void) root; (void) meta;
  return 0;
}

int db_player_set_sector(int player_id, int sector_id)
{
  (void) player_id; (void) sector_id;
  return 0;
}

int server_combat_apply_entry_hazards_report(db_t *db, client_ctx_t *ctx,
                                              int sector_id, json_t **report)
{
  (void) db; (void) ctx; (void) sector_id;
  *report = json_array();
  return 0;
}

void send_response_error(client_ctx_t *ctx, json_t *req, int code,
                         const char *message)
{
  (void) ctx; (void) req; (void) message;
  g_last_response_code = code;
}

void send_response_refused(client_ctx_t *ctx, json_t *req, int code,
                           const char *message, json_t *data)
{
  (void) ctx; (void) req; (void) message; (void) data;
  g_last_response_code = code;
}

void send_response_ok_take(client_ctx_t *ctx, json_t *req, const char *type,
                           json_t **data)
{
  (void) ctx; (void) req;
  assert(strcmp(type, "move.transwarp") == 0);
  assert(json_integer_value(json_object_get(*data, "distance_sectors")) == 3);
  assert(json_integer_value(json_object_get(*data, "fuel_ore_consumed")) == 9);
  g_success_responses++;
  json_decref(*data);
  *data = NULL;
}

void send_response_error_data(client_ctx_t *ctx, json_t *req, int code,
                              const char *message, json_t *data)
{
  (void) ctx; (void) req; (void) message; (void) data;
  g_last_response_code = code;
}

static json_t *request(int destination)
{
  json_t *root = json_object();
  json_t *data = json_object();
  json_object_set_new(data, "to_sector_id", json_integer(destination));
  json_object_set_new(root, "data", data);
  return root;
}

static void reset(void)
{
  g_max_rc = 0;
  g_max_sector = 680;
  g_drive_rc = 0;
  g_has_drive = 1;
  g_fighter_rc = 0;
  g_has_fighter = 1;
  g_origin_sector = 1;
  g_distance_rc = 0;
  g_distance = 3;
  g_fuel_available = 100;
  g_cargo_rc = 0;
  g_fuel_consumed = 0;
  g_fuel_debits = 0;
  g_turn_debits = 0;
  g_sector_updates = 0;
  g_last_response_code = 0;
  g_success_responses = 0;
}

int main(void)
{
  client_ctx_t ctx = {0};
  ctx.player_id = 7;

  reset();
  g_max_rc = -1;
  json_t *root = request(20);
  cmd_move_transwarp(&ctx, root);
  assert(g_last_response_code == ERR_DB);
  assert(g_turn_debits == 0 && g_sector_updates == 0);
  json_decref(root);

  reset();
  root = request(2147483647);
  cmd_move_transwarp(&ctx, root);
  assert(g_last_response_code == ERR_SECTOR_NOT_FOUND);
  assert(g_turn_debits == 0 && g_sector_updates == 0);
  json_decref(root);

  reset();
  g_drive_rc = -1;
  root = request(20);
  cmd_move_transwarp(&ctx, root);
  assert(g_last_response_code == ERR_DB);
  assert(g_turn_debits == 0 && g_sector_updates == 0);
  json_decref(root);

  reset();
  g_has_drive = 0;
  root = request(20);
  cmd_move_transwarp(&ctx, root);
  assert(g_last_response_code == REF_TRANSWARP_UNAVAILABLE);
  assert(g_turn_debits == 0 && g_sector_updates == 0);
  json_decref(root);

  reset();
  g_has_fighter = 0;
  root = request(20);
  cmd_move_transwarp(&ctx, root);
  assert(g_last_response_code == REF_TRANSWARP_UNAVAILABLE);
  assert(g_fuel_debits == 0 && g_turn_debits == 0 && g_sector_updates == 0);
  json_decref(root);

  reset();
  g_fighter_rc = -1;
  root = request(20);
  cmd_move_transwarp(&ctx, root);
  assert(g_last_response_code == ERR_DB);
  assert(g_fuel_debits == 0 && g_turn_debits == 0 && g_sector_updates == 0);
  json_decref(root);

  reset();
  g_distance_rc = REF_NO_WARP_LINK;
  root = request(20);
  cmd_move_transwarp(&ctx, root);
  assert(g_last_response_code == REF_NO_WARP_LINK);
  assert(g_fuel_debits == 0 && g_turn_debits == 0 && g_sector_updates == 0);
  json_decref(root);

  reset();
  g_fuel_available = 8;
  root = request(20);
  cmd_move_transwarp(&ctx, root);
  assert(g_last_response_code == REF_INSUFFICIENT_RES);
  assert(g_fuel_debits == 0 && g_turn_debits == 0 && g_sector_updates == 0);
  json_decref(root);

  reset();
  root = request(20);
  cmd_move_transwarp(&ctx, root);
  assert(g_turn_debits == 1 && g_sector_updates == 1);
  assert(g_fuel_debits == 1 && g_fuel_consumed == 9);
  assert(g_success_responses == 1);
  json_decref(root);

  puts("transwarp handler validation tests passed");
  return 0;
}
