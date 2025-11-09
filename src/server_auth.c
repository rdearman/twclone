#include <string.h>
#include <jansson.h>
#include <sqlite3.h>
#include <stdbool.h>
/* local includes */
#include "server_auth.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_envelope.h"
#include "database.h"
#include "db_player_settings.h"


static bool
player_is_sysop (sqlite3 *db, int player_id)
{
  bool is_sysop = false;
  sqlite3_stmt *st = NULL;

  if (sqlite3_prepare_v2 (db,
			  "SELECT COALESCE(type,2), COALESCE(flags,0) FROM players WHERE id=?1",
			  -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, player_id);
      if (sqlite3_step (st) == SQLITE_ROW)
	{
	  int type = sqlite3_column_int (st, 0);
	  int flags = sqlite3_column_int (st, 1);
	  if (type == 1 || (flags & 0x1))
	    is_sysop = true;	/* adjust rule if needed */
	}
    }
  if (st)
    sqlite3_finalize (st);
  return is_sysop;
}


/* --- login hydration: locked default subscriptions ----------------------- */

static int
subs_upsert_locked_defaults (sqlite3 *db, int player_id, bool is_sysop)
{
  int rc = SQLITE_OK;
  sqlite3_stmt *st = NULL;

  /* 1) 'global' */
  rc = sqlite3_prepare_v2 (db,
			   "INSERT INTO subscriptions(player_id, event_type, delivery, locked, enabled) "
			   "VALUES(?1, 'global', 'push', 1, 1) "
			   "ON CONFLICT(player_id, event_type) DO UPDATE SET locked=1, enabled=1;",
			   -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto done;
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  st = NULL;
  if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
      rc = SQLITE_ERROR;
      goto done;
    }
  rc = SQLITE_OK;

  /* 2) 'player.<id>' */
  char chan[64];
  snprintf (chan, sizeof (chan), "player.%d", player_id);
  rc = sqlite3_prepare_v2 (db,
			   "INSERT INTO subscriptions(player_id, event_type, delivery, locked, enabled) "
			   "VALUES(?1, ?2, 'push', 1, 1) "
			   "ON CONFLICT(player_id, event_type) DO UPDATE SET locked=1, enabled=1;",
			   -1, &st, NULL);
  if (rc != SQLITE_OK)
    goto done;
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_bind_text (st, 2, chan, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  st = NULL;
  if (rc != SQLITE_DONE && rc != SQLITE_ROW)
    {
      rc = SQLITE_ERROR;
      goto done;
    }
  rc = SQLITE_OK;

  /* 3) optional 'sysop' */
  if (is_sysop)
    {
      rc = sqlite3_prepare_v2 (db,
			       "INSERT INTO subscriptions(player_id, event_type, delivery, locked, enabled) "
			       "VALUES(?1, 'sysop', 'push', 1, 1) "
			       "ON CONFLICT(player_id, event_type) DO UPDATE SET locked=1, enabled=1;",
			       -1, &st, NULL);
      if (rc != SQLITE_OK)
	goto done;
      sqlite3_bind_int (st, 1, player_id);
      rc = sqlite3_step (st);
      sqlite3_finalize (st);
      st = NULL;
      if (rc != SQLITE_DONE && rc != SQLITE_ROW)
	{
	  rc = SQLITE_ERROR;
	  goto done;
	}
      rc = SQLITE_OK;
    }

done:
  if (st)
    sqlite3_finalize (st);
  return rc;
}


/* ---- hydration helpers (#194) ---------------------------------- */

/* Required, locked subscriptions (cannot be unsubscribed) */
static const char *k_required_locked_topics[] = {
  "system.notice",
  /* add more here if you need: "system.motd", ... */
};

/* Default prefs to seed if missing */
typedef struct
{
  const char *key;
  const char *type;		/* 'bool','int','string','json' */
  const char *value;		/* stored as TEXT; server enforces type */
} default_pref_t;

static const default_pref_t k_default_prefs[] = {
  {"ui.ansi", "bool", "true"},
  {"ui.clock_24h", "bool", "true"},
  {"ui.locale", "string", "en-GB"},
  {"ui.page_length", "int", "20"},
  {"privacy.dm_allowed", "bool", "true"},
  /* add more defaults as needed */
};

/* Upsert a locked=1, enabled=1 subscription; preserve lock with MAX() */
static int
upsert_locked_subscription (sqlite3 *db, int player_id, const char *topic)
{
  static const char *SQL =
    "INSERT INTO subscriptions(player_id,event_type,delivery,filter_json,locked,enabled) "
    "VALUES(?, ?, 'internal', NULL, 1, 1) "
    "ON CONFLICT(player_id, event_type) DO UPDATE SET "
    "  enabled=1, " "  locked=MAX(subscriptions.locked, excluded.locked)";

  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
    return -1;
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_bind_text (st, 2, topic, -1, SQLITE_STATIC);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}

/* Insert a default pref if missing (do not overwrite user choice) */
static int
insert_default_pref_if_missing (sqlite3 *db, int player_id,
				const char *key, const char *type,
				const char *value)
{
  static const char *SQL =
    "INSERT INTO player_prefs(player_id,key,type,value) "
    "SELECT ?, ?, ?, ? "
    "WHERE NOT EXISTS (SELECT 1 FROM player_prefs WHERE player_id=? AND key=?)";
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
    return -1;
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_bind_text (st, 2, key, -1, SQLITE_STATIC);
  sqlite3_bind_text (st, 3, type, -1, SQLITE_STATIC);
  sqlite3_bind_text (st, 4, value, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 5, player_id);
  sqlite3_bind_text (st, 6, key, -1, SQLITE_STATIC);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  return (rc == SQLITE_DONE) ? 0 : -1;
}

/* Run full hydration transactionally; ignore individual insert errors */
static void
hydrate_player_defaults (int player_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db || player_id <= 0)
    return;

  char *errmsg = NULL;
  (void) sqlite3_exec (db, "BEGIN", NULL, NULL, &errmsg);

  /* locked subs */
  for (size_t i = 0;
       i <
       sizeof (k_required_locked_topics) /
       sizeof (k_required_locked_topics[0]); ++i)
    {
      (void) upsert_locked_subscription (db, player_id,
					 k_required_locked_topics[i]);
    }

  /* default prefs (only if missing) */
  for (size_t i = 0;
       i < sizeof (k_default_prefs) / sizeof (k_default_prefs[0]); ++i)
    {
      (void) insert_default_pref_if_missing (db, player_id,
					     k_default_prefs[i].key,
					     k_default_prefs[i].type,
					     k_default_prefs[i].value);
    }

  (void) sqlite3_exec (db, "COMMIT", NULL, NULL, &errmsg);
}

