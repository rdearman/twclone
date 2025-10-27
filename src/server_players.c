#include <sqlite3.h>
#include <stdio.h>
#include <stdint.h>
#include <jansson.h>
#include <string.h>
#include <stdlib.h>		/* for strtol */
#include <sqlite3.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
// local includes
#include "server_players.h"
#include "database.h"		// play_login, user_create, db_player_info_json, db_player_get_sector, db_session_*
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_rules.h"
#include "db_player_settings.h"
#include "server_envelope.h"
#include "server_auth.h"
#include "server_envelope.h"
#include "server_log.h"


// Assume this external function provides the database handle.
extern sqlite3 *db_get_handle(void);

#include <sqlite3.h>
#include <time.h>
#include <stdio.h>

// Assuming these are defined elsewhere
extern sqlite3 *db_get_handle(void);
// Assuming SQLITE_TRANSIENT is defined (it is standard SQLite define)

/** * Sends a complex mail message to a specific player, including a sender ID and subject.
 * The 'mail' table is assumed to now include sender_id, subject, and a 'read' status.
 *
 * @param player_id The ID of the player to receive the message (the recipient). 
 * @param sender_id The ID of the message sender (use 0 or a specific system ID for system messages).
 * @param subject The subject line of the mail message.
 * @param message The text content of the message body. 
 * @return 0 on success, or non-zero on error. 
 */ 
int h_send_message_to_player(int player_id, int sender_id, const char *subject, const char *message) 
{ 
    sqlite3 *db = db_get_handle(); 
    sqlite3_stmt *st = NULL; 
    int rc; 

    // Use current Unix timestamp for the message time 
    int timestamp = (int)time(NULL); 

    // Updated SQL statement to insert recipient_id (player_id), sender_id, timestamp, 
    // subject, message, and set 'read' status to 0 (unread).
    const char *sql =
        "INSERT INTO mail (sender_id, recipient_id, subject, body) "
        "VALUES (?, ?, ?, ?);";
    
    rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL); 

    if (rc != SQLITE_OK) { 
      LOGE( "SQL error preparing complex message insert: %s\n", sqlite3_errmsg(db)); 
      return 1; 
    } 

    // Bind parameters
    // 1: recipient_id (player_id)
    sqlite3_bind_int(st, 1, player_id);      
    // 2: sender_id
    sqlite3_bind_int(st, 2, sender_id);
    // 3: timestamp
    sqlite3_bind_text(st, 3, subject, -1, SQLITE_TRANSIENT);
    // 5: message (using SQLITE_TRANSIENT)
    sqlite3_bind_text(st, 4, message, -1, SQLITE_TRANSIENT);
        
    // Execute the statement 
    if (sqlite3_step(st) != SQLITE_DONE) { 
      LOGE( "SQL error executing complex message insert for player %d: %s\n", 
            player_id, sqlite3_errmsg(db)); 
        sqlite3_finalize(st); 
        return 1; 
    } 

    sqlite3_finalize(st); 

    LOGD("Complex message sent to player %d from sender %d. Subject: '%s'\n", player_id, sender_id, subject); 
        
    return 0; 
} 

/**
 * @brief Retrieves the ID of the active ship for a given player.
 * * Queries the 'players' table using the player_id to get the value
 * from the 'ship' column.
 * * @param db The SQLite database handle.
 * @param player_id The ID of the player whose ship is being sought.
 * @return The ship ID (int) on success, or 0 if player is not found 
 * or the ship column is NULL/0.
 */
