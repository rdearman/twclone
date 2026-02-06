# Police Phase D Implementation Plan

## Overview
Police Phase D adds crime triggers (attack_port, contraband), enforcement gates (dock intercept), and a player-driven bounty system. This document captures the discovery findings and implementation strategy.

---

## DISCOVERY FINDINGS

### Hook Points Located

| Feature | File | Function | Line | Action |
|---------|------|----------|------|--------|
| **Port Docking** | `server_ports.c` | `cmd_dock_status()` | ~550 | Check wanted_level >= threshold; block dock + return intercept response |
| **Port Attacks** | `server_ports.c` | `cmd_port_rob()` | ~200 | Record CRIME_ATTACK_PORT incident |
| **Contraband Trade** | `server_ports.c` | `cmd_trade_buy()`, `cmd_trade_sell()` | ~350–400 | Record CRIME_CONTRABAND incident if item illegal + player lawful cluster |
| **Ship Destruction** | `server_ships.c` | `handle_ship_destruction()` | ~100 | Pay out bounties on kill; reduce wanted_level for target if system bounty claimed |
| **Incident Tracking** | `repo_clusters.c` | `repo_clusters_upsert_player_status()` | ~50 | Increment suspicion; promote to wanted per Phase B logic |
| **Cluster Resolution** | `server_police.c` | `h_get_cluster_info_for_sector()` | ~52 | Maps sector_id → cluster + law_severity + role |

### Database Model Summary

**Existing Tables Used:**
- `cluster_player_status(player_id, cluster_id, suspicion, wanted_level, banned, bust_count, updated_at)`
- `players(player_id, alignment, ...)`
- `clusters(clusters_id, law_severity, role, ...)`
- `ships(ship_id, player_id, sector_id, ...)`

**New Tables Required (Phase D):**
- `bounties(bounty_id, placed_by_player_id [NULL for system], target_player_id, cluster_id [NULL for cross-cluster], amount, kind [SYSTEM|PLAYER|REVERSE], active BOOL, created_at, claimed_at)`
- `bounty_claims(claim_id, bounty_id, claimed_by_player_id, created_at)`

### Cluster Types & Law Enforcement

| law_severity | role | Incident Tracking | Police | Bounties | Notes |
|--------------|------|-------------------|--------|----------|-------|
| **> 0** | FED/other | ✅ Yes | ✅ Yes | ✅ System | Lawful jurisdiction |
| **0** | ORION/other | ❌ No | ❌ No | ✅ Yes (reverse) | Lawless; bounties still work |

### Player Alignment

- Stored in `players.alignment` (integer)
- Orion reverse bounties: Only evil-aligned (alignment < some threshold, e.g., -1000) can place bounties on good players (alignment > +1000)
- Federation system bounties: Any wanted player (wanted_level > 0) can be targeted

---

## PHASE D SCOPE

### D1: New Crime Triggers

**Tracked in lawful clusters only (law_severity > 0):**
- `CRIME_ATTACK_PORT` - triggered by `cmd_port_rob()`
- `CRIME_CONTRABAND` - triggered by illegal trade in lawful cluster
- `CRIME_ATTACK_PLANET` - triggered by citadel/planet attack (wire into combat handler)
- `CRIME_EVASION` - escalation when player evades dock intercept

**Excluded:**
- ATTACK_SHIP is NOT a crime (except FedSpace 1–10, already handled by Captain Z)

### D2: Enforcement Gate (Dock Intercept)

**Hook:** `cmd_dock_status()` before docking succeeds

**Logic:**
```
IF cluster.law_severity > 0 AND player.wanted_level >= ENFORCE_WANTED_THRESHOLD:
    BLOCK docking
    RETURN intercept response:
        {
            ok: false,
            error: {code: "ENFORCEMENT_INTERCEPT", msg: "..."},
            enforcement: {
                cluster_id: X,
                wanted_level: Y,
                options: ["surrender", "bribe", "evade"]
            }
        }
ELSE:
    allow docking (existing behavior)
```

**Evade Behavior:**
- Does NOT block further play
- Escalates incident state: `suspicion += 1`
- Allows subsequent commands to execute normally (player can move, attack, etc.)
- May promote suspicion→wanted per Phase B rules

### D3: Bounty System

**Three bounty types:**

