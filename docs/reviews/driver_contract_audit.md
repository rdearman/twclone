# Driver Contract Audit: PostgreSQL + MySQL Compatibility

**Review Type:** API specification (NO code changes)  
**Scope:** Define driver interface that abstracts PostgreSQL-specific SQL away from business logic  
**Audiences:** Architecture review, future backend implementers (db_mysql.c, db_sqlite.c)

---

## A) Driver Capability Matrix

This matrix defines what each backend must support. ✅ = native, ⚠️ = fallback/workaround, ❌ = not feasible.

| Capability | PostgreSQL | MySQL 5.7 | MySQL 8.0+ | SQLite | Abstraction Method |
|------------|-----------|-----------|-----------|--------|-------------------|
| **Transactions (BEGIN/COMMIT/ROLLBACK)** | ✅ Full | ✅ Full | ✅ Full | ✅ Full | `db_tx_begin/commit/rollback` |
| **Prepared Statements ($1, $2, ...)** | ✅ Native (libpq) | ✅ Client/Server | ✅ Client/Server | ✅ Native | Placeholder translation layer |
| **Last Insert ID** | ✅ RETURNING + result set | ⚠️ LAST_INSERT_ID() | ⚠️ LAST_INSERT_ID() | ✅ sqlite3_last_insert_rowid() | `db_exec_insert_id()` |
| **Affected Rows** | ✅ PQcmdTuples() | ✅ mysql_affected_rows() | ✅ mysql_affected_rows() | ✅ sqlite3_changes() | `db_exec_rows_affected()` |
| **Upsert (INSERT...ON CONFLICT)** | ✅ Native | ⚠️ INSERT...ON DUPLICATE KEY | ⚠️ INSERT...ON DUPLICATE KEY | ⚠️ INSERT OR REPLACE | `sql_upsert_*()` drivers |
| **RETURNING Clause** | ✅ Native | ❌ Not supported | ❌ Not supported | ❌ Not supported | Fallback: SELECT after INSERT/UPDATE |
| **Row-Level Locking (FOR UPDATE)** | ✅ Full | ✅ Full | ✅ Full | ⚠️ Partial | `sql_for_update()` |
| **SKIP LOCKED Hint** | ✅ Full | ❌ Not in 5.7 | ✅ 8.0.1+ | ❌ Not available | Version check + fallback |
| **JSON Build (json_build_object)** | ✅ Native | ⚠️ JSON_OBJECT() | ⚠️ JSON_OBJECT() | ❌ Not available | `sql_json_object()` driver |
| **JSON Array Unnest** | ✅ json_array_elements_text() | ⚠️ JSON_TABLE (5.7.18+) | ✅ JSON_TABLE | ❌ Not available | `sql_json_array_rows()` driver |
| **Timestamp (NOW)** | ✅ NOW() | ⚠️ NOW() (UTC conversion needed) | ⚠️ NOW() (UTC conversion needed) | ⚠️ CURRENT_TIMESTAMP | `sql_now_utc()` driver |
| **Epoch Conversion (EXTRACT)** | ✅ EXTRACT(EPOCH FROM NOW()) | ⚠️ UNIX_TIMESTAMP(NOW()) | ⚠️ UNIX_TIMESTAMP(NOW()) | ⚠️ strftime('%s', 'now') | `sql_epoch_now()` driver |
| **TIMESTAMPTZ Type** | ✅ Native | ❌ TIMESTAMP/DATETIME | ❌ TIMESTAMP/DATETIME | ❌ Not available | Application-enforced UTC |
| **Type Casting (::int)** | ✅ ::int | ✅ CAST(... AS INT) | ✅ CAST(... AS INT) | ✅ CAST(...) | CAST() syntax (portable) |
| **ILIKE (case-insensitive)** | ✅ ILIKE | ⚠️ LIKE (collation) | ⚠️ LIKE (collation) | ⚠️ LIKE (collation) | SQL_FUNCTION (portable LOWER/LIKE) |
| **LIMIT/OFFSET** | ✅ LIMIT $1 OFFSET $2 | ✅ LIMIT $1 OFFSET $2 | ✅ LIMIT $1 OFFSET $2 | ✅ LIMIT $1 OFFSET $2 | Identical across all |
| **FK + Cascades** | ✅ Full | ✅ Full | ✅ Full | ⚠️ Pragma required | Schema/DDL level |
| **CHECK Constraints** | ✅ Full | ⚠️ MySQL 8.0.16+ | ✅ Full | ⚠️ Enforced in app | Schema/DDL level |
| **SERIAL / AUTO_INCREMENT** | ✅ BIGSERIAL | ✅ BIGINT AUTO_INCREMENT | ✅ BIGINT AUTO_INCREMENT | ✅ INTEGER PRIMARY KEY | Schema/DDL level |

