#include <string.h>
#include <sqlite3.h>
#include "database.h"
#include "server_cmds.h"
#include "errors.h"

/* Constant-time string compare to reduce timing leakage (simple variant). */
static int
ct_str_eq (const char *a, const char *b)
{
  size_t la = a ? strlen (a) : 0;
  size_t lb = b ? strlen (b) : 0;
  size_t n = (la > lb) ? la : lb;
  unsigned char diff = (unsigned char) (la ^ lb);
  for (size_t i = 0; i < n; i++)
    {
      unsigned char ca = (i < la) ? (unsigned char) a[i] : 0;
      unsigned char cb = (i < lb) ? (unsigned char) b[i] : 0;
      diff |= (unsigned char) (ca ^ cb);
    }
  return diff == 0;
}

int
play_login (const char *player_name, const char *password, int *out_player_id)
{
  if (!player_name || !password)
    return AUTH_ERR_BAD_REQUEST;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return AUTH_ERR_DB;

  pthread_mutex_lock (&db_mutex);

  const char *sql = "SELECT id, passwd, type FROM players WHERE name=?1;";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_DB;
    }

  sqlite3_bind_text (st, 1, player_name, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);
  if (rc != SQLITE_ROW)
    {
      sqlite3_finalize (st);
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_INVALID_CRED;	/* no such user */
    }

  const int player_id = sqlite3_column_int (st, 0);
  const unsigned char *dbpass_u8 = sqlite3_column_text (st, 1);
  const int player_type = sqlite3_column_int (st, 2);

  /* Copy the password BEFORE finalize; column_text ptr is invalid after finalize */
  char *dbpass = dbpass_u8 ? strdup ((const char *) dbpass_u8) : NULL;

  sqlite3_finalize (st);
  pthread_mutex_unlock (&db_mutex);

  /* Block NPC logins */
  if (player_type == 1)
    {
      if (dbpass)
	free (dbpass);
      return AUTH_ERR_IS_NPC;
    }

  /* Compare password (constant-time helper) */
  int ok = (dbpass != NULL) && ct_str_eq (password, dbpass);
  if (dbpass)
    free (dbpass);
  if (!ok)
    return AUTH_ERR_INVALID_CRED;

  if (out_player_id)
    *out_player_id = player_id;

  return AUTH_OK;
}

/* Create new user with unique name.
   - Ensures name isn't taken.
   - Assigns legacy 'number' as MAX(number)+1 atomically via SELECT aggregate.
   - Returns new rowid as player_id. */
int
user_create (const char *player_name, const char *password,
	     int *out_player_id)
{
  if (!player_name || !password)
    return AUTH_ERR_BAD_REQUEST;

  sqlite3 *db = db_get_handle ();
  if (!db)
    return AUTH_ERR_DB;

  int rc;
  sqlite3_stmt *st = NULL;

  pthread_mutex_lock (&db_mutex);

  /* 1) Name uniqueness check */
  const char *chk = "SELECT 1 FROM players WHERE name=?1 LIMIT 1;";
  rc = sqlite3_prepare_v2 (db, chk, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_DB;
    }
  sqlite3_bind_text (st, 1, player_name, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc == SQLITE_ROW)
    {
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_NAME_TAKEN;
    }

  /* 2) Insert new player with sequential 'number' */
  const char *ins =
    "INSERT INTO players (name, passwd, number) "
    "SELECT ?1, ?2, COALESCE(MAX(number),0)+1 FROM players;";

  rc = sqlite3_prepare_v2 (db, ins, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_DB;
    }

  sqlite3_bind_text (st, 1, player_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 2, password, -1, SQLITE_TRANSIENT);

  rc = sqlite3_step (st);
  int done_ok = (rc == SQLITE_DONE);
  sqlite3_finalize (st);

  if (!done_ok)
    {
      pthread_mutex_unlock (&db_mutex);
      return AUTH_ERR_DB;
    }

  if (out_player_id)
    *out_player_id = (int) sqlite3_last_insert_rowid (db);

  pthread_mutex_unlock (&db_mutex);

  return AUTH_OK;
}
