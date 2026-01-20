#define TW_DB_INTERNAL 1

#include "db_mysql.h"
#include "db_int.h"
#include "errors.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Phase 6A: MySQL backend skeleton.
 *
 * Must compile/link. May return ERR_NOT_IMPLEMENTED for all operations.
 * No stderr logging from stubs.
 */

struct db_mysql_impl_s { int unused; };

static bool
mysql_not_impl(db_error_t *err, const char *msg)
{
  if (err) {
    db_error_clear(err);
    err->code = ERR_NOT_IMPLEMENTED;
    if (msg) snprintf(err->message, sizeof(err->message), "%s", msg);
  }
  return false;
}

static void mysql_close(db_t *db)
{
  if (!db) return;
  free(db->impl);
  db->impl = NULL;
}

static void mysql_close_child(db_t *db)
{
  /* MySQL backend child close stub.
   * In a real implementation, this would close the socket fd without sending QUIT.
   * For now, just freeing the memory is sufficient.
   */
  if (!db) return;
  free(db->impl);
  db->impl = NULL;
}

static bool mysql_tx_begin(db_t *db, db_tx_flags_t flags, db_error_t *err)
{ (void)db; (void)flags; return mysql_not_impl(err, "MySQL: tx_begin not implemented"); }

static bool mysql_tx_commit(db_t *db, db_error_t *err)
{ (void)db; return mysql_not_impl(err, "MySQL: tx_commit not implemented"); }

static bool mysql_tx_rollback(db_t *db, db_error_t *err)
{ (void)db; return mysql_not_impl(err, "MySQL: tx_rollback not implemented"); }

static bool mysql_exec(db_t *db, const char *sql, const db_bind_t *p, size_t n, db_error_t *err)
{ (void)db; (void)sql; (void)p; (void)n; return mysql_not_impl(err, "MySQL: exec not implemented"); }

static bool mysql_exec_rows_affected(db_t *db, const char *sql, const db_bind_t *p, size_t n, int64_t *rows, db_error_t *err)
{
  if (rows) *rows = 0;
  (void)db; (void)sql; (void)p; (void)n;
  return mysql_not_impl(err, "MySQL: exec_rows_affected not implemented");
}

static bool mysql_exec_insert_id(db_t *db, const char *sql, const db_bind_t *p, size_t n,
                                const char *id_col, int64_t *out_id, db_error_t *err)
{
  if (out_id) *out_id = 0;
  (void)db; (void)sql; (void)p; (void)n; (void)id_col;
  return mysql_not_impl(err, "MySQL: exec_insert_id not implemented");
}

static bool mysql_query(db_t *db, const char *sql, const db_bind_t *p, size_t n, db_res_t **out, db_error_t *err)
{
  if (out) *out = NULL;
  (void)db; (void)sql; (void)p; (void)n;
  return mysql_not_impl(err, "MySQL: query not implemented");
}

static bool mysql_exec_returning(db_t *db, const char *sql, const db_bind_t *p, size_t n, db_res_t **out, db_error_t *err)
{
  if (out) *out = NULL;
  (void)db; (void)sql; (void)p; (void)n;
  return mysql_not_impl(err, "MySQL: exec_returning not implemented");
}