int h_get_active_ship_id(sqlite3 *db, int player_id)
{
    sqlite3_stmt *st = NULL;
    int ship_id = 0;
    int rc;

    // SQL: Select the 'ship' column from 'players' where the 'id' matches the player_id.
    // The player's active ship ID is stored in the 'ship' column.
    const char *sql = "SELECT ship FROM players WHERE id = ?;";

    rc = sqlite3_prepare_v2(db, sql, -1, &st, NULL);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error preparing ship lookup: %s\n", sqlite3_errmsg(db));
        return 0; // Return 0 on error
    }

    sqlite3_bind_int(st, 1, player_id);

    // Execute the query
    rc = sqlite3_step(st);

    if (rc == SQLITE_ROW) {
        // The 'ship' column is at index 0. If it's NULL or 0, ship_id remains 0.
        ship_id = sqlite3_column_int(st, 0);
    } else if (rc != SQLITE_DONE) {
        // Handle error if step was not successful and not just finished.
        fprintf(stderr, "SQL error executing ship lookup for player %d: %s\n", 
            player_id, sqlite3_errmsg(db));
    }

    sqlite3_finalize(st);
    
    // ship_id will be > 0 if a valid ship was found, otherwise 0.
    return ship_id;
}



/**
 * @brief Attempts to de-cloak a ship if it is currently cloaked.
 * * If the ship is successfully de-cloaked, it sends a notification to the owner.
 *
 * @param db The SQLite database connection handle.
 * @param ship_id The ID of the ship to check/de-cloak.
 * @return 0 on success (either already uncloaked or successfully de-cloaked),
 * or non-zero on a critical error (like SQL failure).
 *
 * This should be called whenever the ship does any of the following
 *
 * Movement,Com,"combat.attack, combat.fire_photon, deploy.genesis"
 * Deployment,"Deploying/Laying Traps (Mines, Beacons)","deploy.mine, deploy.beacon",Placing any object on the map.
 * Ship Maintenance,Jettisoning Cargo,jettison,Dumping goods is a physical action visible to scanners.
 * Planet Interaction,Landing on a Planet,planet.land,Docking or landing requires de-cloaking.
 * Planet Interaction,TransWarp drive (TW),move.transwarp,"While a jump (short hop) can keep cloak in some variants, transwarp usually forces  * a decloak and requires massive power."
 * Trading/Ports,Buying/Selling at a Port,"port.buy, port.sell",Docking at a port (even if you stay in space) often removes cloak.
 *
 */
