/**
 * @file sql_driver.c
 * @brief SQL generation implementation for database-specific fragments.
 *
 * Provides backend-agnostic access to database-specific SQL patterns
 * (timestamps, epoch, conflict handling).
 *
 * All return values are static strings; no allocation/free needed.
 */

#include "sql_driver.h"
#include "db_api.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>




/**
 * @brief Return SQL for current timestamp in TIMESTAMP/TIMESTAMPTZ fields.
 *
 * PostgreSQL: NOW()
 * For non-PostgreSQL: FAILS (returns NULL).
 *
 * This is intentionally PostgreSQL-only to prevent accidental use
 * of this function on unsupported backends.
 */
const char *
sql_now_timestamptz(const db_t *db)
{
  if (!db)
    return "NOW()";  // Default to PostgreSQL

  db_backend_t backend = db_backend(db);
  
  switch (backend)
    {
    case DB_BACKEND_POSTGRES:
      return "NOW()";
    
    default:
      /* Fail fast for unsupported backends */
      return NULL;
    }
}

/**
 * @brief Return SQL for current Unix epoch (seconds since 1970) as integer.
 *
 * PostgreSQL: EXTRACT(EPOCH FROM NOW())::bigint
 * For non-PostgreSQL: FAILS (returns NULL).
 *
 * This is intentionally PostgreSQL-only to prevent accidental use
 * of this function on unsupported backends.
 */
const char *
sql_epoch_now(const db_t *db)
{
  if (!db)
    return "EXTRACT(EPOCH FROM NOW())::bigint";  // Default to PostgreSQL

  db_backend_t backend = db_backend(db);
  
  switch (backend)
    {
    case DB_BACKEND_POSTGRES:
      return "EXTRACT(EPOCH FROM NOW())::bigint";
    
    default:
      /* Fail fast for unsupported backends */
      return NULL;
    }
}

/**
 * @brief Return SQL expression that evaluates to current UTC timestamp.
 *
 * PostgreSQL: timezone('UTC', CURRENT_TIMESTAMP)
 * MySQL: UTC_TIMESTAMP()
 * Oracle: SYS_EXTRACT_UTC(SYSTIMESTAMP)
 */
const char *
sql_now_expr(const db_t *db)
{
  db_backend_t b = db ? db_backend(db) : DB_BACKEND_POSTGRES;

  switch (b)
    {
    case DB_BACKEND_POSTGRES:
      return "timezone('UTC', CURRENT_TIMESTAMP)";
    case DB_BACKEND_MYSQL:
      return "UTC_TIMESTAMP()";
    case DB_BACKEND_ORACLE:
      return "SYS_EXTRACT_UTC(SYSTIMESTAMP)";
    default:
      return NULL;
    }
}

/**
 * @brief Convert a timestamp expression/column to epoch seconds.
 *
 * Writes a SQL expression that converts the given timestamp expression to Unix epoch seconds.
 */
int
sql_ts_to_epoch_expr(const db_t *db,
                     const char *ts_expr,
                     char *out_buf,
                     size_t out_sz)
{
  db_backend_t b = db ? db_backend(db) : DB_BACKEND_POSTGRES;

  if (!ts_expr || !out_buf || out_sz == 0) return -1;

  switch (b)
    {
    case DB_BACKEND_POSTGRES:
      return (snprintf(out_buf, out_sz, "EXTRACT(EPOCH FROM %s)", ts_expr) < (int)out_sz) ? 0 : -1;
    case DB_BACKEND_MYSQL:
      return (snprintf(out_buf, out_sz, "UNIX_TIMESTAMP(%s)", ts_expr) < (int)out_sz) ? 0 : -1;
    case DB_BACKEND_ORACLE:
      return (snprintf(out_buf, out_sz,
                       "((CAST((%s AT TIME ZONE 'UTC') AS DATE) - DATE '1970-01-01') * 86400)",
                       ts_expr) < (int)out_sz) ? 0 : -1;
    default:
      return -1;
    }
}

