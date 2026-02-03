# Information Asymmetry and Fog-of-War Canon Audit Report

**Date:** 2026-01-31
**Subject:** TradeWars 2002 Canon Parity Audit — Information Disclosure Boundaries

## 1. Knowledge Surface Inventory

| RPC Command | Scope Exposed | Discovery Constraint |
| :--- | :--- | :--- |
| `sector.info` / `move.describe_sector` | Adjacency, ports, planets, ships, asset counts. | **None** (Global query allowed). |
| `port.info` / `port.describe` | Header, commodities (prices/stock), illegal status. | **None** (Global query allowed). |
| `planet.info` | Full planet record (inventory, citadel, etc.). | **None** (Global query allowed). |
| `move.pathfind` | Full path between any two sectors. | **Omniscient** (Uses full warp graph). |
| `player.computer.recommend_routes` | Trade loops between known ports. | **Partial** (Endpoints must be known). |
| `nav.bookmark.list` | Player-saved sector IDs and names. | **Private** (Per-player). |
| `notes.list` | Player-saved notes on ports/sectors/players. | **Private** (Per-player). |

---

## 2. Canon Checklist Table

| Item | Requirement | Status | Evidence |
| :--- | :--- | :--- | :--- |
| 1 | **Sector knowledge not global** | ❌ Missing | `cmd_move_describe_sector` allows querying any sector by ID. |
| 2 | **Port knowledge boundary** | ❌ Missing | `cmd_trade_port_info` allows global query and reveals buy/sell. |
| 3 | **Planet knowledge boundary** | ❌ Missing | `cmd_planet_info` reveals all fields including internals (`SELECT *`). |
| 4 | **Player-owned memory surfaces** | ✅ Implemented | `player_bookmarks`, `player_notes`, `player_avoid` tables are private. |
| 5 | **Trade route helpers (Personal only)** | ⚠️ Partial | Endpoints are filtered by `player_known_ports`, but distance is omniscient. |
| 6 | **Staleness / Decay economics** | ✅ Implemented | `daily_market_settlement` and `daily_stock_price_recalculation` exist. |
| 7 | **Explicit Staleness markers** | ❌ Missing | Responses lack "last observed" timestamps or markers. |
| 8 | **Anti-leak invariants** | ❌ Missing | Port types and planet internals leaked via global info/scan commands. |

---

## 3. Leakage Findings

### Direct Leaks
- **`planet.info`**: Implementation in `db_planet_get_details_json` uses `SELECT *`, exposing treasury, unassigned colonists, planetary shields, and military reaction levels to any player who knows the planet ID.
- **`sector.info`**: Reveals port and planet types (classes) globally, allowing players to map the universe's economic potential without moving.

### Indirect Leaks / Inference
- **Omniscient Pathfinding**: `move.pathfind` reveals if a path exists between any two sectors, effectively confirming the existence and connectivity of undiscovered regions.
- **Automatic Knowledge Recording**: `cmd_trade_port_info` calls `repo_players_record_port_knowledge` upon query, meaning simply querying a port via RPC (even if not visited) marks it as "known" for route recommendation purposes.

---

## 4. Staleness Findings

- **Time-based Change**: Port stock and prices are updated daily via `h_daily_market_settlement` and `h_daily_stock_price_recalculation`.
- **Visibility**: The player is **not** informed that information might be stale. RPC responses like `trade.port_info` return current DB state as if it were a real-time observation, even if the player is not present.

---

## 5. Test Gaps

1. **Information Proximity Test**: `test_port_info_proximity_gate`
   - Assert that `port.info` returns an error if the player is not in the same sector.
2. **Planet Detail Leak Test**: `test_planet_info_privacy_gate`
   - Assert that `planet.info` only returns basic fields (name, type) if the player has not landed.
3. **Omniscient Pathfinding Block**: `test_pathfind_discovery_gate`
   - Assert that `move.pathfind` only uses sectors/warps previously seen by the player.

---

## 6. Key Code Pointers

- **Sector disclosure**: `build_sector_scan_json` (`src/server_universe.c:591`)
- **Port disclosure**: `cmd_trade_port_info` (`src/server_ports.c:1242`)
- **Planet disclosure**: `db_planet_get_details_json` (`src/db/repo/repo_cmd.c:3435`)
- **Pathfinding**: `cmd_move_pathfind` (`src/server_universe.c:955`)
- **Knowledge Persistence**: `player_known_ports` / `player_visited_sectors` (`sql/pg/000_tables.sql:1538`)
