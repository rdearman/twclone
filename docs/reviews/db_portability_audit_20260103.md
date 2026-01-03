# Read-Only Code Audit Report: Multi-DB Readiness & C Correctness
**Date:** 2026-01-03
**Status:** COMPLETE (Read-Only)

## A) Executive Scorecard

| Category | Score | Summary |
| :--- | :--- | :--- |
| **DB Portability** | **10/100** | **Critical Coupling.** Codebase heavily relies on Postgres-specific features (`RETURNING`, `ON CONFLICT`, `SKIP LOCKED`, JSON functions). Moving to MySQL/Oracle requires major architectural changes to the Data Access Layer (DAL). |
| **C Correctness** | **70/100** | **Good Hygiene.** Consistent error checking and memory management (Jansson refcounting). Manual string building for SQL is risky but standard for C. |
| **Reliability (Load)** | **30/100** | **Severe Bottleneck.** The entire database layer is serialized via a single global mutex (`g_pg_mutex`), negating any multi-threaded benefits and ensuring poor performance under concurrency. |

### Top 10 Blockers (Priority Order)

| ID | Sev | Title | Primary Evidence | Why it Blocks | Safe Remediation |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **PERF-01** | **P0** | **Global Database Mutex** | `src/db/pg/db_pg.c:15` | Serializes **all** database operations. The server is effectively single-threaded for I/O. | Implement connection pooling (e.g., `pgbouncer` or internal pool) and remove the global lock. |
| **DB-01** | **P0** | **`FOR UPDATE SKIP LOCKED`** | `src/server_combat.c:2616` | Postgres-specific concurrency primitive. Not supported in standard MySQL < 8.0 or Oracle. | Abstract into `db_get_queue_item()` or similar; use standard `FOR UPDATE` with shorter transactions for others. |
| **DB-02** | **P0** | **`RETURNING` Clause** | `src/server_players.c:1319` | Used to fetch IDs after INSERT. MySQL/Oracle do not support this syntax standardly. | Introduce `db_insert_and_get_id()` API. For MySQL, use `LAST_INSERT_ID()`. |
| **DB-03** | **P0** | **`ON CONFLICT` (Upsert)** | `src/db_player_settings.c:35` | Postgres specific. MySQL uses `ON DUPLICATE KEY UPDATE`; Oracle `MERGE`. | Introduce `db_upsert()` API that generates the correct dialect syntax. |
| **DB-04** | **P1** | **Postgres JSON Functions** | `src/engine_consumer.c:192` | `json_build_object`, `json_agg` are Postgres specific. | Move JSON construction to C layer or abstract SQL JSON function generation. |
| **DB-05** | **P1** | **Time/Epoch Functions** | `src/server_bank.c:498` | `EXTRACT(EPOCH FROM now())` and `to_timestamp` are Postgres specific. | Standardize on passing `time_t` from C as integers, or use ANSI `CURRENT_TIMESTAMP`. |
| **DB-06** | **P1** | **Postgres Cast Syntax** | `src/server_universe.c:813` | `::int`, `::bigint`, `::text` casts are invalid in MySQL. | Remove casts where possible or use standard `CAST(x AS type)`. |
| **DB-07** | **P2** | **Leaked `libpq` Header** | `src/server_ships.c:2` | Business logic includes driver header, risking direct API usage. | Remove `#include <libpq-fe.h>` from all non-driver files. |
| **DB-08** | **P2** | **Implicit `LIMIT` Batching** | `src/server_engine.c:1463` | Relies on `LIMIT` for batch processing in loops. | Ensure `db_driver` supports `LIMIT` translation (SQL Server uses `TOP`). |
| **OPS-01** | **P2** | **Blocking `usleep`** | `src/database_cmd.c:403` | Threads sleep with mutexes potentially held or blocking the worker pool. | Use event loops or condition variables; avoid `usleep` in worker threads. |

---

## B) Database Portability Deep Audit

### B1) SQL Dialect Inventory

**1. `RETURNING` Clause (ID retrieval)**
*   **Count:** ~15 occurrences
*   **Files:** `src/server_planets.c`, `src/server_players.c`, `src/server_bank.c`, `src/server_combat.c`, `src/server_engine.c`.
*   **Issue:** MySQL does not support `RETURNING` for `INSERT`.
*   **Abstraction:** `db_insert_id(db)` returning the last inserted ID.

