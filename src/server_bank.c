/* src/server_bank.c */
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
#include "game_db.h"
#include "db/sql_driver.h"
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
#include "db/db_api.h"
#include "db/sql_driver.h"


/* ==================================================================== */


/* STATIC HELPER DEFINITIONS                                            */


/* ==================================================================== */


int
h_player_bank_balance_add (db_t *db, int player_id, long long delta,
                           long long *new_balance_out)
{
  if (!db || player_id <= 0 || !new_balance_out)
    {
      return ERR_DB_MISUSE;
    }

  db_error_t err;


  db_error_clear (&err);

  /*
   * 1) Try UPDATE with bounds; if no row and delta>=0, INSERT account.
   * Return: account_id, new_balance
   */
  const char *sql =
    "WITH upd AS ("
    "  UPDATE bank_accounts "
    "  SET balance = balance + {2} "
    "  WHERE owner_type = 'player' AND owner_id = {1} "
    "    AND currency = 'CRD' AND is_active = 1 "
    "    AND (balance + {2}) >= 0 "
    "  RETURNING id, balance"
    "), ins AS ("
    "  INSERT INTO bank_accounts (owner_type, owner_id, currency, balance, is_active) "
    "  SELECT 'player', {1}, 'CRD', {2}, 1 "
    "  WHERE {2} >= 0 AND NOT EXISTS ("
    "    SELECT 1 FROM bank_accounts "
    "    WHERE owner_type = 'player' AND owner_id = {1} "
    "      AND currency = 'CRD' AND is_active = 1"
    "  ) "
    "  RETURNING id, balance"
    ") "
    "SELECT id, balance FROM upd "
    "UNION ALL "
    "SELECT id, balance FROM ins;";

  db_bind_t params[] = {
    db_bind_i32 ((int32_t) player_id),
    db_bind_i64 ((int64_t) delta)
  };

  db_res_t *res = NULL;

  char sql_converted[1024];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, params, 2, &res, &err))
    {
      return err.code ? err.code : ERR_DB_QUERY_FAILED;
    }

  bool have_row = db_res_step (res, &err);
  int64_t account_id = 0;
  int64_t new_bal = 0;


  if (have_row && !err.code)
    {
      account_id = db_res_col_i64 (res, 0, &err);
      new_bal = db_res_col_i64 (res, 1, &err);
    }

  db_res_finalize (res);

  if (err.code)
    {
      return err.code;
    }

  if (!have_row)
    {
      /*
       * Either:
       * - account missing and delta < 0 (cannot create for debit)
       * - account exists but insufficient funds (UPDATE filtered it out)
       *
       * Distinguish with a quick existence check.
       */
      db_error_clear (&err);
      const char *sql_exists =
        "SELECT 1 FROM bank_accounts "
        "WHERE owner_type='player' AND owner_id={1} AND currency='CRD' AND is_active=1 "
        "LIMIT 1;";

      db_bind_t p2[] = { db_bind_i32 ((int32_t) player_id) };


      res = NULL;

      char sql_exists_converted[256];
      sql_build(db, sql_exists, sql_exists_converted, sizeof(sql_exists_converted));

      if (!db_query (db, sql_exists_converted, p2, 1, &res, &err))
        {
          return err.code ? err.code : ERR_DB_QUERY_FAILED;
        }

      bool exists = db_res_step (res, &err);


      db_res_finalize (res);

      if (err.code)
        {
          return err.code;
        }

      return exists ? ERR_DB_CONSTRAINT : ERR_DB_NOT_FOUND;
    }

  /*
   * 2) Insert minimal ledger row into bank_transactions (best-effort but treated as required).
   * Schema: (account_id, tx_type, direction, amount, currency, description, ts, balance_after)
   */
  {
    db_error_clear (&err);

    const char *tx_sql =
      "INSERT INTO bank_transactions "
      "  (account_id, tx_type, direction, amount, currency, description, ts, balance_after) "
      "VALUES "
      "  ({1}, {2}, {3}, {4}, 'CRD', {5}, {6}, {7});";

    const char *direction = (delta >= 0) ? "CREDIT" : "DEBIT";
    int64_t amount_abs = (delta >= 0) ? (int64_t) delta : (int64_t) (-delta);
    int64_t ts = (int64_t) time (NULL);

    /* Keep description stable and generic (you can refine later). */
    const char *desc = "player bank balance adjustment";

    db_bind_t tx_params[] = {
      db_bind_i64 (account_id),
      db_bind_text ("ADJUSTMENT"),
      db_bind_text (direction),
      db_bind_i64 (amount_abs),
      db_bind_text (desc),
      db_bind_i64 (ts),
      db_bind_i64 (new_bal)
    };

    db_res_t *tx_res = NULL;

    char tx_sql_converted[512];
    sql_build(db, tx_sql, tx_sql_converted, sizeof(tx_sql_converted));

    if (!db_query (db, tx_sql_converted,
                   tx_params,
                   sizeof (tx_params) / sizeof (tx_params[0]),
                   &tx_res,
                   &err))
      {
        return err.code ? err.code : ERR_DB_QUERY_FAILED;
      }

    /* INSERT returns no rows; step isn’t needed. */
    db_res_finalize (tx_res);

    if (err.code)
      {
        return err.code;
      }
  }

  *new_balance_out = (long long) new_bal;
  return 0;
}


