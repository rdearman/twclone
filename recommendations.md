## Senior Developer Code Review: `src/` Directory

### Overall Impressions:
The project appears to be a substantial C application for a game server, organized into logical modules (e.g., `server_ships`, `server_planets`, `database`, `server_auth`). The use of SQLite for the database, Jansson for JSON handling, and `pthread` for concurrency indicates a solid foundation for a performant server. The file naming convention is clear and modular.

### General Recommendations:

1.  **Consistency in Error Handling and Logging:**
    *   **Recommendation:** While `LOGE`, `LOGW`, `LOGI` macros are used, consistency in *how* errors propagate and are handled can be improved. Establish a clear pattern for function return values (e.g., always `SQLITE_OK`/`SQLITE_ERROR` or a custom enum for module-specific errors) and ensure all error branches log sufficient context.
    *   **Example for improvement:** In `handle_ship_destruction`, some `LOGW` calls allow the flow to continue, while `LOGE` sometimes leads to an early `return rc`. A clear policy on critical vs. non-critical errors would help.

2.  **Thread Safety and Mutex Usage:**
    *   **Observation:** `pthread_mutex_t db_mutex` is used extensively for database access, which is good. However, there are instances where mutexes are passed (`sqlite3 *db` parameter in helper functions) but the lock is acquired/released inside, sometimes leading to nested locking attempts or inconsistent locking patterns (e.g., `db_get_handle()` acquires a lock, then a calling function might acquire it again).
    *   **Recommendation:** Consolidate mutex ownership. Ideally, a single layer (e.g., the public API functions of `database.c`) should acquire and release the global `db_mutex`. Internal helper functions should assume the mutex is already held and avoid re-locking. The `db_get_handle()` function's `mutex_initialized` block within itself is a bit unusual; typically, mutex initialization would happen once at application startup.
    *   **Potential risk:** Deadlocks or performance overhead from redundant locking.

3.  **Magic Numbers and String Literals:**
    *   **Observation:** Many functions use hardcoded integers (e.g., `1` for default sector, `86400` for session TTL, `0` for escape pod shiptype ID) and string literals (e.g., `"ore"`, `"big_sleep"`).
    *   **Recommendation:** Define these as constants (macros or `const` variables) in appropriate header files (`config.h`, `schemas.h`, `common.h`). This improves readability, makes changes easier, and reduces the chance of typos. For example, `SHIPTYPE_ESCAPE_POD_ID` for `0` or `SECONDS_IN_DAY` for `86400`.
    *   **Example:** In `handle_escape_pod_spawn`, `escape_pod_shiptype_id = 0` is hardcoded.

4.  **Database Schema Evolution and Migration:**
    *   **Observation:** The `create_table_sql` array in `database.c` is extensive. There's also `MIGRATE_A_SQL`, `MIGRATE_B_SQL`, etc. This indicates an evolving schema.
    *   **Recommendation:** Develop a more robust and explicit schema migration system. Manually maintaining large SQL arrays in C code can become cumbersome and error-prone as the project grows. Consider using external SQL files for schema definitions and migrations, managed by a versioning tool, or an embedded migration library if appropriate for C. Ensure forward/backward compatibility considerations are documented.

5.  **Code Duplication (SQL Statements):**
    *   **Observation:** Similar SQL patterns (e.g., `SELECT COUNT(*)...`, `INSERT OR IGNORE INTO subscriptions...`) appear in multiple places.
    *   **Recommendation:** Extract common SQL statements into static `const char *` variables within the `.c` file where they are most used, or if truly generic, into a `database_sql.h` if one existed, but preferably as private statics. This makes SQL easier to review and maintain.

6.  **Function Granularity and Single Responsibility Principle:**
    *   **Observation:** Some functions, like `db_player_info_json`, are quite large and perform multiple tasks (fetching player, ship, type, sector, owner, cargo information, and then structuring it into a JSON object).
    *   **Recommendation:** Break down complex functions into smaller, more focused units. For example, a `db_get_ship_details(ship_id)` and a `db_get_player_details(player_id)` could fetch raw data, and a separate `build_player_info_json(player_data, ship_data)` could assemble the JSON. This improves testability and readability.

