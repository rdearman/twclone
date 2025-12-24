/* src/server_players.c */
#include <sqlite3.h>
#include <stdio.h>
#include <stdint.h>
#include <jansson.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
/* local includes */
#include "server_players.h"
#include "database.h"
#include "database_cmd.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_rules.h"
#include "db_player_settings.h"
#include "server_envelope.h"
#include "server_auth.h"
#include "server_log.h"
#include "common.h"
#include "server_ships.h"
#include "server_loop.h"
#include "server_bank.h"

/* Constants */
enum
{ MAX_BOOKMARKS = 64, MAX_BM_NAME = 64 };
enum
{ MAX_AVOIDS = 64 };
static const char *DEFAULT_PLAYER_FIELDS[] = {
  "id",
  "username",
  "credits",
  "sector",
  "faction",
  NULL
};


/* Externs */
extern sqlite3 *db_get_handle (void);
extern client_node_t *g_clients;
extern pthread_mutex_t g_clients_mu;


/* ==================================================================== */


/* STATIC HELPER DEFINITIONS (Bodies placed BEFORE usage)               */


/* ==================================================================== */


int
h_player_is_npc (sqlite3 *db, int player_id)
{
  sqlite3_stmt *stmt = NULL;
  int is_npc = 0;               // Default to 0 (false) if player not found or error

  if (sqlite3_prepare_v2 (db,
                          "SELECT is_npc FROM players WHERE id = ?",
                          -1, &stmt, NULL) != SQLITE_OK)
    {
      return 0;
    }

  sqlite3_bind_int (stmt, 1, player_id);

  if (sqlite3_step (stmt) == SQLITE_ROW)
    {
      is_npc = sqlite3_column_int (stmt, 0);
    }

  sqlite3_finalize (stmt);
  return is_npc;
}


static int
is_ascii_printable (const char *s)
{
  if (!s)
    {
      return 0;
    }
  for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
      if (*p < 0x20 || *p > 0x7E)
        {
          return 0;
        }
    }
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
    {
      return 0;
    }
  size_t n = strlen (s);


  if (n == 0 || n > max)
    {
      return 0;
    }
  for (size_t i = 0; i < n; ++i)
    {
      char c = s[i];


      if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' ||
            c == '_' || c == '-'))
        {
          return 0;
        }
    }
  return 1;
}


static json_t *
prefs_as_array (int64_t pid)
{
  sqlite3_stmt *it = NULL;
  if (db_prefs_get_all (pid, &it) != 0)
    {
      return json_array ();
    }
  json_t *arr = json_array ();


  while (sqlite3_step (it) == SQLITE_ROW)
    {
      const char *k = (const char *) sqlite3_column_text (it, 0);
      const char *t = (const char *) sqlite3_column_text (it, 1);
      const char *v = (const char *) sqlite3_column_text (it, 2);


      json_t *pref_obj = json_object ();


      json_object_set_new (pref_obj, "key", json_string (k ? k : ""));
      json_object_set_new (pref_obj, "type", json_string (t ? t : "string"));
      json_object_set_new (pref_obj, "value", json_string (v ? v : ""));
      json_array_append_new (arr, pref_obj);
    }
  sqlite3_finalize (it);
  return arr;
}


static json_t *
bookmarks_as_array (int64_t pid)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db,
                          "SELECT name, sector_id FROM player_bookmarks WHERE player_id=?1 ORDER BY name",
                          -1,
                          &st,
                          NULL) != SQLITE_OK)
    {
      return json_array ();
    }
  sqlite3_bind_int64 (st, 1, pid);
  json_t *arr = json_array ();


  while (sqlite3_step (st) == SQLITE_ROW)
    {
      const char *name = (const char *) sqlite3_column_text (st, 0);
      /* sqlite: column_text() pointer invalid after finalize/reset/step */
      json_t *bm_obj = json_object ();


      json_object_set_new (bm_obj, "name", json_string (name ? name : ""));
      json_object_set_new (bm_obj, "sector_id",
                           json_integer (sqlite3_column_int (st, 1)));
      json_array_append_new (arr, bm_obj);
    }
  sqlite3_finalize (st);
  return arr;
}


static json_t *
avoid_as_array (int64_t pid)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db,
                          "SELECT sector_id FROM player_avoid WHERE player_id=?1 ORDER BY sector_id",
                          -1,
                          &st,
                          NULL) != SQLITE_OK)
    {
      return json_array ();
    }
  sqlite3_bind_int64 (st, 1, pid);
  json_t *arr = json_array ();


  while (sqlite3_step (st) == SQLITE_ROW)
    {
      json_array_append_new (arr, json_integer (sqlite3_column_int (st, 0)));
    }
  sqlite3_finalize (st);
  return arr;
}


static json_t *
subscriptions_as_array (int64_t pid)
{
  sqlite3_stmt *it = NULL;
  if (db_subscribe_list (pid, &it) != 0)
    {
      return json_array ();
    }
  json_t *arr = json_array ();


  while (sqlite3_step (it) == SQLITE_ROW)
    {
      const char *tmp_topic = (const char *) sqlite3_column_text (it, 0);
      int locked = sqlite3_column_int (it, 1);
      int enabled = sqlite3_column_int (it, 2);
      const char *tmp_delivery = (const char *) sqlite3_column_text (it, 3);
      const char *tmp_filter = (const char *) sqlite3_column_text (it, 4);

      /* sqlite: column_text() pointer invalid after finalize/reset/step */
      char *topic = tmp_topic ? strdup (tmp_topic) : NULL;
      char *delivery = tmp_delivery ? strdup (tmp_delivery) : NULL;
      char *filter = tmp_filter ? strdup (tmp_filter) : NULL;

      json_t *one = json_object ();


      json_object_set_new (one, "topic", json_string (topic ? topic : ""));
      json_object_set_new (one, "locked", json_boolean (locked ? 1 : 0));
      json_object_set_new (one, "enabled", json_boolean (enabled ? 1 : 0));
      json_object_set_new (one, "delivery",
                           json_string (delivery ? delivery : "push"));


      if (filter)
        {
          json_object_set_new (one, "filter", json_string (filter));
        }
      json_array_append_new (arr, one);
      free (topic);
      free (delivery);
      free (filter);
    }
  sqlite3_finalize (it);
  return arr;
}


static json_t *
players_get_subscriptions (client_ctx_t *ctx)
{
  return subscriptions_as_array (ctx->player_id);
}


static json_t *
players_list_bookmarks (client_ctx_t *ctx)
{
  return bookmarks_as_array (ctx->player_id);
}


static json_t *
players_list_avoid (client_ctx_t *ctx)
{
  return avoid_as_array (ctx->player_id);
}


static json_t *
players_list_notes (client_ctx_t *ctx, json_t *req)
{
  (void) ctx;
  (void) req;
  return json_array ();         /* Placeholder */
}


/* ==================================================================== */


/* CORE LOGIC HANDLERS (Transactional)                                  */


/* ==================================================================== */
static const char *
get_turn_error_message (TurnConsumeResult result)
{
  switch (result)
    {
      case TURN_CONSUME_SUCCESS:
        return "Turn consumed successfully.";
      case TURN_CONSUME_ERROR_DB_FAIL:
        return "Database failure prevented turn consumption.";
      case TURN_CONSUME_ERROR_PLAYER_NOT_FOUND:
        return "Player entity not found in turn registry.";
      case TURN_CONSUME_ERROR_NO_TURNS:
        return "You have run out of turns.";
      default:
        return "An unknown error occurred during turn consumption.";
    }
}


