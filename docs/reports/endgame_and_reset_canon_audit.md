# Endgame Pressure and Reset Canon Audit Report

**Date:** 2026-01-31
**Subject:** TradeWars 2002 Canon Parity Audit — Pressure, Saturation, and Resets

## 1. Universe Growth Summary

The universe in `twclone` is designed to accumulate assets over time, leading to eventual saturation.

- **Assets**: Fighters and mines are stored in `ships`, `planets`, and `sector_assets`. Planets are stored in the `planets` table.
- **Saturation**: The total number of planets is hard-capped (default 300) via a database trigger (`trg_planets_total_cap_before_insert`). Ships are capped at 1000 in configuration.
- **Pressure**: Currently, asset accumulation does **not** create traversal pressure because sector entry hazards (hostile fighters/mines) are not currently invoked by the movement handlers (`move.warp`, `move.transwarp`). This is a significant canon parity gap.

---

## 2. Canon Checklist Table

| Item | Requirement | Status | Evidence |
| :--- | :--- | :--- | :--- |
| 1 | **Asset Accumulation Pressure** | ⚠️ Partial | Assets accumulate, but entry hazards are currently "dead code" (`src/server_combat.c:115`). |
| 2 | **No Automatic Cleanup** | ✅ Implemented | No automated tasks found that clear non-derelict fighters/mines/planets. |
| 3 | **Increasing Marginal Cost** | ❌ Missing | No logic found that increases cost of expansion based on universe age/state. |
| 4 | **Wealth Concentration** | ✅ Implemented | Social/Economic dominance is possible; rankings are observational. |
| 5 | **Diminishing Returns** | ⚠️ Partial | Planet and ship caps prevent infinite scaling, but specific "diminishing returns" on trade/prod are missing. |
| 6 | **No Explicit Win Condition** | ✅ Implemented | No score caps or kill limits that trigger server termination. |
| 7 | **Scoreboards / Rankings** | ⚠️ Partial | `player.rankings` RPC exists but is currently a stub (`src/server_players.c:55`). |
| 8 | **Reset Assumption** | ✅ Implemented | Implicitly assumed; `rig_db.py` uses `TRUNCATE` for full wipes during testing. |
| 9 | **Reset Scope** | ✅ Implemented | Wipes everything including players/ships/planets/assets (`tests.v2/rig_db.py:51`). |
| 10 | **SysOp Control** | ✅ Implemented | Extensive SysOp RPCs for live tuning and operations (`src/server_sysop.c`). |

---

## 3. Endgame Pressure Analysis

The server currently lacks the "natural pressure" that leads to a reset in canon TW2002.

- **Non-Functional Hazards**: While players can deploy thousands of fighters and mines, these assets do not engage incoming ships. This removes the primary late-game obstacle: the "hostile wall" that forces players to use strategy or resources to clear paths.
- **Infinite Life**: Without active hazards or decay (except for limpets), the universe can stay in a "stagnant peak" state indefinitely until a manual reset.
- **Configurable Caps**: Planet and ship caps prevent the database from ballooning, but they serve as technical safety valves rather than gameplay pressure mechanics.

---

## 4. Reset Model Summary

Resets are currently handled as an administrative, out-of-band operation.

- **Mechanism**: There is no `universe.reset` RPC command. Resets are performed by truncating the database tables.
- **Testing Logic**: `rig_db.py` implements the "standard reset" by truncating: `sessions`, `corp_members`, `citadels`, `planets`, `ships`, `ship_ownership`, `bank_accounts`, `turns`, `players`, `corporations`, `sector_assets`, `entity_stock`.
- **Preservation**: The current reset model preserves nothing (not even player accounts), which aligns with a "Big Bang" event but differs from "Tournament" resets where accounts might persist.

---

## 5. Test Gaps

1. **Saturation Cap Test**: `test_planet_cap_enforcement`
   - Assert that creating a planet beyond `max_total_planets` returns `ERR_UNIVERSE_FULL`.
2. **Derelict Cleanup Test**: `test_derelict_towing_persistence`
   - Verify that unowned ships are towed out of FedSpace correctly by `h_fedspace_cleanup`.
3. **Rankings Integrity Test**: `test_rankings_sort_order`
   - Once implemented, verify that rankings correctly reflect net worth (credits + asset value).

---

## 6. Key Code Pointers

- **Planet Cap Trigger**: `fn_planets_total_cap_before_insert` (`sql/pg/092_seed_gameplay.sql:26`)
- **FedSpace Cleanup**: `h_fedspace_cleanup` (`src/server_cron.c:558`)
- **SysOp Interface**: `src/server_sysop.c`
- **Reset Logic (Test)**: `tests.v2/rig_db.py:51`
- **Rankings Stub**: `cmd_player_rankings` (`src/server_players.c:55`)