/**
 * @brief Return ON CONFLICT clause for INSERT ... ON CONFLICT DO NOTHING.
 *
 * PostgreSQL appends this after VALUES clause.
 * For non-PostgreSQL backends: FAILS (returns NULL).
 *
 * This is intentionally PostgreSQL-only to prevent accidental use
 * of this function on unsupported backends.
 */
const char *
sql_insert_ignore_clause(const db_t *db)
{
  if (!db)
    {
      /* Default to PostgreSQL for legacy code */
      return "ON CONFLICT DO NOTHING";
    }

  db_backend_t backend = db_backend(db);
  
  switch (backend)
    {
    case DB_BACKEND_POSTGRES:
      return "ON CONFLICT DO NOTHING";
    
    default:
      /* Fail fast for unsupported backends */
      return NULL;
    }
}

/**
 * @brief Return ON CONFLICT clause prefix with conflict target placeholder.
 *
 * PostgreSQL: "ON CONFLICT(%s) DO"
 * For non-PostgreSQL backends: FAILS (returns NULL).
 *
 * Used to format: snprintf(buf, ..., sql_conflict_target_fmt(db), "col1, col2");
 */
const char *
sql_conflict_target_fmt(const db_t *db)
{
  if (!db)
    {
      /* Default to PostgreSQL for legacy code */
      return "ON CONFLICT(%s) DO";
    }

  db_backend_t backend = db_backend(db);
  
  switch (backend)
    {
    case DB_BACKEND_POSTGRES:
      return "ON CONFLICT(%s) DO";
    
    default:
      /* Fail fast for unsupported backends */
      return NULL;
    }
}


/**
 * @brief Return SQL expression for converting Unix epoch parameter to TIMESTAMPTZ.
 *
 * PostgreSQL: to_timestamp($X)
 * SQLite:     datetime($X, 'unixepoch')
 *
 * Use in WHERE clauses: WHERE timestamp_col <= <this_function>
 */
/* const char * */
/* sql_epoch_to_timestamptz_fmt(const db_t *db) */
/* { */
/*   if (!db) */
/*     return "to_timestamp($%d)";  // Default to PostgreSQL */

/*   db_backend_t backend = db_backend(db); */
  
/*   switch (backend) */
/*     { */
/*     case DB_BACKEND_POSTGRES: */
/*       return "to_timestamp($%d)"; */
    
/*     case DB_BACKEND_SQLITE: */
/*       return "datetime($%d, 'unixepoch')"; */
    
/*     default: */
/*       return "to_timestamp($%d)"; */
/*     } */
/* } */

/**
 * @brief Return format string for entity_stock upsert with epoch timestamp.
 *
 * PostgreSQL: "INSERT INTO entity_stock (...) VALUES (..., %s, ...) ON CONFLICT(...) DO UPDATE SET ... last_updated_ts = %s"
 * For non-PostgreSQL: FAILS (returns NULL).
 *
 * Format: snprintf(buf, size, sql_entity_stock_upsert_epoch_fmt(db), epoch_expr, epoch_expr);
 */
const char *
sql_entity_stock_upsert_epoch_fmt(const db_t *db)
{
  if (!db)
    {
      /* Default to PostgreSQL for legacy code */
      return "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "
             "VALUES ('planet', $1, $2, $3, 0, %s) "
             "ON CONFLICT(entity_type, entity_id, commodity_code) DO UPDATE SET quantity = $3, last_updated_ts = %s;";
    }

  db_backend_t backend = db_backend(db);
  
  switch (backend)
    {
    case DB_BACKEND_POSTGRES:
      return "INSERT INTO entity_stock (entity_type, entity_id, commodity_code, quantity, price, last_updated_ts) "
             "VALUES ('planet', $1, $2, $3, 0, %s) "
             "ON CONFLICT(entity_type, entity_id, commodity_code) DO UPDATE SET quantity = $3, last_updated_ts = %s;";
    
    default:
      /* Fail fast for unsupported backends */
      return NULL;
    }
}

