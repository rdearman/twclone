# Planet Combat & Citadel Canon Audit Report

**Date:** 2026-01-31
**Subject:** TradeWars 2002 Canon Parity Audit — Planets, Combat, and Citadels

## 1. Planet & Citadel Model Summary

### Data Model
- **Planets Table**: Stored in `planets`. Key fields include `owner_id`, `owner_type` (player/corporation), `fighters`, `population`, `citadel_level`, and resource stores (`ore_on_hand`, `organics_on_hand`, `equipment_on_hand`).
- **Citadels Table**: Stored in `citadels`. Tracks `level`, `militaryReactionLevel`, `planetaryShields`, and construction state (`construction_status`, `construction_end_time`).
- **Requirements**: `citadel_requirements` table exists but is currently **bypassed** by the repository layer, which queries requirements directly from `planettypes` columns (e.g., `citadelUpgradeOre_lvl1`).

### Invariants
- Planets are uniquely identified by `planet_id`.
- Ownership is strictly enforced for landing and management.
- Terra (Sector 1) has hardcoded immunity and instant-death sanctions for attackers.

---

## 2. Canon Checklist Table

| Item | Requirement | Status | Evidence |
| :--- | :--- | :--- | :--- |
| 1 | **Planet Creation** (Genesis) | ✅ Implemented | `cmd_planet_genesis_create` in `src/server_planets.c`. Consumes torpedo and turn. |
| 2 | **Planet Ownership** | ✅ Implemented | `planets.owner_id` and `owner_type` fields. Enforced in `cmd_planet_land`. |
| 3 | **Planet Persistence** | ✅ Implemented | Stored in `planets` table; persists across sessions. |
| 4 | **Planetary Fighters** | ✅ Implemented | `planets.fighters` column. Auto-defend in `cmd_combat_attack_planet`. |
| 5 | **Combat Resolution** | ✅ Implemented | Immediate resolution in `cmd_combat_attack_planet`. |
| 6 | **Citadel Presence/Levels** | ✅ Implemented | `citadels` table and `citadel_level` column. Max level 6. |
| 7 | **Citadel Command Surface** | ⚠️ Partial | `citadel.build/upgrade` implemented. Specific sub-commands (transporter, interdictor) are missing or stubs. |
| 8 | **Citadel Combat Impact** | ⚠️ Partial | Shields (L5+) and Military Reaction (L2+) implemented but use potentially mismatched SQL column names. |
| 9 | **Planet Capture** | ✅ Implemented | `db_planets_capture` called when fighters reach 0 during attack. |
| 10 | **Planet Destruction** | ❌ Missing | No logic found for destroying a planet entity (only ship destruction upon attacking Terra). |
| 11 | **FedSpace Constraints** | ✅ Implemented | `cmd_planet_genesis_create` blocks creation in S1-10. Terra protection enforced. |
| 12 | **No free planet farming** | ✅ Implemented | Genesis torpedoes are finite; creation consumes turns. |
| 13 | **Ownership Enforcement** | ✅ Implemented | `require_auth` and ownership checks present in all mutation commands. |

---

## 3. Behaviour Gaps

### Missing Upgrade Completion (Canon-Critical)
While `cmd_citadel_upgrade` successfully starts construction and sets `construction_status = 'upgrading'`, there is **no corresponding logic** in the cron system or server to finalize the upgrade once the timer expires. Citadels will remain in the 'upgrading' state indefinitely.
- **Location**: `src/server_cron.c` or a dedicated construction reaper is missing.

### Schema/Repository Mismatch (Critical Bug)
The repository query `db_planets_get_citadel_defenses` in `src/db/repo/repo_planets.c` uses snake_case column names (`planetary_shields`, `military_reaction_level`), but the schema in `sql/pg/000_tables.sql` defines them as camelCase (`planetaryShields`, `militaryReactionLevel`). This will cause SQL execution errors during planet combat.
- **Location**: `src/db/repo/repo_planets.c:93`.

### No Planet Destruction (Non-Canon Simplification)
Planets can be captured but cannot be wiped from the universe map. In canon TW2002, planets can be destroyed via sufficient bombardment or atomic detonators.
- **Location**: `src/server_planets.c` lacks `cmd_planet_bombard`.

---

## 4. Test Gaps

1. **Citadel Upgrade Lifecycle Test**: `test_citadel_completion`
   - Assert that an 'upgrading' citadel actually increases its level after the `construction_end_time` passes. (Currently expected to fail).
2. **Planet Combat Accuracy Test**: `test_planet_combat_shields`
   - Verify that citadel shields correctly absorb damage. This test would likely reveal the snake_case/camelCase mismatch mentioned above.
3. **Multi-Owner Landing Test**: `test_planet_landing_permissions`
   - Assert that corp members can land on corp-owned planets, but non-members are refused.

---

## 5. Key Code Pointers

- **Combat Handler**: `cmd_combat_attack_planet` (`src/server_planets.c:65`)
- **Upgrade Handler**: `cmd_citadel_upgrade` (`src/server_citadel.c:67`)
- **Creation Handler**: `cmd_planet_genesis_create` (`src/server_planets.c:1427`)
- **Citadel Repo**: `src/db/repo/repo_citadel.c`
- **Planet Repo**: `src/db/repo/repo_planets.c`
- **Requirement Source**: `citadel_requirements` table vs `planettypes` columns.
