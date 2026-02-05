# Police / ISS / Bribery / Surrender Parity Audit

**Investigation Date:** 2026-02-04  
**Status:** Investigation only—no code changes made  
**Scope:** `src/*`, `sql/*`, `docs/*`, `tests.v2/*`

---

## Executive Summary

This audit examines the **police enforcement system** ("law enforcement" across clusters) and documents:
- **Protocol surface area** for `police.bribe` and `police.surrender`
- **Server implementation** status (command registration, handlers, DB integration)
- **Enforcement triggers** (port robbery, attacks, incidents) and cluster jurisdiction
- **Missing parity** between documented RPCs and implementation
- **Gap plan** for cluster-based police parity

### Key Findings

| Category | Status | Notes |
| --- | --- | --- |
| **Protocol Docs** | ⚠️ Partial | `police.bribe` and `police.surrender` mentioned in protocol but not fully specified |
| **RPC Implementation** | ❌ Missing | Neither `police.bribe` nor `police.surrender` registered in command loop |
| **Port Robbery Trigger** | ✅ Implemented | `port.rob` exists; increases `cluster_player_status.suspicion` and registers busts |
| **Cluster Law Model** | ✅ Schema Present | `cluster_player_status` (suspicion, bust_count, wanted_level, banned) fully defined |
| **FedSpace Enforcement** | ✅ Partially | Captain Z (hard punish) implemented for attacks/aggression in sectors 1–10; soft deny missing for assets |
| **Cluster Jurisdiction** | ⚠️ Partial | Federation cluster (1–10 + MSL) seeded; Orion/Ferengi clusters exist but law_severity and police behavior unspecified |
| **Test Coverage** | ⚠️ Minimal | Port robbery tests exist; police.bribe/surrender tests absent |

---

## 1) Protocol Surface Area

### Sources Mentioning Police / Bribe / Surrender

| RPC Name | Protocol File | Section Heading | Claimed Behavior |
| :--- | :--- | :--- | :--- |
| `police.bribe` | `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md:22` | 3. Police & Legal | "RPC for interacting with law enforcement" (details not specified) |
| `police.surrender` | `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md:22` | 3. Police & Legal | "RPC for interacting with law enforcement" (details not specified) |
| `player.illegal_act.v1` | `docs/Intra-Stack_Protocol.md:56, 240` | 4) Server-Emitted Events → engine_events; Appendix A | "Event when player breaks law"; payload: `{ player_id, sector_id, type, target_id }` |

### Protocol Gap Analysis

**Severity:** CRITICAL — Documented but underspecified

- **`police.bribe` RPC:**
  - **Documented location:** `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md:22`
  - **Request schema:** Not specified in protocol
  - **Response schema:** Not specified in protocol
  - **Behavior:** Vague ("interacting with law enforcement")
  - **Status:** Not implemented; no handler in `src/server_loop.c`

- **`police.surrender` RPC:**
  - **Documented location:** `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md:22`
  - **Request schema:** Not specified in protocol
  - **Response schema:** Not specified in protocol
  - **Behavior:** Vague ("interacting with law enforcement")
  - **Status:** Not implemented; no handler in `src/server_loop.c`

- **`player.illegal_act.v1` Event:**
  - **Documented location:** `docs/Intra-Stack_Protocol.md:56, 240`
  - **Payload schema:** `{ player_id: int, sector_id: int, type: string (bust|mine_illegal|attack_unarmed), target_id: int|null }`
  - **Emission trigger:** "when player breaks law" (not specified which laws)
  - **Status:** Never emitted in code (no `db_log_engine_event()` calls with `player.illegal_act.v1`)

### Related Protocol Artifacts

| Artifact | Location | Scope |
| :--- | :--- | :--- |
| Intra-Stack S2S Protocol | `docs/Intra-Stack_Protocol.md:156` | Police bribe/surrender listed as client RPCs that emit events |
| NPC Behaviors | `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md:15` | "Imperial" enforcement behavior type mentioned but not wired to police commands |
| Event Contract | `docs/Intra-Stack_Protocol.md:240` | `player.illegal_act.v1` schema defined; not emitted |

---

## 2) Server Implementation Inventory

### Command Registry Scan

**Search:** `grep -n "police\|bribe\|surrender" /home/rick/twclone/src/server_loop.c`

**Result:** No matches (0 commands registered)

### Command Registry Table

| RPC Name | Implemented? | Handler Function | File | Status |
| :--- | :--- | :--- | :--- | :--- |
| `police.bribe` | ❌ NO | — | — | **Not in command registry** |
| `police.surrender` | ❌ NO | — | — | **Not in command registry** |
| `bounty.list` | ✅ YES | `cmd_bounty_list()` | `src/server_cmds.c:658` | Implemented |
| `bounty.post_federation` | ✅ YES | `cmd_bounty_post_federation()` | `src/server_cmds.c:558` | Implemented |

