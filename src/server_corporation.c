#include <jansson.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <strings.h>
#include <pthread.h>
#include <ctype.h>              // Required for isalnum and isupper
#include "server_corporation.h"
#include "server_config.h"
#include "database.h"
#include "game_db.h"
#include "server_log.h"
#include "server_envelope.h"
#include "errors.h"
#include "server_players.h"
#include "common.h"
#include "server_cron.h"
#include "db/sql_driver.h"


int
h_get_player_corp_role (db_t *db, int player_id, int corp_id,
                        char *role_buffer, size_t buffer_size)
{
  char sql[512];
  sql_build (db, "SELECT role FROM corp_members WHERE player_id = {1} AND corporation_id = {2};", sql, sizeof(sql));
  db_bind_t params[] = { db_bind_i32 (player_id), db_bind_i32 (corp_id) };
  db_res_t *res = NULL;
  db_error_t err;

  if (db_query (db, sql, params, 2, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          const char *role = db_res_col_text (res, 0, &err);


          if (role)
            {
              strncpy (role_buffer, role, buffer_size - 1);
              role_buffer[buffer_size - 1] = '\0';
            }
          else
            {
              role_buffer[0] = '\0';
            }
          db_res_finalize (res);
          return 0;
        }
      db_res_finalize (res);
      role_buffer[0] = '\0';
      return ERR_DB_NOT_FOUND;
    }
  role_buffer[0] = '\0';
  return err.code;
}


int
h_is_player_corp_ceo (db_t *db, int player_id, int *out_corp_id)
{
  if (!db || player_id <= 0)
    {
      return 0;
    }
  char sql[512];
  sql_build (db, "SELECT corporation_id FROM corporations WHERE owner_id = {1};", sql, sizeof(sql));
  db_bind_t params[] = { db_bind_i32 (player_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int found = 0;


  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          int corp_id = db_res_col_i32 (res, 0, &err);


          if (out_corp_id)
            {
              *out_corp_id = corp_id;
            }
          found = 1;
        }
      db_res_finalize (res);
    }
  return found;
}


