#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <jansson.h>

#include "common.h"
#include "db/db_api.h"
#include "game_db.h"
#include "repo_combat.h"
#include "server_combat.h"
#include "server_envelope.h"

static int g_load_fail_on_call;
static int g_load_calls;
static int g_persist_rc;
static int g_begin_ok;
static int g_commit_ok;
static int g_rollback_calls;
static int g_success_responses;
static int g_error_responses;
static int g_last_error;
static int g_destroy_rc;
static int g_create_pod_rc;
static int g_update_pod_rc;
static int g_ship_destroyed;
static int g_pod_active;
static int g_message_calls;
static int g_commit_calls;
static int g_db_available;

int handle_ship_attack (client_ctx_t *ctx, json_t *root, json_t *data, db_t *db);

db_t *
game_db_get_handle (void)
{
  return g_db_available ? (db_t *) 1 : NULL;
}

int
h_get_active_ship_id (db_t *db, int player_id)
{
  (void) db;
  (void) player_id;
  return 10;
}

bool
json_get_int_flexible (json_t *obj, const char *key, int *out)
{
  json_t *value = json_object_get (obj, key);
  if (!json_is_integer (value))
    return false;
  *out = (int) json_integer_value (value);
  return true;
}

int
db_combat_load_ship_full_locked (db_t *db, int ship_id,
                                 repo_combat_ship_full_t *out,
                                 bool skip_locked)
{
  (void) db;
  (void) skip_locked;
  g_load_calls++;
  if (g_load_calls == g_load_fail_on_call)
    return -1;

  memset (out, 0, sizeof (*out));
  out->id = ship_id;
  out->hull = 100;
  out->shields = 10;
  out->sector = 11;
  return 0;
}

int
db_combat_persist_ship_damage (db_t *db, int ship_id, int hull, int shields,
                               int fighters_lost)
{
  (void) db;
  (void) ship_id;
  (void) hull;
  (void) shields;
  (void) fighters_lost;
  return g_persist_rc;
}

bool
db_tx_begin (db_t *db, db_tx_flags_t flags, db_error_t *err)
{
  (void) db;
  (void) flags;
  if (!g_begin_ok && err)
    {
      err->code = ERR_DB_TX;
      strcpy (err->message, "begin failed");
    }
  return g_begin_ok;
}

bool
db_tx_commit (db_t *db, db_error_t *err)
{
  (void) db;
  g_commit_calls++;
  if (!g_commit_ok && err)
    {
      err->code = ERR_DB_TX;
      strcpy (err->message, "commit failed");
    }
  return g_commit_ok;
}

bool
db_tx_rollback (db_t *db, db_error_t *err)
{
  (void) db;
  (void) err;
  g_rollback_calls++;
  g_ship_destroyed = 0;
  g_pod_active = 0;
  return true;
}

void
send_response_error (client_ctx_t *ctx, json_t *req, int code,
                     const char *message)
{
  (void) ctx;
  (void) req;
  (void) message;
  g_error_responses++;
  g_last_error = code;
}

void
send_response_ok_take (client_ctx_t *ctx, json_t *req, const char *type,
                       json_t **data)
{
  (void) ctx;
  (void) req;
  assert (strcmp (type, "combat.attack.result") == 0);
  assert (json_is_true (json_object_get (*data, "success")));
  g_success_responses++;
  json_decref (*data);
  *data = NULL;
}

void
server_log_printf (int priority, const char *format, ...)
{
  (void) priority;
  (void) format;
}

int
db_destroy_ship (db_t *db, int player_id, int ship_id)
{
  (void) db;
  (void) player_id;
  (void) ship_id;
  if (g_destroy_rc == 0)
    g_ship_destroyed = 1;
  return g_destroy_rc;
}

int
db_create_podded_status_entry (db_t *db, int player_id)
{
  (void) db;
  (void) player_id;
  return g_create_pod_rc;
}

int
db_update_player_podded_status (db_t *db, int player_id, const char *status,
                                int value)
{
  (void) db;
  (void) player_id;
  (void) status;
  (void) value;
  if (g_update_pod_rc == 0)
    g_pod_active = 1;
  return g_update_pod_rc;
}

