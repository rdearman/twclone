# 23. Combat & Weapons

## 1. Combat Commands

### `combat.attack.ship`
Attack another ship.
**Args**: `{ "target_ship_id": 123, "weapon": "laser_mk1" }`

### `combat.attack.planet`
Attack a planet.
**Events**: Emits `player.planet_attack.v1`.

### `combat.attack.port`
Attack a port.
**Events**: Emits `player.port_strike.v1`.

## 2. Events

### `combat.hit` (v1)
Broadcast to relevant parties when damage is dealt.
```json
{
  "type": "combat.hit",
  "data": {
    "v": 1,
    "attacker_id": 12,
    "defender_id": 99,
    "weapon": "laser_mk2",
    "damage": 42,
    "defender_hp_after": 58,
    "sector_id": 278
  }
}
```

### `combat.ship_damage.v1`
Engine event for ship damage.

## 3. Enums

*   **Weapons**: `laser_mk1`, `laser_mk2`, `plasma_cannon`, `railgun`.
*   **Status**: `flee`, `destroyed`.
