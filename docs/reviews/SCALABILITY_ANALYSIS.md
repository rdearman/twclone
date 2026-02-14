# Commodity System Scalability Analysis

## Executive Summary
The system has **MIXED scalability**. While adding new commodities to the database is easy, the architecture has HARD constraints that would require code changes and schema migrations to support them.

---

## 1. SHIPS - HARDCODED COLUMNS ❌

### Current State
Ships table has individual columns for each commodity:
```sql
colonists    bigint
equipment    bigint
organics     bigint
ore          bigint
slaves       bigint
weapons      bigint
drugs        bigint
```

### Constraints Blocking New Commodities
1. **Cargo CHECK Constraint** (hard requirement):
   ```sql
   CHECK ((colonists + ore + organics + equipment + slaves + weapons + drugs) <= holds)
   ```
   - Explicitly sums all 7 commodities
   - Would FAIL if any column referenced doesn't exist
   - Must be rewritten for each new commodity

2. **Server Code Hardcoding** (Critical):
   - `src/server_ships.c` - h_update_ship_cargo() has hardcoded IF/ELSE chain (lines 910-947)
     ```c
     if (strcasecmp(commodity_code, "ORE") == 0) col_name = "ore";
     else if (strcasecmp(commodity_code, "ORG") == 0) col_name = "organics";
     // ... etc for each commodity
     ```
   - Each new commodity requires a new else-if branch
   - Must rebuild server binary

3. **Cargo Enumeration**:
   - Every query that pulls ship cargo must list all columns explicitly
   - Example: `SELECT ore, organics, equipment, colonists, slaves, weapons, drugs FROM ships`
   - Would need UPDATE for each new commodity

### What Adding 2 New Commodities Would Require:

**Database Changes:**
- [ ] Add 2 new columns to `ships` table: `water BIGINT`, `spice BIGINT`
- [ ] Update cargo CHECK constraint to include new columns in sum
- [ ] Schema migration for all existing ships (values = 0)
- [ ] Total: ~15 lines of SQL

**Server Code Changes:**
- [ ] Add 2 new if-branches in `h_update_ship_cargo()` 
- [ ] Search/replace all queries selecting ship cargo
- [ ] Recompile server
- [ ] Total: ~20 lines of C code, 1 rebuild

**Impact**: **MODERATE - requires code + schema changes**

---

## 2. PORT TRADING - CHECK CONSTRAINT BLOCKER ❌

### Current State
```sql
CHECK (commodity = ANY (ARRAY['ORE'::text, 'ORG'::text, 'EQU'::text, 'SLV'::text, 'WPN'::text, 'DRG'::text]))
```

### Problem
- Whitelist explicitly names 6 allowed commodities
- Any new commodity insertion FAILS with constraint violation
- Constraint is defined in both schema files (PostgreSQL + MySQL)

### What Adding 2 New Commodities Would Require:

**Database Changes:**
- [ ] Update `port_trade_commodity_check` constraint in live DB
- [ ] Update `sql/pg/000_tables.sql` with new commodity list
- [ ] Update `sql/my/000_tables.sql` with new commodity list
- [ ] Can then INSERT port_trade entries for new commodities
- [ ] Total: ~5 lines per file

**Server Code Changes:**
- [ ] None required - no hardcoding in port trading logic
- [ ] Server accepts any commodity code from `commodities` table
- [ ] No recompile needed

**Impact**: **MINIMAL - database only**

---

## 3. PLANET MARKETS - FLEXIBLE ✅

### Current Implementation
- Uses `h_planet_check_trade_legality()` which calls `h_is_illegal_commodity()`
- `h_is_illegal_commodity()` queries `commodities.illegal` column
- No hardcoded commodity lists in market logic

