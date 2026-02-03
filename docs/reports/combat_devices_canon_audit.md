# Combat Devices Canon Audit Report

**Date:** 2026-01-31
**Subject:** TradeWars 2002 Canon Parity Audit — Fighters, Mines, and Limpets

## 1. Device Inventory

### Fighters
- **Existence:** Stored in `sector_assets` table (`asset_type = 2`). Can be owned by players or corporations.
- **Triggering:** `apply_sector_fighters_on_entry` exists in `src/server_combat.c` but is currently **dead code** (not called by movement handlers).
- **Deployment:** Managed via `combat.deploy_fighters` with TOLL/DEFEND/ATTACK modes.

### Mines
- **Types:** ARMID (1) and Limpet (4) are distinguished in `src/server_combat.c`.
- **Triggers:** `apply_armid_mines_on_entry` and `apply_limpet_mines_on_entry` exist but are **dead code**.
- **Persistence:** Persist in `sector_assets` until swept or recalled. Sequence/chain triggering is not implemented.

### Limpets
- **Attachment:** Stored in `limpet_attached` table when triggered.
- **Effects:** No current mechanical impact (no fuel drain or movement penalty).
- **Removal:** `cmd_combat_scrub_mines` exists as a stub; no specific removal equipment logic found.

---

## 2. Canon Checklist Table

| Item | Requirement | Status | Evidence |
| :--- | :--- | :--- | :--- |
| 1 | **Fighter ownership/affiliation** | ⚠️ Partial | `sector_assets` has `owner_id` and `corporation_id`. |
| 2 | **Automatic fighter combat** | ❌ Missing | Logic exists in `src/server_combat.c` but is not integrated into movement. |
| 3 | **FedSpace restricted/policed** | ⚠️ Partial | `iss_summon` stub in `cmd_combat_deploy_fighters`. |
| 4 | **Fighter persistence/consumption** | ✅ Implemented | Data persists; consumption logic in `apply_sector_fighters_on_entry`. |
| 5 | **Mine types and triggers** | ⚠️ Partial | Logic exists but is not triggered on sector entry. |
| 6 | **Mine sweeping** | ⚠️ Partial | `combat.sweep_mines` is a free, instant stub. |
| 7 | **Mine persistence & chaining** | ⚠️ Partial | Persist in DB; no sequential triggering. |
| 8 | **Limpet attachment** | ✅ Implemented | `limpet_attached` correctly tracks ship/owner pairs. |
| 9 | **Limpet effects (Ongoing)** | ❌ Missing | No impact on turns, fuel, or speed. |
| 10 | **Limpet detection & removal** | ❌ Missing | Detection/Removal equipment and logic are absent. |
| 11 | **No instant/free removal** | ❌ Missing | Current sweeping/recall stubs have zero resource/turn cost. |
| 12 | **No infinite loops** | ⚠️ Partial | Primitive damage logic prevents obvious loops. |
| 13 | **Ownership enforcement** | ❌ Missing | Players can sweep/recall own assets freely. |

---

## 3. Behaviour Gaps

### Core Movement Hooks (Canon-Critical)
The most significant gap is that **sector entry does not trigger combat**. Functions like `apply_sector_fighters_on_entry` are implemented but isolated from the `move.warp` and `move.transwarp` call chains.
- **Location:** `src/server_universe.c`: `cmd_move_warp` needs to invoke hazard checks.

### Limpet Mechanics (Canon-Critical)
Limpets are currently "cosmetic" attachments. They lack the canonical fuel drain or movement visibility pings to owners.
- **Location:** `src/server_players.c`: `h_consume_player_turn` should check for attached limpets.

### Resource/Turn Costs (Canon-Critical)
Fighter recall and mine sweeping are currently "free" actions that do not consume turns or check for specialized equipment (e.g., Mine Sweepers).
- **Location:** `src/server_combat.c`: `cmd_combat_sweep_mines`.

---

## 4. Test Gaps

1. **Auto-Combat Integration Test:** `test_fighter_trigger_on_warp`
   - Assert that warping into a sector with hostile fighters results in ship damage and fighter consumption.
2. **Mine Triggering Test:** `test_mine_detonation_on_entry`
   - Assert that hostile mines detonate upon player entry.
3. **Limpet Persistence Test:** `test_limpet_persistence_across_warp`
   - Assert that an attached limpet remains on the ship after moving through multiple sectors.
4. **Sweeping Cost Test:** `test_sweep_consumes_turns`
   - Assert that sweeping mines consumes at least one turn.

---

## 5. Key Code Pointers

- **Combat Resolver:** `apply_armid_damage_to_ship` (`src/server_combat.c:217`)
- **Entry Triggers:** `apply_sector_fighters_on_entry` (`src/server_combat.c:115`)
- **Asset Storage:** `db_combat_insert_sector_fighters` (`src/db/repo/repo_combat.c:223`)
- **Limpet Registry:** `db_combat_attach_limpet` (`src/db/repo/repo_combat.c:403`)
- **Towing Logic:** `cmd_ship_tow` (`src/server_ships.c:613`)
