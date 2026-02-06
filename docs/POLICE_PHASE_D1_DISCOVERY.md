# Police Phase D1 Discovery Results

## CONFIRMED HOOK POINTS

### 1. Docking Enforcement Gate
**File:** `src/server_ports.c`  
**Function:** `cmd_dock_status()` (line 977)  
**Hook Point:** Line 1049 (after ban check, before actual docking)

**Current Flow:**
```c
// Line 1049: Ban check happens here
if (new_ported_status > 0 && !cluster_can_trade(db, ctx->sector_id, ctx->player_id))
{
    send_response_refused_steal(..., "Port refuses docking: You are banned");
    return 0;
}
// Line 1062: Actual docking happens here
db_ports_set_ported_status(db, player_ship_id, new_ported_status);
```

**Action for D1:** Insert enforcement gate between ban check and docking:
- Resolve sector → cluster info (law_severity, role)
- If lawful AND wanted_level >= ENFORCE_WANTED_THRESHOLD: return intercept response
- Otherwise: proceed to docking

**Related Helper:** `h_get_cluster_info_for_sector()` already exists in `src/server_police.c` (line 52)

---

### 2. Port Aggression (Robbery)
**File:** `src/server_ports.c`  
**Function:** `cmd_port_rob()` (line 1706)  
**Critical Detail:** This is PORT ROBBERY, not port ATTACK. No port attack endpoint exists.

**Current Behavior:**
- FedSpace 1–10: Hard enforced by `fedspace_enforce_no_aggression_hard()` (line 1737)
- Other clusters: Checks ban status (lines 1781–1797)
- Mode "goods" checks illegal commodity status (line 1799+)

**Decision:** CRIME_ATTACK_PORT records on `cmd_port_rob()` execution (post-success). This means:
- Recording happens after the robbery succeeds (or fails?)
- Severity: +2 (port aggression is more serious than contraband)

**Action for D1:** Wire crime recording after successful robbery execution

---

### 3. Contraband Trading
**File:** `src/server_ports.c`  
**Functions:** `cmd_trade_buy()` (line 3009), `cmd_trade_sell()` (line 2248)

**Legality Check:** Function `h_can_trade_commodity()` (defined around line 536)
**Illegal Detection:**
- `h_is_illegal_commodity(db, commodity_code)` — checks commodities table
- Player alignment check: if `player_alignment > neutral_band_value`, illegal trade refused
- Port cluster alignment check: evil ports allow illegal; good ports refuse

**Key Trade Path:**
- `cmd_trade_buy()`: Line 3009 onwards
- `cmd_trade_sell()`: Line 2248 onwards
- Both call `h_can_trade_commodity()` to gate the trade

**Action for D1:**
- After successful trade of illegal commodity: record CRIME_CONTRABAND (+1)
- ONLY if cluster.law_severity > 0
- NEVER if cluster.law_severity == 0 (lawless invariant)

---

## CONFIRMED INCIDENT TRACKING API (Phase B)

All functions in `src/db/repo/repo_clusters.c` and declared in `src/db/repo/repo_clusters.h`

### Read Incident State
```c
int repo_clusters_get_player_suspicion_wanted(
    db_t *db, 
    int cluster_id, 
    int player_id,
    int *suspicion_out,     // Pointer to receive suspicion value
    int *wanted_out         // Pointer to receive wanted_level value
);
```
**Line:** 231  
**Returns:** 0 on success; suspicion and wanted_level are 0 if no row exists

### Upsert & Increment Suspicion
```c
int repo_clusters_upsert_player_status(
    db_t *db, 
    int cluster_id, 
    int player_id,
    int susp_inc,          // Suspicion increment (severity points)
    int busted             // Bool: 1 if this was a bust event
);
```
**Line:** 251  
**Behavior:**
- INSERT if row doesn't exist: suspicion = susp_inc, bust_count = busted ? 1 : 0
- UPDATE if exists: suspicion += susp_inc, bust_count += busted ? 1 : 0, set last_bust_at if busted
- Does NOT promote to wanted; does NOT check thresholds

### Promote Suspicion → Wanted
```c
int repo_clusters_promote_wanted_from_suspicion(
    db_t *db, 
    int cluster_id, 
    int player_id
);
```
**Line:** 336  
**Behavior:**
- Reads current suspicion and wanted_level
- If suspicion >= 3: increment wanted_level, reset suspicion to 0, set banned = 1 if wanted >= 3
- Returns 0 on success

**CRITICAL THRESHOLD:** suspicion >= 3 triggers promotion (Phase B hardcoded)

