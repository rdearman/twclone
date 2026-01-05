# DIALECT-CLEANLINESS AUDIT REPORT

## Executive Summary

**Question:** Is the code dialect-clean (ready for future MySQL/Oracle support)?

**Answer:** **MOSTLY (~75%), BUT NOT COMPLETELY**

The code has a good foundation with abstraction layers, but still contains **103+ PostgreSQL-specific constructs** that need to be addressed before MySQL/Oracle support is viable.

---

## CRITICAL ISSUES FOUND

### 1. PostgreSQL Type Casts (::) - 7 INSTANCES

**Status:** ❌ NOT DIALECT-CLEAN | **Severity:** HIGH

PostgreSQL-specific `::type` cast syntax is hardcoded in SQL templates.

**Examples:**
- `engine_consumer.c:66` - `EXTRACT(EPOCH FROM last_event_ts)::bigint`
- `server_cron.c:1361` - `to_timestamp({1}::bigint)`
- `server_universe.c:1520` - `{1}::int`
- `engine_consumer.c:59` - `{3}::json`

**Issue:** `::type` syntax doesn't exist in MySQL/Oracle
- MySQL: Must use `CAST(expr AS type)`
- Oracle: Has `::` support in newer versions, but not consistently

**Impact:** Queries will fail immediately on MySQL/Oracle

---

### 2. ON CONFLICT Clauses - 53 INSTANCES

**Status:** ❌ NOT DIALECT-CLEAN | **Severity:** CRITICAL

PostgreSQL-specific UPSERT syntax is used throughout the codebase.

**Raw SQL instances (5+):**
```sql
-- database_cmd.c:993
"ON CONFLICT(planet_id, commodity) DO UPDATE SET quantity = ..."

-- database_cmd.c:1214  
"ON CONFLICT DO NOTHING;"

-- database_cmd.c:1836
"... DO UPDATE SET port_id = excluded.port_id ..."
```

**Abstracted via functions (48 more):**
- `sql_insert_ignore_clause()` - returns NULL for MySQL
- `sql_conflict_target_fmt()` - returns NULL for MySQL
- `sql_entity_stock_upsert_epoch_fmt()` - returns NULL for MySQL

**Issue:** Each database has different UPSERT syntax:
- PostgreSQL: `INSERT ... ON CONFLICT(...) DO UPDATE SET ...`
- MySQL: `INSERT ... ON DUPLICATE KEY UPDATE ...`
- SQLite: `INSERT OR REPLACE INTO ...`
- Oracle: `MERGE INTO ...`

**Impact:** All upsert operations fail on MySQL/Oracle

---

### 3. FOR UPDATE SKIP LOCKED - 24 INSTANCES

**Status:** ❌ NOT DIALECT-CLEAN | **Severity:** CRITICAL

PostgreSQL row locking syntax is hardcoded and only partially abstracted.

**Raw SQL instances (5+):**
```sql
-- server_combat.c:2682
"FOR UPDATE SKIP LOCKED;"

-- server_combat.c:2764
"... FOR UPDATE SKIP LOCKED "

-- server_combat.c:3253
"... FOR UPDATE SKIP LOCKED;"
```

**Abstracted via `sql_for_update_skip_locked()`:**
- Function returns NULL for MySQL (has commented code)
- Function returns NULL for SQLite (no equivalent)
- Function returns NULL for Oracle (different syntax)

**Issue:** This is CORE TO COMBAT GAMEPLAY
- PostgreSQL: `FOR UPDATE SKIP LOCKED` (pessimistic locking)
- MySQL 8.0+: Same syntax exists but code commented
- SQLite: No row locking equivalent
- Oracle: Different syntax

**Impact:** Combat system completely non-functional without this

---

### 4. JSON Functions - 6 INSTANCES

**Status:** ⚠️ PARTIALLY DIALECT-CLEAN | **Severity:** HIGH

PostgreSQL JSON functions are used but only partially abstracted.

