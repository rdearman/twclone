#include <jansson.h>
#include <sqlite3.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <strings.h>
#include <pthread.h>
#include <ctype.h>              // Required for isalnum and isupper
#include "server_corporation.h"
#include "server_config.h"
#include "database.h"
#include "server_log.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_players.h"
#include "common.h"
#include "server_cron.h"
extern pthread_mutex_t db_mutex;


int
h_get_player_corp_role (sqlite3 *db, int player_id, int corp_id,
                        char *role_buffer, size_t buffer_size)
{
  sqlite3_stmt *st = NULL;
  const char *sql =
    "SELECT role FROM corp_members WHERE player_id = ? AND corp_id = ?;";
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("h_get_player_corp_role: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (st, 1, player_id);
  sqlite3_bind_int (st, 2, corp_id);
  int rc = sqlite3_step (st);


  if (rc == SQLITE_ROW)
    {
      const char *role = (const char *) sqlite3_column_text (st, 0);


      if (role)
        {
          strncpy (role_buffer, role, buffer_size - 1);
          role_buffer[buffer_size - 1] = '\0';
        }
      else
        {
          role_buffer[0] = '\0';
        }
      rc = SQLITE_OK;
    }
  else
    {
      role_buffer[0] = '\0';
      rc = SQLITE_NOTFOUND;
    }
  sqlite3_finalize (st);
  return rc;
}


int
h_is_player_corp_ceo (sqlite3 *db, int player_id, int *out_corp_id)
{
  if (!db || player_id <= 0)
    {
      return 0;
    }
  sqlite3_stmt *stmt = NULL;
  const char *sql = "SELECT id FROM corporations " "WHERE owner_id = ?;";
  int rc = sqlite3_prepare_v2 (db, sql, -1, &stmt, NULL);


  if (rc != SQLITE_OK)
    {
      return 0;
    }
  sqlite3_bind_int (stmt, 1, player_id);
  rc = sqlite3_step (stmt);
  int found = 0;


  if (rc == SQLITE_ROW)
    {
      int corp_id = sqlite3_column_int (stmt, 0);


      if (out_corp_id)
        {
          *out_corp_id = corp_id;
        }
      found = 1;
    }
  sqlite3_finalize (stmt);
  return found;
}


int
h_get_player_corp_id (sqlite3 *db, int player_id)
{
  sqlite3_stmt *st = NULL;
  int corp_id = 0;
  const char *sql = "SELECT corp_id FROM corp_members WHERE player_id = ?;";
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("h_get_player_corp_id: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      return 0;
    }
  sqlite3_bind_int (st, 1, player_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      corp_id = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  return corp_id;
}


int
h_get_corp_bank_account_id (sqlite3 *db, int corp_id)
{
  sqlite3_stmt *st = NULL;
  int account_id = -1;
  const char *sql =
    "SELECT id FROM bank_accounts WHERE owner_type = 'corp' AND owner_id = ?;";
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("h_get_corp_bank_account_id: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      return -1;
    }
  sqlite3_bind_int (st, 1, corp_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      account_id = sqlite3_column_int (st, 0);
    }
  sqlite3_finalize (st);
  return account_id;
}


int
h_get_corp_credit_rating (sqlite3 *db, int corp_id, int *rating)
{
  sqlite3_stmt *st = NULL;
  int rc = SQLITE_NOTFOUND;
  const char *sql = "SELECT credit_rating FROM corporations WHERE id = ?;";
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("h_get_corp_credit_rating: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (st, 1, corp_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      if (rating)
        {
          *rating = sqlite3_column_int (st, 0);
        }
      rc = SQLITE_OK;
    }
  else
    {
      if (rating)
        {
          *rating = 0;          // Or some default value indicating not found
        }
      rc = SQLITE_NOTFOUND;
    }
  sqlite3_finalize (st);
  return rc;
}


int
h_get_corp_stock_id (sqlite3 *db, int corp_id, int *out_stock_id)
{
  sqlite3_stmt *st = NULL;
  const char *sql = "SELECT id FROM stocks WHERE corp_id = ?;";
  int rc = SQLITE_NOTFOUND;
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("h_get_corp_stock_id: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (st, 1, corp_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      if (out_stock_id)
        {
          *out_stock_id = sqlite3_column_int (st, 0);
        }
      rc = SQLITE_OK;
    }
  else
    {
      if (out_stock_id)
        {
          *out_stock_id = 0;
        }
      rc = SQLITE_NOTFOUND;
    }
  sqlite3_finalize (st);
  return rc;
}


int
h_get_stock_info (sqlite3 *db, int stock_id, char **out_ticker,
                  int *out_corp_id, int *out_total_shares,
                  int *out_par_value, int *out_current_price,
                  long long *out_last_dividend_ts)
{
  sqlite3_stmt *st = NULL;
  const char *sql =
    "SELECT ticker, corp_id, total_shares, par_value, current_price, last_dividend_ts FROM stocks WHERE id = ?;";
  int rc = SQLITE_NOTFOUND;
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("h_get_stock_info: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      return SQLITE_ERROR;
    }
  sqlite3_bind_int (st, 1, stock_id);
  if (sqlite3_step (st) == SQLITE_ROW)
    {
      if (out_ticker)
        {
          *out_ticker = strdup ((const char *) sqlite3_column_text (st, 0));
        }
      if (out_corp_id)
        {
          *out_corp_id = sqlite3_column_int (st, 1);
        }
      if (out_total_shares)
        {
          *out_total_shares = sqlite3_column_int (st, 2);
        }
      if (out_par_value)
        {
          *out_par_value = sqlite3_column_int (st, 3);
        }
      if (out_current_price)
        {
          *out_current_price = sqlite3_column_int (st, 4);
        }
      if (out_last_dividend_ts)
        {
          *out_last_dividend_ts = sqlite3_column_int64 (st, 5);
        }
      rc = SQLITE_OK;
    }
  sqlite3_finalize (st);
  return rc;
}


int
h_update_player_shares (sqlite3 *db, int player_id, int stock_id,
                        int quantity_change)
{
  sqlite3_stmt *st = NULL;
  int rc;
  if (quantity_change == 0)
    {
      return SQLITE_OK;         // No change needed
    }
  if (quantity_change > 0)
    {
      // Add shares, or insert if not exists
      const char *sql_add =
        "INSERT INTO corp_shareholders (player_id, corp_id, shares) "
        "VALUES (?, (SELECT corp_id FROM stocks WHERE id = ?), ?) "
        "ON CONFLICT(player_id, corp_id) DO UPDATE SET shares = shares + excluded.shares;";


      rc = sqlite3_prepare_v2 (db, sql_add, -1, &st, NULL);
      if (rc != SQLITE_OK)
        {
          LOGE
          (
            "h_update_player_shares: Failed to prepare add shares statement: %s",
            sqlite3_errmsg (db));
          return SQLITE_ERROR;
        }
      sqlite3_bind_int (st, 1, player_id);
      sqlite3_bind_int (st, 2, stock_id);
      sqlite3_bind_int (st, 3, quantity_change);
    }
  else
    {
      // Deduct shares, ensure not negative
      const char *sql_deduct =
        "UPDATE corp_shareholders SET shares = shares + ? "
        "WHERE player_id = ? AND corp_id = (SELECT corp_id FROM stocks WHERE id = ?) AND (shares + ?) >= 0;";


      rc = sqlite3_prepare_v2 (db, sql_deduct, -1, &st, NULL);
      if (rc != SQLITE_OK)
        {
          LOGE
          (
            "h_update_player_shares: Failed to prepare deduct shares statement: %s",
            sqlite3_errmsg (db));
          return SQLITE_ERROR;
        }
      sqlite3_bind_int (st, 1, quantity_change);        // quantity_change is negative
      sqlite3_bind_int (st, 2, player_id);
      sqlite3_bind_int (st, 3, stock_id);
      sqlite3_bind_int (st, 4, quantity_change);
    }
  rc = sqlite3_step (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("h_update_player_shares: Failed to execute statement: %s",
            sqlite3_errmsg (db));
      sqlite3_finalize (st);
      return SQLITE_ERROR;
    }
  if (sqlite3_changes (db) == 0 && quantity_change < 0)
    {
      // If no rows were updated for a deduction, it means insufficient shares
      LOGW
      (
        "h_update_player_shares: Player %d has insufficient shares for stock %d to deduct %d.",
        player_id,
        stock_id,
        -quantity_change);
      sqlite3_finalize (st);
      return SQLITE_CONSTRAINT; // Indicate insufficient shares
    }
  sqlite3_finalize (st);
  // Clean up 0-share entries
  const char *sql_cleanup = "DELETE FROM corp_shareholders WHERE shares = 0;";


  sqlite3_exec (db, sql_cleanup, NULL, NULL, NULL);
  return SQLITE_OK;
}


int
cmd_corp_transfer_ceo (client_ctx_t *ctx, json_t *root)
{
  if (!ctx || ctx->player_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = root ? json_object_get (root, "data") : NULL;


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data payload.");
      return 0;
    }
  int target_player_id = 0;


  if (!json_get_int_flexible (data, "target_player_id", &target_player_id) ||
      target_player_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_MISSING_FIELD,
                           "Missing or invalid 'target_player_id'.");
      return 0;
    }
  if (target_player_id == ctx->player_id)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Cannot transfer CEO role to yourself.");
      return 0;
    }
  /* Ensure caller is an active CEO and grab corp_id */
  int corp_id = 0;


  if (!h_is_player_corp_ceo (db, ctx->player_id, &corp_id) || corp_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_PERMISSION_DENIED,
                           "Only active corporation CEOs may transfer leadership.");
      return 0;
    }
  /* Ensure target is a member of the same corp */
  sqlite3_stmt *stmt = NULL;
  const char *sql_check_member =
    "SELECT role FROM corp_members WHERE corp_id = ? AND player_id = ?;";
  int rc = sqlite3_prepare_v2 (db, sql_check_member, -1, &stmt, NULL);


  if (rc != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED,
                           "Failed to check membership.");
      return 0;
    }
  sqlite3_bind_int (stmt, 1, corp_id);
  sqlite3_bind_int (stmt, 2, target_player_id);
  const char *target_role = NULL;


  rc = sqlite3_step (stmt);
  if (rc == SQLITE_ROW)
    {
      target_role = (const char *) sqlite3_column_text (stmt, 0);
    }
  if (!target_role)
    {
      sqlite3_finalize (stmt);
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Target player is not a member of your corporation.");
      return 0;
    }
  sqlite3_finalize (stmt);
  stmt = NULL;
  /* Guard: current CEO must NOT be flying the Corporate Flagship */
  const char *sql_flagship_check =
    "SELECT st.name "
    "FROM players p "
    "JOIN ships s ON p.ship = s.id "
    "JOIN shiptypes st ON s.type_id = st.id " "WHERE p.id = ?;";


  rc = sqlite3_prepare_v2 (db, sql_flagship_check, -1, &stmt, NULL);
  if (rc != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB_QUERY_FAILED,
                           "Failed to check current ship type.");
      return 0;
    }
  sqlite3_bind_int (stmt, 1, ctx->player_id);
  rc = sqlite3_step (stmt);
  if (rc == SQLITE_ROW)
    {
      const char *ship_name = (const char *) sqlite3_column_text (stmt, 0);


      if (ship_name && !strcasecmp (ship_name, "Corporate Flagship"))
        {
          sqlite3_finalize (stmt);
          send_response_error (ctx,
                               root,
                               ERR_INVALID_CORP_STATE,
                               "You cannot transfer CEO while piloting the Corporate Flagship.");
          return 0;
        }
    }
  sqlite3_finalize (stmt);
  stmt = NULL;
  /* Perform the transfer in a transaction */
  rc = sqlite3_exec (db, "BEGIN IMMEDIATE TRANSACTION;", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      send_response_error (ctx, root, ERR_DB, "Failed to start transaction.");
      return 0;
    }
  int ok = 1;


  /* Demote current CEO to Officer */
  if (ok)
    {
      const char *sql_demote =
        "UPDATE corp_members "
        "SET role = 'Officer' "
        "WHERE corp_id = ? AND player_id = ? AND role = 'Leader';";


      rc = sqlite3_prepare_v2 (db, sql_demote, -1, &stmt, NULL);
      if (rc != SQLITE_OK)
        {
          ok = 0;
        }
    }
  if (ok)
    {
      sqlite3_bind_int (stmt, 1, corp_id);
      sqlite3_bind_int (stmt, 2, ctx->player_id);
      rc = sqlite3_step (stmt);
      if (rc != SQLITE_DONE)
        {
          ok = 0;
        }
    }
  if (stmt)
    {
      sqlite3_finalize (stmt);
      stmt = NULL;
    }
  /* Ensure target has a membership row */
  if (ok)
    {
      const char *sql_insert_member =
        "INSERT OR IGNORE INTO corp_members (corp_id, player_id, role) "
        "VALUES (?, ?, 'Member');";


      rc = sqlite3_prepare_v2 (db, sql_insert_member, -1, &stmt, NULL);
      if (rc != SQLITE_OK)
        {
          ok = 0;
        }
      if (ok)
        {
          sqlite3_bind_int (stmt, 1, corp_id);
          sqlite3_bind_int (stmt, 2, target_player_id);
          rc = sqlite3_step (stmt);
          if (rc != SQLITE_DONE)
            {
              ok = 0;
            }
        }
      if (stmt)
        {
          sqlite3_finalize (stmt);
          stmt = NULL;
        }
    }
  /* Promote target to Leader */
  if (ok)
    {
      const char *sql_promote =
        "UPDATE corp_members "
        "SET role = 'Leader' " "WHERE corp_id = ? AND player_id = ?;";


      rc = sqlite3_prepare_v2 (db, sql_promote, -1, &stmt, NULL);
      if (rc != SQLITE_OK)
        {
          ok = 0;
        }
      if (ok)
        {
          sqlite3_bind_int (stmt, 1, corp_id);
          sqlite3_bind_int (stmt, 2, target_player_id);
          rc = sqlite3_step (stmt);
          if (rc != SQLITE_DONE)
            {
              ok = 0;
            }
        }
      if (stmt)
        {
          sqlite3_finalize (stmt);
          stmt = NULL;
        }
    }
  /* Update corporations.owner_id */
  if (ok)
    {
      const char *sql_update_owner =
        "UPDATE corporations SET owner_id = ? WHERE id = ?;";


      rc = sqlite3_prepare_v2 (db, sql_update_owner, -1, &stmt, NULL);
      if (rc != SQLITE_OK)
        {
          ok = 0;
        }
      if (ok)
        {
          sqlite3_bind_int (stmt, 1, target_player_id);
          sqlite3_bind_int (stmt, 2, corp_id);
          rc = sqlite3_step (stmt);
          if (rc != SQLITE_DONE)
            {
              ok = 0;
            }
        }
      if (stmt)
        {
          sqlite3_finalize (stmt);
          stmt = NULL;
        }
    }
  if (!ok)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error (ctx, root, ERR_DB, "Failed to transfer CEO role.");
      return 0;
    }
  sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
  /* TODO: optionally write a corp_log entry about CEO transfer */
  json_t *resp = json_object ();


  json_object_set_new (resp, "corp_id", json_integer (corp_id));
  json_object_set_new (resp, "new_ceo_player_id",
                       json_integer (target_player_id));
  send_response_ok_take (ctx, root, "corp.transfer_ceo.success", &resp);
  return 0;
}


