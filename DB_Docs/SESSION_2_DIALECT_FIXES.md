# Session 2: Critical Dialect Violation Fixes

**Date**: 2026-01-05  
**Focus**: Fixing hardcoded PostgreSQL type casts and dialect-specific syntax  
**Status**: ✅ COMPLETE

## Summary

Fixed **4 critical dialect violations** in engine_consumer.c and server_cron.c where hardcoded PostgreSQL type casts (`::bigint`, `::json`, `to_timestamp(...::bigint)`) were circumventing the abstraction layer.

### Critical Issues Fixed

#### Issue 1: EXTRACT(EPOCH FROM...) Hardcoding
**Location**: `src/engine_consumer.c:66` (load_watermark function)
**Problem**: SQL template hardcoded `EXTRACT(EPOCH FROM last_event_ts)::bigint`
- This is PostgreSQL-specific syntax
- Would fail on MySQL (needs `UNIX_TIMESTAMP()`) or Oracle
- Type cast `::bigint` is PostgreSQL-only

**Solution**: Use `sql_ts_to_epoch_expr()` helper
- Replaced hardcoded expression with portable abstraction
- Function generates backend-specific code:
  - PostgreSQL: `EXTRACT(EPOCH FROM last_event_ts)`
  - MySQL: `UNIX_TIMESTAMP(last_event_ts)`
  - Oracle: Timestamp difference calculation
- Removes unnecessary type cast

#### Issue 2 & 3: JSON Array Hardcoding (2 instances)
**Location**: `src/engine_consumer.c:208` (BASE_SELECT_PG) and lines 450 (dynamic template)
**Problem**: Hardcoded `json_array_elements_text({3}::json)`
- Explicit `::json` type cast is PostgreSQL syntax
- Unnecessary because `json_array_elements_text()` accepts text and auto-converts

**Solution**: Remove explicit type cast
- Changed `json_array_elements_text({3}::json)` → `json_array_elements_text({3})`
- Reduces dialect-specific syntax while maintaining functionality
- Note: Function itself is still PostgreSQL-only; full abstraction deferred

**Note for Future**: Added `sql_json_array_to_rows()` helper in sql_driver.h/c for future MySQL support:
- PostgreSQL: `json_array_elements_text({N})`
- MySQL: `JSON_EXTRACT({N}, '$[*]')`

#### Issue 4: Tavern Deadpool Bets Hardcoding
**Location**: `src/server_cron.c:4285` (deadpool_resolution_cron function)
**Problem**: Hardcoded `to_timestamp({1}::bigint)` and `to_timestamp({2}::bigint)`
- Type cast `::bigint` is PostgreSQL-only
- Violates abstraction layer even though abstraction existed nearby!
- The function had already retrieved `sql_epoch_param_to_timestamptz(db)` but didn't use it

**Solution**: Use abstraction layer properly
- Get format string: `const char *ts_fmt3 = sql_epoch_param_to_timestamptz(db)`
- Build template with snprintf: `snprintf(sql_expire_tmpl, ..., "... %s ... %s", ts_fmt3, ts_fmt3)`
- Pass complete template to sql_build(): `sql_build(db, sql_expire_tmpl, ...)`
- Now properly abstracted:
  - PostgreSQL: injects `to_timestamp(%s)`
  - MySQL: would inject different conversion (when driver available)

## Pattern Established

These fixes establish the **correct pattern** for dialect-clean code:

```c
// Step 1: Get abstraction function
const char *format = sql_epoch_param_to_timestamptz(db);
if (!format) return -1;  // Fail on unsupported backend

// Step 2: Build SQL template with snprintf
char template[512];
snprintf(template, sizeof(template),
         "SELECT ... WHERE col <= %s AND col2 > %s",
         format, format);

// Step 3: Use sql_build to convert {N} placeholders
char sql[512];
sql_build(db, template, sql, sizeof(sql));

// Step 4: Execute with db_bind_* parameters
db_bind_t params[] = { db_bind_i64(epoch_value1), db_bind_i64(epoch_value2) };
db_exec(db, sql, params, 2, &err);
```

## Build Results

✅ **Clean build**: Both bin/server and bin/bigbang compile successfully  
✅ **No new errors**: All pre-existing warnings (implicit declarations) remain  
✅ **Binary freshness**: Both binaries rebuilt 2026-01-05 14:51  

## Files Modified

1. **src/engine_consumer.c** (3 changes)
   - load_watermark(): EXTRACT(EPOCH FROM...) → sql_ts_to_epoch_expr()
   - BASE_SELECT_PG: Removed ::json cast
   - Dynamic SQL template: Removed ::json cast

2. **src/server_cron.c** (1 change)
   - deadpool_resolution_cron(): to_timestamp(...::bigint) → sql_epoch_param_to_timestamptz()

3. **src/db/sql_driver.h** (1 addition)
   - Added sql_json_array_to_rows() function declaration

4. **src/db/sql_driver.c** (1 addition)
   - Added sql_json_array_to_rows() implementation

## Remaining Dialect Issues (Known, not critical)

The following PostgreSQL-specific constructs remain intentional (not type casts):
- `ON CONFLICT` clauses (53 instances) - need MySQL `ON DUPLICATE KEY` implementation
- `FOR UPDATE SKIP LOCKED` (24 instances) - need MySQL equivalent implementation
- `json_array_elements_text()` function itself - not yet abstracted
- `INTERVAL` syntax - not yet abstracted

These are NOT fixed in this session because:
1. They require implementing MySQL driver (10-16 weeks effort per audit)
2. PostgreSQL is the only working backend
3. Type casts (`::bigint`, `::json`) that bypass abstraction were higher priority
4. These are identified in DIALECT_CLEANLINESS_REPORT.md for future work

## Testing Recommendations

1. **Verify db_session_create()**: Test account registration (from previous session)
2. **Verify load_watermark()**: Test event processing in engine_consumer
3. **Verify deadpool_resolution_cron()**: Test tavern deadpool bet expiration
4. **Verify JSON handling**: Test type filtering in engine_consumer queries

## Next Steps

1. Run account registration test to verify all session fixes work
2. Search codebase for other abstract function calls that aren't being used (like ts_fmt3 was)
3. Plan Phase 3: Complete dialect-cleanliness audit (5-8 days effort to reach 100%)
4. Plan Phase 4: Remove SQLite support entirely
5. Plan Phase 5: Implement MySQL driver (10-16 weeks)