int h_decloak_ship(sqlite3 *db, int ship_id)
{
    sqlite3_stmt *st = NULL;
    int rc;
    int player_id = 0;
    int rows_affected = 0;

    // 1. Prepare the statement to UPDATE and check if the ship was cloaked (cloaked IS NOT NULL)
    // We update the ship in a single query: set cloaked=NULL where cloaked is not NULL AND id = ?
    // The RETURNING player_id clause assumes your 'ships' table has a player_id foreign key.
    rc = sqlite3_prepare_v2(db,
        "UPDATE ships "
        "SET cloaked = NULL "
        "WHERE id = ? AND cloaked IS NOT NULL "
        "RETURNING player_id;", 
        -1, &st, NULL);

    if (rc != SQLITE_OK) {
        // Handle SQL preparation error
        fprintf(stderr, "SQL error preparing de-cloak: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    // Bind the ship ID
    sqlite3_bind_int(st, 1, ship_id);
    
    // Execute and check the result
    if (sqlite3_step(st) == SQLITE_ROW) {
        // If a row was returned, the UPDATE was successful (the ship was cloaked)
        
        // Retrieve the player_id of the affected ship
        player_id = sqlite3_column_int(st, 0); 
        
        // 2. Send the notification to the player
        if (player_id > 0) {
            // Use the identified message function
	    h_send_message_to_player(player_id, 1, "Uncloaking", "Your ship's cloaking device has been deactivated due to action."); 
        }

        rows_affected = 1; // Ship was de-cloaked
    }

    sqlite3_finalize(st);

    if (rows_affected > 0) {
        // Commit the de-cloak immediately so subsequent actions see the new state
        // NOTE: If you are already inside a transaction, remove this COMMIT call.
        // Assuming h_decloak_ship is called as part of a larger, atomic command,
        // it might be safer to let the calling function handle the final commit.
        // I will remove the commit here for better transaction safety.
        // return sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL); 
    }

    // Return 0 even if the ship wasn't cloaked, as that's a successful outcome for this function's goal.
    return 0; 
}


enum
{ MAX_BOOKMARKS = 64, MAX_BM_NAME = 64 };
enum
{ MAX_AVOIDS = 64 };


/* ---------- forward decls for settings section builders (stubs for now) ---------- */
static json_t *players_build_settings (client_ctx_t * ctx, json_t * req);
static json_t *players_get_prefs (client_ctx_t * ctx);
static json_t *players_get_subscriptions (client_ctx_t * ctx);
static json_t *players_list_bookmarks (client_ctx_t * ctx);
static json_t *players_list_avoid (client_ctx_t * ctx);
static json_t *players_list_notes (client_ctx_t * ctx, json_t * req);


static int
is_ascii_printable (const char *s)
{
  if (!s)
    return 0;
  for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    if (*p < 0x20 || *p > 0x7E)
      return 0;
  return 1;
}

static int
len_leq (const char *s, size_t m)
{
  return s && strlen (s) <= m;
}


static int
is_valid_key (const char *s, size_t max)
{
  if (!s)
    return 0;
  size_t n = strlen (s);
  if (n == 0 || n > max)
    return 0;
  for (size_t i = 0; i < n; ++i)
    {
      char c = s[i];
      if (!
	  ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.'
	   || c == '_' || c == '-'))
	return 0;
    }
  return 1;
}


int
cmd_player_my_info (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  json_t *pinfo = NULL;
  int prc = db_player_info_json (ctx->player_id, &pinfo);
  if (prc != SQLITE_OK || !pinfo)
    {
      send_enveloped_error (ctx->fd, root, 1503, "Database error");
      return 0;
    }

  send_enveloped_ok (ctx->fd, root, "player.info", pinfo);
  json_decref (pinfo);
  return 0;
}

int
cmd_player_list_online (client_ctx_t *ctx, json_t *root)
{
  /* Until a global connection registry exists, return current player only. */
  json_t *arr = json_array ();
  if (ctx->player_id > 0)
    {
      json_array_append_new (arr,
			     json_pack ("{s:i}", "player_id",
					ctx->player_id));
    }
  json_t *data = json_pack ("{s:o}", "players", arr);
  send_enveloped_ok (ctx->fd, root, "player.list_online", data);
  json_decref (data);
  return 0;
}

/* ==================================================================== */
/*                         SETTINGS AGGREGATE                            */
/* ==================================================================== */

int
cmd_player_set_settings (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_settings");
}



/* ==================================================================== */
/*                             PREFS                                     */
/* ==================================================================== */


int
cmd_player_get_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }

  sqlite3_stmt *it = NULL;
  if (db_prefs_get_all (ctx->player_id, &it) != 0)
    {
      send_enveloped_error (ctx->fd, root, 1503, "Database error");
      return 0;
    }

  json_t *prefs = json_array ();
  while (sqlite3_step (it) == SQLITE_ROW)
    {
      const char *k = (const char *) sqlite3_column_text (it, 0);
      const char *t = (const char *) sqlite3_column_text (it, 1);
      const char *v = (const char *) sqlite3_column_text (it, 2);
      json_t *o = json_object ();
      json_object_set_new (o, "key", json_string (k ? k : ""));
      json_object_set_new (o, "type", json_string (t ? t : "string"));
      json_object_set_new (o, "value", json_string (v ? v : ""));
      json_array_append_new (prefs, o);
    }
  sqlite3_finalize (it);

  json_t *data = json_object ();
  json_object_set_new (data, "prefs", prefs);
  send_enveloped_ok (ctx->fd, root, "player.prefs_v1", data);
  return 0;
}



/* ==================================================================== */
/*                           SUBSCRIPTIONS                               */
/* ==================================================================== */