---

## B) Current API State + Gaps

### B.1 — What's Already Abstracted (Good)

```c
// db_api.h — Core abstraction layer
db_t *db_open(const db_config_t *cfg, db_error_t *err);
bool db_query(db_t *db, const char *sql, const db_bind_t *params, size_t n_params,
              db_res_t **out_res, db_error_t *err);
bool db_exec(db_t *db, const char *sql, const db_bind_t *params, size_t n_params,
             db_error_t *err);
bool db_exec_rows_affected(db_t *db, const char *sql, const db_bind_t *params,
                           size_t n_params, int64_t *out_rows, db_error_t *err);
bool db_exec_insert_id(db_t *db, const char *sql, const db_bind_t *params,
                       size_t n_params, int64_t *out_id, db_error_t *err);
bool db_tx_begin(db_t *db, db_tx_flags_t flags, db_error_t *err);
bool db_tx_commit(db_t *db, db_error_t *err);
bool db_tx_rollback(db_t *db, db_error_t *err);
```

**Coverage:**
- ✅ Statement preparation (implicit via placeholder translation)
- ✅ Transactions
- ✅ Result iteration (db_res_step, db_res_col_*)
- ✅ Last insert ID (partial: INSERT only, not UPDATE...RETURNING)
- ✅ Affected rows
- ✅ Parameter binding (type-safe db_bind_t union)

### B.2 — What's Partially Abstracted (Needs Expansion)

```c
// src/db/sql_driver.h — SQL template generators (exists but incomplete)
const char *sql_epoch_now(db_t *db);                    // Returns EXTRACT(EPOCH...) or UNIX_TIMESTAMP(...)
const char *sql_now_timestamptz(db_t *db);             // Returns NOW() or NOW() + conversion
const char *sql_epoch_to_timestamptz_fmt(db_t *db);   // Returns to_timestamp($X) template
const char *sql_epoch_param_to_timestamptz(db_t *db); // Returns format string for prepared stmt
const char *sql_insert_ignore_clause(db_t *db);       // Returns ON CONFLICT DO NOTHING or equivalent
const char *sql_conflict_target_fmt(db_t *db);        // Returns ON CONFLICT(%s) DO or equivalent
const char *sql_entity_stock_upsert_epoch_fmt(db_t *db); // Complex upsert template
```

**Current Gaps:**
- ❌ No RETURNING clause abstraction (24 occurrences hardcoded)
- ❌ No JSON_BUILD_OBJECT abstraction (1 occurrence in server_cron.c)
- ❌ No JSON_ARRAY_ELEMENTS_TEXT abstraction (2 occurrences in engine_consumer.c)
- ❌ No FOR UPDATE SKIP LOCKED abstraction (23 occurrences in server_combat.c)
- ⚠️ Partial upsert abstraction (templates exist but UPDATE...RETURNING combos not covered)

### B.3 — What's NOT Abstracted (Worst Cases)

**Hardcoded PostgreSQL SQL in application code:**