/**
 * @brief Convert epoch parameter expression to PostgreSQL timestamptz.
 *
 * Returns a SQL fragment that wraps a parameter with to_timestamp().
 * For non-PostgreSQL backends: FAILS (returns NULL).
 */
const char *
sql_epoch_param_to_timestamptz(const db_t *db)
{
  if (!db)
    {
      /* Default to PostgreSQL for legacy code */
      return "to_timestamp(%s)";
    }

  db_backend_t backend = db_backend(db);
  
  switch (backend)
    {
    case DB_BACKEND_POSTGRES:
      return "to_timestamp(%s)";
    
    default:
      /* Fail fast for unsupported backends */
      return NULL;
    }
}

/**
 * @brief Return SQL clause for SELECT ... FOR UPDATE SKIP LOCKED.
 *
 * PostgreSQL: " FOR UPDATE SKIP LOCKED"
 * MySQL 9.5+: " FOR UPDATE SKIP LOCKED" (same syntax, supported since 8.0)
 * For unsupported backends: FAILS (returns NULL).
 */
const char *
sql_for_update_skip_locked(const db_t *db)
{
  if (!db)
    {
      /* Default to PostgreSQL for legacy code */
      return " FOR UPDATE SKIP LOCKED";
    }

  db_backend_t backend = db_backend(db);
  
  switch (backend)
    {
    case DB_BACKEND_POSTGRES:
      return " FOR UPDATE SKIP LOCKED";
    
    /* MySQL 8.0+ supports identical syntax; add case when backend available:
    case DB_BACKEND_MYSQL:
      return " FOR UPDATE SKIP LOCKED";
    */
    
    default:
      /* Fail fast for unsupported backends */
      return NULL;
    }
}

/**
 * @brief Return the SQL function name for building JSON objects.
 *
 * PostgreSQL: "json_build_object"
 * MySQL 8.0+: "JSON_OBJECT"
 * For unsupported backends: FAILS (returns NULL).
 */
const char *
sql_json_object_fn(const db_t *db)
{
  if (!db)
    {
      /* Default to PostgreSQL for legacy code */
      return "json_build_object";
    }

  db_backend_t backend = db_backend(db);
  
  switch (backend)
    {
    case DB_BACKEND_POSTGRES:
      return "json_build_object";
    
    /* MySQL 8.0+ uses JSON_OBJECT; add case when backend available:
    case DB_BACKEND_MYSQL:
      return "JSON_OBJECT";
    */
    
    default:
      /* Fail fast for unsupported backends */
      return NULL;
    }
}

/**
 * @brief Return the SQL function name for aggregating values into a JSON array.
 *
 * PostgreSQL: "json_agg"
 * MySQL 8.0+: "JSON_ARRAYAGG"
 * For unsupported backends: FAILS (returns NULL).
 */
const char *
sql_json_arrayagg_fn(const db_t *db)
{
  if (!db)
    {
      /* Default to PostgreSQL for legacy code */
      return "json_agg";
    }

  db_backend_t backend = db_backend(db);
  
  switch (backend)
    {
    case DB_BACKEND_POSTGRES:
      return "json_agg";
    
    /* MySQL 8.0+ uses JSON_ARRAYAGG; add case when backend available:
    case DB_BACKEND_MYSQL:
      return "JSON_ARRAYAGG";
    */
    
    default:
      /* Fail fast for unsupported backends */
      return NULL;
    }
}

/**
 * @brief Map symbolic conflict intent to PostgreSQL column list.
 *
 * Returns NULL for unknown intents.
 */
