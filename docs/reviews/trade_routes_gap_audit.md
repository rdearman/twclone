# Computer Memory Trade Routes - Gap Audit Report

**Date:** 2026-01-29
**Auditor:** Gemini Agent

## Overall Verdict
The "Computer Memory Trade Routes" feature is **mostly implemented** and functional, adhering to the core requirements of using persistent player knowledge rather than live market data. The database schema, docking hooks, and route calculation logic are in place. However, there are **minor gaps** regarding performance safeguards (unbounded C loops), specific test coverage (verifying scans do *not* add knowledge), and a missing protocol flag indicating the use of the full universe graph for pathfinding. The "scan" requirement is technically met as standard scans do not record knowledge, though the `trade.port_info` command does record knowledge if called from the same sector, which may be a design nuance to confirm.

## Gap Checklist

| Gap ID | Description | Status | Notes |
| :--- | :--- | :--- | :--- |
| 1 | Port knowledge persistence | ✅ Implemented | `player_known_ports` table exists. |
| 2 | Dock hook populates knowledge | ✅ Implemented | `cmd_dock_status` updates DB on success. |
| 3 | Scans do NOT populate knowledge | ✅ Implemented | Standard scans (`sector.scan`, `density.scan`) do not write to DB. |
| 4 | Route calculation exists | ✅ Implemented | BFS-based logic in `repo_players.c`. |
| 5 | Bounded performance safeguards | ⚠️ Partial | BFS depth is limited, but candidate pair count is unbounded in C loop. |
| 6 | Known vs Full Graph | ⚠️ Partial | Uses Full Graph (Option A) but lacks the required response flag. |
| 7 | RPC exists and is wired | ✅ Implemented | `player.computer.recommend_routes`. |
| 8 | Protocol docs updated | ✅ Implemented | Included in `20_Player_Commands.md`. |
| 9 | Tests exist | ⚠️ Partial | Positive tests exist; missing negative test for Scans. |

## Detailed Evidence

### 1. Port Knowledge Persistence
*   **Evidence:** `sql/pg/000_tables.sql` (lines 1538-1543) defines `CREATE TABLE player_known_ports`.
*   **Implementation:** The table stores `player_id` and `port_id`. It does not snapshot commodity modes, meaning the query joins with the live `ports` table. This satisfies "based on ports docked at" but relies on live port modes (acceptable if port modes are static/class-based).

### 2. Dock Hook Populates Knowledge
*   **Evidence:** `src/server_ports.c`: `cmd_dock_status`
    *   Line 1528: Checks for `action="dock"`.
    *   Line 1567: Calls `repo_players_record_port_knowledge` inside the success block (`new_ported_status > 0`).
*   **Verification:** Knowledge is only recorded upon successful docking.

### 3. Scans do NOT Populate Knowledge
*   **Evidence:** `src/server_universe.c`: `cmd_sector_scan`, `cmd_move_scan`, `build_sector_scan_json`.
    *   Code review confirms these functions read data but **do not** call `repo_players_record_port_knowledge`.
*   **Note:** `cmd_trade_port_info` (`src/server_ports.c`) *does* record knowledge. This command can be called if the player is in the same sector. If "scanning" implies any remote query, this is a loophole. If "scanning" refers to `sector.scan`, it is compliant.

### 4. Route Calculation
*   **Evidence:** `src/db/repo/repo_players.c`: `repo_players_get_recommended_routes`.
    *   Selects pairs from `player_known_ports`.
    *   Joins `port_trade` to match BUY/SELL modes.
    *   Uses `h_repo_players_bfs_fast` for pathfinding.

### 5. Bounded Performance Safeguards
*   **Implemented:** `max_hops_between` and `max_hops_from_player` limit BFS depth.
*   **Missing:** The SQL query `Q_ROUTES` selects *all* compatible pairs of known ports.
    *   Line 685: `while (db_res_step(res, &err))` loops through all pairs.
    *   If a player knows N ports, this loop can run N*(N-1) times.
    *   Inside the loop, it runs BFS twice.
    *   **Risk:** DoS if a player visits thousands of ports.
    *   **Fix:** Add `LIMIT` to the SQL or a hard cap counter in the C loop.

### 6. "Known Space Only" vs Full Graph
*   **Implemented:** Option A (Full Universe Graph).
    *   `repo_universe_get_all_warps` retrieves the entire warp map.
    *   Pathfinding uses this complete map.
*   **Missing:** The requirement states: "If A is used, confirm the response includes a flag or wording equivalent to 'based on known ports only; path may include unknown sectors'".
    *   `src/server_players.c` does not add such a flag to the JSON response.

### 7. RPC Exists
*   **Evidence:** `src/server_players.c`: `cmd_player_computer_recommend_routes`.
*   **Wired:** `src/server_loop.c` (implied registry, verified via protocol doc presence).

### 8. Protocol Docs
*   **Evidence:** `docs/PROTOCOL.v3/20_Player_Commands.md`
    *   Section 1.4 `player.computer.recommend_routes`.
    *   Correctly documents arguments and response structure.

### 9. Tests
*   **Evidence:** `tests.v2/suite_computer_loops.json`
    *   Verifies empty routes initially.
    *   Verifies route appears after docking at two compatible ports.
*   **Missing:** A test case that performs a `sector.scan` (or `move.scan`) on a port *without* docking, and asserts that the port does *not* appear in recommended routes.

## Minimal Next Steps

1.  **Add Response Flag:** Update `cmd_player_computer_recommend_routes` in `src/server_players.c` to include `"pathing": "full_universe"` or a warning string in the JSON response.
2.  **Cap Loop Iterations:** Add a hard limit (e.g., `max_pairs_checked = 2000`) in the `while` loop of `repo_players_get_recommended_routes` in `src/db/repo/repo_players.c` to prevent server stalls.
3.  **Add Negative Test:** Create `tests.v2/suite_computer_loops_scan_negative.json` to verify scans don't leak knowledge.

## How to Run Tests
```bash
# Run the existing positive test suite
python3 tests.v2/json_runner.py tests.v2/suite_computer_loops.json
```