| Pattern | Count | File | Severity |
|---------|-------|------|----------|
| `RETURNING <cols>` | 24 | server_planets.c, server_players.c, server_combat.c, server_bank.c, etc. | 🔴 Critical |
| `FOR UPDATE SKIP LOCKED` | 23 | server_combat.c | 🔴 Critical |
| `json_build_object(...)` | 1 | server_cron.c | 🟡 Medium |
| `json_array_elements_text(...)` | 2 | engine_consumer.c | 🟡 Medium |
| `EXCLUDED.<col>` (in ON CONFLICT) | ~10 | server_players.c, db_player_settings.c | 🟡 Medium |
| `EXTRACT(EPOCH FROM NOW())::bigint` | 9 | server_combat.c, server_engine.c | 🟡 Medium (partially via driver) |

**Reproduction Commands:**

```bash
# RETURNING clauses
grep -rn "RETURNING" src/ --include="*.c" --include="*.h" | wc -l  # Output: 24

# ON CONFLICT patterns
grep -rn "ON CONFLICT" src/ --include="*.c" --include="*.h" | wc -l  # Output: 64 (includes driver impl)

# TIMESTAMPTZ in schema
grep -n "timestamptz" sql/pg/000_tables.sql | wc -l  # Output: 81

# JSON builders
grep -rn "json_build_object\|json_agg" src/ --include="*.c" | wc -l  # Output: 1

# EXTRACT(EPOCH) patterns
grep -rn "EXTRACT(EPOCH FROM NOW())" src/ --include="*.c" | wc -l  # Output: 9

# db_exec_insert_id call sites
grep -rn "db_exec_insert_id" src/ --include="*.c" | wc -l  # Output: 16
```

---

## C) Proposed API Additions (Minimal-Change Strategy)

Goal: Add to `src/db/sql_driver.h` and `src/db/sql_driver.c` without touching business logic callsites (except one-line replacements).

### C.1 — Upsert Abstraction (REPLACE ON CONFLICT)

**Current State:** 14 hardcoded ON CONFLICT clauses; no EXCLUDED abstraction

**Proposed Additions:**

```c
/**
 * @file src/db/sql_driver.h
 * @brief SQL driver abstractions for backend-specific SQL patterns
 */

// ============================================================================
// UPSERT / INSERT...ON CONFLICT Abstractions
// ============================================================================

/**
 * @brief Return SQL fragment for "do nothing on conflict".
 * PostgreSQL: "ON CONFLICT DO NOTHING"
 * MySQL:      "ON DUPLICATE KEY UPDATE id=id"  (or similar no-op)
 * @param db Database handle
 * @return Static string; use immediately or copy
 */
const char *sql_upsert_do_nothing(db_t *db);

/**
 * @brief Return SQL fragment for "do update on conflict".
 * PostgreSQL: "ON CONFLICT(%s) DO UPDATE SET %s"
 *   Example: "ON CONFLICT(player_id) DO UPDATE SET credits=EXCLUDED.credits"
 * MySQL:      "ON DUPLICATE KEY UPDATE %s"
 *   Example: "ON DUPLICATE KEY UPDATE credits=VALUES(credits)"
 * @param db Database handle
 * @param conflict_target Column(s) defining uniqueness (e.g., "player_id")
 * @param assignments SET clause assignments (e.g., "credits=$3")
 * @return Formatted SQL fragment (caller owns snprintf into final SQL)
 */
char *sql_upsert_do_update_fmt(db_t *db, const char *conflict_target,
                               const char *assignments, char *out_buf, size_t buf_len);

/**
 * @brief Return value reference in upsert context.
 * PostgreSQL: "EXCLUDED.<col>"  (references row being inserted)
 * MySQL:      "VALUES(<col>)"   (references row being inserted)
 * SQLite:     "excluded.<col>"  (or equivalent)
 * @param db Database handle
 * @param col_name Column name
 * @return Static string representing the "excluded/new value" in upsert
 */
const char *sql_upsert_new_value_ref(db_t *db, const char *col_name);
```

**Usage Example (before → after):**

