# 24. Planets, Outposts, & Stations

Canonical product decisions: [`../V3_GAMEPLAY_DECISIONS.md`](../V3_GAMEPLAY_DECISIONS.md); Exchange accounting: [`../V3_GALACTIC_EXCHANGE_CONTRACT.md`](../V3_GALACTIC_EXCHANGE_CONTRACT.md).

## 1. Planet Interaction

### `planet.land`
Land on a planet (if within range).
**Args**: `{ "planet_id": 123 }`
**Response**: `{ "status": "landed", "planet_id": 123 }`

The planet owner and authorized members of its owning corporation land without
hostile-fire checks. An unauthorized attempt resolves active atmosphere Quasar
fire and the existing planetary fighter/citadel combat rules before the access
check is returned. Quasar damage consumes ship shields before hull, persists in
the same transaction as `combat.hit.quasar`, and destroys the ship if hull
reaches zero. Planetary fighters use the existing combat.attack_planet strength
comparison and citadel gates; both fighter pools, citadel shields, and a
`combat.planet_landing_defense.v1` event are persisted transactionally. The
atmosphere cannon applies the existing military reaction multiplier used by
sector Quasars. The
unauthorized player does not gain access or land after an engagement. The error
response includes `error.data.planet_id`, `error.data.landing_allowed`,
`error.data.ship_remains_in_sector`, and `error.data.combat[]`. A surviving
attacker remains in the sector; a destroyed ship has no active assignment.
Unguarded planets retain the normal permission response, and an unowned planet
retains its existing landing behavior. Terra (planet 1 in sector 1) is public
Federation territory: ordinary and neutral players may land there to collect
colonists, while evil-aligned players are refused by FedLaw. Landing on Terra
never changes its owner and does not make it claimable.
For Terra, `planet.colonists.set` accepts only `action: "pickup"`, and the
ship must be landed on Terra. Drop-off, stock transfer, fighter transfer,
worker assignment, citadel changes, renaming, market operations, and ownership
operations are refused server-side. `planet.info` and
`planet.colonists.get` remain read-only disclosures; launching remains available
so a ship can leave after collecting colonists.

### `planet.launch`
Launch from a planet.
**Response**: `{ "status": "launched", "in_space": true }`

### `planet.info`
Get detailed information about a planet after landing or when authorized as its
player/corporation owner. Terra (planet 1 in sector 1) is a public exception and
may be inspected by any authenticated player. Remote and unowned unlanded
planet details are otherwise refused.
**Args**: `{ "planet_id": 123 }`
**Response** (existing planet fields are retained):
```json
{
  "planet_id": 123,
  "name": "Terra II",
  "sector_id": 10,
  "class": "M",
  "goods": [
    {"commodity": "ORE", "quantity": 500, "max_capacity": 5000},
    {"commodity": "ORG", "quantity": 300, "max_capacity": 4000}
  ],
  "owner_id": 42,
  "owner_type": "player",
  "fighters": 50,
  "citadel": {
    "level": 2,
    "construction_status": "upgrading",
    "target_level": 3,
    "construction_start_time": 1793214000,
    "expected_completion_time": 1793217600,
    "planetary_shields": 500,
    "military_reaction_level": 1,
    "qCannonSector": 50,
    "qCannonAtmosphere": 40,
    "treasury_balance": 25000
  }
}
```

`fighters` is the planet's stored fighter count. `citadel.level`,
`construction_status`, `planetary_shields`, and `military_reaction_level` are
included when a citadel row exists and its defensive state can be read. The
upgrade target and Unix epoch start/completion timestamps are included when
the construction-state repository query is available. When a citadel row is
present, `qCannonSector` and `qCannonAtmosphere` expose its Quantum Cannon
settings; NULL values are reported as `0`. An absent citadel row is distinct
from a failed settings query.
`treasury_balance` is included only for the planet owner or a member of its
owning corporation; landing on the planet alone does not grant treasury access.
Terra's public-information exception does not grant treasury or ownership
authority.

Errors: `401` when unauthenticated, `400` for a missing or invalid
`planet_id`, `403` (`ERR_NOT_FOUND`) when the planet does not exist, `402`
(`ERR_PERMISSION_DENIED`) when the caller is neither landed nor an authorized
owner/member, and a database error when authorized treasury data cannot be
read.

