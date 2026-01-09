# Dialect-Clean Coding Guide

This guide documents the patterns discovered and established in Session 2 of the PostgreSQL/Multi-Database migration work.

## The Problem

The codebase had hardcoded PostgreSQL-specific type casts and functions that bypass the abstraction layer:

```c
// ❌ BAD: Hardcoded PostgreSQL syntax
sql_build(db, 
          "SELECT ... EXTRACT(EPOCH FROM ts)::bigint ... to_timestamp({1}::bigint)",
          sql, sizeof(sql));

// ❌ BAD: Type casts in SQL strings
"WHERE expires_at > to_timestamp({1}::bigint) AND ..."

// ❌ BAD: Ignoring abstraction functions that already exist
const char *fmt = sql_epoch_param_to_timestamptz(db);  // Retrieved but not used!
if (sql_build(db, "... to_timestamp({1}::bigint) ...", sql, sizeof(sql)))
```

## The Solution: Three-Step Pattern

### Step 1: Get Abstraction Function Result

Call the appropriate abstraction function and check for NULL (indicating unsupported backend):

```c
// For converting epoch parameter to timestamp
const char *ts_format = sql_epoch_param_to_timestamptz(db);
if (!ts_format) return -1;  // Fail gracefully on unsupported backend

// For extracting epoch from timestamp column
char epoch_expr[256];
if (sql_ts_to_epoch_expr(db, "column_name", epoch_expr, sizeof(epoch_expr)) != 0)
  return -1;  // Fail gracefully on unsupported backend

// For current timestamp
const char *now = sql_now_expr(db);
if (!now) return -1;  // Fail gracefully on unsupported backend
```

### Step 2: Build SQL Template with snprintf()

Use `snprintf()` to inject the abstraction function result into your SQL template:

```c
// Single timestamp parameter
char sql_template[512];
snprintf(sql_template, sizeof(sql_template),
         "INSERT INTO sessions (expires) VALUES (%s) WHERE id = {1}",
         ts_format);

// Multiple timestamp parameters
snprintf(sql_template, sizeof(sql_template),
         "UPDATE bets SET resolved_at = %s WHERE expires_at <= %s",
         ts_format, ts_format);

// With extracted epoch
snprintf(sql_template, sizeof(sql_template),
         "SELECT id, %s FROM events WHERE ts > {1}",
         epoch_expr);

// With current timestamp
snprintf(sql_template, sizeof(sql_template),
         "UPDATE table SET updated_at = %s WHERE id = {1}",
         now);
```

### Step 3: Pass Complete Template to sql_build()

Let `sql_build()` handle the `{N}` placeholder conversion:

```c
char sql[512];
if (sql_build(db, sql_template, sql, sizeof(sql)) != 0)
  return -1;  // Fail on sql_build error

// Then execute
db_bind_t params[] = { db_bind_i64(epoch_value), db_bind_i32(id) };
db_error_t err;
if (!db_exec(db, sql, params, 2, &err))
  return err.code;
```

## Complete Example

### Bad Code (Dialect-Specific)
```c
int create_session(int player_id, long long expires_epoch) {
  db_t *db = game_db_get_handle();
  const char *sql = "INSERT INTO sessions (player_id, expires) "
                    "VALUES ({1}, to_timestamp({2}::bigint))";  // ❌ ::bigint is hardcoded
  db_bind_t params[] = { db_bind_i32(player_id), db_bind_i64(expires_epoch) };
  db_error_t err;
  if (!db_exec(db, sql, params, 2, &err))
    return -1;
  return 0;
}
```

### Good Code (Dialect-Clean)
```c
int create_session(int player_id, long long expires_epoch) {
  db_t *db = game_db_get_handle();
  if (!db) return -1;
  
  // Step 1: Get abstraction function result
  const char *ts_fmt = sql_epoch_param_to_timestamptz(db);
  if (!ts_fmt) return -1;  // Unsupported backend
  
  // Step 2: Build template with injected format string
  char sql_template[512];
  snprintf(sql_template, sizeof(sql_template),
           "INSERT INTO sessions (player_id, expires) VALUES ({1}, %s)",
           ts_fmt);
  
  // Step 3: Let sql_build handle {N} placeholders
  char sql[512];
  if (sql_build(db, sql_template, sql, sizeof(sql)) != 0)
    return -1;
  
  // Execute
  db_bind_t params[] = { db_bind_i32(player_id), db_bind_i64(expires_epoch) };
  db_error_t err;
  if (!db_exec(db, sql, params, 2, &err))
    return -1;
  return 0;
}
```

## Available Abstraction Functions

### Timestamp Expressions
```c
// Current timestamp in TIMESTAMP/TIMESTAMPTZ fields
const char *sql_now_expr(const db_t *db);
// PostgreSQL: "timezone('UTC', CURRENT_TIMESTAMP)"
// MySQL: "UTC_TIMESTAMP()"
// Oracle: "SYS_EXTRACT_UTC(SYSTIMESTAMP)"
// Returns: NULL for unsupported backend

// Current Unix epoch (seconds since 1970)
const char *sql_epoch_now(const db_t *db);
// PostgreSQL only: "EXTRACT(EPOCH FROM NOW())::bigint"
// Returns: NULL for non-PostgreSQL

// Current epoch for operations
// Usage: Value for inserting into bigint epoch columns
```