### Check Player Banned
```c
int repo_clusters_get_player_banned(
    db_t *db, 
    int cluster_id, 
    int player_id,
    int *banned_out
);
```
**Line:** (in header, not shown, but used by cluster_can_trade)

---

## CONFIRMED CLUSTER RESOLUTION HELPER

**File:** `src/server_police.c`  
**Function:** `h_get_cluster_info_for_sector()` (line 52, static)

**Signature:**
```c
typedef struct {
  int cluster_id;
  char role[64];              /* "FED", "RANDOM", "ORION", "FERENGI", etc. */
  int law_severity;
} cluster_info_t;

static int h_get_cluster_info_for_sector(
    db_t *db, 
    int sector_id,
    cluster_info_t *info_out
);
```

**Returns:**
- 0: Found, info_out populated
- 1: Sector not in any cluster (unclaimed)
- <0: DB error

**Action:** Make this function PUBLIC by moving to repo_clusters or creating wrapper, so it can be reused in port/trade handlers

---

## EXISTING FedSpace Handling (DO NOT MODIFY)

**File:** `src/server_ports.c`  
**Function:** `fedspace_enforce_no_aggression_hard()` (called from cmd_port_rob line 1737)

**Behavior:** Sectors 1–10 are hard-enforced by Captain Z. Any aggression is refused outright.

**For D1:** Do NOT record CRIME_ATTACK_PORT in FedSpace 1–10. Let Captain Z handle it silently.

---

## THRESHOLDS & CONSTANTS TO DEFINE

Based on Phase B and Phase D specs:

```c
// src/server_police.h (NEW SECTION)

#define CRIME_ATTACK_PORT     1
#define CRIME_CONTRABAND      2

#define ENFORCE_WANTED_THRESHOLD 2    // Block dock if wanted >= 2
#define SUSPICION_WANTED_THRESHOLD 3  // Promote to wanted if suspicion >= 3 (from Phase B)
```

---

## LAWLESS INVARIANT ENFORCEMENT

**CRITICAL:** Every crime recording call must guard against lawless clusters:

```c
// Pseudo-code pattern
if (cluster.law_severity == 0) {
    return;  // NO DB writes, NO row creation, ever
}

// Then proceed with incident tracking
repo_clusters_upsert_player_status(...);
repo_clusters_promote_wanted_from_suspicion(...);
```

**Verification in Tests:** Tests will confirm that lawless clusters never have cluster_player_status rows created/modified.

---

## POLICE.STATUS RPC (New Read-Only Endpoint)

**Caller:** Test suites, admin tools  
**Parameters:** `player_id, cluster_id (optional)`  
**Returns (JSON):**
```json
{
  "ok": true,
  "data": {
    "cluster_id": 2,
    "suspicion": 1,
    "wanted_level": 0,
    "banned": false
  }
}
```

**Implementation:** New handler in `src/server_police.c` that calls Phase B repo functions to read state (no mutations).

---

## SUMMARY TABLE: Hook Points for D1

| Feature | File | Function | Line | Action | Guard |
|---------|------|----------|------|--------|-------|
| **Docking Intercept** | server_ports.c | cmd_dock_status | 1049 | Check wanted >= 2, block dock + return intercept response | Lawful only (law_severity > 0) |
| **Port Robbery → Crime** | server_ports.c | cmd_port_rob | 1706+ | After success: record CRIME_ATTACK_PORT +2 | Skip FedSpace 1–10; skip lawless |
| **Contraband Trade → Crime** | server_ports.c | cmd_trade_buy/sell | 3009/2248 | After illegal trade: record CRIME_CONTRABAND +1 | Skip lawless (law_severity == 0) |
| **Police Status** | server_police.c | (new) handle_police_status | — | Query cluster_player_status, return read-only state | Read DB only |

---

## NOTES FOR IMPLEMENTATION

1. **Reusable Helper:** `h_get_cluster_info_for_sector()` is currently static. Make it public or create a public wrapper in `repo_clusters.c` so all handlers can use it.

2. **Single Entry Point:** Create `police_record_crime()` function in `repo_clusters.c` that:
   - Takes (db, player_id, sector_id, crime_type, severity_points)
   - Internally resolves sector → cluster info
   - Checks law_severity == 0 FIRST (return immediately if lawless)
   - Checks role == FED && sector in 1–10 SECOND (return immediately)
   - Calls upsert and promotion helpers

3. **Response Shape:** Dock intercept must return a new response type with `enforcement` field containing cluster_id, wanted_level, and options array. Do not reuse existing refuse_steal responses.

4. **Test Strategy:** Find a lawful test port and a lawless test port via DB queries. Use them deterministically in JSON suites. No hardcoded sector IDs except FedSpace 1–10 check.

---