### Related Crime-Adjacent Commands (Implemented)

| Command | Handler | File | Behavior |
| :--- | :--- | :--- | :--- |
| `port.rob` | `cmd_port_rob()` | `src/server_ports.c:1706` | Triggers suspicion increase and bust registration |
| `combat.attack` | `handle_ship_attack()` | `src/server_combat.c:763` | FedSpace enforcement (Captain Z hard punish for aggression) |
| `combat.attack_planet` | `cmd_combat_attack_planet()` | `src/server_planets.c:114` | FedSpace enforcement present |

### Database Repo Functions (Police/Law Enforcement Related)

| Function | File | Purpose | Implemented |
| :--- | :--- | :--- | :--- |
| `db_ports_check_cluster_ban()` | `src/db/repo/repo_ports.c:354` | Check if player banned in cluster | ✅ YES |
| `db_ports_increase_suspicion()` | `src/db/repo/repo_ports.c:479` | Increase suspicion; upsert to `cluster_player_status` | ✅ YES |
| `db_ports_insert_real_bust()` | `src/db/repo/repo_ports.c:549` | Register a bust (port-level incident) | ✅ YES |
| `repo_clusters_get_player_banned()` | `src/db/repo/repo_clusters.c:213` | Check `cluster_player_status.banned` | ✅ YES |
| `repo_clusters_get_player_suspicion_wanted()` | `src/db/repo/repo_clusters.c:231` | Read suspicion and wanted_level | ✅ YES |
| `db_cron_robbery_decay_suspicion()` | `src/db/repo/repo_cron.c:815` | Daily decay: suspicion *= 0.9 | ✅ YES |
| `db_cron_robbery_clear_busts()` | `src/db/repo/repo_cron.c:835` | Clear expired busts (TTL-based) | ✅ YES |

### Command Loop Evidence

**File:** `src/server_loop.c`

**Total command registry entries:** 250+  
**Entries matching "police":** 0  
**Entries matching "bribe":** 0  
**Entries matching "surrender":** 0  

```c
// Sample of actual registry (lines ~88–91, 248–251):
extern int cmd_bounty_list (client_ctx_t * ctx, json_t * root);
extern int cmd_bounty_post_federation (client_ctx_t * ctx, json_t * root);
extern int cmd_bounty_post_hitlist (client_ctx_t * ctx, json_t * root);
// ... no police.* entries found ...
{"bounty.list", cmd_bounty_list, "List bounties", schema_placeholder, 0, false, NULL},
{"bounty.post_federation", cmd_bounty_post_federation,
 "Post a Federation bounty", schema_placeholder, 0, false, NULL},
```

---

## 3) Enforcement Triggers (Crime-Causing Actions)

### Current Crime Triggers in Code

#### Trigger 1: Port Robbery (`port.rob`)

| Property | Value |
| :--- | :--- |
| **RPC Command** | `port.rob` |
| **Handler** | `cmd_port_rob()` in `src/server_ports.c:1706` |
| **Triggers** | Player attempts to rob port cargo or credits |
| **Cluster Check** | ✅ YES — gets cluster_id from sector; checks `banned` flag |
| **Updates cluster_player_status** | ✅ YES — calls `db_ports_increase_suspicion()` and `db_ports_insert_real_bust()` |
| **Suspicion Impact** | Base + configurable modifiers (good_guy_bonus, pro_criminal_delta, evil_cluster_bonus, good_align_penalty_mult) |
| **Bust Record** | Creates `port_busts` entry with timestamp and `bust_type='real'`; TTL-based expiry (7 days default) |
| **Incident State** | `cluster_player_status.suspicion` increases; can trigger ban if `wanted_level >= 3` |
| **Messages/Events** | Sends `send_response_refused_steal()` on detection; logs to `LOGI`; **no `player.illegal_act.v1` event emitted** |

**Evidence:** `src/server_ports.c:1835–1900+` (full robbery logic)

#### Trigger 2: Overt Aggression in FedSpace (Sectors 1–10)

| Property | Value |
| :--- | :--- | :--- |
| **Actions** | `combat.attack`, `combat.attack_planet`, `port.rob` (in FedSpace) |
| **Sector Check** | ✅ YES — `ctx->sector_id >= 1 && ctx->sector_id <= 10` |
| **Enforcement** | ✅ YES — `fedspace_enforce_no_aggression_hard()` in `src/server_combat.c:78` |
| **Cluster Impact** | FedSpace = Federation cluster; law_severity = 3 (severe) |
| **Punishment** | **Hard punish**: Destroy ship + pod player + send message |
| **Events Emitted** | **No `player.illegal_act.v1` event**; system message sent to attacker |
| **Cluster_player_status** | NOT updated by FedSpace enforcement (hard punish bypasses suspicion tracking) |

**Evidence:** `src/server_combat.c:78–133` (fedspace_enforce_no_aggression_hard implementation)