```c
// BEFORE: Hardcoded PostgreSQL
const char *sql = "INSERT INTO players (player_id, credits) VALUES ($1, $2) "
                  "ON CONFLICT(player_id) DO UPDATE SET credits=EXCLUDED.credits";
db_exec(db, sql, params, 2, &err);

// AFTER: Driver-abstracted
const char *conflict_clause = sql_upsert_do_update_fmt(
    db, "player_id", "credits=$2",  // conflict target and assignments
    buf, sizeof(buf));
snprintf(sql_buf, sizeof(sql_buf),
    "INSERT INTO players (player_id, credits) VALUES ($1, $2) %s",
    conflict_clause);
db_exec(db, sql_buf, params, 2, &err);
```

### C.2 — RETURNING Clause Abstraction

**Current State:** 24 hardcoded RETURNING clauses; no fallback for MySQL

**Proposed Additions:**

```c
/**
 * @brief Execute INSERT/UPDATE and return result columns.
 * 
 * PostgreSQL: Executes SQL with RETURNING, returns result set
 * MySQL:      Executes SQL, then SELECTs affected row(s) by WHERE/PK
 * SQLite:     Similar to MySQL fallback
 * 
 * @param db Database handle
 * @param sql SQL statement (must include RETURNING for PostgreSQL;
 *            ignored on MySQL; typically: "INSERT INTO t(...) VALUES(...) RETURNING id")
 * @param params Parameter bindings
 * @param n_params Number of parameters
 * @param out_res Pointer to store result set handle (caller must finalize)
 * @param err Error structure
 * @return true on success (out_res filled), false on failure
 * 
 * CONTRACT: On MySQL, the result set must be reconstructed via SELECT;
 *           caller must not assume RETURNING was executed literally.
 */
bool db_exec_returning(db_t *db,
                       const char *sql,
                       const db_bind_t *params,
                       size_t n_params,
                       db_res_t **out_res,
                       db_error_t *err);

/**
 * @brief Helper: Determine if RETURNING is supported natively.
 * @param db Database handle
 * @return true if db_exec_returning uses native RETURNING (PostgreSQL)
 *         false if db_exec_returning uses SELECT fallback (MySQL, SQLite)
 */
bool db_backend_supports_returning(db_t *db);
```

**Usage Example (before → after):**

```c
// BEFORE: Hardcoded RETURNING; breaks on MySQL
const char *sql = "INSERT INTO planets (name, owner_id) VALUES ($1, $2) RETURNING planet_id";
db_res_t *res;
db_query(db, sql, params, 2, &res, &err);
while (db_res_step(res, &err)) {
    int64_t planet_id = db_res_col_i64(res, 0, &err);
}
db_res_finalize(res);

// AFTER: Driver-abstracted
const char *sql = "INSERT INTO planets (name, owner_id) VALUES ($1, $2) RETURNING planet_id";
db_res_t *res;
db_exec_returning(db, sql, params, 2, &res, &err);  // Works on both backends
if (res) {
    while (db_res_step(res, &err)) {
        int64_t planet_id = db_res_col_i64(res, 0, &err);
    }
    db_res_finalize(res);
}
```

### C.3 — FOR UPDATE SKIP LOCKED Abstraction

**Current State:** 23 hardcoded `FOR UPDATE SKIP LOCKED` in server_combat.c; fails on MySQL <8.0.1

**Proposed Additions:**

```c
/**
 * @brief Return SQL fragment for "SELECT ... FOR UPDATE [SKIP LOCKED]".
 * 
 * PostgreSQL:  "FOR UPDATE SKIP LOCKED"
 * MySQL 8.0.1+: "FOR UPDATE SKIP LOCKED"
 * MySQL <8.0.1: "FOR UPDATE"  (caller responsible for retry logic on lock timeout)
 * SQLite:      "FOR UPDATE"   (SQLite has limited locking; best effort)
 * 
 * @param db Database handle
 * @return Static string; use immediately or copy
 */
const char *sql_for_update_skip_locked(db_t *db);

/**
 * @brief Check if backend supports SKIP LOCKED natively (non-blocking variant).
 * @param db Database handle
 * @return true if SKIP LOCKED is available (PostgreSQL, MySQL 8.0.1+)
 *         false if fallback to FOR UPDATE required (MySQL <8.0.1, SQLite)
 * 
 * Callers should use this to implement exponential backoff retry logic
 * when SKIP LOCKED is unavailable.
 */
bool db_backend_supports_skip_locked(db_t *db);
```