int
handle_turn_consumption_error (client_ctx_t *ctx,
                               TurnConsumeResult consume_result,
                               const char *cmd, json_t *root,
                               json_t *meta_data)
{
  const char *reason_str = NULL;
  switch (consume_result)
    {
      case TURN_CONSUME_ERROR_DB_FAIL:
        reason_str = "db_failure";
        break;
      case TURN_CONSUME_ERROR_PLAYER_NOT_FOUND:
        reason_str = "player_not_found";
        break;
      case TURN_CONSUME_ERROR_NO_TURNS:
        reason_str = "no_turns_remaining";
        break;
      default:
        reason_str = "unknown_error";
        break;
    }
  json_t *meta = meta_data ? json_copy (meta_data) : json_object ();


  if (meta)
    {
      json_object_set_new (meta, "reason", json_string (reason_str));
      json_object_set_new (meta, "command", json_string (cmd));
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_REF_NO_TURNS,
                                   get_turn_error_message (consume_result),
                                   NULL);
      json_decref (meta);
    }
  return 0;
}


TurnConsumeResult
h_consume_player_turn (sqlite3 *db_conn, client_ctx_t *ctx,
                       int turns_to_consume)
{
  sqlite3_stmt *stmt = NULL;
  int player_id = ctx->player_id;
  int rc;
  int changes;

  if (turns_to_consume <= 0)
    {
      return TURN_CONSUME_ERROR_INVALID_AMOUNT;
    }

  // Check if player has enough turns first
  int turns_remaining = 0;
  const char *sql_select_turns =
    "SELECT turns_remaining FROM turns WHERE player = ?;";


  rc = sqlite3_prepare_v2 (db_conn, sql_select_turns, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return TURN_CONSUME_ERROR_DB_FAIL;
    }
  sqlite3_bind_int (stmt, 1, player_id);
  rc = sqlite3_step (stmt);
  if (rc == SQLITE_ROW)
    {
      turns_remaining = sqlite3_column_int (stmt, 0);
    }
  sqlite3_finalize (stmt);
  stmt = NULL;

  if (turns_remaining < turns_to_consume)
    {
      return TURN_CONSUME_ERROR_NO_TURNS;
    }

  const char *sql_update =
    "UPDATE turns SET turns_remaining = turns_remaining - ?, last_update = strftime('%s', 'now') "
    "WHERE player = ? AND turns_remaining >= ?;";                                                                                                                       // Ensure turns don't go negative on concurrent access


  rc = sqlite3_prepare_v2 (db_conn, sql_update, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      return TURN_CONSUME_ERROR_DB_FAIL;
    }
  sqlite3_bind_int (stmt, 1, turns_to_consume);
  sqlite3_bind_int (stmt, 2, player_id);
  sqlite3_bind_int (stmt, 3, turns_to_consume);
  rc = sqlite3_step (stmt);
  if (rc != SQLITE_DONE)
    {
      sqlite3_finalize (stmt);
      return TURN_CONSUME_ERROR_DB_FAIL;
    }
  changes = sqlite3_changes (db_conn);
  sqlite3_finalize (stmt);
  if (changes == 0)
    {
      return TURN_CONSUME_ERROR_NO_TURNS;
    }

  return TURN_CONSUME_SUCCESS;
}


int
h_get_player_bank_account_id (sqlite3 *db, int player_id)
{
  int account_id = -1;
  int rc = h_get_account_id_unlocked (db, "player", player_id, &account_id);
  if (rc != SQLITE_OK)
    {
      return -1;
    }
  return account_id;
}


int
h_get_credits (sqlite3 *db, const char *owner_type, int owner_id,
               long long *credits_out)
{
  sqlite3_stmt *st = NULL;
  const char *SQL_ENSURE =
    "INSERT OR IGNORE INTO bank_accounts (owner_type, owner_id, currency, balance) VALUES (?1, ?2, 'CRD', 0);";
  if (sqlite3_prepare_v2 (db, SQL_ENSURE, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_text (st, 1, owner_type, -1, SQLITE_STATIC);
      sqlite3_bind_int (st, 2, owner_id);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }
  const char *SQL_SEL =
    "SELECT COALESCE(balance,0) FROM bank_accounts WHERE owner_type=?1 AND owner_id=?2";


  if (sqlite3_prepare_v2 (db, SQL_SEL, -1, &st, NULL) != SQLITE_OK)
    {
      return SQLITE_ERROR;
    }
  sqlite3_bind_text (st, 1, owner_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, owner_id);
  int rc = SQLITE_NOTFOUND;


  if (sqlite3_step (st) == SQLITE_ROW)
    {
      *credits_out = sqlite3_column_int64 (st, 0);
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


int
h_get_cargo_space_free (sqlite3 *db, int player_id, int *free_out)
{
  if (!free_out)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *st = NULL;
  const char *sql =
    "SELECT (COALESCE(s.holds, 0) - COALESCE(s.colonists + s.equipment + s.organics + s.ore + s.slaves + s.weapons + s.drugs, 0)) FROM players p JOIN ships s ON s.id = p.ship WHERE p.id = ?1";


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (st, 1, player_id);
  int total = 0;


  if (sqlite3_step (st) == SQLITE_ROW)
    {
      total = sqlite3_column_int (st, 0);
    }
  else
    {
      sqlite3_finalize (st);
      return SQLITE_ERROR;
    }
  sqlite3_finalize (st);
  if (total < 0)
    {
      total = 0;
    }
  *free_out = total;
  return SQLITE_OK;
}


int
player_credits (client_ctx_t *ctx)
{
  if (!ctx || ctx->player_id <= 0)
    {
      return 0;
    }
  long long c = 0;


  if (h_get_credits (db_get_handle (), "player", ctx->player_id,
                     &c) != SQLITE_OK)
    {
      return 0;
    }
  return c;
}


int
cargo_space_free (client_ctx_t *ctx)
{
  int f = 0;
  if (h_get_cargo_space_free (db_get_handle (), ctx->player_id,
                              &f) != SQLITE_OK)
    {
      return 0;
    }
  return f;
}


int
h_deduct_ship_credits (sqlite3 *db, int player_id, int amount,
                       int *new_balance)
{
  long long new_balance_ll = 0;
  int rc = h_deduct_credits (db,
                             "player",
                             player_id,
                             amount,
                             "WITHDRAWAL",
                             NULL,
                             &new_balance_ll);
  if (rc == SQLITE_OK && new_balance)
    {
      *new_balance = (int) new_balance_ll;
    }
  return rc;
}


int
h_get_player_sector (int player_id)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db,
                          "SELECT COALESCE(sector, 0) FROM players WHERE id=?",
                          -1, &st, NULL) != SQLITE_OK)
    {
      return 0;
    }
  sqlite3_bind_int (st, 1, player_id);
  int sector = 0;


  if (sqlite3_step (st) == SQLITE_ROW)
    {
      sector = sqlite3_column_int (st, 0);
      if (sector < 0)
        {
          sector = 0;
        }
    }
  sqlite3_finalize (st);
  return sector;
}