int
cmd_corp_create (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  const char *name;
  json_t *j_name = json_object_get (data,
                                    "name");


  if (!json_is_string (j_name) || (name = json_string_value (j_name)) == NULL
      || name[0] == '\0')
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_BAD_REQUEST,
                                   "Missing or invalid corporation name.",
                                   NULL);                                                                       // Fixed send_enveloped_refused
      return 0;
    }
  // Use ctx->player_id for player_id
  if (h_get_player_corp_id (db, ctx->player_id) > 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INVALID_ARG,
                                   "You are already a member of a corporation.",
                                   NULL);                                                                               // Fixed send_enveloped_refused
      return 0;
    }
  // Start a transaction for atomicity
  int rc =
    sqlite3_exec (db, "BEGIN IMMEDIATE TRANSACTION;", NULL, NULL, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("cmd_corp_create: Failed to start transaction: %s",
            sqlite3_errmsg (db));
      send_response_error (ctx,
                           root,
                           ERR_DB, "Database error starting transaction.");
      return 0;
    }
  long long creation_fee = g_cfg.corporation_creation_fee;
  long long player_new_balance;
  int player_bank_account_id = h_get_player_bank_account_id (db,
                                                             ctx->player_id);


  if (player_bank_account_id <= 0)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Could not retrieve player bank account for deduction.");
      return 0;
    }
  // Use h_deduct_credits_unlocked as we are inside a transaction
  if (h_deduct_credits_unlocked
        (db, player_bank_account_id, creation_fee, "CORP_CREATION_FEE", NULL,
        &player_new_balance) != SQLITE_OK)
    {
      db_safe_rollback (db, "Safe rollback");
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient funds to create a corporation.",
                                   NULL);                                                                                       // Fixed send_enveloped_refused
      return 0;
    }
  sqlite3_stmt *st = NULL;
  const char *sql_insert_corp =
    "INSERT INTO corporations (name, owner_id) VALUES (?, ?);";


  if (sqlite3_prepare_v2 (db, sql_insert_corp, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("cmd_corp_create: Failed to prepare corp insert: %s",
            sqlite3_errmsg (db));
      db_safe_rollback (db, "Safe rollback");
      // Refund credits if preparation failed
      h_add_credits_unlocked (db, player_bank_account_id, creation_fee,
                              "CORP_FEE_REFUND", NULL, &player_new_balance);
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Database error during corporation creation preparation.");
      return 0;
    }
  sqlite3_bind_text (st, 1, name, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 2, ctx->player_id);     // Changed from ctx->player->id
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  st = NULL;                    // Reset st after finalize
  if (rc != SQLITE_DONE)
    {
      LOGE ("cmd_corp_create: Failed to insert corporation: %s",
            sqlite3_errmsg (db));
      db_safe_rollback (db, "Safe rollback");
      // Refund credits
      h_add_credits_unlocked (db, player_bank_account_id, creation_fee,
                              "CORP_FEE_REFUND", NULL, &player_new_balance);
      if (sqlite3_errcode (db) == SQLITE_CONSTRAINT)
        {
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_NAME_TAKEN,
                                       "A corporation with that name already exists.",
                                       NULL);                                                                                   // Fixed send_enveloped_refused
        }
      else
        {
          send_response_error (ctx,
                               root,
                               ERR_DB,
                               "Database error inserting corporation.");
        }
      return 0;
    }
  int corp_id = (int) sqlite3_last_insert_rowid (db);
  // Explicitly insert player as Leader. Do not rely on trigger or manual fallback.
  const char *sql_insert_member =
    "INSERT INTO corp_members (corp_id, player_id, role) VALUES (?, ?, 'Leader');";


  if (sqlite3_prepare_v2 (db, sql_insert_member, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, corp_id);
      sqlite3_bind_int (st, 2, ctx->player_id); // Changed from ctx->player->id
      if (sqlite3_step (st) != SQLITE_DONE)
        {
          LOGE
          (
            "cmd_corp_create: Failed to insert player %d into corp_members for corp %d: %s",
            ctx->player_id,
            // Changed from ctx->player->id
            corp_id,
            sqlite3_errmsg (db));
          sqlite3_finalize (st);
          db_safe_rollback (db, "Safe rollback");
          h_add_credits_unlocked (db,
                                  player_bank_account_id,
                                  creation_fee,
                                  "CORP_FEE_REFUND",
                                  NULL,
                                  &player_new_balance);                                                                         // Fixed args
          send_response_error (ctx,
                               root,
                               ERR_DB,
                               "Database error adding CEO to corporation.");
          return 0;
        }
      sqlite3_finalize (st);
      st = NULL;
    }
  else
    {
      LOGE
      (
        "cmd_corp_create: Failed to prepare member insert for player %d into corp %d: %s",
        ctx->player_id,
        // Changed from ctx->player->id
        corp_id,
        sqlite3_errmsg (db));
      db_safe_rollback (db, "Safe rollback");
      h_add_credits_unlocked (db,
                              player_bank_account_id,
                              creation_fee,
                              "CORP_FEE_REFUND",
                              NULL,
                              &player_new_balance);                                                                     // Fixed args
      send_response_error (ctx,
                           root,
                           ERR_DB, "Database error preparing CEO addition.");
      return 0;
    }
  const char *sql_create_bank =
    "INSERT INTO bank_accounts (owner_type, owner_id, currency) VALUES ('corp', ?, 'CRD');";


  if (sqlite3_prepare_v2 (db, sql_create_bank, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, corp_id);
      if (sqlite3_step (st) != SQLITE_DONE)
        {
          LOGE
            ("cmd_corp_create: Failed to insert bank account for corp %d: %s",
            corp_id, sqlite3_errmsg (db));
          sqlite3_finalize (st);
          db_safe_rollback (db, "Safe rollback");
          h_add_credits_unlocked (db,
                                  player_bank_account_id,
                                  creation_fee,
                                  "CORP_FEE_REFUND",
                                  NULL,
                                  &player_new_balance);                                                                         // Fixed args
          send_response_error (ctx,
                               root,
                               ERR_DB,
                               "Database error creating bank account for corporation.");
          return 0;
        }
      sqlite3_finalize (st);
      st = NULL;
    }
  else
    {
      LOGE
      (
        "cmd_corp_create: Failed to prepare bank account creation for corp %d: %s",
        corp_id,
        sqlite3_errmsg (db));
      db_safe_rollback (db, "Safe rollback");
      h_add_credits_unlocked (db,
                              player_bank_account_id,
                              creation_fee,
                              "CORP_FEE_REFUND",
                              NULL,
                              &player_new_balance);                                                                     // Fixed args
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Database error preparing bank account creation.");
      return 0;
    }
  const char *sql_convert_planets =
    "UPDATE planets SET owner_id = ?, owner_type = 'corp' WHERE owner_id = ? AND owner_type = 'player';";


  if (sqlite3_prepare_v2 (db, sql_convert_planets, -1, &st, NULL) ==
      SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, corp_id);
      sqlite3_bind_int (st, 2, ctx->player_id); // Changed from ctx->player->id
      if (sqlite3_step (st) != SQLITE_DONE)
        {
          LOGE
          (
            "cmd_corp_create: Failed to update planet ownership for player %d: %s",
            ctx->player_id,
            // Changed from ctx->player->id
            sqlite3_errmsg (db));
          sqlite3_finalize (st);
          db_safe_rollback (db, "Safe rollback");
          h_add_credits_unlocked (db,
                                  player_bank_account_id,
                                  creation_fee,
                                  "CORP_FEE_REFUND",
                                  NULL,
                                  &player_new_balance);                                                                         // Fixed args
          send_response_error (ctx,
                               root,
                               ERR_DB,
                               "Database error updating planet ownership.");
          return 0;
        }
      sqlite3_finalize (st);
      st = NULL;
    }
  else
    {
      LOGE
      (
        "cmd_corp_create: Failed to prepare planet conversion for player %d: %s",
        ctx->player_id,
        // Changed from ctx->player->id
        sqlite3_errmsg (db));
      db_safe_rollback (db, "Safe rollback");
      h_add_credits_unlocked (db,
                              player_bank_account_id,
                              creation_fee,
                              "CORP_FEE_REFUND",
                              NULL,
                              &player_new_balance);                                                                     // Fixed args
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Database error preparing planet ownership update.");
      return 0;
    }
  // If all steps succeeded, commit the transaction
  rc = sqlite3_exec (db, "COMMIT;", NULL, NULL, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("cmd_corp_create: Failed to commit transaction: %s",
            sqlite3_errmsg (db));
      db_safe_rollback (db, "Safe rollback");   // Attempt rollback on commit failure
      h_add_credits_unlocked (db,
                              player_bank_account_id,
                              creation_fee,
                              "CORP_FEE_REFUND",
                              NULL,
                              &player_new_balance);                                                                     // Fixed args
      send_response_error (ctx,
                           root,
                           ERR_DB, "Database error committing transaction.");
      return 0;
    }
  // Only update ctx->corp_id AFTER successful commit
  ctx->corp_id = corp_id;
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "name", json_string (name));
  json_object_set_new (response_data, "message",
                       json_string ("Corporation created successfully."));
  send_response_ok_take (ctx, root, "corp.create.success", &response_data);
  return 0;
}


