#include <string.h>
#include <stdio.h>
#include <stdlib.h> // For malloc, free, calloc, realloc
#include <string.h> // For memcpy, strlen, strncpy, strncat
#include <sqlite3.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <jansson.h>
#include <stdbool.h>
#include <pthread.h>
#include <stddef.h> // For size_t
// local includes
#include  "common.h"
#include "server_config.h"
#include "database.h"
#include "database_cmd.h"
#include "server_log.h"
#include "server_cron.h"


int
db_player_update_commission (sqlite3 *db, int player_id)
{
  if (!db)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;
  int player_alignment = 0;
  long long player_experience = 0;
  int new_commission_id = 0;


  db_mutex_lock ();  // Lock for thread safety
  // 1. Get player's current alignment and experience
  const char *sql_get_player_stats =
    "SELECT alignment, experience FROM players WHERE id = ?1;";


  rc = sqlite3_prepare_v2 (db, sql_get_player_stats, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_player_update_commission: Prepare get player stats error: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, player_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      player_alignment = sqlite3_column_int (st, 0);
      player_experience = sqlite3_column_int64 (st, 1);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      LOGW ("db_player_update_commission: Player with ID %d not found.",
            player_id);
      rc = SQLITE_NOTFOUND;
      goto cleanup;
    }
  else
    {
      LOGE ("db_player_update_commission: Execute get player stats error: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_finalize (st);
  st = NULL;
  // 2. Determine moral track from alignment band
  int band_id;
  int is_evil_track;   // This will be 0 for good/neutral, 1 for evil


  // We only need the is_evil flag from alignment_band for this purpose
  rc = db_alignment_band_for_value (db,
                                    player_alignment,
                                    &band_id,
                                    NULL,
                                    NULL,
                                    NULL,
                                    &is_evil_track,
                                    NULL,
                                    NULL);
  if (rc != SQLITE_OK)
    {
      LOGE (
        "db_player_update_commission: Failed to get alignment band for player %d, alignment %d. RC: %d",
        player_id,
        player_alignment,
        rc);
      goto cleanup;
    }
  // 3. Get the new commission ID based on moral track and experience
  char *commission_title = NULL;   // Not needed for update, but API requires it
  int commission_is_evil_flag;   // Not needed for update, but API requires it


  rc = db_commission_for_player (db,
                                 is_evil_track,
                                 player_experience,
                                 &new_commission_id,
                                 &commission_title,
                                 &commission_is_evil_flag);
  if (commission_title)
    {
      free (commission_title);                    // Free strdup'd memory
    }
  if (rc != SQLITE_OK)
    {
      LOGE (
        "db_player_update_commission: Failed to get commission for player %d (XP %lld, track %d). RC: %d",
        player_id,
        player_experience,
        is_evil_track,
        rc);
      goto cleanup;
    }
  // 4. Update the player's commission in the players table
  const char *sql_update_commission =
    "UPDATE players SET commission = ?1 WHERE id = ?2;";


  rc = sqlite3_prepare_v2 (db, sql_update_commission, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_player_update_commission: Prepare update commission error: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, new_commission_id);
  sqlite3_bind_int (st, 2, player_id);
  rc = sqlite3_step (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("db_player_update_commission: Execute update commission error: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  rc = SQLITE_OK;   // If SQLITE_DONE, then successful
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();  // Unlock
  return rc;
}


int
db_commission_for_player (
  sqlite3 *db,
  int is_evil_track,
  long long xp,
  int *out_commission_id,
  char **out_title,
  int *out_is_evil
  )
{
  if (!db)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;
  const char *sql =
    "SELECT id, description, is_evil "
    "FROM commision "
    "WHERE is_evil = ?1 "
    "AND min_exp <= ?2 "
    "ORDER BY min_exp DESC "
    "LIMIT 1;";


  db_mutex_lock ();  // Lock for thread safety
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_commission_for_player: Prepare error: %s", sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, is_evil_track);
  sqlite3_bind_int64 (st, 2, xp);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      if (out_commission_id)
        {
          *out_commission_id = sqlite3_column_int (st, 0);
        }
      if (out_title)
        {
          *out_title = strdup ((const char *)sqlite3_column_text (st, 1));
        }
      if (out_is_evil)
        {
          *out_is_evil = sqlite3_column_int (st, 2);
        }
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      // No matching commission found for given XP and track. Fallback to lowest rank.
      const char *fallback_sql =
        "SELECT id, description, is_evil FROM commision "
        "WHERE is_evil = ?1 ORDER BY min_exp ASC LIMIT 1;";


      sqlite3_finalize (st);  // Finalize previous statement before new prepare
      st = NULL;   // Reset statement pointer
      rc = sqlite3_prepare_v2 (db, fallback_sql, -1, &st, NULL);
      if (rc != SQLITE_OK)
        {
          LOGE ("db_commission_for_player: Fallback prepare error: %s",
                sqlite3_errmsg (db));
          goto cleanup;
        }
      sqlite3_bind_int (st, 1, is_evil_track);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          if (out_commission_id)
            {
              *out_commission_id = sqlite3_column_int (st, 0);
            }
          if (out_title)
            {
              *out_title = strdup ((const char *)sqlite3_column_text (st, 1));
            }
          if (out_is_evil)
            {
              *out_is_evil = sqlite3_column_int (st, 2);
            }
          LOGW (
            "db_commission_for_player: No commission found for XP %lld, track %d. Falling back to lowest rank.",
            xp,
            is_evil_track);
          rc = SQLITE_OK;
        }
      else
        {
          LOGE (
            "db_commission_for_player: No fallback commission found for track %d. DB might be empty.",
            is_evil_track);
          rc = SQLITE_NOTFOUND;   // Even fallback failed
        }
    }
  else
    {
      LOGE ("db_commission_for_player: Execute error: %s", sqlite3_errmsg (db));
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);      // Finalize if not NULL
    }
  db_mutex_unlock ();  // Unlock
  return rc;
}


int
db_alignment_band_for_value (
  sqlite3 *db,
  int align,
  int *out_id,
  char **out_code,
  char **out_name,
  int *out_is_good,
  int *out_is_evil,
  int *out_can_buy_iss,
  int *out_can_rob_ports
  )
{
  if (!db)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;
  const char *sql =
    "SELECT id, code, name, is_good, is_evil, can_buy_iss, can_rob_ports "
    "FROM alignment_band "
    "WHERE ?1 BETWEEN min_align AND max_align "
    "LIMIT 1;";


  db_mutex_lock ();  // Lock for thread safety
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_alignment_band_for_value: Prepare error: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, align);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      if (out_id)
        {
          *out_id = sqlite3_column_int (st, 0);
        }
      if (out_code)
        {
          *out_code = strdup ((const char *)sqlite3_column_text (st, 1));
        }
      if (out_name)
        {
          *out_name = strdup ((const char *)sqlite3_column_text (st, 2));
        }
      if (out_is_good)
        {
          *out_is_good = sqlite3_column_int (st, 3);
        }
      if (out_is_evil)
        {
          *out_is_evil = sqlite3_column_int (st, 4);
        }
      if (out_can_buy_iss)
        {
          *out_can_buy_iss = sqlite3_column_int (st, 5);
        }
      if (out_can_rob_ports)
        {
          *out_can_rob_ports = sqlite3_column_int (st, 6);
        }
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      // No matching band found, provide a synthetic "NEUTRAL" fallback
      if (out_id)
        {
          *out_id = -1;           // Indicate a synthetic/default entry
        }
      if (out_code)
        {
          *out_code = strdup ("NEUTRAL");
        }
      if (out_name)
        {
          *out_name = strdup ("Neutral");
        }
      if (out_is_good)
        {
          *out_is_good = 0;
        }
      if (out_is_evil)
        {
          *out_is_evil = 0;
        }
      if (out_can_buy_iss)
        {
          *out_can_buy_iss = 0;
        }
      if (out_can_rob_ports)
        {
          *out_can_rob_ports = 0;
        }
      LOGW (
        "db_alignment_band_for_value: No matching alignment band for value %d. Using neutral fallback.",
        align);
      rc = SQLITE_OK;
    }
  else
    {
      LOGE ("db_alignment_band_for_value: Execute error: %s",
            sqlite3_errmsg (db));
    }
cleanup:
  sqlite3_finalize (st);
  db_mutex_unlock ();  // Unlock
  return rc;
}


// Static prototypes for internal helper functions
static int db_is_npc_player (sqlite3 *db, int player_id);
static int column_exists_unlocked (sqlite3 *db,
                                   const char *table,
                                   const char *col);
static int db_ensure_ship_perms_column_unlocked (sqlite3 *db);
static long long h_get_account_alert_threshold_unlocked (sqlite3 *db,
                                                         int account_id,
                                                         const char *owner_type);


static int h_get_bank_balance (const char *owner_type,
                               int owner_id,
                               long long *out_balance);
static const char *get_player_view_column_name (const char *client_name);


/* Parse "2,3,4,5" -> [2,3,4,5] */
json_t *
parse_neighbors_csv (const unsigned char *txt)
{
  json_t *arr = json_array ();
  if (!txt)
    {
      return arr;
    }
  const char *p = (const char *) txt;


  while (*p)
    {
      while (*p == ' ' || *p == '\t')
        {
          p++;                  /* trim left */
        }
      const char *start = p;


      while (*p && *p != ',')
        {
          p++;
        }
      int len = (int) (p - start);


      if (len > 0)
        {
          char buf[32];


          if (len >= (int) sizeof (buf))
            {
              len = (int) sizeof (buf) - 1;     /* defensive */
            }
          memcpy (buf, start, len);
          buf[len] = '\0';
          int id = atoi (buf);


          if (id > 0)
            {
              json_array_append_new (arr, json_integer (id));
            }
        }
      if (*p == ',')
        {
          p++;                  /* skip comma */
        }
    }
  return arr;
}


static int
stmt_to_json_array (sqlite3_stmt *st, json_t **out_array)
{
  if (!out_array)
    {
      /* Caller doesn’t want JSON, just drain the cursor */
      int rc;


      while ((rc = sqlite3_step (st)) == SQLITE_ROW)
        {
        }
      if (rc == SQLITE_DONE)
        {
          return SQLITE_OK;
        }
      return rc;
    }
  json_t *arr = json_array ();


  if (!arr)
    {
      return SQLITE_NOMEM;
    }
  int rc;


  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int cols = sqlite3_column_count (st);
      json_t *obj = json_object ();


      if (!obj)
        {
          rc = SQLITE_NOMEM;
          break;
        }
      for (int i = 0; i < cols; i++)
        {
          const char *col_name = sqlite3_column_name (st, i);
          int col_type = sqlite3_column_type (st, i);
          json_t *val = NULL;


          switch (col_type)
            {
              case SQLITE_INTEGER:
                val = json_integer (sqlite3_column_int64 (st, i));
                break;
              case SQLITE_FLOAT:
                val = json_real (sqlite3_column_double (st, i));
                break;
              case SQLITE_TEXT:
                val = json_string ((const char *) sqlite3_column_text (st, i));
                break;
              case SQLITE_NULL:
              default:
                val = json_null ();
                break;
            }
          if (!val)
            {
              json_decref (obj);
              rc = SQLITE_NOMEM;
              goto done;
            }
          /* ignore failure here; names come from SQLite */
          json_object_set_new (obj, col_name, val);
        }
      if (json_array_append_new (arr, obj) != 0)
        {
          json_decref (obj);
          rc = SQLITE_NOMEM;
          break;
        }
    }
done:
  if (rc == SQLITE_DONE)
    {
      *out_array = arr;
      return SQLITE_OK;
    }
  json_decref (arr);
  return rc;
}


/* ----------------------------------------------------------------------
 * Bank: transactions
 * ---------------------------------------------------------------------- */
int
db_bank_get_transactions (const char *owner_type, int owner_id, int limit,
                          const char *tx_type_filter, long long start_date,
                          long long end_date, long long min_amount,
                          long long max_amount, json_t **out_array)
{
  if (!owner_type)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_MISUSE;
    }
  if (limit <= 0)
    {
      limit = 100;
    }
  char sql[1024];
  char where_clause[512];
  int bind_pos = 3;


  // Base query
  strcpy (sql,
          "SELECT t.* FROM bank_transactions t JOIN bank_accounts a ON t.account_id = a.id WHERE a.owner_type = ?1 AND a.owner_id = ?2 ");
  // Build WHERE clause
  strcpy (where_clause, "");
  if (tx_type_filter)
    {
      strcat (where_clause, "AND t.tx_type = ? ");
    }
  if (start_date > 0)
    {
      strcat (where_clause, "AND t.ts >= ? ");
    }
  if (end_date > 0)
    {
      strcat (where_clause, "AND t.ts <= ? ");
    }
  if (min_amount > 0)
    {
      strcat (where_clause, "AND t.amount >= ? ");
    }
  if (max_amount > 0)
    {
      strcat (where_clause, "AND t.amount <= ? ");
    }
  strcat (sql, where_clause);
  strcat (sql, "ORDER BY t.ts DESC, t.id DESC LIMIT ?");
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("db_bank_get_transactions: SQL error: %s on query %s",
            sqlite3_errmsg (db), sql);
      return rc;
    }
  sqlite3_bind_text (st, 1, owner_type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, owner_id);
  if (tx_type_filter)
    {
      sqlite3_bind_text (st, bind_pos++, tx_type_filter, -1,
                         SQLITE_TRANSIENT);
    }
  if (start_date > 0)
    {
      sqlite3_bind_int64 (st, bind_pos++, start_date);
    }
  if (end_date > 0)
    {
      sqlite3_bind_int64 (st, bind_pos++, end_date);
    }
  if (min_amount > 0)
    {
      sqlite3_bind_int64 (st, bind_pos++, min_amount);
    }
  if (max_amount > 0)
    {
      sqlite3_bind_int64 (st, bind_pos++, max_amount);
    }
  sqlite3_bind_int (st, bind_pos, limit);
  json_t *arr = NULL;


  rc = stmt_to_json_array (st, out_array ? &arr : NULL);
  sqlite3_finalize (st);
  if (rc == SQLITE_OK && out_array)
    {
      *out_array = arr;
    }
  else if (arr)
    {
      json_decref (arr);
    }
  return rc;
}


/* ----------------------------------------------------------------------
 * Bank: interest & orders
 *
 * NOTE: The schema excerpt you provided is too truncated to implement
 * these correctly (we can't reliably see all NOT NULL columns of
 * bank_interest_policy or bank_orders). They remain explicit TODO
 * placeholders instead of silently doing the wrong thing.
 * ---------------------------------------------------------------------- */
int
db_bank_apply_interest (void)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_MISUSE;
    }

  /* TODO: implement based on full bank_interest_policy / bank_accounts
   * schema; for now this is an explicit no-op.
   */
  return SQLITE_OK;
}


int
db_bank_process_orders (void)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_MISUSE;
    }

  /* TODO: implement processing of bank_orders into bank_transactions.
   * Left as a no-op because the bank_orders schema is truncated here.
   */
  return SQLITE_OK;
}


/* ----------------------------------------------------------------------
 * Bank: flags (risk / behaviour flags)
 *
 * Assumes schema roughly:
 *   CREATE TABLE bank_flags (
 *      player_id INTEGER PRIMARY KEY REFERENCES players(id) ON DELETE CASCADE,
 *      flags     INTEGER NOT NULL DEFAULT 0,
 *      risk_tier TEXT NOT NULL DEFAULT 'normal' CHECK(...)
 *   );
 *
 * owner_type is accepted for future extension; currently only 'player'
 * is supported and mapped directly to player_id.
 * ---------------------------------------------------------------------- */
int
db_bank_set_frozen_status (const char *owner_type, int owner_id,
                           int is_frozen)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_MISUSE;
    }
  /* At present the table is keyed by player_id only. */
  if (!owner_type || strcmp (owner_type, "player") != 0)
    {
      return SQLITE_ERROR;
    }
  const char *sql =
    "INSERT INTO bank_flags (player_id, is_frozen) "
    "VALUES (?1, ?2) "
    "ON CONFLICT(player_id) DO UPDATE SET is_frozen = excluded.is_frozen";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, owner_id);
  sqlite3_bind_int (st, 2, is_frozen);
  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE)
    {
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


int
db_bank_get_frozen_status (const char *owner_type, int owner_id,
                           int *out_is_frozen)
{
  if (!out_is_frozen)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_MISUSE;
    }
  if (!owner_type || strcmp (owner_type, "player") != 0)
    {
      return SQLITE_ERROR;
    }
  const char *sql =
    "SELECT is_frozen " "FROM bank_flags " "WHERE player_id = ?1";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, owner_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *out_is_frozen = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      /* No row → treat as 0 flags. */
      *out_is_frozen = 0;
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


/* ----------------------------------------------------------------------
 * Commodities: get / update price
 *
 * We don't know the exact price column name, so try a small set of
 * plausible candidates in order.
 * ---------------------------------------------------------------------- */
static int
select_price_with_column (sqlite3 *db, const char *col,
                          const char *commodity_code, int *out_price)
{
  const char *sql_tmpl = "SELECT %s FROM commodities WHERE code = ?1";
  char sql[256];
  snprintf (sql, sizeof sql, sql_tmpl, col);
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_text (st, 1, commodity_code, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      if (out_price)
        {
          *out_price = sqlite3_column_int (st, 0);
        }
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      rc = SQLITE_NOTFOUND;
    }
  sqlite3_finalize (st);
  return rc;
}


static int
update_price_with_column (sqlite3 *db, const char *col,
                          const char *commodity_code, int new_price)
{
  const char *sql_tmpl = "UPDATE commodities SET %s = ?1 WHERE code = ?2";
  char sql[256];
  snprintf (sql, sizeof sql, sql_tmpl, col);
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, new_price);
  sqlite3_bind_text (st, 2, commodity_code, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE)
    {
      rc = (sqlite3_changes (db) > 0) ? SQLITE_OK : SQLITE_NOTFOUND;
    }
  sqlite3_finalize (st);
  return rc;
}


int
db_commodity_get_price (const char *commodity_code, int *out_price)
{
  if (!commodity_code || !out_price)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_MISUSE;
    }
  static const char *cols[] = { "last_price", "price", "base_price", NULL };


  for (int i = 0; cols[i]; i++)
    {
      int rc =
        select_price_with_column (db, cols[i], commodity_code, out_price);


      if (rc == SQLITE_OK || rc == SQLITE_NOTFOUND)
        {
          return rc;
        }
      /* SQLITE_ERROR / "no such column" → try next candidate. */
    }
  return SQLITE_ERROR;
}


int
db_commodity_update_price (const char *commodity_code, int new_price)
{
  if (!commodity_code)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_MISUSE;
    }
  static const char *cols[] = { "last_price", "price", "base_price", NULL };


  for (int i = 0; cols[i]; i++)
    {
      int rc =
        update_price_with_column (db, cols[i], commodity_code, new_price);


      if (rc == SQLITE_OK || rc == SQLITE_NOTFOUND)
        {
          return rc;
        }
    }
  return SQLITE_ERROR;
}


/* ----------------------------------------------------------------------
 * Commodities: orders & trades
 * ---------------------------------------------------------------------- */
int
db_commodity_create_order (const char *actor_type, int actor_id,
                           const char *commodity_code, const char *side,
                           int quantity, int price)
{
  if (!actor_type || !commodity_code || !side)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_MISUSE;
    }

  /* Minimal insert – relies on defaults for status, timestamps, etc.
   * May need extending once the full commodity_orders schema is final.
   */
  const char *sql =
    "INSERT INTO commodity_orders "
    "  (commodity_id, actor_type, actor_id, side, quantity, price) "
    "SELECT c.id, ?2, ?3, ?4, ?5, ?6 "
    "FROM commodities c " "WHERE c.code = ?1";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_text (st, 1, commodity_code, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (st, 2, actor_type, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 3, actor_id);
  sqlite3_bind_text (st, 4, side, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 5, quantity);
  sqlite3_bind_int (st, 6, price);
  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE)
    {
      rc = (sqlite3_changes (db) > 0) ? SQLITE_OK : SQLITE_NOTFOUND;
    }
  sqlite3_finalize (st);
  return rc;
}


int
db_commodity_fill_order (int order_id, int quantity)
{
  if (order_id <= 0 || quantity <= 0)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_MISUSE;
    }
  /* Fetch current quantity & status */
  const char *sql_sel =
    "SELECT quantity, status " "FROM commodity_orders " "WHERE id = ?1";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql_sel, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, order_id);
  rc = sqlite3_step (st);
  if (rc != SQLITE_ROW)
    {
      sqlite3_finalize (st);
      return (rc == SQLITE_DONE) ? SQLITE_NOTFOUND : rc;
    }
  int current_qty = sqlite3_column_int (st, 0);
  const unsigned char *status_text = sqlite3_column_text (st, 1);


  /* Optional: enforce only 'open' orders */
  if (status_text && strcmp ((const char *) status_text, "open") != 0)
    {
      sqlite3_finalize (st);
      return SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  int remaining = current_qty - quantity;


  if (remaining < 0)
    {
      remaining = 0;
    }
  const char *new_status = (remaining == 0) ? "filled" : "open";
  const char *sql_upd =
    "UPDATE commodity_orders "
    "SET quantity = ?1, status = ?2 " "WHERE id = ?3";
  sqlite3_stmt *st2 = NULL;


  rc = sqlite3_prepare_v2 (db, sql_upd, -1, &st2, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st2, 1, remaining);
  sqlite3_bind_text (st2, 2, new_status, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st2, 3, order_id);
  rc = sqlite3_step (st2);
  if (rc == SQLITE_DONE)
    {
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st2);
  return rc;
}


int
db_commodity_get_orders (const char *commodity_code, const char *status,
                         json_t **out_array)
{
  if (!commodity_code)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_MISUSE;
    }
  const char *sql_with_status =
    "SELECT o.* "
    "FROM commodity_orders o "
    "JOIN commodities c ON o.commodity_id = c.id "
    "WHERE c.code = ?1 AND o.status = ?2 " "ORDER BY o.id ASC";
  const char *sql_no_status =
    "SELECT o.* "
    "FROM commodity_orders o "
    "JOIN commodities c ON o.commodity_id = c.id "
    "WHERE c.code = ?1 " "ORDER BY o.id ASC";
  sqlite3_stmt *st = NULL;
  int rc;


  if (status)
    {
      rc = sqlite3_prepare_v2 (db, sql_with_status, -1, &st, NULL);
      if (rc != SQLITE_OK)
        {
          return rc;
        }
      sqlite3_bind_text (st, 1, commodity_code, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text (st, 2, status, -1, SQLITE_TRANSIENT);
    }
  else
    {
      rc = sqlite3_prepare_v2 (db, sql_no_status, -1, &st, NULL);
      if (rc != SQLITE_OK)
        {
          return rc;
        }
      sqlite3_bind_text (st, 1, commodity_code, -1, SQLITE_TRANSIENT);
    }
  json_t *arr = NULL;


  rc = stmt_to_json_array (st, out_array ? &arr : NULL);
  sqlite3_finalize (st);
  if (rc == SQLITE_OK && out_array)
    {
      *out_array = arr;
    }
  else if (arr)
    {
      json_decref (arr);
    }
  return rc;
}


int
db_commodity_get_trades (const char *commodity_code, int limit,
                         json_t **out_array)
{
  if (!commodity_code)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_MISUSE;
    }
  if (limit <= 0)
    {
      limit = 100;
    }
  const char *sql =
    "SELECT t.* "
    "FROM commodity_trades t "
    "JOIN commodities c ON t.commodity_id = c.id "
    "WHERE c.code = ?1 " "ORDER BY t.id DESC " "LIMIT ?2";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_text (st, 1, commodity_code, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, limit);
  json_t *arr = NULL;


  rc = stmt_to_json_array (st, out_array ? &arr : NULL);
  sqlite3_finalize (st);
  if (rc == SQLITE_OK && out_array)
    {
      *out_array = arr;
    }
  else if (arr)
    {
      json_decref (arr);
    }
  return rc;
}