static const char *
pg_conflict_intent_to_columns(const char *intent)
{
  if (!intent)
    return NULL;
  
  /* Direct column mappings - intent matches column list */
  if (strcmp(intent, "player_id") == 0)
    return "player_id";
  if (strcmp(intent, "player_id,key") == 0)
    return "player_id, key";
  if (strcmp(intent, "player_id,event_type") == 0)
    return "player_id, event_type";
  if (strcmp(intent, "player_id,name") == 0)
    return "player_id, name";
  if (strcmp(intent, "player_id,topic") == 0)
    return "player_id, topic";
  if (strcmp(intent, "player_id,scope,key") == 0)
    return "player_id, scope, key";
  if (strcmp(intent, "planet_id") == 0)
    return "planet_id";
  if (strcmp(intent, "planet_id,commodity") == 0)
    return "planet_id, commodity";
  if (strcmp(intent, "key") == 0)
    return "key";
  if (strcmp(intent, "port_id,player_id") == 0)
    return "port_id, player_id";
  if (strcmp(intent, "cluster_id,player_id") == 0)
    return "cluster_id, player_id";
  if (strcmp(intent, "corp_id,player_id") == 0)
    return "corp_id, player_id";
  if (strcmp(intent, "notice_id,player_id") == 0)
    return "notice_id, player_id";
  if (strcmp(intent, "draw_date") == 0)
    return "draw_date";
  
  /* Semantic intents - map to actual columns */
  if (strcmp(intent, "entity_stock") == 0)
    return "entity_type, entity_id, commodity_code";
  if (strcmp(intent, "engine_deadletter") == 0)
    return "engine_events_deadletter_id";
  
  return NULL;  /* Unknown intent */
}

/**
 * @brief Build SQL upsert clause for INSERT ... ON CONFLICT ... DO UPDATE.
 *
 * PostgreSQL: "ON CONFLICT(columns) DO UPDATE SET update_clause"
 * MySQL (future): "ON DUPLICATE KEY UPDATE update_clause"
 */
int
sql_upsert_do_update(const db_t *db,
                     const char *conflict_intent,
                     const char *update_clause,
                     char *out_buf,
                     size_t buf_size)
{
  if (!out_buf || buf_size == 0 || !update_clause)
    return -1;

  db_backend_t backend = db ? db_backend(db) : DB_BACKEND_POSTGRES;
  
  switch (backend)
    {
    case DB_BACKEND_POSTGRES:
      {
        const char *columns = pg_conflict_intent_to_columns(conflict_intent);
        if (!columns)
          return -1;  /* Unknown intent */
        
        return snprintf(out_buf, buf_size,
                        "ON CONFLICT(%s) DO UPDATE SET %s",
                        columns, update_clause);
      }
    
    /* MySQL 8.0+ uses ON DUPLICATE KEY UPDATE (no conflict target):
    case DB_BACKEND_MYSQL:
      return snprintf(out_buf, buf_size,
                      "ON DUPLICATE KEY UPDATE %s",
                      update_clause);
    */
    
    default:
      /* Fail fast for unsupported backends */
      return -1;
    }
}


/* Safe append helpers */
static int
sqlb_putc(char *out, size_t out_cap, size_t *io, char c)
{
  if (!out || !io || out_cap == 0) return -1;
  if (*io + 1 >= out_cap) return -1;          /* need space for char + NUL */
  out[(*io)++] = c;
  out[*io] = '\0';
  return 0;
}

static int
sqlb_puts(char *out, size_t out_cap, size_t *io, const char *s)
{
  if (!out || !io || out_cap == 0 || !s) return -1;
  while (*s)
    {
      if (sqlb_putc(out, out_cap, io, *s++) != 0)
        return -1;
    }
  return 0;
}

/**
 * @brief Builds a dialect-specific SQL string from a template.
 *
 * Placeholders:
 *   - "{N}" where N is a positive integer.
 *
 * Escapes:
 *   - "{{" -> "{"
 *   - "}}" -> "}"
 *
 * Dialects:
 *   - PostgreSQL: "{1}" -> "$1"
 *   - SQLite:     "{1}" -> "?1"  (preserves index; allows reuse like "... {1} ... {1} ...")
 *   - Others:     "{1}" -> "?"   (typical)
 *
 * @return 0 on success, -1 on overflow/parse error.
 */