7.  **JSON Handling (`jansson`):**
    *   **Observation:** Jansson is used for JSON manipulation. It's generally well-used, but there are some verbose `json_pack` calls and manual `json_decref` management.
    *   **Recommendation:** Where complex JSON objects are constructed, consider helper functions for common patterns (e.g., `create_ship_json(ship_data)`). Ensure every `json_object`, `json_array`, or `json_string` allocation has a corresponding `json_decref` call to prevent memory leaks, especially on error paths. The manual dereferencing can be error-prone.

8.  **Error Messages and Debugging Information:**
    *   **Observation:** `LOGE` often includes `sqlite3_errmsg(db)`. This is good. However, sometimes errors are generic ("Database error") without providing specific context from the underlying SQLite call.
    *   **Recommendation:** Ensure error messages are as informative as possible, including function names, specific parameters, and the raw SQLite error where applicable. This significantly aids debugging.

9.  **Standard Library Usage:**
    *   **Observation:** Mixed use of `rand()`/`srand()` with `time(NULL)` for seeding.
    *   **Recommendation:** For cryptographic or truly unpredictable random numbers, `urandom_bytes` is present and could be leveraged more widely if randomness is critical. For general-purpose pseudo-randomness, ensuring `srand` is called *once* at program start (e.g., in `main`) is sufficient. Multiple calls to `srand(time(NULL))` within quick succession will yield the same sequence of numbers.

### Module-Specific Observations:

*   **`database.c` / `database.h`:**
    *   **Observation:** The core database module is quite large, handling schema creation, default data, and a multitude of CRUD-like operations. The `db_path_exists` function's default return type to `int` is a warning that should be addressed by explicitly typing `int db_path_exists(...)`.
    *   **Recommendation:** Consider further modularizing `database.c` if possible (e.g., `db_schema.c`, `db_queries.c`) to reduce file size and improve logical separation. Ensure all functions have explicit return types.
    *   **New helper functions:** The recently added helper functions for ship destruction (`db_mark_ship_destroyed`, `db_clear_player_active_ship`, etc.) are good examples of granular, single-purpose database operations, fitting well into the overall pattern.

*   **`server_auth.c`:**
    *   **Observation:** The authentication flow now includes complex logic for "Big Sleep" checks and respawning, which is directly coupled with the login process.
    *   **Recommendation:** The logic for checking and ending "Big Sleep" is correctly placed, but could potentially benefit from a dedicated helper function (e.g., `handle_player_big_sleep_status(player_id)` that returns an action or status) to keep `cmd_auth_login` cleaner. This would make the "Big Sleep" logic more reusable and testable outside the main command handler.

*   **`server_ships.c`:**
    *   **Observation:** `handle_ship_destruction` is a critical new function. It correctly delegates to `handle_escape_pod_spawn` and `handle_big_sleep`.
    *   **Recommendation:** The `ship_kill_context_t` struct is well-designed for passing destruction context. Ensure all call sites correctly populate this struct. The fallback to `handle_big_sleep` on `handle_escape_pod_spawn` failure is a good defensive measure. The `process_ship_destruction` function is now largely superseded by `handle_ship_destruction` and could potentially be removed or refactored to call the new handler.

*   **`server_config.h` / `server_config.c`:**
    *   **Observation:** New `death` configuration struct is added. This is good practice.
    *   **Recommendation:** Ensure all configuration values are loaded from a central configuration file (e.g., `config.json` or similar) and not hardcoded if they are expected to change frequently or be tunable by operators.

---

## Database Schema Review: `hold/fullschema.sql` (SQLite)

### Overall Impressions:
The schema is ambitious and reflects a detailed understanding of the domain (space-trading/MMO elements). It covers a wide range of game mechanics: players, ships, planets, corporations, mail, real-time events, and a sophisticated economy system with banks, commodities, stocks, insurance, loans, and even illicit activities. The use of `CHECK` constraints, `FOREIGN KEY`s, `DEFAULT` values, and various indexing strategies is commendable and demonstrates good database design principles. The inclusion of `VIEWS` for simplified data access and `TRIGGERS` for data integrity and automation is also a strong point.

### General Recommendations & Observations:

1.  **Data Type Consistency and Precision (Timestamps):**
    *   **Observation:** Timestamps are stored inconsistently. Some columns use `INTEGER` (e.g., `sessions.created_at`, `engine_audit.ts`), while others use `TEXT` with `strftime('%Y-%m-%dT%H:%M:%SZ','now')` for ISO 8601 format (e.g., `corporations.created_at`, `bank_tx.ts`, many `_ts` columns in economy tables). There are also `TIMESTAMP` (e.g., `ships.cloaked`) which in SQLite is just an affinity.
    *   **Recommendation:** Standardize timestamp storage. For SQLite, `INTEGER` (Unix epoch seconds or milliseconds) is generally preferred for performance (indexing, range queries) and storage efficiency. Use `TEXT` (ISO 8601) only if human readability in the database file is a higher priority than query performance or easy arithmetic. The mix makes querying and comparison harder.

2.  **Normalization vs. Denormalization (and Performance):**
    *   **Observation:** There are instances of denormalization (e.g., `ports.ore_on_hand`, `organics_on_hand`, `equipment_on_hand` which is also in `planet_goods`). `player_info_v1` and `sector_search_index` are well-used `VIEWS` that denormalize data for easier access, which is good.
    *   **Recommendation:** Review denormalized columns for strict necessity. While denormalization can improve read performance, it complicates writes and requires careful synchronization (e.g., through triggers or application logic). For example, `ports.ore_on_hand` is directly tracked on the `ports` table, but `planet_goods` uses a separate table. Consider if a generic `resource_on_hand` table for both `ports` and `planets` (with `owner_type`, `owner_id`, `commodity`, `quantity`) could simplify things.

3.  **Foreign Key Constraints and `ON DELETE` Actions:**
    *   **Observation:** Foreign keys are used extensively, which is excellent for data integrity. `ON DELETE CASCADE` is used appropriately in many places.
    *   **Recommendation:** Review `ON DELETE SET NULL`. Ensure that setting to `NULL` makes sense for the application logic. For core entities, sometimes `ON DELETE RESTRICT` or `ON DELETE NO ACTION` can be safer, forcing application-level handling of deletions.

4.  **`CHECK` Constraints:**
    *   **Observation:** Extensive use of `CHECK` constraints. This is excellent for enforcing business rules at the database level.
    *   **Recommendation:** Continue this practice. For `TEXT` fields with limited valid values, `CHECK` constraints are very valuable.

5.  **Indexing Strategy:**
    *   **Observation:** Many indices are created, especially on foreign key columns and frequently queried columns. `UNIQUE` indices are used for enforcing uniqueness where necessary.
    *   **Recommendation:** This seems well-considered. Regularly review query plans (`EXPLAIN QUERY PLAN`) during development and with production data to identify missing or inefficient indices.

6.  **`TEXT` as JSON Store:**
    *   **Observation:** Several tables store JSON directly in `TEXT` columns.
    *   **Recommendation:** Ensure that the application layer correctly validates and parses these JSON strings. If specific JSON keys within these fields are frequently queried, consider extracting them into their own columns for indexing and easier querying.

7.  **Idempotency and Audit Trails:**
    *   **Observation:** `idempotency` table and `engine_audit` table are great for handling duplicate requests and providing a historical log.
    *   **Recommendation:** Ensure `idempotency_key` generation is robust.

8.  **Triggers:**
    *   **Observation:** Triggers are used effectively for timestamps, corporation rules, and bank transaction integrity.
    *   **Recommendation:** Triggers are powerful. Ensure they are well-documented and thoroughly tested.

### Specific Table/View Observations & Recommendations:

*   **`config` table:** Good pattern for single-row configuration.
*   **`players` table:** Contains many player-related fields.
    *   **Recommendation:** Consider breaking out some aspects into separate tables (e.g., `player_status`, `player_stats`) for better normalization and clarity if fields are often `NULL` or represent distinct concepts.
*   **`ships` table:** Good `CONSTRAINT check_current_cargo_limit`.
    *   **Recommendation:** For a fixed set of cargo types, the current denormalized approach is fine. For a growing list, a separate `ship_cargo` table would be more flexible.
*   **`planettypes` table:** Very wide with `citadelUpgradeTime_lvlX` and `citadelUpgradeOre_lvlX`.
    *   **Recommendation:** This is a prime candidate for vertical normalization into a `citadel_level_stats` table (`(planet_type_id, level, upgrade_time, ore_cost, organics_cost, ...)`) for easier scalability.