int
cmd_player_set_topics (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_topics");
}

int
cmd_player_get_topics (client_ctx_t *ctx, json_t *root)
{
  json_t *topics = players_get_subscriptions (ctx);
  if (!topics)
    send_enveloped_error (ctx->fd, root, 500, "subs_load_failed");

  json_t *out = json_object ();
  json_object_set_new (out, "topics", topics);
  send_enveloped_ok (ctx->fd, root, "player.subscriptions", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                           BOOKMARKS                                   */
/* ==================================================================== */

int
cmd_player_set_bookmarks (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_bookmarks");
}

int
cmd_player_get_bookmarks (client_ctx_t *ctx, json_t *root)
{
  json_t *bookmarks = players_list_bookmarks (ctx);
  if (!bookmarks)
    send_enveloped_error (ctx->fd, root, 500, "bookmarks_load_failed");
  json_t *out = json_object ();
  json_object_set_new (out, "bookmarks", bookmarks);
  send_enveloped_ok (ctx->fd, root, "player.bookmarks", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                              AVOIDS                                   */
/* ==================================================================== */

int
cmd_player_set_avoids (client_ctx_t *ctx, json_t *root)
{
  STUB_NIY (ctx, root, "player.set_avoids");
}

int
cmd_player_get_avoids (client_ctx_t *ctx, json_t *root)
{
  json_t *avoid = players_list_avoid (ctx);
  if (!avoid)
    send_enveloped_error (ctx->fd, root, 500, "avoid_load_failed");

  json_t *out = json_object ();
  json_object_set_new (out, "avoid", avoid);
  send_enveloped_ok (ctx->fd, root, "avoids", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                               NOTES                                   */
/* ==================================================================== */

int
cmd_player_get_notes (client_ctx_t *ctx, json_t *root)
{
  json_t *notes = players_list_notes (ctx, root);	/* supports {"scope":...,"key":...} */
  if (!notes)
    send_enveloped_error (ctx->fd, root, 500, "notes_load_failed");

  json_t *out = json_object ();
  json_object_set_new (out, "notes", notes);
  send_enveloped_ok (ctx->fd, root, "player.notes", out);
  json_decref (out);
  return 0;
}

/* ==================================================================== */
/*                       SECTION BUILDERS (STUBS)                        */
/* ==================================================================== */

static int
_include_wanted (json_t *data, const char *key)
{
  json_t *inc = data ? json_object_get (data, "include") : NULL;
  if (!inc || !json_is_array (inc))
    return 1;			/* no filter → include all */
  size_t i, n = json_array_size (inc);
  for (i = 0; i < n; i++)
    {
      const char *s = json_string_value (json_array_get (inc, i));
      if (s && 0 == strcmp (s, key))
	return 1;
    }
  return 0;
}

static json_t *
players_build_settings (client_ctx_t *ctx, json_t *req)
{
  json_t *out = json_object ();

  if (_include_wanted (req, "prefs"))
    {
      json_t *prefs = players_get_prefs (ctx);
      if (!prefs)
	prefs = json_object ();
      json_object_set_new (out, "prefs", prefs);
    }
  if (_include_wanted (req, "subscriptions"))
    {
      json_t *subs = players_get_subscriptions (ctx);
      if (!subs)
	subs = json_array ();
      json_object_set_new (out, "subscriptions", subs);
    }
  if (_include_wanted (req, "bookmarks"))
    {
      json_t *bm = players_list_bookmarks (ctx);
      if (!bm)
	bm = json_array ();
      json_object_set_new (out, "bookmarks", bm);
    }
  if (_include_wanted (req, "avoid"))
    {
      json_t *av = players_list_avoid (ctx);
      if (!av)
	av = json_array ();
      json_object_set_new (out, "avoid", av);
    }
  if (_include_wanted (req, "notes"))
    {
      json_t *nt = players_list_notes (ctx, req);
      if (!nt)
	nt = json_array ();
      json_object_set_new (out, "notes", nt);
    }
  return out;
}

/* Replace these stubs with DB-backed implementations as you land #189+ */
static json_t *
players_get_prefs (client_ctx_t *ctx)
{
  (void) ctx;
  json_t *prefs = json_object ();
  json_object_set_new (prefs, "ui.ansi", json_true ());
  json_object_set_new (prefs, "ui.clock_24h", json_true ());
  json_object_set_new (prefs, "ui.locale", json_string ("en-GB"));
  json_object_set_new (prefs, "ui.page_length", json_integer (20));
  json_object_set_new (prefs, "privacy.dm_allowed", json_true ());
  return prefs;
}

static json_t *
players_get_subscriptions (client_ctx_t *ctx)
{
  (void) ctx;
  json_t *arr = json_array ();
  json_t *a = json_object ();
  json_object_set_new (a, "topic", json_string ("system.notice"));
  json_object_set_new (a, "locked", json_true ());
  json_array_append_new (arr, a);
  json_t *b = json_object ();
  json_object_set_new (b, "topic", json_string ("sector.*"));
  json_object_set_new (b, "locked", json_false ());
  json_array_append_new (arr, b);
  return arr;
}

static json_t *
players_list_bookmarks (client_ctx_t *ctx)
{
  (void) ctx;
  return json_array ();
}

static json_t *
players_list_avoid (client_ctx_t *ctx)
{
  (void) ctx;
  return json_array ();
}

static json_t *
players_list_notes (client_ctx_t *ctx, json_t *req)
{
  (void) ctx;
  (void) req;
  return json_array ();
}

/* --- local helpers for type mapping/validation --- */
static int
map_pt (const char *s)
{
  if (!s)
    return PT_STRING;
  if (strcmp (s, "bool") == 0)
    return PT_BOOL;
  if (strcmp (s, "int") == 0)
    return PT_INT;
  if (strcmp (s, "json") == 0)
    return PT_JSON;
  return PT_STRING;
}

static int
validate_value (int pt, const char *v)
{
  if (!v)
    return 0;
  char *end = NULL;
  switch (pt)
    {
    case PT_BOOL:
      return (!strcmp (v, "true") || !strcmp (v, "false") || !strcmp (v, "0")
	      || !strcmp (v, "1"));
    case PT_INT:
      strtol (v, &end, 10);
      return (end && *end == '\0');
    case PT_JSON:
      return v[0] == '{' || v[0] == '[';	/* lightweight check */
    case PT_STRING:
    default:
      return 1;
    }
}


/* ------ Set Pref ------- */

/* --- allow ui.locale (simple BCP47-ish) --- */
static int
is_valid_locale (const char *s)
{
  if (!s)
    return 0;
  size_t n = strlen (s);
  if (n < 2 || n > 16)
    return 0;
  for (size_t i = 0; i < n; i++)
    {
      char c = s[i];
      if (!
	  ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
	   || (c >= '0' && c <= '9') || c == '-'))
	return 0;
    }
  return 1;
}




int
cmd_player_set_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return -1;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return -1;
    }

  /* Support both: {data:{patch:{...}}} OR {data:{...}} */
  json_t *patch = json_object_get (data, "patch");
  json_t *prefs = json_is_object (patch) ? patch : data;

  void *it = json_object_iter (prefs);
  while (it)
    {
      const char *key = json_object_iter_key (it);
      json_t *val = json_object_iter_value (it);

      if (!is_valid_key (key, 64))
	{
	  send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
				"invalid key");
	  return -1;
	}

      pref_type t = PT_STRING;
      char buf[512] = { 0 };

      if (json_is_string (val))
	{
	  const char *s = json_string_value (val);
	  if (!is_ascii_printable (s) || strlen (s) > 256)
	    {
	      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
				    "string too long/not printable");
	      return -1;
	    }
	  t = PT_STRING;
	  snprintf (buf, sizeof (buf), "%s", s);
	}
      else if (json_is_integer (val))
	{
	  t = PT_INT;
	  snprintf (buf, sizeof (buf), "%lld",
		    (long long) json_integer_value (val));
	}
      else if (json_is_boolean (val))
	{
	  t = PT_BOOL;
	  snprintf (buf, sizeof (buf), "%s", json_is_true (val) ? "1" : "0");
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
				"unsupported value type");
	  return -1;
	}

      // if (db_prefs_set_one (ctx->player_id, key, t, buf) != 0)
      if (db_prefs_set_one
	  (ctx->player_id, "ui.locale", PT_STRING,
	   json_string_value (val)) != 0)
	{
	  send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
	  return -1;
	}

      if (strcmp (key, "ui.locale") == 0)
	{
	  if (!json_is_string (val)
	      || !is_valid_locale (json_string_value (val)))
	    {
	      send_enveloped_error (ctx->fd, root, ERR_BAD_REQUEST,
				    "invalid ui.locale");
	      return -1;
	    }
	  /* falls through to db_prefs_set_one with type='string' */
	}


      it = json_object_iter_next (prefs, it);
    }

  json_t *resp = json_pack ("{s:b}", "ok", 1);
  send_enveloped_ok (ctx->fd, root, "player.prefs.updated", resp);
  json_decref (resp);
  return 0;
}