static int
stmt_to_json_array (db_res_t *st, json_t **out_array, db_error_t *err)
{
  if (!out_array)
    {
      while (db_res_step (st, err))
        {
        }
      return 0;
    }
  json_t *arr = json_array ();


  if (!arr)
    {
      return ERR_NOMEM;
    }

  while (db_res_step (st, err))
    {
      int cols = db_res_col_count (st);
      json_t *obj = json_object ();


      if (!obj)
        {
          db_res_cancel (st); json_decref (arr); return ERR_NOMEM;
        }
      for (int i = 0; i < cols; i++)
        {
          const char *col_name = db_res_col_name (st, i);
          db_col_type_t col_type = db_res_col_type (st, i);
          json_t *val = NULL;


          switch (col_type)
            {
              case DB_TYPE_INTEGER: val = json_integer (db_res_col_i64 (st,
                                                                        i,
                                                                        err));
                break;
              case DB_TYPE_FLOAT: val = json_real (db_res_col_double (st, i,
                                                                      err));
                break;
              case DB_TYPE_TEXT: val = json_string (db_res_col_text (st, i,
                                                                     err) ?:
                                                    ""); break;
              case DB_TYPE_NULL: val = json_null (); break;
              default: val = json_null (); break;
            }
          json_object_set_new (obj, col_name, val ?: json_null ());
        }
      json_array_append_new (arr, obj);
    }
  *out_array = arr;
  return 0;
}


/* ==================================================================== */


/* HELPER FUNCTIONS (Restored)                                         */


/* ==================================================================== */


static int
h_get_bank_balance (db_t *db,
                    const char *owner_type,
                    int owner_id,
                    long long *out_balance)
{
  if (!db || !owner_type || !out_balance)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL;
  db_error_t err;
  const char *sql =
    "SELECT balance FROM bank_accounts WHERE owner_type = {1} AND owner_id = {2} AND is_active = 1;";
  db_bind_t params[] = { db_bind_text (owner_type), db_bind_i32 (owner_id) };

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db, sql_converted, params, 2, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          *out_balance = db_res_col_i64 (res, 0, &err);
          db_res_finalize (res);
          return 0;
        }
      db_res_finalize (res);
      return ERR_NOT_FOUND;
    }
  return err.code;
}


/* Helper: Resolve owner_type/id to a bank_accounts.id */
int
h_get_account_id_unlocked (db_t *db,
                           const char *owner_type,
                           int owner_id,
                           int *account_id_out)
{
  if (!db || !owner_type || !account_id_out)
    {
      return ERR_INVALID_ARG;
    }

  const char *sql =
    "SELECT id FROM bank_accounts WHERE owner_type = {1} AND owner_id = {2}";
  db_bind_t params[] = { db_bind_text (owner_type), db_bind_i32 (owner_id) };
  db_res_t *res = NULL;
  db_error_t err = {0};
  int rc = ERR_NOT_FOUND;

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_query (db, sql_converted, params, 2, &res, &err))
    {
      rc = err.code;
      goto cleanup;
    }

  if (db_res_step (res, &err))
    {
      *account_id_out = db_res_col_i32 (res, 0, &err);
      rc = 0;
    }

cleanup:
  if (res)
    db_res_finalize (res);
  return rc;
}


int
h_get_system_account_id_unlocked (db_t *db, const char *system_owner_type,
                                  int system_owner_id, int *account_id_out)
{
  int rc = h_get_account_id_unlocked (db, system_owner_type, system_owner_id,
                                      account_id_out);
  if (rc == ERR_NOT_FOUND)
    {
      /* Create the system account if it doesn't exist */
      rc =
        h_create_bank_account_unlocked (db, system_owner_type,
                                        system_owner_id, 0, NULL);
      if (rc == 0)
        {
          rc = h_get_account_id_unlocked (db,
                                          system_owner_type,
                                          system_owner_id,
                                          account_id_out);
        }
    }
  return rc;
}


static int
h_create_personal_bank_alert_notice (db_t *db, int player_id, const char *msg)
{
  db_error_t err;
  const char *now_ts = sql_now_timestamptz(db);
  if (!now_ts)
    {
      return -1;  /* Unsupported backend */
    }
  
  char sql[512];
  snprintf(sql, sizeof(sql),
    "INSERT INTO system_notice (created_at, scope, player_id, title, body, severity) VALUES (%s, 'player', {1}, 'Bank Alert', {2}, 'info');",
    now_ts);
  
  db_bind_t params[] = { db_bind_i32 (player_id), db_bind_text (msg) };

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db, sql_converted, params, 2, &err))
    {
      return err.code;
    }
  return 0;
}


int
calculate_fees (db_t *db,
                const char *tx_type,
                long long base_amount,
                const char *owner_type,
                fee_result_t *out)
{
  (void) db;
  (void) tx_type;
  (void) base_amount;
  (void) owner_type;

  if (out)
    {
      out->fee_total = 0;
      out->fee_to_bank = 0;
      out->tax_to_system = 0;
    }
  return 0;
}


static long long
h_get_account_alert_threshold_unlocked (db_t *db,
                                        int account_id,
                                        const char *owner_type)
{
  (void) account_id;
  if (strcmp (owner_type, "player") == 0)
    {
      return (long long) h_get_config_int_unlocked (db,
                                                    "bank_player_alert_threshold",
                                                    1000000);
    }
  return 9223372036854775807LL; // Max for others
}


