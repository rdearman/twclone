#define TW_DB_INTERNAL 1

#include "db_mysql.h"
#include "db_int.h"
#include "errors.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_MYSQL
#include <mysql.h>

/* libmysqlclient/MariaDB calls on one MYSQL handle are serialized, matching
 * the PostgreSQL driver's connection safety model. */
static pthread_mutex_t g_mysql_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_mysql_library_once = PTHREAD_ONCE_INIT;
static int g_mysql_library_status = 0;

static void mysql_library_initialize (void)
{ g_mysql_library_status = mysql_library_init (0, NULL, NULL); }

#if defined(MARIADB_BASE_VERSION) || (defined(MYSQL_VERSION_ID) && MYSQL_VERSION_ID < 80000)
typedef my_bool mysql_flag_t;
#else
typedef bool mysql_flag_t;
#endif

typedef struct
{
  MYSQL *conn;
  bool in_tx;
} mysql_impl_t;

typedef struct
{
  MYSQL_STMT *stmt;
  MYSQL_RES *metadata;
  MYSQL_BIND *result_bind;
  unsigned long *lengths;
  mysql_flag_t *is_null;
  mysql_flag_t *errors;
  unsigned char **buffers;
  unsigned long *capacities;
  MYSQL_FIELD *fields;
  int n_fields;
  bool has_row;
} mysql_res_impl_t;

typedef struct
{
  MYSQL_BIND *bind;
  unsigned long *lengths;
  mysql_flag_t *is_null;
  char **owned;
} mysql_params_t;

static void
mysql_set_error (db_error_t *err, int code, db_error_category_t category,
                 unsigned int native, const char *message)
{
  if (!err)
    return;
  db_error_clear (err);
  err->code = code;
  err->category = category;
  err->backend_code = (int) native;
  if (message && *message)
    snprintf (err->message, sizeof (err->message), "%s", message);
}

static void
mysql_map_errno (unsigned int native, const char *message, db_error_t *err)
{
  int code = ERR_DB_QUERY_FAILED;
  db_error_category_t category = DB_ERR_CAT_UNKNOWN;

  switch (native)
    {
    case 1062: case 1048: case 3819:
      code = ERR_DB_CONSTRAINT;
      category = DB_ERR_CAT_CONSTRAINT;
      break;
    case 1451: case 1452:
      code = ERR_DB_CONSTRAINT;
      category = DB_ERR_CAT_FK;
      break;
    case 1213:
      category = DB_ERR_CAT_DEADLOCK;
      break;
    case 1205:
      category = DB_ERR_CAT_LOCK_TIMEOUT;
      break;
    case 2002: case 2003: case 2006: case 2013: case 2055:
      code = ERR_DB_CONNECT;
      category = DB_ERR_CAT_CONNECTION;
      break;
    default:
      break;
    }
  mysql_set_error (err, code, category, native, message);
}

static bool
mysql_connection_ok (db_t *db, MYSQL **out, db_error_t *err)
{
  mysql_impl_t *impl = db ? (mysql_impl_t *) db->impl : NULL;
  if (!impl || !impl->conn)
    {
      mysql_set_error (err, ERR_DB_CLOSED, DB_ERR_CAT_CONNECTION, 0,
                       "MySQL connection is closed.");
      return false;
    }
  *out = impl->conn;
  return true;
}

/* Convert the public positional `$N` syntax to MySQL '?' markers without
 * touching quoted strings, identifiers, or SQL comments. Mapping is recorded
 * because markers may be repeated or appear out of ordinal order. */