/* Result-set API stubs. */
static bool mysql_res_step(db_res_t *res, db_error_t *err)
{ (void)res; return mysql_not_impl(err, "MySQL: res_step not implemented"); }
static void mysql_res_cancel(db_res_t *res) { (void)res; }
static int mysql_res_col_count(const db_res_t *res) { (void)res; return 0; }
static const char *mysql_res_col_name(const db_res_t *res, int i) { (void)res; (void)i; return NULL; }
static db_col_type_t mysql_res_col_type(const db_res_t *res, int i) { (void)res; (void)i; return DB_TYPE_UNKNOWN; }
static bool mysql_res_col_is_null(const db_res_t *res, int i) { (void)res; (void)i; return true; }
static int64_t mysql_res_col_i64(const db_res_t *res, int i, db_error_t *err)
{ (void)res; (void)i; mysql_not_impl(err, "MySQL: res_col_i64 not implemented"); return 0; }
static uint64_t mysql_res_col_u64(const db_res_t *res, int i, db_error_t *err)
{ (void)res; (void)i; mysql_not_impl(err, "MySQL: res_col_u64 not implemented"); return 0; }
static int32_t mysql_res_col_i32(const db_res_t *res, int i, db_error_t *err)
{ (void)res; (void)i; mysql_not_impl(err, "MySQL: res_col_i32 not implemented"); return 0; }
static uint32_t mysql_res_col_u32(const db_res_t *res, int i, db_error_t *err)
{ (void)res; (void)i; mysql_not_impl(err, "MySQL: res_col_u32 not implemented"); return 0; }
static bool mysql_res_col_bool(const db_res_t *res, int i, db_error_t *err)
{ (void)res; (void)i; mysql_not_impl(err, "MySQL: res_col_bool not implemented"); return false; }
static double mysql_res_col_double(const db_res_t *res, int i, db_error_t *err)
{ (void)res; (void)i; mysql_not_impl(err, "MySQL: res_col_double not implemented"); return 0.0; }
static const char *mysql_res_col_text(const db_res_t *res, int i, db_error_t *err)
{ (void)res; (void)i; mysql_not_impl(err, "MySQL: res_col_text not implemented"); return NULL; }
static const void *mysql_res_col_blob(const db_res_t *res, int i, size_t *len, db_error_t *err)
{ (void)res; (void)i; if (len) *len = 0; mysql_not_impl(err, "MySQL: res_col_blob not implemented"); return NULL; }
static void mysql_res_finalize(db_res_t *res) { (void)res; }

/* Domain helper required by db_vt_t (temporary): keep as stub. */
static bool mysql_ship_repair_atomic(db_t *db, int player_id, int ship_id, int cost,
                                    int64_t *out_new_credits, db_error_t *err)
{
  if (out_new_credits) *out_new_credits = 0;
  (void)db; (void)player_id; (void)ship_id; (void)cost;
  return mysql_not_impl(err, "MySQL: ship_repair_atomic not implemented");
}

static const db_vt_t MYSQL_VT = {
  .close = mysql_close,
  .close_child = mysql_close_child,
  .tx_begin = mysql_tx_begin,
  .tx_commit = mysql_tx_commit,
  .tx_rollback = mysql_tx_rollback,
  .exec = mysql_exec,
  .exec_rows_affected = mysql_exec_rows_affected,
  .exec_insert_id = mysql_exec_insert_id,
  .query = mysql_query,
  .exec_returning = mysql_exec_returning,
  .res_step = mysql_res_step,
  .res_cancel = mysql_res_cancel,
  .res_col_count = mysql_res_col_count,
  .res_col_name = mysql_res_col_name,
  .res_col_type = mysql_res_col_type,
  .res_col_is_null = mysql_res_col_is_null,
  .res_col_i64 = mysql_res_col_i64,
  .res_col_u64 = mysql_res_col_u64,
  .res_col_i32 = mysql_res_col_i32,
  .res_col_u32 = mysql_res_col_u32,
  .res_col_bool = mysql_res_col_bool,
  .res_col_double = mysql_res_col_double,
  .res_col_text = mysql_res_col_text,
  .res_col_blob = mysql_res_col_blob,
  .res_finalize = mysql_res_finalize,
  .ship_repair_atomic = mysql_ship_repair_atomic,
};

void *
db_mysql_open_internal(db_t *db, const db_config_t *cfg, db_error_t *err)
{
  (void)cfg;

  if (!db) {
    if (err) {
      db_error_clear(err);
      err->code = ERR_DB_INTERNAL;
      snprintf(err->message, sizeof(err->message), "MySQL: NULL db handle");
    }
    return NULL;
  }

  db->vt = &MYSQL_VT;

  struct db_mysql_impl_s *impl = calloc(1, sizeof(*impl));
  if (!impl) {
    if (err) {
      db_error_clear(err);
      err->code = ERR_DB_NOMEM;
      snprintf(err->message, sizeof(err->message), "MySQL: OOM allocating impl");
    }
    return NULL;
  }

  /* Phase 6A: do not attempt to connect yet. */
  if (err) {
    db_error_clear(err);
    err->code = ERR_NOT_IMPLEMENTED;
    snprintf(err->message, sizeof(err->message), "MySQL backend skeleton (not implemented)");
  }

  return impl;
}