int
cmd_corp_list (client_ctx_t *ctx, json_t *root)
{
  (void) ctx;
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  json_t *corp_array = json_array ();
  const char *sql =
    "SELECT c.id, c.name, c.tag, c.owner_id, p.name, "
    "(SELECT COUNT(*) FROM corp_members cm WHERE cm.corp_id = c.id) as member_count "
    "FROM corporations c "
    "LEFT JOIN players p ON c.owner_id = p.id " "WHERE c.id > 0;";


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("cmd_corp_list: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      json_decref (corp_array);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error while fetching corporation list.");
      return 0;
    }
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      json_t *corp_obj = json_object ();


      json_object_set_new (corp_obj, "corp_id",
                           json_integer (sqlite3_column_int (st, 0)));
      json_object_set_new (corp_obj, "name",
                           json_string ((const char *)
                                        sqlite3_column_text (st, 1)));
      const char *tag = (const char *) sqlite3_column_text (st, 2);


      if (tag)
        {
          json_object_set_new (corp_obj, "tag", json_string (tag));
        }
      json_object_set_new (corp_obj, "ceo_id",
                           json_integer (sqlite3_column_int (st, 3)));
      const char *ceo_name = (const char *) sqlite3_column_text (st, 4);


      if (ceo_name)
        {
          json_object_set_new (corp_obj, "ceo_name", json_string (ceo_name));
        }
      json_object_set_new (corp_obj, "member_count",
                           json_integer (sqlite3_column_int (st, 5)));
      json_array_append_new (corp_array, corp_obj);
    }
  sqlite3_finalize (st);
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corporations", corp_array);
  send_response_ok_take (ctx, root, "corp.list.success", &response_data);
  return 0;
}