static bool
mysql_rewrite_placeholders (const char *sql, size_t n_params, char **out_sql,
                            size_t **out_order, size_t *out_count,
                            db_error_t *err)
{
  size_t len, i = 0, out = 0, count = 0, max_index = 0;
  char quote = 0;
  int marker_style = 0; /* 1 = $N positional, 2 = '?' ordinal */
  bool line_comment = false, block_comment = false;
  char *rewritten;
  size_t *order;

  if (!sql || !out_sql || !out_order || !out_count)
    {
      mysql_set_error (err, ERR_DB_CONFIG, DB_ERR_CAT_UNKNOWN, 0,
                       "MySQL query SQL/output is invalid.");
      return false;
    }
  len = strlen (sql);
  rewritten = malloc (len + 1);
  order = len ? calloc (len, sizeof (*order)) : NULL;
  if (!rewritten || (len && !order))
    {
      free (rewritten);
      free (order);
      mysql_set_error (err, ERR_DB_NOMEM, DB_ERR_CAT_UNKNOWN, 0,
                       "Out of memory rewriting MySQL query placeholders.");
      return false;
    }

  while (i < len)
    {
      char c = sql[i];
      if (line_comment)
        {
          rewritten[out++] = c;
          i++;
          if (c == '\n') line_comment = false;
          continue;
        }
      if (block_comment)
        {
          rewritten[out++] = c;
          i++;
          if (c == '*' && i < len && sql[i] == '/')
            {
              rewritten[out++] = sql[i++];
              block_comment = false;
            }
          continue;
        }
      if (quote)
        {
          rewritten[out++] = c;
          i++;
          if (c == '\\' && i < len)
            rewritten[out++] = sql[i++];
          else if (c == quote)
            {
              if (i < len && sql[i] == quote)
                rewritten[out++] = sql[i++];
              else
                quote = 0;
            }
          continue;
        }
      if (c == '\'' || c == '"' || c == '`')
        {
          quote = c;
          rewritten[out++] = sql[i++];
          continue;
        }
      if (c == '#' || (c == '-' && i + 1 < len && sql[i + 1] == '-'))
        {
          line_comment = true;
          rewritten[out++] = sql[i++];
          if (c == '-') rewritten[out++] = sql[i++];
          continue;
        }
      if (c == '/' && i + 1 < len && sql[i + 1] == '*')
        {
          block_comment = true;
          rewritten[out++] = sql[i++];
          rewritten[out++] = sql[i++];
          continue;
        }
      if (c == '$' && i + 1 < len && sql[i + 1] >= '0' && sql[i + 1] <= '9')
        {
          size_t index = 0;
          if (marker_style == 2)
            {
              free (rewritten); free (order);
              mysql_set_error (err, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0,
                               "Do not mix numbered and ordinal MySQL placeholders.");
              return false;
            }
          marker_style = 1;
          i++;
          while (i < len && sql[i] >= '0' && sql[i] <= '9')
            {
              unsigned digit = (unsigned) (sql[i++] - '0');
              if (index > (SIZE_MAX - digit) / 10)
                {
                  free (rewritten); free (order);
                  mysql_set_error (err, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0,
                                   "MySQL placeholder index overflow.");
                  return false;
                }
              index = index * 10 + digit;
            }
          if (index == 0 || index > n_params)
            {
              free (rewritten); free (order);
              mysql_set_error (err, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0,
                               "MySQL query placeholder does not match supplied parameters.");
              return false;
            }
          rewritten[out++] = '?';
          order[count++] = index - 1;
          if (index > max_index) max_index = index;
          continue;
        }
      if (c == '?')
        {
          if (marker_style == 1 || count >= n_params)
            {
              free (rewritten); free (order);
              mysql_set_error (err, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0,
                               "MySQL ordinal placeholder count does not match supplied parameters.");
              return false;
            }
          marker_style = 2;
          rewritten[out++] = '?';
          order[count] = count;
          count++;
          max_index = count;
          i++;
          continue;
        }
      rewritten[out++] = sql[i++];
    }

  if (quote || block_comment || max_index != n_params ||
      (marker_style == 2 && count != n_params))
    {
      free (rewritten); free (order);
      mysql_set_error (err, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0,
                       quote || block_comment ? "Unterminated SQL quote or comment." :
                       "MySQL query placeholder count does not match supplied parameters.");
      return false;
    }
  rewritten[out] = '\0';
  *out_sql = rewritten;
  *out_order = order;
  *out_count = count;
  return true;
}

static void
mysql_params_free (mysql_params_t *params, size_t n)
{
  if (!params) return;
  if (params->owned)
    for (size_t i = 0; i < n; i++) free (params->owned[i]);
  free (params->owned);
  free (params->bind);
  free (params->lengths);
  free (params->is_null);
  memset (params, 0, sizeof (*params));
}

static char *
mysql_timestamp_string (int64_t epoch)
{
  time_t value = (time_t) epoch;
  struct tm tm_utc;
  char buf[32];
  if (!gmtime_r (&value, &tm_utc) ||
      strftime (buf, sizeof (buf), "%Y-%m-%d %H:%M:%S", &tm_utc) == 0)
    return NULL;
  return strdup (buf);
}