int
sql_build(const db_t *db, const char *template, char *out_buf, size_t buf_size)
{
  size_t out_i = 0;
  const char *p;
  db_backend_t backend;

  if (!out_buf || buf_size == 0)
    return -1;

  out_buf[0] = '\0';

  if (!template)
    return -1;

  backend = db ? db_backend(db) : DB_BACKEND_POSTGRES;
  p = template;

  while (*p)
    {
      /* Literal brace escapes */
      if (p[0] == '{' && p[1] == '{')
        {
          if (sqlb_putc(out_buf, buf_size, &out_i, '{') != 0) goto overflow;
          p += 2;
          continue;
        }
      if (p[0] == '}' && p[1] == '}')
        {
          if (sqlb_putc(out_buf, buf_size, &out_i, '}') != 0) goto overflow;
          p += 2;
          continue;
        }

      /* Placeholder: {N} */
      if (*p == '{' && isdigit((unsigned char)p[1]))
        {
          const char *start = p;   /* in case we need to treat as literal */
          unsigned long n = 0;

          p++; /* skip '{' */

          while (isdigit((unsigned char)*p))
            {
              unsigned digit = (unsigned)(*p - '0');
              if (n > (ULONG_MAX - digit) / 10) goto parse_error; /* overflow */
              n = (n * 10) + digit;
              p++;
            }

          if (*p == '}' && n > 0 && n <= (unsigned long)INT_MAX)
            {
              char tmp[32];
              p++; /* skip '}' */

              if (backend == DB_BACKEND_POSTGRES)
                {
                  /* $N */
                  /* tmp size is ample for "$2147483647" */
                  int written = snprintf(tmp, sizeof tmp, "$%lu", n);
                  if (written <= 0 || (size_t)written >= sizeof tmp) goto parse_error;
                  if (sqlb_puts(out_buf, buf_size, &out_i, tmp) != 0) goto overflow;
                }
              else if (backend == DB_BACKEND_SQLITE)
                {
                  /* ?N  (SQLite supports numbered parameters like ?1 ?2 ...) */
                  int written = snprintf(tmp, sizeof tmp, "?%lu", n);
                  if (written <= 0 || (size_t)written >= sizeof tmp) goto parse_error;
                  if (sqlb_puts(out_buf, buf_size, &out_i, tmp) != 0) goto overflow;
                }
              else
                {
                  /* Generic '?' */
                  if (sqlb_putc(out_buf, buf_size, &out_i, '?') != 0) goto overflow;
                }

              continue;
            }

          /* Not a valid {N} -> treat literally from '{' and continue normally */
          p = start;
          /* fall through to copy one char */
        }

      /* Normal character */
      if (sqlb_putc(out_buf, buf_size, &out_i, *p) != 0) goto overflow;
      p++;
    }

  return 0;

overflow:
parse_error:
  /* Fail safe: never return partial SQL. */
  out_buf[0] = '\0';
  return -1;
}

/**
 * @brief Generate SQL expression for expanding a JSON array into rows.
 *
 * Writes a SQL expression suitable for use in a subquery to expand a JSON
 * array into individual elements.
 */
int
sql_json_array_to_rows(const db_t *db,
                       int json_param_idx,
                       char *out_buf,
                       size_t out_sz)
{
  db_backend_t b = db ? db_backend(db) : DB_BACKEND_POSTGRES;

  if (!out_buf || out_sz == 0) return -1;

  switch (b)
    {
    case DB_BACKEND_POSTGRES:
      /* PostgreSQL: json_array_elements_text({N}) returns text values */
      return (snprintf(out_buf, out_sz, "json_array_elements_text({%d})", json_param_idx) < (int)out_sz) ? 0 : -1;

    case DB_BACKEND_MYSQL:
      /* MySQL: JSON_EXTRACT with $[*] pattern - returns multiple rows */
      return (snprintf(out_buf, out_sz, "JSON_EXTRACT({%d}, '$[*]')", json_param_idx) < (int)out_sz) ? 0 : -1;

    default:
      return -1;
    }
}
