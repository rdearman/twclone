# 24. Planets, Outposts, & Stations

## 1. Planet Interaction

### `planet.deposit` / `planet.withdraw`
Transfer cargo/colonists between ship and planet.
**Events**: Emits `player.planet_transfer.v1`.

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
Sell surplus commodities from a player-owned planet to the global market. The planet must have sufficient stock. Proceeds are credited to the player's account.

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