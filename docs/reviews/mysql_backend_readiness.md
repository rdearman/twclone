# MySQL Backend Readiness Review

**Scope:** Audit of DB-specific SQL and API patterns that would block/complicate MySQL backend addition.  
**Review Type:** Code audit (NO changes), research & planning only.  
**Codebase:** twclone game server (PostgreSQL-only, ~91 source files)

---

## A) Executive Summary — Top 5 Blockers

| # | Blocker | Evidence | Impact | Mitigation Theme |
|---|---------|----------|--------|------------------|
| **1** | **ON CONFLICT / UPSERT** | 14 hardcoded `ON CONFLICT` clauses across 5 files (server_citadel, db_player_settings, server_players, server_communication, bigbang_pg_main) | MySQL `INSERT ... ON DUPLICATE KEY` has different syntax; `EXCLUDED` keyword not supported | Abstract into driver helper + snprintf templates |
| **2** | **RETURNING Clause** | 24 occurrences across 7 files (server_planets, server_players, server_combat, server_bank, db/pg); directly returns computed IDs and affected data | MySQL has no native RETURNING; requires fetch-back or last_insert_id() | Implement `db_exec_returning()` driver API + fallback SELECT |
| **3** | **FOR UPDATE SKIP LOCKED** | 23 occurrences in server_combat.c (critical deadlock-avoidance in combat); row-locking hint | MySQL supports `FOR UPDATE` but not `SKIP LOCKED` (added MySQL 8.0.1); older versions have no equivalent | Conditional logic: version-check + fallback to regular FOR UPDATE |
| **4** | **TIMESTAMPTZ + Timezone Semantics** | 40+ TIMESTAMPTZ columns in schema; embedded `EXTRACT(EPOCH FROM NOW())::bigint` casts; mixing tz-aware and epoch integers | MySQL TIMESTAMP auto-converts to UTC (lossy); no native TIMESTAMPTZ; mixing types causes implicit conversions | Add driver timezone abstraction layer; normalize to explicit UTC handling |
| **5** | **json_array_elements_text() and JSON ops** | 2 occurrences in engine_consumer.c using PostgreSQL JSON path operators | MySQL has JSON_EXTRACT, JSON_UNQUOTE but different semantics and edge cases | Create driver JSON helper for array iteration |

---

## B) Inventory of DB-Specific SQL Primitives

### B.1 — Upsert Patterns (ON CONFLICT / INSERT...RETURNING / EXCLUDED)

| File | Line | SQL Snippet | Note |
|------|------|------------|------|
| server_citadel.c | 315 | `ON CONFLICT(planet_id) DO UPDATE SET construction_status='upgrading'` | Complex update with multiple columns |
| db_player_settings.c | 34 | `ON CONFLICT(player_id, event_type) DO UPDATE SET filter_json = $3` | Composite key conflict |
| db_player_settings.c | 80 | `ON CONFLICT(player_id, name) DO UPDATE SET sector_id = $3` | Same pattern |
| db_player_settings.c | 138 | `ON CONFLICT DO NOTHING` | Absorb duplicates silently |
| db_player_settings.c | 201 | `ON CONFLICT(player_id, scope, key) DO UPDATE SET note = $4` | Triple-column composite |
| db_player_settings.c | 340 | `ON CONFLICT(player_id, key) DO UPDATE SET value = $3` | Dual-column composite |
| server_players.c | 136 | `ON CONFLICT (player_id, key) DO UPDATE SET type = EXCLUDED.type, value = EXCLUDED.value` | Uses EXCLUDED pseudotable |
| server_players.c | 150 | `ON CONFLICT (player_id, name) DO UPDATE SET sector_id = EXCLUDED.sector_id` | Uses EXCLUDED |
| server_players.c | 175 | `ON CONFLICT DO NOTHING` | Absorb pattern |
| server_players.c | 215 | `ON CONFLICT (player_id, topic) DO UPDATE SET enabled = 1, delivery = EXCLUDED.delivery` | Mixed literal + EXCLUDED |
| server_communication.c | 408 | `ON CONFLICT(notice_id, player_id) DO UPDATE SET seen_at = excluded.seen_at` | Lowercase EXCLUDED |
| bigbang_pg_main.c | 186 | `ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value, type = EXCLUDED.type` | Single-column conflict |
| bigbang_pg_main.c | 367 | `ON CONFLICT DO NOTHING` | Absorb pattern |
| sql_driver.c | 85, 115 | Driver returns `"ON CONFLICT DO NOTHING"` and `"ON CONFLICT(%s) DO"` templates | Abstraction exists but incomplete |