*   **`bank_accounts` / `bank_tx`:** The generic `owner_type`/`owner_id` pattern is flexible and well-implemented with triggers. This is a strong, scalable design.
*   **`commodity_orders` / `commodity_trades`:** Well-designed for a central exchange.
*   **`stocks` and related tables:** A sophisticated stock market model; looks robust.
*   **`charters` / `expeditions`:** Interesting game mechanics.
*   **Illicit Economy tables:** Creative and complex subsystem, well-structured.
*   **`engine_events`, `engine_commands`, `engine_audit`:** Excellent for event-driven architecture, idempotency, and operational visibility.

### Potential Areas for Future Improvement/Consideration:

1.  **Enforcing Constraints on `owner_type`/`owner_id`:** Be aware that the database cannot natively enforce referential integrity across multiple potential parent tables for this flexible pattern; application logic must handle this.
2.  **`strftime` Performance:** For very high-frequency writes, the cost of calling `strftime` might be non-negligible.
3.  **No `STAT` Tables:** If analytical queries or leaderboards become critical, dedicated summary/statistics tables might be beneficial.
4.  **`ports` commodity storage:** For consistency, `ports` could also use a `port_goods` table similar to `planet_goods` if new commodities are frequently added.

---

## Testing Strategy Recommendations

### Overall Impressions:
The presence of a `tests/` directory with Python scripts and JSON data suggests a functional or integration testing approach. This is crucial for a server application where many components interact. However, a comprehensive testing strategy typically involves a multi-layered approach.

### General Recommendations:

1.  **Introduce Unit Testing for C Modules:**
    *   **Recommendation:** Critical C modules and functions (`database.c`, `server_auth.c`, `server_ships.c` helper functions, utility functions in `common.c`) should have dedicated unit tests.
    *   **Benefits:** Isolation, Early Bug Detection, Faster Feedback, Regression Prevention, Easier Debugging.
    *   **Implementation Suggestion:** Use a lightweight C test framework (e.g., Unity, Check, cmocka, or a simple custom framework). Mock external dependencies when necessary.

2.  **Enhance Integration/Functional Tests (Python `server_tests.py`):**
    *   **Recommendation:** The existing Python tests are valuable for verifying end-to-end flows.
    *   **Improvements:** Test Coverage, Clear Test Cases, Data-Driven Tests, Scenario-Based Testing.

3.  **Database-Specific Testing:**
    *   **Recommendation:** Specific tests should target database logic due to the complexity of the SQLite schema.
    *   **Improvements:** Trigger Testing, Constraint Testing, View Accuracy, Transaction Integrity.

4.  **Performance and Load Testing:**
    *   **Recommendation:** Develop a suite of tests that simulate multiple concurrent users performing common actions.
    *   **Implementation Suggestion:** Use tools like `k6`, `JMeter`, or custom Python scripts with `locust`.

5.  **Chaos Engineering / Resilience Testing:**
    *   **Recommendation:** Simulate failures (database connection loss, network interruptions, resource exhaustion) to ensure graceful handling.

6.  **Fuzz Testing (for input parsing):**
    *   **Recommendation:** Use fuzz testing for network inputs (JSON commands) to uncover vulnerabilities or crashes due to malformed data.
    *   **Implementation Suggestion:** Use tools like `AFL++` or `libFuzzer`.

### Specific Improvements Based on Recent Work (Ship Destruction):

1.  **`handle_ship_destruction` (Unit & Integration):**
    *   **Unit Tests (C):** Test each component helper function and `handle_ship_destruction` itself with various `ship_kill_context_t` inputs.
    *   **Integration Tests (Python):** Verify self-destruct flow: ship destruction, player active ship clear, stat updates, XP reduction, escape pod/Big Sleep scenarios, and `ship.destroyed` event logging.

2.  **"Big Sleep" Logic (Integration):**
    *   **Login during Big Sleep:** Test login attempts while `big_sleep_until` is in the future. Assert `ERR_REF_BIG_SLEEP`.
    *   **Login after Big Sleep:** Test login when `big_sleep_until` has passed. Assert successful login, new starter ship, and `player.big_sleep_ended` event.
    *   **Daily Reset for Pods:** Test `podded_count_today` and `podded_last_reset` behavior across multiple days.

3.  **Escape Pod Logic (Integration):**
    *   **Successful Pod:** Test player with `has_escape_pod=true` and `podded_count_today < max_per_day` destroyed. Verify new escape pod creation and respawn.
    *   **Pod Limit Exceeded:** Test player with `has_escape_pod=true` but `podded_count_today >= max_per_day` destroyed. Verify "Big Sleep" is triggered.
    *   **No Escape Pod:** Test player with `has_escape_pod=false` destroyed. Verify "Big Sleep" is triggered.

