#include "db/repo/repo_bank.h"
#include "db/repo/repo_engine.h"
/* src/server_bank.c */
#define MAX_BACKLOG_DAYS 30
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
#include "db/repo/repo_database.h"
#include "game_db.h"
#include "db/sql_driver.h"
#include "repo_cmd.h"
#include "errors.h"
#include "config.h"
#include "server_cmds.h"
#include "server_rules.h"
#include "repo_player_settings.h"
#include "server_envelope.h"
#include "server_auth.h"
#include "server_log.h"
#include "common.h"
#include "server_ships.h"
#include "server_loop.h"
#include "server_bank.h"
#include "bank_schedule.h"
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

  long long new_bal = 0;
  int account_id =
    repo_bank_player_balance_add (db, player_id, delta, &new_bal);

  if (account_id < 0)
    {
      /*
       * Either:
       * - account missing and delta < 0 (cannot create for debit)
       * - account exists but insufficient funds (UPDATE filtered it out)
       *
       * Distinguish with a quick existence check.
       */
      int exists = 0;
      int rc = repo_bank_check_account_active (db, player_id, &exists);

      if (rc != 0)
	{
	  return rc;
	}

      return exists ? ERR_DB_CONSTRAINT : ERR_DB_NOT_FOUND;
    }

  /*
   * 2) Insert minimal ledger row into bank_transactions (best-effort but treated as required).
   * Schema: (account_id, tx_type, direction, amount, currency, description, ts, balance_after)
   */
  {
    const char *direction = (delta >= 0) ? "CREDIT" : "DEBIT";
    int64_t amount_abs = (delta >= 0) ? (int64_t) delta : (int64_t) (-delta);
    int64_t ts = (int64_t) time (NULL);

    int rc =
      repo_bank_insert_adj_transaction (db, (int64_t) account_id, direction,
					amount_abs, ts, (int64_t) new_bal);
    if (rc != 0)
      {
	return rc;
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
	  db_res_cancel (st);
	  json_decref (arr);
	  return ERR_NOMEM;
	}
      for (int i = 0; i < cols; i++)
	{
	  const char *col_name = db_res_col_name (st, i);
	  db_col_type_t col_type = db_res_col_type (st, i);
	  json_t *val = NULL;


	  switch (col_type)
	    {
	    case DB_TYPE_INTEGER:
	      val = json_integer (db_res_col_i64 (st, i, err));
	      break;
	    case DB_TYPE_FLOAT:
	      val = json_real (db_res_col_double (st, i, err));
	      break;
	    case DB_TYPE_TEXT:
	      val = json_string (db_res_col_text (st, i, err) ? : "");
	      break;
	    case DB_TYPE_NULL:
	      val = json_null ();
	      break;
	    default:
	      val = json_null ();
	      break;
	    }
	  json_object_set_new (obj, col_name, val ? : json_null ());
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
		    int owner_id, long long *out_balance)
{
  if (!db || !owner_type || !out_balance)
    {
      return ERR_DB_MISUSE;
    }
  return repo_bank_get_balance (db, owner_type, owner_id, out_balance);
}


/* Helper: Resolve owner_type/id to a bank_accounts.id */
int
h_get_account_id_unlocked (db_t *db,
			   const char *owner_type,
			   int owner_id, int *account_id_out)
{
  if (!db || !owner_type || !account_id_out)
    {
      return ERR_INVALID_ARG;
    }

  return repo_bank_get_account_id (db, owner_type, owner_id, account_id_out);
}


int
h_get_system_account_id_unlocked (db_t *db, const char *system_owner_type,
				  int system_owner_id, int *account_id_out)
{
  int rc = h_get_account_id_unlocked (db, system_owner_type, system_owner_id,
				      account_id_out);
  /* repo_bank_get_account_id reports an absent account as an internal
   * database-not-found result, not the client-facing ERR_NOT_FOUND code. */
  if (rc == ERR_DB_NOT_FOUND)
    {
      /* Create the system account if it doesn't exist */
      rc =
	h_create_bank_account_unlocked (db, system_owner_type,
					system_owner_id, 0, NULL);
      if (rc == 0)
	{
	  rc = h_get_account_id_unlocked (db,
					  system_owner_type,
					  system_owner_id, account_id_out);
	}
    }
  return rc;
}


static int
h_create_personal_bank_alert_notice (db_t *db, int player_id, const char *msg)
{
  const char *now_ts = sql_now_timestamptz (db);
  if (!now_ts)
    {
      return -1;		/* Unsupported backend */
    }

  return repo_bank_create_system_notice (db, now_ts, player_id, msg);
}


int
calculate_fees (db_t *db,
		const char *tx_type,
		long long base_amount,
		const char *owner_type, fee_result_t *out)
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
  return 9223372036854775807LL;	// Max for others
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


static int
h_adjust_credits_unlocked (db_t *db, int account_id, long long amount,
			   const char *tx_type, const char *tx_group_id,
			   long long *out_new_balance,
			   int64_t *transaction_id_out, bool credit)
{
  if (!db || account_id <= 0 || amount < 0 || !tx_type)
    {
      return ERR_DB_MISUSE;
    }
  long long new_balance = 0;
  int rc = credit
    ? repo_bank_add_credits_returning (db, account_id, amount, &new_balance)
    : repo_bank_deduct_credits_returning (db, account_id, amount, &new_balance);
  if (rc != 0)
    {
      if (!credit && rc == ERR_DB_NOT_FOUND) return ERR_INSUFFICIENT_FUNDS;
      return rc;
    }

  /* Sanity check: balance should never be negative after adding credits */
  if (new_balance < 0)
    {
      LOGE ("Overflow detected: balance became negative after credit");
      return ERR_DB_QUERY_FAILED;
    }

  const char *now_epoch = sql_epoch_now (db);
  if (!now_epoch)
    {
      return ERR_DB_QUERY_FAILED;	/* Unsupported backend */
    }

  const char *sql_tx_format =
    "INSERT INTO bank_transactions (account_id, tx_type, direction, amount, currency, balance_after, tx_group_id, ts) "
    "VALUES ({1}, {2}, '%s', {3}, 'CRD', {4}, {5}, %s);";
  char sql_tx_template[512];
  snprintf (sql_tx_template, sizeof sql_tx_template, sql_tx_format,
            credit ? "CREDIT" : "DEBIT", now_epoch);

  rc = repo_bank_insert_transaction_returning_id
    (db, sql_tx_template, account_id, tx_type,
	     credit ? "CREDIT" : "DEBIT", amount, new_balance,
	     tx_group_id ? tx_group_id : "", now_epoch, transaction_id_out);
  if (rc != 0)
    {
      return rc;
    }


  if (out_new_balance)
    {
      *out_new_balance = new_balance;
    }
  return 0;
}


int
h_add_credits_unlocked_with_transaction_id (db_t *db, int account_id,
			   long long amount, const char *tx_type,
			   const char *tx_group_id, long long *out_new_balance,
			   int64_t *transaction_id_out)
{
  return h_adjust_credits_unlocked (db, account_id, amount, tx_type,
      tx_group_id, out_new_balance, transaction_id_out, true);
}

int
h_add_credits_unlocked (db_t *db, int account_id, long long amount,
			const char *tx_type, const char *tx_group_id,
			long long *out_new_balance)
{
  return h_add_credits_unlocked_with_transaction_id (db, account_id, amount,
      tx_type, tx_group_id, out_new_balance, NULL);
}

int
h_deduct_credits_unlocked_with_transaction_id (db_t *db, int account_id,
			   long long amount, const char *tx_type,
			   const char *tx_group_id, long long *out_new_balance,
			   int64_t *transaction_id_out)
{
  return h_adjust_credits_unlocked (db, account_id, amount, tx_type,
      tx_group_id, out_new_balance, transaction_id_out, false);
}

int
h_deduct_credits_unlocked (db_t *db, int account_id, long long amount,
			   const char *tx_type, const char *tx_group_id,
			   long long *out_new_balance)
{
  return h_deduct_credits_unlocked_with_transaction_id (db, account_id,
      amount, tx_type, tx_group_id, out_new_balance, NULL);
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

  int rc =
    repo_bank_create_account_if_not_exists (db, owner_type, owner_id,
					    initial_balance);
  if (rc != 0)
    {
      return rc;
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
	       const char *tx_group_id, long long *out_new_balance)
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
			       amount, tx_type, tx_group_id, out_new_balance);
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
		    amount, out_new_balance ? *out_new_balance : 0);
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
		  const char *tx_group_id, long long *out_new_balance)
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
				  tx_type, tx_group_id, out_new_balance);
  if (rc != 0)
    {
      LOGE
	("h_deduct_credits: Deduction failed for account %d, amount %lld, rc %d",
	 account_id, amount, rc);
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
		    amount, out_new_balance ? *out_new_balance : 0);
	  h_create_personal_bank_alert_notice (db, owner_id, msg);
	}
    }
  return rc;
}