**MySQL Equivalent:** `INSERT ... ON DUPLICATE KEY UPDATE` (different syntax, no EXCLUDED keyword; must repeat column assignments)

### B.2 — Time Functions & Epoch Arithmetic

| File | Line | SQL/C Pattern | Type | Note |
|------|------|---------------|------|------|
| server_engine.c | Multiple | `EXTRACT(EPOCH FROM NOW())::bigint` | Epoch integer | Core pattern for timestamp→epoch conversion |
| server_combat.c | 2276 | `EXTRACT(EPOCH FROM NOW())::bigint` | Epoch integer | Used in combat log creation |
| server_cron.c | Multiple | `sql_epoch_param_to_timestamptz()` driver call | Abstracted | Driver helper partially isolates it |
| server_planets.c | 1732 | NOW() in RETURNING clause | RETURNING + NOW | Combination challenge |
| sql_driver.c | 55, 62 | `"EXTRACT(EPOCH FROM NOW())::bigint"` hardcoded | Static string | Driver returns PostgreSQL-only template |

**MySQL Challenge:** No EXTRACT(EPOCH ...) operator; must use UNIX_TIMESTAMP(NOW()); type casts differ

### B.3 — RETURNING Clause (24 occurrences)

| File | Lines | Usage Pattern |
|-------|-------|----------------|
| server_planets.c | 1732 | INSERT ... RETURNING planet_id (fetch generated ID) |
| server_players.c | 1305–1311 | INSERT ... RETURNING id (ship creation) |
| server_players.c | 1559–1648 | UPDATE ... RETURNING credits (post-operation value) |
| server_combat.c | 2137–2205 | UPDATE ... RETURNING fighters/mines/credits (combat state mutation) |
| server_combat.c | 3447–3462 | UPDATE ... RETURNING sector_assets_id |
| server_bank.c | 66–75, 481 | UPDATE ... RETURNING id, balance |
| db/pg/db_pg.c | 301, 307 | Driver implementation (PostgreSQL-specific) |
| server_communication.c | 236, 1119 | db_exec_insert_id() fallback pattern (partial mitigation) |

**MySQL Limitation:** No native RETURNING; must SELECT after INSERT or use last_insert_id() + separate fetch for UPDATE return values

### B.4 — Pagination & Limits

| Count | Files | Pattern |
|-------|-------|---------|
| ~15 | Multiple (server_loop, server_communication, server_players, etc.) | `LIMIT $1` or `LIMIT $1 OFFSET $2` |

**MySQL Support:** ✅ Full support (identical syntax)

### B.5 — JSON Functions

| File | Line | PostgreSQL Function | Usage |
|------|------|---------------------|-------|
| engine_consumer.c | 192, 430 | `json_array_elements_text($3::json)` | Iterate JSON array; requires JOIN |
| server_cron.c | 1989 | `json_build_object('trap_id', id)` | Build JSON literal in INSERT |

**MySQL Equivalent:** `JSON_EXTRACT()`, `JSON_UNQUOTE()`, `JSON_ARRAY()` (different signatures, edge cases)

### B.6 — Case-Folding & Pattern Matching