int
h_decloak_ship (db_t *db, int ship_id)
{
  if (!db || ship_id <= 0) return ERR_DB_MISUSE;

  db_error_t err;
  db_error_clear(&err);

  int player_id = 0;
  const char *sql_sel = "SELECT player_id FROM ship_ownership WHERE ship_id=$1 AND is_primary=1";
  db_bind_t p_sel[] = { db_bind_i32(ship_id) };
  db_res_t *res_sel = NULL;

  if (db_query(db, sql_sel, p_sel, 1, &res_sel, &err)) {
      if (db_res_step(res_sel, &err)) {
          player_id = db_res_col_i32(res_sel, 0, &err);
      }
      db_res_finalize(res_sel);
  }

  const char *sql_upd = "UPDATE ships SET cloaked = NULL WHERE id = $1 AND cloaked IS NOT NULL";
  db_bind_t p_upd[] = { db_bind_i32(ship_id) };
  
  if (db_exec(db, sql_upd, p_upd, 1, &err)) {
      if (db_exec_rows_affected(db) > 0 && player_id > 0) {
          json_t *payload = json_object();
          json_object_set_new(payload, "ship_id", json_integer(ship_id));
          db_log_engine_event(time(NULL), "ship.decloak", "player", player_id, 0, payload, NULL);
      }
      return 0;
  }

  return err.code;
}


int
h_player_apply_progress (db_t *db,
                         int player_id,
                         long long delta_xp,
                         int delta_align, const char *reason)
{
  if (!db || player_id <= 0)
    {
      return ERR_DB_MISUSE;
    }

  db_error_t err;
  int retry_count;
  int rc = 0;


  for (retry_count = 0; retry_count < 3; retry_count++)
    {
      db_error_clear (&err);
      rc = 0;

      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
        {
          if (err.code == ERR_DB_BUSY)
            {
              usleep (100000);
              continue;
            }
          return err.code;
        }

      db_res_t *res = NULL;
      int cur_align = 0;
      long long cur_xp = 0;

      const char *sql_select =
        "SELECT alignment, experience FROM players WHERE id=$1 FOR UPDATE";
      db_bind_t select_params[] = {
        db_bind_i32 (player_id)
      };
      size_t n_select_params = sizeof(select_params) / sizeof(select_params[0]);


      if (!db_query (db, sql_select, select_params, n_select_params, &res,
                     &err))
        {
          LOGE ("h_player_apply_progress: Select query error: %s", err.message);
          goto rollback;
        }

      if (db_res_step (res, &err))
        {
          cur_align = db_res_col_i32 (res, 0, &err);
          cur_xp = db_res_col_i64 (res, 1, &err);
        }
      else
        {
          db_res_finalize (res);
          if (err.code == ERR_DB_NO_ROWS)
            {
              rc = ERR_NOT_FOUND;
            }
          else
            {
              LOGE ("h_player_apply_progress: Player select fetch error: %s",
                    err.message);
            }
          goto rollback;
        }
      db_res_finalize (res);

      long long new_xp = cur_xp + delta_xp;


      if (new_xp < 0)
        {
          new_xp = 0;
        }
      int new_align = cur_align + delta_align;


      if (new_align > 2000)
        {
          new_align = 2000;
        }
      if (new_align < -2000)
        {
          new_align = -2000;
        }

      const char *sql_update =
        "UPDATE players SET experience=$1, alignment=$2 WHERE id=$3";
      db_bind_t update_params[] = {
        db_bind_i64 (new_xp),
        db_bind_i32 (new_align),
        db_bind_i32 (player_id)
      };
      size_t n_update_params = sizeof(update_params) / sizeof(update_params[0]);


      if (!db_exec (db, sql_update, update_params, n_update_params, &err))
        {
          LOGE ("h_player_apply_progress: Update query error: %s", err.message);
          goto rollback;
        }

      rc = db_player_update_commission (db, player_id);
      if (rc != 0)
        {
          goto rollback;
        }

      if (!db_tx_commit (db, &err))
        {
          LOGE ("h_player_apply_progress: Commit error: %s", err.message);
          goto rollback;
        }

      LOGD ("Player %d progress updated. Reason: %s", player_id,
            reason ? reason : "N/A");
      return 0; // Success

rollback:
      db_tx_rollback (db, &err);
      if (err.code == ERR_DB_BUSY)
        {
          usleep (100000);
          continue;
        }
      return (rc != 0) ? rc : err.code;
    }

  return ERR_DB_BUSY;
}


int
h_deduct_player_petty_cash (sqlite3 *db,
                            int player_id,
                            long long amount, long long *new_balance_out)
{
  if (amount < 0)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *st = NULL;
  int rc;
  const char *sql =
    "UPDATE players SET credits = credits - ?1 WHERE id = ?2 AND credits >= ?1 RETURNING credits";


  rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      return rc;
    }
  sqlite3_bind_int64 (st, 1, amount);
  sqlite3_bind_int (st, 2, player_id);
  rc = sqlite3_step (st);
  if (rc == SQLITE_ROW)
    {
      if (new_balance_out)
        {
          *new_balance_out = sqlite3_column_int64 (st, 0);
        }
      rc = SQLITE_OK;
    }
  else if (rc == SQLITE_DONE)
    {
      rc = SQLITE_CONSTRAINT;
    }
  sqlite3_finalize (st);
  return rc;
}


int
h_add_player_petty_cash (sqlite3 *db,
                         int player_id,
                         long long amount, long long *new_balance_out)
{
  if (amount < 0)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *st = NULL;
  const char *sql =
    "UPDATE players SET credits = credits + ?1 WHERE id = ?2 RETURNING credits";


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      return SQLITE_ERROR;
    }
  sqlite3_bind_int64 (st, 1, amount);
  sqlite3_bind_int (st, 2, player_id);
  int rc = sqlite3_step (st);


  if (rc == SQLITE_ROW)
    {
      if (new_balance_out)
        {
          *new_balance_out = sqlite3_column_int64 (st, 0);
        }
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


int
h_add_player_petty_cash_unlocked (sqlite3 *db,
                                  int player_id,
                                  long long amount, long long *out)
{
  return h_add_player_petty_cash (db, player_id, amount, out);
}


int
h_deduct_player_petty_cash_unlocked (sqlite3 *db,
                                     int player_id,
                                     long long amount, long long *out)
{
  return h_deduct_player_petty_cash (db, player_id, amount, out);
}


int
auth_player_get_type (int player_id)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  int type = 0;
  if (sqlite3_prepare_v2 (db,
                          "SELECT type FROM players WHERE id=?",
                          -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, player_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          type = sqlite3_column_int (st, 0);
        }
      sqlite3_finalize (st);
    }
  return type;
}


int
h_get_player_petty_cash (db_t *db, int player_id, long long *credits_out)
{
  if (!db || player_id <= 0 || !credits_out)
    {
      return ERR_DB_MISUSE;
    }
  
  db_error_t err;
  db_error_clear(&err);
  
  const char *sql = "SELECT credits FROM players WHERE id=$1";
  db_bind_t params[] = { db_bind_i32(player_id) };
  db_res_t *res = NULL;

  if (db_query(db, sql, params, 1, &res, &err)) {
      if (db_res_step(res, &err)) {
          *credits_out = db_res_col_i64(res, 0, &err);
          db_res_finalize(res);
          return 0;
      }
      db_res_finalize(res);
      return ERR_NOT_FOUND;
  }
  return err.code;
}