static bool
mysql_bind_params (const db_bind_t *params, size_t n_params,
                   const size_t *order, size_t n_markers,
                   mysql_params_t *out, db_error_t *err)
{
  memset (out, 0, sizeof (*out));
  if (n_markers == 0) return true;
  out->bind = calloc (n_markers, sizeof (*out->bind));
  out->lengths = calloc (n_markers, sizeof (*out->lengths));
  out->is_null = calloc (n_markers, sizeof (*out->is_null));
  out->owned = calloc (n_markers, sizeof (*out->owned));
  if (!out->bind || !out->lengths || !out->is_null || !out->owned)
    goto oom;

  for (size_t i = 0; i < n_markers; i++)
    {
      size_t pidx = order[i];
      const db_bind_t *p;
      MYSQL_BIND *b = &out->bind[i];
      if (pidx >= n_params) goto invalid;
      p = &params[pidx];
      b->is_null = &out->is_null[i];
      switch (p->type)
        {
        case DB_BIND_NULL:
          out->is_null[i] = 1;
          b->buffer_type = MYSQL_TYPE_NULL;
          break;
        case DB_BIND_I64:
          b->buffer_type = MYSQL_TYPE_LONGLONG; b->buffer = (void *) &p->v.i64;
          break;
        case DB_BIND_U64:
          b->buffer_type = MYSQL_TYPE_LONGLONG; b->buffer = (void *) &p->v.u64; b->is_unsigned = 1;
          break;
        case DB_BIND_I32:
          b->buffer_type = MYSQL_TYPE_LONG; b->buffer = (void *) &p->v.i32;
          break;
        case DB_BIND_U32:
          b->buffer_type = MYSQL_TYPE_LONG; b->buffer = (void *) &p->v.u32; b->is_unsigned = 1;
          break;
        case DB_BIND_BOOL:
          b->buffer_type = MYSQL_TYPE_TINY; b->buffer = (void *) &p->v.b;
          break;
        case DB_BIND_TEXT: case DB_BIND_JSON: case DB_BIND_TIMESTAMP_NATIVE:
          if (!p->v.text.ptr) { out->is_null[i] = 1; b->buffer_type = MYSQL_TYPE_NULL; break; }
          out->lengths[i] = (unsigned long) (p->v.text.len ? p->v.text.len : strlen (p->v.text.ptr));
          b->buffer_type = MYSQL_TYPE_STRING; b->buffer = (void *) p->v.text.ptr;
          b->buffer_length = out->lengths[i]; b->length = &out->lengths[i];
          break;
        case DB_BIND_BLOB:
          if (!p->v.blob.ptr) { out->is_null[i] = 1; b->buffer_type = MYSQL_TYPE_NULL; break; }
          out->lengths[i] = (unsigned long) p->v.blob.len;
          b->buffer_type = MYSQL_TYPE_BLOB; b->buffer = (void *) p->v.blob.ptr;
          b->buffer_length = out->lengths[i]; b->length = &out->lengths[i];
          break;
        case DB_BIND_TIMESTAMP:
          out->owned[i] = mysql_timestamp_string (p->v.timestamp);
          if (!out->owned[i]) goto invalid;
          out->lengths[i] = (unsigned long) strlen (out->owned[i]);
          b->buffer_type = MYSQL_TYPE_STRING; b->buffer = out->owned[i];
          b->buffer_length = out->lengths[i]; b->length = &out->lengths[i];
          break;
        default:
          goto invalid;
        }
    }
  return true;

oom:
  mysql_params_free (out, n_markers);
  mysql_set_error (err, ERR_DB_NOMEM, DB_ERR_CAT_UNKNOWN, 0,
                   "Out of memory binding MySQL parameters.");
  return false;
invalid:
  mysql_params_free (out, n_markers);
  mysql_set_error (err, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0,
                   "Unsupported or invalid MySQL bind parameter.");
  return false;
}

static MYSQL_STMT *
mysql_prepare (MYSQL *conn, const char *sql, const db_bind_t *params,
               size_t n_params, mysql_params_t *bound, db_error_t *err)
{
  char *rewritten = NULL;
  size_t *order = NULL, count = 0;
  MYSQL_STMT *stmt = NULL;
  if (!mysql_rewrite_placeholders (sql, n_params, &rewritten, &order, &count, err))
    return NULL;
  if (count && !params)
    {
      mysql_set_error (err, ERR_DB_CONFIG, DB_ERR_CAT_UNKNOWN, 0,
                       "MySQL bind parameter array is NULL.");
      goto done;
    }
  if (!mysql_bind_params (params, n_params, order, count, bound, err))
    goto done;

  stmt = mysql_stmt_init (conn);
  if (!stmt)
    {
      mysql_map_errno (mysql_errno (conn), mysql_error (conn), err);
      goto done;
    }
  if (mysql_stmt_prepare (stmt, rewritten, (unsigned long) strlen (rewritten)) != 0)
    {
      mysql_map_errno (mysql_stmt_errno (stmt), mysql_stmt_error (stmt), err);
      mysql_stmt_close (stmt); stmt = NULL;
      goto done;
    }
  if (mysql_stmt_param_count (stmt) != count)
    {
      mysql_set_error (err, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0,
                       "MySQL prepared statement parameter count differs from SQL markers.");
      mysql_stmt_close (stmt); stmt = NULL;
      goto done;
    }
  if (count && mysql_stmt_bind_param (stmt, bound->bind) != 0)
    {
      mysql_map_errno (mysql_stmt_errno (stmt), mysql_stmt_error (stmt), err);
      mysql_stmt_close (stmt); stmt = NULL;
      goto done;
    }
  if (mysql_stmt_execute (stmt) != 0)
    {
      mysql_map_errno (mysql_stmt_errno (stmt), mysql_stmt_error (stmt), err);
      mysql_stmt_close (stmt); stmt = NULL;
    }
done:
  free (rewritten); free (order);
  return stmt;
}

static void
mysql_close_impl (db_t *db)
{
  mysql_impl_t *impl = db ? (mysql_impl_t *) db->impl : NULL;
  if (!impl) return;
  pthread_mutex_lock (&g_mysql_mutex);
  if (impl->conn) mysql_close (impl->conn);
  pthread_mutex_unlock (&g_mysql_mutex);
  free (impl);
  db->impl = NULL;
}