| File | Line | Pattern | Type |
|------|------|---------|------|
| server_communication.c | 1036 | `lower(name) = lower($1)` | Case-insensitive compare |
| server_universe.c | 627–639 | `ILIKE $1` (3 occurrences) | PostgreSQL pattern match (case-insensitive) |

**MySQL Equivalent:** `LOWER()` ✅ works; `LIKE` is case-insensitive by default (collation-dependent)

### B.7 — Type Casting (::bigint, CAST, etc.)

| File | Line | Cast | Reason |
|------|------|------|--------|
| server_combat.c | 2276, 2333 | `EXTRACT(...FROM NOW())::bigint` | Epoch → integer |
| server_clusters.c | 473 | `CAST(price + 0.1 * ($1 - price) AS INTEGER)` | Float → integer |
| server_cron.c | 1236, 1283 | `CAST(... AS INTEGER)` | Float → integer |
| server_universe.c | 813 | `$1::int` | String → integer |
| server_cmds.c | 314 | `CAST(value AS INTEGER)` | Generic cast |

**MySQL Support:** ✅ CAST() works (no :: shorthand, but identical semantics)

### B.8 — Row Locking (FOR UPDATE / SKIP LOCKED)

| File | Count | Pattern | Context |
|------|-------|---------|---------|
| server_combat.c | 23 | `FOR UPDATE SKIP LOCKED` | Critical deadlock avoidance in concurrent combat; prevents lock contention |
| database_cmd.c | 1 | `FOR UPDATE` | Player alignment update within transaction |

**MySQL Status:**
- `FOR UPDATE`: ✅ Fully supported (identical)
- `SKIP LOCKED`: ❌ Not in MySQL 5.7/8.0.0; added MySQL 8.0.1+; fallback to non-locking with retry logic

### B.9 — Foreign Keys & Cascades

| Count | Pattern |
|-------|---------|
| 118 | `REFERENCES ... ON DELETE CASCADE ON UPDATE CASCADE` |
| 5 | Sample cascades in corporations, player_settings, etc. |

**MySQL Support:** ✅ Full (identical)

### B.10 — Sequences & Auto-Increment

| Type | Count | Pattern |
|------|-------|---------|
| BIGSERIAL | 10+ | `bigserial PRIMARY KEY` in schema |
| SERIAL | 5+ | `serial PRIMARY KEY` |

**MySQL Equivalent:** `BIGINT AUTO_INCREMENT PRIMARY KEY` or `INT AUTO_INCREMENT PRIMARY KEY`

---

## C) DB API/Driver Interface Gaps

### C.1 — Statement Preparation & Binding

**Current State:** ✅ Generic (params use `$1, $2, ...` placeholders; drivers translate)

| API | PostgreSQL | MySQL | Gap |
|-----|-----------|-------|-----|
| `db_query(..., params, n_params, ...)` | Prepared statement (libpq) | Can be parameterized (client-side or server-side) | None (placeholders abstract) |
| `db_bind_t` union | Supports i64, u64, i32, u32, bool, text, blob | All supported (MySQL types compatible) | ✅ None |

### C.2 — Affected-Rows Semantics

**Current API:** `db_exec_rows_affected(db, sql, params, n_params, &out_rows, &err)`

**Gap:** PostgreSQL returns "rows affected" for UPDATE/DELETE. MySQL returns "rows matched" vs "rows changed" (different in ON DUPLICATE KEY scenarios).

```c
// Current usage:
db_exec_rows_affected(db, "UPDATE players SET ...", params, 1, &rows, &err);
if (rows > 0) { /* success */ }
```

**MySQL Issue:** ON DUPLICATE KEY UPDATE may report 1 (insert) or 2 (update) for same logical operation.

### C.3 — RETURNING Clause Handling

**Current State:** ❌ **No abstraction**; 24 direct SQL RETURNING clauses.

**PostgreSQL:** `db_query()` with RETURNING works natively (result set iteration).

