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
  int rc = db_bank_get_transactions ("player",
                                     ctx->player_id,
                                     limit,
                                     tx_type_filter,
                                     start_date,
                                     end_date,
                                     min_amount,
                                     max_amount,
                                     &transactions_array);


  if (rc != SQLITE_OK)
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
  sqlite3 *db = db_get_handle ();
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
    "SELECT P.name, BA.balance FROM bank_accounts BA JOIN players P ON P.id = BA.owner_id WHERE BA.owner_type = 'player' ORDER BY BA.balance DESC LIMIT ?1;";
  sqlite3_stmt *stmt = NULL;


  if (sqlite3_prepare_v2 (db, sql_query, -1, &stmt, NULL) != SQLITE_OK)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Database error.");
      return 0;
    }
  sqlite3_bind_int (stmt, 1, limit);
  json_t *leaderboard_array = json_array ();


  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      json_t *entry = json_object ();


      json_object_set_new (entry, "player_name",
                           json_string ((const char *)
                                        sqlite3_column_text (stmt, 0)));
      json_object_set_new (entry, "balance",
                           json_integer (sqlite3_column_int64 (stmt, 1)));
      json_array_append_new (leaderboard_array, entry);
    }
  sqlite3_finalize (stmt);
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
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated", NULL);
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
  sqlite3 *db = db_get_handle ();
  long long player_petty_cash = 0;


  if (h_get_player_petty_cash (db, ctx->player_id,
                               &player_petty_cash) != SQLITE_OK)
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
  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
  if (h_deduct_player_petty_cash (db, ctx->player_id, amount,
                                  NULL) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient funds.", NULL);
      return 0;
    }
  long long new_bank_balance = 0;


  if (h_add_credits (db,
                     "player",
                     ctx->player_id,
                     amount, "DEPOSIT", NULL, &new_bank_balance) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Bank error.");
      return 0;
    }
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
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
  sqlite3 *db = db_get_handle ();
  long long from_bal = 0, to_bal = 0;


  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
  if (h_get_credits (db, "player", ctx->player_id,
                     &from_bal) != SQLITE_OK || from_bal < amount)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
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
                        amount, "TRANSFER", tx_grp, &from_bal) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_response_error (ctx, root, ERR_UNKNOWN, "Deduct failed");
      return 0;
    }
  if (h_add_credits (db, "player", to_id, amount, "TRANSFER", tx_grp,
                     &to_bal) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_response_error (ctx, root, ERR_UNKNOWN, "Add failed");
      return 0;
    }
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
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
  json_t *data = json_object_get (root, "data");
  json_t *j_amt = json_object_get (data, "amount");


  if (!json_is_integer (j_amt) || json_integer_value (j_amt) <= 0)
    {
      send_response_refused_steal (ctx, root, ERR_INVALID_ARG,
                                   "Invalid amount", NULL);
      return 0;
    }
  long long amount = json_integer_value (j_amt);
  sqlite3 *db = db_get_handle ();
  long long new_balance = 0;


  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
  if (h_deduct_credits (db,
                        "player",
                        ctx->player_id,
                        amount,
                        "WITHDRAWAL", NULL, &new_balance) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient funds", NULL);
      return 0;
    }
  if (h_add_player_petty_cash (db, ctx->player_id, amount, NULL) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_response_error (ctx, root, ERR_UNKNOWN,
                           "Petty cash update failed");
      return 0;
    }
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
  json_t *payload = json_object ();


  json_object_set_new (payload, "new_balance", json_integer (new_balance));


  send_response_ok_take (ctx, root, "bank.withdraw.confirmed", &payload);       // Assuming type name
  return 0;
}