### `planet.fighters.transfer`

Transfer fighters between the caller's active ship and a planet they control.
The ship must be landed on the specified planet. `direction` is
`ship_to_planet` or `planet_to_ship`; `quantity` must be a positive integer.

```json
{"command":"planet.fighters.transfer","data":{"planet_id":123,"direction":"ship_to_planet","quantity":25}}
```

Success response type: `planet.fighters.transferred_v1`.

```json
{"planet_id":123,"direction":"ship_to_planet","quantity":25,"planet_fighters":75,"ship_fighters":25}
```

The server rejects unauthenticated callers (`401`), invalid arguments (`400`),
unknown planets (`403` / `ERR_NOT_FOUND`), callers who do not own/control the
planet (`402` / `ERR_PERMISSION_DENIED`), and ships not landed on the planet
(`402`). The transactional repository must
recheck those conditions, available fighter quantity, and ship fighter
capacity while locking both authoritative rows. Any rejected transfer must
leave both fighter counts unchanged. A repository or transaction failure
returns an error; the server never synthesizes a successful transfer.

#### Repository integration contract

The server uses these existing citadel/planet functions:

```c
int repo_citadel_get_construction_state(
    db_t *db, int32_t planet_id, int32_t *level_out,
    char *status_out, size_t status_out_size, int32_t *target_level_out,
    int64_t *started_at_epoch_out, int64_t *completion_epoch_out);

int db_planets_get_citadel_defenses_ex(
    db_t *db, int planet_id, bool *has_active_defenses_out,
    int *level_out, int *shields_out, int *reaction_out);

int db_planets_consume_citadel_shields(
    db_t *db, int planet_id, int expected_shields,
    int amount, int *remaining_out);

int repo_planets_transfer_fighters(
    db_t *db, int player_id, int ship_id, int planet_id,
    const char *direction, int quantity,
    int *planet_fighters_after_out, int *ship_fighters_after_out);

/* Legacy atmospheric defense lookup; its no-cannon result currently shares
 * the failure result, so a distinct result is needed for level-3+ citadels
 * with no atmosphere cannon configured. */
int db_combat_get_planet_atmosphere_quasar(
    db_t *db, int planet_id, int *owner_id, char *owner_type_out,
    int *base_strength_out, int *reaction_out);
```

The construction read returns `0` on a row, `ERR_DB_NOT_FOUND` when there is
no citadel row, and a database error for failed reads. The defense read
distinguishes a failed read from an empty defense row. Shield consumption uses
a compare-and-decrement update and returns `ERR_DB_BUSY` when its snapshot is
stale. The fighter-transfer function is atomic, verifies player/ship/planet
ownership and landing, rejects insufficient source fighters or ship capacity,
and returns authoritative post-transfer quantities only after commit. It does
not charge turns.

### `planet.view`
Read-only planetary overview (`planet.view_v1`). `planet.info` remains the
detailed management response. The overview follows existing planet visibility
and landing access rules and is built from an explicit public-field allowlist;
it does not expose owner IDs or management/scheduling internals. Treasury is
included only for the owner or an authorized corporation member. Public
overview and authorized financial fields are distinct in the response schema.
**Availability**: After successful `planet.land`
**Response**:
```json
{
  "planet_id": 123,
  "name": "Terra II",
  "class": "M",
  "colonists": 5000,
  "population_type": "COLONIST",
  "production": {
    "ore": 100,
    "equipment": 50,
    "fighters": 10
  },
  "storage": {
    "ore": 5000,
    "equipment": 2000,
    "fighters": 50
  },
  "citadel": {
    "level": 3,
    "status": "operational",
    "defenses": {
      "qCannonSector": 50,
      "qCannonAtmosphere": 40,
      "militaryReactionLevel": 1
    }
  },
  "treasury": 100000
}
```

### Approved population and labour policy

A planet has exactly one population type: `COLONIST`, `SLAVE`, or
`UNPOPULATED`; mixed colonist/slave populations are prohibited. Existing
population counts and worker allocations are preserved. Slave labour produces
exactly half the otherwise-computed output, rounded down with integer
arithmetic after the existing authoritative production calculation. There is
no additional permanent morale multiplier. During a persisted unrest
stoppage, slave production output is zero; colonist production continues under
existing rules.