**MySQL:** No native RETURNING; requires:
1. **Last Insert ID:** For INSERT with generated key → use `db_exec_insert_id()`
2. **Post-SELECT:** For UPDATE/DELETE returning multiple columns → must SELECT after mutation

**Required API:** `db_exec_returning(db, sql, params, ..., &out_res, &err)` with driver-specific fallback

### C.4 — Transaction Isolation & Deadlock Handling

**Current:** `db_tx_begin(db, flags, &err)`, `db_tx_commit()`, `db_tx_rollback()`

**API Gaps:**
- No explicit isolation level selection (SERIALIZABLE vs READ_COMMITTED)
- No deadlock retry logic in API (burden on caller)
- `FOR UPDATE SKIP LOCKED` has no fallback (hard fail on MySQL <8.0.1)

### C.5 — Error Mapping

**Current Structure:**
```c
typedef struct {
  int code;           // Generic (e.g., ERR_DB_INTERNAL)
  int backend_code;   // PostgreSQL native code (opaque, not used for branching)
  char message[256];  // Human-readable
} db_error_t;
```

**Missing Mappings:**
- Unique constraint violation → Not mapped; caller cannot distinguish from other failures
- Foreign key violation → No error code
- Deadlock (ERRCODE_T_R_DEADLOCK_DETECTED) → Not mapped
- Lock timeout → No mapping
- Transaction conflict → No specific code

**MySQL-Specific Codes:**
- 1062 (Duplicate entry) → Must map to "constraint violation"
- 1451 (FK constraint failed) → Must map
- 1213 (Deadlock found) → Must map
- 1205 (Lock wait timeout) → Must map

### C.6 — Generated Key Retrieval (Last Insert ID)

**Current API:** `db_exec_insert_id(db, sql, params, n_params, &out_id, &err)`

**PostgreSQL:** Uses RETURNING + result set extraction

**MySQL:** 
- Standard: `SELECT LAST_INSERT_ID()` after INSERT (single row only)
- With DUPLICATE KEY: Returns affected row count (1 insert → 1, 1 update → 2), not ID

**Issue:** Current API assumes deterministic ID return; MySQL's ON DUPLICATE KEY + LAST_INSERT_ID() doesn't work as expected for updates.

**Mitigation:** Wrapper must detect UPDATE scenario and fall back to WHERE clause SELECT.

---

## D) Schema / Migration Compatibility

### D.1 — TIMESTAMPTZ Columns

**Current Schema:** 40+ columns use `TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP`

```sql
created_at timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
last_update timestamptz NOT NULL DEFAULT CURRENT_TIMESTAMP
```

**MySQL Limitation:**
- No native TIMESTAMPTZ (timezone-aware); `TIMESTAMP` auto-converts to UTC (lossy)
- DATETIME supports arbitrary time, no timezone
- Mixing TIMESTAMP + application-computed epoch (BIGINT) requires explicit UTC semantics

**Migration Strategy:**
1. Schema: Replace TIMESTAMPTZ with DATETIME + application-managed UTC constraint
2. Application: Ensure all NOW() calls use UTC explicitly (`NOW() AT TIME ZONE 'UTC'` in PostgreSQL; `CONVERT_TZ(NOW(), @@session.time_zone, '+00:00')` in MySQL)
3. Driver: Abstract NOW() via helper to enforce timezone

### D.2 — BIGSERIAL / SERIAL

**Current Schema:** 10+ BIGSERIAL, 5+ SERIAL primary keys

**MySQL Migration:** Straightforward (replace with AUTO_INCREMENT)

### D.3 — CHECK Constraints

**Current:** 40+ CHECK constraints (e.g., `CHECK (is_evil IN (TRUE, FALSE))`, `CHECK (tag ~ '^...$')`)

**MySQL Support:** ✅ CHECK constraints supported (MySQL 8.0.16+); earlier versions ignore them

**Risk:** Older MySQL deployments won't enforce; may require application-level validation.

