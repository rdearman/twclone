# FedSpace Enforcement Audit Report

**Date:** 2026-01-31
**Subject:** Canon Parity for FedSpace Enforcement (Sectors 1-10)

## 1. FedSpace Definition
The game defines FedSpace using a hardcoded sector range and a database flag.

- **Sector Range:** Sectors `1` through `10` inclusive are designated as FedSpace.
  - Evidence: `src/server_cron.c` defines macros:
    ```c
    #define FEDSPACE_SECTOR_START 1
    #define FEDSPACE_SECTOR_END 10
    ```
  - Evidence: `src/server_cmds.c` helper function `is_fedspace_sector(int sector_id)` returns true for range `[1, 10]`.
- **Database Flag:** The `sectors` table includes an `is_fedspace` boolean column.
  - Evidence: `src/db/repo/repo_cmd.c` function `db_is_sector_fedspace` queries `SELECT 1 FROM sectors WHERE sector_id = {1} AND is_fedspace = TRUE;`.
- **Seed Data:** Sectors 1-10 are seeded with the name "Fedspace X" and `is_fedspace = TRUE`.
  - Evidence: `sql/pg/091_seed_essential.sql`.

## 2. Canon Baseline Checklist

| Rule | Requirement | Status | Evidence |
| :--- | :--- | :--- | :--- |
| 1 | **Protection Eligibility:** Alignment >= 0 and EXP < 1000. | ✅ Implemented | `h_fedspace_cleanup` in `src/server_cron.c` populates `eligible_tows` and identifies players with `experience >= 1000` or `alignment < 0` for removal from S1-S10. |
| 2 | **Captain Z Triggers:** Deployment or Attack in FedSpace. | ⚠️ Partial | `cmd_combat_deploy_fighters` in `src/server_combat.c` calls `iss_summon()` if in S1-S10. However, `handle_ship_attack` in `src/server_combat.c` lacks this call. `iss_summon` is currently a logging stub. |
| 3 | **Overnight Towing:** Parked with >50 fighters. | ✅ Implemented | `h_fedspace_cleanup` in `src/server_cron.c` identifies ships with `fighters > 49` (50+) and owner inactivity > 12 hours (`stale_cutoff`). |
| 4 | **Wanted Surface:** Most Wanted / Bounties / Rewards. | ✅ Implemented | `bounties` table and `v_bounty_board` view exist. RPCs `bounty.post_federation` and `bounty.list` are implemented in `src/server_cmds.c`. |

## 3. What’s Missing

### Enforcement Logic (Captain Z / ISS)
- **Attack Trigger:** `handle_ship_attack` in `src/server_combat.c` does not check for FedSpace or trigger `iss_summon`.
- **Stub Implementation:** `iss_summon` in `src/server_combat.c` is only a log statement:
  ```c
  static void iss_summon (int sector_id, int player_id) {
    LOGI ("ISS Summoned to sector %d for player %d", sector_id, player_id);
  }
  ```
  It lacks the canon behavior of spawning an NPC (Captain Z / ISS) to attack, fine, or immediately tow the offender.
- **Police Interaction:** Protocol `police.bribe` and `police.surrender` commands are documented in `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md` but are **not implemented** in `src/server_loop.c` or any combat handler.

### Real-time Attack Prevention
- While `cron` tows unprotected players, there is no real-time check in `handle_ship_attack` to prevent a high-exp player from attacking a protected novice in S1-S10 before the next cleanup cycle (2m interval in `092_seed_gameplay.sql`).

## 4. Test Gaps

The current test suite `tests.v2/suite_combat_and_crime.json` has a test named `"Guard: Attack in FedSpace (Should Fail)"` but it expects `"status": "ok"`, suggesting it only tests that the command doesn't crash the server, rather than asserting enforcement or refusal.

**Missing Tests:**
1. **`test_captain_z_fighter_trigger`**: Assert that `iss_summon` is called (or NPC spawns) when deploying fighters in Sector 1.
2. **`test_captain_z_attack_trigger`**: Assert that attacking a player in Sector 1 triggers Federation intervention.
3. **`test_tow_excess_fighters`**: Assert that a ship with 51 fighters is towed out of FedSpace after the `stale_cutoff` (12h) is reached.
4. **`test_tow_evil_alignment`**: Assert that an evil player (`alignment < 0`) is towed out of FedSpace during `fedspace_cleanup`.
5. **`test_tow_high_exp`**: Assert that a player with `experience >= 1000` is towed out of FedSpace.

## 5. Key Code Pointers

- **Definition:** `is_fedspace_sector` (`src/server_cmds.c:36`)
- **Towing/Cleanup:** `h_fedspace_cleanup` (`src/server_cron.c:558`)
- **Towing Logic:** `tow_ship` (`src/server_cron.c:83`)
- **Eligible Tows Query:** `db_cron_init_eligible_tows` (`src/db/repo/repo_cron.c:599`)
- **Fighter Trigger:** `cmd_combat_deploy_fighters` (`src/server_combat.c:506`)
- **ISS Stub:** `iss_summon` (`src/server_combat.c:61`)
- **Bounty Posting:** `cmd_bounty_post_federation` (`src/server_cmds.c:558`)
- **Wanted Status:** `cluster_player_status.wanted_level` (`sql/pg/000_tables.sql:810`)