Slave morale is permanently low and cannot be improved by treatment or
management. Unrest is deterministic, may stop production and reduce slave
population, and cannot change ownership. It must be driven by persisted engine
state, never random rolls or random loss quantities. Unrest interval, stoppage
duration and population-loss parameters are not assigned defaults here; no
unrest event may be activated until SysOp policy values and deterministic
allocation of losses across assigned/unassigned population are configured.

Evil-aligned players may purchase slaves or enslave colonists during a
successful planetary conquest. Neutral players may inherit slaves through
capture but may not purchase or enslave additional slaves. Good-aligned
players automatically liberate all slaves when capturing a planet. Evil or
neutral capture may preserve existing slaves. An authorized owner or
corporation manager may explicitly emancipate the entire slave population;
partial emancipation is unsupported and the conversion preserves counts and
production allocations. Emancipation is atomic and costs no turns or credits.

Slave purchases use the existing illegal-commodity/black-market path only
where a port is configured to sell them and remain subject to normal access,
alignment, stock, credit and cargo-capacity checks. Colonists cannot be added
to a slave planet, nor slaves to a colonist planet, without a complete
conversion/removal operation. Slave acquisition must use a safe, authoritative
transport path; it must not create population directly on a remote planet.

`planet.harvest` transfers accumulated ore, organics and equipment production
to the player's currently landed ship, without generating resources or
consuming turns. Planet stock decreases by exactly what the ship receives;
ship capacity is authoritative and the complete transfer is atomic. Harvest
authorization remains subject to existing ownership/corporation permissions;
do not grant unrestricted withdrawal. If the existing withdrawal permission
path does not resolve an actor, that permission question remains a blocker.
When capacity is insufficient, transfer only what fits and leave the remainder
in planetary inventory.

### `planet.deposit` / `planet.withdraw`
Transfer cargo/colonists between ship and planet.
**Events**: Emits `player.planet_transfer.v1`.

Planet commodity balances are read and written through `entity_stock`. A commodity is transferable only when the planet has a matching `planet_goods` row; that row's `max_capacity` is the storage limit. `planet.info.goods` reports the server-confirmed quantity and configured capacity. Colonists remain a population field and are not stored as a commodity balance.

### `planet.colonists.get` / `planet.colonists.set`
Read the number of unassigned planet colonists and the ship's available holds, then move colonists between the landed player's ship and the planet. A drop-off adds colonists to the unassigned pool; it does not automatically start production.

Universe setup seeds Terra (planet 1) in sector 1 with unassigned colonists. There is no separate colonist purchase command; to collect the starting population, land on Terra and use the pickup transfer. The daily `terra_replenish` task restores Terra's commodity stock, not its colonist population.

### `planet.colonists.allocate`
Set the target number of colonists working in each production job on the planet where the player's active ship is landed. `population` is the total colony population across unassigned colonists and worker pools. A drop-off increases population; assigning workers moves colonists between pools without changing it.

```json
{
  "planet_id": 123,
  "ore": 100,
  "organics": 50,
  "equipment": 25
}
```

Workers above planet-class capacity or beyond the available colony population are rejected. The difference from the previous assignments is returned to or taken from the unassigned pool. Every 10-minute planet growth tick grows a non-empty colony toward its class population cap and adds new colonists to the unassigned pool; the same tick applies class production multipliers and updates stored goods. The client should show assignments and estimated output separately from confirmed stock.

### `planet.genesis_create`
Create a new planet using a Genesis Torpedo. Requires a Genesis Torpedo on the player's ship.
**Args**:
```json
{
  "sector_id": 123,
  "name": "New Earth",
  "owner_entity_type": "player"
}
```
**Response**: `planet.genesis_created_v1` with planet details.

### `planet.resource_growth.v1` (Engine)
Engine command handling production and growth.

## 2. Scanning & Info

Planets appear in `sector.info` and `sector.scan` results.

## 3. Mines & Defenses

### `sector.cleanse_mines_fighters.v1` (Engine)
Logic for clearing defenses.

### `player.mine.v1` (Event)
Event when a player lays mines (or collects).

## 5. Citadels

### `citadel.build` / `citadel.upgrade`
Start or upgrade a citadel on a landed planet.
**Lifecycle**: 
- Initiation: Reserves resources and sets `construction_status` to "upgrading".
- Construction: Progress is tracked via `construction_status`, `construction_start_time` and `construction_end_time` (Unix epoch).
- Completion: A background cron process finalizes the upgrade once the current time exceeds `construction_end_time`. At this point, the citadel level is incremented and the status returns to "idle".

