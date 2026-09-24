#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "db/repo/repo_cargo.h"
#include "db/sql_driver.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static const char *valid_commodity_codes[] = {
    "ORE", "ORG", "EQU", "COL", "SLV", "WPN", "DRG", NULL
};

/* Helper: validate commodity code */
static int
is_valid_commodity_code (const char *code)
{
  if (!code || code[0] == '\0')
    return 0;

  for (int i = 0; valid_commodity_codes[i]; i++)
    {
      if (strcasecmp (code, valid_commodity_codes[i]) == 0)
        return 1;
    }
  return 0;
}

/* Normalize code to uppercase for database storage */
static void
normalize_commodity_code (const char *input, char *output, size_t out_sz)
{
  if (!input || !output || out_sz < 4)
    return;

  int i;
  for (i = 0; input[i] && i < (int)out_sz - 1; i++)
    {
      output[i] = toupper ((unsigned char) input[i]);
    }
  output[i] = '\0';
}

/* Get legacy column name for commodity code (for dual-write) */
static const char *
get_legacy_column_name (const char *code)
{
  char upper[4];
  normalize_commodity_code (code, upper, sizeof (upper));

  if (strcmp (upper, "ORE") == 0)
    return "ore";
  if (strcmp (upper, "ORG") == 0)
    return "organics";
  if (strcmp (upper, "EQU") == 0)
    return "equipment";
  if (strcmp (upper, "COL") == 0)
    return "colonists";
  if (strcmp (upper, "SLV") == 0)
    return "slaves";
  if (strcmp (upper, "WPN") == 0)
    return "weapons";
  if (strcmp (upper, "DRG") == 0)
    return "drugs";

  return NULL;
}

int
repo_cargo_get_total (db_t *db, int32_t ship_id, int64_t *total_out)
{
  if (!db || ship_id <= 0 || !total_out)
    return ERR_DB_MISUSE;

  *total_out = 0;

  /* SQL_VERBATIM: Q1 */
  const char *sql =
      "SELECT COALESCE(SUM(quantity), 0) "
      "FROM ship_cargo "
      "WHERE ship_id = {1};";

  db_bind_t params[] = {
      db_bind_i32 (ship_id),
  };

  db_res_t *res = NULL;
  db_error_t err;
  char sql_converted[512];
  sql_build (db, sql, sql_converted, sizeof (sql_converted));

  if (!db_query (db, sql_converted, params, 1, &res, &err))
    {
      return err.code;
    }

  if (res && db_res_step (res, &err))
    {
      *total_out = db_res_col_i64 (res, 0, &err);
      if (err.code != 0)
        {
          db_res_finalize (res);
          return err.code;
        }
    }

  if (res)
    db_res_finalize (res);
  return 0;
}

int
repo_cargo_get (db_t *db, int32_t ship_id, const char *commodity_code, int64_t *quantity_out)
{
  if (!db || ship_id <= 0 || !commodity_code || !quantity_out)
    return ERR_DB_MISUSE;

  *quantity_out = 0;

  char code_upper[4];
  normalize_commodity_code (commodity_code, code_upper, sizeof (code_upper));

  /* SQL_VERBATIM: Q2 */
  const char *sql =
      "SELECT COALESCE(quantity, 0) "
      "FROM ship_cargo "
      "WHERE ship_id = {1} AND commodity_code = {2};";

  db_bind_t params[] = {
      db_bind_i32 (ship_id),
      db_bind_text (code_upper),
  };

  db_res_t *res = NULL;
  db_error_t err;
  char sql_converted[512];
  sql_build (db, sql, sql_converted, sizeof (sql_converted));

  if (!db_query (db, sql_converted, params, 2, &res, &err))
    {
      return err.code;
    }

  if (res && db_res_step (res, &err))
    {
      *quantity_out = db_res_col_i64 (res, 0, &err);
      if (err.code != 0)
        {
          db_res_finalize (res);
          return err.code;
        }
    }

  if (res)
    db_res_finalize (res);
  return 0;
}