int
h_send_message_to_player (db_t *db, int player_id, int channel,
                          const char *subject, const char *message)
{
  (void) db;
  (void) player_id;
  (void) channel;
  (void) subject;
  (void) message;
  g_message_calls++;
  return 0;
}

static void
reset_case (void)
{
  g_load_fail_on_call = 0;
  g_load_calls = 0;
  g_persist_rc = 0;
  g_begin_ok = 1;
  g_commit_ok = 1;
  g_rollback_calls = 0;
  g_success_responses = 0;
  g_error_responses = 0;
  g_last_error = 0;
  g_destroy_rc = 0;
  g_create_pod_rc = 0;
  g_update_pod_rc = 0;
  g_ship_destroyed = 0;
  g_pod_active = 0;
  g_message_calls = 0;
  g_commit_calls = 0;
  g_db_available = 1;
}

static void
run_attack (void)
{
  client_ctx_t ctx = { 0 };
  ctx.player_id = 7;
  ctx.sector_id = 11;
  json_t *root = json_object ();
  json_t *data = json_pack ("{s:i}", "target_ship_id", 20);
  assert (root && data);
  handle_ship_attack (&ctx, root, data, (db_t *) 1);
  json_decref (root);
  json_decref (data);
}

static void
run_fedspace_attack (void)
{
  client_ctx_t ctx = { 0 };
  ctx.player_id = 7;
  ctx.sector_id = 3;
  json_t *root = json_object ();
  json_t *data = json_pack ("{s:i}", "target_ship_id", 20);
  assert (root && data);
  handle_ship_attack (&ctx, root, data, (db_t *) 1);
  json_decref (root);
  json_decref (data);
}

int
main (void)
{
  reset_case ();
  g_begin_ok = 0;
  run_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_DB_TX && g_rollback_calls == 0);

  reset_case ();
  g_load_fail_on_call = 1; /* attacker repository read failure */
  run_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_DB_QUERY_FAILED && g_rollback_calls == 1);

  reset_case ();
  g_load_fail_on_call = 2; /* target repository read failure */
  run_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_DB_QUERY_FAILED && g_rollback_calls == 1);

  reset_case ();
  g_persist_rc = -1;
  run_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_DB_EXEC_FAILED && g_rollback_calls == 1);

  reset_case ();
  g_commit_ok = 0;
  run_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_DB_TX && g_rollback_calls == 1);

  reset_case ();
  run_attack ();
  assert (g_success_responses == 1 && g_error_responses == 0);
  assert (g_load_calls == 2 && g_rollback_calls == 0);

  reset_case ();
  g_begin_ok = 0;
  run_fedspace_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_DB_TX && g_rollback_calls == 0);
  assert (!g_ship_destroyed && !g_pod_active && g_message_calls == 0);

  reset_case ();
  g_db_available = 0;
  run_fedspace_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_DB_CLOSED && g_rollback_calls == 0);
  assert (!g_ship_destroyed && !g_pod_active && g_message_calls == 0);

  reset_case ();
  g_destroy_rc = -1;
  run_fedspace_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_DB_EXEC_FAILED && g_rollback_calls == 1);
  assert (!g_ship_destroyed && !g_pod_active && g_message_calls == 0);

  reset_case ();
  g_create_pod_rc = -1;
  run_fedspace_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_DB_EXEC_FAILED && g_rollback_calls == 1);
  assert (!g_ship_destroyed && !g_pod_active && g_message_calls == 0);

  reset_case ();
  g_update_pod_rc = -1;
  run_fedspace_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_DB_EXEC_FAILED && g_rollback_calls == 1);
  assert (!g_ship_destroyed && !g_pod_active && g_message_calls == 0);

  reset_case ();
  g_commit_ok = 0;
  run_fedspace_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_DB_TX && g_rollback_calls == 1);
  assert (!g_ship_destroyed && !g_pod_active && g_message_calls == 0);

  reset_case ();
  run_fedspace_attack ();
  assert (g_success_responses == 0 && g_error_responses == 1);
  assert (g_last_error == ERR_PERMISSION_DENIED && g_rollback_calls == 0);
  assert (g_ship_destroyed && g_pod_active && g_message_calls == 1);
  assert (g_commit_calls == 1);

  puts ("Combat attack failure handling regression passed.");
  return 0;
}
