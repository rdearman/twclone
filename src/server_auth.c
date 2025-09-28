#include "server_auth.h"
#include "errors.h"
#include "config.h"
#include <string.h>
#include <jansson.h>
#include "server_cmds.h"
#include "server_envelope.h"


int
cmd_auth_login (client_ctx_t *ctx, json_t *root)
{
  /* NEW: pull fields from the "data" object, not the root */
  json_t *jdata = json_object_get (root, "data");
  const char *name = NULL, *pass = NULL;

  if (json_is_object (jdata))
    {
      /* Accept both "user_name" (new) and "player_name" (legacy) */
      json_t *jname = json_object_get (jdata, "user_name");
      if (!json_is_string (jname))
	jname = json_object_get (jdata, "player_name");
      json_t *jpass = json_object_get (jdata, "password");

      name = json_is_string (jname) ? json_string_value (jname) : NULL;
      pass = json_is_string (jpass) ? json_string_value (jpass) : NULL;
    }

  if (!name || !pass)
    {
      send_enveloped_error (ctx->fd, root, AUTH_ERR_BAD_REQUEST,
			    "Missing required field");
    }
  else
    {
      int player_id = 0;
      int rc = play_login (name, pass, &player_id);
      if (rc == AUTH_OK)
	{
	  /* Read canonical sector from DB; fall back to 1 if missing/NULL */
	  int sector_id = 0;
	  if (db_player_get_sector (player_id, &sector_id) != SQLITE_OK
	      || sector_id <= 0)
	    sector_id = 1;

	  /* Mark connection as authenticated and sync session state */
	  ctx->player_id = player_id;
	  ctx->sector_id = sector_id;	/* <-- set unconditionally on login */

	  /* Reply with session info, including current_sector */
	  json_t *data = json_pack ("{s:i, s:i}",
				    "player_id", player_id,
				    "current_sector", sector_id);
	  if (!data)
	    {
	      send_enveloped_error (ctx->fd, root, 1500, "Out of memory");
	      return;
	    }
	  send_enveloped_ok (ctx->fd, root, "auth.session", data);
	  json_decref (data);
	}
      else if (rc == AUTH_ERR_INVALID_CRED)
	{
	  send_enveloped_error (ctx->fd, root, AUTH_ERR_INVALID_CRED,
				"Invalid credentials");
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, AUTH_ERR_DB, "Database error");
	}
    }

  return 0;
}


/* ---------- auth.register ---------- */
int
cmd_auth_register (client_ctx_t *ctx, json_t *root)
{
  json_t *jdata = json_object_get (root, "data");
  const char *name = NULL, *pass = NULL;
  if (json_is_object (jdata))
    {
      json_t *jn = json_object_get (jdata, "user_name");
      json_t *jp = json_object_get (jdata, "password");
      if (!json_is_string (jn))
	jn = json_object_get (jdata, "player_name");
      name = json_is_string (jn) ? json_string_value (jn) : NULL;
      pass = json_is_string (jp) ? json_string_value (jp) : NULL;
    }
  if (!name || !pass)
    {
      send_enveloped_error (ctx->fd, root, 1301, "Missing required field");
    }
  else
    {
      int player_id = 0;
      int rc = user_create (name, pass, &player_id);
      if (rc == AUTH_OK)
	{
	  /* issue a session token (24h = 86400s) */
	  char tok[65];
	  if (db_session_create (player_id, 86400, tok) != SQLITE_OK)
	    {
	      send_enveloped_error (ctx->fd, root, 1500, "Database error");
	    }
	  else
	    {
	      ctx->player_id = player_id;
	      if (ctx->sector_id <= 0)
		ctx->sector_id = 1;
	      json_t *data = json_pack ("{s:i, s:s}", "player_id", player_id,
					"session_token", tok);
	      send_enveloped_ok (ctx->fd, root, "auth.session", data);
	      json_decref (data);
	    }
	}
      else if (rc == AUTH_ERR_NAME_TAKEN)
	{
	  send_enveloped_error (ctx->fd, root, AUTH_ERR_NAME_TAKEN,
				"Username already exists");
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, AUTH_ERR_DB, "Database error");
	}
    }
  return 0;
}