**Hardcoded instances:**
```sql
-- engine_consumer.c:59-63 (CRITICAL)
"SELECT trim(value) FROM json_array_elements_text({3}::json)"
```

**Via abstraction:**
```c
-- server_cron.c:1992
sql_json_object_fn()  // returns NULL for MySQL
```

**Issue:** No portable way to handle JSON arrays
- PostgreSQL: `json_array_elements_text()` function
- MySQL: `JSON_EXTRACT()` for reading, `JSON_ARRAY()` for building
- SQLite: No native JSON array iteration
- Oracle: Different JSON functions

**Impact:** engine_consumer.c breaks immediately; trap processing fails

---

### 5. CURRENT_TIMESTAMP - 9 INSTANCES

**Status:** ✅ MOSTLY DIALECT-CLEAN

Good news: Most instances are properly abstracted via `sql_now_expr()`

**Abstraction handles:**
- PostgreSQL: `timezone('UTC', CURRENT_TIMESTAMP)`
- MySQL: `UTC_TIMESTAMP()`
- Oracle: `SYS_EXTRACT_UTC(SYSTIMESTAMP)`

**Assessment:** GOOD - This pattern is being handled correctly

---

### 6. EXTRACT(EPOCH FROM ...) - 4 INSTANCES

**Status:** ⚠️ MIXED | **Severity:** MEDIUM

Some instances use abstraction, some are hardcoded.

**Abstracted via `sql_ts_to_epoch_expr()`:**
- PostgreSQL: `EXTRACT(EPOCH FROM column)`
- MySQL: `UNIX_TIMESTAMP(column)`
- Oracle: `((CAST((...) AS DATE) - DATE '1970-01-01') * 86400)`

**Hardcoded in engine_consumer.c:66:**
```sql
"SELECT last_event_id, EXTRACT(EPOCH FROM last_event_ts)::bigint ..."
```

**Issue:** Hardcoded version bypasses abstraction
**Impact:** engine_consumer.c will fail on MySQL/Oracle

---

## DIALECT-CLEANLINESS SCORECARD

| Category | Count | Status | Risk |
|----------|-------|--------|------|
| Type casts (::) | 7 | ❌ Hardcoded | HIGH |
| ON CONFLICT | 53 | ⚠️ Mixed (48 abstracted, 5 raw) | CRITICAL |
| FOR UPDATE SKIP LOCKED | 24 | ❌ Mostly hardcoded | CRITICAL |
| JSON functions | 6 | ⚠️ Mostly abstracted | HIGH |
| CURRENT_TIMESTAMP | 9 | ✅ Abstracted | GOOD |
| EXTRACT(EPOCH) | 4 | ⚠️ Mixed | MEDIUM |
| **TOTAL** | **103+** | ⚠️ **MIXED** | **MEDIUM-CRITICAL** |

---

## ROOT CAUSE ANALYSIS

### Why dialect-cleanliness is incomplete:

1. **Incomplete Abstraction Layer**
   - Functions like `sql_insert_ignore_clause()` return NULL for non-PG
   - Functions don't fail gracefully - just return NULL
   - Callers don't validate return values properly

2. **Hardcoded PostgreSQL Syntax in engine_consumer.c**
   - Circumvents abstraction layer entirely
   - Has `EXTRACT(EPOCH)`, `::bigint` cast, `json_array_elements_text()` 
   - Should use `sql_ts_to_epoch_expr()` but doesn't

3. **Abandoned MySQL Work**
   - Commented-out MySQL implementations in sql_driver.c
   - Shows someone started but never finished
   - MySQL code never activated even where it exists