### Constraints
- `planet_goods` table has CHECK constraint: `commodity IN ('ore', 'organics', 'equipment', 'food', 'fuel')`
- Restricts to 5 commodities (legal only, doesn't include SLV/WPN/DRG)
- Would need update

### What Adding 2 New Commodities Would Require:

**Database Changes:**
- [ ] Update `planet_goods_commodity_check` constraint
- [ ] Update `sql/pg/000_tables.sql` definition
- [ ] Update `sql/my/000_tables.sql` definition
- [ ] No server code changes needed
- [ ] Total: ~5 lines per file

**Server Code Changes:**
- [ ] None - market logic is fully dynamic
- [ ] Checks `commodities.illegal` column at runtime
- [ ] Supports any commodity automatically

**Impact**: **MINIMAL - database only**

---

## 4. COMMODITIES TABLE - FULLY FLEXIBLE ✅

### Current State
```sql
CREATE TABLE commodities (
    commodities_id serial PRIMARY KEY,
    code text UNIQUE NOT NULL,
    name text NOT NULL,
    illegal boolean NOT NULL DEFAULT FALSE,
    base_price integer NOT NULL DEFAULT 0,
    volatility integer NOT NULL DEFAULT 0
)
```

### Findings
- No CHECK constraints
- Accepts any code/name combination
- Can add unlimited new commodities
- Foreign key references from `port_trade` and `entity_stock` (flexible)

### What Adding 2 New Commodities Would Require:
- [ ] Just INSERT 2 rows into `commodities` table
- [ ] No schema changes
- [ ] No code changes
- [ ] Immediate effect

**Impact**: **TRIVIAL - data only**

---

## 5. ENTITY_STOCK TABLE - FLEXIBLE ✅

### Current State
- Stores port/planet stock by commodity code
- Uses `commodity_code` text column (no constraint)
- Foreign key to `commodities.code` (dynamic)

### Problem
- `entity_type` CHECK constraint: `IN ('port', 'planet')`
- Restricts to ports/planets only (not ships)

### For New Commodities
- Can store stock for new commodities immediately after adding to `commodities` table
- No schema changes needed

**Impact**: **NONE - fully dynamic**

---

## SUMMARY TABLE

| Component | Current Constraint | Schema Change | Code Change | Difficulty |
|-----------|-------------------|----------------|------------|------------|
| Ships Cargo | Hardcoded columns | **YES** | **YES** | HIGH |
| Ships CHECK Constraint | Explicit sum | **YES** | - | HIGH |
| Port Trading | CHECK whitelist | **YES** | No | LOW |
| Planet Markets | CHECK whitelist | **YES** | No | LOW |
| Commodities Table | None | No | No | TRIVIAL |
| Entity Stock | None | No | No | TRIVIAL |

---

## ADDING 2 COMMODITIES: Water (Legal) + Spice (Illegal)

### Step-by-Step Requirements:

#### Phase 1: Database Schema (MANDATORY)
1. Add columns to ships table:
   - `ALTER TABLE ships ADD COLUMN water BIGINT DEFAULT 0;`
   - `ALTER TABLE ships ADD COLUMN spice BIGINT DEFAULT 0;`

2. Update ships cargo CHECK constraint:
   - Drop old constraint
   - Add new constraint with 9-commodity sum (including water, spice)

3. Update port_trade CHECK constraint:
   - Drop old constraint  
   - Add new constraint: `IN ('ORE', 'ORG', 'EQU', 'SLV', 'WPN', 'DRG', 'WTR', 'SPC')`
   
4. Update planet_goods CHECK constraint (if allowing water on planets):
   - Add 'water' to allowed list

5. Update schema files:
   - `sql/pg/000_tables.sql` (3 constraints)
   - `sql/my/000_tables.sql` (3 constraints)

#### Phase 2: Server Code (IF ships carry new commodities)
1. Update `src/server_ships.c` h_update_ship_cargo():
   - Add 2 new else-if branches for WTR and SPC
   - Lines: ~10 additions

2. Update database queries in `src/db/repo/repo_ships.c`:
   - Update SELECT statements to include water, spice
   - Multiple locations

3. Rebuild server:
   - `make clean && make -j4`

#### Phase 3: Port Configuration (if trading new commodities)
1. Add port_trade entries for new commodities:
   - INSERT into port_trade for desired ports
   - Can do via SQL script, no code changes

2. Update 040_functions.sql (if new ports should trade them):
   - Add new commodities to port type definitions
   - Configure which port types buy/sell each commodity

#### Phase 4: Data Entry
1. Add to commodities table:
   ```sql
   INSERT INTO commodities (code, name, illegal, base_price, volatility)
   VALUES ('WTR', 'Water', false, 80, 15),
          ('SPC', 'Spice', true, 1200, 80);
   ```

2. Update entity_stock if needed:
   - Ports/planets seeding new commodities (automatic from 040_functions.sql)

---

## BOTTLENECKS FOR SCALABILITY

### 🔴 CRITICAL BLOCKERS
1. **Ships table columns** - Schema is denormalized for cargo storage
   - Every new commodity requires schema migration
   - Affects all 10,000+ ships
   - Requires server code changes
   - Can't dynamically add commodities

2. **CHECK constraints** - Explicit whitelists
   - port_trade, planet_goods, ships CHECK constraints hard-code commodity lists
   - Prevents new commodities from being used
   - Forces schema migrations (not dynamic data addition)

3. **Server hardcoding** - h_update_ship_cargo()
   - IF/ELSE chain for cargo updates
   - Requires recompilation for new commodities
   - Not dynamically extensible

### 🟡 MODERATE ISSUES
1. **Database function calls** - Many queries explicitly list commodities
   - Found in ~30+ locations across C code
   - Each new commodity might require query updates

2. **Port generation** - 040_functions.sql hard-codes port trade patterns
   - If new commodity should be traded, need to update port type definitions
   - Not immediately automatic

### 🟢 FLEXIBLE AREAS
1. **Commodities table** - Accepts unlimited entries
2. **Entity stock** - Dynamic commodity tracking for ports/planets
3. **Market legality** - Checks `commodities.illegal` column at runtime
4. **Protocol** - Can send any commodity code if database allows it

---

## ARCHITECTURAL RECOMMENDATIONS

### Short-term (for 1-2 new commodities):
- Accept the schema changes as one-time cost
- Update ships table, CHECK constraints, 040_functions
- Update server code in h_update_ship_cargo()
- Recompile once

### Medium-term (for 3-5+ new commodities):
- **Refactor ships cargo to use EAV (entity-attribute-value) model**
  - Move to ship_cargo table: `(ship_id, commodity_code, quantity)`
  - Eliminates hardcoded columns
  - Eliminates server code changes
  - Fully dynamic

- **Refactor port trading**
  - Use commodity code directly (already works)
  - Remove explicit CHECK constraints, use FK to commodities
  - Allows unlimited commodities

### Long-term (fully extensible):
- Use commodity code everywhere (currently in-progress)
- Store all cargo in generic tables with code references
- Eliminate hardcoded commodity lists
- Add multi-attribute systems (grade, quality, origin, etc.)

---

## VERDICT

**Without code changes: Can add to commodities table, but can't use them for:**
- Ship cargo (hardcoded columns)
- Port trading (CHECK constraint)  
- Planet goods (CHECK constraint)

**With code changes: Moderate effort**
- ~50 lines of SQL (schema + constraints)
- ~20 lines of C (server cargo handling)
- 1-2 hours total for 2 new commodities

**The system was NOT designed for extensibility, but is not impossible to extend.**

The "easy" parts (commodities table, entity_stock, market legality) show good architecture, but they're undermined by the hardcoded ships table design and CHECK constraints.