int
repo_cargo_add (db_t *db, int32_t ship_id, const char *commodity_code, int64_t delta, int64_t *new_quantity_out)
{
  if (!db || ship_id <= 0 || !commodity_code)
    return ERR_DB_MISUSE;

  if (!is_valid_commodity_code (commodity_code))
    return ERR_INVALID_ARG;

  char code_upper[4];
  normalize_commodity_code (commodity_code, code_upper, sizeof (code_upper));

  db_error_t err;

  /* Get current holds */
  int32_t holds = 0;
  {
    /* SQL_VERBATIM: Q3 */
    const char *sql = "SELECT holds FROM ships WHERE ship_id = {1};";
    db_bind_t params[] = {db_bind_i32 (ship_id)};
    db_res_t *res = NULL;
    char sql_converted[256];
    sql_build (db, sql, sql_converted, sizeof (sql_converted));

    if (!db_query (db, sql_converted, params, 1, &res, &err))
      return err.code;

    if (!res || !db_res_step (res, &err))
      {
        if (res)
          db_res_finalize (res);
        return ERR_SHIP_NOT_FOUND;
      }

    holds = db_res_col_i32 (res, 0, &err);
    if (err.code != 0)
      {
        db_res_finalize (res);
        return err.code;
      }
    db_res_finalize (res);
  }

  /* Get current cargo quantities */
  int64_t current_qty = 0;
  if (repo_cargo_get (db, ship_id, code_upper, &current_qty) != 0)
    return ERR_DB_MISUSE;

  int64_t new_qty = current_qty + delta;

  /* Validate: new quantity must be non-negative */
  if (new_qty < 0)
    {
      return ERR_DB_MISUSE;
    }

  /* Get total cargo (excluding this commodity) */
  int64_t total_other = 0;
  {
    /* SQL_VERBATIM: Q4 */
    const char *sql =
        "SELECT COALESCE(SUM(quantity), 0) "
        "FROM ship_cargo "
        "WHERE ship_id = {1} AND commodity_code != {2};";
    db_bind_t params[] = {db_bind_i32 (ship_id), db_bind_text (code_upper)};
    db_res_t *res = NULL;
    char sql_converted[512];
    sql_build (db, sql, sql_converted, sizeof (sql_converted));

    if (!db_query (db, sql_converted, params, 2, &res, &err))
      return err.code;

    if (res && db_res_step (res, &err))
      {
        total_other = db_res_col_i64 (res, 0, &err);
        if (err.code != 0)
          {
            db_res_finalize (res);
            return err.code;
          }
      }
    if (res)
      db_res_finalize (res);
  }

  int64_t total_after = total_other + new_qty;

  /* Enforce holds capacity */
  if (total_after > (int64_t) holds)
    {
      if (new_quantity_out)
        *new_quantity_out = current_qty;
      return ERR_HOLD_FULL;
    }

  /* Insert or update ship_cargo using portable approach */
  if (current_qty == 0 && new_qty > 0)
    {
      /* Insert new row */
      /* SQL_VERBATIM: Q5 */
      const char *sql =
          "INSERT INTO ship_cargo (ship_id, commodity_code, quantity) "
          "VALUES ({1}, {2}, {3});";

      db_bind_t params[] = {
          db_bind_i32 (ship_id),
          db_bind_text (code_upper),
          db_bind_i64 (new_qty),
      };

      char sql_converted[512];
      sql_build (db, sql, sql_converted, sizeof (sql_converted));

      if (!db_exec (db, sql_converted, params, 3, &err))
        {
          return err.code;
        }
    }
  else if (current_qty > 0)
    {
      if (new_qty == 0)
        {
          /* Delete row */
          /* SQL_VERBATIM: Q6 */
          const char *sql =
              "DELETE FROM ship_cargo "
              "WHERE ship_id = {1} AND commodity_code = {2};";

          db_bind_t params[] = {
              db_bind_i32 (ship_id),
              db_bind_text (code_upper),
          };

          char sql_converted[512];
          sql_build (db, sql, sql_converted, sizeof (sql_converted));

          if (!db_exec (db, sql_converted, params, 2, &err))
            {
              return err.code;
            }
        }
      else
        {
          /* Update existing row */
          /* SQL_VERBATIM: Q7 */
          const char *sql =
              "UPDATE ship_cargo "
              "SET quantity = {1} "
              "WHERE ship_id = {2} AND commodity_code = {3};";

          db_bind_t params[] = {
              db_bind_i64 (new_qty),
              db_bind_i32 (ship_id),
              db_bind_text (code_upper),
          };

          char sql_converted[512];
          sql_build (db, sql, sql_converted, sizeof (sql_converted));

          if (!db_exec (db, sql_converted, params, 3, &err))
            {
              return err.code;
            }
        }
    }

  /* Dual-write: update legacy column if it exists */
  {
    const char *legacy_col = get_legacy_column_name (code_upper);
    if (legacy_col)
      {
        /* SQL_VERBATIM: Q8 */
        char sql_buf[512];
        snprintf (sql_buf, sizeof (sql_buf),
                  "UPDATE ships SET %s = {1} WHERE ship_id = {2};",
                  legacy_col);

        db_bind_t params[] = {
            db_bind_i64 (new_qty),
            db_bind_i32 (ship_id),
        };

        char sql_converted[512];
        sql_build (db, sql_buf, sql_converted, sizeof (sql_converted));

        if (!db_exec (db, sql_converted, params, 2, &err))
          {
            /* Log but don't fail: legacy column sync is best-effort */
          }
      }
  }

  if (new_quantity_out)
    *new_quantity_out = new_qty;

  return 0;
}