int
db_get_player_bank_balance (db_t *db, int player_id, long long *balance_out)
{
  return h_get_bank_balance (db, "player", player_id, balance_out);
}


int
db_get_corp_bank_balance (db_t *db, int corp_id, long long *balance_out)
{
  return h_get_bank_balance (db, "corp", corp_id, balance_out);
}


int
db_get_npc_bank_balance (db_t *db, int npc_id, long long *balance_out)
{
  return h_get_bank_balance (db, "npc", npc_id, balance_out);
}


int
db_get_port_bank_balance (db_t *db, int port_id, long long *balance_out)
{
  return h_get_bank_balance (db, "port", port_id, balance_out);
}


int
db_get_planet_bank_balance (db_t *db, int planet_id, long long *balance_out)
{
  return h_get_bank_balance (db, "planet", planet_id, balance_out);
}


int
h_add_credits_unlocked (db_t *db,
                        int account_id,
                        long long amount,
                        const char *tx_type,
                        const char *tx_group_id,
                        long long *out_new_balance)
{
  if (!db || account_id <= 0 || amount < 0 || !tx_type)
    {
      return ERR_DB_MISUSE;
    }
  db_error_t err;
  const char *sql_upd =
    "UPDATE bank_accounts SET balance = balance + {1} WHERE id = {2} RETURNING balance;";
  db_bind_t params[] = { db_bind_i64 (amount), db_bind_i32 (account_id) };
  db_res_t *res = NULL;

  char sql_upd_converted[256];
  sql_build(db, sql_upd, sql_upd_converted, sizeof(sql_upd_converted));

  if (!db_query (db, sql_upd_converted, params, 2, &res, &err))
    {
      return err.code;
    }
  long long new_balance = 0;


  if (db_res_step (res, &err))
    {
      new_balance = db_res_col_i64 (res, 0, &err);
      /* Sanity check: balance should never be negative after adding credits */
      if (new_balance < 0)
        {
          db_res_finalize (res);
          LOGE("Overflow detected: balance became negative after credit");
          return ERR_DB_QUERY_FAILED;
        }
    }
  else
    {
      db_res_finalize (res);
      return ERR_NOT_FOUND;
    }
  db_res_finalize (res);


  const char *now_epoch = sql_epoch_now(db);
  if (!now_epoch)
    {
      return ERR_DB_QUERY_FAILED;  /* Unsupported backend */
    }
  
  char sql_tx[512];
  snprintf(sql_tx, sizeof(sql_tx),
    "INSERT INTO bank_transactions (account_id, tx_type, direction, amount, currency, balance_after, tx_group_id, ts) "
    "VALUES ({1}, {2}, 'CREDIT', {3}, 'CRD', {4}, {5}, %s);",
    now_epoch);
  
  db_bind_t tx_params[] = {
    db_bind_i32 (account_id), db_bind_text (tx_type), db_bind_i64 (amount),
    db_bind_i64 (new_balance), db_bind_text (tx_group_id ? tx_group_id : "")
  };

  char sql_tx_converted[512];
  sql_build(db, sql_tx, sql_tx_converted, sizeof(sql_tx_converted));

  if (!db_exec (db, sql_tx_converted, tx_params, 5, &err))
    {
      return err.code;
    }


  if (out_new_balance)
    {
      *out_new_balance = new_balance;
    }
  return 0;
}


int
h_deduct_credits_unlocked (db_t *db,
                           int account_id,
                           long long amount,
                           const char *tx_type,
                           const char *tx_group_id,
                           long long *out_new_balance)
{
  if (!db || account_id <= 0 || amount < 0 || !tx_type)
    {
      return ERR_DB_MISUSE;
    }
  db_error_t err;
  const char *sql_upd =
    "UPDATE bank_accounts SET balance = balance - {1} WHERE id = {2} AND balance >= {1} RETURNING balance;";
  db_bind_t params[] = { db_bind_i64 (amount), db_bind_i32 (account_id) };
  db_res_t *res = NULL;

  char sql_upd_converted[256];
  sql_build(db, sql_upd, sql_upd_converted, sizeof(sql_upd_converted));

  if (!db_query (db, sql_upd_converted, params, 2, &res, &err))
    {
      return err.code;
    }
  long long new_balance = 0;


  if (db_res_step (res, &err))
    {
      new_balance = db_res_col_i64 (res, 0, &err);
    }
  else
    {
      db_res_finalize (res);
      return ERR_INSUFFICIENT_FUNDS;
    }
  db_res_finalize (res);


  const char *now_epoch = sql_epoch_now(db);
  if (!now_epoch)
    {
      return ERR_DB_QUERY_FAILED;  /* Unsupported backend */
    }
  
  char sql_tx[512];
  snprintf(sql_tx, sizeof(sql_tx),
    "INSERT INTO bank_transactions (account_id, tx_type, direction, amount, currency, balance_after, tx_group_id, ts) "
    "VALUES ({1}, {2}, 'DEBIT', {3}, 'CRD', {4}, {5}, %s);",
    now_epoch);
  
  db_bind_t tx_params[] = {
    db_bind_i32 (account_id), db_bind_text (tx_type), db_bind_i64 (amount),
    db_bind_i64 (new_balance), db_bind_text (tx_group_id ? tx_group_id : "")
  };

  char sql_tx_converted[512];
  sql_build(db, sql_tx, sql_tx_converted, sizeof(sql_tx_converted));

  if (!db_exec (db, sql_tx_converted, tx_params, 5, &err))
    {
      return err.code;
    }


  if (out_new_balance)
    {
      *out_new_balance = new_balance;
    }
  return 0;
}