**Usage Example (before → after):**

```c
// BEFORE: Hardcoded PostgreSQL; breaks on MySQL <8.0.1
const char *sql = "SELECT sector_assets_id FROM sector_assets WHERE asset_location=$1 "
                  "FOR UPDATE SKIP LOCKED LIMIT 1";
db_query(db, sql, params, 1, &res, &err);

// AFTER: Driver-abstracted with fallback
const char *lock_hint = sql_for_update_skip_locked(db);
char sql_buf[512];
snprintf(sql_buf, sizeof(sql_buf),
    "SELECT sector_assets_id FROM sector_assets WHERE asset_location=$1 "
    "%s LIMIT 1", lock_hint);

int retries = 0;
while (retries < 3) {
    if (db_query(db, sql_buf, params, 1, &res, &err)) {
        break;  // Success
    }
    if (db_error_is_lock_timeout(&err)) {
        if (!db_backend_supports_skip_locked(db)) {
            usleep(100000 << retries);  // Exponential backoff
            retries++;
            continue;
        }
    }
    // Real error; bail out
    return false;
}
```

### C.4 — JSON Abstraction

**Current State:** 3 hardcoded JSON operations (1x json_build_object, 2x json_array_elements_text)

**Proposed Additions:**

```c
/**
 * @brief Build a JSON object literal.
 * 
 * PostgreSQL: json_build_object('key1', $1, 'key2', $2, ...)
 * MySQL:      JSON_OBJECT('key1', $1, 'key2', $2, ...)
 * 
 * @param db Database handle
 * @param out_buf Output buffer for SQL fragment
 * @param buf_len Buffer size
 * @param ... Alternating key-value pairs (const char *key, const char *param_ref)
 *            Caller provides parameter placeholders, e.g., "$1", "$2"
 * @return Pointer to out_buf on success, NULL on buffer overflow
 * 
 * Example:
 *   char buf[256];
 *   sql_json_object(db, buf, sizeof(buf),
 *                   "trap_id", "$1",
 *                   "message", "$2",
 *                   NULL);  // Sentinel
 *   // Result: "json_build_object('trap_id', $1, 'message', $2)"  (PostgreSQL)
 *   //      or "JSON_OBJECT('trap_id', $1, 'message', $2)"        (MySQL)
 */
char *sql_json_object(db_t *db, char *out_buf, size_t buf_len, ...);

/**
 * @brief Unnest a JSON array into rows.
 * 
 * PostgreSQL: "CROSS JOIN json_array_elements_text($1::json) AS elem"
 * MySQL:      "CROSS JOIN JSON_TABLE($1, '$[*]' COLUMNS (elem VARCHAR(255)) AS jt"
 * 
 * Usage in WHERE clause:
 *   SELECT ... FROM my_table t
 *   CROSS JOIN (SELECT elem FROM json_array_elements_text($1::json)) AS arr(elem)
 *   WHERE t.type IN (arr.elem)
 * 
 * @param db Database handle
 * @param param_ref Parameter placeholder (e.g., "$1")
 * @param out_buf Output buffer for SQL fragment
 * @param buf_len Buffer size
 * @return Pointer to out_buf on success, NULL on buffer overflow
 */
char *sql_json_array_rows(db_t *db, const char *param_ref,
                          char *out_buf, size_t buf_len);
```

### C.5 — Timestamp/Timezone Abstraction (Enhanced)

**Current State:** Partially abstracted (sql_epoch_now, sql_now_timestamptz); still has `EXTRACT(EPOCH FROM NOW())::bigint` hardcoded in 9 places

