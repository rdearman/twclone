# Design Note: MySQL Compatibility Primitives

## 1. SQL Token Usage Analysis (src/db/repo/**)

| Module | Token | Core Path? | Usage |
| :--- | :--- | :--- | :--- |
| **repo_players.c** | `ON CONFLICT` | Yes | Upserting player preferences, bookmarks, and subscriptions. |
| **repo_players.c** | `RETURNING` | Yes | Retrieving generated ship IDs and updated credit balances. |
| **repo_cmd.c** | `FOR UPDATE` | Yes | Locking player records for atomic alignment/experience updates. |
| **repo_cmd.c** | `GREATEST` | Yes | Clamping commodity quantities to zero in updates. |
| **repo_cmd.c** | `ON CONFLICT` | Yes | Upserting session data, idempotency keys, and rob attempt logs. |
| **repo_cmd.c** | `to_timestamp` | Yes | Converting Unix epochs for event logging and session expiry. |
| **repo_engine_consumer.c**| `ON CONFLICT` | Yes | Maintaining event cursor states and deadletter queues. |
| **repo_ports.c** | `ON CONFLICT` | Yes | Managing port stock levels and player bust status. |
| **repo_bank.c** | `RETURNING` | Yes | Atomic balance updates for deposits and withdrawals. |
| **repo_bank.c** | `ON CONFLICT` | Yes | Lazy creation of bank accounts and updating frozen flags. |
| **repo_planets.c** | `RETURNING` | Yes | Retrieving IDs for newly created planets via Genesis. |
| **repo_planets.c** | `GREATEST` | Yes | Clamping sector navigation hazard (navhaz) values. |
| **repo_universe.c** | `ILIKE` | Yes | Case-insensitive searching for sectors and ports. |
| **repo_universe.c** | `::` (cast) | Yes | Explicit type casting within Common Table Expressions (CTEs). |
| **repo_combat.c** | `FOR UPDATE SKIP LOCKED` | Yes | Non-blocking row locking for high-concurrency combat resolution. |

---

## 2. Proposed DB Primitives

### A. Upsert Abstraction
*   **Signature:** `bool db_upsert(db_t *db, const char *table, const char *conflict_cols, const char *update_set, const db_bind_t *params, size_t n_params, db_error_t *err);`
*   **Semantics:** Atomically inserts a row or updates it if a uniqueness constraint on `conflict_cols` is violated.
*   **PG Strategy:** `INSERT INTO ... ON CONFLICT (conflict_cols) DO UPDATE SET update_set`.
*   **MySQL Strategy:** `INSERT INTO ... ON DUPLICATE KEY UPDATE update_set`.

### B. Insert-and-Return-ID Abstraction
*   **Signature:** `bool db_exec_insert_id(db_t *db, const char *sql, const db_bind_t *params, size_t n_params, int64_t *out_id, db_error_t *err);`
*   **Semantics:** Executes an INSERT statement and retrieves the primary key of the new row.
*   **PG Strategy:** Append `RETURNING <pk>` to the query.
*   **MySQL Strategy:** Execute INSERT followed by `SELECT LAST_INSERT_ID()`.

### C. Row Locking Fragment
*   **Signature:** `const char *sql_lock_clause(db_t *db, bool skip_locked);`
*   **Semantics:** Returns the dialect-specific suffix for row locking in SELECT statements.
*   **PG Strategy:** `FOR UPDATE` or `FOR UPDATE SKIP LOCKED`.
*   **MySQL Strategy:** `FOR UPDATE` (5.7) or `FOR UPDATE SKIP LOCKED` (8.0+).

### D. Case-Insensitive Matching
*   **Signature:** `const char *sql_ilike_op(db_t *db);`
*   **Semantics:** Returns the operator for case-insensitive `LIKE`.
*   **PG Strategy:** `ILIKE`.
*   **MySQL Strategy:** `LIKE` (assumes `_ci` collation) or `LOWER(%s) LIKE LOWER(%s)`.

### E. Temporal Conversion
*   **Signature:** `const char *sql_to_timestamp_fmt(db_t *db);`
*   **Semantics:** Provides the format string for converting an epoch integer parameter to a database timestamp.
*   **PG Strategy:** `to_timestamp(%s)`.
*   **MySQL Strategy:** `FROM_UNIXTIME(%s)`.

### F. Scalar Clamping (Greatest/Least)
*   **Signature:** `const char *sql_greatest_fn(db_t *db);`
*   **Semantics:** Returns the function name for scalar maximum.
*   **PG Strategy:** `GREATEST`.
*   **MySQL Strategy:** `GREATEST`.

### G. Explicit Casting
*   **Signature:** `char *sql_cast_fmt(db_t *db, const char *expr, const char *type, char *buf, size_t buf_sz);`
*   **Semantics:** Formats a casting expression for a specific target type.
*   **PG Strategy:** `expr::type`.
*   **MySQL Strategy:** `CAST(expr AS type)`.