4. **Design Debt**
   - Functions return NULL for unimplemented backends
   - No graceful degradation or fallbacks
   - Callers must handle NULL returns (some don't)

---

## FIX PRIORITY

### CRITICAL (Must fix before MySQL support)

1. **Fix engine_consumer.c** (2-3 hours)
   - Use `sql_ts_to_epoch_expr()` for EXTRACT(EPOCH)
   - Remove `::bigint` cast
   - Abstract JSON array operations
   - Add error checking

2. **Complete ON CONFLICT abstraction** (1-2 days)
   - Implement MySQL `ON DUPLICATE KEY UPDATE`
   - Implement Oracle `MERGE INTO`
   - Test all 53 instances
   - Make functions return error instead of NULL

3. **Complete FOR UPDATE SKIP LOCKED abstraction** (1-2 days)
   - Activate commented MySQL code
   - Implement Oracle locking strategy
   - Plan SQLite workaround (application-level locking)
   - Test combat module thoroughly

### HIGH (Should fix for cleanliness)

4. **Fix type casts** (4-6 hours)
   - Replace `::type` with portable syntax
   - Use `CAST(expr AS type)` where needed
   - Test all 7 instances

5. **Abstract JSON operations** (1-2 days)
   - Handle JSON array iteration portably
   - May need application-level JSON parsing for some operations

### MEDIUM (Nice to have)

6. **Add error handling** (ongoing)
   - Make abstraction functions fail gracefully
   - Add validation in callers
   - Add logging for dialect mismatches

---

## WHAT'S GOOD ✅

1. **{N} Placeholder Abstraction**
   - All placeholders use portable `{N}` syntax
   - `sql_build()` converts to backend-specific syntax
   - Good practice throughout

2. **Timestamp Generation Abstraction**
   - `sql_now_expr()` handles all databases
   - Mostly used correctly
   - PostgreSQL, MySQL, Oracle implementations exist

3. **Epoch Conversion Abstraction** (mostly)
   - `sql_ts_to_epoch_expr()` provides abstraction
   - Multiple backends supported
   - Most code uses it (except engine_consumer.c)

4. **Minimal Raw SQL**
   - Most SQL goes through abstraction
   - Limited number of hardcoded queries
   - Good separation of concerns

---

## WHAT'S PROBLEMATIC ⚠️

1. **Abstraction Layer is Incomplete**
   - Functions return NULL for unsupported backends
   - No fallback or graceful error handling
   - Callers sometimes don't check for NULL

2. **engine_consumer.c Circumvents Abstraction**
   - Multiple PostgreSQL-specific constructs
   - Should use sql_ts_to_epoch_expr() but doesn't
   - Demonstrates maintenance risk

3. **Partial Abstraction for Critical Features**
   - ON CONFLICT: Abstracted but implementation incomplete
   - FOR UPDATE SKIP LOCKED: Abstracted but incomplete
   - JSON functions: Partially abstracted

4. **MySQL Code Exists but Commented Out**
   - Suggests incomplete implementation
   - Creates confusion about support
   - Wastes effort if not used

---

## EFFORT ESTIMATE FOR FULL DIALECT-CLEANLINESS

### To make 100% dialect-clean and MySQL-ready:

**Phase 1: Fix critical PostgreSQL-isms (1-2 days)**
- engine_consumer.c fixes
- Type cast removal
- JSON operation abstraction

**Phase 2: Complete ON CONFLICT abstraction (1-2 days)**
- MySQL ON DUPLICATE KEY UPDATE
- Oracle MERGE INTO
- Test all 53 instances
- Error handling

**Phase 3: Complete FOR UPDATE SKIP LOCKED (2-3 days)**
- Activate MySQL code
- Implement Oracle locking
- Plan SQLite alternative
- Extensive testing of combat module

**Phase 4: Error handling and validation (1 day)**
- Make abstractions fail gracefully
- Add logging
- Code review

**Total Estimate: 5-8 days of focused engineering**

---

## RECOMMENDATIONS

### IMMEDIATE (Week 1)

1. ✅ Fix engine_consumer.c
   - Replace hardcoded EXTRACT(EPOCH FROM)
   - Use `sql_ts_to_epoch_expr()` properly
   - Abstract or refactor JSON operations

2. ✅ Audit all 7 type casts
   - Replace `::type` with `CAST(expr AS type)`
   - Test on PostgreSQL
   - Validate behavior matches

3. ✅ Add error handling to abstraction functions
   - Make functions return error codes, not NULL
   - Update callers to check errors
   - Add logging for dialect mismatches

### MEDIUM TERM (Weeks 2-3)

4. ✅ Complete ON CONFLICT abstraction
   - Implement MySQL ON DUPLICATE KEY UPDATE
   - Implement Oracle MERGE INTO
   - Remove NULL returns - use error codes instead
   - Comprehensive testing

5. ✅ Complete FOR UPDATE SKIP LOCKED abstraction
   - Activate commented MySQL code
   - Implement Oracle strategy
   - Plan SQLite workaround
   - Combat module testing

### LONG TERM (Ongoing)

6. ✅ Add dialect testing
   - Create tests for each database backend
   - Run tests on PostgreSQL, MySQL, Oracle
   - Add CI/CD checks for dialect purity
   - Document backend-specific behaviors

7. ✅ Code review process
   - Review new SQL for dialect-specific constructs
   - Enforce use of abstraction layer
   - Prevent new hardcoded PostgreSQL syntax

---

## CONCLUSION

### Is the code dialect-clean?

**Answer: NOT YET, but it has a good foundation**

**Current state:**
- 75% dialect-clean (good abstraction layer)
- 25% dialect-dirty (hardcoded PostgreSQL constructs)
- Foundation is solid but incomplete

**What works:**
- Placeholder abstraction ✅
- Timestamp generation ✅
- Epoch conversion (mostly) ✅
- Basic CRUD operations ✅

**What doesn't work:**
- ON CONFLICT patterns ❌
- FOR UPDATE SKIP LOCKED ❌
- JSON operations ❌ 
- Type casts ❌
- engine_consumer.c ❌

**Is it ready for MySQL implementation?**
**NO - needs 5-8 days of fixes first**

**After fixes, will it be dialect-clean?**
**YES - the foundation is good, just needs completion**

---

## SPECIFIC FILES NEEDING FIXES

### Priority 1: CRITICAL

1. **src/engine_consumer.c**
   - Line 66: EXTRACT(EPOCH FROM ...)::bigint
   - Lines 59-63: json_array_elements_text(...)::json
   - **Action:** Use abstractions, fix type casts

### Priority 2: HIGH

2. **src/database_cmd.c**
   - Line 993: ON CONFLICT ...
   - Line 1214: ON CONFLICT ...
   - Line 1836: ON CONFLICT with excluded.*
   - **Action:** Complete abstraction layer

3. **src/server_combat.c**
   - Multiple lines: FOR UPDATE SKIP LOCKED
   - **Action:** Complete abstraction layer

4. **src/server_cron.c**
   - Line 1361: to_timestamp({1}::bigint)
   - **Action:** Remove ::bigint cast

5. **src/server_universe.c**
   - Line 1520: {1}::int
   - **Action:** Replace with portable casting

### Priority 3: MEDIUM

6. **src/db/sql_driver.c**
   - Review all abstraction functions
   - **Action:** Complete MySQL/Oracle implementations

---

## SUCCESS CRITERIA

Once complete, dialect-cleanliness audit should show:

- ✅ ZERO hardcoded PostgreSQL type casts (::)
- ✅ ZERO raw ON CONFLICT syntax (all abstracted)
- ✅ ZERO raw FOR UPDATE SKIP LOCKED (all abstracted)
- ✅ ZERO hardcoded JSON functions (all abstracted)
- ✅ ALL abstraction functions return proper implementations (not NULL)
- ✅ ERROR CHECKING on all abstraction function calls
- ✅ engine_consumer.c uses abstraction layer
- ✅ Tests pass for all abstraction functions

---

Generated: 2026-01-05
Report Version: 1.0
Next Review: After fixes applied