static void
mysql_close_child (db_t *db)
{
  mysql_impl_t *impl = db ? (mysql_impl_t *) db->impl : NULL;
  if (!impl) return;
  if (impl->conn)
    {
#ifdef MARIADB_BASE_VERSION
      /* Close the inherited socket first so mysql_close cannot send QUIT on a
       * connection still owned by the parent process. */
      int socket_fd = (int) mysql_get_socket (impl->conn);
      if (socket_fd >= 0) close (socket_fd);
#endif
      mysql_close (impl->conn);
    }
  free (impl);
  db->impl = NULL;
}

static bool
mysql_tx_command (db_t *db, const char *sql, bool next_state, db_error_t *err)
{
  MYSQL *conn;
  if (!mysql_connection_ok (db, &conn, err)) return false;
  pthread_mutex_lock (&g_mysql_mutex);
  int rc = mysql_query (conn, sql);
  if (rc != 0) mysql_map_errno (mysql_errno (conn), mysql_error (conn), err);
  pthread_mutex_unlock (&g_mysql_mutex);
  if (rc == 0) ((mysql_impl_t *) db->impl)->in_tx = next_state;
  return rc == 0;
}

static bool mysql_tx_begin (db_t *db, db_tx_flags_t flags, db_error_t *err)
{
  (void) flags;
  return mysql_tx_command (db, "START TRANSACTION", true, err);
}
static bool mysql_tx_commit (db_t *db, db_error_t *err)
{ return mysql_tx_command (db, "COMMIT", false, err); }
static bool mysql_tx_rollback (db_t *db, db_error_t *err)
{ return mysql_tx_command (db, "ROLLBACK", false, err); }

static bool
mysql_exec_common (db_t *db, const char *sql, const db_bind_t *params,
                   size_t n_params, int64_t *out_rows, int64_t *out_id,
                   db_error_t *err)
{
  MYSQL *conn;
  mysql_params_t bound = { 0 };
  MYSQL_STMT *stmt;
  if (out_rows) *out_rows = 0;
  if (out_id) *out_id = 0;
  if (!mysql_connection_ok (db, &conn, err)) return false;
  pthread_mutex_lock (&g_mysql_mutex);
  stmt = mysql_prepare (conn, sql, params, n_params, &bound, err);
  if (!stmt)
    {
      pthread_mutex_unlock (&g_mysql_mutex);
      mysql_params_free (&bound, n_params);
      return false;
    }
  if (out_rows) *out_rows = (int64_t) mysql_stmt_affected_rows (stmt);
  if (out_id) *out_id = (int64_t) mysql_stmt_insert_id (stmt);
  mysql_stmt_close (stmt);
  mysql_params_free (&bound, n_params);
  pthread_mutex_unlock (&g_mysql_mutex);
  return true;
}

static bool mysql_exec (db_t *db, const char *sql, const db_bind_t *p, size_t n, db_error_t *err)
{ return mysql_exec_common (db, sql, p, n, NULL, NULL, err); }
static bool mysql_exec_rows_affected (db_t *db, const char *sql, const db_bind_t *p,
                                     size_t n, int64_t *rows, db_error_t *err)
{ return mysql_exec_common (db, sql, p, n, rows, NULL, err); }
static bool mysql_exec_insert_id (db_t *db, const char *sql, const db_bind_t *p,
                                 size_t n, const char *id_col, int64_t *id,
                                 db_error_t *err)
{
  (void) id_col; /* MySQL returns LAST_INSERT_ID() for AUTO_INCREMENT inserts. */
  return mysql_exec_common (db, sql, p, n, NULL, id, err);
}