**Proposed Additions:**

```c
/**
 * @brief Return SQL expression for current time in UTC.
 * 
 * PostgreSQL: "NOW() AT TIME ZONE 'UTC'"
 * MySQL:      "CONVERT_TZ(NOW(), @@session.time_zone, '+00:00')"
 * SQLite:     "datetime('now', 'utc')"
 * 
 * Use when storing timestamps in DATETIME/TIMESTAMPTZ columns that represent UTC.
 * 
 * @param db Database handle
 * @return Static string; use immediately or copy
 */
const char *sql_now_utc(db_t *db);

/**
 * @brief Return SQL expression for current epoch (seconds since 1970-01-01 UTC).
 * 
 * PostgreSQL: "EXTRACT(EPOCH FROM NOW())::bigint"
 * MySQL:      "UNIX_TIMESTAMP(NOW())"
 * SQLite:     "strftime('%s', 'now')"
 * 
 * @param db Database handle
 * @return Static string; use immediately or copy
 */
const char *sql_epoch_now_utc(db_t *db);

/**
 * @brief Return SQL fragment for converting epoch parameter to TIMESTAMPTZ.
 * 
 * PostgreSQL: "to_timestamp($%d)"  (caller fills in $1, $2, etc.)
 * MySQL:      "FROM_UNIXTIME($%d)" (same parameter reference)
 * SQLite:     "datetime($%d, 'unixepoch')"
 * 
 * @param db Database handle
 * @param param_index 1-based parameter index (1, 2, 3, ...)
 * @param out_buf Output buffer
 * @param buf_len Buffer size
 * @return Pointer to out_buf on success
 */
char *sql_epoch_to_timestamptz(db_t *db, int param_index,
                               char *out_buf, size_t buf_len);
```

### C.6 — Error Code Classification

**Current State:** Generic error codes (ERR_DB_INTERNAL, etc.); no constraint/deadlock/FK classification

**Proposed Additions:**

```c
/**
 * @brief Backend-agnostic error categories.
 */
typedef enum {
  DB_ERR_CAT_UNKNOWN = 0,
  DB_ERR_CAT_CONSTRAINT,      // Unique constraint, check constraint violation
  DB_ERR_CAT_FK,              // Foreign key constraint violation
  DB_ERR_CAT_DEADLOCK,        // Transaction deadlock or cycle detected
  DB_ERR_CAT_LOCK_TIMEOUT,    // Lock wait timeout (may retry)
  DB_ERR_CAT_SERIALIZATION,   // Serialization conflict / isolation violation
  DB_ERR_CAT_NOT_FOUND,       // Resource not found (no rows affected)
  DB_ERR_CAT_IO,              // Disk I/O error
  DB_ERR_CAT_CONNECTION,      // Connection lost or refused
} db_error_category_t;

/**
 * @brief Map database error to backend-agnostic category.
 * 
 * Allows callers to branch on semantic error type without knowing backend codes.
 * 
 * @param err Error structure populated by db_* function
 * @return Error category
 */
db_error_category_t db_error_classify(const db_error_t *err);

/**
 * @brief Helper: Is this a constraint violation (unique/check)?
 * @param err Error structure
 * @return true if constraint violation, false otherwise
 */
static inline bool db_error_is_constraint(const db_error_t *err) {
  return db_error_classify(err) == DB_ERR_CAT_CONSTRAINT;
}

/**
 * @brief Helper: Is this a deadlock?
 * @param err Error structure
 * @return true if deadlock, false otherwise
 */
static inline bool db_error_is_deadlock(const db_error_t *err) {
  return db_error_classify(err) == DB_ERR_CAT_DEADLOCK;
}

/**
 * @brief Helper: Is this a lock timeout (retryable)?
 * @param err Error structure
 * @return true if lock timeout, false otherwise
 */
static inline bool db_error_is_lock_timeout(const db_error_t *err) {
  return db_error_classify(err) == DB_ERR_CAT_LOCK_TIMEOUT;
}
```