int
h_player_build_title_payload (sqlite3 *db, int player_id, json_t **out_json)
{
  if (!db || player_id <= 0 || !out_json)
    {
      return SQLITE_MISUSE;
    }
  sqlite3_stmt *st = NULL;
  int align = 0, comm_id = 0;
  long long exp = 0;


  if (sqlite3_prepare_v2 (db,
                          "SELECT alignment, experience, commission FROM players WHERE id=?",
                          -1,
                          &st,
                          NULL) != SQLITE_OK)
    {
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (st, 1, player_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      align = sqlite3_column_int (st, 0);
      exp = sqlite3_column_int64 (st, 1);
      comm_id = sqlite3_column_int (st, 2);
    }
  else
    {
      sqlite3_finalize (st);
      return SQLITE_NOTFOUND;
    }
  sqlite3_finalize (st);
  char *band_code = NULL, *band_name = NULL;
  int is_good = 0, is_evil = 0, can_iss = 0, can_rob = 0;


  db_alignment_band_for_value (db,
                               align,
                               NULL,
                               &band_code,
                               &band_name,
                               &is_good, &is_evil, &can_iss, &can_rob);
  int det_comm_id = 0, comm_is_evil = 0;
  char *comm_title = NULL;


  db_commission_for_player (db,
                            is_evil,
                            exp, &det_comm_id, &comm_title, &comm_is_evil);
  if (comm_id != det_comm_id)
    {
      db_player_update_commission (db, player_id);
      comm_id = det_comm_id;
    }
  json_t *obj = json_object ();


  json_object_set_new (obj, "title",
                       json_string (comm_title ? comm_title : "Unknown"));
  json_object_set_new (obj, "commission", json_integer (comm_id));
  json_object_set_new (obj, "alignment", json_integer (align));
  json_object_set_new (obj, "experience", json_integer (exp));
  json_t *band = json_object ();


  json_object_set_new (band, "code",
                       json_string (band_code ? band_code : "UNKNOWN"));
  json_object_set_new (band, "name",
                       json_string (band_name ? band_name : "Unknown"));
  json_object_set_new (band, "is_good", json_boolean (is_good));
  json_object_set_new (band, "is_evil", json_boolean (is_evil));
  json_object_set_new (band, "can_buy_iss", json_boolean (can_iss));
  json_object_set_new (band, "can_rob_ports", json_boolean (can_rob));
  json_object_set_new (obj, "alignment_band", band);
  if (band_code)
    {
      free (band_code);
    }
  if (band_name)
    {
      free (band_name);
    }
  if (comm_title)
    {
      free (comm_title);
    }
  *out_json = obj;
  return SQLITE_OK;
}


int
h_send_message_to_player (int player_id, int sender_id, const char *subject,
                          const char *message)
{
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  if (sqlite3_prepare_v2 (db,
                          "INSERT INTO mail (sender_id, recipient_id, subject, body) VALUES (?, ?, ?, ?)",
                          -1,
                          &st,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, sender_id);
      sqlite3_bind_int (st, 2, player_id);
      sqlite3_bind_text (st, 3, subject, -1, SQLITE_TRANSIENT);
      sqlite3_bind_text (st, 4, message, -1, SQLITE_TRANSIENT);
      sqlite3_step (st);
      sqlite3_finalize (st);
      return 0;
    }
  return 1;
}


int
spawn_starter_ship (sqlite3 *db, int player_id, int sector_id)
{
  sqlite3_stmt *st = NULL;
  int ship_type_id = 0, holds = 0, fighters = 0, shields = 0;
  if (sqlite3_prepare_v2 (db,
                          "SELECT id, initialholds, maxfighters, maxshields FROM shiptypes WHERE name='Scout Marauder'",
                          -1,
                          &st,
                          NULL) != SQLITE_OK)
    {
      return SQLITE_ERROR;
    }
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      ship_type_id = sqlite3_column_int (st, 0);
      holds = sqlite3_column_int (st, 1);
      fighters = sqlite3_column_int (st, 2);
      shields = sqlite3_column_int (st, 3);
    }
  sqlite3_finalize (st);
  if (ship_type_id == 0)
    {
      return SQLITE_NOTFOUND;
    }
  /* Insert Ship */
  const char *sql_ins =
    "INSERT INTO ships (name, type_id, holds, fighters, shields, sector) VALUES ('Starter Ship', ?, ?, ?, ?, ?)";


  if (sqlite3_prepare_v2 (db, sql_ins, -1, &st, NULL) != SQLITE_OK)
    {
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (st, 1, ship_type_id);
  sqlite3_bind_int (st, 2, holds);
  sqlite3_bind_int (st, 3, fighters);
  sqlite3_bind_int (st, 4, shields);
  sqlite3_bind_int (st, 5, sector_id);
  if (sqlite3_step (st) != SQLITE_DONE)
    {
      sqlite3_finalize (st);
      return SQLITE_ERROR;
    }
  int ship_id = sqlite3_last_insert_rowid (db);


  sqlite3_finalize (st);
  /* Ownership */
  if (sqlite3_prepare_v2 (db,
                          "INSERT INTO ship_ownership (ship_id, player_id, role_id, is_primary) VALUES (?, ?, 1, 1)",
                          -1,
                          &st,
                          NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, ship_id);
      sqlite3_bind_int (st, 2, player_id);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }
  /* Update Player */
  if (sqlite3_prepare_v2 (db,
                          "UPDATE players SET ship=?, sector=? WHERE id=?",
                          -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, ship_id);
      sqlite3_bind_int (st, 2, sector_id);
      sqlite3_bind_int (st, 3, player_id);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }
  /* Update Podded */
  sqlite3_exec (db,
                "UPDATE podded_status SET status='alive' WHERE player_id=?",
                NULL, NULL, NULL);
  return SQLITE_OK;
}


int
destroy_ship_and_handle_side_effects (client_ctx_t *ctx, int player_id)
{
  (void) ctx;
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      return -1;
    }
  int ship_id = h_get_active_ship_id (db, player_id);


  if (ship_id <= 0)
    {
      return 0;
    }
  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
  sqlite3_stmt *st = NULL;


  sqlite3_prepare_v2 (db, "DELETE FROM ships WHERE id=?", -1, &st, NULL);
  sqlite3_bind_int (st, 1, ship_id);
  sqlite3_step (st);
  sqlite3_finalize (st);
  sqlite3_prepare_v2 (db,
                      "UPDATE players SET ship=NULL WHERE id=?",
                      -1, &st, NULL);
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_step (st);
  sqlite3_finalize (st);
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
  h_send_message_to_player (player_id,
                            0, "Ship Destroyed", "Your ship was destroyed.");
  return 0;
}


/* ==================================================================== */


/* COMMAND HANDLERS                                                     */


/* ==================================================================== */
static int
h_set_prefs (client_ctx_t *ctx, json_t *prefs)
{
  if (!json_is_object (prefs))
    {
      return -1;
    }
  void *it = json_object_iter (prefs);


  while (it)
    {
      const char *key = json_object_iter_key (it);
      json_t *val = json_object_iter_value (it);
      char buf[512] = { 0 };
      const char *sval = "";


      if (!is_valid_key (key, 64))
        {
          it = json_object_iter_next (prefs, it);
          continue;
        }

      if (json_is_string (val))
        {
          sval = json_string_value (val);
        }
      else if (json_is_integer (val))
        {
          snprintf (buf, sizeof (buf), "%lld",
                    (long long) json_integer_value (val));
          sval = buf;
        }
      else if (json_is_boolean (val))
        {
          sval = json_is_true (val) ? "1" : "0";
        }
      else
        {
          it = json_object_iter_next (prefs, it);
          continue;
        }

      db_prefs_set_one (ctx->player_id, key, PT_STRING, sval);
      it = json_object_iter_next (prefs, it);
    }
  return 0;
}