static bool
mysql_query_impl (db_t *db, const char *sql, const db_bind_t *params,
                  size_t n_params, db_res_t **out_res, db_error_t *err)
{
  MYSQL *conn;
  mysql_params_t bound = { 0 };
  MYSQL_STMT *stmt;
  MYSQL_RES *meta;
  db_res_t *res = NULL;
  mysql_res_impl_t *impl = NULL;
  mysql_flag_t update_max = 1;

  if (out_res) *out_res = NULL;
  if (!out_res)
    {
      mysql_set_error (err, ERR_DB_CONFIG, DB_ERR_CAT_UNKNOWN, 0,
                       "MySQL query result output is NULL.");
      return false;
    }
  if (!mysql_connection_ok (db, &conn, err)) return false;
  pthread_mutex_lock (&g_mysql_mutex);
  /* Prepare separately so UPDATE_MAX_LENGTH is active before execute. */
  char *rewritten = NULL;
  size_t *order = NULL, marker_count = 0;
  if (!mysql_rewrite_placeholders (sql, n_params, &rewritten, &order, &marker_count, err))
    goto fail;
  if (marker_count && !params)
    {
      mysql_set_error (err, ERR_DB_CONFIG, DB_ERR_CAT_UNKNOWN, 0,
                       "MySQL bind parameter array is NULL.");
      goto fail;
    }
  if (!mysql_bind_params (params, n_params, order, marker_count, &bound, err))
    goto fail;
  stmt = mysql_stmt_init (conn);
  if (!stmt)
    {
      mysql_map_errno (mysql_errno (conn), mysql_error (conn), err);
      goto fail;
    }
  if (mysql_stmt_prepare (stmt, rewritten, (unsigned long) strlen (rewritten)) != 0)
    {
      mysql_map_errno (mysql_stmt_errno (stmt), mysql_stmt_error (stmt), err);
      mysql_stmt_close (stmt); goto fail;
    }
  if (mysql_stmt_param_count (stmt) != marker_count ||
      (marker_count && mysql_stmt_bind_param (stmt, bound.bind) != 0))
    {
      mysql_map_errno (mysql_stmt_errno (stmt), mysql_stmt_error (stmt), err);
      mysql_stmt_close (stmt); goto fail;
    }
  (void) mysql_stmt_attr_set (stmt, STMT_ATTR_UPDATE_MAX_LENGTH, &update_max);
  if (mysql_stmt_execute (stmt) != 0)
    {
      mysql_map_errno (mysql_stmt_errno (stmt), mysql_stmt_error (stmt), err);
      mysql_stmt_close (stmt); goto fail;
    }
  meta = mysql_stmt_result_metadata (stmt);

  res = calloc (1, sizeof (*res));
  impl = calloc (1, sizeof (*impl));
  if (!res || !impl)
    {
      mysql_set_error (err, ERR_DB_NOMEM, DB_ERR_CAT_UNKNOWN, 0,
                       "Out of memory allocating MySQL result.");
      free (res); free (impl);
      if (meta) mysql_free_result (meta);
      mysql_stmt_close (stmt); goto fail;
    }
  impl->stmt = stmt;
  impl->metadata = meta;
  if (meta)
    {
      impl->n_fields = (int) mysql_num_fields (meta);
      impl->fields = mysql_fetch_fields (meta);
      if (impl->n_fields > 0)
        {
          size_t n = (size_t) impl->n_fields;
          impl->result_bind = calloc (n, sizeof (*impl->result_bind));
          impl->lengths = calloc (n, sizeof (*impl->lengths));
          impl->is_null = calloc (n, sizeof (*impl->is_null));
          impl->errors = calloc (n, sizeof (*impl->errors));
          impl->buffers = calloc (n, sizeof (*impl->buffers));
          impl->capacities = calloc (n, sizeof (*impl->capacities));
          if (!impl->result_bind || !impl->lengths || !impl->is_null ||
              !impl->errors || !impl->buffers || !impl->capacities)
            {
              mysql_set_error (err, ERR_DB_NOMEM, DB_ERR_CAT_UNKNOWN, 0,
                               "Out of memory allocating MySQL result columns.");
              goto result_fail;
            }
          for (int i = 0; i < impl->n_fields; i++)
            {
              impl->capacities[i] = 1;
              impl->buffers[i] = calloc (2, 1);
              if (!impl->buffers[i])
                {
                  mysql_set_error (err, ERR_DB_NOMEM, DB_ERR_CAT_UNKNOWN, 0,
                                   "Out of memory allocating MySQL result value.");
                  goto result_fail;
                }
              impl->result_bind[i].buffer_type = MYSQL_TYPE_STRING;
              impl->result_bind[i].buffer = impl->buffers[i];
              impl->result_bind[i].buffer_length = 1;
              impl->result_bind[i].length = &impl->lengths[i];
              impl->result_bind[i].is_null = &impl->is_null[i];
              impl->result_bind[i].error = &impl->errors[i];
            }
          if (mysql_stmt_bind_result (stmt, impl->result_bind) != 0)
            {
              mysql_map_errno (mysql_stmt_errno (stmt), mysql_stmt_error (stmt), err);
              goto result_fail;
            }
        }
    }
  if (meta)
    {
      /* Bind before buffering as required by the prepared-statement API. The
       * UPDATE_MAX_LENGTH attribute gives us the actual sizes after buffering;
       * resize and rebind before the first fetch. */
      if (mysql_stmt_store_result (stmt) != 0)
        {
          mysql_map_errno (mysql_stmt_errno (stmt), mysql_stmt_error (stmt), err);
          goto result_fail;
        }
      for (int i = 0; i < impl->n_fields; i++)
        {
          unsigned long cap = impl->fields[i].max_length;
          if (cap < 1) cap = 1;
          if (cap == ULONG_MAX)
            {
              mysql_set_error (err, ERR_DB_NOMEM, DB_ERR_CAT_UNKNOWN, 0,
                               "MySQL result column is too large.");
              goto result_fail;
            }
          if (cap != impl->capacities[i])
            {
              unsigned char *buffer = calloc ((size_t) cap + 1, 1);
              if (!buffer)
                {
                  mysql_set_error (err, ERR_DB_NOMEM, DB_ERR_CAT_UNKNOWN, 0,
                                   "Out of memory resizing MySQL result value.");
                  goto result_fail;
                }
              free (impl->buffers[i]);
              impl->buffers[i] = buffer;
              impl->capacities[i] = cap;
              impl->result_bind[i].buffer = buffer;
              impl->result_bind[i].buffer_length = cap;
            }
        }
      if (impl->n_fields > 0 && mysql_stmt_bind_result (stmt, impl->result_bind) != 0)
        {
          mysql_map_errno (mysql_stmt_errno (stmt), mysql_stmt_error (stmt), err);
          goto result_fail;
        }
    }
  res->db = db;
  res->impl = impl;
  res->num_cols = impl->n_fields;
  res->num_rows = (int) mysql_stmt_num_rows (stmt);
  res->current_row = -1;
  *out_res = res;
  free (rewritten); free (order);
  mysql_params_free (&bound, n_params);
  pthread_mutex_unlock (&g_mysql_mutex);
  return true;

result_fail:
  if (impl)
    {
      if (impl->buffers)
        for (int i = 0; i < impl->n_fields; i++) free (impl->buffers[i]);
      free (impl->result_bind); free (impl->lengths); free (impl->is_null);
      free (impl->errors); free (impl->buffers); free (impl->capacities);
      if (impl->metadata) mysql_free_result (impl->metadata);
      if (impl->stmt) mysql_stmt_close (impl->stmt);
      free (impl);
    }
  free (res);
fail:
  free (rewritten); free (order);
  mysql_params_free (&bound, n_params);
  pthread_mutex_unlock (&g_mysql_mutex);
  return false;
}

