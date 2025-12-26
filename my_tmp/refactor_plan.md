# Database Refactoring & Architecture Plan (Future-Proof)

This plan details the process for refactoring the database access layer to be backend-agnostic, supporting PostgreSQL, SQLite, and future engines like MySQL. It prioritizes a clean separation of concerns and future maintainability by using a "Logic Manager + Driver" pattern.

## 1. Architectural Layers

The architecture is divided into four distinct layers:

1.  **Public Game DB API (`src/database.h`)**: The stable, public contract for the game server.
2.  **Database Logic Manager (`src/game_db.c`)**: A new, backend-agnostic layer that implements the public API. It handles business logic, validation, and dispatches operations to the driver layer.
3.  **Generic DB Driver API (`src/db/db_api.h`)**: A strict internal contract defining what any database driver must do (e.g., open, close, query, exec, transactions).
4.  **Backend Drivers (`src/db/[pg|sqlite]/`)**: The specific implementations of the driver contract for each database engine (e.g., using `libpq` for Postgres, `sqlite3` for SQLite).

## 2. Key Design Principles (The "Contract")

The success of this refactor depends on strictly adhering to the contract defined in `src/db/db_api.h`.

*   **API Naming:** All public-facing game DB functions will be in `database.h` (or a new `game_db.h`). The internal driver functions will be in `db_api.h` with a generic `db_` prefix (e.g., `db_query`).
*   **Error Handling:** A generic `db_error_t` and `db_errc_t` enum will be used to report errors without leaking backend-specific error codes.
*   **Parameter Binding:** A `db_bind_t` struct with helper macros (`DB_P_INT`, `DB_P_TEXT`) will be used for all queries to ensure type safety.
*   **Placeholder Convention:** All SQL passed to the driver layer **MUST** use `$1, $2, ...` placeholders. It is the driver's responsibility to translate these if necessary (e.g., to `?1, ?2` for SQLite).
*   **Result Set Semantics:**
    *   Result sets are managed via an opaque `db_res_t` handle.
    *   Pointers returned by column accessors (`db_res_col_text`, `db_res_col_blob`) are **only valid until the next call to `db_res_step()` or `db_res_finalize()`**. The caller *must* copy the data if it needs to persist longer.
    *   NULL values in the database will be represented by `NULL` pointers or specific return codes from helper functions.
*   **Transaction Management:** The Logic Manager (Layer 2) is responsible for transaction boundaries (`db_tx_begin`, `db_tx_commit`). The driver implements these primitives.
*   **"Ops V-Table" for Optimization:** An optional `db_ops_vtable_t` will be part of the driver interface. This allows drivers (like Postgres) to provide highly optimized implementations for complex, multi-step operations by calling a single stored procedure, bypassing the need for the Logic Manager to execute multiple generic SQL statements.

## 3. Step-by-Step Execution Plan

#### **Phase 1: Implement the Enhanced Generic Driver API**
1.  **Modify `src/db/db_api.h`:** Overwrite the existing header with the new, stricter contract outlined in the feedback, including the error model, typed binds, and result set API with explicit lifetime comments.
2.  **Create `src/db/db_api.c`:** Implement the `db_open` factory function. This will be the only place in the code with a compile-time switch (`#if ENABLE_POSTGRES`) to select and initialize the correct backend driver.
3.  **Update Postgres Driver (`src/db/pg/db_pg.c`):**
    *   Move the file to the new subdirectory structure.
    *   Implement the full `db_api.h` contract, including the new result set functions using `libpq`.

#### **Phase 2: Create the SQLite Driver & New "Common" Layer**
1.  **Create `src/db/sqlite/db_sqlite.c`:** Implement the `db_api.h` contract using `sqlite3_*` functions. This includes writing the logic to translate `$N` placeholders to `?N`. The initial implementation can be a stub.
2.  **Create `src/game_db.h` and `src/game_db.c`:** These will become the new "Logic Manager".

#### **Phase 3: Migrate Logic (Iterative and Test-Driven)**
1.  **Pick a function:** Start with a simple, read-only function from the old `database_cmd.c`, like `db_get_player_xp`.
2.  **Port the function:**
    *   Add its signature to `game_db.h`.
    *   Implement it in `game_db.c`. The new implementation will use `db_query()` from the generic API.
    *   Update the original call site in the server code to include `game_db.h` and call the new function.
3.  **Test:** Compile and run the server to ensure the functionality still works.
4.  **Repeat:** Continue this process function-by-function, migrating logic from the old `database.c` and `database_cmd.c` into `game_db.c`. For complex writes, decide whether to use the generic `db_exec` or add a specific function to the "Ops V-Table".

#### **Phase 4: Update the Build System**
1.  **Modify `bin/Makefile.am`:**
    *   Update `server_SOURCES` to always include `src/game_db.c` and `src/db/db_api.c`.
    *   Use an `if/else` block on `ENABLE_POSTGRES` to conditionally compile *either* `src/db/pg/db_pg.c` *or* `src/db/sqlite/db_sqlite.c`.
    *   Make `server_LDADD` conditional to link either `-lpq` or `-lsqlite3`.

#### **Phase 5: Cleanup**
1.  Once all functions have been migrated, delete the now-empty `database.c` and `database_cmd.c` from the project and the Makefile.

This structured approach allows for a systematic, testable migration to a robust, multi-backend database architecture.