/* ---------- nav.bookmark.* ---------- */



void
cmd_nav_bookmark_add (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return;
    }

  json_t *v = json_object_get (data, "name");
  if (!json_is_string (v))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
			    "missing field: name");
      return;
    }
  const char *name = json_string_value (v);
  if (!is_ascii_printable (name) || !len_leq (name, MAX_BM_NAME))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG, "invalid name");
      return;
    }

  v = json_object_get (data, "sector_id");
  if (!json_is_integer (v) || json_integer_value (v) <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
			    "invalid sector_id");
      return;
    }
  int64_t sector_id = json_integer_value (v);

  /* Cap check */
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db, "SELECT COUNT(*) FROM player_bookmarks WHERE player_id=?1;", -1,
       &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);
  int have = 0;
  if (sqlite3_step (st) == SQLITE_ROW)
    have = sqlite3_column_int (st, 0);
  sqlite3_finalize (st);
  if (have >= MAX_BOOKMARKS)
    {
      json_t *meta =
	json_pack ("{s:i,s:i}", "max", MAX_BOOKMARKS, "have", have);
      send_enveloped_refused (ctx->fd, root, ERR_LIMIT_EXCEEDED,
			      "too many bookmarks", meta);
      return;
    }

  int rc = db_bookmark_upsert (ctx->player_id, name, sector_id);
  if (rc != 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }

  json_t *resp =
    json_pack ("{s:s,s:i}", "name", name, "sector_id", (int) sector_id);
  send_enveloped_ok (ctx->fd, root, "nav.bookmark.added", resp);
  json_decref (resp);
}