int
cmd_bank_balance (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_SECTOR_NOT_FOUND,
                                   "Not authenticated", NULL);
      return 0;
    }
  long long balance = 0;


  if (db_get_player_bank_balance (ctx->player_id, &balance) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR, "Error retrieving balance.");
      return 0;
    }
  json_t *payload = json_object ();


  json_object_set_new (payload, "balance", json_integer (balance));
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

  sqlite3 *db = db_get_handle ();
  json_t *fines_array = json_array ();
  sqlite3_stmt *st = NULL;

  // Corrected SQL query to use 'issued_ts' and reflect available columns
  const char *sql =
    "SELECT id, reason, amount, issued_ts, status FROM fines WHERE recipient_type = 'player' AND recipient_id = ?1 AND status != 'paid';";


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
      json_t *fine = json_object ();


      json_object_set_new (fine, "id",
                           json_integer (sqlite3_column_int (st, 0)));
      json_object_set_new (fine, "reason",
                           json_string ((const char *)
                                        sqlite3_column_text (st, 1)));
      json_object_set_new (fine, "amount",
                           json_integer (sqlite3_column_int64 (st, 2)));
      json_object_set_new (fine, "issued_ts",
                           json_string ((const char *) sqlite3_column_text (st,
                                                                            3)));
      // Corrected to issued_ts and read as TEXT
      json_object_set_new (fine, "status",
                           json_string ((const char *)
                                        sqlite3_column_text (st, 4)));
      json_array_append_new (fines_array, fine);
    }
  sqlite3_finalize (st);

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

  sqlite3 *db = db_get_handle ();
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
  sqlite3_stmt *st_fine = NULL;
  const char *sql_select_fine =
    "SELECT amount, recipient_id, status, recipient_type FROM fines WHERE id = ?1;";


  if (sqlite3_prepare_v2 (db, sql_select_fine, -1, &st_fine, NULL) !=
      SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED,
                           "Database error retrieving fine.");
      return 0;
    }
  sqlite3_bind_int (st_fine, 1, fine_id);

  if (sqlite3_step (st_fine) != SQLITE_ROW)
    {
      sqlite3_finalize (st_fine);
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_NOT_FOUND, "Fine not found.", NULL);
      return 0;
    }

  long long fine_amount = sqlite3_column_int64 (st_fine, 0);
  int fine_recipient_id = sqlite3_column_int (st_fine, 1);
  const char *tmp_status = (const char *) sqlite3_column_text (st_fine, 2);
  const char *tmp_type = (const char *) sqlite3_column_text (st_fine, 3);
  /* sqlite: column_text() pointer invalid after finalize/reset/step */
  char *fine_status = tmp_status ? strdup (tmp_status) : NULL;
  char *fine_recipient_type = tmp_type ? strdup (tmp_type) : NULL;


  sqlite3_finalize (st_fine);

  if (fine_recipient_id != ctx->player_id ||
      (fine_recipient_type && strcasecmp (fine_recipient_type,
                                          "player") != 0))
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_PERMISSION_DENIED,
                                   "Fine does not belong to this player.",
                                   NULL);
      free (fine_status);
      free (fine_recipient_type);
      return 0;
    }
  if (fine_status && strcasecmp (fine_status, "paid") == 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INVALID_ARG,
                                   "Fine already paid.", NULL);
      free (fine_status);
      free (fine_recipient_type);
      return 0;
    }
  free (fine_status);
  free (fine_recipient_type);

  if (amount_to_pay <= 0 || amount_to_pay > fine_amount)
    {
      amount_to_pay = fine_amount;      // Pay full amount if not specified or invalid
    }

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
                                           amount_to_pay, NULL) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Failed to deduct credits for fine payment.");
      return 0;
    }

  // Update fine status
  sqlite3_stmt *st_update = NULL;
  const char *new_status = (amount_to_pay == fine_amount) ? "paid" : "unpaid";  // Use 'unpaid' if partially paid
  const char *sql_update_fine =
    "UPDATE fines SET status = ?, amount = amount - ? WHERE id = ?;";                                   // Reduce amount in DB


  if (sqlite3_prepare_v2 (db, sql_update_fine, -1, &st_update,
                          NULL) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED,
                           "Database error preparing fine update.");
      // Refund credits if update fails
      h_add_player_petty_cash_unlocked (db, ctx->player_id, amount_to_pay,
                                        NULL);
      return 0;
    }

  sqlite3_bind_text (st_update, 1, new_status, -1, SQLITE_STATIC);
  sqlite3_bind_int64 (st_update, 2, amount_to_pay);
  sqlite3_bind_int (st_update, 3, fine_id);

  if (sqlite3_step (st_update) != SQLITE_DONE)
    {
      send_response_error (ctx, root, ERR_DB,
                           "Failed to update fine status.");
      h_add_player_petty_cash_unlocked (db, ctx->player_id, amount_to_pay,
                                        NULL);                                          // Refund
      sqlite3_finalize (st_update);
      return 0;
    }
  sqlite3_finalize (st_update);

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Fine paid successfully."));
  json_object_set_new (response_data, "fine_id", json_integer (fine_id));
  json_object_set_new (response_data,
                       "amount_paid", json_integer (amount_to_pay));
  send_response_ok_take (ctx, root, "fine.pay", &response_data);
  return 0;
}