//// CMD Handlers ////////


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

	  sqlite3 *dbh = db_get_handle ();
	  bool is_sysop = player_is_sysop (dbh, ctx->player_id);
	  int rc =
	    subs_upsert_locked_defaults (dbh, ctx->player_id, is_sysop);
	  if (rc != SQLITE_OK)
	    {
	      send_enveloped_error (ctx->fd, root, 1503,
				    "Database error (subs upsert)");
	      return 0;
	    }


	  /* Reply with session info, including current_sector */
	  json_t *data = json_pack ("{s:i, s:i}",
				    "player_id", player_id,
				    "current_sector", sector_id);
	  if (!data)
	    {
	      send_enveloped_error (ctx->fd, root, 1500, "Out of memory");
	      return 1;
	    }
	  /* Auto-subscribe this player to system.* on login (best-effort). */
	  {
	    sqlite3 *db = db_get_handle ();
	    sqlite3_stmt *st = NULL;
	    if (sqlite3_prepare_v2 (db,
				    "INSERT OR IGNORE INTO subscriptions(player_id,event_type,delivery,enabled) "
				    "VALUES(?1,'system.*','push',1);",
				    -1, &st, NULL) == SQLITE_OK)
	      {
		sqlite3_bind_int64 (st, 1, ctx->player_id);
		(void) sqlite3_step (st);	/* ignore result; UNIQUE prevents dupes */
	      }
	    if (st)
	      sqlite3_finalize (st);
	  }


	  {
	    char *cur = NULL;	// must be char*
	    if (db_prefs_get_one (ctx->player_id, "ui.locale", &cur) !=
		SQLITE_OK || !cur)
	      {
		(void) db_prefs_set_one (ctx->player_id, "ui.locale",
					 PT_STRING, "en-GB");
	      }
	    if (cur)
	      free (cur);
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

  hydrate_player_defaults (ctx->player_id);

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

	      /* Auto-subscribe this player to system.* on login (best-effort). */
	      {
		sqlite3 *db = db_get_handle ();
		sqlite3_stmt *st = NULL;
		if (sqlite3_prepare_v2 (db,
					"INSERT OR IGNORE INTO subscriptions(player_id,event_type,delivery,enabled) "
					"VALUES(?1,'system.*','push',1);",
					-1, &st, NULL) == SQLITE_OK)
		  {
		    sqlite3_bind_int64 (st, 1, ctx->player_id);
		    (void) sqlite3_step (st);	/* ignore result; UNIQUE prevents dupes */
		  }
		if (st)
		  sqlite3_finalize (st);
	      }


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

  hydrate_player_defaults (ctx->player_id);

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
  int pid = 0; /* Declare pid at function scope */
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
      sqlite3 *dbh = db_get_handle ();
      bool is_sysop = player_is_sysop (dbh, ctx->player_id);
      int rc = subs_upsert_locked_defaults (dbh, ctx->player_id, is_sysop);
      if (rc != SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 1503,
				"Database error (subs upsert)");
	  return 0;
	}

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
      sqlite3 *dbh = db_get_handle ();
      bool is_sysop = player_is_sysop (dbh, pid);
      if (subs_upsert_locked_defaults (dbh, pid, is_sysop) != SQLITE_OK)
	{
	  send_enveloped_error (ctx->fd, root, 1503,
				"Database error (subs upsert)");
	  return 0;
	}

      /* Rotate provided token */
      char newtok[65];
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
