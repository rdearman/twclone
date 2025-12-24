/* src/server_auth.c */
#include <string.h>
#include <jansson.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

/* local includes */
#include "server_auth.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_envelope.h"
#include "server_players.h"
#include "server_config.h"
#include "database.h"
#include "game_db.h"
#include "db_player_settings.h"
#include "server_log.h"
#include "db/db_api.h"

int cmd_auth_login (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle(); if (!db) return -1;
  json_t *data = json_object_get(root, "data");
  const char *user = json_string_value(json_object_get(data, "username"));
  const char *pass = json_string_value(json_object_get(data, "passwd"));
  if (!user || !pass) { send_response_error(ctx, root, AUTH_ERR_BAD_REQUEST, "Missing fields"); return 0; }

  int pid = 0;
  if (play_login(user, pass, &pid) == AUTH_OK) {
      if (h_player_is_npc(db, pid)) { send_response_error(ctx, root, ERR_IS_NPC, "NPC login denied"); return 0; }
      ctx->player_id = pid;
      int sid = 1; db_player_get_sector(db, pid, &sid); ctx->sector_id = sid;
      char tok[65]; db_session_create(pid, 86400, tok);
      json_t *resp = json_object(); json_object_set_new(resp, "session", json_string(tok));
      json_object_set_new(resp, "sector_id", json_integer(sid));
      send_response_ok_take(ctx, root, "auth.session", &resp);
  } else send_response_error(ctx, root, AUTH_ERR_INVALID_CRED, "Invalid login");
  return 0;
}

int cmd_auth_register (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle(); if (!db) return -1;
  json_t *data = json_object_get(root, "data");
  const char *user = json_string_value(json_object_get(data, "username"));
  const char *pass = json_string_value(json_object_get(data, "passwd"));
  if (!user || !pass) { send_response_error(ctx, root, ERR_MISSING_FIELD, "Missing fields"); return 0; }

  int pid = 0;
  if (user_create(db, user, pass, &pid) == AUTH_OK) {
      spawn_starter_ship(db, pid, (rand() % 10) + 1);
      char tok[65]; db_session_create(pid, 86400, tok);
      json_t *resp = json_object(); json_object_set_new(resp, "player_id", json_integer(pid));
      json_object_set_new(resp, "session_token", json_string(tok));
      send_response_ok_take(ctx, root, "auth.session", &resp);
  } else send_response_error(ctx, root, ERR_NAME_TAKEN, "Registration failed");
  return 0;
}

int cmd_auth_logout (client_ctx_t *ctx, json_t *root)
{
  ctx->player_id = 0; send_response_ok_borrow(ctx, root, "auth.logged_out", NULL); return 0;
}

int cmd_user_create (client_ctx_t *ctx, json_t *root) { return cmd_auth_register(ctx, root); }
int cmd_auth_refresh (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
int cmd_auth_mfa_totp_verify (client_ctx_t *ctx, json_t *root) { (void)ctx; (void)root; return 0; }
const char * get_welcome_message (int pid) { (void)pid; return "Welcome"; }