int
h_get_credits (db_t *db,
	       const char *owner_type, int owner_id, long long *out_balance)
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
			long long initial_balance, int *account_id_out)
{
  if (!db)
    {
      return -1;
    }
  return h_create_bank_account_unlocked (db,
					 owner_type,
					 owner_id,
					 initial_balance, account_id_out);
}


int
db_bank_deposit (db_t *db,
		 const char *owner_type, int owner_id, long long amount)
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
		  const char *owner_type, int owner_id, long long amount)
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
			  long long min, long long max, json_t **out)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  db_error_t err;
  db_res_t *res = NULL;
  int rc = 0;

  if (repo_bank_get_transactions
      (db, owner_type, owner_id, limit, filter, start, end, min, max, &res,
       &err) == 0)
    {
      rc = stmt_to_json_array (res, out, &err);
      db_res_finalize (res);
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
  return server_bank_apply_interest (db, (int64_t) time (NULL));
}

static int64_t
bank_order_first_run (int64_t now_s, const char *frequency, int hour,
                      int minute, int weekday, int month_day)
{
  time_t value = (time_t) now_s;
  struct tm utc;
  if (!gmtime_r (&value, &utc) || hour < 0 || hour > 23
      || minute < 0 || minute > 59)
    return 0;
  utc.tm_hour = hour;
  utc.tm_min = minute;
  utc.tm_sec = 0;
  if (strcmp (frequency, "daily") == 0)
    {
      time_t candidate = timegm (&utc);
      if (candidate <= value) candidate += 86400;
      return (int64_t) candidate;
    }
  if (strcmp (frequency, "weekly") == 0 && weekday >= 1 && weekday <= 7)
    {
      int today = utc.tm_wday == 0 ? 7 : utc.tm_wday;
      int delta = (weekday - today + 7) % 7;
      time_t candidate = timegm (&utc) + (time_t) delta * 86400;
      if (candidate <= value) candidate += 7 * 86400;
      return (int64_t) candidate;
    }
  if (strcmp (frequency, "monthly") == 0 && month_day >= 1
      && month_day <= 31)
    {
      int year = utc.tm_year + 1900;
      time_t candidate = (time_t) bank_schedule_monthly_at
        (year, utc.tm_mon + 1, month_day, hour, minute);
      if (candidate <= value)
        {
          int next_year = year;
          int next_month = utc.tm_mon + 2;
          if (next_month > 12)
            { next_month = 1; next_year++; }
          candidate = (time_t) bank_schedule_monthly_at
            (next_year, next_month, month_day, hour, minute);
        }
      return (int64_t) candidate;
    }
  return 0;
}