static int
h_set_bookmarks (client_ctx_t *ctx, json_t *list)
{
  if (!json_is_array (list))
    {
      return -1;
    }

  /* 1. Remove all existing */
  json_t *curr = bookmarks_as_array (ctx->player_id);
  size_t idx;
  json_t *val;


  json_array_foreach (curr, idx, val)
  {
    const char *name = json_string_value (json_object_get (val, "name"));
    if (name)
      {
        db_bookmark_remove (ctx->player_id, name);
      }
  }
  json_decref (curr);

  /* 2. Add new */
  json_array_foreach (list, idx, val)
  {
    const char *name = json_string_value (json_object_get (val, "name"));
    int sector_id = json_integer_value (json_object_get (val, "sector_id"));
    if (name && sector_id > 0)
      {
        db_bookmark_upsert (ctx->player_id, name, sector_id);
      }
  }
  return 0;
}


static int
h_set_avoids (client_ctx_t *ctx, json_t *list)
{
  if (!json_is_array (list))
    {
      return -1;
    }

  /* 1. Remove all */
  json_t *curr = avoid_as_array (ctx->player_id);
  size_t idx;
  json_t *val;


  json_array_foreach (curr, idx, val)
  {
    if (json_is_integer (val))
      {
        db_avoid_remove (ctx->player_id, json_integer_value (val));
      }
  }
  json_decref (curr);

  /* 2. Add new */
  json_array_foreach (list, idx, val)
  {
    if (json_is_integer (val))
      {
        db_avoid_add (ctx->player_id, json_integer_value (val));
      }
  }
  return 0;
}


static int
h_set_subscriptions (client_ctx_t *ctx, json_t *list)
{
  if (!json_is_array (list))
    {
      return -1;
    }

  /* 1. Disable all existing */
  json_t *curr = subscriptions_as_array (ctx->player_id);
  size_t idx;
  json_t *val;


  json_array_foreach (curr, idx, val)
  {
    const char *topic = json_string_value (json_object_get (val, "topic"));
    if (topic)
      {
        db_subscribe_disable (ctx->player_id, topic, NULL);
      }
  }
  json_decref (curr);

  /* 2. Enable/Add new */
  json_array_foreach (list, idx, val)
  {
    if (json_is_string (val))
      {
        db_subscribe_upsert (ctx->player_id, json_string_value (val), NULL,
                             0);
      }
    else if (json_is_object (val))
      {
        const char *topic =
          json_string_value (json_object_get (val, "topic"));


        if (topic)
          {
            db_subscribe_upsert (ctx->player_id, topic, NULL, 0);
          }
      }
  }
  return 0;
}


int
cmd_player_set_settings (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA,
                           "data must be object");
      return 0;
    }

  json_t *prefs = json_object_get (data, "prefs");


  if (prefs)
    {
      h_set_prefs (ctx, prefs);
    }

  json_t *bookmarks = json_object_get (data, "bookmarks");


  if (bookmarks)
    {
      h_set_bookmarks (ctx, bookmarks);
    }

  json_t *avoid = json_object_get (data, "avoid");


  if (avoid)
    {
      h_set_avoids (ctx, avoid);
    }

  json_t *subs = json_object_get (data, "subscriptions");


  if (subs)
    {
      h_set_subscriptions (ctx, subs);
    }

  json_t *topics = json_object_get (data, "topics");


  if (topics)
    {
      h_set_subscriptions (ctx, topics);
    }

  return cmd_player_get_settings (ctx, root);
}


static json_t *
get_online_players_json_array (int offset, int limit,
                               const json_t *fields_array)
{
  json_t *players_list = json_array ();
  int *online_player_ids = NULL;
  int total_online = 0;
  pthread_mutex_lock (&g_clients_mu);
  client_node_t *n = g_clients;


  while (n)
    {
      if (n->ctx && n->ctx->player_id > 0)
        {
          total_online++;
        }
      n = n->next;
    }
  if (total_online > 0)
    {
      online_player_ids = (int *) malloc (sizeof (int) * total_online);
      if (online_player_ids)
        {
          int i = 0;


          n = g_clients;
          while (n && i < total_online)
            {
              if (n->ctx && n->ctx->player_id > 0)
                {
                  online_player_ids[i++] = n->ctx->player_id;
                }
              n = n->next;
            }
        }
    }
  pthread_mutex_unlock (&g_clients_mu);
  int start_idx = offset;


  if (start_idx < 0)
    {
      start_idx = 0;
    }
  if (start_idx > total_online)
    {
      start_idx = total_online;
    }
  int end_idx = start_idx + limit;


  if (end_idx > total_online)
    {
      end_idx = total_online;
    }
  json_t *effective_fields = (json_t *) fields_array;
  bool using_defaults = false;


  if (!effective_fields || json_array_size (effective_fields) == 0)
    {
      effective_fields = json_array ();
      for (int f = 0; DEFAULT_PLAYER_FIELDS[f] != NULL; f++)
        {
          json_array_append_new (effective_fields,
                                 json_string (DEFAULT_PLAYER_FIELDS[f]));
        }
      using_defaults = true;
    }
  for (int i = start_idx; i < end_idx; i++)
    {
      if (!online_player_ids)
        {
          break;
        }
      int player_id = online_player_ids[i];
      json_t *info = NULL;


      if (db_player_info_selected_fields (player_id, effective_fields,
                                          &info) == SQLITE_OK && info)
        {
          json_array_append_new (players_list, info);
        }
    }
  if (online_player_ids)
    {
      free (online_player_ids);
    }
  if (using_defaults)
    {
      json_decref (effective_fields);
    }
  json_t *response = json_object ();


  json_object_set_new (response, "total_online", json_integer (total_online));
  json_object_set_new (response, "returned_count",
                       json_integer (json_array_size (players_list)));
  json_object_set_new (response, "offset", json_integer (offset));
  json_object_set_new (response, "limit", json_integer (limit));
  json_object_set_new (response, "players", players_list);
  return response;
}