**Code path examples:**
- `combat.attack` → `handle_ship_attack()` → `fedspace_enforce_no_aggression_hard()` at line 858
- `port.rob` → `cmd_port_rob()` → `fedspace_enforce_no_aggression_hard()` at line 1737
- `combat.attack_planet` → `cmd_combat_attack_planet()` → `fedspace_enforce_no_aggression_hard()` at line 114

#### Trigger 3: Asset Deployment in FedSpace (Fighters, Mines, Beacons)

| Asset Type | Handler | FedSpace Check | ISS Summon | Refusal | Suspicion Update |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Fighters** | `cmd_combat_deploy_fighters()` | ✅ YES (`sid >= 1 && sid <= 10`) | ⚠️ Stub (logs only) | ❌ NO | ❌ NO |
| **Mines (ARMID)** | `cmd_combat_deploy_mines()` | ❌ NO | ❌ NO | ❌ NO | ❌ NO |
| **Mines (Limpet)** | `cmd_combat_deploy_mines()` | ❌ NO | ❌ NO | ❌ NO | ❌ NO |
| **Beacons** | `cmd_universe_set_beacon()` | ⚠️ Partial | ❌ NO | ✅ YES (error returned) | ❌ NO |

**Evidence:**
- Fighter deployment FedSpace check: `src/server_combat.c:592–601`
- ISS summon stub: `src/server_combat.c:62–65` (one-line log, no side effects)
- Beacon refusal: Documented but code path unknown

#### Non-Triggers (Not Wired)

| Trigger Type | Why Not Wired | Evidence |
| :--- | :--- | :--- |
| **Mine placement (non-FedSpace)** | No jurisdiction enforcement | No cluster checks in `cmd_combat_deploy_mines()` |
| **Contraband trade** | Not mentioned in codebase | No "illegal goods" enforcement linked to police |
| **Wanted status execution** | No enforcement mechanism | `cluster_player_status.wanted_level` exists but never triggers action |

### Crime State Representation

**Current model:** `cluster_player_status` row per (cluster_id, player_id) pair

| Column | Type | Semantics | Trigger |
| :--- | :--- | :--- | :--- |
| `cluster_id` | bigint FK | Region of jurisdiction | Derived from sector (via `cluster_sectors` lookup) |
| `player_id` | int FK | Offender | Port robbery, potential future triggers |
| `suspicion` | int (0–100+) | Ongoing suspicion level | Increases on bust; decays 0.9x daily; resets TTL busts after 7 days |
| `bust_count` | int (0+) | Historical count of incidents | Increments per bust; never decrements |
| `last_bust_at` | text (timestamp) | Last incident timestamp | Updated per bust; used for TTL calculation |
| `wanted_level` | int (0–5?) | Escalation tier | Set when `suspicion` reaches thresholds (exact mapping undefined in code) |
| `banned` | bool | Cluster-wide access ban | Set to TRUE when `wanted_level >= 3` (see `src/server_ports.c:589`) |

**Evidence:** `sql/pg/000_tables.sql:806–817` (schema definition)

---

## 4) Cluster Law Data Model Usage (DB + Repo Layer)

### Database Schema Objects

#### Clusters Table

```sql
CREATE TABLE clusters (
    clusters_id serial PRIMARY KEY,
    name text NOT NULL UNIQUE,
    role TEXT NOT NULL,              -- 'FED', 'RANDOM', 'FACTION', etc.
    kind text NOT NULL,              -- 'FACTION', 'RANDOM', etc.
    center_sector bigint,
    law_severity bigint NOT NULL DEFAULT 1,   -- 0 (lawless) to 3+ (severe)
    alignment integer NOT NULL DEFAULT 0      -- -100 (evil) to 100 (lawful)
);
```

**Evidence:** `sql/pg/000_tables.sql:777–786`

#### Cluster Sectors Mapping

```sql
CREATE TABLE cluster_sectors (
    cluster_id bigint NOT NULL,
    sector_id integer NOT NULL,
    PRIMARY KEY (cluster_id, sector_id),
    FOREIGN KEY (cluster_id) REFERENCES clusters,
    FOREIGN KEY (sector_id) REFERENCES sectors
);
```

**Evidence:** `sql/pg/000_tables.sql:788–794`

#### Cluster Player Status (Law Enforcement Record)

```sql
CREATE TABLE cluster_player_status (
    cluster_id bigint NOT NULL,
    player_id integer NOT NULL,
    suspicion integer NOT NULL DEFAULT 0,
    bust_count integer NOT NULL DEFAULT 0,
    last_bust_at text,
    wanted_level integer NOT NULL DEFAULT 0,
    banned boolean NOT NULL DEFAULT FALSE,
    PRIMARY KEY (cluster_id, player_id)
);
```

**Evidence:** `sql/pg/000_tables.sql:806–817`

