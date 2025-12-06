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
      send_enveloped_refused (ctx->fd,
                              root,
                              ERR_NOT_AUTHENTICATED,
                              "Not authenticated",
                              NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_enveloped_error (ctx->fd,
                            root,
                            ERR_INVALID_SCHEMA,
                            "data must be object");
      return 0;
    }
  int limit = 20;
  json_t *j_limit = json_object_get (data, "limit");


  if (json_is_integer (j_limit))
    {
      limit = (int)json_integer_value (j_limit);
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
      send_enveloped_error (ctx->fd,
                            root,
                            ERR_DB_QUERY_FAILED,
                            "Failed to retrieve history.");
      if (transactions_array)
        {
          json_decref (transactions_array);
        }
      return 0;
    }
  json_t *payload = json_pack ("{s:o, s:b}",
                               "history",
                               transactions_array,
                               "has_next_page",
                               json_false ());


  send_enveloped_ok (ctx->fd, root, "bank.history.response", payload);
  json_decref (payload);
  return 0;
}


int
cmd_bank_leaderboard (client_ctx_t *ctx, json_t *root)
{
  sqlite3 *db = db_get_handle ();
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd,
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
    "SELECT P.name, BA.balance FROM bank_accounts BA JOIN players P ON P.id = BA.owner_id WHERE BA.owner_type = 'player' ORDER BY BA.balance DESC LIMIT ?1;";
  sqlite3_stmt *stmt = NULL;


  if (sqlite3_prepare_v2 (db, sql_query, -1, &stmt, NULL) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, "Database error.");
      return 0;
    }
  sqlite3_bind_int (stmt, 1, limit);
  json_t *leaderboard_array = json_array ();


  while (sqlite3_step (stmt) == SQLITE_ROW)
    {
      json_t *entry = json_object ();


      json_object_set_new (entry, "player_name",
                           json_string ((const char *)sqlite3_column_text (stmt,
                                                                           0)));
      json_object_set_new (entry, "balance",
                           json_integer (sqlite3_column_int64 (stmt, 1)));
      json_array_append_new (leaderboard_array, entry);
    }
  sqlite3_finalize (stmt);
  json_t *payload = json_pack ("{s:o}", "leaderboard", leaderboard_array);


  send_enveloped_ok (ctx->fd, root, "bank.leaderboard.response", payload);
  json_decref (payload);
  return 0;
}


int
cmd_bank_deposit (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *j_amount = json_object_get (data, "amount");


  if (!json_is_integer (j_amount) || json_integer_value (j_amount) <= 0)
    {
      send_enveloped_error (ctx->fd, root, 400, "Invalid amount.");
      return 0;
    }
  long long amount = json_integer_value (j_amount);
  sqlite3 *db = db_get_handle ();
  long long player_petty_cash = 0;


  if (h_get_player_petty_cash (db, ctx->player_id,
                               &player_petty_cash) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, "Failed to retrieve balance.");
      return 0;
    }
  if (player_petty_cash < amount)
    {
      send_enveloped_refused (ctx->fd,
                              root,
                              ERR_INSUFFICIENT_FUNDS,
                              "Insufficient petty cash.",
                              NULL);
      return 0;
    }
  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
  if (h_deduct_player_petty_cash (db, ctx->player_id, amount,
                                  NULL) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_refused (ctx->fd,
                              root,
                              ERR_INSUFFICIENT_FUNDS,
                              "Insufficient funds.",
                              NULL);
      return 0;
    }
  long long new_bank_balance = 0;


  if (h_add_credits (db,
                     "player",
                     ctx->player_id,
                     amount,
                     "DEPOSIT",
                     NULL,
                     &new_bank_balance) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, 500, "Bank error.");
      return 0;
    }
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
  json_t *payload = json_pack ("{s:i, s:I}",
                               "player_id",
                               ctx->player_id,
                               "new_balance",
                               new_bank_balance);


  send_enveloped_ok (ctx->fd, root, "bank.deposit.confirmed", payload);
  json_decref (payload);
  return 0;
}