**2. `ON CONFLICT` (Upserts)**
*   **Count:** ~30 occurrences
*   **Files:** `src/db_player_settings.c`, `src/server_corporation.c`, `src/server_players.c`, `src/server_citadel.c`, `src/server_communication.c`, `src/server_bank.c`.
*   **Issue:** Postgres specific. MySQL uses `INSERT ... ON DUPLICATE KEY UPDATE`.
*   **Abstraction:** `db_build_upsert_sql(...)` helper.

**3. `FOR UPDATE [SKIP LOCKED]` (Queue/Locking)**
*   **Count:** ~20 occurrences
*   **Files:** `src/server_combat.c` (heavily used for locking ships/mines), `src/database_cmd.c`.
*   **Issue:** `SKIP LOCKED` is a distinct feature for queue semantics.
*   **Abstraction:** `db_select_for_update(..., skip_locked=true)` or dedicated queue tables.

**4. JSON Functions (`json_build_object`, `json_agg`)**
*   **Count:** ~5 occurrences (in C strings) + SQL files.
*   **Files:** `src/engine_consumer.c`, `src/db/sql_driver.c`.
*   **Issue:** Syntax varies (`JSON_OBJECT` in MySQL).
*   **Abstraction:** Move complex JSON construction to C code (Jansson) instead of SQL.

**5. Time Functions (`EXTRACT(EPOCH)`, `to_timestamp`)**
*   **Count:** ~30 occurrences.
*   **Files:** `src/server_bank.c`, `src/server_combat.c`, `src/server_cron.c`.
*   **Issue:** Epoch handling is specific.
*   **Abstraction:** Pass calculated `time_t` from C into SQL as integers (`$1`), avoiding DB-side time math.

**6. Postgres Placeholders (`$1`, `$2`)**
*   **Count:** Pervasive (Hundreds).
*   **Files:** All `src/server_*.c` and `src/database*.c` files.
*   **Issue:** MySQL uses `?`.
*   **Abstraction:** Driver-level query rewriter or a query builder that generates correct placeholders.

**7. Cast Syntax (`::int`, `::bigint`, `::text`)**
*   **Count:** Frequent.
*   **Files:** `src/server_universe.c`, `src/server_combat.c`.
*   **Issue:** Syntax error in MySQL.
*   **Abstraction:** Use `CAST(val AS type)` or standard ANSI SQL.

### B2) DB API/Driver Boundary Audit

**DB-Facing Modules:**
*   `src/game_db.c` (Manager)
*   `src/db/pg/db_pg.c` (Postgres Driver)
*   `src/db/sql_driver.c` (Helper strings)
*   `src/database_cmd.c` (Business Logic with raw SQL)

**Boundary Violations:**
*   **Leakage:** `src/server_ships.c` includes `<libpq-fe.h>`.
*   **Bypass:** Business logic (e.g., `server_combat.c`) constructs raw SQL strings embedding Postgres-specific keywords (`SKIP LOCKED`). The "Driver" is just a connection holder; the "API" is `db_query(string)`.

**API Gaps for Migration:**
*   **No Placeholders Abstraction:** Logic assumes `$1`.
*   **No ID Retrieval:** Logic assumes `RETURNING`. Needs `db_last_insert_id()`.
*   **No Error Mapping:** `db_pg.c` maps `40P01` (Deadlock) manually. MySQL codes are different (1213).
*   **No Transaction Control:** Transaction boundaries seem manual or implicit.

### B3) Time Handling Portability

| Table/Column | Type | Writer Callsite | Reader Callsite | Risk |
| :--- | :--- | :--- | :--- | :--- |
| `players.podded_last_reset` | `bigint` (Epoch) | `database_cmd.c:1167` (`EXTRACT(EPOCH...)`) | `server_auth.c:298` (integer compare) | Low (Store as int) |
| `bank_transactions.ts` | `bigint` (Epoch) | `server_bank.c:498` (`EXTRACT...`) | `server_bank.c:942` (Order by ts) | Low |
| `system_notice.expires_at` | `bigint` (Epoch) | `server_engine.c:844` | `server_engine.c:1426` | Low |
| **Comparison Strategy** | N/A | SQL (`expires_at > EXTRACT(...)`) | C (`time(NULL)`) | **High**. Mixed logic. |