#### Law Enforcement Configuration

```sql
CREATE TABLE law_enforcement (
    law_enforcement_id serial PRIMARY KEY CHECK (law_enforcement_id = 1),
    robbery_evil_threshold integer DEFAULT -10,
    robbery_xp_per_hold integer DEFAULT 20,
    robbery_credits_per_xp integer DEFAULT 10,
    robbery_bust_chance_base double precision DEFAULT 0.05,
    robbery_turn_cost integer DEFAULT 1,
    good_guy_bust_bonus double precision DEFAULT 0.10,
    pro_criminal_bust_delta double precision DEFAULT -0.02,
    evil_cluster_bust_bonus double precision DEFAULT 0.05,
    good_align_penalty_mult double precision DEFAULT 3.0,
    robbery_real_bust_ttl_days integer DEFAULT 7
);
```

**Evidence:** `sql/pg/000_tables.sql:819–831`

#### Related Port Bust Record

```sql
CREATE TABLE port_busts (
    port_id integer NOT NULL,
    player_id integer NOT NULL,
    last_bust_at timestamptz NOT NULL,
    bust_type text NOT NULL,              -- 'real', etc.
    active boolean NOT NULL DEFAULT TRUE,
    PRIMARY KEY (port_id, player_id)
);
```

**Evidence:** `sql/pg/000_tables.sql:833–842`

### Repo Layer Functions (Server Code)

#### Cluster Jurisdiction Lookup

| Function | File | Signature | Behavior |
| :--- | :--- | :--- | :--- |
| `db_ports_get_cluster_id()` | `src/db/repo/repo_ports.c:347` | `int db_ports_get_cluster_id(db_t*, int sector_id, int* cluster_id_out)` | Queries `cluster_sectors` to resolve sector → cluster_id |
| (Inline usage) | `src/server_ports.c:1783` | Inline: `db_ports_get_cluster_id(db, sector_id, &cluster_id)` | Used in port robbery to check ban status |

#### Suspicion & Wanted Status

| Function | File | Signature | Behavior |
| :--- | :--- | :--- | :--- |
| `repo_clusters_get_player_suspicion_wanted()` | `src/db/repo/repo_clusters.c:231` | `int repo_clusters_get_player_suspicion_wanted(db_t*, int cluster_id, int player_id, int* susp_out, int* wanted_out)` | Reads suspicion and wanted_level from `cluster_player_status` |
| `repo_clusters_get_player_banned()` | `src/db/repo/repo_clusters.c:213` | `int repo_clusters_get_player_banned(db_t*, int cluster_id, int player_id, int* banned_out)` | Reads banned flag |

#### Suspicion & Bust Registration

| Function | File | Signature | Behavior |
| :--- | :--- | :--- | :--- |
| `db_ports_increase_suspicion()` | `src/db/repo/repo_ports.c:479` | `int db_ports_increase_suspicion(db_t*, int cluster_id, int player_id, int susp_inc)` | Upsert: tries UPDATE; if no rows, INSERT with suspicion = susp_inc |
| `db_ports_insert_real_bust()` | `src/db/repo/repo_ports.c:549` | `int db_ports_insert_real_bust(db_t*, int port_id, int player_id)` | Upsert: tries UPDATE; if no rows, INSERT with bust_type='real' |
| `db_ports_check_active_bust()` | `src/db/repo/repo_ports.c:515` | Checks if player has active bust at port | Called before robbery to prevent re-robbery at same port |

#### Wanted Level Escalation

| Function | File | Signature | Behavior | Status |
| :--- | :--- | :--- | :--- | :--- |
| (Implicit in SQL) | `src/server_ports.c:589` | `UPDATE cluster_player_status SET banned=1 WHERE cluster_id=... AND wanted_level >= 3` | Sets banned=TRUE when wanted_level reaches 3+ | ⚠️ No `wanted_level` increment logic found in code |

**Critical gap:** No code increments `wanted_level`; banning only happens if `wanted_level >= 3` (hardcoded threshold), but no query sets `wanted_level` > 0.

#### Decay & Cleanup (Cron)

| Function | File | Trigger | Behavior |
| :--- | :--- | :--- | :--- |
| `db_cron_robbery_decay_suspicion()` | `src/db/repo/repo_cron.c:815` | Daily | `UPDATE cluster_player_status SET suspicion = CAST(suspicion * 0.9 AS INTEGER) WHERE suspicion > 0;` |
| `db_cron_robbery_clear_busts()` | `src/db/repo/repo_cron.c:835` | Daily | Deletes `port_busts` rows older than `robbery_real_bust_ttl_days` (default 7) |
| `h_robbery_daily_cleanup()` | `src/server_cron.c:827` | Server daily cron | Calls both above functions |

**Evidence:** `src/server_cron.c:827–862`

### Sector → Cluster Resolution