int
h_create_bank_account_unlocked (db_t *db,
                                const char *owner_type,
                                int owner_id,
                                long long initial_balance,
                                int *account_id_out)
{
  if (!db || !owner_type)
    {
      return ERR_DB_MISUSE;
    }
  db_error_t err;
  const char *sql =
    "INSERT INTO bank_accounts (owner_type, owner_id, balance, interest_rate_bp, is_active) VALUES ({1}, {2}, {3}, 0, 1) ON CONFLICT DO NOTHING;";
  db_bind_t params[] = { db_bind_text (owner_type), db_bind_i32 (owner_id),
                         db_bind_i64 (initial_balance) };

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db, sql_converted, params, 3, &err))
    {
      return err.code;
    }

  if (account_id_out)
    {
      return h_get_account_id_unlocked (db, owner_type, owner_id,
                                        account_id_out);
    }
  return 0;
}


int
h_add_credits (db_t *db,
               const char *owner_type,
               int owner_id,
               long long amount,
               const char *tx_type,
               const char *tx_group_id,
               long long *out_new_balance)
{
  if (!db || !owner_type || amount < 0 || !tx_type)
    {
      return ERR_DB_MISUSE;
    }
  int account_id = 0;
  int rc = h_get_account_id_unlocked (db, owner_type, owner_id, &account_id);


  if (rc == ERR_NOT_FOUND)
    {
      rc = h_create_bank_account_unlocked (db, owner_type, owner_id, 0, NULL);
      if (rc != 0)
        {
          return rc;
        }
      rc = h_get_account_id_unlocked (db, owner_type, owner_id, &account_id);
    }
  if (rc != 0)
    {
      return rc;
    }


  rc = h_add_credits_unlocked (db,
                               account_id,
                               amount,
                               tx_type,
                               tx_group_id,
                               out_new_balance);
  if (rc == 0 && strcmp (owner_type, "player") == 0)
    {
      long long threshold = h_get_account_alert_threshold_unlocked (db,
                                                                    account_id,
                                                                    owner_type);


      if (amount >= threshold)
        {
          char msg[256];


          snprintf (msg,
                    sizeof (msg),
                    "Deposit of %lld credits received. New balance: %lld.",
                    amount,
                    out_new_balance ? *out_new_balance : 0);
          h_create_personal_bank_alert_notice (db, owner_id, msg);
        }
    }
  return rc;
}


int
h_deduct_credits (db_t *db,
                  const char *owner_type,
                  int owner_id,
                  long long amount,
                  const char *tx_type,
                  const char *tx_group_id,
                  long long *out_new_balance)
{
  if (!db || !owner_type || amount < 0 || !tx_type)
    {
      return ERR_DB_MISUSE;
    }
  int account_id = 0;
  int rc = h_get_account_id_unlocked (db, owner_type, owner_id, &account_id);


  if (rc != 0)
    {
      LOGE ("h_deduct_credits: Account not found for %s %d", owner_type,
            owner_id);
      return rc;
    }


  rc = h_deduct_credits_unlocked (db,
                                  account_id,
                                  amount,
                                  tx_type,
                                  tx_group_id,
                                  out_new_balance);
  if (rc != 0)
    {
      LOGE (
        "h_deduct_credits: Deduction failed for account %d, amount %lld, rc %d",
        account_id,
        amount,
        rc);
    }
  if (rc == 0 && strcmp (owner_type, "player") == 0)
    {
      long long threshold = h_get_account_alert_threshold_unlocked (db,
                                                                    account_id,
                                                                    owner_type);


      if (amount >= threshold)
        {
          char msg[256];


          snprintf (msg,
                    sizeof (msg),
                    "Withdrawal of %lld credits processed. New balance: %lld.",
                    amount,
                    out_new_balance ? *out_new_balance : 0);
          h_create_personal_bank_alert_notice (db, owner_id, msg);
        }
    }
  return rc;
}


int
h_get_credits (db_t *db,
               const char *owner_type,
               int owner_id,
               long long *out_balance)
{
  return h_get_bank_balance (db, owner_type, owner_id, out_balance);
}


int
db_bank_account_exists (db_t *db, const char *owner_type, int owner_id)
{
  if (!db)
    {
      return 0;
    }
  int acc_id = 0;


  if (h_get_account_id_unlocked (db, owner_type, owner_id, &acc_id) == 0)
    {
      return 1;
    }
  return 0;
}


int
db_bank_create_account (db_t *db,
                        const char *owner_type,
                        int owner_id,
                        long long initial_balance,
                        int *account_id_out)
{
  if (!db)
    {
      return -1;
    }
  return h_create_bank_account_unlocked (db,
                                         owner_type,
                                         owner_id,
                                         initial_balance,
                                         account_id_out);
}


