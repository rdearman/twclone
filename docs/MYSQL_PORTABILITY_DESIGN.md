# PHASE 5B: MySQL Portability Design Audit (Revised)

## 1. Inventory of Non-Portable SQL Tokens (src/db/repo/**)

| File | Function | Token | Evidence Snippet | Usage |
| :--- | :--- | :--- | :--- | :--- |
| `repo_players.c` | `db_create_initial_ship` | `RETURNING` | `INSERT INTO ships ... RETURNING id;` | Insert ID retrieval (Core) |
| `repo_players.c` | `db_player_deduct_credits` | `RETURNING` | `UPDATE players SET credits = credits - {1} ... RETURNING credits;` | Atomic update + balance return (Core) |
| `repo_players.c` | `h_set_player_preference` | `ON CONFLICT` | `INSERT INTO player_prefs ... ON CONFLICT (player_id, key) DO UPDATE ...` | Idempotent KV update (Core) |
| `repo_cmd.c` | `db_planet_update_goods` | `GREATEST` | `quantity = GREATEST(planet_goods.quantity + {3}, 0)` | Lower-bound clamping (Core) |
| `repo_cmd.c` | `db_player_get_alignment` | `FOR UPDATE` | `SELECT ... FROM players WHERE player_id = {1} FOR UPDATE;` | Pessimistic locking (Core) |
| `repo_combat.c` | `db_combat_get_ship_locked`| `SKIP LOCKED`| `SELECT ... FOR UPDATE SKIP LOCKED` | Concurrency/Contention mgmt (Core) |
| `repo_universe.c`| `db_search_sectors` | `ILIKE` | `search_term_1 ILIKE {1}` | Case-insensitive search (Core) |
| `repo_cmd.c` | `db_log_engine_event` | `to_timestamp`| `VALUES (to_timestamp({1}), ...)` | Explicit epoch conversion (Target: Removal) |
| `repo_engine_consumer.c`| `repo_engine_fetch_events`| `json_*` | `IN (SELECT trim(json_array_elements_text({3})))`| JSON array unnesting (Core) |

---

## 2. Hard Requirements & Policy Decisions

### 2.1 MySQL Version: 8.0+ Only
**Justification:** 
- **`SKIP LOCKED`:** Essential for combat mechanics to prevent deadlocks under high player concurrency (MySQL 8.0.1+).
- **`JSON_TABLE`:** Required for efficient event consumption/filtering.
- **CTEs:** Used for complex sector/adjacency lookups.

### 2.2 Storage Policy
- **Timestamps:** **Native Types**. 
    - PostgreSQL: `timestamptz`.
    - MySQL: `datetime(6)`.
    - **Refactor:** Eliminate `to_timestamp()` from SQL strings. Use direct parameter binding via `db_bind_timestamp(int64_t epoch_s)`. Driver implementations handle the wire-format conversion.
- **Booleans:** **Native Types**.
    - PostgreSQL: `boolean`.
    - MySQL: `BOOLEAN` (alias for `tinyint(1)`).
    - **Refactor:** Use `db_bind_bool(bool val)` for all logic toggles.
- **Case-Insensitivity:** **Schema Collation**. 
    - Searchable columns must use `utf8mb4_unicode_ci` at the DDL level. 
    - `ILIKE` is replaced with standard `LIKE`.

---

## 3. Minimal DB Primitives (Design Only)

### 3.1 Domain-Specific Atomic Adjustments (Correctness Critical)
Replaces generic `UPDATE ... RETURNING` with operation-level atomicity.

*   **`bool db_player_credits_adjust(db_t *db, int player_id, int64_t delta, int64_t *out_new_balance, db_error_t *err)`**
    *   **Semantics:** Atomically adds/subtracts credits. If `delta` is negative, must fail if `balance + delta < 0`.
    *   **PG Implementation:** `UPDATE players SET credits = credits + $1 WHERE player_id = $2 AND (credits + $1) >= 0 RETURNING credits`.
    *   **MySQL Implementation:** `UPDATE players SET credits = credits + $1 WHERE player_id = $2 AND (credits + $1) >= 0; SELECT credits FROM players WHERE player_id = $2;` (Executed in a single transaction; checking `affected_rows == 1`).

### 3.2 Structured Upsert (Replacing ON CONFLICT)
Avoids "stringly-typed" SQL fragments by using structured column/value mapping.

*   **`bool db_upsert_kv(db_t *db, const char *table, const char *key_col, const char *val_col, const db_bind_t *key_val, const db_bind_t *val_val, db_error_t *err)`**
    *   **Semantics:** Idempotent update for key-value stores (prefs, event watermarks).
    *   **PG Implementation:** `INSERT ... ON CONFLICT(key_col) DO UPDATE SET val_col = EXCLUDED.val_col`.
    *   **MySQL Implementation:** `INSERT ... ON DUPLICATE KEY UPDATE val_col = VALUES(val_col)`.

### 3.3 Temporal & ID Retrieval Helpers
*   **`db_bind_t db_bind_timestamp(int64_t epoch_s)`**: Constructor for binding native timestamp parameters.
*   **`const char * sql_now(db_t *db)`**: Driver fragment for current DB time (e.g., `NOW()` vs `CURRENT_TIMESTAMP`).
*   **`bool db_exec_insert_id(db_t *db, const char *sql, const db_bind_t *p, size_t n, int64_t *out_id, db_error_t *err)`**: 
    *   **Constraint:** Implementation is backend-specific. The driver *must not* mutate the `sql` string. PG driver uses `RETURNING` if the SQL is provided with it; MySQL uses `LAST_INSERT_ID()`.

---

## 4. Work Plan + Risk Map

1.  **Bank & Players (`repo_bank.c`, `repo_players.c`):** Atomic credits. (Affected: ~8 queries).
    - *Risk:* Double-spending or balance desync.
2.  **Engine Consumer (`repo_engine_consumer.c`):** Refactor `to_timestamp` to direct binding and implement JSON unnesting via `JSON_TABLE` (MySQL 8.0).
3.  **Combat (`repo_combat.c`):** Operation-level ship locking.
    - *Risk:* Contention deadlocks.

---

## 5. Verification Plan

*   **The Balance Gate:** Stress test `db_player_credits_adjust` with 50 concurrent threads. Verify exactly 0 negative balances and 100% credit conservation.
*   **The Temporal Gate:** Verify that `db_bind_timestamp` results in identical wall-clock values on both PG and MySQL (checking for timezone/leap-second offset errors).
*   **The JSON Gate:** Exercise event type filtering with empty, single, and 50+ item JSON arrays.