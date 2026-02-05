# 27. NPC & Ferengi AI

## 1. NPC Commands (Engine Enqueued)

These are primarily Engine-driven commands executed by the Server.

*   **`npc.spawn.v1`**: Create a new NPC.
*   **`npc.move.v1`**: Move an NPC.
*   **`npc.attack.v1`**: Initiate NPC attack.
*   **`npc.deploy_fighters.v1`**: Drop defenses.
*   **`npc.destroy.v1`**: Event when NPC is killed.

## 2. Behaviors

*   **Imperial**: Enforcement.
*   **Ferengi**: Trade and theft.
*   **Scaffold**: Basic patrol/waypoint logic.

## 3. Police & Legal

*   **`player.illegal_act.v1`**: Event when player breaks law.
*   **`police.bribe`**: Attempt to bribe local police (Phase C: real effects).
*   **`police.surrender`**: Surrender to local police (Phase C: real effects).

### 3.1 police.bribe

**Phase:** C (real gameplay effects; deterministic)

**Request:**
```json
{
  "amount": <int>  // Bribe amount (must be positive; scales with enforcement level)
}
```

**Response (success - bribe accepted):**
```json
{
  "status": "ok",
  "data": {
    "action": "bribe",
    "sector_id": <int>,
    "cluster_id": <int>,
    "cluster_role": <string>,
    "law_severity": <int>,
    "bribe_amount": <int>,
    "success": true,
    "suspicion_before": <int>,
    "wanted_level_before": <int>,
    "suspicion_after": 0,
    "wanted_level_after": <int>,
    "message": "Bribe accepted. Your record has been partially cleared."
  }
}
```

**Response (failure - bribe rejected):**
```json
{
  "status": "error",
  "error": {
    "code": 201,
    "message": "Bribe rejected. Suspicion increased."
  },
  "data": {
    "action": "bribe",
    "sector_id": <int>,
    "cluster_id": <int>,
    "cluster_role": <string>,
    "law_severity": <int>,
    "bribe_amount": <int>,
    "success": false,
    "suspicion_before": <int>,
    "wanted_level_before": <int>,
    "suspicion_after": <int>,
    "wanted_level_after": <int>,
    "message": "Bribe rejected. Suspicion increased!"
  }
}
```

**Error Codes (Phase C):**
- `1410` (ERR_NO_POLICE_PRESENCE): No police in lawless clusters (law_severity == 0).
- `1411` (ERR_POLICE_BRIBE_REFUSED): Bribery always refused in Federation cluster (role == 'FED').
- `1413` (ERR_NOTHING_TO_SURRENDER): No active incident to bribe away (suspicion=0, wanted_level=0).
- `400` (ERR_BAD_REQUEST): Bribe amount invalid (must be positive).

**Bribe Success Rule (Deterministic, Phase C):**
```
success = (bribe_amount >= 100 * (wanted_level + 1))
```
- **wanted_level == 1**: Requires bribe >= 200 credits
- **wanted_level == 2**: Requires bribe >= 300 credits
- **wanted_level == 3**: Requires bribe >= 400 credits

**Effects on Success:**
- Suspicion is cleared (set to 0)
- Wanted level is reduced by 1
- Banned flag cleared if wanted_level drops below 3

**Effects on Failure:**
- Suspicion is incremented by 1
- If suspicion >= 3, wanted_level is promoted and suspicion resets to 0
- Banned flag set if wanted_level reaches 3

**Jurisdiction Rules:**
- **Lawless (law_severity == 0)**: Return ERR_NO_POLICE_PRESENCE (no police to bribe).
- **Federation (role == 'FED')**: Return ERR_POLICE_BRIBE_REFUSED (bribes never accepted).
- **Other lawful clusters (law_severity > 0, non-FED)**: Full Phase C implementation (success/failure possible).
- **No incident**: Return ERR_NOTHING_TO_SURRENDER (must have active incident).
- **Unclaimed sectors**: Return ERR_NO_POLICE_PRESENCE.

### 3.2 police.surrender

**Phase:** C (real gameplay effects)

**Request:**
```json
{
  // No required fields in Phase C
}
```

**Response (success - Federal):**
```json
{
  "status": "ok",
  "data": {
    "action": "surrender",
    "sector_id": <int>,
    "cluster_id": <int>,
    "cluster_role": "FED",
    "law_severity": <int>,
    "suspicion_before": <int>,
    "wanted_level_before": <int>,
    "suspicion_after": 0,
    "wanted_level_after": 0,
    "message": "You have surrendered to the Federation. Your record has been cleared."
  }
}
```

**Response (success - Local):**
```json
{
  "status": "ok",
  "data": {
    "action": "surrender",
    "sector_id": <int>,
    "cluster_id": <int>,
    "cluster_role": <string>,
    "law_severity": <int>,
    "suspicion_before": <int>,
    "wanted_level_before": <int>,
    "suspicion_after": <int>,
    "wanted_level_after": <int>,
    "message": "You have surrendered to local authorities. Your enforcement level has been reduced."
  }
}
```

**Response (error):**
```json
{
  "status": "error",
  "error": {
    "code": <int>,
    "message": <string>
  },
  "data": {
    "action": "surrender",
    "sector_id": <int>,
    "cluster_id": <int>,
    "cluster_role": <string>,
    "law_severity": <int>
  }
}
```

**Error Codes (Phase C):**
- `1410` (ERR_NO_POLICE_PRESENCE): No police in lawless clusters (law_severity == 0).
- `1413` (ERR_NOTHING_TO_SURRENDER): No active incident (suspicion=0, wanted_level=0).

**Surrender Effects (Phase C):**

**Federation (role == 'FED'):**
- Clears ALL incident fields:
  - `suspicion := 0`
  - `wanted_level := 0`
  - `banned := false`
