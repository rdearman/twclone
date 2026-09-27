# 24. Planets, Outposts, & Stations

## 1. Planet Interaction

### `planet.land`
Land on a planet (if within range).
**Args**: `{ "planet_id": 123 }`
**Response**: `{ "status": "landed", "planet_id": 123 }`

### `planet.launch`
Launch from a planet.
**Response**: `{ "status": "launched", "in_space": true }`

### `planet.info`
Get information about a planet (visible from space or while landed).
**Args**: `{ "planet_id": 123 }`
**Response**:
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
  "owner_type": "player"
}
```

### `planet.view` [NYI]
View detailed planet information after landing (includes colonists, production, citadel status, treasury).
**Availability**: After successful `planet.land`
**Response**:
```json
{
  "planet_id": 123,
  "name": "Terra II",
  "class": "M",
  "colonists": 5000,
  "morale": 85,
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
Place a buy order for a commodity on behalf of a player-owned planet. The player must have sufficient credits to cover the maximum potential cost.

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
    "max_price": 150
  }
}
```