void
cmd_nav_bookmark_remove (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return;
    }

  json_t *v = json_object_get (data, "name");
  if (!json_is_string (v))
    {
      send_enveloped_error (ctx->fd, root, ERR_MISSING_FIELD,
			    "missing field: name");
      return;
    }
  const char *name = json_string_value (v);
  if (!is_ascii_printable (name) || !len_leq (name, MAX_BM_NAME))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG, "invalid name");
      return;
    }

  int rc = db_bookmark_remove (ctx->player_id, name);
  if (rc != 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_FOUND,
			    "bookmark not found");
      return;
    }

  json_t *resp = json_pack ("{s:s}", "name", name);
  send_enveloped_ok (ctx->fd, root, "nav.bookmark.removed", resp);
  json_decref (resp);
}

void
cmd_nav_bookmark_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db,
       "SELECT name, sector_id FROM player_bookmarks WHERE player_id=?1 ORDER BY updated_at DESC,name;",
       -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);

  json_t *items = json_array ();
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      const char *name = (const char *) sqlite3_column_text (st, 0);
      int sector_id = sqlite3_column_int (st, 1);
      json_array_append_new (items,
			     json_pack ("{s:s,s:i}", "name", name ? name : "",
					"sector_id", sector_id));
    }
  sqlite3_finalize (st);

  json_t *resp = json_pack ("{s:O}", "items", items);
  send_enveloped_ok (ctx->fd, root, "nav.bookmark.list", resp);
  json_decref (resp);
}