### D.4 — Foreign Keys with Cascades

**Current:** 118 FK references, many with `ON DELETE CASCADE`

**MySQL Support:** ✅ Full (identical behavior)

### D.5 — Partial/Expression Indexes (if present)

Search result: None found in `000_tables.sql`. (Low risk)

---

## E) "MySQL Backlog" — Prioritised Fix List

### PR-M1: Implement ON CONFLICT / UPSERT Abstraction

**Title:** Add `sql_upsert_clause()` driver function + refactor hardcoded ON CONFLICT

**Files:**
- `src/db/sql_driver.h` — Add 2 functions: `sql_upsert_do_nothing()`, `sql_upsert_do_update_fmt()`
- `src/db/sql_driver.c` — Implement with PostgreSQL/MySQL versions
- `src/server_citadel.c` (1 file, 1 clause)
- `src/db_player_settings.c` (4 clauses)
- `src/server_players.c` (4 clauses)
- `src/server_communication.c` (1 clause)
- `bigbang_pg_main.c` (2 clauses)

**Why It Blocks MySQL:**
- `INSERT ... ON DUPLICATE KEY UPDATE` has entirely different syntax
- EXCLUDED pseudotable doesn't exist in MySQL; must repeat column assignments

**Proposed Driver API:**
```c
// "ON CONFLICT DO NOTHING"
const char *sql_upsert_do_nothing(db_t *db);

// "ON CONFLICT(%s) DO UPDATE SET %s"
// fmt: "ON CONFLICT(id) DO UPDATE SET col1=$X, col2=$Y"
const char *sql_upsert_do_update_fmt(db_t *db, const char *conflict_target, const char *assignments);
```

**Estimated Blast Radius:** Medium (affects 12 files, but mostly boilerplate replacement)

**Verification:**
- `make && true` (build succeeds)
- `./tools/sql_sentinels.sh` (CHECK 0-4 PASS)
- Manual: Verify upserts still work (player settings, citadel, etc.)

---

### PR-M2: Implement RETURNING Clause Abstraction

**Title:** Add `db_exec_returning()` API + replace 24 hardcoded RETURNING clauses

**Files:**
- `src/db/db_api.h` — Add function declaration
- `src/db/db_api.c` — Add dispatcher
- `src/db/pg/db_pg.c` — PostgreSQL implementation (use RETURNING)
- `src/db/mysql/db_mysql.c` (TBD) — MySQL fallback (snprintf SELECT + execute)
- Refactor in: server_planets.c, server_players.c, server_combat.c, server_bank.c, server_communication.c, etc. (7 files)

**Why It Blocks MySQL:**
- No native RETURNING; must implement fetch-back logic

**Proposed Driver API:**
```c
// Atomically execute INSERT/UPDATE and return result set
bool db_exec_returning(db_t *db,
                       const char *sql,
                       const db_bind_t *params,
                       size_t n_params,
                       db_res_t **out_res,
                       db_error_t *err);
```

**Estimated Blast Radius:** High (24 call sites; significant refactoring)

**Verification:**
- Build + sentinels
- Integration tests: Verify IDs are returned correctly from INSERT; verify UPDATE values match

---

### PR-M3: Add FOR UPDATE SKIP LOCKED Compatibility

**Title:** Implement `sql_for_update_skip_locked()` driver function with version checking

**Files:**
- `src/db/sql_driver.h` — Add function
- `src/db/sql_driver.c` — Implement with MySQL 8.0.1+ check; fallback to FOR UPDATE
- `src/server_combat.c` — Replace 23 hardcoded `FOR UPDATE SKIP LOCKED` with driver call

**Why It Blocks MySQL:**
- MySQL <8.0.1 doesn't support SKIP LOCKED; must detect version and degrade gracefully