### Timestamp Conversion

```c
// Convert timestamp column/expression to Unix epoch
int sql_ts_to_epoch_expr(const db_t *db,
                         const char *ts_expr,
                         char *out_buf,
                         size_t out_sz);
// PostgreSQL: "EXTRACT(EPOCH FROM {ts_expr})"
// MySQL: "UNIX_TIMESTAMP({ts_expr})"
// Oracle: Timestamp difference calculation
// Returns: 0 on success, -1 on error
// NOTE: Requires output buffer, result must be injected with snprintf

// Convert epoch parameter to timestamp for comparisons
const char *sql_epoch_param_to_timestamptz(const db_t *db);
// PostgreSQL: "to_timestamp(%s)"
// MySQL: "FROM_UNIXTIME(%s)" (when driver available)
// Oracle: Similar conversion (when driver available)
// Returns: Format string or NULL for unsupported backend
// NOTE: Result is a format string for snprintf injection
```

### JSON Operations

```c
// Build JSON object
const char *sql_json_object_fn(const db_t *db);
// PostgreSQL: "json_build_object"
// MySQL: "JSON_OBJECT"
// Oracle: Similar (when available)

// Aggregate JSON array
const char *sql_json_arrayagg_fn(const db_t *db);
// PostgreSQL: "json_agg"
// MySQL: "JSON_ARRAYAGG"

// Expand JSON array to rows (for filtering)
int sql_json_array_to_rows(const db_t *db,
                           int json_param_idx,
                           char *out_buf,
                           size_t out_sz);
// PostgreSQL: "json_array_elements_text({N})"
// MySQL: "JSON_EXTRACT({N}, '$[*]')"
// Returns: 0 on success, -1 on error
```

### Conflict Handling (PostgreSQL-specific, returns NULL elsewhere)

```c
// Simple ON CONFLICT clause
const char *sql_insert_ignore_clause(const db_t *db);
// PostgreSQL: "ON CONFLICT DO NOTHING"
// Returns: NULL for non-PostgreSQL

// On CONFLICT with target specification
const char *sql_conflict_target_fmt(const db_t *db);
// PostgreSQL: "ON CONFLICT(%s) DO"
// Returns: Format string or NULL
```

### Row Locking

```c
// Pessimistic locking with skip
const char *sql_for_update_skip_locked(const db_t *db);
// PostgreSQL: " FOR UPDATE SKIP LOCKED"
// MySQL 8.0+: " FOR UPDATE SKIP LOCKED" (identical syntax)
// Returns: " FOR UPDATE SKIP LOCKED" or NULL for unsupported backend
```

## Common Mistakes to Avoid

### ❌ Wrong: Hardcoding type casts
```c
"WHERE expires_at > to_timestamp({1}::bigint)"  // PostgreSQL syntax
```

### ✅ Right: Use abstraction
```c
const char *fmt = sql_epoch_param_to_timestamptz(db);
// Then in snprintf: WHERE expires_at > %s", fmt
```

---

### ❌ Wrong: Ignoring abstraction function results
```c
const char *fmt = sql_now_expr(db);  // Obtained but not used
sql_build(db, "INSERT INTO t (ts) VALUES (NOW())", ...);
```

### ✅ Right: Use the result
```c
const char *fmt = sql_now_expr(db);
char template[256];
snprintf(template, sizeof(template), "INSERT INTO t (ts) VALUES (%s)", fmt);
sql_build(db, template, ...);
```

---

### ❌ Wrong: Mixing approaches
```c
snprintf(template, ..., "... %s ...", sql_now_expr(db));  // Works but confusing
```

### ✅ Right: Separate concerns
```c
const char *now = sql_now_expr(db);
if (!now) return -1;
snprintf(template, ..., "... %s ...", now);
```

---

### ❌ Wrong: Not checking NULL returns
```c
const char *fmt = sql_epoch_param_to_timestamptz(db);
snprintf(..., fmt);  // Crashes if NULL for unsupported backend
```

### ✅ Right: Check before using
```c
const char *fmt = sql_epoch_param_to_timestamptz(db);
if (!fmt) return -1;  // Fail gracefully
snprintf(..., fmt);
```

## Testing Strategies

1. **Compile Check**: All code should compile without warnings about undefined functions
2. **NULL Check**: Verify functions return NULL for unsupported backends
3. **PostgreSQL Test**: Verify generated SQL works on PostgreSQL
4. **Future MySQL Test**: Document what MySQL version would need for each construct

## Migration Checklist for Future SQL Code

When writing new database code, use this checklist:

- [ ] No hardcoded PostgreSQL type casts (::bigint, ::json, ::text, etc.)
- [ ] No hardcoded PostgreSQL functions (EXTRACT, INTERVAL, json_*, etc.)
- [ ] All timestamp operations use abstraction functions
- [ ] All JSON operations use abstraction functions
- [ ] All dialect differences handled by sql_driver functions
- [ ] NULL checks for all abstraction function calls
- [ ] Proper use of snprintf for template injection
- [ ] Complete template passed to sql_build()
- [ ] Parameters bound using db_bind_* functions
- [ ] Documented which abstraction patterns used in code review
