#define TW_DB_INTERNAL 1
#include "db_int.h"
#include "repo_sector_notices.h"
#include "db/sql_driver.h"
#include "errors.h"

#include <stdio.h>
#include <stdlib.h>

static int
repo_sector_notice_find_by_key (db_t *db, const char *notice_key,
                                int64_t *notice_id_out)
{
  db_res_t *res = NULL;
  db_error_t err;
  char sql[384];
  sql_build (db,
             "SELECT system_notice_id FROM system_notice WHERE notice_key = {1};",
             sql, sizeof sql);
  if (!db_query (db, sql, (db_bind_t[]){db_bind_text (notice_key)}, 1, &res,
                 &err))
    {
      return -1;
    }
  if (!db_res_step (res, &err))
    {
      db_res_finalize (res);
      return 1;
    }
  *notice_id_out = db_res_col_i64 (res, 0, &err);
  db_res_finalize (res);
  return err.code == 0 ? 0 : -1;
}

int
repo_sector_notice_create (db_t *db, int sector_id, int player_id,
                           const char *subtype, json_t *details,
                           int64_t created_at, int64_t expires_at,
                           const char *notice_key, int64_t *notice_id_out,
                           int *created_out)
{
  if (!db || sector_id <= 0 || player_id <= 0 || !subtype || !*subtype
      || !json_is_object (details) || !notice_id_out || !created_out
      || expires_at <= created_at)
    {
      return -1;
    }

  char *meta = json_dumps (details, JSON_COMPACT | JSON_SORT_KEYS);
  if (!meta)
    {
      return -1;
    }

  db_error_t err;
  char sql[768];
  sql_build (db,
             "INSERT INTO system_notice (created_at, title, body, severity, expires_at, scope, sector_id, player_id, meta, ephemeral, notice_key) VALUES ({1}, {2}, {3}, {4}, {5}, {6}, {7}, {8}, {9}::jsonb, {10}, {11})",
             sql, sizeof sql);
  db_bind_t params[] = {
    db_bind_timestamp_text (created_at),
    db_bind_text ("Sector notice"),
    db_bind_text (subtype),
    db_bind_text ("info"),
    db_bind_timestamp_text (expires_at),
    db_bind_text ("sector"),
    db_bind_i64 (sector_id),
    db_bind_i64 (player_id),
    db_bind_text (meta),
    db_bind_bool (false),
    notice_key && *notice_key ? db_bind_text (notice_key) : db_bind_null ()
  };
  int ok = db_exec_insert_id (db, sql, params,
                              sizeof params / sizeof params[0],
                              "system_notice_id", notice_id_out, &err);
  free (meta);
  if (ok)
    {
      *created_out = 1;
      return 0;
    }

  /* Duplicate request IDs resolve to the original durable notice. */
  if (notice_key && *notice_key && err.code == ERR_DB_CONSTRAINT)
    {
      int found = repo_sector_notice_find_by_key (db, notice_key,
                                                  notice_id_out);
      if (found == 0)
        {
          *created_out = 0;
          return 0;
        }
    }
  return err.code ? err.code : -1;
}