int
cmd_corp_roster (client_ctx_t *ctx, json_t *root)
{
  (void) ctx;
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  int corp_id;
  json_t *j_corp_id = json_object_get (data, "corp_id");


  if (!json_is_integer (j_corp_id))
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Missing or invalid 'corp_id'.");
      return 0;
    }
  corp_id = json_integer_value (j_corp_id);
  json_t *roster_array = json_array ();
  const char *sql =
    "SELECT cm.player_id, p.name, cm.role "
    "FROM corp_members cm "
    "JOIN players p ON cm.player_id = p.id " "WHERE cm.corp_id = ?;";


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      LOGE ("cmd_corp_roster: Failed to prepare statement: %s",
            sqlite3_errmsg (db));
      json_decref (roster_array);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error while fetching roster.");
      return 0;
    }
  sqlite3_bind_int (st, 1, corp_id);
  while (sqlite3_step (st) == SQLITE_ROW)
    {
      json_t *member_obj = json_object ();


      json_object_set_new (member_obj, "player_id",
                           json_integer (sqlite3_column_int (st, 0)));
      json_object_set_new (member_obj, "name",
                           json_string ((const char *)
                                        sqlite3_column_text (st, 1)));
      json_object_set_new (member_obj, "role",
                           json_string ((const char *)
                                        sqlite3_column_text (st, 2)));
      json_array_append_new (roster_array, member_obj);
    }
  sqlite3_finalize (st);
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "roster", roster_array);
  send_response_ok_take (ctx, root, "corp.roster.success", &response_data);
  return 0;
}


