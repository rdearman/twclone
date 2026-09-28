#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_trade_offers.h"
#include "db/sql_driver.h"
#include "errors.h"

#include <stdio.h>
#include <string.h>

static void
copy_column_text (char *out, size_t out_size, const char *value)
{
  if (!out || out_size == 0)
    return;
  snprintf (out, out_size, "%s", value ? value : "");
}

int
repo_trade_offer_get (db_t *db, int64_t offer_id, int for_update,
                      trade_offer_t *offer_out)
{
  if (!db || offer_id <= 0 || !offer_out)
    return ERR_INVALID_ARG;

  db_res_t *res = NULL;
  db_error_t err;
  char sql[768];
  const char *query =
    "SELECT trade_offer_id, sender_player_id, recipient_player_id, "
    "commodity_code, mode, quantity, unit_price, status, created_at, expires_at "
    "FROM trade_offers WHERE trade_offer_id = {1}";
  if (for_update)
    {
      char query_locked[800];
      snprintf (query_locked, sizeof query_locked, "%s FOR UPDATE", query);
      sql_build (db, query_locked, sql, sizeof sql);
    }
  else
    sql_build (db, query, sql, sizeof sql);

  if (!db_query (db, sql, (db_bind_t[]){db_bind_i64 (offer_id)}, 1, &res,
                 &err))
    return err.code ? err.code : ERR_DB_QUERY_FAILED;
  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      return ERR_DB_NOT_FOUND;
    }

  memset (offer_out, 0, sizeof *offer_out);
  offer_out->offer_id = db_res_col_i64 (res, 0, &err);
  offer_out->sender_player_id = db_res_col_i32 (res, 1, &err);
  offer_out->recipient_player_id = db_res_col_i32 (res, 2, &err);
  copy_column_text (offer_out->commodity_code,
                    sizeof offer_out->commodity_code,
                    db_res_col_text (res, 3, &err));
  copy_column_text (offer_out->mode, sizeof offer_out->mode,
                    db_res_col_text (res, 4, &err));
  offer_out->quantity = db_res_col_i32 (res, 5, &err);
  offer_out->unit_price = db_res_col_i64 (res, 6, &err);
  copy_column_text (offer_out->status, sizeof offer_out->status,
                    db_res_col_text (res, 7, &err));
  copy_column_text (offer_out->created_at, sizeof offer_out->created_at,
                    db_res_col_text (res, 8, &err));
  copy_column_text (offer_out->expires_at, sizeof offer_out->expires_at,
                    db_res_col_text (res, 9, &err));
  db_res_finalize (res);
  return err.code == 0 ? 0 : err.code;
}

static int
repo_trade_offer_get_by_key (db_t *db, int sender_player_id,
                             const char *idempotency_key,
                             trade_offer_t *offer_out)
{
  db_res_t *res = NULL;
  db_error_t err;
  char sql[512];
  sql_build (db,
             "SELECT trade_offer_id FROM trade_offers WHERE sender_player_id = {1} AND idempotency_key = {2};",
             sql, sizeof sql);
  if (!db_query (db, sql,
                 (db_bind_t[]){db_bind_i64 (sender_player_id),
                               db_bind_text (idempotency_key)},
                 2, &res, &err))
    return err.code ? err.code : ERR_DB_QUERY_FAILED;
  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      return ERR_DB_NOT_FOUND;
    }
  int64_t offer_id = db_res_col_i64 (res, 0, &err);
  db_res_finalize (res);
  if (err.code != 0)
    return err.code;
  return repo_trade_offer_get (db, offer_id, 0, offer_out);
}

int
repo_trade_offer_find_idempotent (db_t *db, int sender_player_id,
                                  int recipient_player_id,
                                  const char *commodity_code,
                                  const char *mode, int quantity,
                                  int64_t unit_price,
                                  const char *idempotency_key,
                                  trade_offer_t *offer_out)
{
  if (!db || sender_player_id <= 0 || recipient_player_id <= 0
      || !commodity_code || !mode || quantity <= 0 || unit_price < 0
      || !idempotency_key || !*idempotency_key || !offer_out)
    return ERR_INVALID_ARG;

  int rc = repo_trade_offer_get_by_key (db, sender_player_id,
                                        idempotency_key, offer_out);
  if (rc != 0)
    return rc;
  if (offer_out->recipient_player_id != recipient_player_id
      || strcmp (offer_out->commodity_code, commodity_code) != 0
      || strcmp (offer_out->mode, mode) != 0
      || offer_out->quantity != quantity
      || offer_out->unit_price != unit_price)
    return ERR_INVALID_ARG;
  return 0;
}