/* ---------- nav.avoid.* ---------- */


void
cmd_nav_avoid_add (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return;
    }

  json_t *v = json_object_get (data, "sector_id");
  if (!json_is_integer (v) || json_integer_value (v) <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
			    "invalid sector_id");
      return;
    }
  int sector_id = (int) json_integer_value (v);

  /* Cap */
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db, "SELECT COUNT(*) FROM player_avoid WHERE player_id=?1;", -1, &st,
       NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);
  int have = 0;
  if (sqlite3_step (st) == SQLITE_ROW)
    have = sqlite3_column_int (st, 0);
  sqlite3_finalize (st);
  if (have >= MAX_AVOIDS)
    {
      json_t *meta = json_pack ("{s:i,s:i}", "max", MAX_AVOIDS, "have", have);
      send_enveloped_refused (ctx->fd, root, ERR_LIMIT_EXCEEDED,
			      "too many avoids", meta);
      return;
    }

  if (sqlite3_prepare_v2
      (db,
       "INSERT INTO player_avoid(player_id,sector_id,updated_at) VALUES(?1,?2,strftime('%s','now'));",
       -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);
  sqlite3_bind_int (st, 2, sector_id);
  int rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      if (rc == SQLITE_CONSTRAINT)
	{
	  send_enveloped_error (ctx->fd, root, ERR_DUPLICATE_REQUEST,
				"already in avoid list");
	}
      else
	{
	  send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
	}
      return;
    }

  json_t *resp = json_pack ("{s:i}", "sector_id", sector_id);
  send_enveloped_ok (ctx->fd, root, "nav.avoid.added", resp);
  json_decref (resp);
}

void
cmd_nav_avoid_remove (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_SCHEMA,
			    "data must be object");
      return;
    }

  json_t *v = json_object_get (data, "sector_id");
  if (!json_is_integer (v) || json_integer_value (v) <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_INVALID_ARG,
			    "invalid sector_id");
      return;
    }
  int sector_id = (int) json_integer_value (v);

  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db, "DELETE FROM player_avoid WHERE player_id=?1 AND sector_id=?2;",
       -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);
  sqlite3_bind_int (st, 2, sector_id);
  int rc = sqlite3_step (st);
  int rows = sqlite3_changes (db);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE || rows == 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_FOUND, "avoid not found");
      return;
    }

  json_t *resp = json_pack ("{s:i}", "sector_id", sector_id);
  send_enveloped_ok (ctx->fd, root, "nav.avoid.removed", resp);
  json_decref (resp);
}

void
cmd_nav_avoid_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_error (ctx->fd, root, ERR_NOT_AUTHENTICATED,
			    "auth required");
      return;
    }

  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2
      (db,
       "SELECT sector_id FROM player_avoid WHERE player_id=?1 ORDER BY updated_at DESC, sector_id;",
       -1, &st, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "db error");
      return;
    }
  sqlite3_bind_int64 (st, 1, ctx->player_id);

  json_t *items = json_array ();
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int sid = sqlite3_column_int (st, 0);
      json_array_append_new (items, json_integer (sid));
    }
  sqlite3_finalize (st);

  json_t *resp = json_pack ("{s:O}", "items", items);
  send_enveloped_ok (ctx->fd, root, "nav.avoid.list", resp);
  json_decref (resp);
}