static bool mysql_exec_returning (db_t *db, const char *sql, const db_bind_t *p,
                                 size_t n, db_res_t **out, db_error_t *err)
{
  if (sql && strstr (sql, "RETURNING"))
    {
      mysql_set_error (err, ERR_NOT_IMPLEMENTED, DB_ERR_CAT_UNKNOWN, 0,
                       "MySQL does not support PostgreSQL RETURNING; use query-compatible SQL.");
      if (out) *out = NULL;
      return false;
    }
  return mysql_query_impl (db, sql, p, n, out, err);
}

static bool
mysql_res_step (db_res_t *res, db_error_t *err)
{
  mysql_res_impl_t *impl = res ? (mysql_res_impl_t *) res->impl : NULL;
  if (!impl || !impl->stmt)
    {
      mysql_set_error (err, ERR_DB_CLOSED, DB_ERR_CAT_UNKNOWN, 0,
                       "MySQL result is finalized.");
      return false;
    }
  if (impl->n_fields == 0) return false;
  pthread_mutex_lock (&g_mysql_mutex);
  int rc = mysql_stmt_fetch (impl->stmt);
  if (rc == 0 || rc == MYSQL_DATA_TRUNCATED)
    {
      if (rc == MYSQL_DATA_TRUNCATED)
        for (int i = 0; i < impl->n_fields; i++)
          if (impl->errors[i])
            {
              mysql_set_error (err, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0,
                               "MySQL result value exceeded its allocated buffer.");
              pthread_mutex_unlock (&g_mysql_mutex);
              return false;
            }
      for (int i = 0; i < impl->n_fields; i++)
        if (!impl->is_null[i] && impl->lengths[i] <= impl->capacities[i])
          impl->buffers[i][impl->lengths[i]] = '\0';
      res->current_row++;
      impl->has_row = true;
      pthread_mutex_unlock (&g_mysql_mutex);
      return true;
    }
  if (rc == MYSQL_NO_DATA)
    {
      impl->has_row = false;
      pthread_mutex_unlock (&g_mysql_mutex);
      return false;
    }
  mysql_map_errno (mysql_stmt_errno (impl->stmt), mysql_stmt_error (impl->stmt), err);
  pthread_mutex_unlock (&g_mysql_mutex);
  return false;
}

static void mysql_res_cancel (db_res_t *res) { (void) res; }
static int mysql_res_col_count (const db_res_t *res) { return res ? res->num_cols : -1; }
static const char *mysql_res_col_name (const db_res_t *res, int i)
{
  mysql_res_impl_t *impl = res ? (mysql_res_impl_t *) res->impl : NULL;
  return impl && i >= 0 && i < impl->n_fields ? impl->fields[i].name : NULL;
}
static db_col_type_t mysql_res_col_type (const db_res_t *res, int i)
{
  mysql_res_impl_t *impl = res ? (mysql_res_impl_t *) res->impl : NULL;
  if (!impl || i < 0 || i >= impl->n_fields) return DB_TYPE_UNKNOWN;
  switch (impl->fields[i].type)
    {
    case MYSQL_TYPE_TINY: case MYSQL_TYPE_SHORT: case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_LONGLONG: case MYSQL_TYPE_INT24: case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_BIT: return DB_TYPE_INTEGER;
    case MYSQL_TYPE_FLOAT: case MYSQL_TYPE_DOUBLE: case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL: return DB_TYPE_FLOAT;
    case MYSQL_TYPE_TINY_BLOB: case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB: case MYSQL_TYPE_BLOB: return DB_TYPE_BLOB;
    case MYSQL_TYPE_NULL: return DB_TYPE_NULL;
    case MYSQL_TYPE_STRING: case MYSQL_TYPE_VAR_STRING: case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_DATE: case MYSQL_TYPE_TIME: case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP: case MYSQL_TYPE_NEWDATE: return DB_TYPE_TEXT;
    default: return DB_TYPE_UNKNOWN;
    }
}
static bool mysql_res_col_is_null (const db_res_t *res, int i)
{
  mysql_res_impl_t *impl = res ? (mysql_res_impl_t *) res->impl : NULL;
  return !impl || !impl->has_row || i < 0 || i >= impl->n_fields || impl->is_null[i];
}

