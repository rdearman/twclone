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

## 4. Planet Market Integration

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