int
db_bank_deposit (db_t *db,
                 const char *owner_type,
                 int owner_id,
                 long long amount)
{
  if (!db)
    {
      return -1;
    }
  db_error_t err;


  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      return err.code;
    }
  int rc = h_add_credits (db,
                          owner_type,
                          owner_id,
                          amount,
                          "deposit",
                          NULL,
                          NULL);


  if (rc == 0)
    {
      db_tx_commit (db, &err);
    }
  else
    {
      db_tx_rollback (db, NULL);
    }
  return rc;
}


int
db_bank_withdraw (db_t *db,
                  const char *owner_type,
                  int owner_id,
                  long long amount)
{
  if (!db)
    {
      return -1;
    }
  db_error_t err;


  if (!db_tx_begin (db, DB_TX_DEFAULT, &err))
    {
      return err.code;
    }
  int rc = h_deduct_credits (db,
                             owner_type,
                             owner_id,
                             amount,
                             "withdrawal",
                             NULL,
                             NULL);


  if (rc == 0)
    {
      db_tx_commit (db, &err);
    }
  else
    {
      db_tx_rollback (db, NULL);
    }
  return rc;
}


int
db_bank_get_transactions (db_t *db,
                          const char *owner_type,
                          int owner_id,
                          int limit,
                          const char *filter,
                          long long start,
                          long long end,
                          long long min,
                          long long max,
                          json_t **out)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err; char sql[2048];


  snprintf (sql,
            sizeof(sql),
            "SELECT ts, account_id, tx_type, amount, balance_after, description, tx_group_id FROM bank_transactions "
            "WHERE account_id = (SELECT id FROM bank_accounts WHERE owner_type = {1} AND owner_id = {2}) ");

  db_bind_t params[12]; int idx = 0;


  params[idx++] = db_bind_text (owner_type);
  params[idx++] = db_bind_i32 (owner_id);

  if (filter && *filter)
    {
      char buf[32]; snprintf (buf, sizeof(buf), "AND tx_type = $%d ", idx + 1);


      strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);
      params[idx++] = db_bind_text (filter);
    }

  if (start > 0)
    {
      char buf[32]; snprintf (buf, sizeof(buf), "AND ts >= $%d ", idx + 1);


      strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);
      params[idx++] = db_bind_i64 (start);
    }

  if (end > 0)
    {
      char buf[32]; snprintf (buf, sizeof(buf), "AND ts <= $%d ", idx + 1);


      strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);
      params[idx++] = db_bind_i64 (end);
    }

  if (min > 0)
    {
      char buf[32]; snprintf (buf,
                              sizeof(buf),
                              "AND ABS(amount) >= $%d ",
                              idx + 1);


      strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);
      params[idx++] = db_bind_i64 (min);
    }

  if (max > 0)
    {
      char buf[32]; snprintf (buf,
                              sizeof(buf),
                              "AND ABS(amount) <= $%d ",
                              idx + 1);


      strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);
      params[idx++] = db_bind_i64 (max);
    }

  strncat (sql,
           "ORDER BY ts DESC, id DESC LIMIT ",
           sizeof(sql) - strlen (sql) - 1);
  char buf[16]; snprintf (buf, sizeof(buf), "$%d;", idx + 1);


  strncat (sql, buf, sizeof(sql) - strlen (sql) - 1);

  params[idx++] = db_bind_i32 (limit);

  db_res_t *res = NULL; int rc = 0;

  char sql_converted[2048];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db, sql_converted, params, idx, &res, &err))
    {
      rc = stmt_to_json_array (res, out, &err); db_res_finalize (res);
    }
  else
    {
      rc = err.code;
    }
  return rc;
}


int
db_bank_apply_interest (db_t *db)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  /* TODO: implement based on full bank_interest_policy / bank_accounts schema */
  return 0;
}


int
db_bank_process_orders (db_t *db)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  /* TODO: implement processing of bank_orders into bank_transactions */
  return 0;
}


int
db_bank_set_frozen_status (db_t *db,
                           const char *owner_type,
                           int owner_id,
                           int is_frozen)
{
  if (!db || strcmp (owner_type, "player") != 0)
    {
      return ERR_DB_MISUSE;
    }
  const char *sql =
    "INSERT INTO bank_flags (player_id, is_frozen) VALUES ({1}, {2}) ON CONFLICT(player_id) DO UPDATE SET is_frozen = excluded.is_frozen;";
  db_error_t err;
  db_bind_t params[] = { db_bind_i32 (owner_id), db_bind_i32 (is_frozen) };

  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (!db_exec (db, sql_converted, params, 2, &err))
    {
      return err.code;
    }
  return 0;
}


int
db_bank_get_frozen_status (db_t *db,
                           const char *owner_type,
                           int owner_id,
                           int *out_frozen)
{
  if (!db || !out_frozen || strcmp (owner_type, "player") != 0)
    {
      return ERR_DB_MISUSE;
    }
  db_res_t *res = NULL; db_error_t err;
  int rc = 0;
  db_bind_t params[] = { db_bind_i32 (owner_id) };

  const char *sql = "SELECT is_frozen FROM bank_flags WHERE player_id = {1};";
  char sql_converted[256];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db,
                sql_converted,
                params,
                1,
                &res,
                &err))
    {
      if (db_res_step (res, &err))
        {
          *out_frozen = db_res_col_i32 (res,
                                        0,
                                        &err);
        }
      else
        {
          *out_frozen = 0;
        }
      rc = 0;
      goto cleanup;
    }
  rc = err.code;