int
cmd_corp_leave (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  char role[16];


  h_get_player_corp_role (db, ctx->player_id, corp_id, role, sizeof (role));
  if (strcasecmp (role, "Leader") == 0)
    {
      int member_count = 0;
      const char *sql_count =
        "SELECT COUNT(*) FROM corp_members WHERE corp_id = ?;";


      if (sqlite3_prepare_v2 (db, sql_count, -1, &st, NULL) == SQLITE_OK)
        {
          sqlite3_bind_int (st, 1, corp_id);
          if (sqlite3_step (st) == SQLITE_ROW)
            {
              member_count = sqlite3_column_int (st, 0);
            }
          sqlite3_finalize (st);
        }
      if (member_count > 1)
        {
          send_response_error (ctx,
                               root,
                               ERR_INVALID_ARG,
                               "You must transfer leadership before leaving the corporation.");
          return 0;
        }
      const char *sql_delete_corp = "DELETE FROM corporations WHERE id = ?;";


      if (sqlite3_prepare_v2 (db, sql_delete_corp, -1, &st, NULL) ==
          SQLITE_OK)
        {
          sqlite3_bind_int (st, 1, corp_id);
          sqlite3_step (st);
          sqlite3_finalize (st);
        }
      json_t *response_data = json_object ();


      json_object_set_new (response_data, "message",
                           json_string
                           (
                             "You were the last member. The corporation has been dissolved."));
      send_response_ok_take (ctx, root, "corp.leave.dissolved",
                             &response_data);
    }
  else
    {
      const char *sql_delete_member =
        "DELETE FROM corp_members WHERE corp_id = ? AND player_id = ?;";


      if (sqlite3_prepare_v2 (db, sql_delete_member, -1, &st, NULL) ==
          SQLITE_OK)
        {
          sqlite3_bind_int (st, 1, corp_id);
          sqlite3_bind_int (st, 2, ctx->player_id);
          sqlite3_step (st);
          sqlite3_finalize (st);
        }
      json_t *response_data = json_object ();


      json_object_set_new (response_data, "message",
                           json_string ("You have left the corporation."));
      send_response_ok_take (ctx, root, "corp.leave.success", &response_data);
    }
  ctx->corp_id = 0;
  return 0;
}


int
cmd_corp_invite (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  int target_player_id;
  json_t *j_target_id = json_object_get (data, "target_player_id");


  if (!json_is_integer (j_target_id))
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid 'target_player_id'.");
      return 0;
    }
  target_player_id = json_integer_value (j_target_id);
  if (ctx->player_id == target_player_id)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG, "You cannot invite yourself.");
      return 0;
    }
  int inviter_corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (inviter_corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "You must be in a corporation to send invites.");
      return 0;
    }
  char inviter_role[16];


  h_get_player_corp_role (db,
                          ctx->player_id,
                          inviter_corp_id,
                          inviter_role, sizeof (inviter_role));
  if (strcasecmp (inviter_role, "Leader") != 0
      && strcasecmp (inviter_role, "Officer") != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_PERMISSION_DENIED,
                           "You must be a Leader or Officer to invite players.");
      return 0;
    }
  if (h_get_player_corp_id (db, target_player_id) > 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "The player you are trying to invite is already in a corporation.");
      return 0;
    }
  long long expires_at = (long long) time (NULL) + 86400;
  const char *sql_insert_invite =
    "INSERT OR REPLACE INTO corp_invites (corp_id, player_id, expires_at) VALUES (?, ?, ?);";
  sqlite3_stmt *st = NULL;


  if (sqlite3_prepare_v2 (db, sql_insert_invite, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, inviter_corp_id);
      sqlite3_bind_int (st, 2, target_player_id);
      sqlite3_bind_int64 (st, 3, expires_at);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }
  else
    {
      LOGE ("cmd_corp_invite: Failed to prepare invite insert: %s",
            sqlite3_errmsg (db));
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error while sending invitation.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Invitation sent successfully."));
  json_object_set_new (response_data, "corp_id",
                       json_integer (inviter_corp_id));
  json_object_set_new (response_data, "target_player_id",
                       json_integer (target_player_id));
  send_response_ok_take (ctx, root, "corp.invite.success", &response_data);
  return 0;
}


int
cmd_corp_join (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  int corp_id;
  json_t *j_corp_id = json_object_get (data, "corp_id");


  if (!json_is_integer (j_corp_id))
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Missing or invalid 'corp_id'.");
      return 0;
    }
  corp_id = json_integer_value (j_corp_id);
  if (h_get_player_corp_id (db, ctx->player_id) > 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "You are already in a corporation.");
      return 0;
    }
  long long expires_at = 0;
  const char *sql_check_invite =
    "SELECT expires_at FROM corp_invites WHERE corp_id = ? AND player_id = ?;";


  if (sqlite3_prepare_v2 (db, sql_check_invite, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, corp_id);
      sqlite3_bind_int (st, 2, ctx->player_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          expires_at = sqlite3_column_int64 (st, 0);
        }
      sqlite3_finalize (st);
    }
  if (expires_at == 0 || expires_at < (long long) time (NULL))
    {
      send_response_error (ctx,
                           root,
                           ERR_PERMISSION_DENIED,
                           "You do not have a valid invitation to join this corporation.");
      return 0;
    }
  const char *sql_insert_member =
    "INSERT INTO corp_members (corp_id, player_id, role) VALUES (?, ?, 'Member');";


  if (sqlite3_prepare_v2 (db, sql_insert_member, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, corp_id);
      sqlite3_bind_int (st, 2, ctx->player_id);
      if (sqlite3_step (st) != SQLITE_DONE)
        {
          LOGE ("cmd_corp_join: Failed to insert new member: %s",
                sqlite3_errmsg (db));
          send_response_error (ctx,
                               root,
                               ERR_SERVER_ERROR,
                               "Database error while joining corporation.");
          return 0;
        }
      sqlite3_finalize (st);
    }
  else
    {
      LOGE ("cmd_corp_join: Failed to prepare member insert: %s",
            sqlite3_errmsg (db));
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error while joining corporation.");
      return 0;
    }
  const char *sql_delete_invite =
    "DELETE FROM corp_invites WHERE corp_id = ? AND player_id = ?;";


  if (sqlite3_prepare_v2 (db, sql_delete_invite, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, corp_id);
      sqlite3_bind_int (st, 2, ctx->player_id);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Successfully joined the corporation."));
  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  send_response_ok_take (ctx, root, "corp.join.success", &response_data);
  ctx->corp_id = corp_id;
  return 0;
}


