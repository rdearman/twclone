# shiptype_restrictions Table

## Overview

The `shiptype_restrictions` table stores flexible eligibility requirements for ship types. It replaces hardcoded checks with data-driven metadata, allowing SysOps to add new ship types with arbitrary restrictions without modifying or recompiling code.

## Schema

### PostgreSQL

```sql
CREATE TABLE shiptype_restrictions (
    restriction_id serial PRIMARY KEY,
    shiptypes_id integer NOT NULL,
    check_type text NOT NULL CHECK (check_type IN (
        'CEO', 
        'ALIGNMENT_MIN', 
        'ALIGNMENT_MAX', 
        'SCORE_MIN', 
        'CUSTOM'
    )),
    check_value text,
    description text NOT NULL,
    enabled boolean NOT NULL DEFAULT TRUE,
    FOREIGN KEY (shiptypes_id) REFERENCES shiptypes (shiptypes_id) ON DELETE CASCADE,
    CONSTRAINT unique_restriction_per_type UNIQUE (shiptypes_id, check_type)
);
```

### MySQL

```sql
CREATE TABLE shiptype_restrictions (
    restriction_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    shiptypes_id bigint NOT NULL,
    check_type VARCHAR(20) NOT NULL CHECK (check_type IN (
        'CEO', 
        'ALIGNMENT_MIN', 
        'ALIGNMENT_MAX', 
        'SCORE_MIN', 
        'CUSTOM'
    )),
    check_value VARCHAR(255),
    description TEXT NOT NULL,
    enabled boolean NOT NULL DEFAULT TRUE,
    FOREIGN KEY (shiptypes_id) REFERENCES shiptypes (shiptypes_id) ON DELETE CASCADE,
    UNIQUE KEY unique_restriction_per_type (shiptypes_id, check_type)
);
```

## Column Descriptions

| Column | Type | Required | Description |
|--------|------|----------|-------------|
| `restriction_id` | serial/BIGINT | Yes | Primary key, auto-generated |
| `shiptypes_id` | integer | Yes | Foreign key to shiptypes table |
| `check_type` | text/VARCHAR(20) | Yes | Type of restriction (see below) |
| `check_value` | text/VARCHAR(255) | No | Value for the restriction (varies by type) |
| `description` | text/TEXT | Yes | Human-readable reason (shown to players/SysOps) |
| `enabled` | boolean | No | Toggle restriction on/off without deletion |

## Restriction Types

### CEO
Requires player to be a corporation CEO.

**check_value**: `'1'` (literal, indicates requirement is active)

**Example**:
```sql
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description)
SELECT shiptypes_id, 'CEO', '1', 'Must be a corporation CEO'
FROM shiptypes WHERE name = 'Corporate Flagship';
```

**Validation**: Server checks if `player->flags & PLAYER_FLAG_CEO`

---

### ALIGNMENT_MIN
Player alignment must be >= check_value.

**check_value**: Integer (e.g., `'5000'`)

**Example**:
```sql
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description)
SELECT shiptypes_id, 'ALIGNMENT_MIN', '5000', 'Requires good alignment >= 5000'
FROM shiptypes WHERE name = 'IIS';
```

**Validation**: Server checks if `player->alignment >= atoi(check_value)`

---

### ALIGNMENT_MAX
Player alignment must be <= check_value.

**check_value**: Integer (e.g., `'-5000'`)

**Example**:
```sql
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description)
SELECT shiptypes_id, 'ALIGNMENT_MAX', '-5000', 'Evil only'
FROM shiptypes WHERE name = 'Pirate Hunter';
```

**Validation**: Server checks if `player->alignment <= atoi(check_value)`

---

### SCORE_MIN
Player score must be >= check_value.

**check_value**: Integer (e.g., `'50000'`)

**Example**:
```sql
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description)
SELECT shiptypes_id, 'SCORE_MIN', '50000', 'Requires 50k+ points'
FROM shiptypes WHERE name = 'Elite Cruiser';
```

**Validation**: Server checks if `player->score >= atoi(check_value)`

---

### CUSTOM
Reserved for future restriction types (races, positions, tech levels, etc.).

**check_value**: Arbitrary string (interpretation depends on future implementation)