**View Status**: The current level and construction status are visible in `planet.view`.

## 6. Planet Market Integration

### `planet.market.sell`
Sell surplus commodities from a player-owned planet to the global market. The planet must have sufficient inventory. Proceeds are credited to the player's account.

**Request:**
```json
{
  "command": "planet.market.sell",
  "data": {
    "planet_id": 123,
    "commodity": "ORE",
    "quantity": 1000
  }
}
```

**Response:**
```json
{
  "type": "planet.market.sell",
  "data": {
    "planet_id": 123,
    "commodity": "ORE",
    "quantity_sold": 1000,
    "total_credits_received": 50000
  }
}
```

### `planet.market.buy_order`
Place an unreserved, best-effort buy order for a commodity on behalf of a player-owned planet. Placement does not debit or reserve credits, nor reserve planet storage. The player need not have enough current credits or free storage for the entire order when placing it. The commodity and order parameters must be valid, and `quantity_total` must not exceed the configured total storage capacity for that commodity on the planet.

**Request:**
```json
{
  "command": "planet.market.buy_order",
  "data": {
    "planet_id": 123,
    "commodity": "EQU",
    "quantity_total": 500,
    "max_price": 150
  }
}
```

**Response:**
```json
{
  "type": "planet.market.buy_order",
  "data": {
    "order_id": 4567,
    "planet_id": 123,
    "commodity": "EQU",
    "quantity_total": 500,
    "filled_quantity": 0,
    "remaining_quantity": 500,
    "max_price": 150,
    "status": "open",
    "expires_at": 1794230400
  }
}
```

`max_price` is the buyer's per-unit limit. The market matcher may fill the
order only at an eligible execution price no greater than this limit. Open
orders do not escrow funds or reserve capacity. Each match must revalidate the
buyer's then-current available credits and the destination planet's then-current
free capacity inside the settlement transaction, along with seller stock and
both orders' remaining quantities. The fill quantity is bounded by every one
of those limits; a smaller executable quantity is a valid partial fill.

After a partial fill, the order remains open with updated `filled_quantity`
and `remaining_quantity` while it remains executable and has not expired. If a
matching attempt cannot fill even one unit because the buyer cannot afford one
unit at the execution price or the planet has no free capacity, the matcher
cancels the unfilled remainder and records the cancellation reason. It must not
leave an order open that cannot execute under the resources available at that
match. All inventory, credit, order-fill and cancellation changes are
transactional; a failed settlement must not report or persist a partial state.
Cancellation reasons are `insufficient_credits`, `no_capacity`,
`player_requested`, and `expired`.

The order expiry is configured by the integer server setting
`planet_market.buy_order_expiry_seconds`. If unset or invalid, the default is
`86400` seconds (24 hours); valid configured values are `60` through `604800`
seconds (7 days). `expires_at` is the UTC Unix timestamp calculated when the
order is created. Matching must ignore expired orders, and the market
maintenance process marks their unfilled remainder `expired` without moving
credits or inventory. Expiry is evaluated from the persisted timestamp, so it
remains correct across server restarts.

### `planet.market.buy_order.cancel`
Cancel an open planet buy order explicitly. Only the player who placed the
order may cancel it, and the player must still be authorized to control the
destination planet. Cancellation affects only the unfilled remainder; already
settled fills are not reversed. No funds or capacity are released because the
order reserves neither.

**Request:**
```json
{
  "command": "planet.market.buy_order.cancel",
  "data": { "order_id": 4567 }
}
```

**Response:**
```json
{
  "type": "planet.market.buy_order.cancel",
  "data": {
    "order_id": 4567,
    "status": "cancelled",
    "filled_quantity": 125,
    "remaining_quantity": 375,
    "cancellation_reason": "player_requested"
  }
}
```

Only an `open` order can transition to `cancelled` through this command. A
filled, expired, or already-cancelled order is not changed and returns the
standard protocol error for an invalid state transition. Missing orders,
unauthorized players, and players who no longer control the planet return the
standard not-found or permission error without changing order state. The
cancel transition and its audit state must be persisted atomically.
