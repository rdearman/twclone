#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <jansson.h>

#include "db/db_api.h"
#include "db/repo/repo_cmd.h"
#include "game_db.h"
#include "server_envelope.h"
#include "server_s2s.h"

static json_t *g_sent_env;
static int g_accept_calls;

db_t *
game_db_get_handle (void)
{
  return (db_t *) 1;
}

int
db_commands_accept (db_t *db, const char *cmd_type, const char *idem_key,
                    json_t *payload, int *cmd_id, int *duplicate,
                    int *due_at)
{
  assert (db == (db_t *) 1);
  assert (strcmp (cmd_type, "notice.publish") == 0);
  assert (strcmp (idem_key, "s2s-dispatch-regression") == 0);
  assert (json_is_object (payload));
  assert (json_is_string (json_object_get (payload, "message")));
  g_accept_calls++;
  *cmd_id = 731;
  *duplicate = 0;
  *due_at = 12345;
  return 0;
}

const char *
s2s_env_type (json_t *env)
{
  return json_string_value (json_object_get (env, "type"));
}

const char *
s2s_env_id (json_t *env)
{
  return json_string_value (json_object_get (env, "id"));
}

json_t *
s2s_env_payload (json_t *env)
{
  return json_object_get (env, "payload");
}

json_t *
s2s_make_ack (const char *src, const char *dst, const char *ack_of,
               json_t *payload)
{
  json_t *ack = json_object ();
  json_object_set_new (ack, "type", json_string ("s2s.ack"));
  json_object_set_new (ack, "src", json_string (src));
  json_object_set_new (ack, "dst", json_string (dst));
  json_object_set_new (ack, "ack_of", json_string (ack_of));
  json_object_set (ack, "payload", payload);
  return ack;
}

json_t *
s2s_make_error (const char *src, const char *dst, const char *ack_of,
                const char *code, const char *message, json_t *details)
{
  (void) src; (void) dst; (void) ack_of; (void) code; (void) message;
  (void) details;
  return json_pack ("{s:s}", "type", "s2s.error");
}

int
s2s_send_env (s2s_conn_t *conn, json_t *env, int timeout_ms)
{
  (void) conn; (void) timeout_ms;
  json_decref (g_sent_env);
  g_sent_env = json_deep_copy (env);
  return g_sent_env ? 0 : -1;
}

void
server_log_printf (int priority, const char *fmt, ...)
{
  (void) priority; (void) fmt;
}

int
main (void)
{
  json_t *cmd_payload = json_pack ("{s:s}", "message", "dispatch regression");
  json_t *payload = json_pack ("{s:s,s:s,s:o}",
                               "cmd_type", "notice.publish",
                               "idem_key", "s2s-dispatch-regression",
                               "payload", cmd_payload);
  json_t *env = json_pack ("{s:s,s:s,s:o}",
                           "type", "s2s.command.push",
                           "id", "dispatch-test-1",
                           "payload", payload);

  assert (server_s2s_dispatch ((s2s_conn_t *) 1, env) == 0);
  assert (g_accept_calls == 1);
  assert (g_sent_env != NULL);
  assert (strcmp (s2s_env_type (g_sent_env), "s2s.ack") == 0);
  json_t *ack_payload = s2s_env_payload (g_sent_env);
  assert (json_is_true (json_object_get (ack_payload, "accepted")));
  assert (json_is_false (json_object_get (ack_payload, "duplicate")));
  assert (json_integer_value (json_object_get (ack_payload, "cmd_id")) == 731);
  assert (strcmp (json_string_value (json_object_get (ack_payload, "status")),
                  "ready") == 0);
  assert (json_integer_value (json_object_get (ack_payload, "due_at")) == 12345);

  json_decref (env);
  json_decref (g_sent_env);
  puts ("S2S command.push dispatcher regression passed.");
  return 0;
}
