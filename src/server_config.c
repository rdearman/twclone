#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include <jansson.h>
#include <ctype.h>
#include <string.h>
#include <jansson.h>
#include <sqlite3.h>
/* local inlcudes */
#include "server_config.h"
#include "database.h"
#include "server_loop.h"
#include "errors.h"
#include "config.h"
#include "schemas.h"
#include "server_envelope.h"
#include "server_config.h"



void send_enveloped_ok (int fd, json_t * root, const char *type,
			json_t * data);

// exported by server_loop.c
void loop_get_supported_commands(const cmd_desc_t **out_tbl, size_t *out_n);


static int
resolve_current_sector_from_info (json_t *info_obj, int fallback)
{
  if (!json_is_object (info_obj))
    return fallback;

  /* Preferred flat field */
  json_t *j = json_object_get (info_obj, "current_sector");
  if (json_is_integer (j))
    return (int) json_integer_value (j);

  /* Common alternates */
  json_t *ship = json_object_get (info_obj, "ship");
  if (json_is_object (ship))
    {
      j = json_object_get (ship, "sector_id");
      if (json_is_integer (j))
	return (int) json_integer_value (j);
    }
  json_t *player = json_object_get (info_obj, "player");
  if (json_is_object (player))
    {
      j = json_object_get (player, "sector_id");
      if (json_is_integer (j))
	return (int) json_integer_value (j);
    }
  return fallback;
}

/////////////////////////// NEW 

static json_t *
make_session_hello_payload (int is_authed, int player_id, int sector_id)
{
  json_t *payload = json_object();
  json_object_set_new(payload, "protocol_version", json_string("1.0"));
  json_object_set_new(payload, "server_time_unix", json_integer((json_int_t)time(NULL)));
  json_object_set_new(payload, "authenticated", is_authed ? json_true() : json_false());
  if (is_authed) {
    json_object_set_new(payload, "player_id", json_integer(player_id));
    json_object_set_new(payload, "current_sector", sector_id > 0 ? json_integer(sector_id) : json_null());
  } else {
    json_object_set_new(payload, "player_id", json_null());
    json_object_set_new(payload, "current_sector", json_null());
  }
  return payload;
}


/* ---------- system.hello ---------- */
int
cmd_system_hello (client_ctx_t *ctx, json_t *root)
{
  // alias to session.hello
  return cmd_session_hello(ctx, root);
}

//////////////////

// Define the handler function
static void
handle_system_capabilities (int fd, json_t *root)
{
  send_enveloped_ok (fd, root, "system.capabilities",
		     json_incref (g_capabilities));
}


/* /\* ---------- system.describe_schema (optional) ---------- *\/ */
/* int */
/* cmd_system_describe_schema (client_ctx_t *ctx, json_t *root) */
/* { */
/*   json_t *jdata = json_object_get (root, "data"); */
/*   const char *key = NULL; */
/*   if (json_is_object (jdata)) */
/*     { */
/*       json_t *jkey = json_object_get (jdata, "key"); */
/*       if (json_is_string (jkey)) */
/* 	key = json_string_value (jkey); */
/*     } */

/*   if (!key) */
/*     { */
/*       /\* Return the list of keys we have *\/ */
/*       json_t *data = json_pack ("{s:o}", "available", schema_keys ()); */
/*       send_enveloped_ok (ctx->fd, root, "system.schema_list", data); */
/*       json_decref (data); */
/*     } */
/*   else */
/*     { */
/*       json_t *schema = schema_get (key); */
/*       if (!schema) */
/* 	{ */
/* 	  send_enveloped_error (ctx->fd, root, 1306, "Schema not found"); */
/* 	} */
/*       else */
/* 	{ */
/* 	  json_t *data = */
/* 	    json_pack ("{s:s, s:o}", "key", key, "schema", schema); */
/* 	  send_enveloped_ok (ctx->fd, root, "system.schema", data); */
/* 	  json_decref (schema); */
/* 	  json_decref (data); */
/* 	} */
/*     } */

/*   return 0; */
/* } */

/* ---------- session.ping ---------- */
int
cmd_session_ping (client_ctx_t *ctx, json_t *root)
{
  /* Echo back whatever is in data (or {}) */
  json_t *jdata = json_object_get (root, "data");
  json_t *echo =
    json_is_object (jdata) ? json_incref (jdata) : json_object ();
  send_enveloped_ok (ctx->fd, root, "session.pong", echo);
  json_decref (echo);
  return 0;
}


int cmd_session_hello(client_ctx_t *ctx, json_t *root) {
  const char *req_id = json_string_value(json_object_get(root, "id"));

  json_t *payload = make_session_hello_payload((ctx->player_id > 0),
                                               ctx->player_id,
                                               ctx->sector_id);

  // Use ONE helper that builds a proper envelope including reply_to + status.
  // If your send_enveloped_ok doesn't add reply_to, fix it (next section).
  send_enveloped_ok(ctx->fd, root, "session.hello", payload);

  json_decref(payload);
  return 0;      // IMPORTANT: do not send another frame after this
}

int
cmd_session_disconnect (client_ctx_t *ctx, json_t *root)
{
  json_t *data = json_pack ("{s:s}", "message", "Goodbye");
  send_enveloped_ok (ctx->fd, root, "system.goodbye", data);
  json_decref (data);

  shutdown (ctx->fd, SHUT_RDWR);
  close (ctx->fd);
  return 0;			/* or break your per-connection loop appropriately */
}