**Example** (future):
```sql
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description)
SELECT shiptypes_id, 'CUSTOM', 'race:human', 'Humans only'
FROM shiptypes WHERE name = 'Human-Only Fighter';
```

**Note**: CUSTOM type requires custom implementation in `repo_shiptypes_validate_eligibility()`

---

## Constraints

### Uniqueness
`UNIQUE (shiptypes_id, check_type)` ensures only one restriction of each type per ship type.

- ✅ Allowed: Ship A has CEO restriction AND Ship A has ALIGNMENT_MIN restriction
- ❌ Blocked: Ship A has two ALIGNMENT_MIN restrictions

Workaround: Combine multiple requirements into one description or extend check_value syntax.

### Foreign Key Cascading
ON DELETE CASCADE automatically removes restrictions when a ship type is deleted.

---

## Current Data (Seeded at Phase 4)

```sql
SELECT * FROM shiptype_restrictions;
```

| restriction_id | shiptypes_id | check_type | check_value | description | enabled |
|---|---|---|---|---|---|
| 1 | (Corporate Flagship ID) | CEO | 1 | Must be a corporation CEO to purchase Corporate Flagship | true |
| 2 | (IIS ID) | ALIGNMENT_MIN | 5000 | Must have alignment greater than 5000 to purchase IIS | true |

---

## Usage in Server Code

### Validation Function

```c
#include "db/repo/repo_shiptypes.h"

// Build player info struct
player_info_t player;
player.id = player_id;
player.alignment = player->alignment;
player.score = player->score;
player.flags = player->flags;

// Validate eligibility
char reason[256];
if (!repo_shiptypes_validate_eligibility(db_conn, shiptype_id, &player, reason, sizeof(reason))) {
    // Player is NOT eligible
    send_error(player_id, ERR_PLAYER_INELIGIBLE, reason);
    return;
}

// Player IS eligible - proceed with purchase/upgrade
```

### Get Failure Reason

```c
char reason[256];
repo_shiptypes_get_restriction_desc(db_conn, shiptype_id, &player, reason, sizeof(reason));
// reason now contains human-readable explanation (e.g., "Must be a corporation CEO")
```

---

## Common Operations

### Add a Restriction to Existing Ship Type

```sql
-- Allow only CEOs to buy the new "Exec Shuttle"
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description)
SELECT shiptypes_id, 'CEO', '1', 'Executive-level ship; CEOs only'
FROM shiptypes WHERE name = 'Exec Shuttle';
```

### Remove a Restriction (Keep History)

Use `enabled = FALSE` to temporarily disable without deleting:

```sql
UPDATE shiptype_restrictions 
SET enabled = FALSE
WHERE shiptypes_id = (SELECT shiptypes_id FROM shiptypes WHERE name = 'Corporate Flagship')
  AND check_type = 'CEO';
```

### Permanently Delete a Restriction

```sql
DELETE FROM shiptype_restrictions
WHERE shiptypes_id = (SELECT shiptypes_id FROM shiptypes WHERE name = 'Some Ship')
  AND check_type = 'ALIGNMENT_MIN';
```

### Change a Score Requirement

```sql
UPDATE shiptype_restrictions
SET check_value = '100000'
WHERE shiptypes_id = (SELECT shiptypes_id FROM shiptypes WHERE name = 'Elite Cruiser')
  AND check_type = 'SCORE_MIN';
```

### View All Restrictions for a Ship Type

```sql
SELECT * FROM shiptype_restrictions
WHERE shiptypes_id = (SELECT shiptypes_id FROM shiptypes WHERE name = 'Corporate Flagship');
```

### List All Restricted Ships

```sql
SELECT DISTINCT s.name, COUNT(*) as restriction_count
FROM shiptypes s
JOIN shiptype_restrictions r ON s.shiptypes_id = r.shiptypes_id
WHERE r.enabled = TRUE
GROUP BY s.shiptypes_id, s.name
ORDER BY s.name;
```

---

## Design Rationale

### Why Not Hardcoded Checks?

**Before Phase 4** (Old Design):
```c
// server_stardock.c
if (strcasecmp(ship_name, "Corporate Flagship") == 0) {
    if (!(player->flags & PLAYER_FLAG_CEO)) {
        return ERR_PLAYER_INELIGIBLE;
    }
}
if (strcasecmp(ship_name, "IIS") == 0) {
    if (player->alignment < 5000) {
        return ERR_PLAYER_INELIGIBLE;
    }
}
// Adding new restrictions = code change + recompile
```