int
h_get_player_corp_id (db_t *db, int player_id)
{
  char sql[512];
  sql_build (db, "SELECT corporation_id FROM corp_members WHERE player_id = {1};", sql, sizeof(sql));
  db_bind_t params[] = { db_bind_i32 (player_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int corp_id = 0;

  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          corp_id = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }
  else
    {
      LOGE ("h_get_player_corp_id: query failed: %s", err.message);
    }
  return corp_id;
}


int
h_get_corp_bank_account_id (db_t *db, int corp_id)
{
  char sql[512];
  sql_build (db, "SELECT id FROM bank_accounts WHERE owner_type = 'corp' AND owner_id = {1};", sql, sizeof(sql));
  db_bind_t params[] = { db_bind_i32 (corp_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int account_id = -1;

  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          account_id = db_res_col_i32 (res, 0, &err);
        }
      db_res_finalize (res);
    }
  else
    {
      LOGE ("h_get_corp_bank_account_id: query failed: %s", err.message);
    }
  return account_id;
}


int
h_get_corp_credit_rating (db_t *db, int corp_id, int *rating)
{
  char sql[512];
  sql_build (db, "SELECT credit_rating FROM corporations WHERE corporation_id = {1};", sql, sizeof(sql));
  db_bind_t params[] = { db_bind_i32 (corp_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int rc = ERR_DB_NOT_FOUND;

  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          if (rating)
            {
              *rating = db_res_col_i32 (res, 0, &err);
            }
          rc = 0;
        }
      else
        {
          if (rating)
            {
              *rating = 0;
            }
        }
      db_res_finalize (res);
    }
  else
    {
      LOGE ("h_get_corp_credit_rating: query failed: %s", err.message);
      rc = err.code;
    }
  return rc;
}


int
h_get_corp_stock_id (db_t *db, int corp_id, int *out_stock_id)
{
  char sql[512];
  sql_build (db, "SELECT id FROM stocks WHERE corp_id = {1};", sql, sizeof(sql));
  db_bind_t params[] = { db_bind_i32 (corp_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int rc = ERR_DB_NOT_FOUND;

  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          if (out_stock_id)
            {
              *out_stock_id = db_res_col_i32 (res, 0, &err);
            }
          rc = 0;
        }
      else
        {
          if (out_stock_id)
            {
              *out_stock_id = 0;
            }
        }
      db_res_finalize (res);
    }
  else
    {
      LOGE ("h_get_corp_stock_id: query failed: %s", err.message);
      rc = err.code;
    }
  return rc;
}


int
h_get_stock_info (db_t *db, int stock_id, char **out_ticker,
                  int *out_corp_id, int *out_total_shares,
                  int *out_par_value, int *out_current_price,
                  long long *out_last_dividend_ts)
{
  char sql[512];
  sql_build (db, "SELECT ticker, corp_id, total_shares, par_value, current_price, last_dividend_ts FROM stocks WHERE id = {1};", sql, sizeof(sql));
  db_bind_t params[] = { db_bind_i32 (stock_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int rc = ERR_DB_NOT_FOUND;

  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          if (out_ticker)
            {
              const char *tmp = db_res_col_text (res, 0, &err);


              *out_ticker = tmp ? strdup (tmp) : NULL;
            }
          if (out_corp_id)
            {
              *out_corp_id = db_res_col_i32 (res, 1, &err);
            }
          if (out_total_shares)
            {
              *out_total_shares = db_res_col_i32 (res, 2, &err);
            }
          if (out_par_value)
            {
              *out_par_value = db_res_col_i32 (res, 3, &err);
            }
          if (out_current_price)
            {
              *out_current_price = db_res_col_i32 (res, 4, &err);
            }
          if (out_last_dividend_ts)
            {
              *out_last_dividend_ts = db_res_col_i64 (res, 5, &err);
            }
          rc = 0;
        }
      db_res_finalize (res);
    }
  else
    {
      LOGE ("h_get_stock_info: query failed: %s", err.message);
      rc = err.code;
    }
  return rc;
}


int
h_update_player_shares (db_t *db, int player_id, int stock_id,
                        int quantity_change)
{
  if (quantity_change == 0)
    {
      return 0;         // No change needed
    }

  db_error_t err;


  if (quantity_change > 0)
    {
      // Add shares, or insert if not exists
      const char *conflict_fmt = sql_conflict_target_fmt(db);
      if (!conflict_fmt)
        {
          return -1;  /* Unsupported backend */
        }
      
      char conflict_clause[128];
      snprintf(conflict_clause, sizeof(conflict_clause),
        conflict_fmt, "player_id, corp_id");
      
      char sql_add[512];
      char sql_template[512];
      snprintf(sql_template, sizeof(sql_template),
        "INSERT INTO corp_shareholders (player_id, corp_id, shares) "
        "VALUES ({1}, (SELECT corp_id FROM stocks WHERE id = {2}), {3}) "
        "%s UPDATE SET shares = shares + excluded.shares;",
        conflict_clause);
      sql_build(db, sql_template, sql_add, sizeof(sql_add));

      db_bind_t params[] = { db_bind_i32 (player_id), db_bind_i32 (stock_id),
                             db_bind_i32 (quantity_change) };


      if (!db_exec (db, sql_add, params, 3, &err))
        {
          LOGE ("h_update_player_shares: Failed to add shares: %s",
                err.message);
          return err.code;
        }
    }
  else
    {
      char sql_deduct[512];
      sql_build (db, "UPDATE corp_shareholders SET shares = shares + {1} "
        "WHERE player_id = {2} AND corp_id = (SELECT corp_id FROM stocks WHERE id = {3}) AND (shares + {4}) >= 0;",
        sql_deduct, sizeof(sql_deduct));

      db_bind_t params[] = {
        db_bind_i32 (quantity_change),
        db_bind_i32 (player_id),
        db_bind_i32 (stock_id),
        db_bind_i32 (quantity_change)
      };
      int64_t rows_affected = 0;


      if (!db_exec_rows_affected (db,
                                  sql_deduct,
                                  params,
                                  4,
                                  &rows_affected,
                                  &err))
        {
          LOGE ("h_update_player_shares: Failed to deduct shares: %s",
                err.message);
          return err.code;
        }

      if (rows_affected == 0)
        {
          LOGW (
            "h_update_player_shares: Player %d has insufficient shares for stock %d.",
            player_id,
            stock_id);
          return ERR_DB_CONSTRAINT;
        }
    }

  // Clean up 0-share entries
  db_exec (db, "DELETE FROM corp_shareholders WHERE shares = 0;", NULL, 0,
           &err);
  return 0;
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
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
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
  char sql_check_member[512];
  sql_build (db, "SELECT role FROM corp_members WHERE corp_id = {1} AND player_id = {2};", sql_check_member, sizeof(sql_check_member));
  db_bind_t params_check[] = { db_bind_i32 (corp_id),
                               db_bind_i32 (target_player_id) };
  db_res_t *res_check = NULL;
  db_error_t err;
  const char *target_role = NULL;


  if (db_query (db, sql_check_member, params_check, 2, &res_check, &err))
    {
      if (db_res_step (res_check, &err))
        {
          target_role = db_res_col_text (res_check, 0, &err);
        }
    }

  if (!target_role)
    {
      db_res_finalize (res_check);
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Target player is not a member of your corporation.");
      return 0;
    }
  db_res_finalize (res_check);

  /* Guard: current CEO must NOT be flying the Corporate Flagship */
  char sql_flagship_check[512];
  sql_build (db,
    "SELECT st.name "
    "FROM players p "
    "JOIN ships s ON p.ship_id = s.id "
    "JOIN shiptypes st ON s.type_id = st.id WHERE p.id = {1};",
    sql_flagship_check, sizeof(sql_flagship_check));

  db_bind_t params_fs[] = { db_bind_i32 (ctx->player_id) };
  db_res_t *res_fs = NULL;
  bool is_flagship = false;


  if (db_query (db, sql_flagship_check, params_fs, 1, &res_fs, &err))
    {
      if (db_res_step (res_fs, &err))
        {
          const char *ship_name = db_res_col_text (res_fs, 0, &err);


          if (ship_name && !strcasecmp (ship_name, "Corporate Flagship"))
            {
              is_flagship = true;
            }
        }
      db_res_finalize (res_fs);
    }

  if (is_flagship)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_CORP_STATE,
                           "You cannot transfer CEO while piloting the Corporate Flagship.");
      return 0;
    }

  /* Perform the transfer in a transaction */
  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, err.code, "Failed to start transaction.");
      return 0;
    }

  bool ok = true;

  /* Demote current CEO to Officer */
  char sql_demote[512];
  sql_build (db, "UPDATE corp_members SET role = 'Officer' WHERE corp_id = {1} AND player_id = {2} AND role = 'Leader';", sql_demote, sizeof(sql_demote));
  db_bind_t params_demote[] = { db_bind_i32 (corp_id),
                                db_bind_i32 (ctx->player_id) };


  if (!db_exec (db, sql_demote, params_demote, 2, &err))
    {
      ok = false;
    }

  /* Ensure target has a membership row */
  if (ok)
    {
      const char *conflict_clause = sql_insert_ignore_clause(db);
      if (!conflict_clause)
        {
          ok = false;  /* Unsupported backend */
        }
      else
        {
          char sql_insert_member[256];
          char sql_insert_template[256];
          snprintf(sql_insert_template, sizeof(sql_insert_template),
            "INSERT INTO corp_members (corp_id, player_id, role) "
            "VALUES ({1}, {2}, 'Member') %s;",
            conflict_clause);
          sql_build(db, sql_insert_template, sql_insert_member, sizeof(sql_insert_member));
          
          db_bind_t params_ins[] = { db_bind_i32 (corp_id),
                                     db_bind_i32 (target_player_id) };


          if (!db_exec (db, sql_insert_member, params_ins, 2, &err))
            {
              ok = false;
            }
        }
    }

  /* Promote target to Leader */
  if (ok)
    {
      char sql_promote[512];
      sql_build (db, "UPDATE corp_members SET role = 'Leader' WHERE corp_id = {1} AND player_id = {2};", sql_promote, sizeof(sql_promote));
      db_bind_t params_promote[] = { db_bind_i32 (corp_id),
                                     db_bind_i32 (target_player_id) };


      if (!db_exec (db, sql_promote, params_promote, 2, &err))
        {
          ok = false;
        }
    }

  /* Update corporations.owner_id */
  if (ok)
    {
      char sql_update_owner[512];
      sql_build (db, "UPDATE corporations SET owner_id = {1} WHERE corporation_id = {2};", sql_update_owner, sizeof(sql_update_owner));
      db_bind_t params_owner[] = { db_bind_i32 (target_player_id),
                                   db_bind_i32 (corp_id) };


      if (!db_exec (db, sql_update_owner, params_owner, 2, &err))
        {
          ok = false;
        }
    }

  if (!ok)
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, ERR_DB, "Failed to transfer CEO role.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code,
                           "Failed to commit transaction.");
      return 0;
    }

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
                           "Missing data object.");
      return 0;
    }
  const char *name;
  json_t *j_name = json_object_get (data, "name");


  if (!json_is_string (j_name) || (name = json_string_value (j_name)) == NULL
      || name[0] == '\0')
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_BAD_REQUEST,
                                   "Missing or invalid corporation name.",
                                   NULL);
      return 0;
    }

  if (h_get_player_corp_id (db, ctx->player_id) > 0)
    {
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INVALID_ARG,
                                   "You are already a member of a corporation.",
                                   NULL);
      return 0;
    }

  long long creation_fee = g_cfg.corporation_creation_fee;
  long long player_new_balance;
  int player_bank_account_id = 0;


  if (h_get_account_id_unlocked (db,
                                 "player",
                                 ctx->player_id,
                                 &player_bank_account_id) != 0)
    {
      player_bank_account_id = 0; // Ensure 0 on failure
    }


  if (player_bank_account_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Could not retrieve player bank account.");
      return 0;
    }

  db_error_t err;


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, err.code, "Database busy (create)");
      return 0;
    }

  if (h_deduct_credits_unlocked (db,
                                 player_bank_account_id,
                                 creation_fee,
                                 "CORP_CREATION_FEE",
                                 NULL,
                                 &player_new_balance) != 0)
    {
      db_tx_rollback (db, NULL);
      send_response_refused_steal (ctx,
                                   root,
                                   ERR_INSUFFICIENT_FUNDS,
                                   "Insufficient funds.",
                                   NULL);
      return 0;
    }

  char sql_insert_corp[512];
  sql_build (db, "INSERT INTO corporations (name, owner_id) VALUES ({1}, {2});", sql_insert_corp, sizeof(sql_insert_corp));
  db_bind_t params_corp[] = { db_bind_text (name),
                              db_bind_i32 (ctx->player_id) };
  int64_t new_corp_id = 0;


  if (!db_exec_insert_id (db,
                          sql_insert_corp,
                          params_corp,
                          2,
                          &new_corp_id,
                          &err))
    {
      db_tx_rollback (db, NULL);
      if (err.code == ERR_DB_CONSTRAINT)
        {
          send_response_refused_steal (ctx,
                                       root,
                                       ERR_NAME_TAKEN,
                                       "A corporation with that name already exists.",
                                       NULL);
        }
      else
        {
          send_response_error (ctx, root, err.code, "Database error.");
        }
      return 0;
    }

  int corp_id = (int) new_corp_id;

  char sql_insert_member[512];
  sql_build (db, "INSERT INTO corp_members (corp_id, player_id, role) VALUES ({1}, {2}, 'Leader');", sql_insert_member, sizeof(sql_insert_member));
  db_bind_t params_mem[] = { db_bind_i32 (corp_id),
                             db_bind_i32 (ctx->player_id) };


  if (!db_exec (db, sql_insert_member, params_mem, 2, &err))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, err.code, "Failed to add CEO.");
      return 0;
    }

  char sql_create_bank[512];
  sql_build (db, "INSERT INTO bank_accounts (owner_type, owner_id, currency) VALUES ('corp', {1}, 'CRD');", sql_create_bank, sizeof(sql_create_bank));
  db_bind_t params_bank[] = { db_bind_i32 (corp_id) };


  if (!db_exec (db, sql_create_bank, params_bank, 1, &err))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, err.code, "Failed to create bank.");
      return 0;
    }

  char sql_convert_planets[512];
  sql_build (db, "UPDATE planets SET owner_id = {1}, owner_type = 'corp' WHERE owner_id = {2} AND owner_type = 'player';", sql_convert_planets, sizeof(sql_convert_planets));
  db_bind_t params_pl[] = { db_bind_i32 (corp_id),
                            db_bind_i32 (ctx->player_id) };


  if (!db_exec (db, sql_convert_planets, params_pl, 2, &err))
    {
      db_tx_rollback (db, NULL);
      send_response_error (ctx, root, err.code, "Failed to update planets.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code, "Commit failed.");
      return 0;
    }

  ctx->corp_id = corp_id;
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "name", json_string (name));
  send_response_ok_take (ctx, root, "corp.create.success", &response_data);
  return 0;
}