1. **SYSTEM / Federation:**
   - Auto-created when `wanted_level >= BOUNTY_WANTED_THRESHOLD` in lawful cluster
   - Payout reduces/clears wanted_level in that cluster (deterministic)
   - Example: wanted_level 5 in Federation Core → system bounty placed → destroy ship → bounty pays out + wanted_level set to 0

2. **PLAYER-PLACED / Lawful:**
   - Players can manually place bounties on other players in lawful clusters
   - No special enforcement; purely economic

3. **REVERSE / Orion:**
   - Only evil-aligned players (alignment < -1000) can place
   - Only good-aligned players (alignment > +1000) can be targeted
   - Placed in Orion cluster; lawless (no police, no suspension)
   - Bounties still pay out on kill

**RPC Endpoints:**
- `bounty.place(target_player_id, cluster_id, amount, reason_text)` → returns bounty_id
- `bounty.list(cluster_id [optional], [player_id])` → returns active bounties
- `bounty.claim(bounty_id, claimed_by_player_id)` → marks as claimed, pays out (called from ship destruction hook)

**Claim Hook:**
- Integrated into `handle_ship_destruction()` at kill confirmation
- If active bounties exist for victim:
  - For SYSTEM bounties: reduce victim's wanted_level by (bounty_amount / some_divisor) OR clear to 0 if == victim's current wanted_level
  - For PLAYER/REVERSE bounties: transfer amount from victim's account to killer's account
  - Mark bounty as claimed

### D4: Police Status RPC (Read-Only)

**New RPC:** `police.status(player_id, cluster_id [optional])`

**Returns:**
```json
{
  "cluster_id": X,
  "suspicion": N,
  "wanted_level": M,
  "banned": boolean,
  "bounties_active": [
    { "bounty_id": ..., "placed_by": "...", "amount": ..., "kind": "SYSTEM" }
  ]
}
```

This allows players to check their incident state and active bounties without needing UI.

---

## CRITICAL INVARIANT

**Lawless clusters NEVER create or modify cluster_player_status rows.**

Not even for evasion. Not even for contraband. This keeps Orion (and all lawless clusters) clean and avoids subtle bugs when reverse bounties exist but police state does not.

**Implementation:** Every crime recording call must check `cluster.law_severity == 0` at the entry point and return immediately if true. The guard goes FIRST, before any DB writes.

---

## IMPLEMENTATION STRATEGY

### Phase 1: Constants & Helper Functions

**File:** `src/server_police.h`

```c
#define CRIME_ATTACK_PORT 1
#define CRIME_CONTRABAND 2
#define CRIME_ATTACK_PLANET 3
#define CRIME_EVASION 4

#define ENFORCE_WANTED_THRESHOLD 2    // block dock if wanted_level >= 2
#define BOUNTY_WANTED_THRESHOLD 3     // create system bounty if wanted_level >= 3
#define SUSPICION_WANTED_THRESHOLD 3  // (from Phase B) promote suspicion->wanted at 3
```

**File:** `src/db/repo/repo_clusters.c`

New function:
```c
void police_record_crime(
    db_t *db,
    int player_id,
    int sector_id,
    int crime_type,
    int severity_points,
    const char *context_json
)
```

Logic:
1. Resolve sector_id → cluster_id via `h_get_cluster_info_for_sector()`
2. If cluster.law_severity == 0, return (lawless, no recording)
3. If cluster.role == 'FED' && sector_id in [1–10], return (Captain Z handles)
4. Upsert `cluster_player_status` with `suspicion += severity_points`
5. Check if suspicion >= SUSPICION_WANTED_THRESHOLD; if so, promote to wanted
6. If wanted_level >= BOUNTY_WANTED_THRESHOLD, auto-create system bounty

### Phase 2: Crime Recording Hooks

**File:** `src/server_ports.c`

In `cmd_port_rob()`:
```c
police_record_crime(db, player_id, sector_id, CRIME_ATTACK_PORT, 2, NULL);
```

In `cmd_trade_buy()` / `cmd_trade_sell()`:
```c
if (is_illegal && cluster_is_lawful(sector_id)) {
    police_record_crime(db, player_id, sector_id, CRIME_CONTRABAND, 1, NULL);
}
```

**File:** `src/server_ships.c`

In `handle_ship_destruction()`:
```c
if (attacker_is_player && victim_is_player) {
    // resolve victim's sector → cluster
    if (cluster_is_lawful(victim_sector)) {
        police_record_crime(db, attacker_id, victim_sector, CRIME_ATTACK_PLANET, 1, NULL);
    }
}
```

### Phase 3: Dock Intercept Gate