4.  **Configuration Testing:**
    *   **Recommendation:** Create tests that verify system behavior with different `g_cfg.death.*` values.

### Recommendations for the `tests/` directory:

*   **Structure:** Break down `server_tests.py` into smaller, focused modules.
*   **Documentation:** Ensure tests are well-commented.
*   **CI/CD Integration:** Automate test execution as part of a Continuous Integration pipeline.

---

## Client-Side Recommendations: `client/` Directory

### Overall Impressions:
The presence of `client.py` and `menus.json` suggests a text-based or simple GUI client, likely interacting with the C server. Given the game's nature (space trading), a command-line or ncurses-style interface is probable. Python is a good choice for rapid client development, especially for prototyping or text-based interfaces.

### General Recommendations:

1.  **Protocol Definition and Client-Server Contract:**
    *   **Recommendation:** Formalize the communication protocol between the Python client and the C server. This ensures clear boundaries and expectations.
    *   **Improvements:**
        *   **Versioned API:** If not already, consider versioning the API.
        *   **Standardized Message Formats:** Ensure all messages conform to a strict JSON schema.
        *   **Documentation:** Maintain clear documentation of all client-server interactions.

2.  **Robust Error Handling and User Feedback:**
    *   **Recommendation:** The client should gracefully handle all possible server errors (including the newly implemented `ERR_REF_BIG_SLEEP`).
    *   **Improvements:**
        *   **Meaningful Messages:** Translate raw error codes into user-friendly messages.
        *   **Resilience:** Implement retry mechanisms and clear disconnection/reconnection logic.
        *   **Input Validation:** Perform client-side validation before sending requests.

3.  **Asynchronous Communication:**
    *   **Recommendation:** For a responsive client, asynchronous communication is crucial.
    *   **Improvements:**
        *   **Non-blocking I/O:** Use Python's `asyncio` for network communication.
        *   **Event-Driven Architecture:** Design the client to consume server events asynchronously.