int
cmd_corp_join (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return -1;
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
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data object.");
      return 0;
    }
  int corp_id;
  json_t *j_corp_id = json_object_get (data, "corp_id");


  if (!json_is_integer (j_corp_id))
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid 'corp_id'.");
      return 0;
    }
  corp_id = (int) json_integer_value (j_corp_id);
  if (h_get_player_corp_id (db, ctx->player_id) > 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "You are already in a corporation.");
      return 0;
    }

  long long expires_at = 0;
  char sql_check_invite[512];
  sql_build (db, "SELECT expires_at FROM corp_invites WHERE corp_id = {1} AND player_id = {2};", sql_check_invite, sizeof(sql_check_invite));
  db_bind_t params_check[] = { db_bind_i32 (corp_id),
                               db_bind_i32 (ctx->player_id) };
  db_res_t *res = NULL;
  db_error_t err;


  if (db_query (db, sql_check_invite, params_check, 2, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          expires_at = db_res_col_i64 (res, 0, &err);
        }
      db_res_finalize (res);
    }

  if (expires_at == 0 || expires_at < (long long) time (NULL))
    {
      send_response_error (ctx,
                           root,
                           ERR_PERMISSION_DENIED,
                           "You do not have a valid invitation to join this corporation.");
      return 0;
    }

  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, err.code, "Database busy (join)");
      return 0;
    }

  char sql_insert_member[512];
  sql_build (db, "INSERT INTO corp_members (corp_id, player_id, role) VALUES ({1}, {2}, 'Member');", sql_insert_member, sizeof(sql_insert_member));
  db_bind_t params_mem[] = { db_bind_i32 (corp_id),
                             db_bind_i32 (ctx->player_id) };


  if (!db_exec (db, sql_insert_member, params_mem, 2, &err))
    {
      db_tx_rollback (db, NULL);
      LOGE ("cmd_corp_join: Failed to insert new member: %s", err.message);
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Database error while joining corporation.");
      return 0;
    }

  char sql_delete_invite[512];
  sql_build (db, "DELETE FROM corp_invites WHERE corp_id = {1} AND player_id = {2};", sql_delete_invite, sizeof(sql_delete_invite));


  if (!db_exec (db, sql_delete_invite, params_mem, 2, &err))
    {
      // Not critical if delete fails, but log it
      LOGW ("cmd_corp_join: Failed to delete invite: %s", err.message);
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code, "Commit failed (join)");
      return 0;
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
cmd_corp_list (client_ctx_t *ctx, json_t *root)
{
  (void) ctx;
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *corp_array = json_array ();
  const char *sql =
    "SELECT c.corporation_id, c.name, c.tag, c.owner_id, p.name, "
    "(SELECT COUNT(*) FROM corp_members cm WHERE cm.corporation_id = c.corporation_id) as member_count "
    "FROM corporations c "
    "LEFT JOIN players p ON c.owner_id = p.player_id "
    "WHERE c.corporation_id > 0;";

  db_res_t *res = NULL;
  db_error_t err;


  if (db_query (db, sql, NULL, 0, &res, &err))
    {
      while (db_res_step (res, &err))
        {
          json_t *corp_obj = json_object ();


          json_object_set_new (corp_obj, "corp_id",
                               json_integer (db_res_col_i32 (res, 0, &err)));
          json_object_set_new (corp_obj, "name",
                               json_string (db_res_col_text (res,
                                                             1,
                                                             &err)));
          const char *tag = db_res_col_text (res, 2, &err);


          if (tag)
            {
              json_object_set_new (corp_obj, "tag", json_string (tag));
            }
          json_object_set_new (corp_obj, "ceo_id",
                               json_integer (db_res_col_i32 (res, 3, &err)));
          const char *ceo_name = db_res_col_text (res, 4, &err);


          if (ceo_name)
            {
              json_object_set_new (corp_obj, "ceo_name",
                                   json_string (ceo_name));
            }
          json_object_set_new (corp_obj, "member_count",
                               json_integer (db_res_col_i32 (res, 5, &err)));
          json_array_append_new (corp_array, corp_obj);
        }
      db_res_finalize (res);
    }
  else
    {
      LOGE ("cmd_corp_list: Query failed: %s", err.message);
      json_decref (corp_array);
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Database error while fetching corporation list.");
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corporations", corp_array);
  send_response_ok_take (ctx, root, "corp.list.success", &response_data);
  return 0;
}


int
cmd_corp_roster (client_ctx_t *ctx, json_t *root)
{
  (void) ctx;
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!json_is_object (data))
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data object.");
      return 0;
    }
  int corp_id;
  json_t *j_corp_id = json_object_get (data, "corp_id");


  if (!json_is_integer (j_corp_id))
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid 'corp_id'.");
      return 0;
    }
  corp_id = (int) json_integer_value (j_corp_id);
  json_t *roster_array = json_array ();
  char sql[1024];
  sql_build (db,
    "SELECT cm.player_id, p.name, cm.role "
    "FROM corp_members cm "
    "JOIN players p ON cm.player_id = p.player_id "
    "WHERE cm.corporation_id = {1};",
    sql, sizeof(sql));

  db_bind_t params[] = { db_bind_i32 (corp_id) };
  db_res_t *res = NULL;
  db_error_t err;


  if (db_query (db, sql, params, 1, &res, &err))
    {
      while (db_res_step (res, &err))
        {
          json_t *member_obj = json_object ();


          json_object_set_new (member_obj, "player_id",
                               json_integer (db_res_col_i32 (res, 0, &err)));
          json_object_set_new (member_obj, "name",
                               json_string (db_res_col_text (res, 1, &err)));
          json_object_set_new (member_obj, "role",
                               json_string (db_res_col_text (res, 2, &err)));
          json_array_append_new (roster_array, member_obj);
        }
      db_res_finalize (res);
    }
  else
    {
      LOGE ("cmd_corp_roster: Query failed: %s", err.message);
      json_decref (roster_array);
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Database error while fetching roster.");
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "corp_id", json_integer (corp_id));
  json_object_set_new (response_data, "roster", roster_array);
  send_response_ok_take (ctx, root, "corp.roster.success", &response_data);
  return 0;
}