---

## D) Minimal-Change Migration Strategy

**Goal:** Add driver abstractions without touching business logic (except 1-line replacements).

### Phase 1: Add Driver Functions (No Callsite Changes)

**Timeline:** 1 week

**Work:**
1. Extend `src/db/sql_driver.h` with C.1–C.6 signatures
2. Implement in `src/db/sql_driver.c` (PostgreSQL branch, MySQL stubs)
3. Implement in `src/db/pg/db_pg.c` (PostgreSQL-specific logic)
4. Add unit tests for each driver function (verify output format)
5. Build + sentinel checks (no functional change)

**Verification:**
```bash
make -j4 && ./tools/sql_sentinels.sh
# Expected: All 4 checks PASS
```

### Phase 2: Migrate One High-Impact Pattern (PR per pattern)

**PR-M1: Upsert Abstraction**
- Refactor 14 ON CONFLICT clauses → driver calls
- 5 files: server_citadel.c, db_player_settings.c, server_players.c, server_communication.c, bigbang_pg_main.c
- Impact: ~5 lines per file (snprintf templates)
- Verification: Unit test upserts, manual player/corp/citadel testing

**PR-M2: RETURNING Clause Abstraction**
- Refactor 24 RETURNING clauses → `db_exec_returning()`
- 7 files: server_planets.c, server_players.c, server_combat.c, server_bank.c, etc.
- Impact: Replace `db_query()` with `db_exec_returning()`; result iteration unchanged
- Verification: Verify generated IDs, UPDATE return values

**PR-M3: FOR UPDATE SKIP LOCKED**
- Replace 23 hardcoded `FOR UPDATE SKIP LOCKED` → driver call
- 1 file: server_combat.c
- Impact: ~2 lines per usage (snprintf + lock_hint)
- Verification: Combat lock contention test, verify no lock timeouts

**PR-M4: JSON + Timestamp Helpers**
- Replace 3 JSON + 9 EXTRACT patterns → driver calls
- 3 files: server_cron.c, engine_consumer.c, server_engine.c, server_combat.c
- Impact: ~1 line per usage (snprintf)
- Verification: Cron task execution, JSON event creation

**PR-M5: Error Code Mapping**
- Implement `db_error_classify()` in driver layer
- Refactor callers checking specific error codes
- 5–10 files (low priority; can be deferred)
- Impact: Replace hardcoded error checks with semantic helpers

### Phase 3: Implement MySQL Backend (db_mysql.c)

**Timeline:** 2 weeks (after Phase 1 + Phase 2)

**Work:**
1. Create `src/db/mysql/db_mysql.c` (libmysqlclient or Connector/C)
2. Implement core db_* functions (open, query, exec, tx_*)
3. Implement all driver functions (sql_*) with MySQL equivalents
4. RETURNING fallback logic (SELECT after INSERT/UPDATE)
5. Error mapping (MySQL codes → DB_ERR_CAT_*)
6. Build + test (MySQL 5.7 + 8.0)

**Verification:**
```bash
# Set DB_BACKEND=MYSQL in config
make -j4 && ./tools/sql_sentinels.sh
# All tests pass on MySQL 5.7 and 8.0+
```

---

## E) API Stability & Versioning

**Contract guarantees:**

1. **Driver functions are backend-agnostic:** All `sql_*()` helpers return static strings or format strings; never execute SQL directly
2. **No breaking changes to db_api.h:** Additions only; never remove/modify existing functions
3. **Backward compatibility:** Existing PostgreSQL code continues to work without modification
4. **Fallback semantics documented:** Every driver function documents what happens on unsupported backends

**Example versioning scheme (optional):**

```c
#define DB_DRIVER_API_VERSION 2  // Increment on breaking changes

typedef struct {
  int api_version;
  // ... function pointers for driver v-table
} db_driver_vtable_t;
```

---

## F) SQL Reproducibility Audit Commands