**Problems**:
- ❌ Adding a ship type with restrictions requires code modification
- ❌ Recompilation and deployment required
- ❌ No way for SysOps to add restrictions without developer involvement
- ❌ Maintenance burden for multiple restrictions per ship

### Why Metadata Table?

**After Phase 4** (New Design):
```sql
-- SysOp adds restriction via SQL, no code change needed
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description)
VALUES ((SELECT shiptypes_id FROM shiptypes WHERE name = 'New Elite Ship'),
        'SCORE_MIN', '75000', 'Requires 75k+ score');
-- Takes effect immediately
```

**Benefits**:
- ✅ SysOps control restrictions without code changes
- ✅ New restriction types can be added in DB (enum expanded)
- ✅ No recompilation needed
- ✅ Auditable history (enabled/disabled dates)
- ✅ Extensible to arbitrary restriction types
- ✅ Human-readable descriptions for player messaging

---

## Future Extensions (CUSTOM Type)

The CUSTOM restriction type is reserved for future use:

```sql
-- Example: Race-specific ship (future)
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description)
VALUES (..., 'CUSTOM', 'species=Human', 'Humans only');

-- Example: Tech-level gate (future)
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description)
VALUES (..., 'CUSTOM', 'tech>=5', 'Requires technology 5+');

-- Example: Sector-based restriction (future)
INSERT INTO shiptype_restrictions (shiptypes_id, check_type, check_value, description)
VALUES (..., 'CUSTOM', 'sector<>0', 'Not available in FedSpace');
```

Implementation would extend `repo_shiptypes_validate_eligibility()` to parse and evaluate check_value for CUSTOM restrictions.

---

## Migration & Compatibility

**Phase 4 Seeding**: Initial restrictions for existing ships are seeded via migration scripts:
- `sql/pg/101_phase4_shiptypes_restrictions.sql` (PostgreSQL)
- `sql/my/101_phase4_shiptypes_restrictions.sql` (MySQL)

**Backward Compatibility**: If table is empty or a ship type has no restrictions, all players are eligible (no restrictions = no barriers).

---

## Error Handling

When a player does not meet a restriction:

```
ERR_PLAYER_INELIGIBLE: "<reason from description field>"
```

Example responses:
- `"Must be a corporation CEO to purchase Corporate Flagship"`
- `"Must have alignment greater than 5000 to purchase IIS"`
- `"Requires 50k+ score"`

---

## Testing

### Test Scenario: CEO-Only Ship

```bash
# Setup
INSERT INTO shiptype_restrictions 
  (shiptypes_id, check_type, check_value, description)
VALUES (X, 'CEO', '1', 'CEO test restriction');

# Test 1: CEO can buy
buy_ship(player=ceo, ship_type=X) → SUCCESS

# Test 2: Non-CEO cannot buy
buy_ship(player=regular, ship_type=X) → ERR_PLAYER_INELIGIBLE

# Cleanup
DELETE FROM shiptype_restrictions WHERE shiptypes_id = X AND check_type = 'CEO';
```

### Test Scenario: Alignment Gate

```bash
# Setup
INSERT INTO shiptype_restrictions 
  (shiptypes_id, check_type, check_value, description)
VALUES (Y, 'ALIGNMENT_MIN', '5000', 'Alignment test');

# Test 1: High alignment can buy
buy_ship(player=align:6000, ship_type=Y) → SUCCESS

# Test 2: Low alignment cannot buy
buy_ship(player=align:4000, ship_type=Y) → ERR_PLAYER_INELIGIBLE

# Cleanup
DELETE FROM shiptype_restrictions WHERE shiptypes_id = Y AND check_type = 'ALIGNMENT_MIN';
```

---

## See Also

- `src/db/repo/repo_shiptypes.h` - Public API
- `src/db/repo/repo_shiptypes.c` - Implementation details
- `src/server_stardock.c` - Integration points (shipyard.list, shipyard.upgrade)
- `src/errors.h` - ERR_PLAYER_INELIGIBLE and other error codes