int
repo_cargo_sync_legacy_columns (db_t *db, int32_t ship_id)
{
  if (!db || ship_id <= 0)
    return ERR_DB_MISUSE;

  db_error_t err;

  /* Verify ship exists */
  {
    /* SQL_VERBATIM: Q9 */
    const char *sql = "SELECT ship_id FROM ships WHERE ship_id = {1} LIMIT 1;";
    db_bind_t params[] = {db_bind_i32 (ship_id)};
    db_res_t *res = NULL;
    char sql_converted[256];
    sql_build (db, sql, sql_converted, sizeof (sql_converted));

    if (!db_query (db, sql_converted, params, 1, &res, &err))
      return err.code;

    if (!res || !db_res_step (res, &err))
      {
        if (res)
          db_res_finalize (res);
        return ERR_SHIP_NOT_FOUND;
      }
    db_res_finalize (res);
  }

  /* For each legacy column, sync from ship_cargo */
  const char *legacy_columns[] = {"ore", "organics", "equipment", "colonists", "slaves", "weapons", "drugs"};
  const char *commodity_codes[] = {"ORE", "ORG", "EQU", "COL", "SLV", "WPN", "DRG"};

  for (int i = 0; i < 7; i++)
    {
      int64_t qty = 0;
      if (repo_cargo_get (db, ship_id, commodity_codes[i], &qty) != 0)
        continue; /* Best effort */

      /* SQL_VERBATIM: Q10 */
      char sql_buf[256];
      snprintf (sql_buf, sizeof (sql_buf),
                "UPDATE ships SET %s = {1} WHERE ship_id = {2};",
                legacy_columns[i]);

      db_bind_t params[] = {
          db_bind_i64 (qty),
          db_bind_i32 (ship_id),
      };

      char sql_converted[512];
      sql_build (db, sql_buf, sql_converted, sizeof (sql_converted));

      if (!db_exec (db, sql_converted, params, 2, &err))
        {
          /* Best effort, continue */
        }
    }

  return 0;
}