/* src/server_players.c */
int
cmd_player_my_info (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated", NULL);
      return 0;
    }
  sqlite3 *db = db_get_handle ();


  if (!db)
    {
      send_response_error (ctx,
                           root,
                           ERR_CITADEL_REQUIRED, "Database unavailable");
      return 0;
    }
  /* 1. Fetch Basic Player Data directly (Bypass db_player_info_json) */
  sqlite3_stmt *st = NULL;
  const char *sql =
    " SELECT  p.name, p.credits, t.turns_remaining, p.sector, p.ship, p.alignment, p.experience, cm.corp_id FROM players AS p JOIN turns AS t ON t.player = p.id  LEFT JOIN corp_members AS cm ON cm.player_id = p.id  WHERE p.id = ?1";


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_CITADEL_REQUIRED,
                           "DB Error preparing player info");
      return 0;
    }
  sqlite3_bind_int (st, 1, ctx->player_id);
  int rc = sqlite3_step (st);


  if (rc != SQLITE_ROW)
    {
      sqlite3_finalize (st);
      send_response_error (ctx, root, REF_AUTOPILOT_RUNNING,
                           "Player not found");
      return 0;
    }
  /* Extract columns */
  const char *name = (const char *) sqlite3_column_text (st, 0);
  long long credits = sqlite3_column_int64 (st, 1);
  int turns = sqlite3_column_int (st, 2);
  int sector = sqlite3_column_int (st, 3);
  int ship_id = sqlite3_column_int (st, 4);
  int align = sqlite3_column_int (st, 5);
  long long exp = sqlite3_column_int64 (st, 6);
  int corp_id = sqlite3_column_int (st, 7);


  /* 2. Build JSON Response */
  json_t *player_obj = json_object ();


  json_object_set_new (player_obj, "id", json_integer (ctx->player_id));
  json_object_set_new (player_obj, "username",
                       json_string (name ? name : "Unknown"));
  sqlite3_finalize (st);        // Moved here to keep 'name' valid
  /* Format credits as string "1000.00" for compatibility */
  char credits_str[64];


  snprintf (credits_str, sizeof (credits_str), "%lld.00", credits);
  json_object_set_new (player_obj, "credits", json_string (credits_str));
  json_object_set_new (player_obj, "turns_remaining", json_integer (turns));
  json_object_set_new (player_obj, "sector", json_integer (sector));
  json_object_set_new (player_obj, "ship_id", json_integer (ship_id));
  json_object_set_new (player_obj, "corp_id", json_integer (corp_id));
  json_object_set_new (player_obj, "alignment", json_integer (align));
  json_object_set_new (player_obj, "experience", json_integer (exp));
  /* 3. Add Title Info (Reuse existing helper) */
  json_t *title_info = NULL;


  if (h_player_build_title_payload (db, ctx->player_id,
                                    &title_info) == SQLITE_OK && title_info)
    {
      json_object_set_new (player_obj, "title_info", title_info);
    }
  /* 4. Wrap in envelope */
  json_t *pinfo = json_object ();


  json_object_set_new (pinfo, "player", player_obj);
  send_response_ok_take (ctx, root, "player.info", &pinfo);
  return 0;
}


int
cmd_player_list_online (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated", NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      data = json_object ();
    }
  int offset = 0;
  json_t *j_offset = json_object_get (data, "offset");


  if (json_is_integer (j_offset))
    {
      offset = json_integer_value (j_offset);
    }
  int limit = 100;
  json_t *j_limit = json_object_get (data, "limit");


  if (json_is_integer (j_limit))
    {
      limit = json_integer_value (j_limit);
    }
  if (limit > 1000)
    {
      limit = 1000;
    }
  json_t *fields = json_object_get (data, "fields");


  if (!json_is_array (fields))
    {
      fields = NULL;
    }
  json_t *resp = get_online_players_json_array (offset, limit, fields);


  if (!resp)
    {
      send_response_error (ctx, root, ERR_UNKNOWN,
                           "Failed to retrieve list.");
      return 0;
    }
  send_response_ok_take (ctx, root, "player.list_online.result", &resp);
  return 0;
}


int
cmd_player_rankings (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Authentication required.", NULL);
      return 0;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");
  const char *order_by = "experience";
  int limit = 10;
  int offset = 0;


  if (json_is_object (data))
    {
      json_t *by = json_object_get (data, "by");


      if (json_is_string (by))
        {
          const char *req = json_string_value (by);


          if (strcmp (req, "experience") == 0
              || strcmp (req, "net_worth") == 0)
            {
              order_by = req;
            }
          else
            {
              send_response_refused_steal (ctx,
                                           root,
                                           ERR_INVALID_ARG,
                                           "Invalid criterion", NULL);
              return 0;
            }
        }
      json_t *j_lim = json_object_get (data, "limit");


      if (json_is_integer (j_lim))
        {
          limit = json_integer_value (j_lim);
        }
      if (limit > 100)
        {
          limit = 100;
        }
      json_t *j_off = json_object_get (data, "offset");


      if (json_is_integer (j_off))
        {
          offset = json_integer_value (j_off);
        }
    }
  json_t *rankings = json_array ();
  sqlite3_stmt *st = NULL;
  char sql[512];


  if (strcmp (order_by, "experience") == 0)
    {
      snprintf (sql,
                sizeof (sql),
                "SELECT id, name, alignment, experience FROM players ORDER BY experience DESC LIMIT %d OFFSET %d;",
                limit,
                offset);
    }
  else
    {
      snprintf (sql,
                sizeof (sql),
                "SELECT p.id, p.name, p.alignment, p.experience, COALESCE(ba.balance, 0) as net_worth "
                "FROM players p LEFT JOIN bank_accounts ba ON p.id = ba.owner_id AND ba.owner_type = 'player' "
                "ORDER BY net_worth DESC LIMIT %d OFFSET %d;",
                limit,
                offset);
    }
  int rc = sqlite3_prepare_v2 (db, sql, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB, "Database error retrieving rankings.");
      return 0;
    }
  int rank_num = offset + 1;


  while (sqlite3_step (st) == SQLITE_ROW)
    {
      int pid = sqlite3_column_int (st, 0);
      const char *name = (const char *) sqlite3_column_text (st, 1);
      int align = sqlite3_column_int (st, 2);
      long long exp = sqlite3_column_int64 (st, 3);
      json_t *entry = json_object ();


      json_object_set_new (entry, "rank", json_integer (rank_num++));
      json_object_set_new (entry, "id", json_integer (pid));
      /* sqlite: column_text() pointer invalid after finalize/reset/step */
      json_object_set_new (entry, "username", json_string (name ? name : ""));
      json_object_set_new (entry, "alignment", json_integer (align));
      json_object_set_new (entry, "experience", json_integer (exp));
      if (strcmp (order_by, "net_worth") == 0)
        {
          long long nw = sqlite3_column_int64 (st, 4);
          char buf[32];


          snprintf (buf, sizeof (buf), "%lld.00", nw);


          json_object_set_new (entry, "net_worth", json_string (buf));
        }
      json_t *title = NULL;


      if (h_player_build_title_payload (db, pid, &title) == SQLITE_OK
          && title)
        {
          json_object_set_new (entry, "title_info", title);
        }
      json_array_append_new (rankings, entry);
    }
  sqlite3_finalize (st);
  json_t *resp = json_object ();


  json_object_set_new (resp, "rankings", rankings);
  send_response_ok_take (ctx, root, "player.rankings", &resp);
  return 0;
}


/* Settings Command Implementation */
int
cmd_player_get_settings (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required");
      return 0;
    }
  json_t *prefs = prefs_as_array (ctx->player_id);
  json_t *bm = bookmarks_as_array (ctx->player_id);
  json_t *avoid = avoid_as_array (ctx->player_id);
  json_t *subs = subscriptions_as_array (ctx->player_id);
  json_t *data = json_object ();


  json_object_set (data, "prefs", prefs);
  json_object_set (data, "bookmarks", bm);
  json_object_set (data, "avoid", avoid);
  json_object_set (data, "subscriptions", subs);


  send_response_ok_take (ctx, root, "player.settings_v1", &data);
  return 0;
}


int
cmd_player_get_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated", NULL);
      return 0;
    }
  json_t *prefs = prefs_as_array (ctx->player_id);
  json_t *data = json_object ();


  json_object_set_new (data, "prefs", prefs);
  send_response_ok_take (ctx, root, "player.prefs_v1", &data);
  return 0;
}


int
cmd_player_set_topics (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *topics = json_object_get (data, "topics");


  if (!topics)
    {
      topics = json_object_get (data, "subscriptions");
    }

  if (h_set_subscriptions (ctx, topics) != 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid topics list");
      return 0;
    }
  return cmd_player_get_topics (ctx, root);
}


int
cmd_player_get_topics (client_ctx_t *ctx, json_t *root)
{
  json_t *topics = players_get_subscriptions (ctx);
  json_t *out = json_object ();
  json_object_set_new (out, "topics", topics ? topics : json_array ());
  send_response_ok_take (ctx, root, "player.subscriptions", &out);
  return 0;
}