Every claim in this review is reproducible via these exact commands:

### Hardcoded PostgreSQL Patterns

```bash
# 1. RETURNING clauses (24 total)
grep -rn "RETURNING" src/ --include="*.c" --include="*.h" | wc -l
# Output: 24

# 2. FOR UPDATE SKIP LOCKED (23 in server_combat.c)
grep -rn "FOR UPDATE SKIP LOCKED" src/ --include="*.c" | wc -l
# Output: 23

# 3. ON CONFLICT patterns (14 real upserts; 64 total with driver impl)
grep -rn "ON CONFLICT" src/ --include="*.c" --include="*.h" | grep -v "sql_driver" | wc -l
# Output: 14

# 4. json_build_object (1 occurrence)
grep -rn "json_build_object" src/ --include="*.c" | wc -l
# Output: 1

# 5. json_array_elements_text (2 occurrences)
grep -rn "json_array_elements_text" src/ --include="*.c" | wc -l
# Output: 2

# 6. EXTRACT(EPOCH FROM NOW()) (9 hardcoded, rest via driver)
grep -rn "EXTRACT(EPOCH FROM NOW())" src/ --include="*.c" | wc -l
# Output: 9

# 7. EXCLUDED references in ON CONFLICT (10 occurrences)
grep -rn "EXCLUDED\." src/ --include="*.c" | wc -l
# Output: 10
```

### Schema Analysis

```bash
# TIMESTAMPTZ columns (81 total)
grep -n "timestamptz" sql/pg/000_tables.sql | wc -l
# Output: 81

# BIGSERIAL usage (11 total)
grep -n "bigserial" sql/pg/000_tables.sql | wc -l
# Output: 11

# FK references (118 total)
grep -c "REFERENCES" sql/pg/000_tables.sql
# Output: 118
```

### Driver API Usage

```bash
# db_exec_insert_id call sites (16 uses)
grep -rn "db_exec_insert_id" src/ --include="*.c" | wc -l
# Output: 16

# db_query/db_exec calls (hundreds, baseline)
grep -rn "db_query\|db_exec" src/ --include="*.c" | grep -v "//" | wc -l
# Output: ~500+
```

---

## G) Conclusion & Recommendations

### Summary

| Component | Status | Effort | Risk | Blocker? |
|-----------|--------|--------|------|----------|
| **Prepared Statements** | ✅ Abstracted | 0 | None | ❌ No |
| **Transactions** | ✅ Abstracted | 0 | None | ❌ No |
| **Upsert (ON CONFLICT)** | ⚠️ Partial | Low | Low | ⚠️ Yes (14 sites) |
| **RETURNING Clause** | ❌ Not abstracted | High | High | 🔴 Yes (24 sites) |
| **Row Locking (SKIP LOCKED)** | ❌ Not abstracted | Medium | Medium | 🔴 Yes (23 sites) |
| **JSON Operations** | ❌ Not abstracted | Low | Low | ⚠️ Yes (3 sites) |
| **Timestamp/Epoch** | ⚠️ Partial | Low | Low | ⚠️ Yes (9 EXTRACT) |
| **Error Mapping** | ❌ Not abstracted | Low | Low | ⚠️ Infrastructure |

### Recommendations

1. **Immediate (Phase 1):** Add all driver functions to sql_driver.h/c (no callsite changes)
2. **Short-term (Phase 2):** Migrate patterns in order (Upsert → RETURNING → SKIP LOCKED → JSON → Error codes)
3. **Medium-term (Phase 3):** Implement db_mysql.c using completed abstractions
4. **Long-term:** Consider db_sqlite.c for fallback backend (lower priority)

### Success Criteria

- ✅ All sentinel checks PASS
- ✅ All tests pass on PostgreSQL + MySQL 5.7 + MySQL 8.0+
- ✅ No hardcoded PostgreSQL SQL in business logic (server_*.c, engine_*.c)
- ✅ All driver functions documented + tested
- ✅ Build succeeds with no new warnings
