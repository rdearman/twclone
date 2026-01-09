# Quick Reference: Dialect-Clean SQL Patterns

## Three-Step Pattern (TL;DR)

```c
// 1. Get abstraction
const char *fmt = sql_epoch_param_to_timestamptz(db);
if (!fmt) return -1;

// 2. Inject with snprintf
char template[512];
snprintf(template, sizeof(template), "... WHERE col > %s", fmt);

// 3. Pass to sql_build
char sql[512];
sql_build(db, template, sql, sizeof(sql));
```

## Common Abstractions

```c
// Current timestamp
const char *now = sql_now_expr(db);
// "timezone('UTC', CURRENT_TIMESTAMP)" on PostgreSQL
// "UTC_TIMESTAMP()" on MySQL

// Epoch from timestamp column
char buf[256];
sql_ts_to_epoch_expr(db, "column_name", buf, sizeof(buf));
// "EXTRACT(EPOCH FROM column_name)" on PostgreSQL
// "UNIX_TIMESTAMP(column_name)" on MySQL

// Convert epoch parameter to timestamp
const char *fmt = sql_epoch_param_to_timestamptz(db);
// "to_timestamp(%s)" on PostgreSQL
// "FROM_UNIXTIME(%s)" on MySQL (future)
```

## Quick Checklist

- [ ] No `::bigint`, `::json`, `::text` in SQL strings
- [ ] No `EXTRACT(EPOCH` in SQL - use `sql_ts_to_epoch_expr()`
- [ ] No `to_timestamp(...::int)` - use `sql_epoch_param_to_timestamptz()`
- [ ] No bare `NOW()` - use `sql_now_expr()`
- [ ] Check NULL returns from abstractions
- [ ] Use snprintf for injection
- [ ] Pass complete template to sql_build()

## Avoid These

```c
// ❌ Hardcoded type casts
"WHERE expires > to_timestamp({1}::bigint)"

// ❌ Ignoring abstraction results
const char *fmt = sql_epoch_param_to_timestamptz(db);
// ... don't use fmt

// ❌ Not checking NULL
const char *fmt = sql_now_expr(db);
snprintf(..., fmt);  // Crashes if NULL

// ❌ Wrong order
snprintf(..., sql_now_expr(db));  // Confusing
```

## Do This Instead

```c
// ✅ Use abstraction
const char *fmt = sql_epoch_param_to_timestamptz(db);
if (!fmt) return -1;
snprintf(template, sizeof(template), "WHERE expires > %s", fmt);
sql_build(db, template, sql, sizeof(sql));
```

## Files to Review

- `src/db/sql_driver.h` - Function declarations
- `src/db/sql_driver.c` - Implementations
- `DIALECT_CLEAN_CODING_GUIDE.md` - Full reference
- `SESSION_2_DIALECT_FIXES.md` - Examples from fixes