int
cmd_corp_kick (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  int target_player_id;
  json_t *j_target_id = json_object_get (data, "target_player_id");


  if (!json_is_integer (j_target_id))
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid 'target_player_id'.");
      return 0;
    }
  target_player_id = json_integer_value (j_target_id);
  if (ctx->player_id == target_player_id)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG, "You cannot kick yourself.");
      return 0;
    }
  int kicker_corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (kicker_corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  int target_corp_id = h_get_player_corp_id (db, target_player_id);


  if (target_corp_id != kicker_corp_id)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Target player is not in your corporation.");
      return 0;
    }
  char kicker_role[16];
  char target_role[16];


  h_get_player_corp_role (db, ctx->player_id, kicker_corp_id, kicker_role,
                          sizeof (kicker_role));
  h_get_player_corp_role (db, target_player_id, target_corp_id, target_role,
                          sizeof (target_role));
  bool can_kick = false;


  if (strcasecmp (kicker_role, "Leader") == 0
      && (strcasecmp (target_role, "Officer") == 0
          || strcasecmp (target_role, "Member") == 0))
    {
      can_kick = true;
    }
  else if (strcasecmp (kicker_role, "Officer") == 0
           && strcasecmp (target_role, "Member") == 0)
    {
      can_kick = true;
    }
  if (!can_kick)
    {
      send_response_error (ctx,
                           root,
                           ERR_PERMISSION_DENIED,
                           "Your rank is not high enough to kick this member.");
      return 0;
    }
  const char *sql_delete_member =
    "DELETE FROM corp_members WHERE corp_id = ? AND player_id = ?;";
  sqlite3_stmt *st = NULL;


  if (sqlite3_prepare_v2 (db, sql_delete_member, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, kicker_corp_id);
      sqlite3_bind_int (st, 2, target_player_id);
      sqlite3_step (st);
      sqlite3_finalize (st);
    }
  else
    {
      LOGE ("cmd_corp_kick: Failed to prepare delete statement: %s",
            sqlite3_errmsg (db));
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error while kicking member.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string
                         ("Player successfully kicked from the corporation."));
  json_object_set_new (response_data, "kicked_player_id",
                       json_integer (target_player_id));
  send_response_ok_take (ctx, root, "corp.kick.success", &response_data);
  return 0;
}


int
cmd_corp_dissolve (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  sqlite3_stmt *st = NULL;
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  char role[16];


  h_get_player_corp_role (db, ctx->player_id, corp_id, role, sizeof (role));
  if (strcasecmp (role, "Leader") != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_PERMISSION_DENIED,
                           "Only the corporation's leader can dissolve it.");
      return 0;
    }
  const char *sql_update_planets =
    "UPDATE planets SET owner_id = 0, owner_type = 'player' WHERE owner_id = ? AND owner_type = 'corp';";


  if (sqlite3_prepare_v2 (db, sql_update_planets, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, corp_id);
      if (sqlite3_step (st) != SQLITE_DONE)
        {
          LOGE
          (
            "cmd_corp_dissolve: Failed to update planet ownership for corp %d: %s",
            corp_id,
            sqlite3_errmsg (db));
        }
      sqlite3_finalize (st);
    }
  else
    {
      LOGE
        ("cmd_corp_dissolve: Failed to prepare planet update statement: %s",
        sqlite3_errmsg (db));
    }
  const char *sql_delete_corp = "DELETE FROM corporations WHERE id = ?;";


  if (sqlite3_prepare_v2 (db, sql_delete_corp, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, corp_id);
      if (sqlite3_step (st) != SQLITE_DONE)
        {
          LOGE ("cmd_corp_dissolve: Failed to delete corporation %d: %s",
                corp_id, sqlite3_errmsg (db));
          send_response_error (ctx,
                               root,
                               ERR_SERVER_ERROR,
                               "Database error during corporation dissolution.");
          return 0;
        }
      sqlite3_finalize (st);
    }
  else
    {
      LOGE ("cmd_corp_dissolve: Failed to prepare corp delete statement: %s",
            sqlite3_errmsg (db));
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error during corporation dissolution.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Corporation has been dissolved."));
  json_object_set_new (response_data, "dissolved_corp_id",
                       json_integer (corp_id));
  send_response_ok_take (ctx, root, "corp.dissolve.success", &response_data);
  ctx->corp_id = 0;
  return 0;
}


int
cmd_corp_balance (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  long long balance;


  if (db_get_corp_bank_balance (corp_id, &balance) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Failed to retrieve corporation balance.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "balance", json_integer (balance));
  send_response_ok_take (ctx, root, "corp.balance.success", &response_data);
  return 0;
}


int
cmd_corp_deposit (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!data)
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
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  if (db_bank_transfer ("player", ctx->player_id, "corp", corp_id, amount) !=
      SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_INSUFFICIENT_FUNDS,
                           "Transfer failed. Check your balance.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Deposit successful."));
  json_object_set_new (response_data, "amount", json_integer (amount));
  send_response_ok_take (ctx, root, "corp.deposit.success", &response_data);
  return 0;
}


int
cmd_corp_withdraw (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!data)
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
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  char role[16];


  h_get_player_corp_role (db, ctx->player_id, corp_id, role, sizeof (role));
  if (strcasecmp (role, "Leader") != 0 && strcasecmp (role, "Officer") != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_PERMISSION_DENIED,
                           "You do not have permission to withdraw funds.");
      return 0;
    }
  if (db_bank_transfer ("corp", corp_id, "player", ctx->player_id, amount) !=
      SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_INSUFFICIENT_FUNDS,
                           "Transfer failed. Check corporation balance.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Withdrawal successful."));
  json_object_set_new (response_data, "amount", json_integer (amount));
  send_response_ok_take (ctx, root, "corp.withdraw.success", &response_data);
  return 0;
}