int
db_planet_get_goods_on_hand (int planet_id, const char *commodity_code,
                             int *out_quantity)
{
  if (!commodity_code || !out_quantity)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_MISUSE;
    }
  const char *sql =
    "SELECT quantity "
    "FROM planet_goods " "WHERE planet_id = ?1 AND commodity = ?2";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, planet_id);
  sqlite3_bind_text (st, 2, commodity_code, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *out_quantity = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      *out_quantity = 0;
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


int
db_planet_update_goods_on_hand (int planet_id, const char *commodity_code,
                                int quantity_change)
{
  if (!commodity_code)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_MISUSE;
    }
  int current = 0;
  int rc = db_planet_get_goods_on_hand (planet_id, commodity_code, &current);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  int new_qty = current + quantity_change;


  if (new_qty < 0)
    {
      new_qty = 0;
    }
  /* Try update first */
  const char *sql_upd =
    "UPDATE planet_goods "
    "SET quantity = ?1 " "WHERE planet_id = ?2 AND commodity = ?3";
  sqlite3_stmt *st = NULL;


  rc = sqlite3_prepare_v2 (db, sql_upd, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, new_qty);
  sqlite3_bind_int (st, 2, planet_id);
  sqlite3_bind_text (st, 3, commodity_code, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE && sqlite3_changes (db) > 0)
    {
      sqlite3_finalize (st);
      return SQLITE_OK;
    }
  sqlite3_finalize (st);
  /* No row existed; insert if quantity > 0 */
  if (new_qty == 0)
    {
      return SQLITE_OK;
    }
  const char *sql_ins =
    "INSERT INTO planet_goods (planet_id, commodity, quantity) "
    "VALUES (?1, ?2, ?3)";


  rc = sqlite3_prepare_v2 (db, sql_ins, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, planet_id);
  sqlite3_bind_text (st, 2, commodity_code, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 3, new_qty);
  rc = sqlite3_step (st);
  if (rc == SQLITE_DONE)
    {
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


// New helper functions for ship destruction and player status
// db_mark_ship_destroyed: Marks a ship as destroyed.
int
db_mark_ship_destroyed (sqlite3 *db, int ship_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql = "UPDATE ships SET destroyed = 1 WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_mark_ship_destroyed: prepare error: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, ship_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("db_mark_ship_destroyed: execute error: %s", sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}


// db_clear_player_active_ship: Clears the active ship for a player.
int
db_clear_player_active_ship (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql = "UPDATE players SET ship = 0 WHERE id = ?;";        // Set ship to 0 (NULL)
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_clear_player_active_ship: prepare error: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("db_clear_player_active_ship: execute error: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}


// db_increment_player_stat: Increments a specified player statistic.
int
db_increment_player_stat (sqlite3 *db, int player_id, const char *stat_name)
{
  sqlite3_stmt *st = NULL;
  int rc;
  char *sql_query = NULL;
  // Using sqlite3_mprintf to safely construct the query with a dynamic column name
  sql_query =
    sqlite3_mprintf ("UPDATE players SET %q = %q + 1 WHERE id = ?;",
                     stat_name, stat_name);
  if (!sql_query)
    {
      return SQLITE_NOMEM;
    }
  rc = sqlite3_prepare_v2 (db, sql_query, -1, &st, NULL);
  sqlite3_free (sql_query);     // Free the allocated string immediately after preparing
  if (rc != SQLITE_OK)
    {
      LOGE ("db_increment_player_stat: prepare error for %s: %s", stat_name,
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("db_increment_player_stat: execute error for %s: %s", stat_name,
            sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}


// db_get_player_xp: Retrieves a player's experience points.
int
db_get_player_xp (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  int xp = 0;
  const char *sql = "SELECT experience FROM players WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_player_xp: prepare error: %s", sqlite3_errmsg (db));
      return 0;                 // Return 0 on error
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      xp = sqlite3_column_int (st, 0);
    }
  else if (rc != SQLITE_DONE)
    {
      LOGE ("db_get_player_xp: execute error: %s", sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return xp;
}


// db_update_player_xp: Updates a player's experience points.
int
db_update_player_xp (sqlite3 *db, int player_id, int new_xp)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql = "UPDATE players SET experience = ? WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_update_player_xp: prepare error: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, new_xp);
  sqlite3_bind_int (st, 2, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("db_update_player_xp: execute error: %s", sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}


// db_shiptype_has_escape_pod: Checks if a ship type has an escape pod.
bool
db_shiptype_has_escape_pod (sqlite3 *db, int ship_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  int type_id = -1;
  const char *sql = "SELECT type_id FROM ships WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_shiptype_has_escape_pod: prepare error: %s",
            sqlite3_errmsg (db));
      return false;             // Safest default
    }
  sqlite3_bind_int (st, 1, ship_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      type_id = sqlite3_column_int (st, 0);
    }
  else if (rc != SQLITE_DONE)
    {
      LOGE ("db_shiptype_has_escape_pod: execute error: %s",
            sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  // Per game rules, only the Escape Pod (shiptype_id 0) itself lacks an escape pod.
  // If we couldn't find the ship or its type, we default to false for safety.
  if (type_id == -1)
    {
      return false;
    }
  return (type_id != 0);
}


// db_get_player_podded_count_today: Retrieves player's podded count for today.
int
db_get_player_podded_count_today (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  int count = 0;
  const char *sql =
    "SELECT podded_count_today FROM podded_status WHERE player_id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_player_podded_count_today: prepare error: %s",
            sqlite3_errmsg (db));
      return 0;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      count = sqlite3_column_int (st, 0);
    }
  else if (rc == SQLITE_DONE)
    {
      // No entry, create one
      db_create_podded_status_entry (db, player_id);    // This will insert a default row
      // After creation, count is 0
    }
  else
    {
      LOGE ("db_get_player_podded_count_today: execute error: %s",
            sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return count;
}


// db_get_player_podded_last_reset: Retrieves player's last podded reset timestamp.
long long
db_get_player_podded_last_reset (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  long long timestamp = 0;
  const char *sql =
    "SELECT podded_last_reset FROM podded_status WHERE player_id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_player_podded_last_reset: prepare error: %s",
            sqlite3_errmsg (db));
      return 0;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      timestamp = sqlite3_column_int64 (st, 0);
    }
  else if (rc == SQLITE_DONE)
    {
      // No entry, create one
      db_create_podded_status_entry (db, player_id);    // This will insert a default row
      // After creation, current timestamp should be returned as default
      timestamp = time (NULL);
    }
  else
    {
      LOGE ("db_get_player_podded_last_reset: execute error: %s",
            sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return timestamp;
}


// db_reset_player_podded_count: Resets player's podded count and updates last reset timestamp.
int
db_reset_player_podded_count (sqlite3 *db, int player_id, long long timestamp)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql =
    "UPDATE podded_status SET podded_count_today = 0, podded_last_reset = ? WHERE player_id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_reset_player_podded_count: prepare error: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int64 (st, 1, timestamp);
  sqlite3_bind_int (st, 2, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("db_reset_player_podded_count: execute error: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}


// db_update_player_podded_status: Updates a player's podded status.
int
db_update_player_podded_status (sqlite3 *db, int player_id,
                                const char *status, long long big_sleep_until)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql =
    "UPDATE podded_status SET status = ?, big_sleep_until = ? WHERE player_id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_update_player_podded_status: prepare error: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (st, 1, status, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (st, 2, big_sleep_until);
  sqlite3_bind_int (st, 3, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("db_update_player_podded_status: execute error: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}


// db_create_podded_status_entry: Creates a default entry in podded_status for a player.
int
db_create_podded_status_entry (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql =
    "INSERT OR IGNORE INTO podded_status (player_id, podded_count_today, podded_last_reset, status, big_sleep_until) VALUES (?, 0, ?, 'alive', 0);";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_create_podded_status_entry: prepare error: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_bind_int64 (st, 2, time (NULL));
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("db_create_podded_status_entry: execute error: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}


// db_get_shiptype_info: Retrieves holds, fighters, and shields for a shiptype.
int
db_get_shiptype_info (sqlite3 *db, int shiptype_id, int *holds, int *fighters,
                      int *shields)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql =
    "SELECT initialholds, maxfighters, maxshields FROM shiptypes WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_shiptype_info: prepare error: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, shiptype_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *holds = sqlite3_column_int (st, 0);
      *fighters = sqlite3_column_int (st, 1);
      *shields = sqlite3_column_int (st, 2);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      LOGW ("db_get_shiptype_info: No shiptype found for ID %d.",
            shiptype_id);
      rc = SQLITE_NOTFOUND;
    }
  else
    {
      LOGE ("db_get_shiptype_info: execute error: %s", sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return rc;
}


int
db_player_land_on_planet (sqlite3 *db, int player_id, int planet_id)
{
  if (!db)
    {
      return SQLITE_ERROR;
    }
  sqlite3_stmt *st_find_ship = NULL;
  sqlite3_stmt *st_update_ship = NULL;
  sqlite3_stmt *st_update_player = NULL;
  int ship_id = -1;
  int rc = SQLITE_ERROR;
  const char *sql_find_ship = "SELECT ship FROM players WHERE id = ?;";


  if (sqlite3_prepare_v2 (db, sql_find_ship, -1, &st_find_ship, NULL) !=
      SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st_find_ship, 1, player_id);
  if (sqlite3_step (st_find_ship) == SQLITE_ROW)
    {
      ship_id = sqlite3_column_int (st_find_ship, 0);
    }
  sqlite3_finalize (st_find_ship);
  st_find_ship = NULL;
  if (ship_id == -1)
    {
      return SQLITE_ERROR; // Player has no ship or not found
    }
  const char *sql_update_ship =
    "UPDATE ships SET onplanet = ?, sector = NULL, ported = 0 WHERE id = ?;";


  if (sqlite3_prepare_v2 (db, sql_update_ship, -1, &st_update_ship, NULL) !=
      SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st_update_ship, 1, planet_id);
  sqlite3_bind_int (st_update_ship, 2, ship_id);
  if (sqlite3_step (st_update_ship) != SQLITE_DONE)
    {
      return rc;
    }
  const char *sql_update_player =
    "UPDATE players SET lastplanet = ?, sector = NULL WHERE id = ?;";


  if (sqlite3_prepare_v2 (db, sql_update_player, -1, &st_update_player, NULL)
      !=
      SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st_update_player, 1, planet_id);
  sqlite3_bind_int (st_update_player, 2, player_id);
  if (sqlite3_step (st_update_player) != SQLITE_DONE)
    {
      return rc;
    }
  rc = SQLITE_OK;
  if (st_find_ship)
    {
      sqlite3_finalize (st_find_ship);
    }
  if (st_update_ship)
    {
      sqlite3_finalize (st_update_ship);
    }
  if (st_update_player)
    {
      sqlite3_finalize (st_update_player);
    }
  return rc;
}


int
db_player_launch_from_planet (sqlite3 *db, int player_id, int *out_sector_id)
{
  if (!db)
    {
      return SQLITE_ERROR;
    }
  sqlite3_stmt *st = NULL;
  int ship_id = -1;
  int last_planet_id = -1;
  int planet_sector_id = -1;
  int rc = SQLITE_ERROR;
  const char *sql_get_info =
    "SELECT ship, lastplanet FROM players WHERE id = ?;";


  rc = sqlite3_prepare_v2 (db, sql_get_info, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, player_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      ship_id = sqlite3_column_int (st, 0);
      last_planet_id = sqlite3_column_int (st, 1);
    }
  sqlite3_finalize (st);
  st = NULL;
  if (ship_id == -1 || last_planet_id <= 0)
    {
      // Player is not on a planet or has no ship
      return SQLITE_MISUSE;
    }
  const char *sql_get_planet_sector =
    "SELECT sector FROM planets WHERE id = ?;";


  rc = sqlite3_prepare_v2 (db, sql_get_planet_sector, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, last_planet_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      planet_sector_id = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  st = NULL;
  if (planet_sector_id <= 0)
    {
      // Planet has no sector, shouldn't happen
      return SQLITE_ERROR;
    }
  const char *sql_update_ship =
    "UPDATE ships SET onplanet = NULL, sector = ? WHERE id = ?;";


  rc = sqlite3_prepare_v2 (db, sql_update_ship, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, planet_sector_id);
  sqlite3_bind_int (st, 2, ship_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      return rc;
    }
  sqlite3_finalize (st);
  st = NULL;
  const char *sql_update_player =
    "UPDATE players SET sector = ? WHERE id = ?;";


  rc = sqlite3_prepare_v2 (db, sql_update_player, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, planet_sector_id);
  sqlite3_bind_int (st, 2, player_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      return rc;
    }
  rc = SQLITE_OK;
  if (out_sector_id)
    {
      *out_sector_id = planet_sector_id;
    }
  if (st)
    {
      sqlite3_finalize (st);
    }
  return rc;
}


int
db_get_port_sector (sqlite3 *db, int port_id)
{
  if (!db)
    {
      return 0;
    }
  sqlite3_stmt *st = NULL;
  int sector = 0;
  int rc;


  rc = sqlite3_prepare_v2 (db,
                           "SELECT sector FROM ports WHERE id=?1",
                           -1,
                           &st,
                           NULL);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
  sqlite3_bind_int (st, 1, port_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      sector = sqlite3_column_int (st, 0);
    }
done:
  if (st)
    {
      sqlite3_finalize (st);
    }
  return sector;
}


// db_bounty_create: Inserts a new bounty record.
int
db_bounty_create (sqlite3 *db, const char *posted_by_type, int posted_by_id,
                  const char *target_type, int target_id, long long reward,
                  const char *description)
{
  (void) description;
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql =
    "INSERT INTO bounties (posted_by_type, posted_by_id, target_type, target_id, reward, status, posted_ts) "
    "VALUES (?, ?, ?, ?, ?, 'open', strftime('%Y-%m-%dT%H:%M:%fZ','now'));";


  // Note: Ignoring description as per schema verification in plan
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_bounty_create: prepare error: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (st, 1, posted_by_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, posted_by_id);
  sqlite3_bind_text (st, 3, target_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 4, target_id);
  sqlite3_bind_int64 (st, 5, reward);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("db_bounty_create: execute error: %s", sqlite3_errmsg (db));
      return rc;
    }
  return SQLITE_OK;
}


// db_player_get_alignment: Retrieves a player's current alignment.
int
db_player_get_alignment (sqlite3 *db, int player_id, int *alignment)
{
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql = "SELECT alignment FROM players WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_player_get_alignment: prepare error: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *alignment = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      rc = SQLITE_NOTFOUND;
    }
  else
    {
      LOGE ("db_player_get_alignment: execute error: %s",
            sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return rc;
}


// db_get_law_config_int: Retrieves an integer configuration value from law_enforcement_config.
int
db_get_law_config_int (const char *key, int default_value)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      LOGE ("db_get_law_config_int: Failed to get DB handle.");
      return default_value;
    }
  sqlite3_stmt *st = NULL;
  int value = default_value;
  const char *sql =
    "SELECT value FROM law_enforcement_config WHERE key = ? AND value_type = 'INTEGER';";


  db_mutex_lock ();
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_law_config_int: Prepare failed for key '%s': %s",
            key,
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_text (st, 1, key, -1, SQLITE_STATIC);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      value = sqlite3_column_int (st, 0);
    }
  else
    {
      LOGW (
        "db_get_law_config_int: Key '%s' not found or not an INTEGER. Using default: %d",
        key,
        default_value);
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return value;
}


// db_get_law_config_text: Retrieves a text configuration value from law_enforcement_config.
char *
db_get_law_config_text (const char *key, const char *default_value)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      LOGE ("db_get_law_config_text: Failed to get DB handle.");
      return strdup (default_value ? default_value : "");
    }
  sqlite3_stmt *st = NULL;
  char *value = NULL;
  const char *sql =
    "SELECT value FROM law_enforcement_config WHERE key = ? AND value_type = 'TEXT';";


  db_mutex_lock ();
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_law_config_text: Prepare failed for key '%s': %s",
            key,
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_text (st, 1, key, -1, SQLITE_STATIC);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      const unsigned char *text_val = sqlite3_column_text (st, 0);


      value = strdup ((const char *)text_val);
    }
  else
    {
      LOGW (
        "db_get_law_config_text: Key '%s' not found or not TEXT. Using default: '%s'",
        key,
        default_value ? default_value : "");
      value = strdup (default_value ? default_value : "");
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return value;   // Caller is responsible for freeing this memory
}


// db_player_get_last_rob_attempt: Retrieves a player's last rob attempt details.
int
db_player_get_last_rob_attempt (int player_id,
                                int *last_port_id_out,
                                long long *last_attempt_at_out)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_ERROR;
    }
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;


  if (last_port_id_out)
    {
      *last_port_id_out = 0;
    }
  if (last_attempt_at_out)
    {
      *last_attempt_at_out = 0;
    }
  const char *sql =
    "SELECT last_port_id, last_attempt_at FROM player_last_rob WHERE player_id = ?;";


  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_player_get_last_rob_attempt: Prepare failed: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, player_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      if (last_port_id_out)
        {
          *last_port_id_out = sqlite3_column_int (st, 0);
        }
      if (last_attempt_at_out)
        {
          *last_attempt_at_out = sqlite3_column_int64 (st, 1);
        }
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      rc = SQLITE_NOTFOUND;   // No record for this player
    }
  else
    {
      LOGE ("db_player_get_last_rob_attempt: Execute failed: %s",
            sqlite3_errmsg (db));
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


// db_player_set_last_rob_attempt: Records a player's last rob attempt.
int
db_player_set_last_rob_attempt (int player_id,
                                int last_port_id,
                                long long last_attempt_at)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_ERROR;
    }
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;
  const char *sql =
    "INSERT INTO player_last_rob (player_id, last_port_id, last_attempt_at) VALUES (?, ?, ?) "
    "ON CONFLICT(player_id) DO UPDATE SET last_port_id = excluded.last_port_id, last_attempt_at = excluded.last_attempt_at;";


  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_player_set_last_rob_attempt: Prepare failed: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_bind_int (st, 2, last_port_id);
  sqlite3_bind_int64 (st, 3, last_attempt_at);
  if (sqlite3_step (st) == SQLITE_DONE)
    {
      rc = SQLITE_OK;
    }
  else
    {
      LOGE ("db_player_set_last_rob_attempt: Execute failed: %s",
            sqlite3_errmsg (db));
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


// h_get_cluster_id_for_sector: Retrieves the cluster ID for a given sector.
int
h_get_cluster_id_for_sector (sqlite3 *db, int sector_id, int *out_cluster_id)
{
  if (!db || !out_cluster_id)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;
  const char *sql =
    "SELECT cluster_id FROM cluster_sectors WHERE sector_id = ?1;";


  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_get_cluster_id_for_sector: Prepare error: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, sector_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      *out_cluster_id = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      LOGW ("h_get_cluster_id_for_sector: No cluster found for sector ID %d.",
            sector_id);
      rc = SQLITE_NOTFOUND;
    }
  else
    {
      LOGE ("h_get_cluster_id_for_sector: Execute error: %s",
            sqlite3_errmsg (db));
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


// h_get_cluster_alignment: Retrieves the raw alignment score for the cluster containing a given sector.
int
h_get_cluster_alignment (sqlite3 *db, int sector_id, int *out_alignment)
{
  if (!db || !out_alignment)
    {
      return SQLITE_MISUSE;
    }
  int rc = SQLITE_ERROR;
  int cluster_id = 0;


  rc = h_get_cluster_id_for_sector (db, sector_id, &cluster_id);
  if (rc != SQLITE_OK)
    {
      LOGW (
        "h_get_cluster_alignment: Failed to get cluster ID for sector %d. RC: %d",
        sector_id,
        rc);
      // Fallback or specific error handling
      *out_alignment = 0;   // Default to neutral if no cluster
      return rc;
    }
  sqlite3_stmt *st = NULL;
  const char *sql = "SELECT alignment FROM clusters WHERE id = ?1;";


  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_get_cluster_alignment: Prepare error: %s", sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, cluster_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      *out_alignment = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      LOGW ("h_get_cluster_alignment: No alignment found for cluster ID %d.",
            cluster_id);
      *out_alignment = 0;   // Default to neutral if cluster found but no alignment
      rc = SQLITE_NOTFOUND;
    }
  else
    {
      LOGE ("h_get_cluster_alignment: Execute error: %s", sqlite3_errmsg (db));
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


// h_get_cluster_alignment_band: Retrieves the alignment band ID for the cluster containing a given sector.
int
h_get_cluster_alignment_band (sqlite3 *db, int sector_id, int *out_band_id)
{
  if (!db || !out_band_id)
    {
      return SQLITE_MISUSE;
    }
  int rc = SQLITE_ERROR;
  int cluster_alignment_score = 0;


  rc = h_get_cluster_alignment (db, sector_id, &cluster_alignment_score);
  if (rc != SQLITE_OK && rc != SQLITE_NOTFOUND)     // SQLITE_NOTFOUND means default to neutral, which is fine
    {
      LOGE (
        "h_get_cluster_alignment_band: Failed to get cluster alignment for sector %d. RC: %d",
        sector_id,
        rc);
      return rc;
    }
  // Pass the raw alignment score to db_alignment_band_for_value to get the band ID
  // db_alignment_band_for_value also handles its own locking, but we will pass it the mutex for this purpose.
  // However, given it locks internally, calling it directly is fine.
  // It returns SQLITE_OK even on synthetic neutral fallback, so checking rc is enough.
  rc = db_alignment_band_for_value (db,
                                    cluster_alignment_score,
                                    out_band_id,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL);
  return rc;
}


// db_port_add_bust_record: Adds a new bust record for a player at a port.
int
db_port_add_bust_record (int port_id,
                         int player_id,
                         const char *bust_type,
                         long long timestamp)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_ERROR;
    }
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;
  // Use INSERT OR REPLACE to ensure only one active bust record per player per port
  const char *sql =
    "INSERT INTO port_busts (port_id, player_id, last_bust_at, bust_type, active) VALUES (?, ?, ?, ?, 1) "
    "ON CONFLICT(port_id, player_id) DO UPDATE SET last_bust_at = excluded.last_bust_at, bust_type = excluded.bust_type, active = 1;";


  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_port_add_bust_record: Prepare failed: %s", sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, port_id);
  sqlite3_bind_int (st, 2, player_id);
  sqlite3_bind_int64 (st, 3, timestamp);
  sqlite3_bind_text (st, 4, bust_type, -1, SQLITE_STATIC);
  if (sqlite3_step (st) == SQLITE_DONE)
    {
      rc = SQLITE_OK;
    }
  else
    {
      LOGE ("db_port_add_bust_record: Execute failed: %s", sqlite3_errmsg (db));
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


// db_port_get_active_busts: Retrieves the number of active bust records for a player at a specific port.
int
db_port_get_active_busts (int port_id, int player_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_ERROR;
    }
  sqlite3_stmt *st = NULL;
  //int count = 0;
  int rc = SQLITE_ERROR;
  const char *sql =
    "SELECT COUNT(*) FROM port_busts WHERE port_id = ? AND player_id = ? AND active = 1;";


  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_port_get_active_busts: Prepare failed: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, port_id);
  sqlite3_bind_int (st, 2, player_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      sqlite3_column_int (st, 0); // Read the column to advance the statement, but ignore the value
      rc = SQLITE_OK;
    }
  else
    {
      LOGE ("db_port_get_active_busts: Execute failed: %s",
            sqlite3_errmsg (db));
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


/* Return 1 if path exists from `from` to `to`, 0 if no path, <0 on error. */
int
db_path_exists (sqlite3 *db, int from, int to)
{
  int max_id = 0;
  sqlite3_stmt *st = NULL;
  int rc;
  /* Get max id once; in practice you can cache this in caller. */
  rc = sqlite3_prepare_v2 (db, "SELECT MAX(id) FROM sectors", -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return -1;
    }
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      max_id = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  if (max_id <= 0 || from <= 0 || from > max_id || to <= 0 || to > max_id)
    {
      return 0;                 /* treat as “no path” */
    }
  size_t N = (size_t) max_id + 1;
  unsigned char *seen = calloc (N, 1);
  int *queue = malloc (N * sizeof (int));


  if (!seen || !queue)
    {
      free (seen);
      free (queue);
      return -1;
    }
  /* Prepare neighbour query */
  rc = sqlite3_prepare_v2 (db,
                           "SELECT to_sector FROM sector_warps WHERE from_sector = ?1",
                           -1,
                           &st,
                           NULL);
  if (rc != SQLITE_OK || !st)
    {
      free (seen);
      free (queue);
      return -1;
    }
  int qh = 0, qt = 0;


  queue[qt++] = from;
  seen[from] = 1;
  int found = 0;


  while (qh < qt && !found)
    {
      int u = queue[qh++];


      sqlite3_reset (st);
      sqlite3_clear_bindings (st);
      sqlite3_bind_int (st, 1, u);
      while ((rc = sqlite3_step (st)) == SQLITE_ROW)
        {
          int v = sqlite3_column_int (st, 0);


          if (v <= 0 || v > max_id)
            {
              continue;
            }
          if (seen[v])
            {
              continue;
            }
          seen[v] = 1;
          if (v == to)
            {
              found = 1;
              break;
            }
          queue[qt++] = v;
        }
    }
  sqlite3_finalize (st);
  free (seen);
  free (queue);
  return found;
}


/* /\* Return 1 if path exists from `from` to `to`, 0 if no path, <0 on error. *\/ */


/* int */


/* db_path_exists (sqlite3 *db, int from, int to) */


/* { */


/*   int max_id = 0; */


/*   sqlite3_stmt *st = NULL; */


/*   int rc; */


/*   /\* Get max id once; in practice you can cache this in caller. *\/ */


/*   rc = sqlite3_prepare_v2 (db, "SELECT MAX(id) FROM sectors", -1, &st, NULL); */


/*   if (rc != SQLITE_OK) */


/*     return -1; */


/*   if (sqlite3_step (st) == SQLITE_ROW) */


/*     max_id = sqlite3_column_int (st, 0); */


/*   sqlite3_finalize (st); */


/*   if (max_id <= 0 || from <= 0 || from > max_id || to <= 0 || to > max_id) */


/*     return 0;			/\* treat as “no path” *\/ */


/*   size_t N = (size_t) max_id + 1; */


/*   unsigned char *seen = calloc (N, 1); */


/*   int *queue = malloc (N * sizeof (int)); */


/*   if (!seen || !queue) */


/*     { */


/*       free (seen); */


/*       free (queue); */


/*       return -1; */


/*     } */


/*   /\* Prepare neighbour query *\/ */


/*   rc = sqlite3_prepare_v2 (db, */


/*                         "SELECT to_sector FROM sector_warps WHERE from_sector = ?1", */


/*                         -1, &st, NULL); */


/*   if (rc != SQLITE_OK || !st) */


/*     { */


/*       free (seen); */


/*       free (queue); */


/*       return -1; */


/*     } */


/*   int qh = 0, qt = 0; */


/*   queue[qt++] = from; */


/*   seen[from] = 1; */


/*   int found = 0; */


/*   while (qh < qt && !found) */


/*     { */


/*       int u = queue[qh++]; */


/*       sqlite3_reset (st); */


/*       sqlite3_clear_bindings (st); */


/*       sqlite3_bind_int (st, 1, u); */


/*       while ((rc = sqlite3_step (st)) == SQLITE_ROW) */


/*      { */


/*        int v = sqlite3_column_int (st, 0); */


/*        if (v <= 0 || v > max_id) */


/*          continue; */


/*        if (seen[v]) */


/*          continue; */


/*        seen[v] = 1; */


/*        if (v == to) */


/*          { */


/*            found = 1; */


/*            break; */


/*          } */


/*        queue[qt++] = v; */


/*      } */


/*     } */


/*   sqlite3_finalize (st); */


/*   free (seen); */


/*   free (queue); */


/*   return found; */


/* } */


// Implementation of db_get_config_int (thread-safe wrapper)
int
db_get_config_int (sqlite3 *db, const char *key_col_name, int default_value)
{
  db_mutex_lock ();
  long long value =
    h_get_config_int_unlocked (db, key_col_name, (long long) default_value);


  db_mutex_unlock ();
  return (int) value;
}


// Implementation of db_get_config_bool (thread-safe wrapper)
bool
db_get_config_bool (sqlite3 *db, const char *key_col_name, bool default_value)
{
  db_mutex_lock ();
  long long value =
    h_get_config_int_unlocked (db, key_col_name, (long long) default_value);


  db_mutex_unlock ();
  return (bool) value;
}


extern long long h_get_account_alert_threshold_unlocked (sqlite3 *db,
                                                         int account_id,
                                                         const char
                                                         *owner_type);


extern int h_get_account_id_unlocked (sqlite3 *db, const char *owner_type,
                                      int owner_id, int *account_id_out);
extern int h_create_bank_account_unlocked (sqlite3 *db,
                                           const char *owner_type,
                                           int owner_id,
                                           long long initial_balance,
                                           int *account_id_out);
extern int h_deduct_credits_unlocked (sqlite3 *db, int account_id,
                                      long long amount, const char *tx_type,
                                      const char *tx_group_id,
                                      long long *new_balance_out);
extern int h_add_credits_unlocked (sqlite3 *db, int account_id,
                                   long long amount, const char *tx_type,
                                   const char *tx_group_id,
                                   long long *new_balance_out);
extern int db_notice_create (const char *title,
                             const char *body,
                             const char *severity,
                             time_t expires_at);                                                                // Existing system notice creation


// --- NEW HELPER: For creating personalized bank alert notices ---
// This helper function creates a system notice but includes context data
// that can be used by a communication module to target specific players
// or filter for relevant notices.
static int
h_create_personal_bank_alert_notice (sqlite3 *db,
                                     const char *target_owner_type,
                                     int target_owner_id, const char *title,
                                     const char *body,
                                     json_t *transfer_context)
{
  if (!db || !target_owner_type || target_owner_id <= 0 || !title || !body)
    {
      LOGE ("h_create_personal_bank_alert_notice: Invalid arguments.");
      return -1;
    }
  json_t *full_context = json_object ();


  if (!full_context)
    {
      LOGE ("h_create_personal_bank_alert_notice: OOM creating context.");
      return -1;
    }
  // Add target owner info to the context
  json_object_set_new (full_context, "target_owner_type",
                       json_string (target_owner_type));
  json_object_set_new (full_context, "target_owner_id",
                       json_integer (target_owner_id));
  // Add transfer-specific context
  if (transfer_context)
    {
      // Deep copy the transfer_context into full_context to avoid decref issues later
      json_t *transfer_context_copy = json_deep_copy (transfer_context);


      if (transfer_context_copy)
        {
          json_object_update (full_context, transfer_context_copy);
          json_decref (transfer_context_copy);  // The update function increments ref, so we decref original copy
        }
      else
        {
          LOGE
          (
            "h_create_personal_bank_alert_notice: OOM copying transfer_context.");
        }
    }
  // Dump context as body for db_notice_create
  char *context_str = json_dumps (full_context, JSON_COMPACT);


  if (!context_str)
    {
      LOGE ("h_create_personal_bank_alert_notice: OOM dumping context JSON.");
      json_decref (full_context);
      return -1;
    }
  // For now, severity will be "info", expires_at will be current time + 7 days
  // The actual delivery will be handled by the subscription/notification system.
  time_t expires_at_ts = time (NULL) + (7 * 24 * 60 * 60);      // 7 days from now
  int rc = db_notice_create (title, context_str, "info", expires_at_ts);        // Use the original db_notice_create


  free (context_str);
  json_decref (full_context);
  if (rc < 0)
    {
      LOGE
        ("h_create_personal_bank_alert_notice: Failed to create system notice.");
      return -1;
    }
  return 0;
}


/**
 * @brief Transfers credits between two owners.
 * Updated for Issue 376: Now triggers bank alerts if transfer size exceeds defined thresholds.
 *
 * @param db The SQLite database handle.
 * @param from_owner_type The type of the sender's owner (e.g., "player", "corp").
 * @param from_owner_id The ID of the sender's owner.
 * @param to_owner_type The type of the recipient's owner.
 * @param to_owner_id The ID of the recipient's owner.
 * @param amount The amount to transfer.
 * @param tx_type The transaction type (e.g., "TRANSFER").
 * @param tx_group_id An optional group ID for related transactions.
 * @return SQLITE_OK on success, or an SQLite error code.
 */
int
h_bank_transfer_unlocked (sqlite3 *db,
                          const char *from_owner_type, int from_owner_id,
                          const char *to_owner_type, int to_owner_id,
                          long long amount,
                          const char *tx_type, const char *tx_group_id)
{
  int from_account_id, to_account_id;
  int rc;
  // Get source account ID
  rc =
    h_get_account_id_unlocked (db, from_owner_type, from_owner_id,
                               &from_account_id);
  if (rc != SQLITE_OK)
    {
      // If source account doesn't exist, and it's not a system account, it's an error.
      // System accounts might be implicit.
      if (strcmp (from_owner_type, "system") != 0
          && strcmp (from_owner_type, "gov") != 0)
        {
          LOGW ("h_bank_transfer_unlocked: Source account %s:%d not found.",
                from_owner_type, from_owner_id);
          return SQLITE_NOTFOUND;
        }
      // For system/gov, if not found, it implies no balance to deduct from, so treat as insufficient funds
      LOGW
      (
        "h_bank_transfer_unlocked: Implicit system/gov source account %s:%d not found or created. Treating as insufficient funds.",
        from_owner_type,
        from_owner_id);
      return SQLITE_CONSTRAINT; // Special return for insufficient funds
    }
  // Get or create destination account ID
  rc =
    h_get_account_id_unlocked (db, to_owner_type, to_owner_id,
                               &to_account_id);
  if (rc == SQLITE_NOTFOUND)
    {
      rc =
        h_create_bank_account_unlocked (db, to_owner_type, to_owner_id, 0,
                                        &to_account_id);
      if (rc != SQLITE_OK)
        {
          LOGE
          (
            "h_bank_transfer_unlocked: Failed to create destination account %s:%d: %s",
            to_owner_type,
            to_owner_id,
            sqlite3_errmsg (db));
          return rc;
        }
    }
  else if (rc != SQLITE_OK)
    {
      LOGE
        ("h_bank_transfer_unlocked: Failed to get destination account %s:%d: %s",
        to_owner_type,
        to_owner_id,
        sqlite3_errmsg (db));
      return rc;
    }
  // Deduct from source
  rc =
    h_deduct_credits_unlocked (db, from_account_id, amount, tx_type,
                               tx_group_id, NULL);
  if (rc != SQLITE_OK)
    {
      LOGW
      (
        "h_bank_transfer_unlocked: Failed to deduct %lld from %s:%d (account %d). Error: %d",
        amount,
        from_owner_type,
        from_owner_id,
        from_account_id,
        rc);
      return rc;                // Insufficient funds or other deduction error
    }
  // Add to destination
  rc =
    h_add_credits_unlocked (db, to_account_id, amount, tx_type, tx_group_id,
                            NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
      (
        "h_bank_transfer_unlocked: Failed to add %lld to %s:%d (account %d). This should not happen after successful deduction. Error: %d",
        amount,
        to_owner_type,
        to_owner_id,
        to_account_id,
        rc);
      // Attempt to refund the source account (critical error recovery)
      h_add_credits_unlocked (db, from_account_id, amount, "REFUND",
                              "TRANSFER_FAILED", NULL);
      return rc;
    }

  /* --------------------------------------------------------------------------
   * Issue 376: Logic for "bank.alerts"
   * Check thresholds and generate notices for large transfers.
   * -------------------------------------------------------------------------- */
  {
    long long from_threshold = 0;
    long long to_threshold = 0;


    // 1. Check Thresholds
    // We retrieve the alert threshold for both accounts.
    // h_get_account_alert_threshold_unlocked is assumed to return long long and take owner_type.
    from_threshold =
      h_get_account_alert_threshold_unlocked (db, from_account_id,
                                              from_owner_type);
    to_threshold =
      h_get_account_alert_threshold_unlocked (db, to_account_id,
                                              to_owner_type);
    // 2. Evaluate Alert
    // Trigger if amount meets/exceeds threshold, provided threshold is positive.
    int alert_sender = (from_threshold > 0 && amount >= from_threshold);
    int alert_receiver = (to_threshold > 0 && amount >= to_threshold);


    if (alert_sender || alert_receiver)
      {
        // Prepare context data common to both notices
        json_t *transfer_context = json_object ();


        if (transfer_context)
          {
            json_object_set_new (transfer_context, "amount",
                                 json_integer (amount));
            json_object_set_new (transfer_context, "from_owner_type",
                                 json_string (from_owner_type));
            json_object_set_new (transfer_context, "from_owner_id",
                                 json_integer (from_owner_id));
            json_object_set_new (transfer_context, "to_owner_type",
                                 json_string (to_owner_type));
            json_object_set_new (transfer_context, "to_owner_id",
                                 json_integer (to_owner_id));
            json_object_set_new (transfer_context, "tx_type",
                                 json_string (tx_type));
            // tx_group_id is optional, only add if not NULL
            if (tx_group_id)
              {
                json_object_set_new (transfer_context, "tx_group_id",
                                     json_string (tx_group_id));
              }
          }
        else
          {
            LOGE
            (
              "h_bank_transfer_unlocked: OOM creating transfer_context for alert.");
          }
        // 3. Construct and Create Notices
        char title_buffer[128];
        char body_buffer[512];  // Using snprintf for body might be better if complex, else just use context_str


        if (alert_sender)
          {
            snprintf (title_buffer, sizeof (title_buffer),
                      "Large Transfer Sent (ID: %d)", from_owner_id);
            snprintf (body_buffer, sizeof (body_buffer),
                      "You sent %lld credits to %s:%d.", amount,
                      to_owner_type, to_owner_id);
            h_create_personal_bank_alert_notice (db, from_owner_type,
                                                 from_owner_id, title_buffer,
                                                 body_buffer,
                                                 transfer_context);
          }
        if (alert_receiver)
          {
            snprintf (title_buffer, sizeof (title_buffer),
                      "Large Transfer Received (ID: %d)", to_owner_id);
            snprintf (body_buffer, sizeof (body_buffer),
                      "You received %lld credits from %s:%d.", amount,
                      from_owner_type, from_owner_id);
            h_create_personal_bank_alert_notice (db, to_owner_type,
                                                 to_owner_id, title_buffer,
                                                 body_buffer,
                                                 transfer_context);
          }
        if (transfer_context)
          {
            json_decref (transfer_context);
          }
      }
  }
  /* -------------------------------------------------------------------------- */
  return SQLITE_OK;
}


////////////////////////////////////////////////////////////////////
int
db_ship_rename_if_owner (sqlite3 *db,
                         int player_id,
                         int ship_id,
                         const char *new_name)
{
  if (!new_name || !*new_name)
    {
      return SQLITE_MISUSE;
    }
  int rc = SQLITE_OK;
  sqlite3_stmt *st = NULL;
  /* Verify ownership in ship_ownership */
  static const char *SQL_OWN =
    "SELECT 1 FROM ship_ownership WHERE ship_id=? AND player_id=?;";


  rc = sqlite3_prepare_v2 (db, SQL_OWN, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, ship_id);
  sqlite3_bind_int (st, 2, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  st = NULL;
  if (rc != SQLITE_ROW)
    {
      return SQLITE_CONSTRAINT;
    }
  /* Do the rename */
  static const char *SQL_REN = "UPDATE ships SET name=? WHERE id=?;";


  rc = sqlite3_prepare_v2 (db, SQL_REN, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_text (st, 1, new_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (st, 2, ship_id);
  rc = (sqlite3_step (st) == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
  sqlite3_finalize (st);
  st = NULL;
  return rc;
}


int
db_destroy_ship (sqlite3 *db, int player_id, int ship_id)
{
  int rc = SQLITE_ERROR;
  sqlite3_stmt *stmt = NULL;
  // 1. Get ship details before deletion for logging/event creation
  char ship_name[256] = { 0 };
  {
    const char *sql_get_ship_info =
      "SELECT name, sector FROM ships WHERE id = ?;";


    rc = sqlite3_prepare_v2 (db, sql_get_ship_info, -1, &stmt, NULL);
    if (rc != SQLITE_OK)
      {
        return rc;
      }
    sqlite3_bind_int (stmt, 1, ship_id);
    if (sqlite3_step (stmt) == SQLITE_ROW)
      {
        strncpy (ship_name, (const char *) sqlite3_column_text (stmt, 0),
                 sizeof (ship_name) - 1);
        // ship_sector = sqlite3_column_int (stmt, 1); // No longer needed
      }
    sqlite3_finalize (stmt);
    stmt = NULL;
  }
  // 2. Delete from ships table
  const char *sql_delete_ship = "DELETE FROM ships WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql_delete_ship, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (stmt, 1, ship_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE)
    {
      return rc;
    }
  // 3. Delete from ship_ownership table
  const char *sql_delete_ownership =
    "DELETE FROM ship_ownership WHERE ship_id = ?;";


  rc = sqlite3_prepare_v2 (db, sql_delete_ownership, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (stmt, 1, ship_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE)
    {
      return rc;
    }
  // 4. Update player's active ship to 0 if it was the destroyed ship
  const char *sql_update_player =
    "UPDATE players SET ship = 0 WHERE id = ? AND ship = ?;";


  rc = sqlite3_prepare_v2 (db, sql_update_player, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (stmt, 1, player_id);
  sqlite3_bind_int (stmt, 2, ship_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE)
    {
      return rc;
    }
  // Log ship destroyed event
  json_t *payload = json_object ();


  json_object_set_new (payload, "ship_id", json_integer (ship_id));
  json_object_set_new (payload, "ship_name", json_string (ship_name));
  json_object_set_new (payload, "player_id", json_integer (player_id));
  //  h_log_engine_event((long long)time(NULL), "ship.destroyed", "player", player_id, ship_sector, payload, NULL);
  return SQLITE_OK;
}


int
db_create_initial_ship (int player_id, const char *ship_name, int sector_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db || player_id <= 0 || !ship_name || !*ship_name || sector_id <= 0)
    {
      LOGE (
        "db_create_initial_ship: Invalid input player_id=%d, ship_name='%s', sector_id=%d",
        player_id,
        ship_name,
        sector_id);
      return -1;                // Invalid input
    }
  int rc = SQLITE_ERROR;
  sqlite3_stmt *stmt = NULL;
  int ship_type_id = -1;
  int new_ship_id = -1;
  // 1. Get shiptype_id for "Scout Marauder"
  static const char *SQL_GET_SHIPTYPE_ID =
    "SELECT id FROM shiptypes WHERE name = 'Scout Marauder';";


  rc = sqlite3_prepare_v2 (db, SQL_GET_SHIPTYPE_ID, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_create_initial_ship: Failed to prepare shiptype ID lookup: %s",
            sqlite3_errmsg (db));
      return -1;
    }
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      ship_type_id = sqlite3_column_int (stmt, 0);
      LOGD ("db_create_initial_ship: Found 'Scout Marauder' with ID: %d",
            ship_type_id);
    }
  else
    {
      LOGE (
        "db_create_initial_ship: 'Scout Marauder' shiptype not found in DB. rc: %d",
        rc);
    }
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (ship_type_id == -1)
    {
      LOGE ("db_create_initial_ship: Ship type ID for 'Scout Marauder' is -1.");
      return -1;
    }
  // 2. Create a new ship in the ships table
  static const char *SQL_CREATE_SHIP =
    "INSERT INTO ships ("
    "  type_id, name, sector, "
    "  holds, fighters, shields, "
    "  ore, organics, equipment, colonists, "
    "  flags, perms, "
    "  attack, "
    "  mines, limpets, genesis, "
    "  detonators, probes, photons, beacons, "
    "  cloaking_devices, "
    "  has_transwarp, has_planet_scanner, has_long_range_scanner"
    ") "
    "SELECT "
    "  st.id, ?, ?, "
    "  st.initialholds, st.maxfighters, st.maxshields, "
    "  0, 0, 0, 0, "
    "  777, 731, "
    "  st.maxattack, "
    "  0, 0, 0, "
    "  0, 0, 0, 0, "
    "  0, "
    "  st.can_transwarp, st.can_planet_scan, st.can_long_range_scan "
    "FROM shiptypes st WHERE st.id = ?;";


  rc = sqlite3_prepare_v2 (db, SQL_CREATE_SHIP, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_create_initial_ship: Failed to prepare SQL_CREATE_SHIP: %s",
            sqlite3_errmsg (db));
      return -1;
    }
  sqlite3_bind_text (stmt, 1, ship_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int (stmt, 2, sector_id);
  sqlite3_bind_int (stmt, 3, ship_type_id);     // shiptype_id for WHERE clause
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("db_create_initial_ship: Failed to execute SQL_CREATE_SHIP: %s",
            sqlite3_errmsg (db));
      sqlite3_finalize (stmt);
      return -1;
    }
  sqlite3_finalize (stmt);
  stmt = NULL;
  new_ship_id = sqlite3_last_insert_rowid (db);
  if (new_ship_id <= 0)
    {
      LOGE ("db_create_initial_ship: Failed to get new_ship_id after insert.");
      return -1;
    }
  LOGD ("db_create_initial_ship: New ship created with ID: %d", new_ship_id);
  // 3. Assign ownership using h_ship_claim_unlocked
  // db_ship_claim also updates players.ship and sets primary status
  // Since we are already in a transaction, use the unlocked version
  rc = h_ship_claim_unlocked (db, player_id, sector_id, new_ship_id, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_create_initial_ship: Failed to claim new ship: %s",
            sqlite3_errmsg (db));
      return -1;
    }
  LOGD ("db_create_initial_ship: Ship claimed by player %d.", player_id);
  return new_ship_id;
}


// Helper to check if player is NPC
static int
db_is_npc_player (sqlite3 *db, int player_id)
{
  int rc;
  sqlite3_stmt *st = NULL;
  int is_npc = 0;

  /* Example policy:
     - players.flags has an NPC bit, OR
     - player name is 'Ferrengi' or 'Imperial Starship' (fallback) */
  rc = sqlite3_prepare_v2 (db,
                           "SELECT " "  CASE "
                           "    WHEN (flags & 0x0008) != 0 THEN 1 "                                             /* PFLAG_NPC example */
                           "    WHEN name IN ('Ferrengi','Imperial Starship') THEN 1 "
                           "    ELSE 0 "
                           "  END "
                           "FROM players WHERE id=?;",
                           -1,
                           &st,
                           NULL);
  if (rc != SQLITE_OK)
    {
      return 0;
    }
  sqlite3_bind_int (st, 1, player_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      is_npc = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  return is_npc;
}


/* Call when a ship’s pilot changes (or after spawning an NPC ship). */
int
db_apply_lock_policy_for_pilot (sqlite3 *db,
                                int ship_id,
                                int new_pilot_player_id_or_0)
{
  int rc;
  if (new_pilot_player_id_or_0 > 0
      && db_is_npc_player (db, new_pilot_player_id_or_0))
    {
      /* NPC piloted -> lock it */
      sqlite3_stmt *st = NULL;


      rc = sqlite3_prepare_v2 (db,
                               "UPDATE ships SET flags = COALESCE(flags,0) | ? WHERE id=?;",
                               -1,
                               &st,
                               NULL);
      if (rc == SQLITE_OK)
        {
          sqlite3_bind_int (st, 1, SHIPF_LOCKED);
          sqlite3_bind_int (st, 2, ship_id);
          rc = (sqlite3_step (st) == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
        }
      sqlite3_finalize (st);
    }
  else
    {
      /* Not NPC piloted (or unpiloted) -> clear LOCK (engine managed) */
      sqlite3_stmt *st = NULL;


      rc = sqlite3_prepare_v2 (db,
                               "UPDATE ships SET flags = COALESCE(flags,0) & ~? WHERE id=?;",
                               -1,
                               &st,
                               NULL);
      if (rc == SQLITE_OK)
        {
          sqlite3_bind_int (st, 1, SHIPF_LOCKED);
          sqlite3_bind_int (st, 2, ship_id);
          rc = (sqlite3_step (st) == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
        }
      sqlite3_finalize (st);
    }
  return rc;
}


/* Thread-safe: picks a random name into `out`.
   Returns SQLITE_OK on success, SQLITE_NOTFOUND if table empty, or SQLite error code. */
int
db_rand_npc_shipname (char *out, size_t out_sz)
{
  if (out && out_sz)
    {
      out[0] = '\0';
    }
  int rc;


  db_mutex_lock ();
  sqlite3_stmt *st = NULL;
  static const char *SQL =
    "SELECT name FROM npc_shipnames ORDER BY RANDOM() LIMIT 1;";


  rc =
    sqlite3_prepare_v3 (db_get_handle (),
                        SQL,
                        -1,
                        SQLITE_PREPARE_PERSISTENT,
                        &st,
                        NULL);
  if (rc == SQLITE_OK)
    {
      rc = sqlite3_step (st);
      if (rc == SQLITE_ROW)
        {
          const unsigned char *uc = sqlite3_column_text (st, 0);
          const char *name = (const char *) (uc ? (const char *) uc : "");


          // const char *name = (const char *) (uc ? uc : "");
          if (out && out_sz)
            {
              /* safe copy; always NUL-terminated */
              snprintf (out, out_sz, "%s", name);
            }
          rc = SQLITE_OK;
        }
      else if (rc == SQLITE_DONE)
        {
          rc = SQLITE_NOTFOUND; /* no rows in npc_shipnames */
        }
      else
        {
          /* rc is already an error code from sqlite3_step */
        }
    }
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


int
db_ensure_ship_perms_column (void)
{
  int rc;
  sqlite3 *db = db_get_handle ();
  db_mutex_lock ();
  rc = db_ensure_ship_perms_column_unlocked (db);
  db_mutex_unlock ();
  return rc;
}


/* Unlocked: caller already holds db_mutex */
static int
column_exists_unlocked (sqlite3 *db, const char *table, const char *col)
{
  sqlite3_stmt *st = NULL;
  const char *sql =
    "PRAGMA table_info(?)";
  int exists = 0;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("column_exists_unlocked: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_text (st, 1, table, -1, SQLITE_STATIC);
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      const char *name = (const char *) sqlite3_column_text (st, 1);


      if (strcasecmp (name, col) == 0)
        {
          exists = 1;
          break;
        }
    }
cleanup:
  sqlite3_finalize (st);
  return exists;
}


/* Unlocked: caller already holds db_mutex */
static int
db_ensure_ship_perms_column_unlocked (sqlite3 *db)
{
  int rc = SQLITE_OK;
  if (!db)
    {
      return SQLITE_ERROR;
    }
  if (!column_exists_unlocked (db, "ships", "perms"))
    {
      char *errmsg = NULL;


      rc = sqlite3_exec (db,
                         "ALTER TABLE ships ADD COLUMN perms INTEGER NOT NULL DEFAULT 731;",
                         NULL,
                         NULL,
                         &errmsg);
      if (rc != SQLITE_OK)
        {
          LOGE ("ALTER TABLE ships ADD COLUMN perms failed: %s",
                errmsg ? errmsg : "(unknown)");
          sqlite3_free (errmsg);
          return rc;
        }
    }
  return SQLITE_OK;
}


/* ---------- SECTOR SCAN CORE (thread-safe, single statement) ---------- */


/* Shape returned via *out_obj:
   {
     "name": TEXT,
     "safe_zone": INT,
     "port_count": INT,
     "ship_count": INT,
     "planet_count": INT,
     "beacon_text": TEXT
   }
 */
int
db_sector_scan_core (int sector_id, json_t **out_obj)
{
  if (out_obj)
    {
      *out_obj = NULL;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_ERROR;
    }
  const char *sql =
    "SELECT s.name, "
    "       0 AS safe_zone, "
    "       (SELECT COUNT(1) FROM ports   p  WHERE p.sector  = s.id) AS port_count, "
    "       (SELECT COUNT(1) FROM ships   sh WHERE sh.sector = s.id) AS ship_count, "
    "       (SELECT COUNT(1) FROM planets pl WHERE pl.sector  = s.id) AS planet_count, "
    "       s.beacon AS beacon_text " "FROM sectors s WHERE s.id = ?1";
  int rc = SQLITE_ERROR;
  sqlite3_stmt *st = NULL;


  /* Full critical section: prepare → bind → step → finalize */
  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("[scan_core] prepare failed (sector=%d): %s",
            sector_id, sqlite3_errmsg (db));
      goto done;
    }
  sqlite3_bind_int (st, 1, sector_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      const char *name = (const char *) sqlite3_column_text (st, 0);
      int safe_zone = sqlite3_column_int (st, 1);
      int port_count = sqlite3_column_int (st, 2);
      int ship_count = sqlite3_column_int (st, 3);
      int planet_count = sqlite3_column_int (st, 4);
      const char *btxt = (const char *) sqlite3_column_text (st, 5);
      json_t *o = json_object ();


      json_object_set_new (o, "name", json_string (name ? name : "Unknown"));
      json_object_set_new (o, "safe_zone", json_integer (safe_zone));
      json_object_set_new (o, "port_count", json_integer (port_count));
      json_object_set_new (o, "ship_count", json_integer (ship_count));
      json_object_set_new (o, "planet_count", json_integer (planet_count));
      json_object_set_new (o, "beacon_text", json_string (btxt ? btxt : ""));
      if (out_obj)
        {
          *out_obj = o;
        }
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_ERROR;        /* no row → sector missing */
    }
done:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


int
db_notice_create (const char *title, const char *body,
                  const char *severity, time_t expires_at)
{
  static const char *SQL =
    "INSERT INTO system_notice (created_at, title, body, severity, expires_at) "
    "VALUES (strftime('%s','now'), ?1, ?2, ?3, ?4);";
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return -1;
    }
  sqlite3_stmt *stmt = NULL;


  if (sqlite3_prepare_v2 (db, SQL, -1, &stmt, NULL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_text (stmt, 1, title ? title : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, 2, body ? body : "", -1, SQLITE_TRANSIENT);
  sqlite3_bind_text (stmt, 3, severity ? severity : "info", -1,
                     SQLITE_TRANSIENT);
  if (expires_at > 0)
    {
      sqlite3_bind_int64 (stmt, 4, (sqlite3_int64) expires_at);
    }
  else
    {
      sqlite3_bind_null (stmt, 4);
    }
  int rc = sqlite3_step (stmt);
  int ok = (rc == SQLITE_DONE);


  sqlite3_finalize (stmt);
  if (!ok)
    {
      return -1;
    }
  return (int) sqlite3_last_insert_rowid (db);
}


json_t *
db_notice_list_unseen_for_player (int player_id)
{
  static const char *SQL =
    "SELECT n.id, n.created_at, n.title, n.body, n.severity, n.expires_at "
    "FROM system_notice AS n "
    "LEFT JOIN notice_seen AS ns "
    "  ON ns.notice_id = n.id AND ns.player_id = ?1 "
    "WHERE (n.expires_at IS NULL OR n.expires_at > strftime('%s','now')) "
    "  AND ns.notice_id IS NULL " "ORDER BY n.created_at DESC;";
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return NULL;
    }
  sqlite3_stmt *stmt = NULL;


  if (sqlite3_prepare_v2 (db, SQL, -1, &stmt, NULL) != SQLITE_OK)
    {
      return NULL;
    }
  sqlite3_bind_int (stmt, 1, player_id);
  json_t *arr = json_array ();


  if (!arr)
    {
      sqlite3_finalize (stmt);
      return NULL;
    }
  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      int id = sqlite3_column_int (stmt, 0);
      time_t created = (time_t) sqlite3_column_int64 (stmt, 1);
      const unsigned char *title = sqlite3_column_text (stmt, 2);
      const unsigned char *body = sqlite3_column_text (stmt, 3);
      const unsigned char *sev = sqlite3_column_text (stmt, 4);
      sqlite3_int64 expires_i64 = sqlite3_column_type (stmt, 5) == SQLITE_NULL
        ? 0 : sqlite3_column_int64 (stmt, 5);
      json_t *obj = json_object ();
      json_object_set_new (obj, "id", json_integer (id));
      json_object_set_new (obj, "created_at", json_integer ((int) created));
      json_object_set_new (obj, "title", json_string (title ? (const char *) title : ""));
      json_object_set_new (obj, "body", json_string (body ? (const char *) body : ""));
      json_object_set_new (obj, "severity", json_string (sev ? (const char *) sev : "info"));
      json_object_set_new (obj, "expires_at", json_integer ((int) expires_i64));


      if (obj)
        {
          json_array_append_new (arr, obj);
        }
    }
  sqlite3_finalize (stmt);
  return arr;
}


int
db_notice_mark_seen (int notice_id, int player_id)
{
  static const char *SQL =
    "INSERT OR REPLACE INTO notice_seen (notice_id, player_id, seen_at) "
    "VALUES (?1, ?2, strftime('%s','now'));";
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return -1;
    }
  sqlite3_stmt *stmt = NULL;


  if (sqlite3_prepare_v2 (db, SQL, -1, &stmt, NULL) != SQLITE_OK)
    {
      return -1;
    }
  sqlite3_bind_int (stmt, 1, notice_id);
  sqlite3_bind_int (stmt, 2, player_id);
  int rc = sqlite3_step (stmt);


  sqlite3_finalize (stmt);
  return (rc == SQLITE_DONE) ? 0 : -1;
}


int
db_player_name (int64_t player_id, char **out)
{
  if (!out)
    {
      return -2;
    }
  *out = NULL;
  /* Use the same handle + mutex model as the rest of database.c */
  db_mutex_lock ();
  sqlite3 *dbh = db_get_handle ();


  if (!dbh)
    {
      db_mutex_unlock ();
      return -3;
    }
  static const char *SQL =
    "SELECT COALESCE(name, '') AS pname "
    "FROM players " "WHERE id = ?1 " "LIMIT 1";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (dbh, SQL, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      db_mutex_unlock ();
      return -4;
    }
  rc = sqlite3_bind_int64 (st, 1, (sqlite3_int64) player_id);
  if (rc != SQLITE_OK)
    {
      sqlite3_finalize (st);
      db_mutex_unlock ();
      return -5;
    }
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      const unsigned char *txt = sqlite3_column_text (st, 0);


      if (txt && txt[0])
        {
          size_t n = strlen ((const char *) txt);
          char *dup = (char *) malloc (n + 1);


          if (!dup)
            {
              sqlite3_finalize (st);
              db_mutex_unlock ();
              return -6;
            }
          memcpy (dup, txt, n + 1);
          *out = dup;           /* caller will free() */
          sqlite3_finalize (st);
          db_mutex_unlock ();
          return 0;
        }
      /* had a row but empty name — treat as not found */
    }
  sqlite3_finalize (st);
  db_mutex_unlock ();
  return -1;                    /* not found */
}


int
db_get_ship_name (sqlite3 *db, int ship_id, char **out_name)
{
  if (!out_name)
    {
      return -2;
    }
  *out_name = NULL;
  db_mutex_lock ();
  sqlite3 *dbh = db;


  if (!dbh)
    {
      db_mutex_unlock ();
      return -3;
    }
  static const char *SQL =
    "SELECT COALESCE(name, '') FROM ships WHERE id = ?1 LIMIT 1";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (dbh, SQL, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_ship_name: prepare failed: %s", sqlite3_errmsg (dbh));
      db_mutex_unlock ();
      return -4;
    }
  sqlite3_bind_int (st, 1, ship_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      const unsigned char *txt = sqlite3_column_text (st, 0);


      if (txt && txt[0])
        {
          *out_name = strdup ((const char *)txt);
          if (!*out_name)
            {
              rc = SQLITE_NOMEM;
            }
          else
            {
              rc = SQLITE_OK;
            }
        }
      else
        {
          rc = SQLITE_NOTFOUND;   // Ship found, but name is empty
        }
    }
  else if (rc == SQLITE_DONE)
    {
      rc = SQLITE_NOTFOUND;   // No ship with that ID
    }
  else
    {
      LOGE ("db_get_ship_name: execution failed: %s", sqlite3_errmsg (dbh));
      rc = SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  db_mutex_unlock ();
  return rc;
}


int
db_get_port_name (sqlite3 *db, int port_id, char **out_name)
{
  if (!out_name)
    {
      return -2;
    }
  *out_name = NULL;
  db_mutex_lock ();
  sqlite3 *dbh = db;


  if (!dbh)
    {
      db_mutex_unlock ();
      return -3;
    }
  static const char *SQL =
    "SELECT COALESCE(name, '') FROM ports WHERE id = ?1 LIMIT 1";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (dbh, SQL, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("db_get_port_name: prepare failed: %s", sqlite3_errmsg (dbh));
      db_mutex_unlock ();
      return -4;
    }
  sqlite3_bind_int (st, 1, port_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      const unsigned char *txt = sqlite3_column_text (st, 0);


      if (txt && txt[0])
        {
          *out_name = strdup ((const char *)txt);
          if (!*out_name)
            {
              rc = SQLITE_NOMEM;
            }
          else
            {
              rc = SQLITE_OK;
            }
        }
      else
        {
          rc = SQLITE_NOTFOUND;   // Port found, but name is empty
        }
    }
  else if (rc == SQLITE_DONE)
    {
      rc = SQLITE_NOTFOUND;   // No port with that ID
    }
  else
    {
      LOGE ("db_get_port_name: execution failed: %s", sqlite3_errmsg (dbh));
      rc = SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  db_mutex_unlock ();
  return rc;
}


// Note: This SQL omits the 'id' (autoincrement) and 'processed_at' (defaults to NULL)
static const char *INSERT_ENGINE_EVENT_SQL =
  "INSERT INTO engine_events (ts, type, actor_owner_type, actor_owner_id, sector_id, payload) "
  "VALUES (?, ?, ?, ?, ?, ?);";


int
db_log_engine_event (long long ts,
                     const char *type,
                     const char *actor_owner_type, int actor_player_id,
                     int sector_id, json_t *payload, const char *idem_key)
{
  (void) idem_key;
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;
  // 1. Convert the JSON payload object into a serialised string
  char *payload_str = json_dumps (payload, JSON_COMPACT);


  if (!payload_str)
    {
      // fprintf(stderr, "Error: Failed to serialize JSON payload.\n");
      return SQLITE_NOMEM;      // No memory or serialization error
    }
  // 2. Prepare the statement
  rc = sqlite3_prepare_v2 (db, INSERT_ENGINE_EVENT_SQL, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      // fprintf(stderr, "DB Error (prepare): %s\n", sqlite3_errmsg(db));
      goto cleanup;
    }
  // 3. Bind parameters
  sqlite3_bind_int64 (stmt, 1, ts);
  sqlite3_bind_text (stmt, 2, type, -1, SQLITE_STATIC);
  // Bind owner type and ID
  if (actor_owner_type && strlen (actor_owner_type) > 0)
    {
      sqlite3_bind_text (stmt, 3, actor_owner_type, -1, SQLITE_STATIC);
    }
  else
    {
      sqlite3_bind_null (stmt, 3);
    }
  if (actor_player_id > 0)
    {
      sqlite3_bind_int (stmt, 4, actor_player_id);
    }
  else
    {
      sqlite3_bind_null (stmt, 4);
    }
  if (sector_id > 0)
    {
      sqlite3_bind_int (stmt, 5, sector_id);
    }
  else
    {
      sqlite3_bind_null (stmt, 5);
    }
  // Bind the JSON string (SQLITE_TRANSIENT copies the string, safe for us to free)
  sqlite3_bind_text (stmt, 6, payload_str, -1, SQLITE_TRANSIENT);
  // 4. Execute the statement
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      // fprintf(stderr, "DB Error (step): %s\n", sqlite3_errmsg(db));
      rc = SQLITE_ERROR;        // Set a general error code for return
    }
  else
    {
      rc = SQLITE_OK;           // Success
    }
cleanup:
  // 5. Cleanup
  sqlite3_finalize (stmt);
  // Free the string created by json_dumps
  free (payload_str);
  return rc;
}


int
db_news_insert_feed_item (int ts, const char *category, const char *scope,
                          const char *headline, const char *body,
                          json_t *context_data)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;
  char *article_text = NULL;
  char *context_str = NULL;
  int expiration_ts = ts + NEWS_EXPIRATION_SECONDS;     // NEWS_EXPIRATION_SECONDS is defined in server_cron.h
  // Combine headline and body into article_text
  // Add scope to context_data if it's not null
  if (scope && strlen (scope) > 0)
    {
      if (!context_data)
        {
          context_data = json_object ();
        }
      json_object_set_new (context_data, "scope", json_string (scope));
    }
  if (context_data)
    {
      context_str = json_dumps (context_data,
                                JSON_COMPACT);
      if (!context_str)
        {
          LOGE
            ("ERROR: db_news_insert_feed_item Failed to serialize context_data.");
          goto cleanup;
        }
      if (asprintf
            (&article_text, "HEADLINE: %s\nBODY: %s\nCONTEXT: %s", headline,
            body, context_str) == -1)
        {
          LOGE
          (
            "ERROR: db_news_insert_feed_item Failed to allocate article_text with context.");
          goto cleanup;
        }
    }
  else
    {
      if (asprintf (&article_text, "HEADLINE: %s\nBODY: %s", headline, body)
          == -1)
        {
          LOGE
          (
            "ERROR: db_news_insert_feed_item Failed to allocate article_text without context.");
          goto cleanup;
        }
    }
  if (!article_text)
    {
      LOGE
        ("ERROR: db_news_insert_feed_item Failed to allocate article_text.");
      goto cleanup;
    }
  const char *sql =
    "INSERT INTO news_feed (published_ts, expiration_ts, news_category, article_text) "
    "VALUES (?, ?, ?, ?);";


  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("ERROR: db_news_insert_feed_item prepare failed: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (stmt, 1, ts);
  sqlite3_bind_int (stmt, 2, expiration_ts);
  sqlite3_bind_text (stmt, 3, category, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 4, article_text, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("ERROR: db_news_insert_feed_item execution failed: %s",
            sqlite3_errmsg (db));
      rc = SQLITE_ERROR;
    }
  else
    {
      rc = SQLITE_OK;
    }
cleanup:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  if (article_text)
    {
      free (article_text);
    }
  if (context_str)
    {
      free (context_str);
    }
  if (context_data)
    {
      json_decref (context_data); // Decref if created locally or passed in
    }
  return rc;
}


int
db_is_sector_fedspace (int ck_sector)
{
  sqlite3 *db = db_get_handle ();
  //sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;
  static const char *FEDSPACE_SQL =
    "SELECT sector_id from stardock_location where sector_id=?1;";
  sqlite3_stmt *st = NULL;
  /* Full critical section: prepare → bind → step → finalize */
  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, FEDSPACE_SQL, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("db_is_sector_fedspace: %s", sqlite3_errmsg (db));
      goto done;
    }
  sqlite3_bind_int (st, 1, ck_sector);
  rc = sqlite3_step (st);
  int sec_ret = 1;


  if (rc == SQLITE_ROW)
    {
      sec_ret = sqlite3_column_int (st, 0);
    }
  if ((ck_sector == sec_ret) || (ck_sector >= 1 && ck_sector <= 10))
    {
      rc = 1;
    }
  else
    {
      rc = 0;
    }
done:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


/* Returns the port_id (primary key) for a given sector, or -1 on error/not found */
int
db_get_port_id_by_sector (int sector_id)
{
  sqlite3 *db = db_get_handle ();       // Get the handle
  sqlite3_stmt *stmt = NULL;
  int port_id = -1;
  const char *sql = "SELECT id FROM ports WHERE sector=?1;";
  db_mutex_lock ();       // Critical section starts
  if (sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
      LOGE ("db_get_port_id_by_sector: %s", sqlite3_errmsg (db));
      goto done;
    }
  sqlite3_bind_int (stmt, 1, sector_id);
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      port_id = sqlite3_column_int (stmt, 0);
    }
done:
  sqlite3_finalize (stmt);
  db_mutex_unlock ();     // Critical section ends
  return port_id;
}


int
db_get_ship_sector_id (sqlite3 *db, int ship_id)
{
  sqlite3_stmt *st = NULL;
  int rc;                       // For SQLite's intermediate return codes.
  int out_sector = 0;
  // 1. Acquire the lock at the very beginning of the function.
  db_mutex_lock ();
  const char *sql = "SELECT sector FROM ships WHERE id=?";


  // 2. Prepare the statement. Use the passed 'db' handle.
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      // Log error if preparation fails
      LOGE ("Failed to prepare SQL statement for ship sector ID.");
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, ship_id);
  // 3. Step the statement.
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      // Data found: safely read the column, treating NULL as 0
      out_sector =
        sqlite3_column_type (st,
                             0) == SQLITE_NULL ? 0 : sqlite3_column_int (st,
                                                                         0);
    }
  else if (rc != SQLITE_DONE)
    {
      // Handle step errors (not SQLITE_ROW and not SQLITE_DONE)
      LOGE ("Error stepping SQL statement for ship sector ID.");
      // out_sector remains 0
    }
  // If rc == SQLITE_DONE, the ship was not found, and out_sector remains 0, which is correct.
cleanup:
  // 4. Finalize the statement. This must be done whether the function succeeded or failed.
  if (st)
    {
      sqlite3_finalize (st);
    }
  // 5. Release the lock. This is the final step before returning.
  db_mutex_unlock ();
  // 6. Return the result. 0 on error or not found, >0 on success.
  return out_sector;
}


/**
 * @brief Retrieves the owner player ID and/or corporation ID for a given ship.
 *
 * @param db The SQLite database handle.
 * @param ship_id The ID of the ship to query.
 * @param out_player_id Optional: Pointer to store the owner player ID.
 * @param out_corp_id Optional: Pointer to store the owner corporation ID.
 * @return SQLITE_OK on success, SQLITE_NOTFOUND if ship has no owner, or SQLite error code.
 */
int
db_get_ship_owner_id (sqlite3 *db,
                      int ship_id,
                      int *out_player_id,
                      int *out_corp_id)
{
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;
  db_mutex_lock ();
  const char *sql =
    "SELECT so.player_id, cm.corp_id "
    "FROM ship_ownership so "
    "LEFT JOIN corp_members cm ON so.player_id = cm.player_id " // Assuming players own ships and are in corps
    "WHERE so.ship_id = ? AND so.role_id = 1;"; // role_id 1 typically means primary owner


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("db_get_ship_owner_id: Prepare error: %s", sqlite3_errmsg (db));
      goto cleanup;
    }

  sqlite3_bind_int (st, 1, ship_id);

  if (sqlite3_step (st) == SQLITE_ROW)
    {
      if (out_player_id)
        {
          *out_player_id = sqlite3_column_int (st, 0);
        }
      if (out_corp_id)
        {
          // Check if corp_id is NULL before reading
          if (sqlite3_column_type (st, 1) != SQLITE_NULL)
            {
              *out_corp_id = sqlite3_column_int (st, 1);
            }
          else
            {
              *out_corp_id = 0; // No corp owner
            }
        }
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      rc = SQLITE_NOTFOUND; // Ship found, but no owner_id in ship_ownership
    }
  else
    {
      LOGE ("db_get_ship_owner_id: Execute error: %s", sqlite3_errmsg (db));
    }

cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


/**
 * @brief Checks if a given ship is currently piloted by an active player.
 *
 * @param db The SQLite database handle.
 * @param ship_id The ID of the ship to check.
 * @return true if the ship is piloted, false otherwise or on error.
 */
bool
db_is_ship_piloted (sqlite3 *db, int ship_id)
{
  sqlite3_stmt *st = NULL;
  bool piloted = false;
  // int rc;
  db_mutex_lock ();
  const char *sql = "SELECT 1 FROM players WHERE ship = ? AND loggedin = 1;"; // Assuming 'loggedin' indicates active player


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("db_is_ship_piloted: Prepare error: %s", sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, ship_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      piloted = true;
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return piloted;
}


void
h_generate_hex_uuid (char *buffer, size_t buffer_size)
{
  if (buffer_size < 33)
    {                           // 32 chars + null terminator
      return;
    }
  // A simple pseudo-random hex string generation. Not a true UUID.
  // For a proper UUID, platform-specific functions or libuuid would be better.
  // This is sufficient for grouping transactions in this context.
  const char *hex_chars = "0123456789abcdef";


  for (size_t i = 0; i < 32; ++i)
    {
      buffer[i] = hex_chars[rand () % 16];
    }
  buffer[32] = '\0';
}


// Helper to get the account_id for a system-owned account (e.g., for fees/taxes)
int
h_get_system_account_id_unlocked (sqlite3 *db, const char *system_owner_type,
                                  int system_owner_id, int *account_id_out)
{
  int rc = h_get_account_id_unlocked (db, system_owner_type, system_owner_id,
                                      account_id_out);
  if (rc == SQLITE_NOTFOUND)
    {
      // Create the system account if it doesn't exist
      rc =
        h_create_bank_account_unlocked (db, system_owner_type,
                                        system_owner_id, 0, account_id_out);
      if (rc != SQLITE_OK)
        {
          LOGE
          (
            "h_get_system_account_id_unlocked: Failed to create system account %s:%d (rc=%d)",
            system_owner_type,
            system_owner_id,
            rc);
        }
    }
  return rc;
}


// New helper to get account_id from owner_type and owner_id
int
h_get_account_id_unlocked (sqlite3 *db, const char *owner_type, int owner_id,
                           int *account_id_out)
{
  sqlite3_stmt *st = NULL;
  int rc;
  int account_id = -1;
  // Select the account ID
  const char *sql_insert =
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, balance, currency, interest_rate_bp, last_interest_tick, tx_alert_threshold, is_active) "
    "VALUES (?1, ?2, 0, 'CRD', 0, (CAST(strftime('%s','now') AS INTEGER) / (24 * 60 * 60)), 0, 1);";
  rc = sqlite3_prepare_v2 (db, sql_insert, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_get_account_id_unlocked: Failed to prepare insert statement: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_text (st, 1, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, owner_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  st = NULL;

  if (rc != SQLITE_DONE && rc != SQLITE_OK)
    {
      LOGE ("h_get_account_id_unlocked: Failed to execute insert statement: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }

  const char *sql_select =
    "SELECT id FROM bank_accounts WHERE owner_type = ?1 AND owner_id = ?2";


  rc = sqlite3_prepare_v2 (db, sql_select, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_get_account_id_unlocked: Failed to prepare select statement: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_text (st, 1, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, owner_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      account_id = sqlite3_column_int (st, 0);
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_NOTFOUND;
    }

cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  if (account_id_out)
    {
      *account_id_out = account_id;
    }
  return rc;
}


// Helper to create a bank account and return its ID
int
h_create_bank_account_unlocked (sqlite3 *db, const char *owner_type,
                                int owner_id, long long initial_balance,
                                int *account_id_out)
{
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;
  // Current timestamp in epoch days for last_interest_tick
  int current_epoch_day = (int) (time (NULL) / (24 * 60 * 60));
  const char *sql =
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance, interest_rate_bp, last_interest_tick, tx_alert_threshold, is_active) VALUES (?1, ?2, 'CRD', ?3, 0, ?4, 0, 1);";
  // Using 'CRD' as default currency
  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_create_bank_account_unlocked: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_text (stmt, 1, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (stmt, 2, owner_id);
  sqlite3_bind_int64 (stmt, 3, initial_balance);
  sqlite3_bind_int (stmt, 4, current_epoch_day);
  rc = sqlite3_step (stmt);
  if (rc == SQLITE_DONE)
    {
      *account_id_out = (int) sqlite3_last_insert_rowid (db);
      rc = SQLITE_OK;
    }
  else
    {
      LOGE ("h_create_bank_account_unlocked: Failed to execute statement: %s",
            sqlite3_errmsg (db));
    }
  sqlite3_finalize (stmt);
  return rc;
}


// Helper to get a specific integer config value
long long
h_get_config_int_unlocked (sqlite3 *db, const char *key,
                           long long default_value)
{
  sqlite3_stmt *stmt = NULL;
  long long value = default_value;
  // Direct bind of column name is not possible with ?, so use sqlite3_mprintf
  char *dynamic_sql =
    sqlite3_mprintf ("SELECT value FROM config WHERE key = %Q;", key);
  if (!dynamic_sql)
    {
      LOGE
        ("h_get_config_int_unlocked: Memory allocation failed for SQL query.");
      return default_value;
    }
  int rc = sqlite3_prepare_v2 (db, dynamic_sql, -1, &stmt, NULL);


  sqlite3_free (dynamic_sql);   // Free dynamic SQL string immediately
  if (rc != SQLITE_OK)
    {
      LOGE
        ("h_get_config_int_unlocked: Failed to prepare statement for key %s: %s",
        key,
        sqlite3_errmsg (db));
      goto cleanup;
    }
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      value = sqlite3_column_int64 (stmt, 0);
    }
  else
    {
      LOGW
      (
        "h_get_config_int_unlocked: Config key %s not found or error. Using default value %lld.",
        key,
        default_value);
    }
cleanup:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  return value;
}


// Helper to get tx_alert_threshold for an account
static long long
h_get_account_alert_threshold_unlocked (sqlite3 *db, int account_id,
                                        const char *owner_type)
{
  long long threshold = 0;      // Default to 0 (disabled)
  sqlite3_stmt *stmt = NULL;
  int rc;
  const char *sql =
    "SELECT tx_alert_threshold FROM bank_accounts WHERE id = ?;";
  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE
      (
        "h_get_account_alert_threshold_unlocked: Failed to prepare statement: %s",
        sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (stmt, 1, account_id);
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      threshold = sqlite3_column_int64 (stmt, 0);
    }
  sqlite3_finalize (stmt);
  stmt = NULL;                  // Reset stmt pointer
  // If account-specific threshold is 0, fall back to global defaults from config
  if (threshold == 0)
    {
      if (strcmp (owner_type, "player") == 0)
        {
          threshold =
            h_get_config_int_unlocked (db, "bank_alert_threshold_player",
                                       1000000);
        }
      else if (strcmp (owner_type, "corp") == 0)
        {
          threshold =
            h_get_config_int_unlocked (db, "bank_alert_threshold_corp",
                                       5000000);
        }
      // For other owner_types, if no specific config, threshold remains 0
    }
cleanup:
  return threshold;
}


/******************************************************************************
*  BANKING – FINAL CANONICAL VERSIONS
*  Matches signatures in database.h exactly.
*  Must replace ALL previous versions of these functions.
******************************************************************************/


/*
 * Internal helper: Add credits. Caller must hold db_mutex.
 */
int
h_add_credits_unlocked (sqlite3 *db,
                        int account_id,
                        long long amount,
                        const char *tx_type,
                        const char *tx_group_id, long long *new_balance_out)
{
  if (!db || account_id <= 0 || amount <= 0 || !tx_type)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *stmt = NULL;
  int rc;
  const char *sql =
    "INSERT INTO bank_transactions (account_id, tx_type, direction, amount, currency, ts, tx_group_id) "
    "VALUES (?, ?, 'CREDIT', ?, 'CRD', strftime('%s','now'), ?);";


  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (stmt, 1, account_id);
  sqlite3_bind_text (stmt, 2, tx_type, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (stmt, 3, amount);
  if (tx_group_id)
    {
      sqlite3_bind_text (stmt, 4, tx_group_id, -1, SQLITE_STATIC);
    }
  else
    {
      sqlite3_bind_null (stmt, 4);
    }
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  if (rc != SQLITE_DONE)
    {
      return rc;
    }
  if (new_balance_out)
    {
      sqlite3_stmt *b = NULL;


      rc =
        sqlite3_prepare_v2 (db,
                            "SELECT balance FROM bank_accounts WHERE id=?;",
                            -1, &b, NULL);
      if (rc == SQLITE_OK)
        {
          sqlite3_bind_int (b, 1, account_id);
          if (sqlite3_step (b) == SQLITE_ROW)
            {
              *new_balance_out = sqlite3_column_int64 (b, 0);
            }
        }
      sqlite3_finalize (b);
    }
  return SQLITE_OK;
}


/*
 * Internal helper: Deduct credits. Caller must hold db_mutex.
 */
int
h_deduct_credits_unlocked (sqlite3 *db,
                           int account_id,
                           long long amount,
                           const char *tx_type,
                           const char *tx_group_id,
                           long long *new_balance_out)
{
  if (!db || account_id <= 0 || amount <= 0 || !tx_type)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *stmt = NULL;
  int rc;
  const char *sql =
    "INSERT INTO bank_transactions (account_id, tx_type, direction, amount, currency, ts, tx_group_id) "
    "VALUES (?, ?, 'DEBIT', ?, 'CRD', strftime('%s','now'), ?);";


  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (stmt, 1, account_id);
  sqlite3_bind_text (stmt, 2, tx_type, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (stmt, 3, amount);
  if (tx_group_id)
    {
      sqlite3_bind_text (stmt, 4, tx_group_id, -1, SQLITE_STATIC);
    }
  else
    {
      sqlite3_bind_null (stmt, 4);
    }
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  /* SQLITE_CONSTRAINT means “insufficient funds” because of trigger */
  if (rc != SQLITE_DONE)
    {
      return rc;
    }
  if (new_balance_out)
    {
      sqlite3_stmt *b = NULL;


      rc =
        sqlite3_prepare_v2 (db,
                            "SELECT balance FROM bank_accounts WHERE id=?;",
                            -1, &b, NULL);
      if (rc == SQLITE_OK)
        {
          sqlite3_bind_int (b, 1, account_id);
          if (sqlite3_step (b) == SQLITE_ROW)
            {
              *new_balance_out = sqlite3_column_int64 (b, 0);
            }
        }
      sqlite3_finalize (b);
    }
  return SQLITE_OK;
}


/*
 * Public: Add credits to owner's account.
 */
int
h_add_credits (sqlite3 *db, const char *owner_type, int owner_id,
               long long amount, const char *tx_type,
               const char *tx_group_id, long long *new_balance_out)
{
  if (amount <= 0)
    {
      amount = 1;
    }
  if (!db || owner_type == NULL || owner_id <= 0 || amount <= 0 ||
      tx_type == NULL)
    {
      LOGE (
        "h_add_credits: Invalid input. owner_type=%s, owner_id=%d, amount=%lld, tx_type=%s",
        owner_type,
        owner_id,
        amount,
        tx_type);
      return SQLITE_MISUSE;
    }
  int rc;


  db_mutex_lock ();
  // Get or create account ID
  int account_id = -1;


  rc = h_get_account_id_unlocked (db, owner_type, owner_id, &account_id);
  if (rc == SQLITE_NOTFOUND)
    {
      LOGD ("h_add_credits: Account not found for %s:%d, creating new account.",
            owner_type,
            owner_id);
      rc =
        h_create_bank_account_unlocked (db, owner_type, owner_id, 0,
                                        &account_id);
      if (rc != SQLITE_OK)
        {
          LOGE ("h_add_credits: Failed to create bank account for %s:%d, rc=%d",
                owner_type,
                owner_id,
                rc);
          db_mutex_unlock ();
          return rc;
        }
    }
  else if (rc != SQLITE_OK)
    {
      LOGE ("h_add_credits: Failed to get bank account for %s:%d, rc=%d",
            owner_type,
            owner_id,
            rc);
      db_mutex_unlock ();
      return rc;
    }
  rc = h_add_credits_unlocked (db,
                               account_id,
                               amount,
                               tx_type,
                               tx_group_id,
                               new_balance_out);
  if (rc != SQLITE_OK)
    {
      LOGE (
        "h_add_credits: h_add_credits_unlocked failed for account %d, rc=%d",
        account_id,
        rc);
    }

  db_mutex_unlock ();
  return rc;
}


/*
 * Public: Deduct credits from owner's account.
 */
int
h_deduct_credits (sqlite3 *db,
                  const char *owner_type,
                  int owner_id,
                  long long amount,
                  const char *tx_type,
                  const char *tx_group_id, long long *new_balance_out)
{
  if (!db || !owner_type || amount <= 0)
    {
      return SQLITE_MISUSE;
    }
  db_mutex_lock ();
  int account_id = -1;
  int rc = h_get_account_id_unlocked (db, owner_type, owner_id, &account_id);


  if (rc != SQLITE_OK)
    {
      db_mutex_unlock ();
      return rc;
    }
  rc = h_deduct_credits_unlocked (db, account_id, amount,
                                  tx_type ? tx_type : "DEBIT",
                                  tx_group_id, new_balance_out);
  db_mutex_unlock ();
  return rc;
}


// Function to calculate fees and taxes for a given transaction type and amount
int
calculate_fees (sqlite3 *db, const char *tx_type, long long base_amount,
                const char *owner_type, fee_result_t *out)
{
  if (!db || !tx_type || !out)
    {
      return SQLITE_MISUSE;
    }
  out->fee_total = 0;
  out->fee_to_bank = 0;
  out->tax_to_system = 0;
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;
  long long current_time = time (NULL);
  // Query for active fee rules
  const char *sql =
    "SELECT fee_code, value, is_percentage, min_tx_amount, max_tx_amount "
    "FROM bank_fee_schedules " "WHERE tx_type = ?1 "
    "  AND (owner_type IS NULL OR owner_type = ?2) " "  AND currency = 'CRD' "                                                                                                                                          // Assuming CRD for now
    "  AND effective_from <= ?3 "
    "  AND (effective_to IS NULL OR effective_to >= ?3) " "ORDER BY fee_code;";                                 // Deterministic order


  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("calculate_fees: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_text (stmt, 1, tx_type, -1, SQLITE_STATIC);
  sqlite3_bind_text (stmt, 2, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (stmt,
                      3,
                      current_time);
  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      const char *fee_code = (const char *) sqlite3_column_text (stmt, 0);
      long long value = sqlite3_column_int64 (stmt, 1);
      int is_percentage = sqlite3_column_int (stmt, 2);
      long long min_tx_amount = sqlite3_column_int64 (stmt, 3);
      long long max_tx_amount = sqlite3_column_type (stmt,
                                                     4) ==
                                SQLITE_NULL ? -1 : sqlite3_column_int64 (stmt,
                                                                         4);                                            // -1 for no max


      // Apply min/max transaction amount filters
      if (base_amount < min_tx_amount)
        {
          continue;
        }
      if (max_tx_amount != -1 && base_amount > max_tx_amount)
        {
          continue;
        }
      long long charge = 0;


      if (is_percentage)
        {
          charge = (base_amount * value) / 10000;       // value is in basis points
        }
      else
        {
          charge = value;       // flat fee
        }
      // Clamp charge at 0 and max (optional, config-driven)
      if (charge < 0)
        {
          charge = 0;
        }
      // if (charge > MAX_CHARGE_AMOUNT) charge = MAX_CHARGE_AMOUNT; // Needs global config
      out->fee_total += charge;
      // Distribute charge to specific categories based on fee_code
      // This is a placeholder; actual distribution logic may be more complex
      if (strcmp (fee_code, "TAX_RATE") == 0 || strcmp (tx_type, "TAX") == 0)
        {
          out->tax_to_system += charge;
        }
      else
        {
          out->fee_to_bank += charge;
        }
    }
  rc = SQLITE_OK;
cleanup:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  db_mutex_unlock ();
  return rc;
}


/*
 * Helper function to update commodity stock on a planet.
 * Returns SQLITE_OK on success, or an SQLite error code.
 * If new_quantity is not NULL, it will be set to the commodity's new quantity.
 */
int
h_update_planet_stock (sqlite3 *db, int planet_id, const char *commodity_code,
                       int quantity_change, int *new_quantity)
{
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;
  if (!db || !commodity_code || planet_id <= 0)
    {
      return SQLITE_MISUSE;
    }
  db_mutex_lock ();
  const char *sql =
    "UPDATE planet_goods SET quantity = quantity + ? "
    "WHERE planet_id = ? AND commodity = ?;";


  rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_update_planet_stock: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (stmt, 1, quantity_change);
  sqlite3_bind_int (stmt, 2, planet_id);
  sqlite3_bind_text (stmt, 3, commodity_code, -1, SQLITE_STATIC);
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("h_update_planet_stock: Failed to execute statement: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  if (new_quantity)
    {
      sqlite3_stmt *qty_stmt = NULL;
      const char *qty_sql =
        "SELECT quantity FROM planet_goods WHERE planet_id = ? AND commodity = ?;";


      rc = sqlite3_prepare_v2 (db, qty_sql, -1, &qty_stmt, NULL);
      if (rc == SQLITE_OK)
        {
          sqlite3_bind_int (qty_stmt, 1, planet_id);
          sqlite3_bind_text (qty_stmt, 2, commodity_code, -1, SQLITE_STATIC);
          if (sqlite3_step (qty_stmt) == SQLITE_ROW)
            {
              *new_quantity = sqlite3_column_int (qty_stmt, 0);
            }
          sqlite3_finalize (qty_stmt);
        }
      else
        {
          LOGE
            ("h_update_planet_stock: Failed to prepare quantity statement: %s",
            sqlite3_errmsg (db));
        }
    }
  rc = SQLITE_OK;
cleanup:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  db_mutex_unlock ();
  return rc;
}


int
db_fighters_at_sector_json (int sector_id, json_t **out_array)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  json_t *arr = NULL;
  int rc = SQLITE_ERROR;
  db_mutex_lock ();
  if (!out_array)
    {
      goto cleanup;
    }
  *out_array = NULL;
  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }
  const char *sql =
    "SELECT player, corporation, quantity "
    "FROM sector_assets WHERE sector = ? AND asset_type = ?;";


  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, sector_id);
  sqlite3_bind_int (st, 2, ASSET_FIGHTER);
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int player_id = sqlite3_column_int (st, 0);
      int corporation_id = sqlite3_column_int (st, 1);
      int quantity = sqlite3_column_int (st, 2);
      json_t *obj = json_object ();


      if (!obj)
        {
          rc = SQLITE_NOMEM;
          goto cleanup;
        }
      if (corporation_id > 0)
        {
          json_object_set_new (obj, "owner_type", json_string ("corporation"));
          json_object_set_new (obj, "owner_id", json_integer (corporation_id));
        }
      else if (player_id > 0)
        {
          json_object_set_new (obj, "owner_type", json_string ("player"));
          json_object_set_new (obj, "owner_id", json_integer (player_id));
        }
      else
        {
          // Should not happen if asset has an owner
          json_object_set_new (obj, "owner_type", json_string ("unknown"));
          json_object_set_new (obj, "owner_id", json_integer (0));
        }
      json_object_set_new (obj, "quantity", json_integer (quantity));
      json_array_append_new (arr, obj);
    }
  if (rc == SQLITE_DONE)
    {
      *out_array = arr;
      rc = SQLITE_OK;
      arr = NULL; // Transfer ownership
    }
  else
    {
      // Error in sqlite3_step
      rc = SQLITE_ERROR;
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  if (arr)
    {
      json_decref (arr);
    }
  db_mutex_unlock ();
  return rc;
}


int
db_mines_at_sector_json (int sector_id, json_t **out_array)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  json_t *arr = NULL;
  int rc = SQLITE_ERROR;
  db_mutex_lock ();
  if (!out_array)
    {
      goto cleanup;
    }
  *out_array = NULL;
  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }
  const char *sql =
    "SELECT player, corporation, quantity, asset_type "
    "FROM sector_assets WHERE sector = ? AND asset_type IN (?, ?);";


  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, sector_id);
  sqlite3_bind_int (st, 2, ASSET_MINE);
  sqlite3_bind_int (st, 3, ASSET_LIMPET_MINE);
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int player_id = sqlite3_column_int (st, 0);
      int corporation_id = sqlite3_column_int (st, 1);
      int quantity = sqlite3_column_int (st, 2);
      int asset_type = sqlite3_column_int (st, 3);
      json_t *obj = json_object ();


      if (!obj)
        {
          rc = SQLITE_NOMEM;
          goto cleanup;
        }
      if (corporation_id > 0)
        {
          json_object_set_new (obj, "owner_type", json_string ("corporation"));
          json_object_set_new (obj, "owner_id", json_integer (corporation_id));
        }
      else if (player_id > 0)
        {
          json_object_set_new (obj, "owner_type", json_string ("player"));
          json_object_set_new (obj, "owner_id", json_integer (player_id));
        }
      else
        {
          // Should not happen if asset has an owner
          json_object_set_new (obj, "owner_type", json_string ("unknown"));
          json_object_set_new (obj, "owner_id", json_integer (0));
        }
      json_object_set_new (obj, "quantity", json_integer (quantity));
      json_object_set_new (obj, "mine_type", json_integer (asset_type)); // Add mine type
      json_array_append_new (arr, obj);
    }
  if (rc == SQLITE_DONE)
    {
      *out_array = arr;
      rc = SQLITE_OK;
      arr = NULL; // Transfer ownership
    }
  else
    {
      // Error in sqlite3_step
      rc = SQLITE_ERROR;
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  if (arr)
    {
      json_decref (arr);
    }
  db_mutex_unlock ();
  return rc;
}


/**
 * @brief Generic helper to get a bank balance for any owner type.
 * @param owner_type The type of the owner (e.g., "player", "corp").
 * @param owner_id The ID of the owner.
 * @param out_balance Pointer to store the resulting balance.
 * @return SQLITE_OK on success, or an error code.
 */
static int
h_get_bank_balance (const char *owner_type, int owner_id,
                    long long *out_balance)
{
  if (out_balance == NULL)
    {
      return SQLITE_MISUSE;
    }
  *out_balance = 0;             // Default to 0
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_ERROR;
    }
  int account_id = -1;
  // Note: This helper already locks. We need to reconsider mutex strategy for nested calls.
  // For now, assume this is called within a wider mutex scope or will acquire its own.
  // Given the public wrappers don't lock, this needs to be thread-safe.
  // Let's modify h_get_account_id_unlocked to be h_get_account_id and acquire its own lock.
  // Or, ensure h_get_bank_balance itself is thread-safe.
  // The previous h_get_account_id_unlocked is meant to be called when db_mutex is already held.
  // h_get_bank_balance is static and called by public functions that also lock, so it's okay.
  int rc_account_id =
    h_get_account_id_unlocked (db, owner_type, owner_id, &account_id);


  if (rc_account_id == SQLITE_NOTFOUND)
    {
      return SQLITE_OK;         // No account means balance is 0
    }
  else if (rc_account_id != SQLITE_OK)
    {
      return rc_account_id;     // Error in getting account_id
    }
  const char *sql = "SELECT balance FROM bank_accounts WHERE id = ?1;"; // Use account_id
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st, 1, account_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *out_balance = sqlite3_column_int64 (st, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      // Should not happen if h_get_account_id_unlocked found an account, but handle defensively.
      rc = SQLITE_OK;
    }
  else
    {
      rc = SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  return rc;
}


/**
 * @brief Gets the bank balance for a player.
 */
int
db_get_player_bank_balance (int player_id, long long *out_balance)
{
  return h_get_bank_balance ("player", player_id, out_balance);
}


/**
 * @brief Gets the bank balance for a corporation.
 */
int
db_get_corp_bank_balance (int corp_id, long long *out_balance)
{
  return h_get_bank_balance ("corp", corp_id, out_balance);
}


/**
 * @brief Gets the bank balance for an NPC.
 */
int
db_get_npc_bank_balance (int npc_id, long long *out_balance)
{
  return h_get_bank_balance ("npc", npc_id, out_balance);
}


/**
 * @brief Gets the bank balance for a port.
 */
int
db_get_port_bank_balance (int port_id, long long *out_balance)
{
  return h_get_bank_balance ("port", port_id, out_balance);
}


/**
 * @brief Gets the bank balance for a planet.
 */
int
db_get_planet_bank_balance (int planet_id, long long *out_balance)
{
  return h_get_bank_balance ("planet", planet_id, out_balance);
}


int
db_bank_account_exists (const char *owner_type, int owner_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return -1;                // Error
    }
  int account_id = -1;
  int rc;


  db_mutex_lock ();
  rc = h_get_account_id_unlocked (db, owner_type, owner_id, &account_id);
  db_mutex_unlock ();
  if (rc == SQLITE_OK)
    {
      return 1;                 // Account exists
    }
  else if (rc == SQLITE_NOTFOUND)
    {
      return 0;                 // Account does not exist
    }
  else
    {
      return -1;                // Error
    }
}


/**
 * @brief Creates a new bank account for a given owner.
 * @param owner_type The type of the owner (e.g., "player", "corp").
 * @param owner_id The ID of the owner.
 * @param initial_balance The initial balance for the account.
 * @param account_id_out Pointer to store the ID of the newly created account.
 * @return SQLITE_OK on success, or an SQLite error code.
 */
int
db_bank_create_account (const char *owner_type, int owner_id,
                        long long initial_balance, int *account_id_out)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_ERROR;
    }
  int rc;


  db_mutex_lock ();
  rc =
    h_create_bank_account_unlocked (db, owner_type, owner_id, initial_balance,
                                    account_id_out);
  db_mutex_unlock ();
  return rc;
}


int
db_bank_deposit (const char *owner_type, int owner_id, long long amount)
{
  if (amount <= 0)
    {
      return SQLITE_MISUSE;
    }
  return h_add_credits (db_get_handle (),
                        owner_type,
                        owner_id,
                        amount,
                        "DEPOSIT",
                        // tx_type
                        NULL,
                        // tx_group_id
                        NULL    // new_balance_out
                        );
}


int
db_bank_withdraw (const char *owner_type, int owner_id, long long amount)
{
  if (amount <= 0)
    {
      return SQLITE_MISUSE;
    }
  return h_deduct_credits (db_get_handle (),
                           owner_type,
                           owner_id,
                           amount,
                           "WITHDRAWAL",
                           // tx_type
                           NULL,
                           // tx_group_id
                           NULL // new_balance_out
                           );
}


/**
 * @brief Transfers an amount between two bank accounts.
 * @return SQLITE_OK on success, or an error code from withdraw/deposit.
 */
int
db_bank_transfer (const char *from_owner_type, int from_owner_id,
                  const char *to_owner_type, int to_owner_id,
                  long long amount)
{
  db_mutex_lock ();
  int rc =
    h_bank_transfer_unlocked (db_get_handle (), from_owner_type,
                              from_owner_id,
                              to_owner_type, to_owner_id,
                              amount, "TRANSFER", NULL);


  db_mutex_unlock ();
  return rc;
}


// Unlocked helper for ship claiming. Assumes db_mutex is held and transaction is active (or not needed).
int
h_ship_claim_unlocked (sqlite3 *db,
                       int player_id,
                       int sector_id,
                       int ship_id,
                       json_t **out_ship)
{
  int rc = SQLITE_OK;
  sqlite3_stmt *stmt = NULL;
  LOGD ("h_ship_claim_unlocked: checking ship_id=%d, sector_id=%d",
        ship_id,
        sector_id);
  static const char *SQL_CHECK =
    "SELECT s.id FROM ships s "
    "LEFT JOIN players pil ON pil.ship = s.id "
    "WHERE s.id=? AND s.sector=? "
    "AND pil.id IS NULL;";


  rc = sqlite3_prepare_v2 (db, SQL_CHECK, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("h_ship_claim_unlocked: prepare failed: %s", sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (stmt, 1, ship_id);
  sqlite3_bind_int (stmt, 2, sector_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_ROW)
    {
      LOGE (
        "h_ship_claim_unlocked: Ship check failed. rc=%d. ship_id=%d, sector_id=%d. (Maybe flags or pilot issue?)",
        rc,
        ship_id,
        sector_id);
      return SQLITE_CONSTRAINT;
    }
  LOGD ("h_ship_claim_unlocked: Ship check passed.");
  /* Optional: strip defence so claims aren’t “free upgrades” */
  static const char *SQL_NORMALISE =
    "UPDATE ships SET fighters=fighters, shields=shields WHERE id=? AND 1=0;";


  rc = sqlite3_prepare_v2 (db, SQL_NORMALISE, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (stmt, 1, ship_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE)
    {
      return rc;
    }
  /* Switch current pilot */
  static const char *SQL_SET_PLAYER_SHIP =
    "UPDATE players SET ship = ? WHERE id = ?;";


  rc = sqlite3_prepare_v2 (db, SQL_SET_PLAYER_SHIP, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (stmt, 1, ship_id);
  sqlite3_bind_int (stmt, 2, player_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE)
    {
      return rc;
    }
  /* Grant ownership to the claimer and mark as primary */
  static const char *SQL_CLR_OLD_PRIMARY =
    "UPDATE ship_ownership SET is_primary=0 WHERE player_id=?;";


  rc = sqlite3_prepare_v2 (db, SQL_CLR_OLD_PRIMARY, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (stmt, 1, player_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE)
    {
      return rc;
    }
  // Delete existing owner record for this ship (if any)
  static const char *SQL_DELETE_OLD_OWNER =
    "DELETE FROM ship_ownership WHERE ship_id=? AND role_id=1;";


  rc = sqlite3_prepare_v2 (db, SQL_DELETE_OLD_OWNER, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (stmt, 1, ship_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE)
    {
      return rc;
    }
  /* Insert new owner record */
  static const char *SQL_INSERT_NEW_OWNER =
    "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary, acquired_at) "
    "VALUES (?, ?, 1, 1, strftime('%s','now'));";


  rc = sqlite3_prepare_v2 (db, SQL_INSERT_NEW_OWNER, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (stmt, 1, ship_id);
  sqlite3_bind_int (stmt, 2, player_id);
  rc = sqlite3_step (stmt);
  sqlite3_finalize (stmt);
  stmt = NULL;
  if (rc != SQLITE_DONE)
    {
      return rc;
    }
  /* Fetch snapshot for reply (owner from ship_ownership, pilot = you now) */
  static const char *SQL_FETCH =
    "SELECT s.id AS ship_id, "
    "       COALESCE(NULLIF(s.name,''), st.name || ' #' || s.id) AS ship_name, "
    "       st.id AS type_id, st.name AS type_name, "
    "       s.sector AS sector_id, "
    "       own.player_id AS owner_id, "
    "       COALESCE( (SELECT name FROM players WHERE id=own.player_id), 'derelict') AS owner_name, "
    "       0 AS is_derelict, "
    "       s.fighters, s.shields, "
    "       s.holds AS holds_total, (s.holds - s.holds) AS holds_free, "
    "       s.ore, s.organics, s.equipment, s.colonists, "
    "       COALESCE(s.flags, 731) AS perms "
    "FROM ships s "
    "LEFT JOIN shiptypes st       ON st.id = s.type_id "
    "LEFT JOIN ship_ownership own ON own.ship_id = s.id "
    "WHERE s.id=?;";


  rc = sqlite3_prepare_v2 (db, SQL_FETCH, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (stmt, 1, ship_id);
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_ROW)
    {
      sqlite3_finalize (stmt);
      return rc;
    }
  int c = 0;
  int ship_id_row = sqlite3_column_int (stmt, c++);
  const char *ship_nm = (const char *)sqlite3_column_text (stmt, c++);


  if (!ship_nm)
    {
      ship_nm = "";
    }
  int type_id = sqlite3_column_int (stmt, c++);
  const char *type_nm = (const char *)sqlite3_column_text (stmt, c++);


  if (!type_nm)
    {
      type_nm = "";
    }
  int sector_row = sqlite3_column_int (stmt, c++);
  int owner_id = (sqlite3_column_type (stmt,
                                       c) ==
                  SQLITE_NULL) ? 0 : sqlite3_column_int (stmt, c);


  c++;
  const char *owner_nm = (const char *)sqlite3_column_text (stmt, c++);


  if (!owner_nm)
    {
      owner_nm = "derelict";
    }
  int is_derelict = sqlite3_column_int (stmt, c++);
  int fighters = sqlite3_column_int (stmt, c++);
  int shields = sqlite3_column_int (stmt, c++);
  int holds_total = sqlite3_column_int (stmt, c++);
  int holds_free = sqlite3_column_int (stmt, c++);
  int ore = sqlite3_column_int (stmt, c++);
  int organics = sqlite3_column_int (stmt, c++);
  int equipment = sqlite3_column_int (stmt, c++);
  int colonists = sqlite3_column_int (stmt, c++);
  int flags = sqlite3_column_int (stmt, c++);
  const char *reg = (const char *)sqlite3_column_text (stmt, c++);


  if (!reg)
    {
      reg = "";
    }
  /* build the JSON for the claimed ship */
  int perms = sqlite3_column_int (stmt, c++);
  char perm_str[8];


  snprintf (perm_str, sizeof(perm_str), "%03d", perms);
  if (out_ship)
    {
      *out_ship = json_object ();
      json_object_set_new (*out_ship, "id", json_integer (ship_id_row));
      json_object_set_new (*out_ship, "name", json_string (ship_nm));


      json_t *type_obj = json_object ();
      json_object_set_new (type_obj, "id", json_integer (type_id));
      json_object_set_new (type_obj, "name", json_string (type_nm));
      json_object_set_new (*out_ship, "type", type_obj);


      json_object_set_new (*out_ship, "sector_id", json_integer (sector_row));


      json_t *owner_obj = json_object ();
      json_object_set_new (owner_obj, "id", json_integer (owner_id));
      json_object_set_new (owner_obj, "name", json_string (owner_nm));
      json_object_set_new (*out_ship, "owner", owner_obj);


      json_t *flags_obj = json_object ();
      json_object_set_new (flags_obj, "derelict", json_boolean (is_derelict != 0));
      json_object_set_new (flags_obj, "boardable", json_boolean (is_derelict != 0));
      json_object_set_new (flags_obj, "raw", json_integer (flags));
      json_object_set_new (*out_ship, "flags", flags_obj);


      json_t *defence_obj = json_object ();
      json_object_set_new (defence_obj, "shields", json_integer (shields));
      json_object_set_new (defence_obj, "fighters", json_integer (fighters));
      json_object_set_new (*out_ship, "defence", defence_obj);


      json_t *holds_obj = json_object ();
      json_object_set_new (holds_obj, "total", json_integer (holds_total));
      json_object_set_new (holds_obj, "free", json_integer (holds_free));
      json_object_set_new (*out_ship, "holds", holds_obj);


      json_t *cargo_obj_inner = json_object ();
      json_object_set_new (cargo_obj_inner, "ore", json_integer (ore));
      json_object_set_new (cargo_obj_inner, "organics", json_integer (organics));
      json_object_set_new (cargo_obj_inner, "equipment", json_integer (equipment));
      json_object_set_new (cargo_obj_inner, "colonists", json_integer (colonists));
      json_object_set_new (*out_ship, "cargo", cargo_obj_inner);


      json_t *perms_obj = json_object ();
      json_object_set_new (perms_obj, "value", json_integer (perms));
      json_object_set_new (perms_obj, "octal", json_string (perm_str));
      json_object_set_new (*out_ship, "perms", perms_obj);


      json_object_set_new (*out_ship, "registration", json_string (reg));
    }
  sqlite3_finalize (stmt);
  return SQLITE_OK;
}


int
db_ship_claim (sqlite3 *db,
               int player_id,
               int sector_id,
               int ship_id,
               json_t **out_ship)
{
  if (!out_ship)
    {
      return SQLITE_MISUSE;
    }
  *out_ship = NULL;
  int rc;


  rc = h_ship_claim_unlocked (db, player_id, sector_id, ship_id, out_ship);
  return rc;
}


int
db_player_info_json (int player_id, json_t **out)
{
  sqlite3_stmt *st = NULL;
  json_t *root_obj = NULL;      /* Renamed from obj */
  int ret_code = SQLITE_ERROR;
  db_mutex_lock ();
  if (out)
    {
      *out = NULL;
    }
  sqlite3 *dbh = db_get_handle ();


  if (!dbh)
    {
      goto cleanup;
    }
  // This SQL query is correct from our last fix
  const char *sql = "SELECT " " p.id, p.number, p.name, "       // Player info (0, 1, 2)
                    " p.ship, " // Player's ship ID (3)
                    " s.id, "   // (4)
                    " s.name, " // (5)
                    " s.type_id, " // (6)
                    " st.name, " // (7)
                    " s.holds, " // (8)
                    " s.fighters, " // (9)
                    " s.sector, " // (10)
                    " sec.name, " // (11)
                    " own.player_id AS owner_id, " // (12)
                    " COALESCE( (SELECT name FROM players WHERE id=own.player_id), 'derelict') AS owner_name, "
                    // (13)
                    " s.ore, s.organics, s.equipment, s.colonists, s.shields " // (14, 15, 16, 17, 18)
                    "FROM players p " "LEFT JOIN ships s      ON s.id = p.ship "
                    "LEFT JOIN shiptypes st ON st.id = s.type_id "
                    "LEFT JOIN sectors sec  ON sec.id = s.sector "                                                                                              // Join on ship's sector
                    "LEFT JOIN ship_ownership own ON s.id = own.ship_id AND own.role_id = 1 "
                    // Join for owner
                    "WHERE p.id = ?";
  int rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      // --- Create the nested objects ---
      root_obj = json_object ();
      json_t *player_obj = json_object ();
      json_t *ship_obj = json_object ();


      if (!root_obj || !player_obj || !ship_obj)
        {
          // Out of memory
          json_decref (root_obj);
          json_decref (player_obj);
          json_decref (ship_obj);
          ret_code = SQLITE_NOMEM;
          goto cleanup;
        }
      json_object_set_new (root_obj, "player", player_obj);
      json_object_set_new (root_obj, "ship", ship_obj);
      // --- Populate "player" object ---
      int p_id = sqlite3_column_int (st, 0);
      int p_number = sqlite3_column_int (st, 1);
      const char *p_name = (const char *) sqlite3_column_text (st, 2);


      //int p_ship = sqlite3_column_int (st, 3);	// Player's ship ID
      json_object_set_new (player_obj, "id", json_integer (p_id));
      json_object_set_new (player_obj, "number", json_integer (p_number));
      json_object_set_new (player_obj, "name", json_string (p_name));
      // --- Populate "ship" object (with location) ---
      int s_id = sqlite3_column_int (st, 4);    // s.id
      int s_number = sqlite3_column_int (st, 4);        // s.id again, for ship_number
      const char *s_name = (const char *) sqlite3_column_text (st, 5);  // s.name


      json_object_set_new (ship_obj, "id", json_integer (s_id));
      json_object_set_new (ship_obj, "number", json_integer (s_number));
      json_object_set_new (ship_obj, "name", json_string (s_name));
      json_t *ship_type_obj = json_object ();
      int st_id = sqlite3_column_int (st, 7);
      const char *st_name = (const char *) sqlite3_column_text (st, 8);


      json_object_set_new (ship_type_obj, "id", json_integer (st_id));
      json_object_set_new (ship_type_obj, "name", json_string (st_name));
      json_object_set_new (ship_obj, "type", ship_type_obj);
      int s_holds = sqlite3_column_int (st, 8);
      int s_fighters = sqlite3_column_int (st, 9);


      json_object_set_new (ship_obj, "holds", json_integer (s_holds));
      json_object_set_new (ship_obj, "fighters", json_integer (s_fighters));
      int s_shields = sqlite3_column_int (st, 18);


      json_object_set_new (ship_obj, "shields", json_integer (s_shields));
      json_t *location_obj = json_object ();
      int loc_sector_id = sqlite3_column_int (st, 10);
      const char *loc_sector_name =
        (const char *) sqlite3_column_text (st, 11);


      json_object_set_new (location_obj, "sector_id",
                           json_integer (loc_sector_id));
      json_object_set_new (location_obj, "sector_name",
                           json_string (loc_sector_name));
      json_object_set_new (ship_obj, "location", location_obj);
      // Add owner information to ship_obj
      int owner_id = sqlite3_column_int (st, 12);
      const char *owner_name = (const char *) sqlite3_column_text (st, 13);
      json_t *owner_obj = json_object ();


      json_object_set_new (owner_obj, "id", json_integer (owner_id));
      json_object_set_new (owner_obj, "name", json_string (owner_name));
      json_object_set_new (ship_obj, "owner", owner_obj);
      // Add cargo information to ship_obj
      int ore = sqlite3_column_int (st, 14);
      int organics = sqlite3_column_int (st, 15);
      int equipment = sqlite3_column_int (st, 16);
      int colonists = sqlite3_column_int (st, 17);
      json_t *cargo_obj = json_object ();
      json_object_set_new (cargo_obj, "ore", json_integer (ore));
      json_object_set_new (cargo_obj, "organics", json_integer (organics));
      json_object_set_new (cargo_obj, "equipment", json_integer (equipment));
      json_object_set_new (cargo_obj, "colonists", json_integer (colonists));


      json_object_set_new (ship_obj, "cargo", cargo_obj);
      ret_code = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      // No player found, return empty object
      ret_code = SQLITE_OK;
      root_obj = json_object ();
    }
  else
    {
      ret_code = rc;
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  if (ret_code == SQLITE_OK)
    {
      if (out)
        {
          *out = root_obj;
        }
      else
        {
          json_decref (root_obj);
        }
    }
  else
    {
      if (root_obj)
        {
          json_decref (root_obj);
        }
    }
  db_mutex_unlock ();
  return ret_code;
}


// Mapping from client-friendly field names to player_info_v1 view column names
static const struct {
  const char *client_name;
  const char *view_name;
} player_field_map[] = {
  {"id", "player_id"},
  {"username", "player_name"},
  {"number", "player_number"},
  {"sector", "sector_id"},
  {"sector_name", "sector_name"},
  {"credits", "petty_cash"},   // Client requested "credits" maps to "petty_cash" in view
  {"alignment", "alignment"},
  {"experience", "experience"},
  {"ship_number", "ship_number"},
  {"ship_id", "ship_id"},
  {"ship_name", "ship_name"},
  {"ship_type_id", "ship_type_id"},
  {"ship_type_name", "ship_type_name"},
  {"ship_holds_capacity", "ship_holds_capacity"},
  {"ship_holds_current", "ship_holds_current"},
  {"ship_fighters", "ship_fighters"},
  {"ship_mines", "ship_mines"},
  {"ship_limpets", "ship_limpets"},
  {"ship_genesis", "ship_genesis"},
  {"ship_photons", "ship_photons"},
  {"ship_beacons", "ship_beacons"},
  {"ship_colonists", "ship_colonists"},
  {"ship_equipment", "ship_equipment"},
  {"ship_organics", "ship_organics"},
  {"ship_ore", "ship_ore"},
  {"ship_ported", "ship_ported"},
  {"ship_onplanet", "ship_onplanet"},
  {"approx_worth", "approx_worth"},
  {NULL, NULL}   // Sentinel
};


// Function to get the corresponding view column name
static const char *
get_player_view_column_name (const char *client_name)
{
  for (int i = 0; player_field_map[i].client_name != NULL; i++)
    {
      if (strcmp (client_name, player_field_map[i].client_name) == 0)
        {
          return player_field_map[i].view_name;
        }
    }
  return NULL;   // Not found
}


int
db_player_info_selected_fields (int player_id,
                                const json_t *fields_array,
                                json_t **out)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *stmt = NULL;
  int rc = SQLITE_ERROR;
  *out = NULL;
  if (!db || !fields_array || !json_is_array (fields_array))
    {
      LOGE ("db_player_info_selected_fields: Invalid arguments provided.\n");
      return SQLITE_MISUSE;
    }
  json_t *selected_view_columns_json = json_array ();


  if (!selected_view_columns_json)
    {
      LOGE (
        "db_player_info_selected_fields: Failed to allocate JSON array for selected columns.\n");
      return SQLITE_NOMEM;
    }
  size_t index;
  json_t *value;


  // Build the SELECT clause dynamically
  json_array_foreach (fields_array, index, value) {
    const char *client_field_name = json_string_value (value);
    if (client_field_name)
      {
        const char *view_col_name =
          get_player_view_column_name (client_field_name);


        if (view_col_name)
          {
            // Append the view's column name, not the client's.
            // We'll map back to client name in the result parsing.
            json_array_append_new (selected_view_columns_json,
                                   json_string (view_col_name));
          }
        else
          {
            LOGW (
              "db_player_info_selected_fields: Requested field '%s' not mapped to a view column. Ignoring.\n",
              client_field_name);
          }
      }
  }
  if (json_array_size (selected_view_columns_json) == 0)
    {
      LOGW (
        "db_player_info_selected_fields: No valid fields to select. Returning empty object.\n");
      *out = json_object ();  // Return an empty object if no fields were valid.
      json_decref (selected_view_columns_json);
      return SQLITE_OK;
    }
  // Allocate buffer for the dynamic SELECT clause (e.g., "col1, col2, col3")
  // Max 256 chars per column name, plus ", " separator. Max ~30 fields from player_field_map.
  // 30 * (256 + 2) is ~7.5KB. So 8KB is a safe buffer size.
  char select_clause_buffer[8192];


  select_clause_buffer[0] = '\0';
  size_t current_buffer_len = 0;


  json_array_foreach (selected_view_columns_json, index, value) {
    const char *col_name_in_view = json_string_value (value);
    if (col_name_in_view)
      {
        if (current_buffer_len > 0)
          {
            strncat (select_clause_buffer,
                     ", ",
                     sizeof(select_clause_buffer) - current_buffer_len - 1);
            current_buffer_len += 2;
          }
        strncat (select_clause_buffer, col_name_in_view,
                 sizeof(select_clause_buffer) - current_buffer_len - 1);
        current_buffer_len += strlen (col_name_in_view);
      }
  }
  json_decref (selected_view_columns_json);  // Free the temporary array
  // Construct the full SQL query
  char *sql_query = sqlite3_mprintf (
    "SELECT %s FROM player_info_v1 WHERE player_id = ?1;",
    select_clause_buffer
    );


  if (!sql_query)
    {
      LOGE (
        "db_player_info_selected_fields: SQL query string allocation failed.\n");
      return SQLITE_NOMEM;
    }
  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, sql_query, -1, &stmt, NULL);
  sqlite3_free (sql_query);  // Free sqlite3_mprintf'd string immediately
  if (rc != SQLITE_OK)
    {
      LOGE (
        "db_player_info_selected_fields: Failed to prepare statement for player %d: %s\n",
        player_id,
        sqlite3_errmsg (db));
      db_mutex_unlock ();
      return rc;
    }
  sqlite3_bind_int (stmt, 1, player_id);
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      *out = json_object ();
      if (!*out)
        {
          LOGE (
            "db_player_info_selected_fields: Failed to allocate output JSON object.\n");
          sqlite3_finalize (stmt);
          db_mutex_unlock ();
          return SQLITE_NOMEM;
        }
      // Iterate through the original requested fields_array to populate the output JSON
      // This ensures the keys in the output match the client's request
      json_array_foreach (fields_array, index, value) {
        const char *client_field_name = json_string_value (value);
        if (!client_field_name)
          {
            continue;
          }
        const char *view_col_name =
          get_player_view_column_name (client_field_name);


        if (!view_col_name)
          {
            // This case should ideally be covered by the initial validation/logging
            continue;
          }
        // Find the column index in the result set by the view's column name
        int col_idx = sqlite3_column_type (stmt,
                                           0) ==
                      SQLITE_NULL ? -1 : sqlite3_column_count (stmt);                                    // Initialize to invalid


        for (int c = 0; c < sqlite3_column_count (stmt); ++c)
          {
            if (strcmp (sqlite3_column_name (stmt, c), view_col_name) == 0)
              {
                col_idx = c;
                break;
              }
          }
        if (col_idx != -1)
          {
            json_t *col_value = NULL;


            switch (sqlite3_column_type (stmt, col_idx))
              {
                case SQLITE_INTEGER:
                  col_value = json_integer (sqlite3_column_int64 (stmt,
                                                                  col_idx));
                  break;
                case SQLITE_FLOAT:
                  col_value = json_real (sqlite3_column_double (stmt, col_idx));
                  break;
                case SQLITE_TEXT:
                  col_value =
                    json_string ((const char *)sqlite3_column_text (stmt,
                                                                    col_idx));
                  break;
                case SQLITE_NULL:
                  col_value = json_null ();
                  break;
                default:
                  LOGW (
                    "db_player_info_selected_fields: Unsupported SQLITE type for column '%s'.\n",
                    view_col_name);
                  break;
              }
            if (col_value)
              {
                json_object_set_new (*out, client_field_name, col_value);    // Use client_field_name for output key
              }
          }
        else
          {
            LOGW (
              "db_player_info_selected_fields: Internal error: Column '%s' (for client field '%s') not found in query result for player %d.\n",
              view_col_name,
              client_field_name,
              player_id);
          }
      }
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      LOGD ("db_player_info_selected_fields: Player ID %d not found.\n",
            player_id);
      rc = SQLITE_NOTFOUND;
    }
  else
    {
      LOGE (
        "db_player_info_selected_fields: Query execution failed for player %d: %s\n",
        player_id,
        sqlite3_errmsg (db));
    }
  sqlite3_finalize (stmt);
  db_mutex_unlock ();
  return rc;
}


int
db_sector_beacon_text (int sector_id, char **out_text)
{
  // Initialize all pointers to NULL for a clean slate.
  sqlite3_stmt *st = NULL;
  // The final return code for the function.
  int ret_code = SQLITE_ERROR;
  // 1. Acquire the lock at the beginning of the function.
  db_mutex_lock ();
  // Initialize the output pointer.
  if (out_text)
    {
      *out_text = NULL;
    }
  // Get the database handle.
  sqlite3 *dbh = db_get_handle ();


  if (!dbh)
    {
      ret_code = SQLITE_ERROR;
      goto cleanup;
    }
  const char *sql = "SELECT beacon FROM sectors WHERE id=?";
  // 2. Prepare the statement. Check for errors and jump to cleanup if needed.
  int rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }
  // 3. Bind the parameter.
  sqlite3_bind_int (st, 1, sector_id);
  // 4. Get results. A successful step will return SQLITE_ROW.
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      const unsigned char *txt = sqlite3_column_text (st, 0);


      if (txt && *txt)
        {
          const char *c = (const char *) txt;


          if (out_text)
            {
              // The caller is now responsible for freeing this memory.
              *out_text = strdup (c);
            }
        }
      ret_code = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      // No rows were returned, which is a successful query.
      ret_code = SQLITE_OK;
    }
  else
    {
      // An error occurred during sqlite3_step.
      ret_code = rc;
    }
cleanup:
  // 5. Always finalize the SQLite statement if it was created.
  if (st)
    {
      sqlite3_finalize (st);
    }
  // 6. Always release the lock at the very end.
  db_mutex_unlock ();
  // 7. A single return statement, as per the pattern.
  return ret_code;
}


int
db_ships_at_sector_json (int player_id, int sector_id, json_t **out)
{
  (void) player_id;
  sqlite3_stmt *st = NULL;
  int ret_code = SQLITE_ERROR;  /* default to error until succeeded */


  if (out)
    {
      *out = NULL;
    }
  /* 1) Lock DB */
  db_mutex_lock ();
  /* 2) Result array */
  json_t *ships = json_array ();


  if (!ships)
    {
      ret_code = SQLITE_NOMEM;
      goto cleanup;
    }
  /* 3) Query: ship name, type name, owner name, ship id (by sector) */
  const char *sql =
    "SELECT s.name, st.name, p.name, s.id " "FROM ships s "
    "LEFT JOIN shiptypes st ON s.type_id = st.id "
    "LEFT JOIN ship_ownership so ON s.id = so.ship_id AND so.role_id = 1 "                                                                                                                              /* Assuming role_id 1 is the owner */
    "LEFT JOIN players p ON so.player_id = p.id "
    "WHERE s.sector=?;";
  int rc = sqlite3_prepare_v2 (db_get_handle (), sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, sector_id);
  /* 4) Build JSON rows */
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      const char *ship_name = (const char *) sqlite3_column_text (st, 0);
      const char *ship_type_name = (const char *) sqlite3_column_text (st, 1);
      const char *owner_name = (const char *) sqlite3_column_text (st, 2);
      int ship_id = sqlite3_column_int (st, 3);
      json_t *ship = json_object ();


      if (!ship)
        {
          ret_code = SQLITE_NOMEM;
          goto cleanup;
        }
      /* Optional generic keys for client consistency (match ports): */
      json_object_set_new (ship, "id", json_integer (ship_id));
      json_object_set_new (ship, "name",
                           json_string (ship_name ? ship_name : ""));
      json_object_set_new (ship, "type",
                           json_string (ship_type_name ? ship_type_name :
                                        ""));
      /* Legacy / verbose keys you were already using: */
      json_object_set_new (ship, "ship_name",
                           json_string (ship_name ? ship_name : ""));
      json_object_set_new (ship, "ship_type",
                           json_string (ship_type_name ? ship_type_name :
                                        ""));
      /* Owner: default to "derelict" when NULL/empty */
      if (owner_name && *owner_name)
        {
          json_object_set_new (ship, "owner", json_string (owner_name));
        }
      else
        {
          json_object_set_new (ship, "owner", json_string ("derelict"));
        }
      json_array_append_new (ships, ship);
    }
  /* 5) Success path */
  ret_code = SQLITE_OK;
  if (out)
    {
      *out = ships;             /* transfer ownership */
      ships = NULL;             /* prevent cleanup from freeing it */
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  /* Only free if we did NOT transfer ownership */
  if (ships)
    {
      json_decref (ships);
    }
  db_mutex_unlock ();
  return ret_code;
}


int
db_ports_at_sector_json (int sector_id, json_t **out_array)
{
  // Initialize all pointers to NULL for a clean slate.
  *out_array = NULL;
  json_t *ports = NULL;
  sqlite3_stmt *st = NULL;
  int rc;                       // For SQLite return codes.
  int ret_code = -1;            // The final return code for the function.


  // 1. Acquire the lock at the beginning of the function.
  db_mutex_lock ();
  // Allocate the JSON array. If this fails, we jump to cleanup.
  ports = json_array ();
  if (!ports)
    {
      ret_code = SQLITE_NOMEM;
      goto cleanup;
    }
  const char *sql = "SELECT id, name, type FROM ports WHERE sector=?;";


  // 2. Prepare the statement. Check for errors and jump to cleanup if needed.
  rc = sqlite3_prepare_v2 (db_get_handle (), sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, sector_id);
  // 3. Loop through the results. If an error occurs (e.g., failed json_object() allocation),
  // we jump to cleanup to free all resources and the mutex.
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int port_id = sqlite3_column_int (st, 0);
      const char *port_name = (const char *) sqlite3_column_text (st, 1);
      const char *port_type = (const char *) sqlite3_column_text (st, 2);
      json_t *port = json_object ();


      if (!port)
        {
          ret_code = SQLITE_NOMEM;
          goto cleanup;
        }
      json_object_set_new (port, "id", json_integer (port_id));
      json_object_set_new (port, "name", json_string (port_name));
      json_object_set_new (port, "type", json_string (port_type));
      json_array_append_new (ports, port);
    }
  // 4. If the loop finished, set the final return code to success.
  ret_code = SQLITE_OK;
  *out_array = ports;
  ports = NULL;                 // We've transferred ownership, so set to NULL to prevent freeing in cleanup.
cleanup:
  // 5. Always finalize the SQLite statement if it was created.
  if (st)
    {
      sqlite3_finalize (st);
    }
  // 6. Always clean up the `ports` array if it was allocated but not returned.
  if (ports)
    {
      json_decref (ports);
    }
  // 7. Always release the lock at the very end.
  db_mutex_unlock ();
  return ret_code;
}


// In database.c
int
db_sector_has_beacon (int sector_id)
{
  sqlite3_stmt *stmt = NULL;
  int has_beacon = 0; // Moved declaration and initialization to top
  // 1. Acquire the lock before any database interaction.
  db_mutex_lock ();
  // 2. Prepare the statement. This is the first place an error could occur.
  const char *sql = "SELECT beacon FROM sectors WHERE id = ?;";


  if (sqlite3_prepare_v2 (db_get_handle (), sql, -1, &stmt, NULL) !=
      SQLITE_OK)
    {
      /* fprintf (stderr, "SQL error in db_sector_has_beacon: %s\n", */
      /*               sqlite3_errmsg (db_get_handle ())); */
      /* return -1;             // Indicates an error */
      // If preparation fails, we jump to the cleanup block to release the lock.
      goto cleanup;
    }
  // 3. Bind the parameter.
  sqlite3_bind_int (stmt, 1, sector_id);
  // 4. Execute the statement.
  int rc = sqlite3_step (stmt);


  if (rc == SQLITE_ROW)
    {
      if (sqlite3_column_text (stmt, 0) != NULL)
        {
          has_beacon = 1;
        }
    }
  // The cleanup label is the single point of exit.
cleanup:
  // 5. Finalize the statement if it was successfully prepared.
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  // 6. Release the lock at the end of the function, regardless of success or failure.
  db_mutex_unlock ();
  return has_beacon;
}


int
db_sector_set_beacon (int sector_id, const char *beacon_text, int player_id)
{
  sqlite3 *dbh = db_get_handle ();
  sqlite3_stmt *st_sel = NULL, *st_upd = NULL;
  sqlite3_stmt *st_asset = NULL;
  int rc = SQLITE_ERROR, had_beacon = 0;
  db_mutex_lock ();
  // 1. SELECT: Check for existing beacon
  const char *sql_sel = "SELECT beacon FROM sectors WHERE id=?1;";


  rc = sqlite3_prepare_v2 (dbh, sql_sel, -1, &st_sel, NULL);
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  sqlite3_bind_int (st_sel, 1, sector_id);
  rc = sqlite3_step (st_sel);
  if (rc == SQLITE_ROW)
    {
      const unsigned char *txt = sqlite3_column_text (st_sel, 0);


      had_beacon = (txt && txt[0]) ? 1 : 0;
    }
  else if (rc != SQLITE_DONE)
    {
      goto cleanup;
    }
  sqlite3_finalize (st_sel);
  st_sel = NULL;
  // 2. UPDATE: Update the sectors table (beacon text)
  const char *sql_upd = "UPDATE sectors SET beacon=?1 WHERE id=?2;";


  rc = sqlite3_prepare_v2 (dbh, sql_upd, -1, &st_upd, NULL);
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  if (had_beacon && (beacon_text == NULL || *beacon_text == '\0'))
    {
      // Explode/Clear: Set sectors.beacon to NULL
      sqlite3_bind_null (st_upd, 1);
    }
  else
    {
      if (beacon_text && *beacon_text)
        {
          sqlite3_bind_text (st_upd, 1, beacon_text, -1, SQLITE_TRANSIENT);
        }
      else
        {
          sqlite3_bind_null (st_upd, 1);
        }
    }
  sqlite3_bind_int (st_upd, 2, sector_id);
  rc = sqlite3_step (st_upd);
  if (rc == SQLITE_DONE)
    {
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st_upd);
  st_upd = NULL;
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  // 3. Asset Tracking: Update the sector_assets table (ownership)
  if (had_beacon && (beacon_text == NULL || *beacon_text == '\0'))
    {
      // Case A: Beacon removed (Exploded/Cancelled) - DELETE asset ownership
      const char *sql_del_asset =
        "DELETE FROM sector_assets WHERE sector=?1 AND asset_type='3';";


      rc = sqlite3_exec (dbh, sql_del_asset, NULL, NULL, NULL);
      if (rc != SQLITE_OK)
        {
          LOGE
            ("db_sector_set_beacon: DELETE asset failed for sector %d, rc=%d",
            sector_id, rc);
        }
    }
  else if (beacon_text && *beacon_text)
    {
      // Case B: Beacon set or updated - INSERT OR REPLACE asset ownership
      const char *sql_ins_asset =
        "INSERT OR REPLACE INTO sector_assets (sector, asset_type, player, quantity, deployed_at) "
        "VALUES (?1, ?2, ?3, 1, ?4);";                                                                                                                          // *** ADDED: quantity and deployed_at ***


      rc = sqlite3_prepare_v2 (dbh, sql_ins_asset, -1, &st_asset, NULL);
      if (rc != SQLITE_OK)
        {
          LOGE
          (
            "db_sector_set_beacon: INSERT asset PREPARE failed for sector %d, rc=%d. Msg: %s",
            sector_id,
            rc,
            sqlite3_errmsg (dbh));
          goto cleanup;
        }
      // Get current timestamp here if not available globally
      int64_t now_s = (int64_t) time (NULL);


      sqlite3_bind_int (st_asset, 1, sector_id);
      sqlite3_bind_text (st_asset, 2, "3", -1, SQLITE_STATIC);  // Use "3" as you were using
      sqlite3_bind_int (st_asset, 3, player_id);        // BIND the player_id
      sqlite3_bind_int64 (st_asset, 4, now_s);  // *** NEW: BIND deployed_at ***
      rc = sqlite3_step (st_asset);
      if (rc == SQLITE_DONE)
        {
          rc = SQLITE_OK;
        }
      sqlite3_finalize (st_asset);
      st_asset = NULL;
    }
cleanup:
  if (st_sel)
    {
      sqlite3_finalize (st_sel);
    }
  if (st_upd)
    {
      sqlite3_finalize (st_upd);
    }
  if (st_asset)
    {
      sqlite3_finalize (st_asset);
    }
  db_mutex_unlock ();
  return rc;
}


// In database.c
int
db_player_has_beacon_on_ship (int player_id)
{
  sqlite3_stmt *stmt = NULL;
  int has_beacon = 0; // Moved declaration and initialization to top
  // 1. Acquire the lock before any database interaction.
  db_mutex_lock ();
  const char *sql =
    "SELECT T2.beacons FROM players AS T1 JOIN ships AS T2 ON T1.ship = T2.id WHERE T1.id = ?;";
  // 2. Prepare the statement. This is the first place an error could occur.
  int rc = sqlite3_prepare_v2 (db_get_handle (), sql, -1, &stmt, NULL);


  if (rc != SQLITE_OK)
    {
      // If preparation fails, we jump to the cleanup block to release the lock.
      goto cleanup;
    }
  // 3. Bind the parameter.
  sqlite3_bind_int (stmt, 1, player_id);
  // 4. Execute the statement.
  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      if (sqlite3_column_int (stmt, 0) > 0)
        {
          has_beacon = 1;
        }
    }
  // The cleanup label is the single point of exit.
cleanup:
  // 5. Finalize the statement if it was successfully prepared.
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  // 6. Release the lock at the end of the function, regardless of success or failure.
  db_mutex_unlock ();
  // sqlite3_finalize(stmt);
  return has_beacon;
}


// This is the correct, thread-safe way to implement the function.
int
db_player_decrement_beacon_count (int player_id)
{
  sqlite3_stmt *stmt = NULL;
  int rc = -1;                  // Initialize return code to a non-OK value.
  // 1. Acquire the lock before any database interaction.
  db_mutex_lock ();
  const char *sql =
    "UPDATE ships SET beacons = beacons - 1 WHERE id = (SELECT ship FROM players WHERE id = ?);";


  // 2. Prepare the statement. This is the first place an error could occur.
  rc = sqlite3_prepare_v2 (db_get_handle (), sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      // If preparation fails, we jump to the cleanup block to release the lock.
      goto cleanup;
    }
  // 3. Bind the parameter.
  sqlite3_bind_int (stmt, 1, player_id);
  // 4. Execute the statement.
  rc = sqlite3_step (stmt);
  // The cleanup label is the single point of exit.
cleanup:
  // 5. Finalize the statement if it was successfully prepared.
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  // 6. Release the lock at the end of the function, regardless of success or failure.
  db_mutex_unlock ();
  return rc;
}


// Post-bigbang fix: chain trap sectors and bridge to the main graph.
// Returns SQLITE_OK on success (or no work to do), otherwise an SQLite error code.
int
db_chain_traps_and_bridge (int fedspace_max /* typically 10 */ )
{
  sqlite3 *dbh = db_get_handle ();
  if (!dbh)
    {
      return SQLITE_ERROR;
    }
  int rc = SQLITE_ERROR;
  sqlite3_stmt *st_traps = NULL;
  sqlite3_stmt *st_ins = NULL;
  sqlite3_stmt *st_anchor = NULL;
  // 1) Collect trap sector ids (id > fedspace_max), i.e., sectors with 0 in and 0 out.
  //    NB: Uses the sector_warps indexes you already create.
  const char *sql_traps =
    "WITH ow AS (SELECT from_sector AS id, COUNT(*) AS c FROM sector_warps GROUP BY from_sector), "
    "     iw AS (SELECT to_sector   AS id, COUNT(*) AS c FROM sector_warps GROUP BY to_sector) "
    "SELECT s.id "
    "FROM sectors s "
    "LEFT JOIN ow ON ow.id = s.id "
    "LEFT JOIN iw ON iw.id = s.id "
    "WHERE s.id > ?1 AND COALESCE(ow.c,0)=0 AND COALESCE(iw.c,0)=0 "
    "ORDER BY s.id;";


  rc = sqlite3_prepare_v2 (dbh, sql_traps, -1, &st_traps, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int (st_traps, 1, fedspace_max);
  // We’ll store trap ids in memory first.
  int cap = 64, n = 0;
  int *traps = (int *) malloc (sizeof (int) * cap);


  if (!traps)
    {
      rc = SQLITE_NOMEM;
      goto cleanup_only;
    }
  while ((rc = sqlite3_step (st_traps)) == SQLITE_ROW)
    {
      if (n == cap)
        {
          cap *= 2;
          int *tmp = (int *) realloc (traps, sizeof (int) * cap);


          if (!tmp)
            {
              free (traps);
              rc = SQLITE_NOMEM;
              goto cleanup_only;
            }
          traps = tmp;
        }
      traps[n++] = sqlite3_column_int (st_traps, 0);
    }
  if (rc != SQLITE_DONE)
    {
      free (traps);
      goto cleanup_only;
    }
  // Nothing to do? Just return OK.
  if (n == 0)
    {
      rc = SQLITE_OK;
      goto cleanup_only;
    }
  // 2) Chain them bidirectionally: (a<->b), (b<->c), ...
  const char *sql_ins =
    "INSERT OR IGNORE INTO sector_warps(from_sector, to_sector) VALUES (?1, ?2);";


  rc = sqlite3_prepare_v2 (dbh, sql_ins, -1, &st_ins, NULL);
  if (rc != SQLITE_OK)
    {
      free (traps);
      goto cleanup_only;
    }
  for (int i = 0; i + 1 < n; ++i)
    {
      // forward
      sqlite3_bind_int (st_ins, 1, traps[i]);
      sqlite3_bind_int (st_ins, 2, traps[i + 1]);
      if ((rc = sqlite3_step (st_ins)) != SQLITE_DONE)
        {
          free (traps);
          goto cleanup_only;
        }
      sqlite3_reset (st_ins);
      // reverse
      sqlite3_bind_int (st_ins, 1, traps[i + 1]);
      sqlite3_bind_int (st_ins, 2, traps[i]);
      if ((rc = sqlite3_step (st_ins)) != SQLITE_DONE)
        {
          free (traps);
          goto cleanup_only;
        }
      sqlite3_reset (st_ins);
    }
  // 3) Pick a random “anchor” sector that looks like it’s in the main graph:
  //    any non-FedSpace sector that already participates in at least one warp
  //    (incoming OR outgoing). Then bridge anchor <-> first_trap.
  const char *sql_anchor =
    "WITH x AS ("
    "  SELECT s.id "
    "  FROM sectors s "
    "  WHERE s.id > ?1 AND EXISTS ("
    "    SELECT 1 FROM sector_warps w "
    "    WHERE w.from_sector = s.id OR w.to_sector = s.id"
    "  )" ") SELECT id FROM x ORDER BY RANDOM() LIMIT 1;";


  rc = sqlite3_prepare_v2 (dbh, sql_anchor, -1, &st_anchor, NULL);
  if (rc != SQLITE_OK)
    {
      free (traps);
      goto cleanup_only;
    }
  sqlite3_bind_int (st_anchor, 1, fedspace_max);
  int anchor = 0;


  rc = sqlite3_step (st_anchor);
  if (rc == SQLITE_ROW)
    {
      anchor = sqlite3_column_int (st_anchor, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      anchor = 0;
      rc = SQLITE_OK;
    }                           // no candidate
  else
    {
      free (traps);
      goto cleanup_only;
    }
  // If we didn’t find any anchor (e.g., no other warps were built yet),
  // fall back to FedSpace 1 as the anchor (if allowed by your rules).
  if (anchor == 0)
    {
      anchor = fedspace_max + 1; // very small fallback; adjust if needed.
    }
  // Bridge: anchor <-> traps[0]
  sqlite3_bind_int (st_ins, 1, anchor);
  sqlite3_bind_int (st_ins, 2, traps[0]);
  if ((rc = sqlite3_step (st_ins)) != SQLITE_DONE)
    {
      free (traps);
      goto cleanup_only;
    }
  sqlite3_reset (st_ins);
  sqlite3_bind_int (st_ins, 1, traps[0]);
  sqlite3_bind_int (st_ins, 2, anchor);
  if ((rc = sqlite3_step (st_ins)) != SQLITE_DONE)
    {
      free (traps);
      goto cleanup_only;
    }
  sqlite3_reset (st_ins);
  free (traps);
  // All good.
  rc = SQLITE_OK;
cleanup_only:
  if (st_traps)
    {
      sqlite3_finalize (st_traps);
    }
  if (st_ins)
    {
      sqlite3_finalize (st_ins);
    }
  if (st_anchor)
    {
      sqlite3_finalize (st_anchor);
    }
  return rc;
}


int
db_ships_inspectable_at_sector_json (int player_id, int sector_id,
                                     json_t **out_array)
{
  if (!out_array)
    {
      return SQLITE_MISUSE;
    }
  *out_array = NULL;
  static const char *SQL =
    "SELECT "
    "  s.id AS ship_id, "
    "  COALESCE(NULLIF(s.name,''), st.name || ' #' || s.id) AS ship_name, "
    "  st.id AS type_id, "
    "  st.name AS type_name, " "  s.sector AS sector_id, "
    /* owner from ship_ownership (NOT pilot) */
    "  own.player_id AS owner_id, "
    "  COALESCE( (SELECT name FROM players WHERE id = own.player_id), 'derelict') AS owner_name, "
    /* derelict/boardable == unpiloted, regardless of owner */
    "  CASE WHEN pil.id IS NULL THEN 1 ELSE 0 END AS is_derelict, "
    "  s.fighters, s.shields, "
    "  s.holds AS holds_total, (s.holds - s.holds) AS holds_free, "
    "  s.ore, s.organics, s.equipment, s.colonists, "
    "  COALESCE(s.flags,0) AS flags, s.id AS registration, COALESCE(s.perms, 731) AS perms "
    "FROM ships s " "LEFT JOIN shiptypes      st  ON st.id = s.type "
    "LEFT JOIN ship_ownership own ON own.ship_id = s.id "
    "LEFT JOIN players pil ON pil.ship = s.id "                                                                                                                                                                                                                                                                                                                                                                                                                                 /* current pilot (if any) */
    "WHERE s.sector = ? "
    "  AND (pil.id IS NULL OR pil.id != ?) "
    "ORDER BY is_derelict DESC, s.id ASC;";
  int rc = SQLITE_OK;
  sqlite3_stmt *stmt = NULL;
  json_t *arr = NULL;


  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db_get_handle (), SQL, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      goto fail_locked;
    }
  sqlite3_bind_int (stmt, 1, sector_id);
  sqlite3_bind_int (stmt, 2, player_id);
  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto fail_locked;
    }
  while ((rc = sqlite3_step (stmt)) == SQLITE_ROW)
    {
      int c = 0;
      int ship_id = sqlite3_column_int (stmt, c++);
      const char *ship_nm = (const char *) sqlite3_column_text (stmt, c++);


      if (!ship_nm)
        {
          ship_nm = "";
        }
      int type_id = sqlite3_column_int (stmt, c++);
      const char *type_nm = (const char *) sqlite3_column_text (stmt, c++);


      if (!type_nm)
        {
          type_nm = "";
        }
      int sector_row = sqlite3_column_int (stmt, c++);
      int owner_id =
        (sqlite3_column_type (stmt, c) ==
         SQLITE_NULL) ? 0 : sqlite3_column_int (stmt, c);


      c++;
      const char *owner_nm = (const char *) sqlite3_column_text (stmt, c++);


      if (!owner_nm)
        {
          owner_nm = "derelict";
        }
      int is_derelict = sqlite3_column_int (stmt, c++);
      int fighters = sqlite3_column_int (stmt, c++);
      int shields = sqlite3_column_int (stmt, c++);
      int holds_total = sqlite3_column_int (stmt, c++);
      int holds_free = sqlite3_column_int (stmt, c++);
      int ore = sqlite3_column_int (stmt, c++);
      int organics = sqlite3_column_int (stmt, c++);
      int equipment = sqlite3_column_int (stmt, c++);
      int colonists = sqlite3_column_int (stmt, c++);
      int flags = sqlite3_column_int (stmt, c++);
      const char *reg = (const char *) sqlite3_column_text (stmt, c++);


      if (!reg)
        {
          reg = "";
        }
      int perms = sqlite3_column_int (stmt, c++);
      char perm_str[8];


      snprintf (perm_str, sizeof (perm_str), "%03d", perms);
      json_t *row = json_object ();
      json_object_set_new (row, "id", json_integer (ship_id));
      json_object_set_new (row, "name", json_string (ship_nm));


      json_t *type_obj = json_object ();
      json_object_set_new (type_obj, "id", json_integer (type_id));
      json_object_set_new (type_obj, "name", json_string (type_nm));
      json_object_set_new (row, "type", type_obj);


      json_object_set_new (row, "sector_id", json_integer (sector_row));


      json_t *owner_obj = json_object ();
      json_object_set_new (owner_obj, "id", json_integer (owner_id));
      json_object_set_new (owner_obj, "name", json_string (owner_nm));
      json_object_set_new (row, "owner", owner_obj);


      json_t *flags_obj = json_object ();
      json_object_set_new (flags_obj, "derelict", json_boolean (is_derelict != 0));
      json_object_set_new (flags_obj, "boardable", json_boolean (is_derelict != 0));
      json_object_set_new (flags_obj, "raw", json_integer (flags));
      json_object_set_new (row, "flags", flags_obj);


      json_t *defence_obj = json_object ();
      json_object_set_new (defence_obj, "shields", json_integer (shields));
      json_object_set_new (defence_obj, "fighters", json_integer (fighters));
      json_object_set_new (row, "defence", defence_obj);


      json_t *holds_obj = json_object ();
      json_object_set_new (holds_obj, "total", json_integer (holds_total));
      json_object_set_new (holds_obj, "free", json_integer (holds_free));
      json_object_set_new (row, "holds", holds_obj);


      json_t *cargo_obj_inner = json_object ();
      json_object_set_new (cargo_obj_inner, "ore", json_integer (ore));
      json_object_set_new (cargo_obj_inner, "organics", json_integer (organics));
      json_object_set_new (cargo_obj_inner, "equipment", json_integer (equipment));
      json_object_set_new (cargo_obj_inner, "colonists", json_integer (colonists));
      json_object_set_new (row, "cargo", cargo_obj_inner);


      json_t *perms_obj = json_object ();
      json_object_set_new (perms_obj, "value", json_integer (perms));
      json_object_set_new (perms_obj, "octal", json_string (perm_str));
      json_object_set_new (row, "perms", perms_obj);


      json_object_set_new (row, "registration", json_string (reg));


      if (!row)
        {
          rc = SQLITE_NOMEM;
          goto fail_locked;
        }
      json_array_append_new (arr, row);
    }
  if (rc != SQLITE_DONE)
    {
      goto fail_locked;
    }
  sqlite3_finalize (stmt);
  db_mutex_unlock ();
  *out_array = arr;
  return SQLITE_OK;
fail_locked:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  db_mutex_unlock ();
  if (arr)
    {
      json_decref (arr);
    }
  return rc;
}


//////////////////////////////////////////////////////////////////////////////////////////
int
db_ship_flags_set (int ship_id, int mask)
{
  int rc;
  db_mutex_lock ();
  rc = sqlite3_exec (db_get_handle (), "BEGIN IMMEDIATE", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      goto out_unlock;
    }
  sqlite3_stmt *st = NULL;


  rc = sqlite3_prepare_v2 (db_get_handle (),
                           "UPDATE ships SET flags = COALESCE(flags,0) | ? WHERE id=?;",
                           -1,
                           &st,
                           NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, mask);
      sqlite3_bind_int (st, 2, ship_id);
      rc = (sqlite3_step (st) == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  if (rc == SQLITE_OK)
    {
      sqlite3_exec (db_get_handle (), "COMMIT", NULL, NULL, NULL);
    }
  else
    {
      sqlite3_exec (db_get_handle (), "ROLLBACK", NULL, NULL, NULL);
    }
out_unlock:
  db_mutex_unlock ();
  return rc;
}


int
db_ship_flags_clear (int ship_id, int mask)
{
  int rc;
  db_mutex_lock ();
  rc = sqlite3_exec (db_get_handle (), "BEGIN IMMEDIATE", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      goto out_unlock;
    }
  sqlite3_stmt *st = NULL;


  rc = sqlite3_prepare_v2 (db_get_handle (),
                           "UPDATE ships SET flags = COALESCE(flags,0) & ~? WHERE id=?;",
                           -1,
                           &st,
                           NULL);
  if (rc == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, mask);
      sqlite3_bind_int (st, 2, ship_id);
      rc = (sqlite3_step (st) == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  if (rc == SQLITE_OK)
    {
      sqlite3_exec (db_get_handle (), "COMMIT", NULL, NULL, NULL);
    }
  else
    {
      sqlite3_exec (db_get_handle (), "ROLLBACK", NULL, NULL, NULL);
    }
out_unlock:
  db_mutex_unlock ();
  return rc;
}


int
db_sector_info_json (int sector_id, json_t **out)
{
  sqlite3 *db = NULL;
  sqlite3_stmt *st = NULL;
  json_t *root = NULL;
  int rc = SQLITE_ERROR;        // Default to error
  // 1. Acquire the lock FIRST to ensure thread safety
  db_mutex_lock ();
  if (out)
    {
      *out = NULL;
    }
  db = db_get_handle ();
  if (!db)
    {
      goto cleanup;
    }
  root = json_object ();
  json_object_set_new (root, "sector_id", json_integer (sector_id));
  /* 0) Sector core: name (+ optional beacon if present in schema) */
  {
    const char *sql_min = "SELECT name FROM sectors WHERE id=?1;";


    if (sqlite3_prepare_v2 (db, sql_min, -1, &st, NULL) == SQLITE_OK)
      {
        sqlite3_bind_int (st, 1, sector_id);
        if (sqlite3_step (st) == SQLITE_ROW)
          {
            const char *nm = (const char *) sqlite3_column_text (st, 0);


            if (nm)
              {
                json_object_set_new (root, "name", json_string (nm));
              }
          }
        sqlite3_finalize (st);
        st = NULL;
      }
    const char *sql_rich =
      "SELECT "
      "  COALESCE(beacon_text, ''), COALESCE(beacon_by, 0), "
      "  COALESCE(security_level, 0), COALESCE(safe_zone, 0) "
      "FROM sectors WHERE id=?1;";


    if (sqlite3_prepare_v2 (db, sql_rich, -1, &st, NULL) == SQLITE_OK)
      {
        sqlite3_bind_int (st, 1, sector_id);
        if (sqlite3_step (st) == SQLITE_ROW)
          {
            const char *btxt = (const char *) sqlite3_column_text (st, 0);
            int bby = sqlite3_column_int (st, 1);
            int sec_level = sqlite3_column_int (st, 2);
            int safe = sqlite3_column_int (st, 3);


            if (btxt && btxt[0])
              {
                json_t *b = json_object ();
                json_object_set_new (b, "text", json_string (btxt));
                json_object_set_new (b, "by_player_id", json_integer (bby));


                json_object_set_new (root, "beacon", b);
              }
            if (sec_level != 0 || safe != 0)
              {
                json_t *sec = json_object ();
                json_object_set_new (sec, "level", json_integer (sec_level));
                json_object_set_new (sec, "is_safe_zone",
                                     json_boolean (safe ? 1 : 0));


                json_object_set_new (root, "security", sec);
              }
          }
        sqlite3_finalize (st);
        st = NULL;
      }
  }
/* 1) Adjacency via sector_adjacency(neighbors CSV) */
  {
    const char *sql =
      "SELECT neighbors FROM sector_adjacency WHERE sector_id=?1;";


    if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK)
      {
        sqlite3_bind_int (st, 1, sector_id);
        if (sqlite3_step (st) == SQLITE_ROW)
          {
            const unsigned char *neighbors = sqlite3_column_text (st, 0);
            json_t *adj = parse_neighbors_csv (neighbors);


            if (json_array_size (adj) > 0)
              {
                json_object_set_new (root, "adjacent", adj);
              }
            else
              {
                json_decref (adj);
              }
          }
        sqlite3_finalize (st);
        st = NULL;
      }
  }
  /* 2) Port (first port in sector, if any) via sector_ports */
  {
    const char *sql =
      "SELECT port_id, port_name, COALESCE(type_code,''), COALESCE(is_open,1) "
      "FROM sector_ports WHERE sector_id=?1 ORDER BY port_id LIMIT 1;";


    if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK)
      {
        sqlite3_bind_int (st, 1, sector_id);
        if (sqlite3_step (st) == SQLITE_ROW)
          {
            int pid = sqlite3_column_int (st, 0);
            const char *pname = (const char *) sqlite3_column_text (st, 1);
            const char *ptype = (const char *) sqlite3_column_text (st, 2);
            int is_open = sqlite3_column_int (st, 3);
            json_t *port = json_object ();


            json_object_set_new (port, "id", json_integer (pid));
            if (pname)
              {
                json_object_set_new (port, "name", json_string (pname));
              }
            if (ptype)
              {
                json_object_set_new (port, "type", json_string (ptype));
              }
            json_object_set_new (port, "status",
                                 json_string (is_open ? "open" : "closed"));
            json_object_set_new (root, "port", port);
          }
        sqlite3_finalize (st);
        st = NULL;
      }
  }
  /* 3) Planets via sector_planets */
  {
    const char *sql =
      "SELECT planet_id, planet_name, COALESCE(owner_id,0) "
      "FROM sector_planets WHERE sector_id=?1 ORDER BY planet_id;";


    if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK)
      {
        sqlite3_bind_int (st, 1, sector_id);
        json_t *arr = json_array ();


          while (sqlite3_step (st) == SQLITE_ROW)
          {
            int id = sqlite3_column_int (st, 0);
            const char *nm = (const char *) sqlite3_column_text (st, 1);
            int owner = sqlite3_column_int (st, 2);
            json_t *pl = json_object ();
            json_object_set_new (pl, "id", json_integer (id));
            json_object_set_new (pl, "owner_id", json_integer (owner));


            if (nm)
              {
                json_object_set_new (pl, "name", json_string (nm));
              }
            json_array_append_new (arr, pl);
          }
        sqlite3_finalize (st);
        st = NULL;
        if (json_array_size (arr) > 0)
          {
            json_object_set_new (root, "planets", arr);
          }
        else
          {
            json_decref (arr);
          }
      }
  }
  /* 4) Entities via ships_by_sector (treat them as 'ship' entities) */
  {
    const char *sql =
      "SELECT ship_id, COALESCE(ship_name,''), COALESCE(owner_player_id,0) "
      "FROM ships_by_sector WHERE sector_id=?1 ORDER BY ship_id;";


    if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK)
      {
        sqlite3_bind_int (st, 1, sector_id);
        json_t *arr = json_array ();


        while (sqlite3_step (st) == SQLITE_ROW)
          {
            int id = sqlite3_column_int (st, 0);
            const char *nm = (const char *) sqlite3_column_text (st, 1);
            int owner = sqlite3_column_int (st, 2);
            json_t *e = json_object ();
            json_object_set_new (e, "id", json_integer (id));
            json_object_set_new (e, "kind", json_string ("ship"));
            json_object_set_new (e, "owner_id", json_integer (owner));


            if (nm && *nm)
              {
                json_object_set_new (e, "name", json_string (nm));
              }
            json_array_append_new (arr, e);
          }
        sqlite3_finalize (st);
        st = NULL;
        if (json_array_size (arr) > 0)
          {
            json_object_set_new (root, "entities", arr);
          }
        else
          {
            json_decref (arr);
          }
      }
  }
  /* 5) Security/topology flags via sector_summary (if present) */
  {
    const char *sql =
      "SELECT "
      "  COALESCE(degree, NULL), "
      "  COALESCE(dead_in, NULL), "
      "  COALESCE(dead_out, NULL), "
      "  COALESCE(is_isolated, NULL) "
      "FROM sector_summary WHERE sector_id=?1;";


    if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK)
      {
        sqlite3_bind_int (st, 1, sector_id);
        if (sqlite3_step (st) == SQLITE_ROW)
          {
            int has_any = 0;
            json_t *sec = json_object ();


            if (sqlite3_column_type (st, 0) != SQLITE_NULL)
              {
                json_object_set_new (sec, "degree",
                                     json_integer (sqlite3_column_int
                                                     (st, 0)));
                has_any = 1;
              }
            if (sqlite3_column_type (st, 1) != SQLITE_NULL)
              {
                json_object_set_new (sec, "dead_in",
                                     json_integer (sqlite3_column_int
                                                     (st, 1)));
                has_any = 1;
              }
            if (sqlite3_column_type (st, 2) != SQLITE_NULL)
              {
                json_object_set_new (sec, "dead_out",
                                     json_integer (sqlite3_column_int
                                                     (st, 2)));
                has_any = 1;
              }
            if (sqlite3_column_type (st, 3) != SQLITE_NULL)
              {
                json_object_set_new (sec, "is_isolated",
                                     sqlite3_column_int (st,
                                                         3) ? json_true () :
                                     json_false ());
                has_any = 1;
              }
            if (has_any)
              {
                json_object_set_new (root, "security", sec);
              }
            else
              {
                json_decref (sec);
              }
          }
        sqlite3_finalize (st);
        st = NULL;
      }
  }
  if (out)
    {
      *out = root;
      rc = SQLITE_OK;
    }
  else
    {
      json_decref (root);
      root = NULL;
      rc = SQLITE_OK;
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


/* ---------- BASIC SECTOR (id + name) ---------- */
int
db_sector_basic_json (int sector_id, json_t **out_obj)
{
  sqlite3 *dbh = NULL;
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;        // Default to error
  // Acquire the lock first
  db_mutex_lock ();
  if (!out_obj)
    {
      goto cleanup;             // Nothing to return the data to
    }
  *out_obj = NULL;
  dbh = db_get_handle ();
  if (!dbh)
    {
      goto cleanup;
    }
  const char *sql = "SELECT id, name FROM sectors WHERE id = ?";


  rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, sector_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      *out_obj = json_object ();
      json_object_set_new (*out_obj, "sector_id", json_integer (sqlite3_column_int (st, 0)));
      json_object_set_new (*out_obj, "name", json_string ((const char *) sqlite3_column_text (st, 1)));
      rc = *out_obj ? SQLITE_OK : SQLITE_NOMEM;
    }
  else
    {
      // If sector not found, still return an object with sector_id
      *out_obj = json_object ();
      json_object_set_new (*out_obj, "sector_id", json_integer (sector_id));
      rc = *out_obj ? SQLITE_OK : SQLITE_NOMEM;
    }
cleanup:
  // Finalize the statement if it was prepared
  if (st)
    {
      sqlite3_finalize (st);
    }
  // Release the lock
  db_mutex_unlock ();
  return rc;
}


/* ---------- ADJACENT WARPS (from sector_warps) ---------- */


/**
 * @brief Retrieves a list of adjacent sectors for a given sector, returning them as a JSON array.
 * * This function is thread-safe as all database operations are protected by a mutex lock.
 * It queries the database for adjacent sectors, trying a standard schema first and
 * falling back to a schema with quoted reserved words if necessary.
 *
 * @param sector_id The ID of the sector to query.
 * @param out_array A pointer to a json_t* where the resulting JSON array will be stored.
 * @return SQLITE_OK on success, or an SQLite error code on failure.
 */


//////////////////
int
db_adjacent_sectors_json (int sector_id, json_t **out_array)
{
  if (out_array)
    {
      *out_array = NULL;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_ERROR;
    }
  const char *sql =
    "SELECT to_sector FROM sector_warps WHERE from_sector = ?1 ORDER BY to_sector";
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_ERROR;


  db_mutex_lock ();
  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      goto done;
    }
  sqlite3_bind_int (st, 1, sector_id);
  json_t *arr = json_array ();


  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      int to = sqlite3_column_int (st, 0);


      json_array_append_new (arr, json_integer (to));
    }
  if (rc == SQLITE_DONE)
    {
      if (out_array)
        {
          *out_array = arr;
        }
      rc = SQLITE_OK;
    }
  else
    {
      json_decref (arr);
      rc = SQLITE_ERROR;
    }
done:
  if (st)
    {
      sqlite3_finalize (st);
    }
  db_mutex_unlock ();
  return rc;
}


////////////////


/* ---------- PORTS AT SECTOR (visible only) ---------- */
int
db_port_info_json (int port_id,
                   json_t **out_obj)
{
  sqlite3_stmt *st = NULL;
  json_t *port = NULL;
  json_t *commodities_array = NULL;
  int rc = SQLITE_ERROR;        // Default to error
  sqlite3 *dbh = NULL;
  db_mutex_lock ();
  if (!out_obj)
    {
      goto cleanup;
    }
  *out_obj = NULL;
  dbh = db_get_handle ();
  if (!dbh)
    {
      goto cleanup;
    }
  port = json_object ();
  if (!port)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }
  // First, get the basic port info and commodity on-hand/max values
  const char *port_sql =
    "SELECT id, name, type, techlevel, credits, sector, "
    "ore_on_hand, organics_on_hand, equipment_on_hand, petty_cash "
    "FROM ports WHERE id=?;";


  rc = sqlite3_prepare_v2 (dbh, port_sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, port_id);
  if (sqlite3_step (st) != SQLITE_ROW)
    {
      rc = SQLITE_NOTFOUND;
      goto cleanup;
    }
  json_object_set_new (port, "id", json_integer (sqlite3_column_int (st, 0)));
  json_object_set_new (port, "name",
                       json_string ((const char *)
                                    sqlite3_column_text (st, 1)));
  json_object_set_new (port, "type",
                       json_string ((const char *)
                                    sqlite3_column_text (st, 2)));
  json_object_set_new (port, "tech_level",
                       json_integer (sqlite3_column_int (st, 3)));
  json_object_set_new (port, "credits",
                       json_integer (sqlite3_column_int (st, 4)));
  json_object_set_new (port, "sector_id",
                       json_integer (sqlite3_column_int (st, 5)));
  json_object_set_new (port, "petty_cash", json_integer (sqlite3_column_int (st,
                                                                             9)));
  // petty_cash
  commodities_array = json_array ();
  if (!commodities_array)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }
  // Add Ore info
  json_t *ore_item = json_object ();
  json_object_set_new (ore_item, "commodity", json_string ("ore"));
  json_object_set_new (ore_item, "quantity", json_integer (sqlite3_column_int (st, 6)));
  json_array_append_new (commodities_array, ore_item);


  // Add Organics info
  json_t *organics_item = json_object ();
  json_object_set_new (organics_item, "commodity", json_string ("organics"));
  json_object_set_new (organics_item, "quantity", json_integer (sqlite3_column_int (st, 7)));
  json_array_append_new (commodities_array, organics_item);


  // Add Equipment info
  json_t *equipment_item = json_object ();
  json_object_set_new (equipment_item, "commodity", json_string ("equipment"));
  json_object_set_new (equipment_item, "quantity", json_integer (sqlite3_column_int (st, 8)));
  json_array_append_new (commodities_array, equipment_item);
  sqlite3_finalize (st);
  st = NULL;
  rc = SQLITE_OK;
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  if (rc == SQLITE_OK)
    {
      json_object_set_new (port, "commodities", commodities_array);
      *out_obj = port;
    }
  else
    {
      if (port)
        {
          json_decref (port);
        }
      if (commodities_array)
        {
          json_decref (commodities_array);
        }
    }
  db_mutex_unlock ();
  return rc;
}


/* ---------- PLAYERS AT SECTOR (lightweight: id + name) ---------- */
int
db_players_at_sector_json (int sector_id, json_t **out_array)
{
  sqlite3 *dbh = NULL;
  sqlite3_stmt *st = NULL;
  json_t *arr = NULL;
  int rc = SQLITE_ERROR;        // Default to error
  // 1. Acquire the lock FIRST
  db_mutex_lock ();
  if (!out_array)
    {
      goto cleanup;
    }
  *out_array = NULL;
  dbh = db_get_handle ();
  if (!dbh)
    {
      goto cleanup;
    }
  /* Prefer explicit 'sector' and 'name' columns */
  const char *sql =
    "SELECT id, COALESCE(name, player_name) AS pname FROM players WHERE sector = ? ORDER BY id";


  rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      /* Fallback if some builds use 'sector' instead of 'sector' */
      const char *sql2 =
        "SELECT id, COALESCE(name, player_name) AS pname FROM players WHERE sector = ? ORDER BY id";


      rc = sqlite3_prepare_v2 (dbh, sql2, -1, &st, NULL);
      if (rc != SQLITE_OK)
        {
          goto cleanup;
        }
    }
  sqlite3_bind_int (st, 1, sector_id);
  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      const unsigned char *nm = sqlite3_column_text (st, 1);
      json_t *o = json_object ();
      json_object_set_new (o, "id", json_integer (sqlite3_column_int (st, 0)));
      json_object_set_new (o, "name", json_string (nm ? (const char *) nm : ""));


      if (!o)
        {
          rc = SQLITE_NOMEM;
          goto cleanup;
        }
      json_array_append_new (arr, o);
    }
  if (rc == SQLITE_DONE)
    {
      *out_array = arr;
      rc = SQLITE_OK;
    }
  else
    {
      json_decref (arr);
      arr = NULL;
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  // 2. Release the lock LAST
  db_mutex_unlock ();
  return rc;
}


/* ---------- BEACONS AT SECTOR (optional table) ---------- */
int
db_beacons_at_sector_json (int sector_id, json_t **out_array)
{
  sqlite3 *dbh = NULL;
  sqlite3_stmt *st = NULL;
  json_t *arr = NULL;
  int rc = SQLITE_ERROR;
  // 1. Acquire the lock FIRST
  db_mutex_lock ();
  if (!out_array)
    {
      goto cleanup;
    }
  *out_array = NULL;
  dbh = db_get_handle ();
  if (!dbh)
    {
      goto cleanup;
    }
  const char *sql =
    "SELECT id, owner_id, message "
    "FROM beacons WHERE sector_id = ? ORDER BY id";


  rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, sector_id);
  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      json_t *o = json_object ();
      json_object_set_new (o, "id", json_integer (sqlite3_column_int (st, 0)));
      json_object_set_new (o, "owner_id", json_integer (sqlite3_column_int (st, 1)));
      json_object_set_new (o, "message",
                           json_string ((const char *) sqlite3_column_text (st, 2)));


      if (!o)
        {
          rc = SQLITE_NOMEM;
          json_decref (arr);
          arr = NULL;
          goto cleanup;
        }
      json_array_append_new (arr, o);
    }
  if (rc == SQLITE_DONE)
    {
      *out_array = arr;
      rc = SQLITE_OK;
    }
  else
    {
      json_decref (arr);
      arr = NULL;
    }
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  // 2. Release the lock LAST
  db_mutex_unlock ();
  return rc;
}


/* ---------- PLANETS AT SECTOR ---------- */
int
db_planets_at_sector_json (int sector_id, json_t **out_array)
{
  sqlite3 *dbh = NULL;
  sqlite3_stmt *st = NULL;
  json_t *arr = NULL;
  int rc = SQLITE_ERROR;        // Default to error
  // 1. Acquire the lock FIRST
  db_mutex_lock ();
  if (!out_array)
    {
      goto cleanup;
    }
  *out_array = NULL;
  dbh = db_get_handle ();
  if (!dbh)
    {
      goto cleanup;
    }
  const char *sql =
    "SELECT id, name, owner, fighters, colonist, "
    "fuel, organics, equipment, citadel_level FROM planets WHERE sector = ? ORDER BY id;";


  rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, sector_id);
  arr = json_array ();
  if (!arr)
    {
      rc = SQLITE_NOMEM;
      goto cleanup;
    }
  while ((rc = sqlite3_step (st)) == SQLITE_ROW)
    {
      json_t *o_planet = json_object ();


      if (!o_planet)
        {
          rc = SQLITE_NOMEM;
          goto cleanup;
        }
      json_object_set_new (o_planet, "id",
                           json_integer (sqlite3_column_int (st, 0)));
      json_object_set_new (o_planet, "name",
                           json_string ((const char *)
                                        sqlite3_column_text (st, 1)));
      json_object_set_new (o_planet, "owner_id",
                           (sqlite3_column_type (st, 2) ==
                            SQLITE_NULL) ? json_null () :
                           json_integer (sqlite3_column_int (st, 2)));
      json_object_set_new (o_planet, "fighters",
                           (sqlite3_column_type (st, 3) ==
                            SQLITE_NULL) ? json_null () :
                           json_integer (sqlite3_column_int (st, 3)));
      json_t *colonists = json_object ();


      if (!colonists)
        {
          json_decref (o_planet);
          rc = SQLITE_NOMEM;
          goto cleanup;
        }
      json_object_set_new (colonists, "fuel",
                           (sqlite3_column_type (st, 4) ==
                            SQLITE_NULL) ? json_null () :
                           json_integer (sqlite3_column_int (st, 4)));
      json_object_set_new (colonists, "organics",
                           (sqlite3_column_type (st, 5) ==
                            SQLITE_NULL) ? json_null () :
                           json_integer (sqlite3_column_int (st, 5)));
      json_object_set_new (colonists, "equipment",
                           (sqlite3_column_type (st, 6) ==
                            SQLITE_NULL) ? json_null () :
                           json_integer (sqlite3_column_int (st, 6)));
      json_object_set_new (o_planet, "colonists", colonists);
      json_t *resources = json_object ();


      if (!resources)
        {
          json_decref (o_planet);
          rc = SQLITE_NOMEM;
          goto cleanup;
        }
      json_object_set_new (resources, "fuel",
                           (sqlite3_column_type (st, 7) ==
                            SQLITE_NULL) ? json_null () :
                           json_integer (sqlite3_column_int (st, 7)));
      json_object_set_new (resources, "organics",
                           (sqlite3_column_type (st, 8) ==
                            SQLITE_NULL) ? json_null () :
                           json_integer (sqlite3_column_int (st, 8)));
      json_object_set_new (resources, "equipment",
                           (sqlite3_column_type (st, 9) ==
                            SQLITE_NULL) ? json_null () :
                           json_integer (sqlite3_column_int (st, 9)));
      json_object_set_new (o_planet, "resources", resources);
      json_object_set_new (o_planet, "citadel_level",
                           (sqlite3_column_type (st, 10) ==
                            SQLITE_NULL) ? json_null () :
                           json_integer (sqlite3_column_int (st, 10)));
      json_array_append_new (arr, o_planet);
    }
  if (rc != SQLITE_DONE)
    {
      json_decref (arr);
      arr = NULL;
      goto cleanup;
    }
  *out_array = arr;
  rc = SQLITE_OK;
cleanup:
  if (st)
    {
      sqlite3_finalize (st);
    }
  // 2. Release the lock LAST
  db_mutex_unlock ();
  return rc;
}


int
db_player_set_sector (int player_id, int sector_id)
{
  sqlite3 *db = db_get_handle ();
  if (!db)
    {
      return SQLITE_MISUSE;
    }
  // First, update the player's sector
  sqlite3_stmt *st = NULL;
  int rc =
    sqlite3_prepare_v2 (db, "UPDATE players SET sector=?1 WHERE id=?2;", -1,
                        &st, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("db_player_set_sector: Prepare failed for players update: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, sector_id);
  sqlite3_bind_int (st, 2, player_id);
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("db_player_set_sector: Step failed for players update: %s",
            sqlite3_errmsg (db));
      return rc; // Propagate error
    }
  if (sqlite3_changes (db) == 0)
    {
      LOGW (
        "db_player_set_sector: No player updated for id %d. Does player exist?",
        player_id);
    }
  // Second, update the ship's sector if the player is piloting a ship
  // We need to find the ship ID first.
  int ship_id = 0;
  sqlite3_stmt *st_ship = NULL;


  if (sqlite3_prepare_v2 (db,
                          "SELECT ship FROM players WHERE id=?1;",
                          -1,
                          &st_ship,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st_ship, 1, player_id);
      if (sqlite3_step (st_ship) == SQLITE_ROW)
        {
          ship_id = sqlite3_column_int (st_ship, 0);
        }
      sqlite3_finalize (st_ship);
    }
  if (ship_id > 0)
    {
      sqlite3_stmt *st_upd_ship = NULL;


      if (sqlite3_prepare_v2 (db,
                              "UPDATE ships SET sector=?1 WHERE id=?2;",
                              -1,
                              &st_upd_ship,
                              NULL) == SQLITE_OK)
        {
          sqlite3_bind_int (st_upd_ship, 1, sector_id);
          sqlite3_bind_int (st_upd_ship, 2, ship_id);
          sqlite3_step (st_upd_ship);
          sqlite3_finalize (st_upd_ship);
          if (sqlite3_changes (db) == 0)
            {
              LOGW (
                "db_player_set_sector: No ship updated for id %d (player %d).",
                ship_id,
                player_id);
            }
        }
      else
        {
          LOGE ("db_player_set_sector: Prepare failed for ships update: %s",
                sqlite3_errmsg (db));
        }
    }
  return SQLITE_OK;
}


int
db_player_set_alignment (int player_id, int alignment)
{
  sqlite3 *db = db_get_handle ();
  if (!db || player_id <= 0)
    {
      return SQLITE_MISUSE;
    }
  int rc = SQLITE_ERROR;
  sqlite3_stmt *stmt = NULL;


  db_mutex_lock ();
  static const char *SQL_UPDATE_ALIGNMENT =
    "UPDATE players SET alignment = ? WHERE id = ?;";


  rc = sqlite3_prepare_v2 (db, SQL_UPDATE_ALIGNMENT, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("ERROR: db_player_set_alignment prepare failed: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  sqlite3_bind_int (stmt, 1, alignment);
  sqlite3_bind_int (stmt, 2, player_id);
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      LOGE ("ERROR: db_player_set_alignment execution failed: %s",
            sqlite3_errmsg (db));
      goto cleanup;
    }
  rc = SQLITE_OK;
cleanup:
  if (stmt)
    {
      sqlite3_finalize (stmt);
    }
  db_mutex_unlock ();
  return rc;
}


int
db_player_get_sector (int player_id, int *out_sector)
{
  sqlite3_stmt *st = NULL;
  int ret_code = SQLITE_ERROR;
  int rc;                       // For SQLite's intermediate return codes.
  // 1. Acquire the lock at the very beginning of the function.
  db_mutex_lock ();
  // Initialize the output value to a safe default.
  if (out_sector)
    {
      *out_sector = 0;
    }
  sqlite3 *dbh = db_get_handle ();


  if (!dbh)
    {
      // The database handle is not valid, set return code and go to cleanup.
      goto cleanup;
    }
  const char *sql =
    "SELECT T2.sector FROM players AS T1 JOIN ships AS T2 ON T1.ship = T2.id WHERE T1.id = ?;";


  // 2. Prepare the statement. This is the first point of failure.
  rc = sqlite3_prepare_v2 (dbh, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      ret_code = rc;
      goto cleanup;
    }
  sqlite3_bind_int (st, 1, player_id);
  // 3. Step the statement.
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      // A row was returned successfully.
      if (out_sector)
        {
          *out_sector =
            sqlite3_column_type (st,
                                 0) ==
            SQLITE_NULL ? 0 : sqlite3_column_int (st, 0);
        }
      ret_code = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      // Query completed with no rows, which is a successful result.
      ret_code = SQLITE_OK;
    }
  else
    {
      // An error occurred during the step.
      ret_code = rc;
    }
cleanup:
  // 4. Finalize the statement. This must be done whether the function succeeded or failed.
  if (st)
    {
      sqlite3_finalize (st);
    }
  // 5. Release the lock. This is the final step before returning.
  db_mutex_unlock ();
  // 6. Return the final status code from a single point.
  return ret_code;
}


bool
h_is_black_market_port (sqlite3 *db, int port_id)
{
  sqlite3_stmt *st;
  int rc = sqlite3_prepare_v2 (db,
                               "SELECT type, name FROM ports WHERE id = ?",
                               -1,
                               &st,
                               NULL);
  if (rc != SQLITE_OK)
    {
      return false;
    }
  sqlite3_bind_int (st, 1, port_id);
  bool is_bm = false;


  if (sqlite3_step (st) == SQLITE_ROW)
    {
      // int type = sqlite3_column_int(st, 0); // Unused
      const char *name = (const char *)sqlite3_column_text (st, 1);


      if (name && strstr (name, "Black Market"))
        {
          is_bm = true;
        }
    }
  sqlite3_finalize (st);
  return is_bm;
}


/* ----------------------------------------------------------------------
 * Ports: goods on hand
 * ---------------------------------------------------------------------- */
int
db_port_get_goods_on_hand (int port_id, const char *commodity_code,
                           int *out_quantity)
{
  if (!commodity_code)
    {
      return SQLITE_MISUSE;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return SQLITE_ERROR;
    }
  if (out_quantity)
    {
      *out_quantity = 0;
    }
  const char *sql =
    "SELECT quantity FROM entity_stock WHERE entity_type='port' AND entity_id=?1 AND commodity_code=?2";
  sqlite3_stmt *st = NULL;
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("db_port_get_goods_on_hand prepare failed: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (st, 1, port_id);
  sqlite3_bind_text (st, 2, commodity_code, -1, SQLITE_TRANSIENT);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      if (out_quantity)
        {
          *out_quantity = sqlite3_column_int (st, 0);
        }
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      rc = SQLITE_NOTFOUND;
    }
  else
    {
      LOGE ("db_port_get_goods_on_hand step failed: %s", sqlite3_errmsg (db));
    }
  sqlite3_finalize (st);
  return rc;
}


int
h_get_port_commodity_quantity (sqlite3 *db,
                               int port_id,
                               const char *commodity_code,
                               int *quantity_out)
{
  if (!db || port_id <= 0 || !commodity_code || !quantity_out)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *stmt = NULL;
  const char *sql =
    "SELECT quantity FROM entity_stock WHERE entity_type = 'port' AND entity_id = ?1 AND commodity_code = ?2 LIMIT 1;";
  int rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("h_get_port_commodity_quantity: prepare failed: %s",
            sqlite3_errmsg (db));
      return rc;
    }
  sqlite3_bind_int (stmt, 1, port_id);
  sqlite3_bind_text (stmt, 2, commodity_code, -1, SQLITE_STATIC);
  rc = sqlite3_step (stmt);
  if (rc == SQLITE_ROW)
    {
      *quantity_out = sqlite3_column_int (stmt, 0);
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      *quantity_out = 0;        // Commodity not found in stock
      rc = SQLITE_NOTFOUND;
    }
  else
    {
      LOGE ("h_get_port_commodity_quantity: step failed: %s",
            sqlite3_errmsg (db));
      rc = SQLITE_ERROR;
    }
  sqlite3_finalize (stmt);
  return rc;
}

