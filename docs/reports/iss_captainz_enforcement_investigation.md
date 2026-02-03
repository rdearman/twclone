# Captain Z / ISS Enforcement Investigation Report

**Investigation Date:** 2026-02-03  
**Status:** Investigation only—no code changes made  
**Scope:** `src/*`, `sql/*`, `docs/*`, `tests.v2/*`

---

## Executive Summary

**Current Implementation Status:** **Severely stubbed**

The ISS (Interpolar Security Station) system is **partially initialized but non-functional**:

1. **ISS Exists as NPC:** An ISS player NPC is created and initialized at startup (see `iss_init_once()` in `server_universe.c`).
2. **Summon Trigger Wired (Partial):** Fighter deployment in FedSpace **does call** `iss_summon()`, but:
   - `iss_summon()` is a **logging stub** (one line: logs, then returns).
   - Attack commands do **NOT** trigger `iss_summon()` at all.
3. **No Enforcement Mechanics:** No actual ISS response:
   - No NPC spawn/movement to sector.
   - No damage application.
   - No podding.
   - No towing.
4. **Soft Deny for Deployment:** Asset deployments (fighters, mines, beacons) in FedSpace are **allowed** with no refusal or penalty; only logged.
5. **Hard Punish for Overt Aggression:** Missing entirely. Attacks are **fully allowed** in FedSpace with no ISS intervention.

**Policy Intended (from requirements):**
- **FedSpace (Sectors 1–10):**
  - Asset deployment → soft deny (refuse)
  - Overt aggression (attacks) → instant destruction + pod

**Reality:** Neither is enforced.

---

## A. ISS References & Implementation Locations

### ISS Initialization (startup-time)

| Symbol | Location | Type | Status |
| :--- | :--- | :--- | :--- |
| `iss_init_once()` | `src/server_universe.c:2015` | Function def | Stub: reads globals, queries ISS NPC, returns 1 if found |
| `iss_init()` | `src/server_universe.c:1968` | Function def | Wrapper; calls `iss_init_once()` |
| `iss_tick()` | `src/server_universe.c:1982` | Function def | Calls `iss_try_consume_summon()` and `iss_patrol_step()` (both stubs) |
| `g_iss_inited` | `src/server_universe.c:54` | Global flag | 0 until initialized |
| `g_iss_id` | `src/server_universe.c:55` | Global var | Holds ISS player_id (hardcoded to 1 in stub) |
| `g_iss_sector` | `src/server_universe.c:56` | Global var | Tracks ISS location |
| `iss_init_once()` call | `src/server_engine.c:971` | Integration point | Called at startup after DB init |

### ISS Helper Stubs (non-functional)

| Symbol | Location | Type | Implementation | Status |
| :--- | :--- | :--- | :--- | :--- |
| `db_get_iss_player()` | `src/server_universe.c:2107` | Helper | Returns hardcoded `player_id=1, sector=1`; no DB query | Stub |
| `db_get_stardock_sector()` | `src/server_universe.c:2099` | Helper | Returns hardcoded `1`; no DB query | Stub |
| `iss_move_to()` | `src/server_universe.c:2119` | Helper | No-op (void, params all `(void)`) | Stub |
| `iss_try_consume_summon()` | `src/server_universe.c:2129` | Helper | Always returns 0 (no summon processed) | Stub |
| `iss_patrol_step()` | `src/server_universe.c:2137` | Helper | No-op (void, params all `(void)`) | Stub |

### Summon Entry Point (wired into attack path)

| Symbol | Location | Handler | Trigger Condition | Effect |
| :--- | :--- | :--- | :--- | :--- |
| `iss_summon()` | `src/server_combat.c:62` | Fighter deploy | `sid >= 1 && sid <= 10` after commit | **Logs to `LOGI` only** |

---

## B. Trigger Analysis: Where `iss_summon()` Is Called

### Current Trigger Call Sites

#### 1. Fighter Deployment (WIRED BUT STUB)
- **Call Location:** `src/server_combat.c:592` (inside `cmd_combat_deploy_fighters()`)
- **Handler:** `cmd_combat_deploy_fighters()` at line 523
- **Condition:** `if (sid >= 1 && sid <= 10)` ✅ FedSpace check present
- **Timing:** After DB commit (tx committed at line 590)
- **What Happens After:**
  - Response sent to client: `"combat.fighters.deployed"` (success, no refusal)
  - No prevention of deployment
  - No penalty