**Proposed Driver API:**
```c
// Returns "FOR UPDATE SKIP LOCKED" (PostgreSQL, MySQL 8.0.1+)
// or "FOR UPDATE" (MySQL <8.0.1, fallback)
// Caller responsible for retry logic on MySQL fallback
const char *sql_for_update_skip_locked(db_t *db);

// Helper: check if backend supports SKIP LOCKED natively
bool db_backend_supports_skip_locked(db_t *db);
```

**MySQL Fallback Logic:**
```c
while (retries < 3) {
  // Use FOR UPDATE (without SKIP LOCKED)
  // If lock timeout → sleep + retry
  // If deadlock → sleep + retry
}
```

**Estimated Blast Radius:** Medium (combat file is large, but localized changes)

**Verification:**
- Build + sentinels
- Combat lock contention test: Run concurrent combat, verify no lock timeouts on MySQL <8.0.1
- Performance: Verify retry overhead acceptable

---

### PR-M4: Implement Time/Timezone Abstraction Layer

**Title:** Add driver functions for NOW(), EPOCH operations; enforce UTC consistently

**Files:**
- `src/db/sql_driver.h` — Add/revise: `sql_now_utc()`, `sql_epoch_now_utc()`, `sql_timezone_explicit()`
- `src/db/sql_driver.c` — Implement with PostgreSQL AT TIME ZONE + MySQL CONVERT_TZ
- No code changes needed (already using `sql_epoch_now()`, `sql_now_timestamptz()`)
- **Schema migration** (separate): Replace TIMESTAMPTZ with DATETIME + document UTC assumption

**Why It Blocks MySQL:**
- TIMESTAMPTZ doesn't exist; TIMESTAMP auto-converts to UTC (lossy)
- Must explicitly enforce UTC in application + driver

**Proposed Driver Additions:**
```c
// "NOW() AT TIME ZONE 'UTC'"
const char *sql_now_utc(db_t *db);

// MySQL: "CONVERT_TZ(NOW(), @@session.time_zone, '+00:00')"
const char *sql_now_utc_explicit(db_t *db);

// For WHERE clauses: ensure comparison uses UTC
const char *sql_where_timestamp_utc_fmt(db_t *db);
```

**Schema Changes:**
- Replace `timestamptz` with `DATETIME` in MySQL schema
- Add application-level assertion: "All timestamps stored as UTC"
- Update migrations to ensure MySQL `@@session.time_zone = '+00:00'` on connection

**Estimated Blast Radius:** Low (driver only; schema migration is separate infrastructure task)

**Verification:**
- Build + sentinels
- Time-dependent tests: Verify timestamps round-trip correctly across UTC ↔ local conversions
- TZ test: Create record in UTC-0, read in UTC-5, verify offset correct

---

### PR-M5: Implement JSON Array Iteration Abstraction

**Title:** Add `sql_json_array_to_rows()` driver function; replace `json_array_elements_text()`

**Files:**
- `src/db/sql_driver.h` — Add function
- `src/db/sql_driver.c` — Implement: PostgreSQL uses `json_array_elements_text()` + JOIN; MySQL uses `JSON_EXTRACT()` loop simulation or JSON_TABLE
- `src/engine_consumer.c` (2 occurrences) — Replace with driver call

**Why It Blocks MySQL:**
- PostgreSQL: `json_array_elements_text()` returns set (implicit unnest)
- MySQL: Requires `JSON_TABLE()` (MySQL 5.7.18+) or application-side JSON parsing

**Proposed Driver API:**
```c
// PostgreSQL: " CROSS JOIN json_array_elements_text($1::json) AS elem "
// MySQL:      " CROSS JOIN JSON_TABLE($1, '$[*]' COLUMNS (elem VARCHAR(255))) AS jt "
const char *sql_json_array_unnest_fmt(db_t *db, const char *param_placeholder);
```

**Estimated Blast Radius:** Low (2 call sites; isolated JSON feature)

**Verification:**
- Build + sentinels
- JSON test: Verify array iteration works correctly on both backends

---