4.  **UI/UX Considerations (if it's a console client):**
    *   **Recommendation:** Even for text-based interfaces, a good user experience is important.
    *   **Improvements:**
        *   **Clear State Display:** Always show the player's current status.
        *   **Command History/Completion:** Implement features common in CLI tools.
        *   **Pagination:** For lists, implement pagination.
        *   **Color/Formatting:** Use ANSI escape codes to enhance readability.

5.  **Configuration Management (`menus.json`):**
    *   **Recommendation:** `menus.json` is a good way to manage UI elements outside of code.
    *   **Improvements:**
        *   **Schema Validation:** Ensure a clear schema for `menus.json` and validate against it.
        *   **Dynamic Updates:** Consider if menus could be updated dynamically from the server.

6.  **Security Considerations:**
    *   **Recommendation:** The client still has security responsibilities.
    *   **Improvements:**
        *   **No Hardcoded Credentials:** Ensure no sensitive information is hardcoded.
        *   **Secure Session Handling:** Properly store and transmit session tokens.

7.  **Client-Side Testing:**
    *   **Recommendation:** The client needs its own test suite.
    *   **Improvements:**
        *   **Unit Tests:** Test individual client functions.
        *   **Integration Tests:** Test the client's interaction with a *mocked* or *test instance* of the server.

### Specific Recommendations Related to Recent Server Changes:

1.  **"Big Sleep" User Experience:**
    *   **Recommendation:** When `ERR_REF_BIG_SLEEP` is received, the client should display a clear message indicating the player's status and the `big_sleep_until` timestamp.
    *   **Implementation:** The client needs to parse `err_data` from the error envelope and present it.

2.  **Ship Destruction Feedback:**
    *   **Recommendation:** After a player's ship is destroyed, the client should clearly inform the user about the outcome (escape pod or "Big Sleep").
    *   **Implementation:** The `ship.self_destruct.confirmed` response might need to carry more information about the outcome.

3.  **Event Consumption:**
    *   **Recommendation:** The client should listen for `player.big_sleep_ended` and `ship.destroyed` engine events to provide real-time updates or notifications.

---

## AI QA Bot Recommendations: `ai_player/` Directory

### Overall Impressions:
The `ai_player` directory outlines a robust, Python-based AI agent, seemingly designed to act as an automated QA tester for the game server. Its structure, including LLM integration, planning, state management, and bug reporting, is impressive. This bot serves as both a functional test and potentially a performance/stress test tool.

### General Recommendations for the AI QA Bot:

1.  **Alignment with Server Protocol and Data Models:**
    *   **Recommendation:** The `ai_player`'s understanding of the game's protocol (`parse_protocol.py`, `protocol_string.txt`) and underlying data models must be kept in perfect sync with the C server.
    *   **Improvements:**
        *   **Automated Protocol Generation/Validation:** Derive `protocol_string.txt` or a similar specification directly from the server's C headers or a shared IDL.
        *   **Schema Enforcement:** Ensure the AI's internal representation strictly adheres to the SQLite schema and client-server JSON structures.

2.  **State Management Robustness:**
    *   **Recommendation:** The `state_manager.py` and `state.json` are critical for accurately reflecting the server's authoritative state.
    *   **Improvements:**
        *   **Idempotent State Updates:** Handle potential out-of-order or duplicate messages.
        *   **State Reconciliation/Correction:** Implement mechanisms to detect and reconcile discrepancies with the server's state.
        *   **Snapshotting and Rollback:** Enable saving and restoring AI state for debugging and re-runs.

3.  **Intelligent Planning and Decision Making (`planner.py`, `bandit_policy.py`):**
    *   **Recommendation:** These modules are the "brain" of the AI.
    *   **Improvements:**
        *   **Goal-Oriented Planning:** Define and pursue diverse goals.
        *   **Adaptability:** Learn from past interactions to optimize test paths.
        *   **Exploration vs. Exploitation:** Balance exploring new mechanics with exploiting known bug areas.
        *   **LLM Integration (`llm_client.py`):** Clearly define and guard against LLM hallucinations affecting test validity.

4.  **Comprehensive Bug Reporting (`bug_reporter.py`, `bug_reports/`):**
    *   **Recommendation:** Automated bug reporting is a standout feature.
    *   **Improvements:**
        *   **Actionable Reports:** Ensure reports contain necessary context (steps, logs, timestamps).
        *   **Categorization/Prioritization:** Implement logic to categorize and prioritize bugs.
        *   **Deduplication:** Prevent duplicate reports.
        *   **Integration with Issue Tracker:** Automate GitHub issue creation.

5.  **Test Case Diversity and Coverage:**
    *   **Recommendation:** The AI bot should aim to cover happy paths, edge cases, stress, and security vulnerabilities.
    *   **Improvements:**
        *   **Negative Testing:** Attempt invalid actions and malformed inputs.
        *   **Concurrency/Multi-AI Scenarios:** Deploy multiple AI players concurrently.
        *   **Long-Running Tests:** Uncover memory leaks and performance degradation.

6.  **Observability and Debugging:**
    *   **Recommendation:** Easy to understand AI actions, decisions, and failures.
    *   **Improvements:**
        *   **Detailed Logging (`ai_player.log`):** Log decisions, actions, responses, and state changes.
        *   **Visualization:** For complex planning, visualize AI state or path.
        *   **Reproducibility:** Record interactions for exact reproduction of bugs.

### Specific Recommendations Related to Recent Server Changes (Ship Destruction):

1.  **Testing "Big Sleep" Login Flow:**
    *   **Recommendation:** The AI should explicitly test the "Big Sleep" mechanic.
    *   **AI Actions:** Self-destruct, attempt immediate login (expect `ERR_REF_BIG_SLEEP`), parse `big_sleep_until`, wait, re-login (expect success, new starter ship), and verify `player.big_sleep_ended` event.

2.  **Testing Escape Pod Flow:**
    *   **Recommendation:** Test successful escape pod deployment and fallbacks to "Big Sleep".
    *   **AI Actions:** Acquire ship with `has_escape_pod=true`, trigger destruction, verify respawn in escape pod. Test exceeding `max_per_day` limit.

3.  **Stat Update Verification:**
    *   **Recommendation:** After ship destruction, verify player statistics (`times_blown_up`, `experience`).
    *   **AI Actions:** Query player status before and after self-destruction, assert expected changes.

4.  **Configuration Parameter Testing:**
    *   **Recommendation:** Run tests with varying `death.*` config values to ensure server behavior matches.