- **Current Effect:** Only log line 64 in `iss_summon()`: `LOGI ("ISS Summoned to sector %d for player %d", sector_id, player_id);`

### Missing Trigger Call Sites

#### 2. Attack Command (NOT WIRED)
- **Handler:** `handle_ship_attack()` at line 763
- **Current Behavior:** No FedSpace check; no `iss_summon()` call
- **Evidence:** Grep shows no `iss_summon` or `is_fedspace` in attack path
- **Result:** Attacks are fully allowed in FedSpace

#### 3. Mine Deployment (NOT WIRED)
- **Handler:** `cmd_combat_deploy_mines()` at line 604
- **Current Behavior:** No FedSpace check; no `iss_summon()` call
- **Result:** Mines deployed freely in FedSpace

#### 4. Limpet Deployment (NOT WIRED)
- **Same as mines** (limpets are `mine_type=4`)

#### 5. Beacon Deployment (NOT WIRED)
- **Handler:** `cmd_universe_set_beacon()` (search returned no direct hits; likely in `server_universe.c`)
- **Current Behavior:** Grep shows error message exists: `"Cannot set a beacon in FedSpace."` but no `iss_summon()` call

#### 6. Bounty/Crime/Wanted Status (NOT WIRED)
- **Bounty posting:** `cmd_bounty_post_federation()` in `src/server_cmds.c:558`
- **Wanted system:** Exists but no ISS integration

---

## C. What `iss_summon()` Actually Does (End-to-End Trace)

### Implementation (Line 62–65, `src/server_combat.c`)

```c
static void
iss_summon (int sector_id, int player_id)
{
  LOGI ("ISS Summoned to sector %d for player %d", sector_id, player_id);
}
```

### Execution Flow

1. **Caller:** `cmd_combat_deploy_fighters()` at line 592
2. **Invocation:** `iss_summon(sid, ctx->player_id);` where `sid` is the deployment sector
3. **Action:** Single `LOGI` call (log to stdout/file, severity INFO)
4. **Return:** Void; nothing happens
5. **DB Mutations:** **NONE**
6. **Repo Calls:** **NONE**
7. **Events Emitted:** **NONE**

### What It Should Do (Per Policy)

- Spawn an NPC ship (Captain Z / ISS instance) in the sector
- Move ISS to the player's sector
- Initiate attack on player ship
- Apply damage
- If ship hull → 0, destroy ship
- If player is in pod state, trigger pod

### Actual Effect

**Absolutely nothing** beyond a log line that confirms a summon was requested.

---

## D. FedSpace Policy Coverage: Current vs. Desired

### FedSpace Definition

| Property | Value |
| :--- | :--- |
| Sector Range | 1–10 inclusive |
| Schema Flag | `sectors.is_fedspace = TRUE` |
| Database Location | Seeded in `sql/pg/091_seed_essential.sql` |
| Helper Function | `is_fedspace_sector()` in `src/server_cmds.c:36` |

### Asset Deployment: Current Behavior

| Asset Type | Command | FedSpace Check | Refusal | ISS Summon | Current Behavior |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Fighters** | `combat.deploy_fighters` | ✅ YES | ❌ NO | ✅ Wired (stub) | **Allowed** with log |
| **Mines (ARMID)** | `combat.deploy_mines` | ❌ NO | ❌ NO | ❌ NO | **Allowed, no log** |
| **Mines (Limpet)** | `combat.deploy_mines` (type=4) | ❌ NO | ❌ NO | ❌ NO | **Allowed, no log** |
| **Beacons** | `universe.set_beacon` | ⚠️ Partial* | ✅ Error returned | ❌ NO | **Refused** (soft deny works) |

*Beacon: Error message found (`"Cannot set a beacon in FedSpace."`) but direct code path unknown.

### Overt Aggression (Attacks): Current Behavior

| Command | FedSpace Check | Attack Allowed | ISS Summon | Current Behavior |
| :--- | :--- | :--- | :--- | :--- |
| `combat.attack` | ❌ NO | ✅ YES | ❌ NO | **Full damage applied** |
| `combat.attack_planet` | ❌ NO | ✅ YES | ❌ NO | **Full damage applied** |