---

## C) C Correctness Audit

### C1) Memory / Resource Management
*   **Jansson Ownership:** `src/server_corporation.c` and `src/server_communication.h` correctly document and use `json_decref`. "Steal" semantics are used, reducing leak risk if followed.
*   **Cleanup:** `src/server_ships.c` uses `handle_ship_destruction` which is void of manual memory management in the snippet, relying on `db` handle.
*   **Findings:**
    *   `src/server_corporation.c.before_cocci`: High density of `json_decref`, implies complex JSON manipulation.
    *   `src/server_bulk.c`: `json_decref(ctx->captured_envelopes)` in loop cleanup.

### C2) Concurrency / Locking
*   **Global Mutex:** `src/db/pg/db_pg.c:15` (`g_pg_mutex`) locks *every* DB call.
    *   **Risk:** Deadlock is unlikely (single lock), but performance is strictly serialized.
    *   **Scope:** Cron, Engine, and Server threads all contend for this one lock.
*   **Thread Safety:** `src/server_loop.c` uses `g_clients_mu` to protect the client list.
*   **Hidden Global:** `src/server_engine.c:647` uses `static time_t start_ts`. This makes the function stateful and thread-unsafe if called concurrently.

### C3) Error Handling
*   **Good Pattern:** `src/server_players.c` consistently checks `if (db_query(...))`.
*   **Missing Error Check:** `src/server_ships.c` (snippet) checks `!db` but `handle_ship_destruction` return values should be propagated.

---

## D) Operational / Load Readiness

**Risks:**
1.  **Single Global Lock:** `src/db/pg/db_pg.c`. The system cannot scale beyond a single core's worth of DB throughput, regardless of hardware.
2.  **Blocking Sleeps:** `src/database_cmd.c` uses `usleep(100000)` (100ms) in what appears to be command handlers. This blocks the worker thread.
3.  **Connection Model:** No pooling visible in `db_pg.c`. It opens one connection (`PQconnectdb`) and mutex-wraps it.
4.  **Rate Limiting:** `ERR_RATE_LIMITED` exists in `errors.h`, but enforcement via `usleep` is brittle.

---

## E) Prioritized Action Plan (No Code)

**Phase 1: Safety & Hygiene (Days 1-2)**
1.  **PR-1:** Remove `libpq-fe.h` from `src/server_ships.c`.
    *   *Verification:* `grep` confirms no `libpq` usage in `src/` except `db/pg`.
2.  **PR-2:** Remove `static time_t` from `src/server_engine.c`. Pass state via context struct.
    *   *Verification:* `nm` or manual inspection.

**Phase 2: Database Abstraction (Days 3-7)**
3.  **PR-3:** Introduce `db_build_upsert_sql` in `db_api.h`.
    *   *Objective:* Centralize `ON CONFLICT` string generation.
    *   *Verification:* Unit test generating PG and (mock) MySQL strings.
4.  **PR-4:** Introduce `db_insert_returning_id` API.
    *   *Objective:* Hide `RETURNING` clause.
    *   *Verification:* Replace 5 usages in `server_players.c`.

**Phase 3: Performance (Days 8-14)**
5.  **PR-5:** Implement Connection Pooling in `db_pg.c`.
    *   *Objective:* Remove `g_pg_mutex`. Allow parallel `db_query` calls.
    *   *Risk:* Must ensure underlying `libpq` usage is thread-safe (one `PGconn` per thread or pool checkout).
6.  **PR-6:** Remove `usleep` from business logic.
    *   *Objective:* Use non-blocking delays or scheduler events.

---

### Audit Meta-Data
**Commands Ran:** `grep -rn` for SQL keywords, C symbols, and threading primitives. `read_file` for deep dives into `db_pg.c` and `server_ships.c`.
**Limitations:** Binary files (`.o`) matched some greps (ignored). Deep logic flow of `server_combat.c` not fully traced, but `FOR UPDATE` usage is confirmed.