int
cmd_corp_statement (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");
  int limit = 20;               // default limit


  if (data)
    {
      json_t *j_limit = json_object_get (data, "limit");


      if (json_is_integer (j_limit))
        {
          limit = (int) json_integer_value (j_limit);
        }
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  json_t *transactions = NULL;


  if (db_bank_get_transactions ("corp", corp_id, limit, NULL, 0, 0,     // tx_type_filter, start_date, end_date
                                0, 0,   // min_amount, max_amount
                                &transactions) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Failed to retrieve corporation transactions.");
      if (transactions)
        {
          json_decref (transactions);
        }
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "transactions", transactions);
  send_response_ok_take (ctx, root, "corp.statement.success", &response_data);
  return 0;
}


int
cmd_corp_status (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG, "You are not in a corporation.");
      return 0;
    }
  sqlite3_stmt *st = NULL;
  const char *sql =
    "SELECT name, tag, created_at, owner_id FROM corporations WHERE id = ?;";


  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) != SQLITE_OK)
    {
      send_response_error (ctx, root, ERR_SERVER_ERROR, "Database error.");
      return 0;
    }
  sqlite3_bind_int (st, 1, corp_id);
  json_t *response_data = json_object ();


  if (sqlite3_step (st) == SQLITE_ROW)
    {
      json_object_set_new (response_data, "corp_id", json_integer (corp_id));
      json_object_set_new (response_data, "name",
                           json_string ((const char *)
                                        sqlite3_column_text (st, 0)));
      const char *tag = (const char *) sqlite3_column_text (st, 1);


      if (tag)
        {
          json_object_set_new (response_data, "tag", json_string (tag));
        }
      json_object_set_new (response_data, "created_at",
                           json_integer (sqlite3_column_int (st, 2)));
      json_object_set_new (response_data, "ceo_id",
                           json_integer (sqlite3_column_int (st, 3)));
    }
  sqlite3_finalize (st);
  st = NULL;
  sql = "SELECT COUNT(*) FROM corp_members WHERE corp_id = ?;";
  if (sqlite3_prepare_v2 (db, sql, -1, &st, NULL) == SQLITE_OK)
    {
      sqlite3_bind_int (st, 1, corp_id);
      if (sqlite3_step (st) == SQLITE_ROW)
        {
          json_object_set_new (response_data, "member_count",
                               json_integer (sqlite3_column_int (st, 0)));
        }
      sqlite3_finalize (st);
    }
  char role[16];


  h_get_player_corp_role (db, ctx->player_id, corp_id, role, sizeof (role));
  json_object_set_new (response_data, "your_role", json_string (role));
  send_response_ok_take (ctx, root, "corp.status.success", &response_data);
  return 0;
}


int
cmd_stock_ipo_register (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root,
                                  "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  /* Ensure caller is an active CEO and grab corp_id */
  int corp_id = 0;


  if (!h_is_player_corp_ceo (db, ctx->player_id, &corp_id) || corp_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_PERMISSION_DENIED,
                           "Only corporation CEOs can register for IPO.");
      return 0;
    }
  /* Check if already publicly traded */
  int stock_id = 0;


  if (h_get_corp_stock_id (db, corp_id, &stock_id) == SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Your corporation is already publicly traded.");
      return 0;
    }
  /* Check credit rating */
  int credit_rating = 0;


  if (h_get_corp_credit_rating (db, corp_id, &credit_rating) != SQLITE_OK
      || credit_rating < 400)
    {                           // Assuming 400 is a "Default" threshold
      send_response_error (ctx,
                           root,
                           ERR_INVALID_CORP_STATE,
                           "Corporation credit rating is too low to go public.");
      return 0;
    }
  const char *ticker;
  json_t *j_ticker = json_object_get (data, "ticker");


  if (!json_is_string (j_ticker)
      || (ticker = json_string_value (j_ticker)) == NULL)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Missing or invalid 'ticker'.");
      return 0;
    }
  // Basic ticker validation: 3-5 uppercase alphanumeric characters
  if (strlen (ticker) < 3 || strlen (ticker) > 5)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Ticker must be 3-5 characters long.");
      return 0;
    }
  for (size_t i = 0; i < strlen (ticker); i++)
    {
      if (!isalnum ((unsigned char) ticker[i])
          || !isupper ((unsigned char) ticker[i]))
        {
          send_response_error (ctx,
                               root,
                               ERR_INVALID_ARG,
                               "Ticker must be uppercase alphanumeric characters.");
          return 0;
        }
    }
  int total_shares;


  if (!json_get_int_flexible (data, "total_shares", &total_shares)
      || total_shares <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid 'total_shares'.");
      return 0;
    }
  int par_value;


  if (!json_get_int_flexible (data, "par_value", &par_value) || par_value < 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid 'par_value'.");
      return 0;
    }
  sqlite3_stmt *st = NULL;
  const char *sql_insert_stock =
    "INSERT INTO stocks (corp_id, ticker, total_shares, par_value, current_price) VALUES (?, ?, ?, ?, ?);";
  int rc = sqlite3_prepare_v2 (db, sql_insert_stock, -1, &st, NULL);


  if (rc != SQLITE_OK)
    {
      LOGE ("cmd_stock_ipo_register: Failed to prepare stock insert: %s",
            sqlite3_errmsg (db));
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error during IPO registration.");
      return 0;
    }
  sqlite3_bind_int (st, 1, corp_id);
  sqlite3_bind_text (st, 2, ticker, -1, SQLITE_STATIC);
  sqlite3_bind_int (st, 3, total_shares);
  sqlite3_bind_int (st, 4, par_value);
  sqlite3_bind_int (st, 5, par_value);  // Current price initially set to par value
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("cmd_stock_ipo_register: Failed to insert stock: %s",
            sqlite3_errmsg (db));
      if (sqlite3_errcode (db) == SQLITE_CONSTRAINT)
        {
          send_response_error (ctx,
                               root,
                               ERR_NAME_TAKEN,
                               "A stock with that ticker already exists.");
        }
      else
        {
          send_response_error (ctx,
                               root,
                               ERR_SERVER_ERROR,
                               "Database error during IPO registration.");
        }
      return 0;
    }
  int new_stock_id = (int) sqlite3_last_insert_rowid (db);


  // Distribute initial shares to the corporation itself (as a shareholder)
  rc = h_update_player_shares (db, 0, new_stock_id, total_shares);      // player_id 0 for corporation
  if (rc != SQLITE_OK)
    {
      LOGE
      (
        "cmd_stock_ipo_register: Failed to distribute initial shares to corp %d for stock %d: %s",
        corp_id,
        new_stock_id,
        sqlite3_errmsg (db));
      // This is a critical error, consider rolling back or marking stock invalid
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string
                         ("Corporation successfully registered for IPO."));
  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "stock_id",
                       json_integer (new_stock_id));
  json_object_set_new (response_data, "ticker", json_string (ticker));
  send_response_ok_take (ctx, root, "stock.ipo.register.success",
                         &response_data);
  json_t *payload = json_object ();


  json_object_set_new (payload, "corp_id", json_integer (corp_id));
  json_object_set_new (payload, "stock_id", json_integer (new_stock_id));
  json_object_set_new (payload, "ticker", json_string (ticker));


  db_log_engine_event (time (NULL), "stock.ipo.registered", "corp", corp_id,
                       0, payload, NULL);
  json_decref (payload);
  return 0;
}