int
cmd_corp_kick (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return -1;
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
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data object.");
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
  target_player_id = (int) json_integer_value (j_target_id);
  if (ctx->player_id == target_player_id)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "You cannot kick yourself.");
      return 0;
    }
  int kicker_corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (kicker_corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "You are not in a corporation.");
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


  h_get_player_corp_role (db,
                          ctx->player_id,
                          kicker_corp_id,
                          kicker_role,
                          sizeof (kicker_role));
  h_get_player_corp_role (db,
                          target_player_id,
                          target_corp_id,
                          target_role,
                          sizeof (target_role));
  bool can_kick = false;


  if (strcasecmp (kicker_role, "Leader") == 0 && (strcasecmp (target_role,
                                                              "Officer") == 0 ||
                                                  strcasecmp (target_role,
                                                              "Member") == 0))
    {
      can_kick = true;
    }
  else if (strcasecmp (kicker_role, "Officer") == 0 && strcasecmp (target_role,
                                                                   "Member") ==
           0)
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
  char sql_delete_member[512];
  sql_build (db, "DELETE FROM corp_members WHERE corp_id = {1} AND player_id = {2};", sql_delete_member, sizeof(sql_delete_member));
  db_bind_t params[] = { db_bind_i32 (kicker_corp_id),
                         db_bind_i32 (target_player_id) };
  db_error_t err;


  if (!db_exec (db, sql_delete_member, params, 2, &err))
    {
      LOGE ("cmd_corp_kick: Failed to delete member: %s", err.message);
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Database error while kicking member.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string (
                         "Player successfully kicked from the corporation."));
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
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "You are not in a corporation.");
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

  db_error_t err;


  if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
    {
      send_response_error (ctx, root, err.code, "Database busy (dissolve)");
      return 0;
    }

  char sql_update_planets[512];
  sql_build (db, "UPDATE planets SET owner_id = 0, owner_type = 'player' WHERE owner_id = {1} AND owner_type = 'corp';", sql_update_planets, sizeof(sql_update_planets));
  db_bind_t params[] = { db_bind_i32 (corp_id) };


  if (!db_exec (db, sql_update_planets, params, 1, &err))
    {
      db_tx_rollback (db, NULL);
      LOGE (
        "cmd_corp_dissolve: Failed to update planet ownership for corp %d: %s",
        corp_id,
        err.message);
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Database error during corporation dissolution.");
      return 0;
    }

  char sql_delete_corp[512];
  sql_build (db, "DELETE FROM corporations WHERE corporation_id = {1};", sql_delete_corp, sizeof(sql_delete_corp));


  if (!db_exec (db, sql_delete_corp, params, 1, &err))
    {
      db_tx_rollback (db, NULL);
      LOGE ("cmd_corp_dissolve: Failed to delete corporation %d: %s",
            corp_id,
            err.message);
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Database error during corporation dissolution.");
      return 0;
    }

  if (!db_tx_commit (db, &err))
    {
      send_response_error (ctx, root, err.code, "Commit failed (dissolve)");
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Corporation has been dissolved."));
  json_object_set_new (response_data,
                       "dissolved_corp_id",
                       json_integer (corp_id));
  send_response_ok_take (ctx, root, "corp.dissolve.success", &response_data);
  ctx->corp_id = 0;
  return 0;
}