cleanup:
  if (res)
      db_res_finalize(res);
  return rc;
}


/* Transfer: Debit Sender -> Credit Receiver */
int
db_bank_transfer (db_t *db,
                  const char *from_type,
                  int from_id,
                  const char *to_type,
                  int to_id,
                  long long amount)
{
  if (amount <= 0)
    {
      return ERR_INVALID_ARG;
    }

  db_error_t err;


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      return err.code;
    }

  int rc = h_deduct_credits (db,
                             from_type,
                             from_id,
                             amount,
                             "TRANSFER",
                             NULL,
                             NULL);


  if (rc != 0)
    {
      db_tx_rollback (db, NULL);
      return rc;
    }

  rc = h_add_credits (db,
                      to_type,
                      to_id,
                      amount,
                      "TRANSFER",
                      NULL,
                      NULL);
  if (rc != 0)
    {
      db_tx_rollback (db, NULL);
      return rc;
    }

  if (!db_tx_commit (db, &err))
    {
      return err.code;
    }
  return 0;
}


/* ==================================================================== */


/* COMMAND HANDLERS                                                    */


/* ==================================================================== */


int
cmd_bank_history (client_ctx_t *ctx, json_t *root)
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
      send_response_error (ctx, root, ERR_INVALID_SCHEMA,
                           "data must be object");
      return 0;
    }
  int limit = 20;
  json_t *j_limit = json_object_get (data, "limit");


  if (json_is_integer (j_limit))
    {
      limit = (int) json_integer_value (j_limit);
    }
  if (limit <= 0 || limit > 50)
    {
      limit = 20;
    }

  const char *tx_type_filter = NULL;
  json_t *j_tx_type = json_object_get (data, "tx_type");


  if (json_is_string (j_tx_type))
    {
      tx_type_filter = json_string_value (j_tx_type);
    }

  long long start_date = 0;
  json_t *j_start = json_object_get (data, "start_date");


  if (json_is_integer (j_start))
    {
      start_date = json_integer_value (j_start);
    }

  long long end_date = 0;
  json_t *j_end = json_object_get (data, "end_date");


  if (json_is_integer (j_end))
    {
      end_date = json_integer_value (j_end);
    }

  long long min_amount = 0;
  json_t *j_min = json_object_get (data, "min_amount");


  if (json_is_integer (j_min))
    {
      min_amount = json_integer_value (j_min);
    }

  long long max_amount = 0;
  json_t *j_max = json_object_get (data, "max_amount");


  if (json_is_integer (j_max))
    {
      max_amount = json_integer_value (j_max);
    }

  json_t *transactions_array = NULL;
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }

  int rc = db_bank_get_transactions (db,
                                     "player",
                                     ctx->player_id,
                                     limit,
                                     tx_type_filter,
                                     start_date,
                                     end_date,
                                     min_amount,
                                     max_amount,
                                     &transactions_array);


  if (rc != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED,
                           "Failed to retrieve history.");
      if (transactions_array)
        {
          json_decref (transactions_array);
        }
      return 0;
    }
  json_t *payload = json_object ();


  json_object_set (payload, "history", transactions_array);
  json_object_set_new (payload, "has_next_page", json_false ());

  send_response_ok_take (ctx, root, "bank.history.response", &payload);
  return 0;
}


int
cmd_bank_leaderboard (client_ctx_t *ctx, json_t *root)
{
  db_t *db = game_db_get_handle ();
  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  int limit = 20;


  if (json_is_object (data))
    {
      json_t *j_limit = json_object_get (data, "limit");


      if (json_is_integer (j_limit))
        {
          limit = (int) json_integer_value (j_limit);
        }
    }
  if (limit <= 0 || limit > 100)
    {
      limit = 20;
    }

  const char *sql_query =
    "SELECT P.name, BA.balance FROM bank_accounts BA JOIN players P ON P.player_id = BA.owner_id "
    "WHERE BA.owner_type = 'player' ORDER BY BA.balance DESC LIMIT {1};";
  db_bind_t params[] = { db_bind_i32 (limit) };
  db_res_t *res = NULL;
  db_error_t err;
  json_t *leaderboard_array = json_array ();

  char sql_converted[512];
  sql_build(db, sql_query, sql_converted, sizeof(sql_converted));

  if (db_query (db, sql_converted, params, 1, &res, &err))
    {
      while (db_res_step (res, &err))
        {
          json_t *entry = json_object ();


          json_object_set_new (entry, "player_name",
                               json_string (db_res_col_text (res, 0, &err)));
          json_object_set_new (entry, "balance",
                               json_integer (db_res_col_i64 (res,
                                                             1,
                                                             &err)));
          json_array_append_new (leaderboard_array, entry);
        }
      db_res_finalize (res);
    }
  else
    {
      LOGE ("cmd_bank_leaderboard: query failed: %s", err.message);
      send_response_error (ctx, root, ERR_DB, "Database error.");
      json_decref (leaderboard_array);
      return 0;
    }

  json_t *payload = json_object ();


  json_object_set_new (payload, "leaderboard", leaderboard_array);
  send_response_ok_take (ctx, root, "bank.leaderboard.response", &payload);
  return 0;
}