int
cmd_player_set_bookmarks (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *bm = json_object_get (data, "bookmarks");


  if (h_set_bookmarks (ctx, bm) != 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG,
                           "Invalid bookmarks list");
      return 0;
    }
  return cmd_player_get_bookmarks (ctx, root);
}


int
cmd_player_get_bookmarks (client_ctx_t *ctx, json_t *root)
{
  json_t *bookmarks = players_list_bookmarks (ctx);
  json_t *out = json_object ();
  json_object_set_new (out, "bookmarks",
                       bookmarks ? bookmarks : json_array ());
  send_response_ok_take (ctx, root, "player.bookmarks", &out);
  return 0;
}


int
cmd_player_set_avoids (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Auth required");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *av = json_object_get (data, "avoid");


  if (h_set_avoids (ctx, av) != 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid avoid list");
      return 0;
    }
  return cmd_player_get_avoids (ctx, root);
}


int
cmd_player_get_avoids (client_ctx_t *ctx, json_t *root)
{
  json_t *avoid = players_list_avoid (ctx);
  json_t *out = json_object ();
  json_object_set_new (out, "avoid", avoid ? avoid : json_array ());
  send_response_ok_take (ctx, root, "avoids", &out);
  return 0;
}


int
cmd_player_get_notes (client_ctx_t *ctx, json_t *root)
{
  json_t *notes = players_list_notes (ctx, root);
  json_t *out = json_object ();
  json_object_set_new (out, "notes", notes ? notes : json_array ());
  send_response_ok_take (ctx, root, "player.notes", &out);
  return 0;
}


int
cmd_player_set_prefs (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "auth required");
      return -1;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA,
                           "data must be object");
      return -1;
    }
  json_t *patch = json_object_get (data, "patch");
  json_t *prefs = json_is_object (patch) ? patch : data;
  void *it = json_object_iter (prefs);


  while (it)
    {
      const char *key = json_object_iter_key (it);
      json_t *val = json_object_iter_value (it);


      if (!is_valid_key (key, 64))
        {
          send_response_error (ctx, root, ERR_INVALID_ARG, "invalid key");
          return -1;
        }
      const char *sval = "";
      char buf[512] = { 0 };


      if (json_is_string (val))
        {
          sval = json_string_value (val);
          if (!is_ascii_printable (sval) || strlen (sval) > 256)
            {
              send_response_error (ctx, root, ERR_INVALID_ARG,
                                   "string invalid");
              return -1;
            }
        }
      else if (json_is_integer (val))
        {
          snprintf (buf,
                    sizeof (buf),
                    "%lld", (long long) json_integer_value (val));
          sval = buf;
        }
      else if (json_is_boolean (val))
        {
          sval = json_is_true (val) ? "1" : "0";
        }
      else
        {
          send_response_error (ctx, root, ERR_INVALID_ARG,
                               "unsupported type");
          return -1;
        }
      if (db_prefs_set_one (ctx->player_id, key, PT_STRING, sval) != 0)
        {
          send_response_error (ctx, root, ERR_UNKNOWN, "db error");
          return -1;
        }
      it = json_object_iter_next (prefs, it);
    }
  json_t *resp = json_object ();


  json_object_set_new (resp, "ok", json_boolean (1));


  send_response_ok_take (ctx, root, "player.prefs.updated", &resp);
  return 0;
}


void
cmd_nav_bookmark_add (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "auth required");
      return;
    }
  json_t *data = json_object_get (root, "data");
  const char *name = json_string_value (json_object_get (data, "name"));
  int sector_id = json_integer_value (json_object_get (data, "sector_id"));


  if (!name || !is_ascii_printable (name) || !len_leq (name, MAX_BM_NAME))
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "invalid name");
      return;
    }
  if (sector_id <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "invalid sector");
      return;
    }
  int rc = db_bookmark_upsert (ctx->player_id, name, sector_id);


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_UNKNOWN, "db error");
      return;
    }
  json_t *resp = json_object ();


  json_object_set_new (resp, "name", json_string (name));
  json_object_set_new (resp, "sector_id", json_integer (sector_id));


  send_response_ok_take (ctx, root, "nav.bookmark.added", &resp);
}


void
cmd_nav_bookmark_remove (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      return;
    }
  const char *name =
    json_string_value (json_object_get (json_object_get (root,
                                                         "data"),
                                        "name"));


  if (db_bookmark_remove (ctx->player_id, name) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "not found");
      return;
    }
  json_t *resp = json_object ();


  json_object_set_new (resp, "name", json_string (name));


  send_response_ok_take (ctx, root, "nav.bookmark.removed", &resp);
}


void
cmd_nav_bookmark_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      return;
    }
  json_t *items = bookmarks_as_array (ctx->player_id);
  json_t *resp = json_object ();


  json_object_set (resp, "items", items);


  send_response_ok_take (ctx, root, "nav.bookmark.list", &resp);
}


void
cmd_nav_avoid_add (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "auth required");
      return;
    }
  json_t *data = json_object_get (root, "data");
  int sector_id = json_integer_value (json_object_get (data, "sector_id"));


  if (sector_id <= 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "invalid sector");
      return;
    }
  if (db_avoid_add (ctx->player_id, sector_id) != 0)
    {
      send_response_error (ctx, root, ERR_UNKNOWN, "db error");
      return;
    }
  json_t *resp = json_object ();


  json_object_set_new (resp, "sector_id", json_integer (sector_id));


  send_response_ok_take (ctx, root, "nav.avoid.added", &resp);
}


void
cmd_nav_avoid_remove (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      return;
    }
  json_t *data = json_object_get (root, "data");
  int sector_id = json_integer_value (json_object_get (data, "sector_id"));


  if (db_avoid_remove (ctx->player_id, sector_id) != 0)
    {
      send_response_error (ctx, root, ERR_NOT_FOUND, "not found");
      return;
    }
  json_t *resp = json_object ();


  json_object_set_new (resp, "sector_id", json_integer (sector_id));


  send_response_ok_take (ctx, root, "nav.avoid.removed", &resp);
}


void
cmd_nav_avoid_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      return;
    }
  json_t *items = players_list_avoid (ctx);
  json_t *resp = json_object ();


  json_object_set (resp, "items", items);


  send_response_ok_take (ctx, root, "nav.avoid.list", &resp);
}


int
cmd_player_set_trade_account_preference (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Auth required", NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *pref = json_object_get (data, "prefer_bank");


  if (!json_is_boolean (pref))
    {
      send_response_error (ctx,
                           root, ERR_INVALID_ARG, "prefer_bank must be bool");
      return 0;
    }
  int val = json_is_true (pref) ? 1 : 0;


  if (db_prefs_set_one (ctx->player_id,
                        "trade.prefer_bank", PT_BOOL, val ? "1" : "0") != 0)
    {
      send_response_error (ctx, root, ERR_UNKNOWN, "db error");
      return 0;
    }
  json_t *resp = json_object ();


  json_object_set_new (resp, "ok", json_boolean (1));


  send_response_ok_take (ctx, root, "player.prefs.updated", &resp);
  return 0;
}


