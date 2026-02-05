# Cluster Generation v2 & Major Space Lanes (MSL)

## Overview

As of the Cluster Generation v2 update, the universe initialization process for clusters and MSL has been consolidated into the **BigBang** phase of universe creation. This document describes the new pipeline and lifecycle.

## Key Changes

### Before
- **MSL Generation**: Server startup (cron) during first boot
- **Cluster Creation**: Early in BigBang, limited to 1-hop neighbors only
- **Timing**: Clusters and MSL initialized asynchronously

### After
- **MSL Generation**: BigBang (after warps exist)
- **Cluster Generation v2**: BigBang (after homeworlds, using flood-fill BFS)
- **Timing**: All synchronized and deterministic
- **Server Startup**: MSL and clusters are read-only (validation only)

---

## BigBang Pipeline (Universe Creation)

The complete sequence in `bigbang_pg_main.c`:

1. **Create admin/app database** (DB lifecycle)
2. **Apply schema** (tables, indexes, views)
3. **Generate basic entities:**
   - Sectors (with random beacons)
   - Ports, planets, stardocks, taverns
4. **Create initial clusters** (Federation Core only, stub)
5. **Generate planets** (with ratios)
6. **Spawn initial fleet**
7. **Apply game defaults**
8. **Generate topology:**
   - Tunnel chains (isolated paths connecting distant sectors)
   - NPC homeworlds (Ferengi, Orion)
   - Random warps (full connectivity)
   - FedSpace exits (ensure escape routes from sectors 1-10)
9. **Generate MSL** (NEW: replaces server-startup generation)
   - Computes paths from FedSpace (1-10) to all Stardocks
   - Marks all path sectors in `msl_sectors` table
10. **Generate Clusters v2** (NEW: replaces basic cluster generation)
   - Creates Federation cluster with sectors 1-10 + all MSL sectors
   - Creates Orion/Ferengi clusters (if homeworlds exist)
   - Creates random clusters with flood-fill region assignment (~50% coverage)

---

## MSL Generation (`generate_msl()`)

**Location:** `sql/pg/040_functions.sql`

**Inputs:**
- Sectors table (full universe)
- sector_warps table (all connectivity)
- stardock_location table (major trade hubs)

**Output:**
- msl_sectors table populated with all sectors on paths from FedSpace 1-10 to every stardock

**Algorithm:**
- For each stardock:
  - For each FedSpace sector (1-10):
    - BFS pathfind from FedSpace sector → stardock
    - Insert all path sectors into msl_sectors
- Result: All critical trade route sectors are marked as MSL

**Determinism:**
- SQL pathfinding uses BFS, which is deterministic given the same sector/warp graph
- No random seeding involved

---

## Cluster Generation v2 (`generate_clusters_v2()`)

**Location:** `sql/pg/040_functions.sql`

**Inputs:**
- target_random_cluster_count: Number of random (non-Fed/Orion/Ferengi) clusters (default 9)
- Universe topology (sectors, warps)
- MSL sectors (computed above)

**Output:**
- clusters table: Federation Core, Orion, Ferengi, 9 random clusters
- cluster_sectors table: Membership assignments via multi-hop BFS flood-fill

### Federation Cluster

- **Name:** "Federation Core"
- **Role:** FED
- **Members:** Sectors 1-10 (published FedSpace) + all MSL sectors
- **Law:** Severe (3)
- **Alignment:** Lawful (100)
- **Created:** Idempotent (upserted on conflicts)

### Random Clusters

- **Count:** Default 9 (configurable)
- **Centers:** Randomly selected from unclaimed sectors > 10
- **Name:** "Cluster {center_sector_id}"
- **Role:** RANDOM
- **Law:** Neutral (1)
- **Alignment:** Random (-100 to +100)
- **Members:** Multi-hop (3 hops) flood-fill from center, excluding already-claimed sectors

### Coverage

- **Target:** ~50% of universe in random clusters
- **Distribution:** Deterministic per-cluster sizing (not random)
- **Free Space:** Remaining unclaimed sectors remain ungoverned (open to players)

### Flood-Fill Algorithm

Uses recursive CTE (BFS):

```
1. Add center sector to cluster
2. Explore neighbors (follow warps)
3. Add unclaimed neighbors to cluster
4. Recursively expand (limited to 3 hops, configurable)
5. Stop when:
   - All neighbors explored
   - Sector already claimed by another cluster
   - Recursion depth limit reached
```

---

## Server Startup Behavior

**Location:** `src/server_cron.c` → `populate_msl_if_empty()`

At server startup:
1. Query: `SELECT COUNT(*) FROM msl_sectors`
2. If count > 0: Log "MSL already populated" and skip
3. If count = 0: Generate MSL (for backward compatibility or manual re-initialization)

**Important:** In normal operation (after BigBang), MSL is pre-computed and server startup does not regenerate it.

---

## Database Schema

### msl_sectors