int
cmd_bank_deposit (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *j_amount = json_object_get (data, "amount");


  if (!json_is_integer (j_amount) || json_integer_value (j_amount) <= 0)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Invalid amount.");
      return 0;
    }
  long long amount = json_integer_value (j_amount);
  long long player_petty_cash = 0;


  if (h_get_player_petty_cash (db, ctx->player_id, &player_petty_cash) != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Failed to retrieve balance.");
      return 0;
    }
  if (player_petty_cash < amount)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient petty cash.",
                                   NULL);
      return 0;
    }

  db_error_t err;


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, err.code, "Database busy.");
      return 0;
    }

  if (h_deduct_player_petty_cash_unlocked (db, ctx->player_id, amount,
                                           NULL) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient funds.",
                                   NULL);
      return 0;
    }
  long long new_bank_balance = 0;
  int account_id = 0;


  if (h_get_account_id_unlocked (db, "player", ctx->player_id,
                                 &account_id) != 0 || account_id <= 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Account not found.");
      return 0;
    }

  if (h_add_credits_unlocked (db,
                              account_id,
                              amount,
                              "DEPOSIT",
                              "",
                              &new_bank_balance) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Bank error.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code, "Commit failed.");
      return 0;
    }

  json_t *payload = json_object ();


  json_object_set_new (payload, "player_id", json_integer (ctx->player_id));
  json_object_set_new (payload, "new_balance", json_integer (new_bank_balance));
  send_response_ok_take (ctx, root, "bank.deposit.confirmed", &payload);
  return 0;
}


int
cmd_bank_transfer (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *j_to = json_object_get (data, "to_player_id");
  json_t *j_amt = json_object_get (data, "amount");


  if (!json_is_integer (j_to) || !json_is_integer (j_amt))
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INVALID_ARG,
                                   "Invalid arguments",
                                   NULL);
      return 0;
    }
  int to_id = json_integer_value (j_to);
  long long amount = json_integer_value (j_amt);


  if (amount <= 0 || to_id == ctx->player_id)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INVALID_ARG,
                                   "Invalid amount or recipient",
                                   NULL);
      return 0;
    }

  long long from_bal = 0, to_bal = 0;


  if (h_get_credits (db, "player", ctx->player_id,
                     &from_bal) != 0 || from_bal < amount)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient funds",
                                   NULL);
      return 0;
    }

  char tx_grp[65];


  h_generate_hex_uuid (tx_grp, sizeof (tx_grp));

  db_error_t err;


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, err.code, "Database busy.");
      return 0;
    }

  if (h_deduct_credits (db,
                        "player",
                        ctx->player_id,
                        amount,
                        "TRANSFER",
                        tx_grp,
                        &from_bal) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_UNKNOWN, "Transfer failed (deduct)");
      return 0;
    }

  if (h_add_credits (db, "player", to_id, amount, "TRANSFER", tx_grp,
                     &to_bal) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_UNKNOWN, "Transfer failed (add)");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code, "Commit failed.");
      return 0;
    }

  json_t *payload = json_object ();


  json_object_set_new (payload, "from_player_id",
                       json_integer (ctx->player_id));
  json_object_set_new (payload, "to_player_id", json_integer (to_id));
  json_object_set_new (payload, "from_balance", json_integer (from_bal));
  json_object_set_new (payload, "to_balance", json_integer (to_bal));
  send_response_ok_take (ctx, root, "bank.transfer.confirmed", &payload);
  return 0;
}


int
cmd_bank_withdraw (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated",
                                   NULL);
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing payload.");
      return 0;
    }

  json_t *j_amount = json_object_get (data, "amount");
  long long amount =
    json_is_integer (j_amount) ? json_integer_value (j_amount) : 0;


  if (amount <= 0)
    {
      send_response_error (ctx, root, ERR_MISSING_FIELD, "Invalid amount.");
      return 0;
    }

  long long new_balance = 0;
  db_error_t err;


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_refused_steal (ctx, root, err.code, "Database busy.", NULL);
      return 0;
    }

  int account_id = 0;


  if (h_get_account_id_unlocked (db, "player", ctx->player_id,
                                 &account_id) != 0 || account_id <= 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Account not found.");
      return 0;
    }

  if (h_deduct_credits_unlocked (db,
                                 account_id,
                                 amount,
                                 "WITHDRAWAL",
                                 "",
                                 &new_balance) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient funds",
                                   NULL);
      return 0;
    }
  if (h_add_player_petty_cash (db, ctx->player_id, amount, NULL) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_UNKNOWN, "Petty cash update failed");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code, "Commit failed.");
      return 0;
    }

  json_t *payload = json_object ();


  json_object_set_new (payload, "new_balance", json_integer (new_balance));
  send_response_ok_take (ctx, root, "bank.withdraw.confirmed", &payload);
  return 0;
}


int
cmd_bank_balance (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Not authenticated",
                                   NULL); return 0;
    }
  long long p_balance = 0;
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle"); return 0;
    }

  if (db_get_player_bank_balance (db, ctx->player_id, &p_balance) != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Error retrieving balance.");
      return 0;
    }
  json_t *payload = json_object ();


  json_object_set_new (payload, "balance", json_integer (p_balance));
  send_response_ok_take (ctx, root, "bank.balance", &payload);
  return 0;
}


