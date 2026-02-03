# Scanner Information Canon Audit Report

**Date:** 2026-01-31
**Subject:** TradeWars 2002 Canon Parity Audit — Scanners and Information Disclosure

## 1. Scanner Inventory

The following scanner-related commands are implemented in the server:

| Command | Protocol Name | Handler | Description |
| :--- | :--- | :--- | :--- |
| **Density Scan** | `sector.scan.density` | `cmd_sector_scan_density` | Aggregate density of current and adjacent sectors. |
| **Sector Scan** | `sector.scan` | `cmd_sector_scan` | Detailed view of the current sector. |
| **Fast Scan** | `move.scan` | `cmd_move_scan` | Detailed view of the current sector (alias for sector.scan). |
| **Sector Info** | `sector.info` | `cmd_sector_info` | Detailed view of a specified sector. |

---

## 2. Canon Checklist Table

| Item | Requirement | Status | Evidence |
| :--- | :--- | :--- | :--- |
| 1 | **Density Scan** (Presence only) | ✅ Implemented | `repo_universe_get_sector_density` calculates an aggregate integer score. |
| 2 | **Sector Scan** (Immediate only) | ⚠️ Partial | Correctly lists objects, but leaks port `type` (class) via `db_ports_at_sector_json`. |
| 3 | **Long Range Scan** (Topology) | ❌ Missing | No LR Scan command or handler exists in `src/`. |
| 4 | **Holo Scan** / Advanced | ❌ Missing | Not implemented. |
| 5 | **Knowledge Persistence** | ✅ Implemented | `cmd_sector_scan` and `cmd_move_scan` do not call persistence logic. |
| 6 | **Protocol Parity** | ⚠️ Partial | Docs imply `move.scan` is a summary, but implementation returns a full detailed scan. |
| 7 | **Anti-leak Invariants** | ❌ Missing | Port `type` (class) is directly returned in sector scans, allowing inference of trade modes. |

---

## 3. Leakage Analysis

### Direct Leaks (Port Class)
The function `db_ports_at_sector_json` (used by all detailed scans) explicitly selects the `type` column from the `ports` table:
- **Query:** `SELECT port_id, name, type FROM ports WHERE sector_id = {1};`
- **Result:** The `type` field (integer 1-8) directly maps to canonical port classes (e.g., SBB, SSB). This allows players to know exactly what a port buys and sells without docking.

### Information Symmetry Loophole
`cmd_sector_info` (protocol `sector.info`) allows querying *any* sector by ID without distance checks. Because it uses the same `build_sector_scan_json` helper as local scans, it effectively acts as a universe-wide "Super Scanner," leaking port locations and classes for every sector.

---

## 4. Persistence Boundary Verification

The investigation confirmed that scanning commands correctly avoid the persistence layer:
- **Verified:** `cmd_sector_scan`, `cmd_move_scan`, and `cmd_sector_scan_density` do **not** call `repo_players_record_port_knowledge`.
- **Contrast:** `cmd_trade_port_info` (`port.info` / `port.status`) **does** call persistence. This is canonical when docked, but problematic when used as a remote query.

---

## 5. Test Gaps

1. **Information Leakage Test**: `test_scan_does_not_leak_class`
   - Assert that the `ports` array in a `sector.scan` response does **not** contain the `type` field.
2. **Proximity Enforcement Test**: `test_sector_info_proximity`
   - Assert that `sector.info` returns an error if the target sector is not adjacent or the current sector.
3. **Persistence Side-Effect Test**: `test_scan_no_persistence`
   - Assert that performing a `sector.scan` does not add a record to the `player_known_ports` (or equivalent) table.

---

## 6. Key Code Pointers

- **Response Builder:** `build_sector_scan_json` (`src/server_universe.c:591`)
- **Port Data Provider:** `db_ports_at_sector_json` (`src/db/repo/repo_cmd.c:3079`)
- **Density Engine:** `repo_universe_get_sector_density` (`src/db/repo/repo_universe.c:149`)
- **Persistence Boundary:** `repo_players_record_port_knowledge` (`src/db/repo/repo_players.c:424`)