static const char *
mysql_col_text_checked (const db_res_t *res, int i, db_error_t *err)
{
  mysql_res_impl_t *impl = res ? (mysql_res_impl_t *) res->impl : NULL;
  if (!impl || !impl->has_row || i < 0 || i >= impl->n_fields)
    {
      mysql_set_error (err, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0,
                       "MySQL result column is not available.");
      return NULL;
    }
  if (impl->is_null[i]) return NULL;
  return (const char *) impl->buffers[i];
}
static bool mysql_parse_i64 (const db_res_t *r, int i, int64_t *out, db_error_t *e)
{
  const char *s = mysql_col_text_checked (r, i, e); char *end = NULL; long long v;
  if (!s) return e && e->code ? false : ((*out = 0), true);
  errno = 0; v = strtoll (s, &end, 10);
  if (errno || !end || *end) { mysql_set_error (e, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0, "MySQL column is not a signed integer."); return false; }
  *out = (int64_t) v; return true;
}
static bool mysql_parse_u64 (const db_res_t *r, int i, uint64_t *out, db_error_t *e)
{
  const char *s = mysql_col_text_checked (r, i, e); char *end = NULL; unsigned long long v;
  if (!s) return e && e->code ? false : ((*out = 0), true);
  if (*s == '-') { mysql_set_error (e, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0, "MySQL column is not an unsigned integer."); return false; }
  errno = 0; v = strtoull (s, &end, 10);
  if (errno || !end || *end) { mysql_set_error (e, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0, "MySQL column is not an unsigned integer."); return false; }
  *out = (uint64_t) v; return true;
}
static int64_t mysql_res_col_i64 (const db_res_t *r, int i, db_error_t *e)
{ int64_t v = 0; (void) mysql_parse_i64 (r, i, &v, e); return v; }
static uint64_t mysql_res_col_u64 (const db_res_t *r, int i, db_error_t *e)
{ uint64_t v = 0; (void) mysql_parse_u64 (r, i, &v, e); return v; }
static int32_t mysql_res_col_i32 (const db_res_t *r, int i, db_error_t *e)
{
  int64_t v = mysql_res_col_i64 (r, i, e);
  if (e && !e->code && (v < INT32_MIN || v > INT32_MAX)) mysql_set_error (e, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0, "MySQL integer column is outside int32 range.");
  return (int32_t) v;
}
static uint32_t mysql_res_col_u32 (const db_res_t *r, int i, db_error_t *e)
{
  uint64_t v = mysql_res_col_u64 (r, i, e);
  if (e && !e->code && v > UINT32_MAX) mysql_set_error (e, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0, "MySQL integer column is outside uint32 range.");
  return (uint32_t) v;
}
static bool mysql_res_col_bool (const db_res_t *r, int i, db_error_t *e)
{
  const char *s = mysql_col_text_checked (r, i, e);
  if (!s) return false;
  return !strcmp (s, "1") || !strcasecmp (s, "true") || !strcasecmp (s, "t");
}
static double mysql_res_col_double (const db_res_t *r, int i, db_error_t *e)
{
  const char *s = mysql_col_text_checked (r, i, e); char *end = NULL; double v;
  if (!s) return 0.0;
  errno = 0; v = strtod (s, &end);
  if (errno || !end || *end) { mysql_set_error (e, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0, "MySQL column is not numeric."); return 0.0; }
  return v;
}
static const char *mysql_res_col_text (const db_res_t *r, int i, db_error_t *e)
{ return mysql_col_text_checked (r, i, e); }
static const void *mysql_res_col_blob (const db_res_t *r, int i, size_t *len, db_error_t *e)
{
  mysql_res_impl_t *impl = r ? (mysql_res_impl_t *) r->impl : NULL;
  if (len) *len = 0;
  if (!impl || !impl->has_row || i < 0 || i >= impl->n_fields)
    { mysql_set_error (e, ERR_DB_QUERY_FAILED, DB_ERR_CAT_UNKNOWN, 0, "MySQL result column is not available."); return NULL; }
  if (impl->is_null[i]) return NULL;
  if (len) *len = impl->lengths[i];
  return impl->buffers[i];
}
static void mysql_res_finalize (db_res_t *res)
{
  if (!res) return;
  mysql_res_impl_t *impl = (mysql_res_impl_t *) res->impl;
  if (impl)
    {
      pthread_mutex_lock (&g_mysql_mutex);
      if (impl->stmt) mysql_stmt_close (impl->stmt);
      if (impl->metadata) mysql_free_result (impl->metadata);
      pthread_mutex_unlock (&g_mysql_mutex);
      if (impl->buffers)
        for (int i = 0; i < impl->n_fields; i++) free (impl->buffers[i]);
      free (impl->result_bind); free (impl->lengths); free (impl->is_null);
      free (impl->errors); free (impl->buffers); free (impl->capacities);
      free (impl);
    }
  free (res);
}