/* Banking owns the balance and ledger mutation; the engine only schedules it. */
int
server_bank_apply_interest (db_t *db, int64_t now_s)
{
  if (!db)
    return ERR_DB_CLOSED;

  db_error_t err;
  db_res_t *res = repo_engine_get_active_interest_accounts (db, &err);
  if (!res)
    return err.code ? err.code : ERR_DB_QUERY_FAILED;

  const int current_epoch_day = (int) (now_s / 86400);
  const long long minimum =
    h_get_config_int_unlocked (db, "bank_min_balance_for_interest", 0);
  const long long maximum =
    h_get_config_int_unlocked (db, "bank_max_daily_interest_per_account",
                               9223372036854775807LL);
  int rc = 0;

  while (db_res_step (res, &err))
    {
      const int account_id = (int) db_res_col_i32 (res, 0, &err);
      long long balance = db_res_col_i64 (res, 3, &err);
      const int rate_bp = (int) db_res_col_i32 (res, 4, &err);
      const int last_tick = (int) db_res_col_i32 (res, 5, &err);
      int days = current_epoch_day - last_tick;

      if (days <= 0 || balance < minimum)
        continue;
      if (days > MAX_BACKLOG_DAYS)
        days = MAX_BACKLOG_DAYS;

      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
        {
          rc = err.code;
          break;
        }

      char tx_group[96];
      snprintf (tx_group, sizeof tx_group, "bank-interest-%d-%d",
                account_id, current_epoch_day);
      const char *now_epoch = sql_epoch_now (db);
      if (!now_epoch)
        {
          db_tx_rollback (db, NULL);
          rc = ERR_DB_QUERY_FAILED;
          break;
        }

      for (int day = 0; day < days; ++day)
        {
          if (balance <= 0)
            break;
          long long daily = (long long)
            (((__int128) balance * rate_bp) / (10000 * 365));
          if (daily > maximum)
            daily = maximum;
          if (daily <= 0)
            continue;

          long long new_balance = 0;
          rc = repo_bank_add_credits_returning (db, account_id, daily,
                                                &new_balance);
          if (rc != 0)
            break;

          const char *tx_template =
            "INSERT INTO bank_transactions "
            "(account_id, tx_type, direction, amount, currency, "
            "balance_after, tx_group_id, ts) VALUES "
            "({1}, {2}, 'CREDIT', {3}, 'CRD', {4}, {5}, %s);";
          rc = repo_bank_insert_transaction (db, tx_template, account_id,
                                             "INTEREST", "CREDIT", daily,
                                             new_balance, tx_group, now_epoch);
          if (rc != 0)
            break;
          balance = new_balance;
        }

      if (rc == 0)
        rc = repo_engine_update_last_interest_tick (db, current_epoch_day,
                                                    account_id);
      if (rc == 0 && !db_tx_commit (db, &err))
        rc = err.code;
      if (rc != 0)
        {
          db_tx_rollback (db, NULL);
          break;
        }
    }

  if (rc == 0 && err.code != 0)
    rc = err.code;
  db_res_finalize (res);
  return rc;
}