int
repo_trade_offer_create (db_t *db, int sender_player_id,
                         int recipient_player_id,
                         const char *commodity_code, const char *mode,
                         int quantity, int64_t unit_price,
                         int64_t expires_at, const char *idempotency_key,
                         trade_offer_t *offer_out, int *created_out)
{
  if (!db || sender_player_id <= 0 || recipient_player_id <= 0
      || sender_player_id == recipient_player_id || !commodity_code
      || !mode || (strcmp (mode, "buy") != 0 && strcmp (mode, "sell") != 0)
      || quantity <= 0 || unit_price < 0 || expires_at <= 0 || !offer_out
      || !created_out)
    return ERR_INVALID_ARG;

  db_error_t err;
  char sql[1024];
  sql_build (db,
             "INSERT INTO trade_offers (sender_player_id, recipient_player_id, commodity_code, mode, quantity, unit_price, expires_at, idempotency_key) VALUES ({1}, {2}, {3}, {4}, {5}, {6}, {7}, {8})",
             sql, sizeof sql);
  int64_t offer_id = 0;
  db_bind_t params[] = {
    db_bind_i64 (sender_player_id),
    db_bind_i64 (recipient_player_id),
    db_bind_text (commodity_code),
    db_bind_text (mode),
    db_bind_i64 (quantity),
    db_bind_i64 (unit_price),
    db_bind_timestamp_text (expires_at),
    idempotency_key && *idempotency_key ? db_bind_text (idempotency_key) :
    db_bind_null ()
  };

  if (db_exec_insert_id (db, sql, params,
                         sizeof params / sizeof params[0],
                         "trade_offer_id", &offer_id, &err))
    {
      *created_out = 1;
      return repo_trade_offer_get (db, offer_id, 0, offer_out);
    }

  if (idempotency_key && *idempotency_key
      && err.code == ERR_DB_CONSTRAINT)
    {
      int rc = repo_trade_offer_find_idempotent (
        db, sender_player_id, recipient_player_id, commodity_code, mode,
        quantity, unit_price, idempotency_key, offer_out);
      if (rc != 0)
        return rc;
      *created_out = 0;
      return 0;
    }
  return err.code ? err.code : ERR_DB_EXEC_FAILED;
}

int
repo_trade_offer_transition (db_t *db, int64_t offer_id, const char *status,
                             int64_t now_s, int *changed_out)
{
  if (!db || offer_id <= 0 || !status || !changed_out)
    return ERR_INVALID_ARG;

  const char *query = NULL;
  if (strcmp (status, "accepted") == 0)
    {
      query = "UPDATE trade_offers SET status = {1}, accepted_at = {2} WHERE trade_offer_id = {3} AND status = 'pending' AND expires_at > {2};";
    }
  else if (strcmp (status, "cancelled") == 0)
    {
      query = "UPDATE trade_offers SET status = {1}, cancelled_at = {2} WHERE trade_offer_id = {3} AND status = 'pending' AND expires_at > {2};";
    }
  else if (strcmp (status, "expired") == 0)
    query = "UPDATE trade_offers SET status = {1}, expired_at = {2} WHERE trade_offer_id = {3} AND status = 'pending' AND expires_at <= {2};";
  else
    return ERR_INVALID_ARG;

  db_error_t err;
  int64_t changed = 0;
  char sql[640];
  sql_build (db, query, sql, sizeof sql);
  if (!db_exec_rows_affected (db, sql,
                              (db_bind_t[]){db_bind_text (status),
                                            db_bind_timestamp_text (now_s),
                                            db_bind_i64 (offer_id)},
                              3, &changed, &err))
    return err.code ? err.code : ERR_DB_EXEC_FAILED;
  *changed_out = changed > 0;
  return 0;
}