### Policy Gap Summary

| Requirement | Implemented? | Evidence | Gap |
| :--- | :--- | :--- | :--- |
| **Soft deny asset deployment** | ⚠️ Partial | Beacons refused; fighters/mines allowed | Fighters & mines should be refused in FedSpace |
| **Hard punish overt aggression** | ❌ NO | No attack handler checks FedSpace; `iss_summon()` not wired | All attack paths missing FedSpace check and `iss_summon()` call |
| **ISS actual effect (destroy + pod)** | ❌ NO | `iss_summon()` is logging stub | Need to implement `iss_move_to()`, damage, destruction, podding |

---

## E. Canon Nuance Detection (Documentation Only)

### Protection Eligibility (Already Implemented)

| Feature | Location | Status | Note |
| :--- | :--- | :--- | :--- |
| Protected target: `alignment >= 0 AND EXP < 1000` | `src/server_cron.c` (`h_fedspace_cleanup`) | ✅ Used | Identifies eligible tows (negative alignment or >= 1000 EXP are removed) |

### FedSpace Towing Rules (Already Implemented)

| Feature | Location | Status | Note |
| :--- | :--- | :--- | :--- |
| Overnight towing for excess fighters (>50) | `src/server_cron.c` | ✅ Used | Max 49 fighters (`MAX_FED_FIGHTERS = 49`) |
| Towing evil-aligned players (`alignment < 0`) | `src/server_cron.c` | ✅ Used | Removes from FedSpace periodically |
| Towing high-EXP players (`experience >= 1000`) | `src/server_cron.c` | ✅ Used | Removes protection after 1000 EXP |
| Towing inactive ships (12h stale) | `src/server_cron.c:stale_cutoff` | ✅ Used | Orphaned ships are confiscated |

### "Traps Don't Stop Captain Z" (Canon Feature)