int
cmd_insurance_policies_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Authentication required", NULL);
      return 0;
    }

  sqlite3 *db = db_get_handle ();
  json_t *policies_array = json_array ();
  sqlite3_stmt *st = NULL;

  const char *sql =
    "SELECT id, subject_type, subject_id, start_ts, expiry_ts, premium, payout FROM insurance_policies WHERE holder_type = 'player' AND holder_id = ?1;";


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED,
                           "Database error preparing query.");
      return 0;
    }
  sqlite3_bind_int (st, 1, ctx->player_id);

  while (sqlite3_step (st) == SQLITE_ROW)

    {
      json_t *policy = json_object ();

      int id = sqlite3_column_int (st, 0);

      const char *tmp_subject_type =
        (const char *) sqlite3_column_text (st, 1);

      const char *tmp_start_ts = (const char *) sqlite3_column_text (st, 3);

      const char *tmp_expiry_ts = (const char *) sqlite3_column_text (st, 4);


      /* sqlite: column_text() pointer invalid after finalize/reset/step */

      json_object_set_new (policy, "id", json_integer (id));

      json_object_set_new (policy,
                           "subject_type",
                           json_string (tmp_subject_type ? tmp_subject_type :
                                        ""));

      json_object_set_new (policy, "subject_id",
                           json_integer (sqlite3_column_int (st, 2)));

      json_object_set_new (policy, "start_ts",
                           json_string (tmp_start_ts ? tmp_start_ts : ""));

      json_object_set_new (policy, "expiry_ts",
                           json_string (tmp_expiry_ts ? tmp_expiry_ts : ""));

      json_object_set_new (policy, "premium",
                           json_integer (sqlite3_column_int64 (st, 5)));

      json_object_set_new (policy, "payout",
                           json_integer (sqlite3_column_int64 (st, 6)));

      json_array_append_new (policies_array, policy);
    }
  sqlite3_finalize (st);

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "policies", policies_array);
  send_response_ok_take (ctx, root, "insurance.policies.list",
                         &response_data);
  return 0;
}


int
cmd_insurance_policies_buy (client_ctx_t *ctx,
                            json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Authentication required", NULL);
      return 0;
    }

  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data payload.");
      return 0;
    }

  const char *subject_type = json_string_value (json_object_get (data,
                                                                 "subject_type"));
  int subject_id = json_integer_value (json_object_get (data, "subject_id"));
  int duration = json_integer_value (json_object_get (data, "duration"));       // This comes from JSON
  long long premium_to_pay = json_integer_value (json_object_get (data,
                                                                  "premium"));


  if (!subject_type || subject_id <= 0 || duration <= 0
      || premium_to_pay <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid subject_type, subject_id, duration, or premium.");
      return 0;
    }

  // MVP: Hardcode payout for simplicity, or calculate based on premium
  long long payout_amount = premium_to_pay * 10;        // Simple 10x payout

  // Check player credits
  long long player_credits = 0;


  if (h_get_player_petty_cash (db, ctx->player_id,
                               &player_credits) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED,
                           "Failed to retrieve player credits.");
      return 0;
    }

  if (player_credits < premium_to_pay)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient credits to buy policy.",
                                   NULL);
      return 0;
    }

  // Deduct premium
  if (h_deduct_player_petty_cash_unlocked (db,
                                           ctx->player_id,
                                           premium_to_pay, NULL) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Failed to deduct premium from player credits.");
      return 0;
    }

  // Insert policy row
  sqlite3_stmt *st = NULL;
  const char *sql =
    "INSERT INTO insurance_policies (holder_type, holder_id, subject_type, subject_id, start_ts, expiry_ts, premium, payout, active) VALUES ('player', ?1, ?2, ?3, strftime('%Y-%m-%dT%H:%M:%SZ','now'), strftime('%Y-%m-%dT%H:%M:%SZ','now', '+' || ?4 || ' days'), ?5, ?6, 1);";


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED,
                           "Database error preparing policy insert.");
      // Refund credits
      h_add_player_petty_cash_unlocked (db, ctx->player_id, premium_to_pay,
                                        NULL);
      return 0;
    }

  sqlite3_bind_int (st, 1, ctx->player_id);
  sqlite3_bind_text (st, 2, subject_type, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 3, subject_id);
  sqlite3_bind_int (st, 4, duration);   // binds to ?4, used in strftime
  sqlite3_bind_int64 (st, 5, premium_to_pay);
  sqlite3_bind_int64 (st, 6, payout_amount);

  if (sqlite3_step (st) != SQLITE_DONE)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB, "Failed to insert insurance policy.");
      // Refund credits
      h_add_player_petty_cash_unlocked (db, ctx->player_id, premium_to_pay,
                                        NULL);
      sqlite3_finalize (st);
      return 0;
    }
  sqlite3_finalize (st);

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string
                         ("Insurance policy purchased successfully."));
  json_object_set_new (response_data, "policy_id",
                       json_integer (sqlite3_last_insert_rowid (db)));
  json_object_set_new (response_data, "premium",
                       json_integer (premium_to_pay));
  json_object_set_new (response_data, "payout", json_integer (payout_amount));
  send_response_ok_take (ctx, root, "insurance.policies.buy", &response_data);
  return 0;
}


int
cmd_insurance_claim_file (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Authentication required", NULL);
      return 0;
    }

  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data payload.");
      return 0;
    }

  int policy_id = json_integer_value (json_object_get (data, "policy_id"));
  const char *incident_description = json_string_value (json_object_get (data,
                                                                         "incident_description"));


  if (policy_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Missing or invalid policy_id.");
      return 0;
    }

  // Validate policy ownership and existence
  sqlite3_stmt *st_check = NULL;
  const char *sql_check_policy =
    "SELECT id FROM insurance_policies WHERE id = ?1 AND holder_type = 'player' AND holder_id = ?2 AND active = 1 AND expiry_ts > strftime('%Y-%m-%dT%H:%M:%SZ','now');";


  if (sqlite3_prepare_v2 (db, sql_check_policy, -1, &st_check,
                          NULL) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED,
                           "Database error checking policy.");
      return 0;
    }
  sqlite3_bind_int (st_check, 1, policy_id);
  sqlite3_bind_int (st_check, 2, ctx->player_id);
  if (sqlite3_step (st_check) != SQLITE_ROW)
    {
      sqlite3_finalize (st_check);
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_FOUND,
                                   "Active policy not found or does not belong to player.",
                                   NULL);
      return 0;
    }
  sqlite3_finalize (st_check);

  // Insert claim row
  sqlite3_stmt *st = NULL;
  // Use event_id and ts from schema. Use incident_description for event_id.
  const char *sql =
    "INSERT INTO insurance_claims (policy_id, event_id, amount, status, ts) VALUES (?1, ?2, 0, 'open', strftime('%Y-%m-%dT%H:%M:%SZ','now'));";


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED,
                           "Database error preparing claim insert.");
      return 0;
    }

  sqlite3_bind_int (st, 1, policy_id);
  sqlite3_bind_text (st, 2, incident_description, -1, SQLITE_STATIC);   // Use incident_description for event_id

  if (sqlite3_step (st) != SQLITE_DONE)
    {
      send_response_error (ctx,
                           root, ERR_DB, "Failed to insert insurance claim.");
      sqlite3_finalize (st);
      return 0;
    }
  sqlite3_finalize (st);

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Insurance claim filed successfully."));
  json_object_set_new (response_data, "claim_id",
                       json_integer (sqlite3_last_insert_rowid (db)));
  json_object_set_new (response_data, "status", json_string ("filed")); // Use 'filed' as per MVP semantics
  send_response_ok_take (ctx, root, "insurance.claim.file", &response_data);
  return 0;
}