**Current pattern:**
1. Player is in `sector_id`
2. Code calls `db_ports_get_cluster_id(db, sector_id, &cluster_id)` 
3. Queries `SELECT cluster_id FROM cluster_sectors WHERE sector_id = {1}` (implicit join or single-table lookup)
4. Uses returned `cluster_id` for all `cluster_player_status` operations

**Evidence:** `src/server_ports.c:1782–1784` (example usage in port robbery)

---

## 5) Fed vs Local vs Lawless Jurisdiction Resolution (Current vs Missing)

### Current Jurisdiction Logic

#### FedSpace (Sectors 1–10)

| Property | Status | Evidence |
| :--- | :--- | :--- |
| **Defined** | ✅ YES | Macro in `src/server_cron.c:33–34`: `#define FEDSPACE_SECTOR_START 1` and `FEDSPACE_SECTOR_END 10` |
| **Helper function** | ✅ YES | `is_fedspace_sector(int sector_id)` in `src/server_cmds.c:36` |
| **DB flag** | ✅ YES | `sectors.is_fedspace = TRUE` for sectors 1–10 |
| **Cluster assignment** | ✅ YES | Federation Core cluster (role='FED', law_severity=3) |
| **Hard punish (Captain Z)** | ✅ YES | `fedspace_enforce_no_aggression_hard()` destroys + pods on aggression |
| **Soft deny (assets)** | ⚠️ Partial | Beacons refused; fighters/mines still allowed (stub ISS summon does nothing) |

**Code path:** `src/server_combat.c:78–133`, `src/server_cmds.c:36`, `src/server_cron.c:33–34`

#### Federation Cluster (Current Definition)

| Property | Value | Evidence |
| :--- | :--- | :--- |
| **Name** | "Federation Core" | `sql/pg/040_functions.sql` (cluster generation) |
| **Role** | 'FED' | Cluster type for federation |
| **Members** | Sectors 1–10 + all MSL sectors | `docs/CLUSTER_GENERATION_V2.md:88–95` |
| **Law Severity** | 3 (severe) | `sql/pg/040_functions.sql`: `law_severity = 3` |
| **Alignment** | 100 (lawful) | `sql/pg/040_functions.sql`: `alignment = 100` |
| **Police behavior** | Hard punish only (Captain Z) | Defined in code; soft deny missing |

**Evidence:** `docs/CLUSTER_GENERATION_V2.md`, `sql/pg/040_functions.sql:580–595`

#### Random Clusters (Default)

| Property | Value | Evidence |
| :--- | :--- | :--- |
| **Count** | 9 (default, configurable) | `docs/CLUSTER_GENERATION_V2.md:80` |
| **Role** | 'RANDOM' | Cluster generation function |
| **Law Severity** | 1 (neutral) | `sql/pg/040_functions.sql`: `law_severity = 1` |
| **Alignment** | -100 to +100 (random) | `sql/pg/040_functions.sql` |
| **Police behavior** | **NOT SPECIFIED IN CODE** | No jurisdiction rules for random clusters |
| **Port robbery rules** | Suspicion increases; ban possible | Implemented in `cmd_port_rob()` |

**Evidence:** `docs/CLUSTER_GENERATION_V2.md:97–106`, `sql/pg/040_functions.sql`

#### Orion Cluster (Status: Planned, Not Implemented)

| Property | Value | Evidence |
| :--- | :--- | :--- |
| **Definition** | NPC home cluster (faction) | `docs/CLUSTER_GENERATION_V2.md:45` |
| **Law Severity** | **NOT SPECIFIED** | Documentation says "Orion/Ferengi NOT YET IMPLEMENTED in v2" |
| **Alignment** | **NOT SPECIFIED** | |
| **Police behavior** | **UNDEFINED** | No code governs Orion jurisdiction rules |
| **Lawless claim** | ❓ Unconfirmed | User requirement says Orion is lawless (law_severity=0) but not implemented |

**Evidence:** `docs/CLUSTER_GENERATION_V2.md:285` ("Future Work: Add Orion/Ferengi cluster creation in v2")

#### Ferengi Cluster (Status: Planned, Not Implemented)

| Property | Value | Evidence |
| :--- | :--- | :--- |
| **Definition** | NPC home cluster (faction) | `docs/CLUSTER_GENERATION_V2.md:45` |
| **Law Severity** | **NOT SPECIFIED** | Documentation says "Orion/Ferengi NOT YET IMPLEMENTED in v2" |
| **Alignment** | **NOT SPECIFIED** | |
| **Police behavior** | **UNDEFINED** | No code governs Ferengi jurisdiction rules |

**Evidence:** `docs/CLUSTER_GENERATION_V2.md:285`

#### MSL Sectors (Major Space Lanes)