**File:** `src/server_ports.c`

In `cmd_dock_status()`, after ban check:
```c
cluster_info_t cluster = h_get_cluster_info_for_sector(db, player_sector);
if (cluster.law_severity > 0) {
    int wanted = repo_clusters_get_player_wanted_level(db, player_id, cluster.id);
    if (wanted >= ENFORCE_WANTED_THRESHOLD) {
        // BLOCK docking; return intercept response
        response.ok = false;
        response.error.code = ENFORCEMENT_INTERCEPT;
        response.enforcement.cluster_id = cluster.id;
        response.enforcement.wanted_level = wanted;
        response.enforcement.options = ["surrender", "bribe", "evade"];
        return response;
    }
}
```

### Phase 4: Police.Evade RPC

**New RPC Handler:** `server_police.c`

```c
handle_police_evade(db_t *db, int player_id, int cluster_id)
{
    // Escalate incident: suspicion += 1 + evasion_multiplier
    // May promote to wanted per Phase B logic
    police_record_crime(db, player_id, cluster.center_sector, CRIME_EVASION, 2, NULL);
    return {ok: true, msg: "Evasion successful; escaping..."};
}
```

**Does NOT block play.** Player can continue moving, attacking, etc. Docking just fails.

### Phase 5: Bounty System

**DB Tables:**

```sql
CREATE TABLE bounties (
    bounty_id SERIAL PRIMARY KEY,
    placed_by_player_id INT,  -- NULL for system bounties
    target_player_id INT NOT NULL,
    cluster_id INT,
    amount DECIMAL(10,2) NOT NULL,
    kind VARCHAR(20) NOT NULL,  -- SYSTEM, PLAYER, REVERSE
    active BOOLEAN DEFAULT true,
    created_at TIMESTAMPTZ DEFAULT NOW(),
    claimed_at TIMESTAMPTZ,
    claimed_by_player_id INT,
    FOREIGN KEY (target_player_id) REFERENCES players(player_id)
);

CREATE TABLE bounty_claims (
    claim_id SERIAL PRIMARY KEY,
    bounty_id INT NOT NULL REFERENCES bounties(bounty_id),
    claimed_by_player_id INT NOT NULL,
    created_at TIMESTAMPTZ DEFAULT NOW()
);
```

**Repo Functions:**

In `repo_clusters.c`:
- `int repo_bounty_create(db_t *db, int placed_by [NULL], int target, int cluster, double amount, const char *kind)`
- `json_t *repo_bounty_list(db_t *db, int cluster_id, int [optional] player_id_filter)`
- `void repo_bounty_claim(db_t *db, int bounty_id, int claimed_by_player_id)`

**RPC Handlers:**

In `server_police.c`:
- `handle_bounty_place(...)`
- `handle_bounty_list(...)`

**Claim Hook:**

In `server_ships.c` / `handle_ship_destruction()`:
```c
if (victim_is_player && attacker_is_player) {
    json_t *bounties = repo_bounty_list(db, victim_cluster_id, victim_player_id);
    for (each bounty in bounties) {
        if (bounty.active) {
            repo_bounty_claim(db, bounty.id, attacker_id);
            // Pay out: transfer amount or reduce wanted_level
        }
    }
}
```

---

## JSON TEST SUITES (Deterministic)

All tests must:
- **Find sectors by DB queries**, not hardcoded IDs
- **No sleeps, no timers, no random elements**
- **All setup deterministic:** use admin RPCs to create test players, force ship destruction if needed

### Test Suite 1: `suite_police_phase_d_attack_port.json`

- Setup: Create 2 test players in lawful cluster
- Test: Player A attacks port via `cmd_port_rob()`
- Verify: `suspicion > 0` in that cluster + `wanted_level` promoted if suspicion >= 3
- Verify: System bounty created if wanted_level >= 3

### Test Suite 2: `suite_police_phase_d_contraband.json`

- Setup: Create player in lawful cluster; find illegal commodity
- Test: Player trades illegal goods
- Verify: `suspicion` incremented + wanted promotion if applicable
- Verify: Lawless cluster has NO incident tracking (separate test case)

### Test Suite 3: `suite_police_phase_d_dock_intercept.json`

- Setup: Create player with wanted_level >= 2 in lawful cluster
- Test: Attempt to dock
- Verify: Docking blocked; intercept response returned with options
- Test: Player calls `police.evade`
- Verify: Docking still fails; suspicion escalated; player can continue playing (move, attack)