- Message: "Your record has been cleared."
- Note: Full record wipe is Federation jurisdiction privilege; other clusters do not offer full amnesty.

**Local lawful (law_severity > 0, non-FED):**
- Reduces incident state by ONE tier:
  - If `wanted_level > 0`: Decrement `wanted_level` by 1
  - Else if `suspicion > 0`: Clear `suspicion`
- Clears `banned` flag ONLY if `wanted_level` drops below 3
- Message: "Your enforcement level has been reduced."
- Note: Surrender to local authorities negotiates a reduced status, not full amnesty.

**Jurisdiction Rules:**
- **Lawless (law_severity == 0)**: Return ERR_NO_POLICE_PRESENCE.
- **Federation (role == 'FED')**: Full incident wipe (all fields cleared).
- **Local lawful (law_severity > 0, non-FED)**: Reduce by one tier.
- **No incident**: Return ERR_NOTHING_TO_SURRENDER.
- **Unclaimed sectors**: Return ERR_NO_POLICE_PRESENCE.

## 4. Phase A Implementation Notes

These RPCs are jurisdiction-aware stubs that correctly refuse or defer actions based on cluster law_severity and role. Full gameplay (bribery probability, sentencing, confiscation, record wipes) is deferred to Phase B and later phases.

**Cluster Jurisdiction Summary:**
| Cluster Type | law_severity | Role | bribe | surrender |
| :--- | :--- | :--- | :--- | :--- |
| FedSpace (1-10) | 3 | FED | Refused | Not impl. |
| Federation (11+) | 3 | FED | Refused | Not impl. |
| Random | 1 | RANDOM | Not impl. | Not impl. |
| Orion | 0 | ORION | No police | No police |
| Ferengi | TBD | FERENGI | TBD | TBD |
| Unclaimed | — | — | No police | No police |

## 5. Phase B: Incident State Model

**Incident State Definition:**

An "active incident" exists when a player has:
- `suspicion > 0` OR `wanted_level > 0` OR `banned = 1`

These fields are persisted in the `cluster_player_status` table, keyed by `(cluster_id, player_id)`.

**Phase B Mechanics (Stub):**

1. **Incident Triggers:** Crime actions (e.g., port robbery) increment `suspicion` in the player's current cluster only if lawful (law_severity > 0 and not FED 1-10).

2. **Suspicion Thresholds:**
   - `suspicion >= 3` → promotes to `wanted_level += 1` and resets `suspicion := 0`
   - `wanted_level >= 3` → sets `banned := 1` (optional enforcement in Phase C+)

3. **Incident Resolution (Surrender Phase B Stub):**
   - `police.surrender` with active incident clears all fields: `suspicion := 0, wanted_level := 0, banned := 0`
   - Returns `ERR_NOT_IMPLEMENTED` (code 502) with Phase B note (actual sentencing deferred to Phase C)

4. **Bribe with Incident (Phase B Stub):**
   - `police.bribe` with active incident returns `ERR_NOT_IMPLEMENTED` (code 502)
   - Response includes snapshot: `suspicion`, `wanted_level`, `banned` (for client rendering)
   - Full bribery negotiation deferred to Phase C

**Exclusions:**
- Lawless clusters (law_severity == 0): No incident tracking
- FedSpace sectors 1–10: Captain Z handles hard punish; no local incident tracking
- Unclaimed sectors: No cluster context; no incident tracking

## 6. Phase C: Resolution Mechanics & Enforcement Effects

**Real Gameplay Implementation:**

Phase C implements actual consequences for `police.bribe` and `police.surrender` with deterministic, jurisdiction-aware effects.

### 6.1 Surrender Outcomes

**Federation Cluster:** Full amnesty wipes all incident fields (`suspicion=0, wanted_level=0, banned=0`).

**Local Lawful Cluster:** Partial resolution reduces incident state by one tier:
- If wanted_level > 0: decrement by 1
- Else if suspicion > 0: clear
- Banned flag cleared only if wanted_level drops below 3

### 6.2 Bribe Mechanics (Deterministic)

**Bribe Success Rule:** `success = (bribe_amount >= 100 * (wanted_level + 1))`

**On Success:**
- Suspicion cleared to 0
- Wanted level reduced by 1
- Banned flag cleared if wanted_level drops below 3

**On Failure:**
- Suspicion incremented by 1
- If suspicion >= 3, promoted to wanted_level (Phase B threshold logic reused)
- Banned flag set if wanted_level >= 3

### 6.3 Ban Flag Enforcement

When `cluster_player_status.banned == true` for a cluster:

**Sector Entry (move.warp):**
- Warp to any sector in banned cluster is BLOCKED
- Error: `ERR_BANNED_FROM_JURISDICTION` (1412)
- Message: "You are banned from this jurisdiction."

**Planet Landing (planet.land):**
- Landing on any planet in banned cluster is BLOCKED
- Error: `ERR_BANNED_FROM_JURISDICTION` (1412)
- Message: "You are banned from this jurisdiction and cannot land."

**Note:** Ban is enforced at cluster level, not per-sector; one banned status blocks all access within that cluster.

### 6.4 Phase C Error Codes

- `1412` (ERR_BANNED_FROM_JURISDICTION): Player banned from cluster; entry/landing blocked.
- `1413` (ERR_NOTHING_TO_SURRENDER): No active incident to resolve.

### 6.5 What Phase C Does NOT Yet Implement

- Bounty hunting
- Jail time or asset confiscation
- Sentencing beyond incident state
- Unbanning procedures (deferred to Phase D)
- Cluster-tuned bribe thresholds (Phase D)
- Record wiping outside Federation (Phase D)