int
cmd_stock_buy (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  int stock_id;


  if (!json_get_int_flexible (data, "stock_id", &stock_id) || stock_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Missing or invalid 'stock_id'.");
      return 0;
    }
  int quantity;


  if (!json_get_int_flexible (data, "quantity", &quantity) || quantity <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Missing or invalid 'quantity'.");
      return 0;
    }
  char *ticker = NULL;
  int corp_id = 0;
  int total_shares = 0;
  int par_value = 0;
  int current_price = 0;
  long long last_dividend_ts = 0;
  int rc = h_get_stock_info (db, stock_id, &ticker, &corp_id, &total_shares,
                             &par_value, &current_price, &last_dividend_ts);


  if (rc != SQLITE_OK)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Stock not found.");
      free (ticker);
      return 0;
    }
  long long total_cost = (long long) quantity * current_price;
  // Check player balance
  long long player_balance;


  if (db_get_player_bank_balance (ctx->player_id, &player_balance) !=
      SQLITE_OK || player_balance < total_cost)
    {
      send_response_error (ctx,
                           root,
                           ERR_INSUFFICIENT_FUNDS,
                           "Insufficient funds to purchase shares.");
      free (ticker);
      return 0;
    }
  // Perform transfer
  rc =
    db_bank_transfer ("player", ctx->player_id, "corp", corp_id, total_cost);
  if (rc != SQLITE_OK)
    {
      LOGE ("cmd_stock_buy: Bank transfer failed for player %d, stock %d: %s",
            ctx->player_id, stock_id, sqlite3_errstr (rc));
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Failed to complete share purchase due to banking error.");
      free (ticker);
      return 0;
    }
  // Update player shares
  rc = h_update_player_shares (db, ctx->player_id, stock_id, quantity);
  if (rc != SQLITE_OK)
    {
      LOGE
      (
        "cmd_stock_buy: Failed to update player shares for player %d, stock %d: %s",
        ctx->player_id,
        stock_id,
        sqlite3_errstr (rc));
      // Critical error: funds transferred, but shares not updated. Manual intervention needed or complex rollback.
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Failed to update player shares after purchase.");
      free (ticker);
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Shares purchased successfully."));
  json_object_set_new (response_data, "stock_id", json_integer (stock_id));
  json_object_set_new (response_data, "ticker", json_string (ticker));
  json_object_set_new (response_data, "quantity", json_integer (quantity));
  json_object_set_new (response_data, "total_cost",
                       json_integer (total_cost));
  send_response_ok_take (ctx, root, "stock.buy.success", &response_data);
  json_t *payload = json_object ();


  json_object_set_new (payload, "player_id", json_integer (ctx->player_id));
  json_object_set_new (payload, "stock_id", json_integer (stock_id));
  json_object_set_new (payload, "quantity", json_integer (quantity));
  json_object_set_new (payload, "cost", json_integer (total_cost));


  db_log_engine_event (time (NULL), "stock.buy", "player", ctx->player_id, 0,
                       payload, NULL);
  json_decref (payload);
  free (ticker);
  return 0;
}


int
cmd_stock_dividend_set (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED, "Authentication required.");
      return -1;
    }
  sqlite3 *db = db_get_handle ();
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  /* Ensure caller is an active CEO and grab corp_id */
  int corp_id = 0;


  if (!h_is_player_corp_ceo (db, ctx->player_id, &corp_id) || corp_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_PERMISSION_DENIED,
                           "Only corporation CEOs can set dividends.");
      return 0;
    }
  int stock_id = 0;


  if (h_get_corp_stock_id (db, corp_id, &stock_id) != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Your corporation is not publicly traded.");
      return 0;
    }
  int amount_per_share;


  if (!json_get_int_flexible (data, "amount_per_share", &amount_per_share)
      || amount_per_share < 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid 'amount_per_share'.");
      return 0;
    }
  // Get total shares to calculate total dividend payout
  int total_shares = 0;
  int rc = h_get_stock_info (db, stock_id, NULL, NULL, &total_shares,
                             NULL, NULL, NULL);


  if (rc != SQLITE_OK)
    {
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Failed to retrieve stock information.");
      return 0;
    }
  long long total_payout = (long long) amount_per_share * total_shares;
  // Check if corporation has enough funds
  long long corp_balance;


  if (db_get_corp_bank_balance (corp_id, &corp_balance) != SQLITE_OK
      || corp_balance < total_payout)
    {
      send_response_error (ctx,
                           root,
                           ERR_INSUFFICIENT_FUNDS,
                           "Corporation has insufficient funds to declare this dividend.");
      return 0;
    }
  sqlite3_stmt *st = NULL;
  const char *sql_insert_dividend =
    "INSERT INTO stock_dividends (stock_id, amount_per_share, declared_ts) VALUES (?, ?, ?);";


  rc = sqlite3_prepare_v2 (db, sql_insert_dividend, -1, &st, NULL);
  if (rc != SQLITE_OK)
    {
      LOGE ("cmd_stock_dividend_set: Failed to prepare insert statement: %s",
            sqlite3_errmsg (db));
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error declaring dividend.");
      return 0;
    }
  sqlite3_bind_int (st, 1, stock_id);
  sqlite3_bind_int (st, 2, amount_per_share);
  sqlite3_bind_int64 (st, 3, time (NULL));
  rc = sqlite3_step (st);
  sqlite3_finalize (st);
  if (rc != SQLITE_DONE)
    {
      LOGE ("cmd_stock_dividend_set: Failed to insert dividend: %s",
            sqlite3_errmsg (db));
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Database error declaring dividend.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Dividend declared successfully."));
  json_object_set_new (response_data, "stock_id", json_integer (stock_id));
  json_object_set_new (response_data, "amount_per_share",
                       json_integer (amount_per_share));
  json_object_set_new (response_data, "total_payout",
                       json_integer (total_payout));
  send_response_ok_take (ctx, root, "stock.dividend.set.success",
                         &response_data);
  json_t *payload = json_object ();


  json_object_set_new (payload, "corp_id", json_integer (corp_id));
  json_object_set_new (payload, "stock_id", json_integer (stock_id));
  json_object_set_new (payload, "amount_per_share",
                       json_integer (amount_per_share));
  json_object_set_new (payload, "total_payout", json_integer (total_payout));


  db_log_engine_event (time (NULL), "stock.dividend.declared", "corp",
                       corp_id, 0, payload, NULL);
  json_decref (payload);
  return 0;
}


int
cmd_stock (client_ctx_t *ctx, json_t *root)
{
  json_t *data = json_object_get (root, "data");
  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST,
                           "Missing data object.");
      return 0;
    }
  const char *subcommand =
    json_string_value (json_object_get (data, "subcommand"));


  if (!subcommand)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST, "Missing 'subcommand' in data.");
      return 0;
    }
  if (strcasecmp (subcommand, "ipo.register") == 0)
    {
      return cmd_stock_ipo_register (ctx, root);
    }
  else if (strcasecmp (subcommand, "buy") == 0)
    {
      return cmd_stock_buy (ctx, root);
    }
  else if (strcasecmp (subcommand, "dividend.set") == 0)
    {
      return cmd_stock_dividend_set (ctx, root);
    }
  else
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_IMPLEMENTED,
                           "Stock subcommand not implemented.");
      return 0;
    }
}