static bool mysql_ship_repair_atomic (db_t *db, int player_id, int ship_id,
                                      int cost, int64_t *credits, db_error_t *err)
{
  (void) db; (void) player_id; (void) ship_id; (void) cost;
  if (credits) *credits = 0;
  mysql_set_error (err, ERR_NOT_IMPLEMENTED, DB_ERR_CAT_UNKNOWN, 0,
                   "MySQL ship_repair_atomic is deferred until a portable implementation is available.");
  return false;
}

static const db_vt_t MYSQL_VT = {
  .close = mysql_close_impl, .close_child = mysql_close_child,
  .tx_begin = mysql_tx_begin, .tx_commit = mysql_tx_commit,
  .tx_rollback = mysql_tx_rollback, .exec = mysql_exec,
  .exec_rows_affected = mysql_exec_rows_affected,
  .exec_insert_id = mysql_exec_insert_id, .query = mysql_query_impl,
  .exec_returning = mysql_exec_returning,
  .res_step = mysql_res_step, .res_cancel = mysql_res_cancel,
  .res_col_count = mysql_res_col_count, .res_col_name = mysql_res_col_name,
  .res_col_type = mysql_res_col_type, .res_col_is_null = mysql_res_col_is_null,
  .res_col_i64 = mysql_res_col_i64, .res_col_u64 = mysql_res_col_u64,
  .res_col_i32 = mysql_res_col_i32, .res_col_u32 = mysql_res_col_u32,
  .res_col_bool = mysql_res_col_bool, .res_col_double = mysql_res_col_double,
  .res_col_text = mysql_res_col_text, .res_col_blob = mysql_res_col_blob,
  .res_finalize = mysql_res_finalize, .ship_repair_atomic = mysql_ship_repair_atomic,
};

void *
db_mysql_open_internal (db_t *db, const db_config_t *cfg, db_error_t *err)
{
  MYSQL *conn;
  mysql_impl_t *impl;
  unsigned int timeout;
  if (!db || !cfg)
    {
      mysql_set_error (err, ERR_DB_CONFIG, DB_ERR_CAT_UNKNOWN, 0,
                       "MySQL open received invalid configuration.");
      return NULL;
    }
  (void) pthread_once (&g_mysql_library_once, mysql_library_initialize);
  if (g_mysql_library_status != 0)
    {
      mysql_set_error (err, ERR_DB_CONNECT, DB_ERR_CAT_CONNECTION, 0,
                       "MySQL client library initialization failed.");
      return NULL;
    }
  conn = mysql_init (NULL);
  if (!conn)
    {
      mysql_set_error (err, ERR_DB_NOMEM, DB_ERR_CAT_UNKNOWN, 0,
                       "MySQL client could not allocate a connection.");
      return NULL;
    }
  if (cfg->connect_timeout_ms > 0)
    {
      timeout = (unsigned int) ((cfg->connect_timeout_ms + 999) / 1000);
      if (!timeout) timeout = 1;
      (void) mysql_options (conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    }
  pthread_mutex_lock (&g_mysql_mutex);
  if (!mysql_real_connect (conn, cfg->mysql_host, cfg->mysql_user,
                           cfg->mysql_password, cfg->mysql_database,
                           cfg->mysql_port, cfg->mysql_unix_socket, 0))
    {
      mysql_map_errno (mysql_errno (conn), mysql_error (conn), err);
      pthread_mutex_unlock (&g_mysql_mutex);
      mysql_close (conn);
      return NULL;
    }
  if (mysql_query (conn, "SET time_zone = '+00:00'") != 0)
    {
      mysql_map_errno (mysql_errno (conn), mysql_error (conn), err);
      pthread_mutex_unlock (&g_mysql_mutex);
      mysql_close (conn);
      return NULL;
    }
  (void) mysql_set_character_set (conn, "utf8mb4");
  pthread_mutex_unlock (&g_mysql_mutex);
  impl = calloc (1, sizeof (*impl));
  if (!impl)
    {
      mysql_close (conn);
      mysql_set_error (err, ERR_DB_NOMEM, DB_ERR_CAT_UNKNOWN, 0,
                       "Out of memory allocating MySQL backend state.");
      return NULL;
    }
  impl->conn = conn;
  db->vt = &MYSQL_VT;
  db_error_clear (err);
  return impl;
}

#else /* !HAVE_MYSQL: preserve PostgreSQL-only builds */

void *
db_mysql_open_internal (db_t *db, const db_config_t *cfg, db_error_t *err)
{
  (void) db; (void) cfg;
  if (err)
    {
      db_error_clear (err);
      err->code = ERR_NOT_IMPLEMENTED;
      snprintf (err->message, sizeof (err->message),
                "MySQL backend is disabled; configure with --enable-mysql and install the client development package.");
    }
  return NULL;
}

#endif
