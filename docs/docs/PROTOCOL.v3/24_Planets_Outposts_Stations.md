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
