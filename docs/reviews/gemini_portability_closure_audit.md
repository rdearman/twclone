# Portability Closure Audit (Mechanical Checklist)
**Date:** 2026-01-03
**Status:** Audit Complete

## 1. Dialect Inventory

This inventory counts specific SQL patterns that block migration to MySQL/Oracle.

| Pattern | Count (Est.) | Driver Primitive? | Migrated Call Sites | Blocking Severity |
| :--- | :--- | :--- | :--- | :--- |
| **`$1, $2, ...` (Placeholders)** | **500+** | **NO** | 0 / 500+ | **Critical**. Hardcoded Postgres syntax. |
| **`RETURNING` (ID fetch)** | **15** | **NO** | 0 / 15 | **Critical**. Logic relies on single-roundtrip insert-read. |
| **`ON CONFLICT` (Upsert)** | **30** | **YES** (`sql_upsert_do_update`) | ~5 / 30 | **High**. Primitive exists but adoption is low. |
| **`FOR UPDATE SKIP LOCKED`** | **20** | **YES** (`sql_for_update_skip_locked`) | ~10 / 20 | **High**. Primitive exists, adoption partial. |
| **JSON Functions (`json_build_...`)** | **5** | **YES** (`sql_json_object_fn`) | 0 / 5 | **Medium**. Logic constructs JSON in SQL. |
| **Type Casts (`::int`, `::text`)** | **100+** | **NO** | 0 / 100+ | **Medium**. Postgres-specific cast syntax. |
| **`ILIKE` (Case-insensitive)** | **5** | **NO** | 0 / 5 | **Medium**. Postgres-specific operator. |
| **`LIMIT` / `OFFSET`** | **50+** | **NO** | 0 / 50+ | **Low**. (Compatible with MySQL, incompatible with Oracle/MSSQL). |

## 2. Driver Coverage Map

Analysis of `src/db/sql_driver.c` capabilities versus the dialect inventory.

*   **`ON CONFLICT`**: **Covered**. `sql_upsert_do_update` abstracts `ON CONFLICT` vs `ON DUPLICATE KEY UPDATE`.
    *   *Action:* Migrate remaining raw SQL in `server_players.c`, `server_corporation.c`.
*   **`FOR UPDATE SKIP LOCKED`**: **Covered**. `sql_for_update_skip_locked` exists.
    *   *Action:* Verify all usages in `server_combat.c` use the helper.
*   **Time/Epoch**: **Partially Covered**. `sql_now_timestamptz` and `sql_epoch_now` exist but are Postgres-only implementations (return `NULL` for others).
    *   *Action:* Implement MySQL/SQLite versions in `sql_driver.c`.
*   **JSON**: **Covered**. `sql_json_object_fn` exists.
    *   *Action:* Adopt in `engine_consumer.c`.
*   **Placeholders (`$N`)**: **NOT COVERED**. Major gap.
    *   *Action:* Need a query pre-processor or builder to swap `$1` -> `?`.
*   **`RETURNING`**: **NOT COVERED**.
    *   *Action:* Need `db_insert_id(db)` API.
*   **Casts (`::`)**: **NOT COVERED**.
    *   *Action:* Remove casts where possible; use `CAST()` standard SQL.

## 3. Concurrency Reality Check

*   **Global Mutex:** **CONFIRMED**.
    *   `src/db/pg/db_pg.c` defines `static pthread_mutex_t g_pg_mutex = PTHREAD_MUTEX_INITIALIZER;`.
    *   Every `db_query` call locks this mutex.
*   **Impact:** The application is serialized at the database adapter level. Parallel worker threads (cron, engine) provide **zero** concurrency benefit for DB-bound tasks.
*   **Intent:** Likely a "quick fix" for using a single non-thread-safe `PGconn` across threads.

## 4. UTC DATETIME Plan

**Current State:** Chaotic Mix.
*   **`TIMESTAMPTZ`**: ~70% of tables (e.g., `players.login_time`, `sessions.created_at`).
*   **`BIGINT` (Epoch)**: ~25% of tables, including high-volume ones (`engine_events`, `bank_transactions`, `entity_stock`).
*   **`TEXT`**: ~5% of tables (`stocks.last_dividend_ts`, `insurance_policies.start_ts`).

**Comparison Logic:**
*   SQL: `expires_at > EXTRACT(EPOCH FROM NOW())` (Postgres specific)
*   C: `time(NULL)` (Epoch)

**Recommendation:**
*   **Standard:** **`TIMESTAMPTZ` (UTC)** for all stored times.
*   **Reasoning:**
    *   Native DB support for date math (`NOW() - INTERVAL '1 day'`).
    *   Human-readable in DB tools.
    *   Standardizes C interaction to `ISO8601` strings or struct `tm`.
*   **Migration Plan (Blast Radius: High):**
    1.  **Refactor C:** Change all C `time_t` logic to bind ISO8601 strings instead of integers.
    2.  **Schema Migration:** `ALTER TABLE x ALTER COLUMN y TYPE TIMESTAMPTZ USING to_timestamp(y)`.
    3.  **Driver Update:** Update `sql_driver` to return `CURRENT_TIMESTAMP` instead of `EXTRACT(EPOCH...)`.

## 5. Burn-down Checklist (Next Steps)

1.  [ ] **Driver**: Implement `db_insert_id()` to replace `RETURNING`.
2.  [ ] **Driver**: Implement `sql_ilike()` helper.
3.  [ ] **Refactor**: Replace all `::type` casts with `CAST(x AS type)`.
4.  [ ] **Refactor**: Replace `RETURNING` usage with `db_insert_id()`.
5.  [ ] **Refactor**: Migrate raw `ON CONFLICT` to `sql_upsert_do_update`.
6.  [ ] **Refactor**: Migrate `json_build_object` to C-side construction.
7.  [ ] **Arch**: Implement Placeholder translation (`$1` -> `?`).