| Property | Value | Evidence |
| :--- | :--- | :--- |
| **Definition** | Trade routes from FedSpace to all stardocks | `docs/CLUSTER_GENERATION_V2.md:50–68` |
| **Cluster Assignment** | Part of Federation Core | `docs/CLUSTER_GENERATION_V2.md:92` |
| **Jurisdiction** | Federation law_severity=3 | Inherited from Federation cluster |
| **Police behavior** | Captain Z (hard punish only) | Same as FedSpace enforcement |

**Evidence:** `sql/pg/msl_sectors` table, `docs/CLUSTER_GENERATION_V2.md`

### Missing Jurisdiction Logic

#### What Does NOT Exist

| Feature | Required By | Status | Impact |
| :--- | :--- | :--- | :--- |
| **Orion law_severity = 0 (lawless)** | User requirements | ❌ NOT IMPLEMENTED | No code skips police checks in Orion; no bribery-only mode for Orion |
| **Ferengi law_severity = ? (special)** | User requirements | ❌ NOT IMPLEMENTED | No special Ferengi police rules (trade/theft behavior) |
| **Random cluster police behavior** | Cluster jurisdiction model | ❌ NOT IMPLEMENTED | Port robbery increases suspicion but no escalation to wanted/ban |
| **police.bribe RPC** | Protocol spec | ❌ NOT IMPLEMENTED | No way to pay off police; no bribery mechanism |
| **police.surrender RPC** | Protocol spec | ❌ NOT IMPLEMENTED | No way to surrender to police; no incident resolution |
| **Wanted level escalation logic** | Cluster model (wanted_level column exists) | ❌ PARTIAL | Column exists but never incremented; ban only if wanted_level >= 3 (unreachable) |
| **Cluster-specific police behavior** | Jurisdiction model | ❌ PARTIAL | FedSpace (hard) and random (soft) defined; Orion/Ferengi/Ferengi undefined |

#### What SHOULD Exist (Per Model Requirements)

**Orion Cluster (Lawless)**
- `law_severity = 0`
- Police checks should be **skipped** (no suspicion, wanted, or bans)
- `police.bribe` and `police.surrender` should be unavailable
- Port robbery should succeed without incident tracking

**Federation Cluster (Severe)**
- `law_severity = 3`
- Captain Z (hard punish) for aggression: ✅ Implemented
- Soft deny for assets (fighters/mines in FedSpace): ⚠️ Stub only
- `police.bribe` and `police.surrender` availability: ❌ Not implemented
- Port robbery: triggers suspicion → wanted → ban escalation

**Random Clusters (Neutral)**
- `law_severity = 1`
- Port robbery: triggers suspicion
- Wanted/ban escalation: **Currently missing** (suspicion increases but wanted_level never increments)
- `police.bribe` / `police.surrender`: ❌ Not implemented

---

## 6) Test Coverage Audit

### Existing Test Files Mentioning Crime/Police Concepts

| Suite File | Tests | Coverage |
| :--- | :--- | :--- |
| `suite_combat_port_rob.json` | 7 | Port robbery mechanics (basic positive/negative) |
| `suite_combat_advanced.json` | 5 | References port.rob; limited police coverage |
| `suite_combat_and_crime.json` | 1 | FedSpace aggression (minimal: only checks status=ok) |
| `suite_economy_fine_pay.json` | 5 | Fine payment system (unrelated to police RPCs) |
| `suite_bounty.json` | 5 | Bounty posting/listing (not police bribe/surrender) |
| Test harness: `test_rig.json` | Setup | Creates "wanted_criminal" user for testing |

### Test Coverage Matrix

| Scenario | Test Exists? | Suite | Notes |
| :--- | :--- | :--- | :--- |
| **Port robbery succeeds** | ✅ YES | `suite_combat_port_rob.json` | Basic positive case (evil alignment) |
| **Port robbery fails (no auth)** | ✅ YES | `suite_combat_port_rob.json` | Negative case (401) |
| **Suspicion increases on bust** | ❌ NO | — | Not tested; DB mutation not verified |
| **Suspicion decays daily** | ❌ NO | — | Cron job `db_cron_robbery_decay_suspicion()` untested |
| **Bust expires after 7 days** | ❌ NO | — | Cron job `db_cron_robbery_clear_busts()` untested |
| **Wanted level escalates** | ❌ NO | — | No wanted_level increment in code; test N/A |
| **Player banned when wanted >= 3** | ❌ NO | — | Wanted level never increments; ban unreachable |
| **FedSpace aggression pods player** | ✅ PARTIAL | `suite_combat_and_crime.json` | Test name suggests FedSpace check; actual assertion only checks status=ok |
| **Port robbery in FedSpace triggers Captain Z** | ❌ NO | — | Not tested |
| **police.bribe RPC** | ❌ NO | — | RPC not implemented; test N/A |
| **police.surrender RPC** | ❌ NO | — | RPC not implemented; test N/A |
| **player.illegal_act.v1 event emitted** | ❌ NO | — | Event never emitted; test N/A |
| **Cluster ban prevents docking/robbery** | ❌ NO | — | Ban flag checked; enforcement not tested |