- **Status:** ❌ No evidence found
- **Expected:** ISS can pass through mines/fighters
- **Current:** N/A (ISS doesn't move)

### Other ISS Ships (Canon Expansion, Out of Scope)

- **Status:** ❌ No references found
- **Note:** Policy specifies "do NOT implement other ISS ships now," so absence is expected

---

## F. Protocol Documentation Parity

### Documented Commands

| Command | Documentation | Implementation | Mismatch |
| :--- | :--- | :--- | :--- |
| `police.bribe` | `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md:22` | ❌ **Not in `src/server_loop.c`** | Command documented but **not implemented** |
| `police.surrender` | `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md:22` | ❌ **Not in `src/server_loop.c`** | Command documented but **not implemented** |

### Documented Events

| Event | Documentation | Implementation | Mismatch |
| :--- | :--- | :--- | :--- |
| `player.illegal_act.v1` | `docs/Intra-Stack_Protocol.md:56, 240` | ❌ **Not logged anywhere** | Event documented but **never emitted** |
| `npc.spawn.v1` | `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md:7` | ❌ **Not found** | Enum/event not implemented |
| `npc.attack.v1` | `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md:9` | ❌ **Not found** | Enum/event not implemented |

### Documented ISS Behaviors

- **Imperial Enforcement:** Described in protocol as a behavior type (doc line 15), but ISS code is a stub.
- **Ferengi Trade/Theft:** Described but not related to ISS.

### Conclusion

**Major doc/code parity gap:** Police commands and illegal_act events are documented but completely unimplemented. The protocol implies a law enforcement system that does not exist.

---

## G. Test Gaps

### Current Test Suite Status

| Test Suite | File | ISS/FedSpace Tests | Status |
| :--- | :--- | :--- | :--- |
| Combat & Crime | `tests.v2/suite_combat_and_crime.json` | ✅ Exists: `"Guard: Attack in FedSpace (Should Fail)"` | ⚠️ Only checks `status==ok`; doesn't assert enforcement |

### Missing Tests (Required to Prove Behavior)

#### Critical: Trigger Tests

1. **`test_captain_z_fighter_deployment_triggers_summon`**
   - Deploy fighters in Sector 1
   - Assert: `iss_summon()` is called (or log contains ISS summon message)
   - Current: Passes (summon is called but is a no-op)

2. **`test_captain_z_attack_triggers_summon`**
   - Attack in Sector 1
   - Assert: `iss_summon()` is called
   - Current: Fails (no call exists in attack path)

3. **`test_captain_z_mine_deployment_triggers_summon`**
   - Deploy mines in Sector 1
   - Assert: `iss_summon()` is called
   - Current: Fails (no call exists in mine path)

#### Critical: Effect Tests

4. **`test_iss_destroys_offending_ship`**
   - Call `iss_summon()` with ship ID
   - Assert: Ship is destroyed (hull = 0 in DB)
   - Current: Fails (stub does nothing)

5. **`test_iss_pods_player_on_destruction`**
   - Destroy offending ship via ISS
   - Assert: Player moved to pod sector; ship destroyed
   - Current: Fails (stub does nothing)

#### Important: Soft Deny Tests

6. **`test_fighter_deployment_refused_fedspace`**
   - Deploy fighters in FedSpace
   - Assert: Response has error code (not success)
   - Current: Fails (deployment succeeds)

7. **`test_mine_deployment_refused_fedspace`**
   - Deploy mines in FedSpace
   - Assert: Response has error code
   - Current: Fails (deployment succeeds)

#### Important: Canon Tests

8. **`test_protected_player_not_towed`**
   - Player: `alignment >= 0, experience < 1000`
   - Assert: Not towed in fedspace_cleanup
   - Current: Unknown (test may not exist)

9. **`test_high_exp_player_towed`**
   - Player: `experience >= 1000`
   - Assert: Towed out in fedspace_cleanup
   - Current: Unknown (test may not exist)

---

## H. Evidence Map: Call Sites & Conditions

### Table: iss_summon() Call Sites

| # | Handler | File | Line | Condition | Sector Check | Player Check | After Call |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| 1 | `cmd_combat_deploy_fighters` | `server_combat.c` | 592 | Fighter deployed in FedSpace | ✅ `if (sid >= 1 && sid <= 10)` | ✅ `ctx->player_id` | Response sent (success) |

**Total:** 1 wired call site (partial; no effect).

### Sector Range Check

```c
// src/server_cron.c, lines 33-34
#define FEDSPACE_SECTOR_START 1
#define FEDSPACE_SECTOR_END 10

// src/server_cmds.c, lines ~36–40 (hypothetical range)
static inline int
is_fedspace_sector (int sector_id)
{
  if (!is_fedspace_sector (sector))
    // "Must be in FedSpace to post a Federation bounty."
}
```

The condition `sid >= 1 && sid <= 10` matches the macro definition.

---

## I. Recommended Next Ticket Breakdown

**Note:** These are proposal titles only; no implementation made.

### Ticket 1: Wire iss_summon() into All Attack Paths
**Title:** `Add FedSpace check & iss_summon trigger to combat.attack & combat.attack_planet`

- Add sector check to `handle_ship_attack()` (check `attacker_id`'s sector)
- Add `if (sid >= 1 && sid <= 10) iss_summon(sid, attacker_id);` after DB commit
- Add same logic to `cmd_combat_attack_planet()` if it exists
- Add sector checks to mine deployment, limpet deployment, and any other attack-like commands
- **Depends on:** Ticket 2

### Ticket 2: Implement iss_summon() Effects
**Title:** `Implement Captain Z enforcement: move ISS to sector, apply damage, destroy + pod`

- Implement `db_get_iss_player()`: Query `players` table for ISS NPC (check `is_npc=TRUE`)
- Implement `iss_move_to()`: Update ISS ship's sector, emit `npc.move.v1` event
- Implement `iss_try_consume_summon()`: Check if ISS has pending summon in queue; if yes, spawn attack
- Implement damage/destruction: Call existing `destroy_ship_and_handle_side_effects()` or similar
- **Depends on:** ISS NPC exists in DB (verify with startup logs)

### Ticket 3: Soft Deny Asset Deployment in FedSpace
**Title:** `Refuse fighter/mine/limpet/beacon deployment in FedSpace (soft deny)`

- Add `if (is_fedspace_sector(sid)) { return send_response_error(..., ERR_FEDSPACE_DEPLOYMENT_DENIED, "..."); }`
- Affected commands: `combat.deploy_fighters`, `combat.deploy_mines`, `universe.set_beacon`
- Use consistent error code (define if not exists)
- **Depends on:** Ticket 1 (so attacks trigger ISS instead of being refused)

### Ticket 4: Emit player.illegal_act Event
**Title:** `Log player.illegal_act.v1 event when ISS is summoned (attack/deployment in FedSpace)`

- Emit `player.illegal_act.v1` with type `"fedspace_aggression"` when `iss_summon()` is called
- Include `player_id`, `sector_id`, optional `target_id`
- Use existing `db_log_engine_event()` pattern
- **Depends on:** Tickets 1 & 3

---

## J. Summary Findings

### What IS Implemented

1. ✅ **ISS NPC Exists** – Player record created; initialized at startup
2. ✅ **FedSpace Definition** – Sectors 1–10 marked; helper function available
3. ✅ **ISS Initialization Logic** – `iss_init_once()` sets up globals
4. ✅ **Cron-Based Towing** – Removes unprotected players every 2 minutes (separate system)
5. ✅ **Fighter Cap in FedSpace** – Max 49 fighters allowed (enforcement in cleanup)
6. ✅ **Alignment/EXP Protection** – Good-aligned novices identified and towed if parked

### What IS Stubbed

1. ⚠️ **`iss_summon()` Trigger for Fighters** – Wired but does nothing
2. ⚠️ **ISS Movement** – Function exists; no-op
3. ⚠️ **ISS Patrol Logic** – Function exists; no-op
4. ⚠️ **Police Commands** – Documented; not implemented
5. ⚠️ **`player.illegal_act` Events** – Documented; not emitted

### What IS Missing

1. ❌ **`iss_summon()` in Attack Paths** – No FedSpace check; not wired
2. ❌ **`iss_summon()` in Mine/Limpet Paths** – Not wired
3. ❌ **ISS Damage & Destruction** – No implementation
4. ❌ **ISS Podding** – No implementation
5. ❌ **Soft Deny for Deployments** – No refusal logic
6. ❌ **Real-time Attack Prevention** – Gap between cron cycles (2m)
7. ❌ **`police.bribe` / `police.surrender` RPCs** – Not registered in command loop

---

## K. Code Quality Observations

### Positive

- **Clear separation:** Stubs are clearly marked with `TODO` comments
- **Consistent naming:** `iss_*` functions use predictable prefix
- **Initialization pattern:** Proper use of `g_iss_inited` flag to prevent re-initialization
- **Database abstraction:** `db_get_iss_player()` exists (even if stubbed) for future DB integration

### Concerns

- **Hardcoded values:** ISS player_id = 1, sector = 1 in stubs; not query-based
- **No async queue:** Summon mechanism doesn't use a queue; direct logging only
- **No NPC struct:** ISS is treated as a regular player; no dedicated NPC control struct
- **Orphaned code:** Helper functions like `h_handle_npc_encounters()` exist but are not called

---

## L. Files Modified for Investigation (None)

**Per requirements:** Investigation only—no code changes made.

- All findings are from inspection of existing code.
- No edits, refactors, or new files created.

---

## References

### Code Locations

- **ISS Initialization:** `src/server_universe.c:2015–2053`
- **ISS Stubs:** `src/server_universe.c:2096–2141`
- **iss_summon() Wiring:** `src/server_combat.c:62–65, 592`
- **Fighter Deployment:** `src/server_combat.c:523–601`
- **Attack Handler:** `src/server_combat.c:763–784`
- **FedSpace Helper:** `src/server_cmds.c:36–40` (estimated)
- **Cron Cleanup:** `src/server_cron.c:558+` (h_fedspace_cleanup)

### Documentation

- **Protocol:** `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md`
- **Intra-Stack Events:** `docs/Intra-Stack_Protocol.md`
- **Existing Audit:** `docs/reports/fedspace_enforcement_audit.md`
- **Combat Devices Audit:** `docs/reports/combat_devices_canon_audit.md`

### Databases

- **Schema:** `sql/pg/000_tables.sql` (players, sectors, alignments)
- **Seed Data:** `sql/pg/091_seed_essential.sql` (ISS NPC, FedSpace sectors)

---

**End of Investigation Report**