```sql
CREATE TABLE msl_sectors (
    sector_id SERIAL PRIMARY KEY REFERENCES sectors(sector_id)
);
```

**Purpose:** Tracks all Major Space Lane sectors. Allows fast queries for:
- Asset cleanup in MSL (mission: keep illegal goods OUT)
- Genesis restrictions (planets cannot be created in MSL)
- Combat rules (MSL sectors have special combat enforcement)

### cluster_sectors

```sql
CREATE TABLE cluster_sectors (
    cluster_id BIGINT NOT NULL,
    sector_id INTEGER NOT NULL,
    PRIMARY KEY (cluster_id, sector_id),
    FOREIGN KEY (cluster_id) REFERENCES clusters(clusters_id),
    FOREIGN KEY (sector_id) REFERENCES sectors(sector_id)
);
```

**Purpose:** Maps sectors to clusters. Allows:
- Cluster membership queries (which cluster owns this sector?)
- Region-based economy (commodity prices vary by cluster)
- Law enforcement (cluster alignment affects crime rules)
- Policing/bribery (cluster-level NPC enforcement)

---

## Configuration

### BigBang Parameters

In `bigbang.json`:

```json
{
  "sectors": 500,
  "density": 4,
  "port_ratio": 40,
  "planet_ratio": 30.0,
  "db": "twclone",
  "admin": "dbname=postgres",
  "app": "dbname=%DB%"
}
```

### Cluster Generation Tuning

Edit `src/bigbang_pg_main.c`:

```c
exec_sql (app, "SELECT generate_clusters_v2(9)", "generate_clusters_v2");
```

Change `9` to desired random cluster count.

### MSL/Cluster Parameters (In SQL)

Edit `sql/pg/040_functions.sql`:

- **generate_msl():**
  - Loop over all stardocks (hardcoded: all in stardock_location)
  - Loop over FedSpace 1-10 (hardcoded: 1..10)

- **generate_clusters_v2():**
  - `target_random_cluster_count` parameter
  - Flood-fill hops: Hardcoded to 3 (`WHERE ff.hops < 3`)
  - Coverage target: Hardcoded to 50% (`(v_max_sector * 50)`)

---

## Testing

### Determinism Verification

1. Run BigBang with same seed (requires seed implementation)
2. Verify msl_sectors is identical
3. Verify cluster_sectors is identical
4. Verify same cluster centers chosen

### Coverage Assertions

```sql
-- Fed cluster has sectors 1-10
SELECT COUNT(*) FROM cluster_sectors 
WHERE cluster_id = (SELECT clusters_id FROM clusters WHERE role='FED')
  AND sector_id BETWEEN 1 AND 10;
-- Expected: 10 (may be more if MSL includes 1-10)

-- Fed cluster has all MSL sectors
SELECT COUNT(*) FROM cluster_sectors cs
WHERE cluster_id = (SELECT clusters_id FROM clusters WHERE role='FED')
  AND EXISTS (SELECT 1 FROM msl_sectors WHERE sector_id = cs.sector_id);
-- Expected: COUNT(msl_sectors)

-- Random clusters have >1 sector (real regions)
SELECT COUNT(*) FROM clusters
WHERE role='RANDOM' AND (
  SELECT COUNT(*) FROM cluster_sectors 
  WHERE cluster_id = clusters.clusters_id
) > 1;
-- Expected: All random clusters

-- Free space exists
SELECT COUNT(*) FROM sectors
WHERE NOT EXISTS (
  SELECT 1 FROM cluster_sectors WHERE sector_id = sectors.sector_id
);
-- Expected: > 0
```

---

## Known Limitations

1. **Small Universes:** If the universe is too small (<100 sectors), random clusters may exhaust all available space quickly.
2. **Seed Determinism:** Current BigBang uses `srand(time(NULL))`, so cluster centers are NOT deterministic across runs. To enable determinism, add `--seed` option to BigBang.
3. **Recursion Depth:** SQL recursive CTEs have limits. If flood-fill hits the 50-hop limit before filling target, clusters will be smaller than intended.
4. **Orion/Ferengi:** Not yet implemented in v2. Placeholder in code for future expansion.

---

## Migration from v1 to v2

If you have an existing universe:

1. Run BigBang again on a fresh database (recommended)
2. OR manually run in existing DB:
   ```sql
   DELETE FROM msl_sectors;
   DELETE FROM cluster_sectors;
   SELECT generate_msl();
   SELECT generate_clusters_v2(9);
   ```

**Warning:** This will rebuild clusters and may change cluster membership.

---

## Future Work

- [ ] Add Orion/Ferengi cluster creation in v2
- [ ] Implement cluster naming from name pool (not just "Cluster {id}")
- [ ] Support custom cluster alignment/law_severity per role
- [ ] Performance optimization for large universes (10k+ sectors)
- [ ] Deterministic seeding in BigBang (--seed option)