### Test Implementation Gaps (Critical)

1. **Suspicion System:**
   - Need test to verify `db_ports_increase_suspicion()` modifies `cluster_player_status`
   - Need test to verify daily cron decay (suspicion *= 0.9)
   - Need test to verify bust TTL expiry (7 days)

2. **Wanted Escalation:**
   - Need test to escalate from suspicion → wanted_level
   - Need test to escalate from wanted_level → banned
   - **Blocker:** No code increments `wanted_level`; logic missing

3. **Police RPCs:**
   - Need test for `police.bribe` (command not registered)
   - Need test for `police.surrender` (command not registered)
   - Need protocol specification for request/response schemas

4. **FedSpace Soft Deny:**
   - Need test for fighter deployment refusal in FedSpace
   - Need test for mine deployment refusal in FedSpace
   - **Blocker:** `iss_summon()` is stub; no side effects

5. **Jurisdiction-Specific Behavior:**
   - Need test for Orion cluster (lawless) port robbery
   - Need test for Ferengi cluster behavior (not yet defined)
   - Need test for random cluster escalation

---

## 7) Minimal Implementation Plan

### Phase A: Protocol Specification & Command Registration

**Goal:** Define `police.bribe` and `police.surrender` RPCs; register in command loop.

#### A1. Define police.bribe RPC

**Required decisions:**
- **Request schema:** cluster_id/sector_id, bribe_amount, optional officer_id
- **Response schema:** success, message, new_suspicion, incident_cleared
- **Behavior:** Probability-based bribe success depending on alignment, cluster, amount, suspicion

#### A2. Define police.surrender RPC

**Required decisions:**
- **Request schema:** cluster_id/sector_id, optional confession
- **Response schema:** success, sentence details (turn_penalty, fine, rehabilitation)
- **Behavior:** Voluntary incident resolution with penalties

#### A3. Register RPC Handlers in command loop
- Add to `src/server_loop.c`
- Create stub handlers

**Acceptance criteria:**
- Commands appear in help output
- Parser recognizes input without error

---

### Phase B: Incident State Management & Escalation

**Goal:** Implement wanted_level escalation and incident resolution.

#### B1. Define Escalation Logic

| Suspicion Range | Wanted Level | Incident State |
| :--- | :--- | :--- |
| 0–20 | 0 | None |
| 21–40 | 1 | Minor suspect |
| 41–70 | 2 | Major suspect |
| 71–100 | 3 | Wanted criminal |

#### B2. Define Incident Expiry Paths
- **Decay:** Daily suspicion decay (already implemented)
- **Bribery:** Police.bribe success → reset incident
- **Surrender:** Police.surrender → accept penalty + reset
- **Ban:** wanted_level >= 3 → banned flag

#### B3. Jurisdiction-Specific Rules

**FedSpace (law_severity = 3):**
- Hard punish (destroy+pod) for aggression
- Soft deny for asset deployment
- Bribe success rate = 0%
- Surrender = automatic

**Random Cluster (law_severity = 1):**
- Suspicion tracking
- Escalation path available
- Bribe success = 50–70%

**Orion Cluster (law_severity = 0):**
- No police presence
- Port robbery: no tracking
- Bribe/Surrender: unavailable

---

### Phase C: Jurisdiction Rules (Cluster-Specific Police Behavior)

**Goal:** Enforce cluster-specific law and police availability.

#### C1. Orion Cluster Setup
- Create with `law_severity = 0`
- Skip suspicion tracking in port robbery
- Disable bribe/surrender

#### C2. Federation Cluster Behavior
- Implement soft deny (asset refusal)
- Replace `iss_summon()` stub with actual enforcement

#### C3. Random Cluster Behavior
- Enable wanted_level escalation
- Implement bribe/surrender handlers

#### C4. Ferengi Cluster
- Deferred pending design

---

### Phase D: Triggers & Crime Categories

**Goal:** Ensure all crime actions tracked correctly by jurisdiction.

#### D1. Confirmed Triggers
- Port robbery (all clusters)
- FedSpace aggression (hard punish)
- Asset deployment in FedSpace (soft deny)

#### D2. Optional Future Triggers
- Contraband trade
- Piracy
- Bounty execution

---

### Phase E: Tests to Add

**Core test categories:**
1. Suspicion & escalation (suspicion increases, decays, escalates)
2. Police RPCs (bribe success/failure, surrender acceptance)
3. Jurisdiction rules (FedSpace hard/soft, Orion lawless, random escalation)
4. Edge cases (concurrent writes, TTL expiry, alignment-based variation)

**~20 new tests total**

---

## 8) Summary of Gaps

### Critical Gaps (Blocking Parity)