/* ---------- auth.logout ---------- */
int
cmd_auth_logout (client_ctx_t *ctx, json_t *root)
{
  json_t *jdata = json_object_get (root, "data");
  const char *tok = NULL;
  if (json_is_object (jdata))
    {
      json_t *jt = json_object_get (jdata, "session_token");
      if (json_is_string (jt))
	tok = json_string_value (jt);
    }
  /* If no token provided, log out the connection; if token given, revoke it. */
  if (tok)
    (void) db_session_revoke (tok);
  ctx->player_id = 0;		/* drop connection auth state */

  json_t *data = json_pack ("{s:s}", "message", "Logged out");
  send_enveloped_ok (ctx->fd, root, "auth.logged_out", data);
  json_decref (data);
  return 0;
}

int
cmd_user_create (client_ctx_t *ctx, json_t *root)
{
  json_t *jdata = json_object_get (root, "data");
  const char *name = NULL, *pass = NULL;
  if (json_is_object (jdata))
    {
      json_t *jname = json_object_get (jdata, "user_name");
      if (!json_is_string (jname))
	jname = json_object_get (jdata, "player_name");
      json_t *jpass = json_object_get (jdata, "password");
      name = json_is_string (jname) ? json_string_value (jname) : NULL;
      pass = json_is_string (jpass) ? json_string_value (jpass) : NULL;
    }

  if (!name || !pass)
    {
      send_enveloped_error (ctx->fd, root, AUTH_ERR_BAD_REQUEST,
			    "Missing required field");
    }
  else
    {
      int player_id = 0;
      int rc = user_create (name, pass, &player_id);
      if (rc == AUTH_OK)
	{
	  json_t *data = json_pack ("{s:i}", "player_id", player_id);
	  send_enveloped_ok (ctx->fd, root, "user.created", data);
	  json_decref (data);
	}
      else if (rc == AUTH_ERR_NAME_TAKEN)
	{
	  send_enveloped_error (ctx->fd, root, AUTH_ERR_NAME_TAKEN,
				"Username already exists");
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, AUTH_ERR_DB, "Database error");
	}
    }
  return 0;
}

int
cmd_auth_refresh (client_ctx_t *ctx, json_t *root)
{
  /* Try data.session_token */
  json_t *jdata = json_object_get (root, "data");
  const char *tok = NULL;
  if (json_is_object (jdata))
    {
      json_t *jt = json_object_get (jdata, "session_token");
      if (json_is_string (jt))
	tok = json_string_value (jt);
    }

  /* Try meta.session_token */
  if (!tok)
    {
      json_t *jmeta = json_object_get (root, "meta");
      if (json_is_object (jmeta))
	{
	  json_t *jt = json_object_get (jmeta, "session_token");
	  if (json_is_string (jt))
	    tok = json_string_value (jt);
	}
    }

  /* If still no token, fall back to the connection’s logged-in player */
  if (!tok && ctx->player_id > 0)
    {
      char newtok[65];
      if (db_session_create (ctx->player_id, 86400, newtok) != SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 1500, "Database error");
	}
      else
	{
	  json_t *data = json_pack ("{s:i, s:s}", "player_id", ctx->player_id,
				    "session_token", newtok);
	  send_enveloped_ok (ctx->fd, root, "auth.session", data);
	  json_decref (data);
	}
    }
  else if (tok)
    {
      /* Rotate provided token */
      char newtok[65];
      int pid = 0;
      int rc = db_session_refresh (tok, 86400, newtok, &pid);
      if (rc == SQLITE_OK)
	{
	  ctx->player_id = pid;
	  if (ctx->sector_id <= 0)
	    ctx->sector_id = 1;
	  json_t *data =
	    json_pack ("{s:i, s:s}", "player_id", pid, "session_token",
		       newtok);
	  send_enveloped_ok (ctx->fd, root, "auth.session", data);
	  json_decref (data);
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, 1401,
				"Invalid or expired session");
	}
    }
  else
    {
      /* Not logged in and no token supplied */
      send_enveloped_error (ctx->fd, root, 1301, "Missing required field");
    }
  return 0;
}


int
cmd_auth_mfa_totp_verify (client_ctx_t *ctx, json_t *root)
{
  // You can parse the code now if you like, but we’ll NIY this for the moment.
  // json_t *data = json_object_get(root, "data");
  // const char *code = data && json_is_string(json_object_get(data, "code"))
  //     ? json_string_value(json_object_get(data, "code")) : NULL;

  send_enveloped_error (ctx->fd, root, 1101,
			"Not implemented: auth.mfa.totp.verify");
  return 0;
}