### PR-M6: Add Error Code Mapping (Generic → Backend-Specific)

**Title:** Extend `db_error_t` with backend-specific error code mapping; add helpers for constraint violations

**Files:**
- `src/db/db_api.h` — Extend error struct with mapped codes (e.g., ERR_CONSTRAINT_VIOLATION)
- `src/db/db_api.c` — Add helper: `db_error_is_constraint_violation()`, `db_error_is_deadlock()`
- `src/db/pg/db_pg.c` — Add PostgreSQL error mapping (ERRCODE_UNIQUE_VIOLATION → ERR_CONSTRAINT_VIOLATION)
- `src/db/mysql/db_mysql.c` (TBD) — Add MySQL error mapping (1062 → ERR_CONSTRAINT_VIOLATION)
- Refactor callers that branch on error codes

**Why It Matters for MySQL:**
- PostgreSQL error codes (numeric) ≠ MySQL error codes
- Callers must not hardcode backend-specific codes

**Proposed API Extension:**
```c
// In db_error_t:
typedef enum {
  ERR_DB_OK = 0,
  ERR_CONSTRAINT_VIOLATION,
  ERR_FK_VIOLATION,
  ERR_DEADLOCK,
  ERR_LOCK_TIMEOUT,
  ERR_SERIALIZATION_CONFLICT,
} db_error_category_t;

// In db_api.h:
db_error_category_t db_error_category(const db_error_t *err);
```

**Estimated Blast Radius:** Low (infrastructure; doesn't require code refactoring if used proactively)

**Verification:**
- Build + sentinels
- Error test: Trigger constraint violation on both backends; verify category is correct

---

## F) Non-Goals (What Was NOT Done)

- ❌ **No refactoring of existing code** beyond listing what needs abstraction
- ❌ **No schema migration** (TIMESTAMPTZ → DATETIME); documented as separate infrastructure task
- ❌ **No MySQL backend implementation** (db_mysql.c creation); roadmap only
- ❌ **No bulk find-replace** or formatting sweeps
- ❌ **No git operations** (restore, reset, etc.)
- ❌ **No behavioral changes** to existing PostgreSQL functionality
- ❌ **No version-bumping** or CI/build system changes

---

## Summary Table: Readiness by Category

| Category | Status | Effort | Risk |
|----------|--------|--------|------|
| **Binding/Placeholders** | ✅ Ready | 0 | None |
| **LIMIT/OFFSET** | ✅ Ready | 0 | None |
| **FK/Cascades** | ✅ Ready (schema only) | Low | Low |
| **SERIAL/AUTO_INCREMENT** | ✅ Ready (schema only) | Low | Low |
| **Upsert (ON CONFLICT)** | ⚠️ Needs Abstraction | Medium | Medium |
| **RETURNING Clause** | ⚠️ Needs Abstraction | High | High |
| **FOR UPDATE SKIP LOCKED** | ⚠️ Needs Fallback | Medium | Medium |
| **Time/Timezone** | ⚠️ Needs Abstraction | Low | Medium |
| **JSON Operations** | ⚠️ Needs Abstraction | Low | Low |
| **Error Code Mapping** | ⚠️ Partial (infrastructure) | Low | Low |

---

## Conclusion

**MySQL backend is feasible**, but requires **6 sequential PRs** implementing driver abstractions before a MySQL implementation (db_mysql.c) can be added. The largest blocker is **RETURNING clause handling** (24 call sites; requires API redesign). The riskiest is **SKIP LOCKED fallback** (combat system deadlock avoidance). 

**Recommended approach:** Implement PRs in order (M1 → M2 → M3 → M4 → M5 → M6), verify each with full build + sentinel checks + minimal integration testing. Once complete, MySQL backend can be added with confidence that all SQL primitives are abstracted.

**Estimated total effort:** ~2–3 developer-weeks (depending on test coverage for edge cases like deadlock retry logic and timezone conversions).
