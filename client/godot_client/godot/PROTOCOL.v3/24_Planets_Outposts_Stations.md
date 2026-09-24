
## 5. Planet Combat (Phase C0+C1)

### `combat.attack_planet`
Attack a planet with ship fighters. Only works if planet is in the same sector.
**Terra Protection:** Attacking Sector 1 (Terra) results in immediate ship destruction.

**Request:**
```json
{
  "command": "combat.attack_planet",
  "data": {
    "planet_id": 123
  }
}
```

**Response:**
```json
{
  "type": "combat.attack_planet",
  "data": {
    "planet_id": 123,
    "attacker_remaining_fighters": 50,
    "defender_remaining_fighters": 0,
    "captured": true
  }
}
```