static json_t *
prefs_as_array (int64_t pid)
{
  sqlite3_stmt *it = NULL;
  if (db_prefs_get_all (pid, &it) != 0)
    return json_array ();

  json_t *arr = json_array ();
  while (sqlite3_step (it) == SQLITE_ROW)
    {
      const char *k = (const char *) sqlite3_column_text (it, 0);
      const char *t = (const char *) sqlite3_column_text (it, 1);
      const char *v = (const char *) sqlite3_column_text (it, 2);
      json_t *one = json_pack ("{s:s, s:s, s:s}", "key", k ? k : "", "type",
			       t ? t : "string", "value", v ? v : "");
      json_array_append_new (arr, one);
    }
  sqlite3_finalize (it);
  return arr;
}

static json_t *
bookmarks_as_array (int64_t pid)
{
  static const char *SQL =
    "SELECT name, sector_id FROM player_bookmarks WHERE player_id=?1 ORDER BY name;";
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
    return json_array ();
  sqlite3_bind_int64 (st, 1, pid);

  json_t *arr = json_array ();
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      const char *name = (const char *) sqlite3_column_text (st, 0);
      int64_t sector_id = sqlite3_column_int64 (st, 1);
      json_array_append_new (arr,
			     json_pack ("{s:s, s:i}", "name",
					name ? name : "", "sector_id",
					(int) sector_id));
    }
  sqlite3_finalize (st);
  return arr;
}

static json_t *
avoid_as_array (int64_t pid)
{
  static const char *SQL =
    "SELECT sector_id FROM player_avoid WHERE player_id=?1 ORDER BY sector_id;";
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db, SQL, -1, &st, NULL) != SQLITE_OK)
    return json_array ();
  sqlite3_bind_int64 (st, 1, pid);

  json_t *arr = json_array ();
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int64_t sector_id = sqlite3_column_int64 (st, 0);
      json_array_append_new (arr, json_integer ((json_int_t) sector_id));
    }
  sqlite3_finalize (st);
  return arr;
}

static json_t *
subscriptions_as_array (int64_t pid)
{
  sqlite3_stmt *it = NULL;
  if (db_subscribe_list (pid, &it) != 0)
    return json_array ();

  json_t *arr = json_array ();
  while (sqlite3_step (it) == SQLITE_ROW)
    {
      const char *topic = (const char *) sqlite3_column_text (it, 0);
      int locked = sqlite3_column_int (it, 1);
      int enabled = sqlite3_column_int (it, 2);
      const char *delivery = (const char *) sqlite3_column_text (it, 3);
      const char *filter = (const char *) sqlite3_column_text (it, 4);
      json_t *one = json_pack ("{s:s, s:b, s:b, s:s}",
			       "topic", topic ? topic : "",
			       "locked", locked ? 1 : 0,
			       "enabled", enabled ? 1 : 0,
			       "delivery", delivery ? delivery : "push");
      if (filter)
	json_object_set_new (one, "filter", json_string (filter));
      json_array_append_new (arr, one);
    }
  sqlite3_finalize (it);
  return arr;
}

/* player.get_settings → player.settings_v1 */
int
cmd_player_get_settings (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_enveloped_error (ctx ? ctx->fd : -1, root, ERR_NOT_AUTHENTICATED,
			    "Authentication required");
      return 0;
    }

  json_t *prefs = prefs_as_array (ctx->player_id);
  json_t *bm = bookmarks_as_array (ctx->player_id);
  json_t *avoid = avoid_as_array (ctx->player_id);
  json_t *subs = subscriptions_as_array (ctx->player_id);

  json_t *data = json_pack ("{s:o, s:o, s:o, s:o}",
			    "prefs", prefs,
			    "bookmarks", bm,
			    "avoid", avoid,
			    "subscriptions", subs);

  send_enveloped_ok (ctx->fd, root, "player.settings_v1", data);
  return 0;
}