### Test Suite 4: `suite_bounty_fed_system.json`

- Setup: Create player with wanted_level >= 3 (system bounty auto-created)
- Test: Create second player; destroy first player's ship
- Verify: Bounty claimed; wanted_level reduced/cleared for victim
- Verify: Killer receives payment (or payout message)

### Test Suite 5: `suite_bounty_orion_reverse.json`

- Setup: Create evil player (alignment < -1000) in Orion cluster; create good player
- Test: Evil places bounty on good player
- Verify: Bounty created with kind='REVERSE'
- Test: Destroy good player's ship
- Verify: Evil player receives payout; no incident tracking (lawless)

### Test Suite 6: `suite_police_status_rpc.json`

- Setup: Create player with active incidents + bounties
- Test: Call `police.status(player_id, cluster_id)`
- Verify: Returns suspicion, wanted_level, bounties_active array with correct structure

---

## UPDATE EXISTING SUITES

**Regression manifest:** Update `tests.v2/suite_regression_full.json` to include:
```json
{
  "name": "Police Phase D Full Coverage",
  "suites": [
    "suite_police_phase_a.json",
    "suite_police_phase_b_incidents.json",
    "suite_police_phase_c_resolution.json",
    "suite_police_phase_d_attack_port.json",
    "suite_police_phase_d_contraband.json",
    "suite_police_phase_d_dock_intercept.json",
    "suite_bounty_fed_system.json",
    "suite_bounty_orion_reverse.json",
    "suite_police_status_rpc.json"
  ]
}
```

---

## PROTOCOL.v3 DOCUMENTATION

### New / Updated Sections

**police.status (NEW)**
```
RPC: police.status
Parameters: player_id, cluster_id (optional)
Returns: {cluster_id, suspicion, wanted_level, banned, bounties_active}
Purpose: Query incident state + active bounties for a player in a cluster.
Notes: Read-only; no side effects.
```

**police.evade (NEW)**
```
RPC: police.evade
Parameters: cluster_id
Returns: {ok, msg}
Purpose: Player response to dock intercept; escalates incident instead of blocking play.
Side effect: suspicion += 1 + multiplier; may promote to wanted.
Notes: Called only during dock intercept; does not prevent further play.
```

**bounty.place (NEW)**
```
RPC: bounty.place
Parameters: target_player_id, cluster_id, amount, reason (optional)
Returns: {bounty_id, ok}
Purpose: Place a bounty on another player.
Constraints:
  - Lawful clusters: anyone can place (economic)
  - Orion reverse: only alignment < -1000 on alignment > +1000 targets
Notes: Does not prevent play; purely economic.
```

**bounty.list (NEW)**
```
RPC: bounty.list
Parameters: cluster_id (optional), player_id (optional filter)
Returns: [{bounty_id, placed_by, target, amount, kind, active}]
Purpose: List active bounties in a cluster or for a specific player.
Notes: No pagination; limited to active bounties < N days old.
```

**dock.status (UPDATED)**
```
Updated response for dock intercept:
If wanted_level >= ENFORCE_WANTED_THRESHOLD:
    {
        ok: false,
        error: {code: "ENFORCEMENT_INTERCEPT", msg: "..."},
        enforcement: {
            cluster_id: X,
            wanted_level: Y,
            options: ["surrender", "bribe", "evade"]
        }
    }
```

---

## ACCEPTANCE CRITERIA

- [x] All existing Phase A/B/C suites pass unchanged
- [ ] All Phase D suites pass deterministically
- [ ] Lawless clusters: never record incidents; bounties still work
- [ ] Dock intercept blocks docking when wanted_level >= threshold
- [ ] attack_port and contraband increment incident state in lawful clusters
- [ ] System bounties created for wanted_level >= threshold
- [ ] Orion reverse bounties respect alignment constraints
- [ ] Bounty payouts work on ship destruction
- [ ] police.evade escalates incident without blocking play
- [ ] No jail timers; no login blocks; no session termination
- [ ] Git diff shows no test deletions (only additions)

---

## NOTES

- Phase D does NOT implement NPC bounty hunters or police fleets
- Jail sentences (timers, login blocks) are explicitly out of scope
- Lawless clusters are jurisdiction-free: no incidents, no police, but bounties work
- Cluster center_sector always resolves from `clusters.center_sector` (not sector 1–10 heuristic)
- System bounty amount = (wanted_level * 1000) or configurable; not yet decided