int
cmd_corp_leave (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "You are not in a corporation.");
      return 0;
    }
  char role[16];


  h_get_player_corp_role (db, ctx->player_id, corp_id, role, sizeof (role));
  if (strcasecmp (role, "Leader") == 0)
    {
      int member_count = 0;
      char sql_count[512];
      sql_build (db, "SELECT COUNT(*) FROM corp_members WHERE corp_id = {1};", sql_count, sizeof(sql_count));
      db_bind_t params_count[] = { db_bind_i32 (corp_id) };
      db_res_t *res_count = NULL;
      db_error_t err;


      if (db_query (db, sql_count, params_count, 1, &res_count, &err))
        {
          if (db_res_step (res_count, &err))
            {
              member_count = db_res_col_i32 (res_count, 0, &err);
            }
          db_res_finalize (res_count);
        }

      if (member_count > 1)
        {
          send_response_error (ctx,
                               root,
                               ERR_INVALID_ARG,
                               "You must transfer leadership before leaving the corporation.");
          return 0;
        }

      if (!db_tx_begin (db, DB_TX_IMMEDIATE, &err))
        {
          send_response_error (ctx, root, err.code, "Database busy (leave)");
          return 0;
        }

      char sql_delete_corp[512];
      sql_build (db, "DELETE FROM corporations WHERE corporation_id = {1};", sql_delete_corp, sizeof(sql_delete_corp));
      db_bind_t params_del[] = { db_bind_i32 (corp_id) };


      if (!db_exec (db, sql_delete_corp, params_del, 1, &err))
        {
          db_tx_rollback (db, NULL);
          LOGE ("cmd_corp_leave: Failed to delete corp: %s", err.message);
          send_response_error (ctx,
                               root,
                               ERR_DB,
                               "Database error during dissolution.");
          return 0;
        }

      if (!db_tx_commit (db, &err))
        {
          send_response_error (ctx, root, err.code, "Commit failed (leave)");
          return 0;
        }

      json_t *response_data = json_object ();


      json_object_set_new (response_data, "message",
                           json_string (
                             "You were the last member. The corporation has been dissolved."));
      send_response_ok_take (ctx, root, "corp.leave.dissolved", &response_data);
    }
  else
    {
      char sql_delete_member[512];
      sql_build (db, "DELETE FROM corp_members WHERE corp_id = {1} AND player_id = {2};", sql_delete_member, sizeof(sql_delete_member));
      db_bind_t params_del[] = { db_bind_i32 (corp_id),
                                 db_bind_i32 (ctx->player_id) };
      db_error_t err;


      if (!db_exec (db, sql_delete_member, params_del, 2, &err))
        {
          LOGE ("cmd_corp_leave: Failed to leave corp: %s", err.message);
          send_response_error (ctx,
                               root,
                               ERR_DB,
                               "Database error while leaving corporation.");
          return 0;
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
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return -1;
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
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data object.");
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
  target_player_id = (int) json_integer_value (j_target_id);
  if (ctx->player_id == target_player_id)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "You cannot invite yourself.");
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
                          inviter_role,
                          sizeof (inviter_role));
  if (strcasecmp (inviter_role, "Leader") != 0 && strcasecmp (inviter_role,
                                                              "Officer") != 0)
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
  const char *conflict_fmt = sql_conflict_target_fmt(db);
  if (!conflict_fmt)
    {
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Unsupported database backend.");
      return 0;
    }
  
  char conflict_clause[128];
  snprintf(conflict_clause, sizeof(conflict_clause),
    conflict_fmt, "corp_id, player_id");
  
  char sql_insert_invite[512];
  char sql_insert_template[512];
  snprintf(sql_insert_template, sizeof(sql_insert_template),
    "INSERT INTO corp_invites (corp_id, player_id, invited_at, expires_at) VALUES ({1}, {2}, {3}, {4}) "
    "%s UPDATE SET invited_at = excluded.invited_at, expires_at = excluded.expires_at;",
    conflict_clause);
  sql_build(db, sql_insert_template, sql_insert_invite, sizeof(sql_insert_invite));
  
  db_bind_t params[] = {
    db_bind_i32 (inviter_corp_id),
    db_bind_i32 (target_player_id),
    db_bind_i64 (time (NULL)),
    db_bind_i64 (expires_at)
  };
  db_error_t err;


  if (!db_exec (db, sql_insert_invite, params, 4, &err))
    {
      LOGE ("cmd_corp_invite: Failed to insert invite: %s", err.message);
      send_response_error (ctx,
                           root,
                           ERR_DB,
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
cmd_corp_balance (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "You are not in a corporation.");
      return 0;
    }
  long long balance = 0;


  if (db_get_corp_bank_balance (db, corp_id, &balance) != 0)
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
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
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
                           ERR_MISSING_FIELD,
                           "Missing or invalid 'amount'.");
      return 0;
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "You are not in a corporation.");
      return 0;
    }
  if (db_bank_transfer (db, "player", ctx->player_id, "corp", corp_id,
                        amount) != 0)
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
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return -1;
    }
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
  json_t *data = json_object_get (root, "data");


  if (!data)
    {
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data payload.");
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
                           ERR_MISSING_FIELD,
                           "Missing or invalid 'amount'.");
      return 0;
    }
  int corp_id = h_get_player_corp_id (db, ctx->player_id);


  if (corp_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "You are not in a corporation.");
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
  if (db_bank_transfer (db, "corp", corp_id, "player", ctx->player_id,
                        amount) != 0)
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
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
      return 0;
    }
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


  if (db_bank_get_transactions (db, "corp", corp_id, limit, NULL, 0, 0,     // tx_type_filter, start_date, end_date
                                0, 0,   // min_amount, max_amount
                                &transactions) != 0)
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
  db_t *db = game_db_get_handle ();


  if (!db)
    {
      send_response_error (ctx, root, ERR_DB, "No database handle");
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

  char sql_corp_info[512];
  sql_build (db, "SELECT name, tag, created_at, owner_id FROM corporations WHERE corporation_id = {1};", sql_corp_info, sizeof(sql_corp_info));
  db_bind_t params_corp[] = { db_bind_i32 (corp_id) };
  db_res_t *res_corp = NULL;
  db_error_t err;
  json_t *response_data = json_object ();


  if (db_query (db,
                sql_corp_info,
                params_corp,
                1,
                &res_corp,
                &err))
    {
      if (db_res_step (res_corp, &err))
        {
          json_object_set_new (response_data, "corp_id",
                               json_integer (corp_id));
          json_object_set_new (response_data, "name",
                               json_string (db_res_col_text (res_corp, 0,
                                                             &err)));
          const char *tag = db_res_col_text (res_corp, 1, &err);


          if (tag)
            {
              json_object_set_new (response_data, "tag", json_string (tag));
            }
          json_object_set_new (response_data, "created_at",
                               json_integer (db_res_col_i32 (res_corp, 2,
                                                             &err)));
          json_object_set_new (response_data, "ceo_id",
                               json_integer (db_res_col_i32 (res_corp, 3,
                                                             &err)));
        }
      db_res_finalize (res_corp);
    }
  else
    {
      LOGE ("cmd_corp_status: corp info query failed: %s", err.message);
      send_response_error (ctx, root, ERR_DB, "Database error.");
      json_decref (response_data);
      return 0;
    }

  char sql_member_count[512];
  sql_build (db, "SELECT COUNT(*) FROM corp_members WHERE corp_id = {1};", sql_member_count, sizeof(sql_member_count));
  db_bind_t params_count[] = { db_bind_i32 (corp_id) };
  db_res_t *res_count = NULL;


  if (db_query (db, sql_member_count, params_count, 1, &res_count, &err))
    {
      if (db_res_step (res_count, &err))
        {
          json_object_set_new (response_data,
                               "member_count",
                               json_integer (db_res_col_i32 (res_count, 0,
                                                             &err)));
        }
      db_res_finalize (res_count);
    }
  else
    {
      LOGE ("cmd_corp_status: member count query failed: %s", err.message);
      send_response_error (ctx, root, ERR_DB, "Database error.");
      json_decref (response_data);
      return 0;
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


  if (h_get_corp_stock_id (db, corp_id, &stock_id) == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Your corporation is already publicly traded.");
      return 0;
    }
  /* Check credit rating */
  int credit_rating = 0;


  if (h_get_corp_credit_rating (db, corp_id, &credit_rating) != 0
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
  db_error_t err;
  char sql_insert_stock[512];
  sql_build (db, "INSERT INTO stocks (corp_id, ticker, total_shares, par_value, current_price) VALUES ({1}, {2}, {3}, {4}, {5});", sql_insert_stock, sizeof(sql_insert_stock));
  db_bind_t params_stock[] = { db_bind_i32 (corp_id), db_bind_text (ticker),
                               db_bind_i32 (total_shares),
                               db_bind_i32 (par_value),
                               db_bind_i32 (par_value) };
  int64_t new_stock_id_64 = 0;


  if (!db_exec_insert_id (db,
                          sql_insert_stock,
                          params_stock,
                          5,
                          &new_stock_id_64,
                          &err))
    {
      LOGE ("cmd_stock_ipo_register: Failed to insert stock: %s", err.message);
      if (err.code == ERR_DB_CONSTRAINT)
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
                               ERR_DB,
                               "Database error during IPO registration.");
        }
      return 0;
    }
  int new_stock_id = (int) new_stock_id_64;

  // Distribute initial shares to the corporation itself (as a shareholder)
  int rc = h_update_player_shares (db, 0, new_stock_id, total_shares);      // player_id 0 for corporation


  if (rc != 0)
    {
      LOGE (
        "cmd_stock_ipo_register: Failed to distribute initial shares to corp %d for stock %d: %d",
        corp_id,
        new_stock_id,
        rc);
      // This is a critical error, consider rolling back or marking stock invalid
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string (
                         "Corporation successfully registered for IPO."));
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


  if (rc != 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Stock not found.");
      free (ticker);
      return 0;
    }
  long long total_cost = (long long) quantity * current_price;
  // Check player balance
  long long player_balance;


  if (db_get_player_bank_balance (db, ctx->player_id, &player_balance) != 0
      || player_balance < total_cost)
    {
      send_response_error (ctx,
                           root,
                           ERR_INSUFFICIENT_FUNDS,
                           "Insufficient funds to purchase shares.");
      free (ticker);
      return 0;
    }
  // Perform transfer
  rc = db_bank_transfer (db,
                         "player",
                         ctx->player_id,
                         "corp",
                         corp_id,
                         total_cost);
  if (rc != 0)
    {
      LOGE ("cmd_stock_buy: Bank transfer failed for player %d, stock %d: %d",
            ctx->player_id, stock_id, rc);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Failed to complete share purchase due to banking error.");
      free (ticker);
      return 0;
    }
  // Update player shares
  rc = h_update_player_shares (db, ctx->player_id, stock_id, quantity);
  if (rc != 0)
    {
      LOGE (
        "cmd_stock_buy: Failed to update player shares for player %d, stock %d: %d",
        ctx->player_id,
        stock_id,
        rc);
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
cmd_stock_dividend_set (client_ctx_t *ctx,
                        json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return -1;
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
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data object.");
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


  if (h_get_corp_stock_id (db, corp_id, &stock_id) != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Your corporation is not publicly traded.");
      return 0;
    }
  int amount_per_share;


  if (!json_get_int_flexible (data, "amount_per_share",
                              &amount_per_share) || amount_per_share < 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid 'amount_per_share'.");
      return 0;
    }
  // Get total shares to calculate total dividend payout
  int total_shares = 0;
  int rc = h_get_stock_info (db,
                             stock_id,
                             NULL,
                             NULL,
                             &total_shares,
                             NULL,
                             NULL,
                             NULL);


  if (rc != 0)
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


  if (db_get_corp_bank_balance (db, corp_id,
                                &corp_balance) != 0 ||
      corp_balance < total_payout)
    {
      send_response_error (ctx,
                           root,
                           ERR_INSUFFICIENT_FUNDS,
                           "Corporation has insufficient funds to declare this dividend.");
      return 0;
    }

  db_error_t err;
  char sql_insert_dividend[512];
  sql_build (db, "INSERT INTO stock_dividends (stock_id, amount_per_share, declared_ts) VALUES ({1}, {2}, {3});", sql_insert_dividend, sizeof(sql_insert_dividend));
  db_bind_t params[] = { db_bind_i32 (stock_id), db_bind_i32 (amount_per_share),
                         db_bind_i64 (time (NULL)) };


  if (!db_exec (db, sql_insert_dividend, params, 3, &err))
    {
      LOGE ("cmd_stock_dividend_set: Failed to insert dividend: %s",
            err.message);
      send_response_error (ctx,
                           root,
                           ERR_DB,
                           "Database error declaring dividend.");
      return 0;
    }
  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Dividend declared successfully."));
  json_object_set_new (response_data, "stock_id", json_integer (stock_id));
  json_object_set_new (response_data, "amount_per_share",
                       json_integer (amount_per_share));
  json_object_set_new (response_data,
                       "total_payout",
                       json_integer (total_payout));
  send_response_ok_take (ctx, root, "stock.dividend.set.success",
                         &response_data);
  json_t *payload = json_object ();


  json_object_set_new (payload, "corp_id", json_integer (corp_id));
  json_object_set_new (payload, "stock_id", json_integer (stock_id));
  json_object_set_new (payload, "amount_per_share",
                       json_integer (amount_per_share));
  json_object_set_new (payload, "total_payout", json_integer (total_payout));

  db_log_engine_event (time (NULL),
                       "stock.dividend.declared",
                       "corp",
                       corp_id,
                       0,
                       payload,
                       NULL);
  json_decref (payload);
  return 0;
}


int
cmd_stock_sell (client_ctx_t *ctx, json_t *root)
{
  if (ctx->player_id == 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_NOT_AUTHENTICATED,
                           "Authentication required.");
      return -1;
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
      send_response_error (ctx, root, ERR_BAD_REQUEST, "Missing data object.");
      return 0;
    }
  int stock_id;


  if (!json_get_int_flexible (data, "stock_id", &stock_id) || stock_id <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid 'stock_id'.");
      return 0;
    }
  int quantity;


  if (!json_get_int_flexible (data, "quantity", &quantity) || quantity <= 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_BAD_REQUEST,
                           "Missing or invalid 'quantity'.");
      return 0;
    }

  char *ticker = NULL;
  int corp_id = 0;
  int current_price = 0;


  if (h_get_stock_info (db,
                        stock_id,
                        &ticker,
                        &corp_id,
                        NULL,
                        NULL,
                        &current_price,
                        NULL) != 0)
    {
      send_response_error (ctx, root, ERR_INVALID_ARG, "Stock not found.");
      return 0;
    }

  long long total_proceeds = (long long) quantity * current_price;

  // Verify shares owned
  char sql_shares[512];
  sql_build (db, "SELECT shares FROM corp_shareholders WHERE player_id = {1} AND corp_id = (SELECT corp_id FROM stocks WHERE id = {2});", sql_shares, sizeof(sql_shares));
  db_bind_t params_s[] = { db_bind_i32 (ctx->player_id),
                           db_bind_i32 (stock_id) };
  db_res_t *res_s = NULL;
  db_error_t err;
  int shares_owned = 0;


  if (db_query (db, sql_shares, params_s, 2, &res_s, &err))
    {
      if (db_res_step (res_s, &err))
        {
          shares_owned = db_res_col_i32 (res_s, 0, &err);
        }
      db_res_finalize (res_s);
    }

  if (shares_owned < quantity)
    {
      send_response_error (ctx,
                           root,
                           ERR_INVALID_ARG,
                           "Insufficient shares to sell.");
      free (ticker);
      return 0;
    }

  // Perform transfer: corp to player
  if (db_bank_transfer (db,
                        "corp",
                        corp_id,
                        "player",
                        ctx->player_id,
                        total_proceeds) != 0)
    {
      send_response_error (ctx,
                           root,
                           ERR_INSUFFICIENT_FUNDS,
                           "Corporation has insufficient funds to buy back shares.");
      free (ticker);
      return 0;
    }

  // Update player shares
  if (h_update_player_shares (db, ctx->player_id, stock_id, -quantity) != 0)
    {
      LOGE (
        "cmd_stock_sell: Failed to update player shares for player %d, stock %d",
        ctx->player_id,
        stock_id);
      send_response_error (ctx,
                           root,
                           ERR_SERVER_ERROR,
                           "Failed to update shares.");
      free (ticker);
      return 0;
    }

  json_t *response_data = json_object ();


  json_object_set_new (response_data, "message",
                       json_string ("Shares sold successfully."));
  json_object_set_new (response_data, "stock_id", json_integer (stock_id));
  json_object_set_new (response_data, "ticker", json_string (ticker));
  json_object_set_new (response_data, "quantity", json_integer (quantity));
  json_object_set_new (response_data, "total_proceeds",
                       json_integer (total_proceeds));
  send_response_ok_take (ctx, root, "stock.sell.success", &response_data);

  json_t *payload = json_object ();


  json_object_set_new (payload, "player_id", json_integer (ctx->player_id));
  json_object_set_new (payload, "stock_id", json_integer (stock_id));
  json_object_set_new (payload, "quantity", json_integer (quantity));
  json_object_set_new (payload, "proceeds", json_integer (total_proceeds));
  db_log_engine_event (time (NULL),
                       "stock.sell",
                       "player",
                       ctx->player_id,
                       0,
                       payload,
                       NULL);
  json_decref (payload);

  free (ticker);
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
  else if (strcasecmp (subcommand, "sell") == 0)
    {
      return cmd_stock_sell (ctx, root);
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


int
h_corp_is_publicly_traded (db_t *db, int corp_id, bool *is_publicly_traded)
{
  if (!db || corp_id <= 0 || !is_publicly_traded)
    {
      return ERR_INVALID_ARG;
    }

  char sql[512];
  sql_build (db, "SELECT 1 FROM stocks WHERE corp_id = {1};", sql, sizeof(sql));
  db_bind_t params[] = { db_bind_i32 (corp_id) };
  db_res_t *res = NULL;
  db_error_t err;
  int rc = 0;


  *is_publicly_traded = false;

  if (db_query (db, sql, params, 1, &res, &err))
    {
      if (db_res_step (res, &err))
        {
          *is_publicly_traded = true;
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


int
h_daily_corp_tax (db_t *db, int64_t now_s)
{
  if (!db)
    {
      return ERR_INVALID_ARG;
    }

  const char *sql_corps = "SELECT id, name FROM corporations;";
  db_res_t *res_corps = NULL;
  db_error_t err;


  if (!db_query (db, sql_corps, NULL, 0, &res_corps, &err))
    {
      LOGE ("h_daily_corp_tax: Failed to fetch corporations: %s", err.message);
      return err.code;
    }

  while (db_res_step (res_corps, &err))
    {
      int corp_id = db_res_col_i32 (res_corps, 0, &err);
      const char *corp_name = db_res_col_text (res_corps, 1, &err);
      long long balance = 0;


      if (db_get_corp_bank_balance (db, corp_id, &balance) == 0 && balance > 0)
        {
          long long tax_amount = (balance * CORP_TAX_RATE_BP) / 10000;


          if (tax_amount > 0)
            {
              if (h_deduct_credits (db,
                                    "corp",
                                    corp_id,
                                    tax_amount,
                                    "TAX",
                                    NULL,
                                    NULL) == 0)
                {
                  LOGI ("Daily tax of %lld deducted from corp %s (%d)",
                        tax_amount,
                        corp_name,
                        corp_id);
                }
            }
        }
    }
  db_res_finalize (res_corps);
  return 0;
}


int
h_dividend_payout (db_t *db, int64_t now_s)
{
  if (!db)
    {
      return ERR_INVALID_ARG;
    }

  const char *sql_unpaid =
    "SELECT id, stock_id, amount_per_share FROM stock_dividends WHERE paid_ts IS NULL;";
  db_res_t *res_unpaid = NULL;
  db_error_t err;


  if (!db_query (db, sql_unpaid, NULL, 0, &res_unpaid, &err))
    {
      LOGE ("h_dividend_payout: Failed to fetch unpaid dividends: %s",
            err.message);
      return err.code;
    }

  while (db_res_step (res_unpaid, &err))
    {
      int div_id = db_res_col_i32 (res_unpaid, 0, &err);
      int stock_id = db_res_col_i32 (res_unpaid, 1, &err);
      int amount_per_share = db_res_col_i32 (res_unpaid, 2, &err);

      int corp_id = 0;
      int total_shares = 0;


      if (h_get_stock_info (db,
                            stock_id,
                            NULL,
                            &corp_id,
                            &total_shares,
                            NULL,
                            NULL,
                            NULL) != 0)
        {
          continue;
        }

      long long total_payout = (long long)amount_per_share * total_shares;
      long long corp_balance = 0;


      if (db_get_corp_bank_balance (db, corp_id,
                                    &corp_balance) == 0 &&
          corp_balance >= total_payout)
        {
          if (db_tx_begin (db, DB_TX_IMMEDIATE, &err))
            {
              bool ok = true;


              if (h_deduct_credits (db,
                                    "corp",
                                    corp_id,
                                    total_payout,
                                    "DIVIDEND",
                                    NULL,
                                    NULL) != 0)
                {
                  ok = false;
                }

              if (ok)
                {
              char sql_holders[512];
              sql_build (db, "SELECT player_id, shares FROM corp_shareholders WHERE corp_id = (SELECT corp_id FROM stocks WHERE id = {1}) AND shares > 0;", sql_holders, sizeof(sql_holders));
              db_bind_t params_h[] = { db_bind_i32 (stock_id) };
                  db_res_t *res_holders = NULL;


                  if (db_query (db, sql_holders, params_h, 1, &res_holders,
                                &err))
                    {
                      while (db_res_step (res_holders, &err))
                        {
                          int player_id = db_res_col_i32 (res_holders, 0, &err);
                          int shares = db_res_col_i32 (res_holders, 1, &err);
                          long long player_payout = (long long)shares *
                                                    amount_per_share;


                          if (player_id == 0)   // Corporation itself
                            {
                              h_add_credits (db,
                                             "corp",
                                             corp_id,
                                             player_payout,
                                             "DIVIDEND_PAYOUT",
                                             NULL,
                                             NULL);
                            }
                          else
                            {
                              h_add_credits (db,
                                             "player",
                                             player_id,
                                             player_payout,
                                             "DIVIDEND_PAYOUT",
                                             NULL,
                                             NULL);
                            }
                        }
                      db_res_finalize (res_holders);
                    }
                  else
                    {
                      ok = false;
                    }
                }

              if (ok)
                {
              char sql_mark_paid[512];
              sql_build (db, "UPDATE stock_dividends SET paid_ts = {1} WHERE id = {2};", sql_mark_paid, sizeof(sql_mark_paid));
              db_bind_t params_paid[] = { db_bind_i64 (now_s),
                                          db_bind_i32 (div_id) };


                  if (!db_exec (db, sql_mark_paid, params_paid, 2, &err))
                    {
                      ok = false;
                    }
                }

              if (ok)
                {
                  db_tx_commit (db, &err);
                  LOGI (
                    "Dividend payout for stock %d (div_id %d) completed. Total: %lld",
                    stock_id,
                    div_id,
                    total_payout);
                }
              else
                {
                  db_tx_rollback (db, NULL);
                  LOGE ("Dividend payout failed for stock %d (div_id %d)",
                        stock_id,
                        div_id);
                }
            }
        }
      else
        {
          LOGW (
            "Corp %d has insufficient funds for dividend payout of stock %d",
            corp_id,
            stock_id);
        }
    }
  db_res_finalize (res_unpaid);
  return 0;
}