int
cmd_bank_transfer (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd,
                              root,
                              ERR_NOT_AUTHENTICATED,
                              "Not authenticated",
                              NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *j_to = json_object_get (data, "to_player_id");
  json_t *j_amt = json_object_get (data, "amount");


  if (!json_is_integer (j_to) || !json_is_integer (j_amt))
    {
      send_enveloped_refused (ctx->fd,
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
      send_enveloped_refused (ctx->fd,
                              root,
                              ERR_INVALID_ARG,
                              "Invalid amount or recipient",
                              NULL);
      return 0;
    }
  sqlite3 *db = db_get_handle ();
  long long from_bal = 0, to_bal = 0;


  sqlite3_exec (db, "BEGIN IMMEDIATE;", NULL, NULL, NULL);
  if (h_get_credits (db, "player", ctx->player_id,
                     &from_bal) != SQLITE_OK || from_bal < amount)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_refused (ctx->fd,
                              root,
                              ERR_INSUFFICIENT_FUNDS,
                              "Insufficient funds",
                              NULL);
      return 0;
    }
  char tx_grp[UUID_STR_LEN];


  h_generate_hex_uuid (tx_grp, sizeof (tx_grp));
  if (h_deduct_credits (db,
                        "player",
                        ctx->player_id,
                        amount,
                        "TRANSFER",
                        tx_grp,
                        &from_bal) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "Deduct failed");
      return 0;
    }
  if (h_add_credits (db, "player", to_id, amount, "TRANSFER", tx_grp,
                     &to_bal) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd, root, ERR_UNKNOWN, "Add failed");
      return 0;
    }
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
  json_t *payload = json_pack ("{s:i, s:i, s:I, s:I}",
                               "from_player_id",
                               ctx->player_id,
                               "to_player_id",
                               to_id,
                               "from_balance",
                               from_bal,
                               "to_balance",
                               to_bal);


  send_enveloped_ok (ctx->fd, root, "bank.transfer.confirmed", payload);
  json_decref (payload);
  return 0;
}


int
cmd_bank_withdraw (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd,
                              root,
                              ERR_NOT_AUTHENTICATED,
                              "Not authenticated",
                              NULL);
      return 0;
    }
  json_t *data = json_object_get (root, "data");
  json_t *j_amt = json_object_get (data, "amount");


  if (!json_is_integer (j_amt) || json_integer_value (j_amt) <= 0)
    {
      send_enveloped_refused (ctx->fd,
                              root,
                              ERR_INVALID_ARG,
                              "Invalid amount",
                              NULL);
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
                        "WITHDRAWAL",
                        NULL,
                        &new_balance) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_refused (ctx->fd,
                              root,
                              ERR_INSUFFICIENT_FUNDS,
                              "Insufficient funds",
                              NULL);
      return 0;
    }
  if (h_add_player_petty_cash (db, ctx->player_id, amount, NULL) != SQLITE_OK)
    {
      sqlite3_exec (db, "ROLLBACK;", NULL, NULL, NULL);
      send_enveloped_error (ctx->fd,
                            root,
                            ERR_UNKNOWN,
                            "Petty cash update failed");
      return 0;
    }
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
  json_t *payload = json_pack ("{s:I}", "new_balance", new_balance);


  send_enveloped_ok (ctx->fd, root, "bank.withdraw.confirmed", payload); // Assuming type name
  json_decref (payload);
  return 0;
}


int
cmd_bank_balance (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_enveloped_refused (ctx->fd, root, 1401, "Not authenticated", NULL);
      return 0;
    }
  long long balance = 0;


  if (db_get_player_bank_balance (ctx->player_id, &balance) != SQLITE_OK)
    {
      send_enveloped_error (ctx->fd, root, 500, "Error retrieving balance.");
      return 0;
    }
  json_t *payload = json_object ();


  json_object_set_new (payload, "balance", json_integer (balance));
  send_enveloped_ok (ctx->fd, root, "bank.balance", payload);
  return 0;
}