int
cmd_fine_list (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Authentication required",
                                   NULL); return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle"); return 0;
    }

  json_t *fines_array = json_array ();
  db_error_t err;
  const char *sql =
    "SELECT fines_id as id, reason, amount, issued_ts, status FROM fines WHERE recipient_type = 'player' AND recipient_id = {1} AND status != 'paid';";
  db_bind_t params[] = { db_bind_i32 (ctx->player_id) };
  db_res_t *res = NULL;

  char sql_converted[512];
  sql_build(db, sql, sql_converted, sizeof(sql_converted));

  if (db_query (db, sql_converted, params, 1, &res, &err))
    {
      while (db_res_step (res, &err))
        {
          json_t *fine = json_object ();


          json_object_set_new (fine, "id", json_integer (db_res_col_i32 (res,
                                                                         0,
                                                                         &err)));
          json_object_set_new (fine, "reason",
                               json_string (db_res_col_text (res,
                                                             1,
                                                             &err)));
          json_object_set_new (fine, "amount",
                               json_integer (db_res_col_i64 (res,
                                                             2,
                                                             &err)));
          json_object_set_new (fine, "issued_ts",
                               json_string (db_res_col_text (res,
                                                             3,
                                                             &err)));
          json_object_set_new (fine, "status",
                               json_string (db_res_col_text (res,
                                                             4,
                                                             &err)));
          json_array_append_new (fines_array, fine);
        }
      db_res_finalize (res);
    }
  else
    {
      LOGE ("cmd_fine_list: query failed: %s", err.message);
      send_response_error (ctx, root, ERR_DB, "Database error.");
      json_decref (fines_array);
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "fines", fines_array);
  send_response_ok_take (ctx, root, "fine.list", &response_data);
  return 0;
}


int
cmd_fine_pay (client_ctx_t *ctx,
              json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_AUTHENTICATED,
                                   "Authentication required",
                                   NULL); return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle"); return 0;
    }

  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
      return 0;
    }

  int fine_id = json_integer_value (json_object_get (data, "fine_id"));
  long long amount_to_pay = json_integer_value (json_object_get (data,
                                                                 "amount"));


  if (fine_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid fine_id."); return 0;
    }

  long long fine_amount = 0;
  int fine_recipient_id = 0;
  const char *fine_status = NULL;
  const char *fine_recipient_type = NULL;

  db_error_t err;
  const char *sql_select_fine =
    "SELECT amount, recipient_id, status, recipient_type FROM fines WHERE fines_id = {1};";
  db_bind_t params_fine[] = { db_bind_i32 (fine_id) };
  db_res_t *res_fine = NULL;

  char sql_select_fine_converted[512];
  sql_build(db, sql_select_fine, sql_select_fine_converted, sizeof(sql_select_fine_converted));

  if (db_query (db, sql_select_fine_converted, params_fine, 1, &res_fine, &err))
    {
      if (db_res_step (res_fine, &err))
        {
          fine_amount = db_res_col_i64 (res_fine, 0, &err);
          fine_recipient_id = db_res_col_i32 (res_fine, 1, &err);
          fine_status = db_res_col_text (res_fine, 2, &err);
          fine_recipient_type = db_res_col_text (res_fine, 3, &err);
        }
      db_res_finalize (res_fine);
    }
  else
    {
      send_response_error (ctx, root, ERR_DB,
                           "Database error retrieving fine.");
      return 0;
    }

  if (fine_recipient_id != ctx->player_id ||
      (fine_recipient_type && strcasecmp (fine_recipient_type, "player") != 0))
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Fine does not belong to this player.",
                                   NULL);
      return 0;
    }
  if (fine_status && strcasecmp (fine_status, "paid") == 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INVALID_ARG,
                                   "Fine already paid.",
                                   NULL);
      return 0;
    }

  if (amount_to_pay <= 0 || amount_to_pay > fine_amount)
    {
      amount_to_pay = fine_amount;
    }

  long long player_credits = 0;


  if (h_get_player_petty_cash (db, ctx->player_id, &player_credits) != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Failed to retrieve player credits.");
      return 0;
    }
  if (player_credits < amount_to_pay)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient credits to pay fine.",
                                   NULL);
      return 0;
    }

  if (h_deduct_player_petty_cash_unlocked (db,
                                           ctx->player_id,
                                           amount_to_pay,
                                           NULL) != 0)
    {
      send_response_error (ctx, root, ERR_DB, "Failed to deduct credits.");
      return 0;
    }

  const char *new_status = (amount_to_pay == fine_amount) ? "paid" : "unpaid";
  const char *sql_update_fine =
    "UPDATE fines SET status = {1}, amount = amount - {2} WHERE fines_id = {3};";
  db_bind_t params_update[] = { db_bind_text (new_status),
                                db_bind_i64 (amount_to_pay),
                                db_bind_i32 (fine_id) };

  char sql_update_fine_converted[512];
  sql_build(db, sql_update_fine, sql_update_fine_converted, sizeof(sql_update_fine_converted));

  if (!db_exec (db, sql_update_fine_converted, params_update, 3, &err))
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Database error preparing fine update.");
      h_add_player_petty_cash_unlocked (db, ctx->player_id, amount_to_pay,
                                        NULL);                                    // Refund
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Fine paid successfully."));
  json_object_set_new (response_data, "fine_id", json_integer (fine_id));
  json_object_set_new (response_data,
                       "amount_paid",
                       json_integer (amount_to_pay));
  send_response_ok_take (ctx, root, "fine.pay.success", &response_data);
  return 0;
}