int
db_bank_process_orders (db_t *db)
{
  if (!db)
    {
      return ERR_DB_CLOSED;
    }
  int64_t now_s = (int64_t) time (NULL);
  char idcol[32];
  snprintf (idcol, sizeof idcol, "%s",
            db_backend (db) == DB_BACKEND_MYSQL ? "id" : "bank_orders_id");
  char due_sql_tmpl[512], due_sql[768];
  snprintf (due_sql_tmpl, sizeof due_sql_tmpl,
            "SELECT %s FROM bank_orders WHERE enabled=TRUE "
            "AND frequency IN ('daily','weekly','monthly') AND next_run_at <= {1} "
            "ORDER BY next_run_at, %s LIMIT 100;", idcol, idcol);
  if (sql_build (db, due_sql_tmpl, due_sql, sizeof due_sql) != 0)
    return ERR_DB_QUERY_FAILED;
  db_error_t err = {0};
  db_res_t *due = NULL;
  if (!db_query (db, due_sql, (db_bind_t[]){db_bind_timestamp_text (now_s)},
                 1, &due, &err))
    return err.code ? err.code : ERR_DB_QUERY_FAILED;
  int ids[100];
  size_t count = 0;
  while (count < 100 && db_res_step (due, &err))
    ids[count++] = db_res_col_i32 (due, 0, &err);
  db_res_finalize (due);
  if (err.code)
    return err.code;

  char epoch_expr[128];
  if (sql_ts_to_epoch_expr (db, "next_run_at", epoch_expr,
                            sizeof epoch_expr) != 0)
    return ERR_DB_QUERY_FAILED;
  for (size_t i = 0; i < count; ++i)
    {
      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
        return err.code ? err.code : ERR_DB_TX;
      char order_sql_tmpl[768], order_sql[1024];
      snprintf (order_sql_tmpl, sizeof order_sql_tmpl,
        "SELECT player_id,to_id,amount,frequency,utc_hour,utc_minute,"
        "COALESCE(month_day,0),%s AS due_epoch "
        "FROM bank_orders WHERE %s={1} AND enabled=TRUE FOR UPDATE",
        epoch_expr, idcol);
      if (sql_build (db, order_sql_tmpl, order_sql, sizeof order_sql) != 0)
        { db_tx_rollback (db, NULL); return ERR_DB_QUERY_FAILED; }
      db_res_t *order = NULL;
      if (!db_query (db, order_sql, (db_bind_t[]){db_bind_i32 (ids[i])},
                     1, &order, &err))
        { db_tx_rollback (db, NULL); return err.code; }
      if (!db_res_step (order, &err))
        { db_res_finalize (order); db_tx_rollback (db, NULL); continue; }
      int payer = db_res_col_i32 (order, 0, &err);
      int recipient = db_res_col_i32 (order, 1, &err);
      long long amount = db_res_col_i64 (order, 2, &err);
      const char *frequency_col = db_res_col_text (order, 3, &err);
      char frequency[16] = {0};
      if (frequency_col) snprintf (frequency, sizeof frequency, "%s", frequency_col);
      int hour = db_res_col_i32 (order, 4, &err);
      int minute = db_res_col_i32 (order, 5, &err);
      int month_day = db_res_col_i32 (order, 6, &err);
      int64_t occurrence = db_res_col_i64 (order, 7, &err);
      db_res_finalize (order);
      if (err.code || occurrence <= 0
          || payer <= 0 || recipient <= 0 || amount <= 0)
        { db_tx_rollback (db, NULL); return err.code ? err.code : ERR_DB_QUERY_FAILED; }
      /* Another scheduler worker may have settled this order after the due-ID
       * snapshot but before this worker acquired the row lock. Its next
       * occurrence is then in the future; it is stale work, not a processing
       * error and must not interrupt the rest of this batch. */
      if (occurrence > now_s)
        { db_tx_rollback (db, NULL); continue; }

      char occurrence_group[96];
      snprintf (occurrence_group, sizeof occurrence_group,
                "bank-order-%d-%lld", ids[i], (long long) occurrence);
      int recipient_exists = 0;
      db_res_t *player = NULL;
      char recipient_sql[192];
      if (sql_build (db, "SELECT player_id FROM players WHERE player_id={1} FOR UPDATE",
                     recipient_sql, sizeof recipient_sql) != 0
          || !db_query (db, recipient_sql,
                        (db_bind_t[]){db_bind_i32 (recipient)}, 1,
                        &player, &err))
        { db_tx_rollback (db, NULL); return err.code ? err.code : ERR_DB_QUERY_FAILED; }
      recipient_exists = db_res_step (player, &err);
      db_res_finalize (player);
      const char *outcome = "paid";
      if (!recipient_exists)
        {
          char disable_tmpl[160], disable_sql[256];
          snprintf (disable_tmpl, sizeof disable_tmpl,
                    "UPDATE bank_orders SET enabled=FALSE WHERE %s={1}", idcol);
          if (sql_build (db, disable_tmpl, disable_sql, sizeof disable_sql) != 0
              || !db_exec (db, disable_sql,
                           (db_bind_t[]){db_bind_i32 (ids[i])}, 1, &err))
            { db_tx_rollback (db, NULL); return err.code ? err.code : ERR_DB_QUERY_FAILED; }
          outcome = "cancelled_invalid_recipient";
        }
      else
        {
          int payer_account = 0, recipient_account = 0;
          int rc = repo_bank_get_account_id (db, "player", payer,
                                             &payer_account);
          if (rc == ERR_DB_NOT_FOUND)
            outcome = "skipped_insufficient_funds";
          else if (rc != 0)
            { db_tx_rollback (db, NULL); return rc; }
          else
            {
              rc = repo_bank_create_account_if_not_exists (db, "player",
                                                            recipient, 0);
              if (rc == 0)
                rc = repo_bank_get_account_id (db, "player", recipient,
                                               &recipient_account);
              if (rc != 0)
                { db_tx_rollback (db, NULL); return rc; }
              int lock_ids[2] = {payer_account, recipient_account};
              if (lock_ids[0] > lock_ids[1])
                { int tmp = lock_ids[0]; lock_ids[0] = lock_ids[1]; lock_ids[1] = tmp; }
              long long locked_balance = 0;
              for (int lock_index = 0; lock_index < 2; ++lock_index)
                if (repo_bank_get_balance_by_account_id_for_update
                    (db, lock_ids[lock_index], &locked_balance) != 0)
                  { db_tx_rollback (db, NULL); return ERR_DB_QUERY_FAILED; }
              rc = h_deduct_credits (db, "player", payer, amount,
                                     "TRANSFER", occurrence_group, NULL);
              if (rc == ERR_INSUFFICIENT_FUNDS || rc == ERR_DB_NOT_FOUND)
                outcome = "skipped_insufficient_funds";
              else if (rc != 0)
                { db_tx_rollback (db, NULL); return rc; }
              else if ((rc = h_add_credits (db, "player", recipient, amount,
                                             "TRANSFER", occurrence_group, NULL)) != 0)
                { db_tx_rollback (db, NULL); return rc; }
            }
        }

      struct tm scheduled;
      time_t occurrence_time = (time_t) occurrence;
      if (!gmtime_r (&occurrence_time, &scheduled))
        { db_tx_rollback (db, NULL); return ERR_INVALID_ARG; }
      time_t next = (time_t) occurrence;
      if (strcmp (frequency, "daily") == 0)
        next += 86400;
      else if (strcmp (frequency, "weekly") == 0)
        next += 7 * 86400;
      else if (strcmp (frequency, "monthly") == 0)
        {
          int year = scheduled.tm_year + 1900;
          int month = scheduled.tm_mon + 2;
          if (month > 12)
            { month = 1; year++; }
          next = (time_t) bank_schedule_monthly_at
            (year, month, month_day, hour, minute);
          if (next <= occurrence)
            { db_tx_rollback (db, NULL); return ERR_INVALID_ARG; }
        }
      else
        { db_tx_rollback (db, NULL); return ERR_INVALID_ARG; }

      char occurrence_insert[512], occurrence_insert_sql[768];
      snprintf (occurrence_insert, sizeof occurrence_insert,
        "INSERT INTO bank_order_occurrences (bank_orders_id,scheduled_for,outcome,amount) "
        "VALUES ({1},{2},{3},{4})");
      if (sql_build (db, occurrence_insert, occurrence_insert_sql,
                     sizeof occurrence_insert_sql) != 0
          || !db_exec (db, occurrence_insert_sql,
            (db_bind_t[]){db_bind_i32 (ids[i]),
                          db_bind_timestamp_text (occurrence),
                          db_bind_text (outcome), db_bind_i64 (amount)},
            4, &err))
        { db_tx_rollback (db, NULL); return err.code ? err.code : ERR_DB_QUERY_FAILED; }
      if (strcmp (outcome, "cancelled_invalid_recipient") != 0)
        {
          char advance_tmpl[256], advance_sql[384];
          snprintf (advance_tmpl, sizeof advance_tmpl,
            "UPDATE bank_orders SET last_occurrence_at={1},next_run_at={2} WHERE %s={3}", idcol);
          if (sql_build (db, advance_tmpl, advance_sql, sizeof advance_sql) != 0
              || !db_exec (db, advance_sql,
                (db_bind_t[]){db_bind_timestamp_text (occurrence),
                              db_bind_timestamp_text ((int64_t) next),
                              db_bind_i32 (ids[i])}, 3, &err))
            { db_tx_rollback (db, NULL); return err.code ? err.code : ERR_DB_QUERY_FAILED; }
        }
      if (!db_tx_commit (db, &err))
        { db_tx_rollback (db, NULL); return err.code ? err.code : ERR_DB_TX; }
    }
  return 0;
}


