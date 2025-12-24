
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


/* Bank Commands */
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
  long long start_date = 0, end_date = 0;
  long long min_amount = 0, max_amount = 0;
  json_t *transactions_array = NULL;
  
  db_t *db = game_db_get_handle();
  if (!db) {
      send_response_error(ctx, root, ERR_DB, "No database handle");
      return 0;
  }

  int rc = db_bank_get_transactions (db, "player",
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
  if (!db) {
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
  const char *sql_query =
    "SELECT P.name, BA.balance FROM bank_accounts BA JOIN players P ON P.id = BA.owner_id WHERE BA.owner_type = 'player' ORDER BY BA.balance DESC LIMIT $1;";
  db_bind_t params[] = { db_bind_i32(limit) };
  db_res_t *res = NULL;
  db_error_t err;
  json_t *leaderboard_array = json_array ();

  if (db_query(db, sql_query, params, 1, &res, &err)) {
      while (db_res_step(res, &err)) {
          json_t *entry = json_object ();
          json_object_set_new (entry, "player_name", json_string (db_res_col_text(res, 0, &err)));
          json_object_set_new (entry, "balance", json_integer (db_res_col_i64(res, 1, &err)));
          json_array_append_new (leaderboard_array, entry);
      }
      db_res_finalize(res);
  } else {
      LOGE ("cmd_bank_leaderboard: query failed: %s", err.message);
      send_response_error (ctx, root, ERR_DB, "Database error.");
      json_decref(leaderboard_array);
      return 0;
  }
  
  json_t *payload = json_object ();
  json_object_set (payload, "leaderboard", leaderboard_array);

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
  if (!db) {
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


  if (h_get_player_petty_cash (db, ctx->player_id,
                               &player_petty_cash) != 0)
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
  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) {
      send_response_error (ctx, root, err.code, "Database busy.");
      return 0;
  }

  if (h_deduct_player_petty_cash_unlocked (db, ctx->player_id, amount, NULL) != 0)
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
  if (h_get_account_id_unlocked(db, "player", ctx->player_id, &account_id) != 0 || account_id <= 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Account not found.");
      return 0;
    }

  if (h_add_credits_unlocked (db,
                     account_id,
                     amount, "DEPOSIT", NULL, &new_bank_balance) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Bank error.");
      return 0;
    }
  
  if (!db_tx_commit(db, &err)) {
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
  if (!db) {
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

  db_error_t err;
  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) {
      send_response_refused_steal (ctx, root, err.code, "Database busy.", NULL);
      return 0;
  }

  if (h_get_credits (db, "player", ctx->player_id,
                     &from_bal) != 0 || from_bal < amount)
    {
      db_tx_rollback (db, NULL);
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient funds", NULL);
      return 0;
    }
  char tx_grp[UUID_STR_LEN];


  h_generate_hex_uuid (tx_grp, sizeof (tx_grp));
  if (h_deduct_credits (db,
                        "player",
                        ctx->player_id,
                        amount, "TRANSFER", tx_grp, &from_bal) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_UNKNOWN, "Deduct failed");
      return 0;
    }
  if (h_add_credits (db, "player", to_id, amount, "TRANSFER", tx_grp,
                     &to_bal) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_UNKNOWN, "Add failed");
      return 0;
    }
  if (!db_tx_commit(db, &err)) {
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
                                   "Not authenticated", NULL);
      return 0;
    }
  db_t *db = game_db_get_handle ();
  if (!db) {
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
  long long amount = 0;
  json_t *j_amount = json_object_get (data, "amount");


  if (json_is_integer (j_amount))
    {
      amount = json_integer_value (j_amount);
    }
  if (amount <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD, "Missing or invalid 'amount'.");
      return 0;
    }
  long long new_balance = 0;

  db_error_t err;
  if (!db_tx_begin(db, DB_TX_IMMEDIATE, &err)) {
      send_response_refused_steal (ctx, root, err.code, "Database busy.", NULL);
      return 0;
  }

  int account_id = 0;
  if (h_get_account_id_unlocked(db, "player", ctx->player_id, &account_id) != 0 || account_id <= 0)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Account not found.");
      return 0;
    }

  if (h_deduct_credits_unlocked (db,
                        account_id,
                        amount,
                        "WITHDRAWAL", NULL, &new_balance) != 0)
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
  
  if (!db_tx_commit(db, &err)) {
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
  if (!db) {
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
  if (!db) {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
  }
  json_t *fines_array = json_array ();
  db_error_t err;

  const char *sql =
    "SELECT id, reason, amount, issued_ts, status FROM fines WHERE recipient_type = 'player' AND recipient_id = $1 AND status != 'paid';";
  db_bind_t params[] = { db_bind_i32(ctx->player_id) };
  db_res_t *res = NULL;

  if (db_query(db, sql, params, 1, &res, &err)) {
      while (db_res_step(res, &err)) {
          json_t *fine = json_object ();
          json_object_set_new (fine, "id", json_integer (db_res_col_i32(res, 0, &err)));
          json_object_set_new (fine, "reason", json_string (db_res_col_text(res, 1, &err)));
          json_object_set_new (fine, "amount", json_integer (db_res_col_i64(res, 2, &err)));
          json_object_set_new (fine, "issued_ts", json_string (db_res_col_text(res, 3, &err)));
          json_object_set_new (fine, "status", json_string (db_res_col_text(res, 4, &err)));
          json_array_append_new (fines_array, fine);
      }
      db_res_finalize(res);
  } else {
      LOGE ("cmd_fine_list: query failed: %s", err.message);
      send_response_error (ctx, root, ERR_DB, "Database error.");
      json_decref(fines_array);
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
  if (!db) {
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
                                                                 "amount"));    // Optional, if 0, pay full


  if (fine_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Missing or invalid fine_id.");
      return 0;
    }

  // Retrieve fine details
  long long fine_amount = 0;
  int fine_recipient_id = 0;
  const char *fine_status = NULL;
  const char *fine_recipient_type = NULL;

  db_error_t err;
  const char *sql_select_fine = "SELECT amount, recipient_id, status, recipient_type FROM fines WHERE id = $1;";
  db_bind_t params_fine[] = { db_bind_i32(fine_id) };
  db_res_t *res_fine = NULL;

  if (db_query(db, sql_select_fine, params_fine, 1, &res_fine, &err)) {
      if (db_res_step(res_fine, &err)) {
          fine_amount = db_res_col_i64(res_fine, 0, &err);
          fine_recipient_id = db_res_col_i32(res_fine, 1, &err);
          fine_status = db_res_col_text(res_fine, 2, &err);
          fine_recipient_type = db_res_col_text(res_fine, 3, &err);
      }
      db_res_finalize(res_fine);
  } else {
      send_response_error (ctx, root, ERR_DB, "Database error retrieving fine.");
      return 0;
  }
  
  if (fine_recipient_id != ctx->player_id ||
      (fine_recipient_type && strcasecmp (fine_recipient_type,
                                          "player") != 0))
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
                                   "Fine already paid.", NULL);
      return 0;
    }

  if (amount_to_pay <= 0 || amount_to_pay > fine_amount)
    {
      amount_to_pay = fine_amount;      // Pay full amount if not specified or invalid
    }

  // Check player credits
  long long player_credits = 0;

  if (h_get_player_petty_cash (db, ctx->player_id,
                               &player_credits) != 0)
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
                                   "Insufficient credits to pay fine.", NULL);
      return 0;
    }

  // Deduct credits and update fine status
  if (h_deduct_player_petty_cash_unlocked (db,
                                           ctx->player_id,
                                           amount_to_pay, NULL) != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Failed to deduct credits for fine payment.");
      return 0;
    }

  // Update fine status
  const char *new_status = (amount_to_pay == fine_amount) ? "paid" : "unpaid";
  const char *sql_update_fine =
    "UPDATE fines SET status = $1, amount = amount - $2 WHERE id = $3;";
  db_bind_t params_update[] = { db_bind_text(new_status), db_bind_i64(amount_to_pay), db_bind_i32(fine_id) };

  if (!db_exec(db, sql_update_fine, params_update, 3, &err)) {
      send_response_error (ctx, root, ERR_DB, "Database error preparing fine update.");
      h_add_player_petty_cash_unlocked (db, ctx->player_id, amount_to_pay, NULL); // Refund
      return 0;
  }
  
  json_t *response_data = json_object ();

  json_object_set_new (response_data, "message",
                       json_string ("Fine paid successfully."));
  json_object_set_new (response_data, "fine_id", json_integer (fine_id));
  json_object_set_new (response_data, "amount_paid",
                       json_integer (amount_to_pay));
  send_response_ok_take (ctx, root, "fine.pay.success", &response_data);
  return 0;
}