| Gap | Impact | Severity |
| :--- | :--- | :--- |
| **`police.bribe` RPC not implemented** | No way to resolve incidents via bribery | �� CRITICAL |
| **`police.surrender` RPC not implemented** | No way to resolve incidents via surrender | 🔴 CRITICAL |
| **Wanted_level escalation logic missing** | Suspicion tracked but never escalates to wanted/ban | 🔴 CRITICAL |
| **`player.illegal_act.v1` event never emitted** | Protocol event documented but never triggered | 🟠 HIGH |
| **FedSpace soft deny (assets) is stub only** | Fighters/mines deployable in FedSpace (should be refused) | 🟠 HIGH |
| **Orion cluster law_severity not set to 0** | Lawless clusters have no special behavior | 🟠 HIGH |

### Minor Gaps (Quality/Coverage)

| Gap | Impact | Severity |
| :--- | :--- | :--- |
| **Test coverage for suspicion/wanted escalation** | No tests verify core mechanics | 🟡 MEDIUM |
| **Ferengi cluster behavior undefined** | Placeholder cluster with no rules | 🟡 MEDIUM |
| **Protocol spec incomplete (police RPCs)** | Schemas not formally documented | 🟡 MEDIUM |

---

## References

### Code Locations

| Component | File | Lines |
| :--- | :--- | :--- |
| **FedSpace enforcement (Captain Z)** | `src/server_combat.c` | 68–133 |
| **Port robbery trigger** | `src/server_ports.c` | 1706–1900+ |
| **Suspicion increase** | `src/db/repo/repo_ports.c` | 479–507 |
| **Bust registration** | `src/db/repo/repo_ports.c` | 549–577 |
| **Cluster ban check** | `src/db/repo/repo_ports.c` | 354–369 |
| **Daily decay cron** | `src/db/repo/repo_cron.c` | 815–841 |
| **Command registry** | `src/server_loop.c` | 1–500+ (no police.* entries) |
| **Cluster jurisdiction lookup** | `src/server_ports.c` | 1782–1784 |

### Database Locations

| Object | File | Type |
| :--- | :--- | :--- |
| **clusters** | `sql/pg/000_tables.sql:777` | Table |
| **cluster_sectors** | `sql/pg/000_tables.sql:788` | Table |
| **cluster_player_status** | `sql/pg/000_tables.sql:806` | Table |
| **law_enforcement** | `sql/pg/000_tables.sql:819` | Table (config) |
| **port_busts** | `sql/pg/000_tables.sql:833` | Table |
| **msl_sectors** | `sql/pg/000_tables.sql:712` | Table |

### Documentation Locations

| Document | File | Scope |
| :--- | :--- | :--- |
| **Protocol: Police & Legal** | `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md:19–22` | RPC definitions (stub) |
| **Intra-Stack: Events** | `docs/Intra-Stack_Protocol.md:56, 156, 240` | Event schemas and client RPC mapping |
| **Cluster Generation v2** | `docs/CLUSTER_GENERATION_V2.md` | Cluster seeding, MSL, federation setup |
| **FedSpace Enforcement Audit** | `docs/reports/fedspace_enforcement_audit.md` | Prior investigation (Captain Z) |
| **ISS Captain Z Investigation** | `docs/reports/iss_captainz_enforcement_investigation.md` | Detailed enforcement stubs |

### Test Locations

| Suite | File | Tests |
| :--- | :--- | :--- |
| **Port Robbery** | `tests.v2/suite_combat_port_rob.json` | 7 (basic mechanics) |
| **Combat & Crime** | `tests.v2/suite_combat_and_crime.json` | 1 (FedSpace, minimal assertion) |
| **Economy Fine Pay** | `tests.v2/suite_economy_fine_pay.json` | 5 (fine payment, not police) |
| **Bounty System** | `tests.v2/suite_bounty.json` | 5 (bounty posting, not police) |

---

## Conclusion

The **police / ISS / bribery / surrender system is a framework in progress:**

1. **Database schema** is complete (`cluster_player_status`, law_enforcement config)
2. **Port robbery trigger** is implemented (suspicion tracking)
3. **FedSpace hard enforcement** (Captain Z) is implemented
4. **Police RPCs** are documented but NOT implemented
5. **Wanted escalation logic** is missing (wanted_level column exists but never incremented)
6. **Cluster jurisdiction rules** are partially defined (federation yes; Orion/Ferengi no)
7. **Test coverage** is minimal (mechanics tested; escalation/police not tested)

**To achieve parity:**
- Implement `police.bribe` and `police.surrender` RPCs with request/response schemas
- Add wanted_level escalation logic (cron or event-driven)
- Implement cluster-specific police behavior (hard, soft, lawless)
- Add comprehensive test suite for all phases
- Update protocol documentation with RPC schemas

The **Phase A–E implementation plan** above provides a concrete roadmap to full parity.

---

**End of Police Parity Audit Report**