int
db_bank_set_frozen_status (db_t *db,
			   const char *owner_type,
			   int owner_id, int is_frozen)
{
  if (!db || strcmp (owner_type, "player") != 0)
    {
      return ERR_DB_MISUSE;
    }
  return repo_bank_set_frozen_status (db, owner_id, is_frozen);
}


int
db_bank_get_frozen_status (db_t *db,
			   const char *owner_type,
			   int owner_id, int *out_frozen)
{
  if (!db || !out_frozen || strcmp (owner_type, "player") != 0)
    {
      return ERR_DB_MISUSE;
    }
  return repo_bank_get_frozen_status (db, owner_id, out_frozen);
}


/* Transfer: Debit Sender -> Credit Receiver */
int
db_bank_transfer (db_t *db,
		  const char *from_type,
		  int from_id,
		  const char *to_type, int to_id, long long amount)
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

  rc = h_add_credits (db, to_type, to_id, amount, "TRANSFER", NULL, NULL);
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


  if (data && !json_is_object (data))
    {
      send_response_error (ctx, root, ERR_INVALID_SCHEMA,
			   "data must be object");
      return 0;
    }
  int limit = 20;
  const char *tx_type_filter = NULL;
  long long start_date = 0;
  long long end_date = 0;
  long long min_amount = 0;
  long long max_amount = 0;

  if (data)
    {
      json_t *j_limit = json_object_get (data, "limit");


      if (json_is_integer (j_limit))
	{
	  limit = (int) json_integer_value (j_limit);
	}
      if (limit <= 0 || limit > 50)
	{
	  limit = 20;
	}

      json_t *j_tx_type = json_object_get (data, "tx_type");


      if (json_is_string (j_tx_type))
	{
	  tx_type_filter = json_string_value (j_tx_type);
	}

      json_t *j_start = json_object_get (data, "start_date");


      if (json_is_integer (j_start))
	{
	  start_date = json_integer_value (j_start);
	}

      json_t *j_end = json_object_get (data, "end_date");


      if (json_is_integer (j_end))
	{
	  end_date = json_integer_value (j_end);
	}

      json_t *j_min = json_object_get (data, "min_amount");


      if (json_is_integer (j_min))
	{
	  min_amount = json_integer_value (j_min);
	}

      json_t *j_max = json_object_get (data, "max_amount");


      if (json_is_integer (j_max))
	{
	  max_amount = json_integer_value (j_max);
	}
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


  json_object_set_new (payload, "history", transactions_array);
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
				   "Not authenticated", NULL);
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

  db_res_t *res = NULL;
  db_error_t err;
  json_t *leaderboard_array = json_array ();

  if ((res = repo_bank_get_leaderboard (db, limit, &err)) != NULL)
    {
      while (db_res_step (res, &err))
	{
	  json_t *entry = json_object ();


	  json_object_set_new (entry, "player_name",
			       json_string (db_res_col_text (res, 0, &err)));
	  json_object_set_new (entry, "balance",
			       json_integer (db_res_col_i64 (res, 1, &err)));
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
				   "Not authenticated", NULL);
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
			   ERR_SERVER_ERROR, "Failed to retrieve balance.");
      return 0;
    }
  if (player_petty_cash < amount)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_INSUFFICIENT_FUNDS,
				   "Insufficient petty cash.", NULL);
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
				   "Insufficient funds.", NULL);
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
			      amount, "DEPOSIT", "", &new_bank_balance) != 0)
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
  json_object_set_new (payload, "new_balance",
		       json_integer (new_bank_balance));
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
				   "Not authenticated", NULL);
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  if (!data || !json_is_object(data))
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_INVALID_ARG,
				   "Invalid arguments", NULL);
      return 0;
    }
  json_t *j_to = json_object_get (data, "to_player_id");
  if (!j_to) j_to = json_object_get (data, "recipient_id");
  json_t *j_amt = json_object_get (data, "amount");


  if (!json_is_integer (j_to) || !json_is_integer (j_amt))
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_INVALID_ARG,
				   "Invalid arguments", NULL);
      return 0;
    }
  int to_id = json_integer_value (j_to);
  long long amount = json_integer_value (j_amt);


  if (amount <= 0 || to_id == ctx->player_id)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_INVALID_ARG,
				   "Invalid amount or recipient", NULL);
      return 0;
    }

  long long from_bal = 0, to_bal = 0;


  if (h_get_credits (db, "player", ctx->player_id,
		     &from_bal) != 0 || from_bal < amount)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_INSUFFICIENT_FUNDS,
				   "Insufficient funds", NULL);
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
			amount, "TRANSFER", tx_grp, &from_bal) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_UNKNOWN,
			   "Transfer failed (deduct)");
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
cmd_bank_orders_create (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    { send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required"); return 0; }
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");
  int recipient = 0;
  long long amount = 0;
  const char *frequency = json_is_object (data)
    ? json_string_value (json_object_get (data, "frequency")) : NULL;
  const char *time_utc = json_is_object (data)
    ? json_string_value (json_object_get (data, "time_utc")) : NULL;
  if (!db || !json_is_object (data)
      || !json_get_int_flexible (data, "recipient_player_id", &recipient)
      || !json_get_int64_flexible (data, "amount", &amount)
      || recipient <= 0 || amount <= 0 || !frequency || !time_utc)
    { send_response_error (ctx, root, ERR_INVALID_ARG, "Invalid standing order fields"); return 0; }
  int hour = -1, minute = -1, used = 0;
  if (sscanf (time_utc, "%2d:%2d%n", &hour, &minute, &used) != 2
      || used != 5 || time_utc[used] != '\0' || hour > 23 || minute > 59)
    { send_response_error (ctx, root, ERR_INVALID_ARG, "time_utc must be HH:MM"); return 0; }
  int weekday = 0, month_day = 0;
  json_t *j_weekday = json_object_get (data, "weekday");
  json_t *j_monthday = json_object_get (data, "day_of_month");
  if (strcmp (frequency, "weekly") == 0)
    {
      if (!json_is_integer (j_weekday) || (weekday = (int) json_integer_value (j_weekday)) < 1 || weekday > 7)
        { send_response_error (ctx, root, ERR_INVALID_ARG, "weekly orders require ISO weekday 1-7"); return 0; }
    }
  else if (strcmp (frequency, "monthly") == 0)
    {
      if (!json_is_integer (j_monthday) || (month_day = (int) json_integer_value (j_monthday)) < 1 || month_day > 31)
        { send_response_error (ctx, root, ERR_INVALID_ARG, "monthly orders require day_of_month 1-31"); return 0; }
    }
  else if (strcmp (frequency, "daily") != 0)
    { send_response_error (ctx, root, ERR_INVALID_ARG, "frequency must be daily, weekly or monthly"); return 0; }

  int64_t now = (int64_t) time (NULL);
  int64_t next = bank_order_first_run (now, frequency, hour, minute,
                                      weekday, month_day);
  if (!next)
    { send_response_error (ctx, root, ERR_INVALID_ARG, "Could not compute schedule"); return 0; }
  db_error_t err = {0};
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    { send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Could not begin order creation"); return 0; }
  db_res_t *check = NULL;
  char sql[512];
  if (sql_build (db, "SELECT player_id FROM players WHERE player_id={1} FOR UPDATE", sql, sizeof sql) != 0
      || !db_query (db, sql, (db_bind_t[]){db_bind_i32 (recipient)}, 1, &check, &err))
    { db_tx_rollback (db, NULL); send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Recipient lookup failed"); return 0; }
  int exists = db_res_step (check, &err);
  db_res_finalize (check);
  if (!exists)
    { db_tx_rollback (db, NULL); send_response_error (ctx, root, ERR_TARGET_INVALID, "Recipient player does not exist"); return 0; }

  const char *idcol = db_backend (db) == DB_BACKEND_MYSQL ? "id" : "bank_orders_id";
  char schedule[40], insert_tmpl[1024], insert_sql[1280];
  snprintf (schedule, sizeof schedule, "%s@%sZ", frequency, time_utc);
  snprintf (insert_tmpl, sizeof insert_tmpl,
    "INSERT INTO bank_orders (player_id,kind,schedule,next_run_at,enabled,amount,currency,to_entity,to_id,memo,frequency,utc_hour,utc_minute,weekday,month_day) "
    "VALUES ({1},'recurring',{2},{3},TRUE,{4},'CRD','player',{5},NULL,{6},{7},{8},NULLIF({9},0),NULLIF({10},0))");
  if (sql_build (db, insert_tmpl, insert_sql, sizeof insert_sql) != 0)
    { db_tx_rollback (db, NULL); send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Order storage unavailable"); return 0; }
  int64_t order_id = 0;
  db_bind_t binds[] = {db_bind_i32 (ctx->player_id), db_bind_text (schedule),
    db_bind_timestamp_text (next), db_bind_i64 (amount), db_bind_i32 (recipient),
    db_bind_text (frequency), db_bind_i32 (hour), db_bind_i32 (minute),
    db_bind_i32 (weekday), db_bind_i32 (month_day)};
  if (!db_exec_insert_id (db, insert_sql, binds, 10, idcol, &order_id, &err))
    { db_tx_rollback (db, NULL); send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Could not create standing order"); return 0; }
  if (!db_tx_commit (db, &err))
    { db_tx_rollback (db, NULL); send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Could not commit standing order"); return 0; }
  json_t *payload = json_pack ("{s:I,s:i,s:I,s:s,s:s,s:I}", "order_id", order_id,
    "recipient_player_id", recipient, "amount", amount,
    "frequency", frequency, "time_utc", time_utc, "next_run_at", (json_int_t) next);
  send_response_ok_take (ctx, root, "bank.orders.created", &payload);
  return 0;
}

int
cmd_bank_orders_list (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    { send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required"); return 0; }
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");
  int page = 1, limit = 50;
  if (json_is_object (data))
    {
      json_t *v = json_object_get (data, "page");
      if (v && (!json_is_integer (v) || (page = (int) json_integer_value (v)) < 1))
        { send_response_error (ctx, root, ERR_INVALID_ARG, "page must be positive"); return 0; }
      v = json_object_get (data, "limit");
      if (v && (!json_is_integer (v) || (limit = (int) json_integer_value (v)) < 1 || limit > 100))
        { send_response_error (ctx, root, ERR_INVALID_ARG, "limit must be 1-100"); return 0; }
    }
  if (!db || (int64_t) (page - 1) * limit > INT32_MAX)
    { send_response_error (ctx, root, ERR_INVALID_ARG, "page out of range"); return 0; }
  const char *idcol = db_backend (db) == DB_BACKEND_MYSQL ? "id" : "bank_orders_id";
  char query_tmpl[768], query[1024];
  snprintf (query_tmpl, sizeof query_tmpl,
    "SELECT %s AS order_id,to_id AS recipient_player_id,amount,frequency,utc_hour,utc_minute,weekday,month_day,enabled,%s AS next_run_at "
    "FROM bank_orders WHERE player_id={1} ORDER BY %s LIMIT {2} OFFSET {3}", idcol,
    db_backend (db) == DB_BACKEND_POSTGRES ? "EXTRACT(EPOCH FROM next_run_at)::bigint" : "UNIX_TIMESTAMP(next_run_at)", idcol);
  if (sql_build (db, query_tmpl, query, sizeof query) != 0)
    { send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Order listing unavailable"); return 0; }
  db_error_t err = {0}; db_res_t *res = NULL; json_t *orders = NULL;
  if (!db_query (db, query, (db_bind_t[]){db_bind_i32 (ctx->player_id), db_bind_i32 (limit), db_bind_i64 ((int64_t)(page-1)*limit)}, 3, &res, &err)
      || stmt_to_json_array (res, &orders, &err) != 0)
    { if (res) db_res_finalize (res); send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Order listing failed"); return 0; }
  db_res_finalize (res);
  json_t *payload = json_pack ("{s:o,s:i,s:i}", "orders", orders, "page", page, "limit", limit);
  send_response_ok_take (ctx, root, "bank.orders.list_v1", &payload);
  return 0;
}

int
cmd_bank_orders_cancel (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    { send_response_error (ctx, root, ERR_NOT_AUTHENTICATED, "Authentication required"); return 0; }
  db_t *db = game_db_get_handle ();
  json_t *data = json_object_get (root, "data");
  long long order_id = 0;
  if (!db || !json_is_object (data) || !json_get_int64_flexible (data, "order_id", &order_id) || order_id <= 0)
    { send_response_error (ctx, root, ERR_INVALID_ARG, "order_id must be positive"); return 0; }
  const char *idcol = db_backend (db) == DB_BACKEND_MYSQL ? "id" : "bank_orders_id";
  char update_tmpl[256], update[384];
  snprintf (update_tmpl, sizeof update_tmpl, "UPDATE bank_orders SET enabled=FALSE WHERE %s={1} AND player_id={2}", idcol);
  if (sql_build (db, update_tmpl, update, sizeof update) != 0)
    { send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Order update unavailable"); return 0; }
  db_error_t err = {0}; int64_t rows = 0;
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err)
      || !db_exec_rows_affected (db, update, (db_bind_t[]){db_bind_i64 (order_id), db_bind_i32 (ctx->player_id)}, 2, &rows, &err))
    { db_tx_rollback (db, NULL); send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Could not cancel order"); return 0; }
  if (rows == 0)
    {
      char check_tmpl[256], check_sql[384];
      snprintf (check_tmpl, sizeof check_tmpl, "SELECT 1 FROM bank_orders WHERE %s={1} AND player_id={2} AND enabled=FALSE", idcol);
      sql_build (db, check_tmpl, check_sql, sizeof check_sql);
      db_res_t *res = NULL;
      if (!db_query (db, check_sql, (db_bind_t[]){db_bind_i64 (order_id), db_bind_i32 (ctx->player_id)}, 2, &res, &err))
        { db_tx_rollback (db, NULL); send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Could not verify order"); return 0; }
      int found = db_res_step (res, &err); db_res_finalize (res);
      if (!found)
        { db_tx_rollback (db, NULL); send_response_error (ctx, root, ERR_NOT_FOUND, "Standing order not found"); return 0; }
    }
  if (!db_tx_commit (db, &err))
    { db_tx_rollback (db, NULL); send_response_error (ctx, root, ERR_DB_QUERY_FAILED, "Cancellation failed"); return 0; }
  json_t *payload = json_pack ("{s:I,s:b}", "order_id", order_id, "cancelled", 1);
  send_response_ok_take (ctx, root, "bank.orders.cancelled", &payload);
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
				   "Not authenticated", NULL);
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
      send_response_error (ctx, root, ERR_INVALID_ARG, "Amount must be positive.");
      return 0;
    }

  long long new_balance = 0;
  db_error_t err;


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_refused_steal (ctx, root, err.code, "Database busy.",
				   NULL);
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
				 amount, "WITHDRAWAL", "", &new_balance) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_refused_steal (ctx,
				   root,
				   ERR_INSUFFICIENT_FUNDS,
				   "Insufficient funds", NULL);
      return 0;
    }
  if (h_add_player_petty_cash (db, ctx->player_id, amount, NULL) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_UNKNOWN,
			   "Petty cash update failed");
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
				   "Not authenticated", NULL);
      return 0;
    }
  long long p_balance = 0;
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }

  if (db_get_player_bank_balance (db, ctx->player_id, &p_balance) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_SERVER_ERROR, "Error retrieving balance.");
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
				   "Authentication required", NULL);
      return 0;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }

  json_t *fines_array = json_array ();
  db_error_t err;
  db_res_t *res = NULL;

  if ((res = repo_bank_get_fines (db, ctx->player_id, &err)) != NULL)
    {
      while (db_res_step (res, &err))
	{
	  json_t *fine = json_object ();


	  json_object_set_new (fine, "id", json_integer (db_res_col_i32 (res,
									 0,
									 &err)));
	  json_object_set_new (fine, "reason",
			       json_string (db_res_col_text (res, 1, &err)));
	  json_object_set_new (fine, "amount",
			       json_integer (db_res_col_i64 (res, 2, &err)));
	  json_object_set_new (fine, "issued_ts",
			       json_string (db_res_col_text (res, 3, &err)));
	  json_object_set_new (fine, "status",
			       json_string (db_res_col_text (res, 4, &err)));
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
cmd_fine_pay (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_NOT_AUTHENTICATED,
				   "Authentication required", NULL);
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
      send_response_error (ctx, root, ERR_BAD_REQUEST,
			   "Missing data payload.");
      return 0;
    }

  int fine_id = json_integer_value (json_object_get (data, "fine_id"));
  long long amount_to_pay = json_integer_value (json_object_get (data,
								 "amount"));


  if (fine_id <= 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_BAD_REQUEST, "Missing or invalid fine_id.");
      return 0;
    }

  long long fine_amount = 0;
  int fine_recipient_id = 0;
  const char *fine_status = NULL;
  const char *fine_recipient_type = NULL;

  db_error_t err;
  db_res_t *res_fine = NULL;
  int check_failed = 0;

  if ((res_fine = repo_bank_get_fine_details (db, fine_id, &err)) != NULL)
    {
      if (db_res_step (res_fine, &err))
	{
	  fine_amount = db_res_col_i64 (res_fine, 0, &err);
	  fine_recipient_id = db_res_col_i32 (res_fine, 1, &err);
	  fine_status = db_res_col_text (res_fine, 2, &err);
	  fine_recipient_type = db_res_col_text (res_fine, 3, &err);

	  if (fine_recipient_id != ctx->player_id ||
	      (fine_recipient_type
	       && strcasecmp (fine_recipient_type, "player") != 0))
	    {
	      send_response_refused_steal (ctx,
					   root,
					   ERR_PERMISSION_DENIED,
					   "Fine does not belong to this player.",
					   NULL);
	      check_failed = 1;
	    }
	  else if (fine_status && strcasecmp (fine_status, "paid") == 0)
	    {
	      send_response_refused_steal (ctx,
					   root,
					   ERR_INVALID_ARG,
					   "Fine already paid.", NULL);
	      check_failed = 1;
	    }
	}
      else
        {
          send_response_error (ctx, root, 404, "Fine not found.");
          check_failed = 1;
        }
      db_res_finalize (res_fine);
    }
  else
    {
      send_response_error (ctx, root, ERR_DB,
			   "Database error retrieving fine.");
      return 0;
    }

  if (check_failed)
    {
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
			   ERR_DB, "Failed to retrieve player credits.");
      return 0;
    }
  if (player_credits < amount_to_pay)
    {
      send_response_refused_steal (ctx,
				   root,
				   ERR_INSUFFICIENT_FUNDS,
				   "Insufficient credits to pay fine.", NULL);
      return 0;
    }

  if (h_deduct_player_petty_cash_unlocked (db,
					   ctx->player_id,
					   amount_to_pay, NULL) != 0)
    {
      send_response_error (ctx, root, ERR_DB, "Failed to deduct credits.");
      return 0;
    }

  const char *new_status = (amount_to_pay == fine_amount) ? "paid" : "unpaid";
  if (repo_bank_update_fine (db, fine_id, new_status, amount_to_pay) != 0)
    {
      send_response_error (ctx,
			   root,
			   ERR_DB, "Database error preparing fine update.");
      h_add_player_petty_cash_unlocked (db, ctx->player_id, amount_to_pay, NULL);	// Refund
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
		       json_string ("Fine paid successfully."));
  json_object_set_new (response_data, "fine_id", json_integer (fine_id));
  json_object_set_new (response_data,
		       "amount_paid", json_integer (amount_to_pay));
  send_response_ok_take (ctx, root, "fine.pay.success", &response_data);
  return 0;
}
