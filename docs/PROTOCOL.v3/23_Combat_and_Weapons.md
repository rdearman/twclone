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

## 2. Asset Deployment Rules (FedSpace & MSL)

### Published FedSpace (Sectors 1–10)
- **Fighters**: Cannot be deployed (soft deny; assets not lost)
- **Mines** (Armid & Limpet): Cannot be deployed (soft deny; assets not lost)
- **Beacons**: Cannot be set (soft deny; no beacon placed)
- **Error Code**: Deployment commands return status `refused` with error code 1454
- **Message**: "Cannot deploy [asset type] in protected FedSpace."

### Aggression in FedSpace
- **Hard Enforcement**: Any overt aggression initiated in FedSpace (sectors 1–10) triggers immediate Captain Z intervention
- **Enforcement**: Attacker's ship is instantly destroyed and attacker is podded
- **No Mitigation**: Intervention occurs regardless of target type; action does not proceed
- **Effect**: Attack attempt returns status `refused` with error code 402 and message "Captain Z intervenes"

### Major Space Lanes (MSL Sectors > 10)
- **Deployment**: Fighters, mines, and beacons may be deployed normally
- **Federation Patrols**: Periodic cleanup removes all deployed assets from MSL sectors >10
- **Cleanup Timing**: Runs as part of the regular cron cycle
- **Effect**: Assets are destroyed (not returned to ship); players receive notification "MSL Sweep: Assets Destroyed"

## 3. Events

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

## 4. Enums

*   **Weapons**: `laser_mk1`, `laser_mk2`, `plasma_cannon`, `railgun`.
*   **Status**: `flee`, `destroyed`.
