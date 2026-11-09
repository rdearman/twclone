# 23. Combat & Weapons

Canonical product decisions: [`../V3_GAMEPLAY_DECISIONS.md`](../V3_GAMEPLAY_DECISIONS.md).

## 1. Combat Commands

### `combat.attack.ship`
Attack another ship.
**Args**: `{ "target_ship_id": 123, "weapon": "laser_mk1" }`

### `combat.attack_planet`
Attack a planet in the current sector.
**Args**: `{ "planet_id": 123 }`.
**Response**: `combat.attack_planet` returns `planet_id`,
`attacker_remaining_fighters`, `defender_remaining_fighters`, and `captured`.
Successful attacks are recorded as `player.attack_planet.v1` with `planet_id`,
`result`, `ship_loss`, and `planet_loss`; captures are recorded as
`player.capture_planet.v1` with `planet_id` and `previous_owner`.
Both events are persisted in the same transaction as fighter losses, ownership,
and population conversion; event-write failure aborts the combat settlement.
A capture also compares the observed owner, population type and planetary
fighter count at settlement; concurrent changes abort rather than bypassing
reinforced defenses or overwriting a newer capture.

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

### Ship destruction and respawn

An attack on Terra (planet 1 or a planet in sector 1) is refused after the
attacker's active hull is settled through the universal destruction path. The
attacker is assigned an escape pod. This enforcement does not seize unrelated
ships, credits, bank balances, planets or corporate assets; ordinary losses
for the destroyed hull still apply. If destruction settlement fails, the
server returns a database error and does not claim enforcement succeeded.

Every player ship destruction results in escape-pod respawn. This does not
depend on pod equipment. The destroyed ship's cargo and equipment are lost;
bank credits, planets and corporate assets survive. No basic replacement ship
is issued, and the player has exactly one valid active pod ship after
settlement. All destruction sources use the same idempotent loss/respawn
settlement. The current C repository transaction writes the destroyed hull,
loss state, pod ownership/active-ship assignment, podded status and one
per-hull destruction event atomically. Eligible insurance pays configured
player-held ship-policy payouts, capped in aggregate at the shiptype base cost,
to the player's bank ledger in the same transaction. It covers neither cargo
nor installed equipment, does not directly replace the ship, and cannot pay
twice for one destruction. Policies are applied in policy-ID order if several
cover a hull. If a policy fund or bank account is missing, or a fund cannot
cover its allocated payout, destruction still completes and the claim remains
open for the existing claim workflow; guaranteed payout under insurer
insolvency remains unresolved. Selling an insured ship ends that ship's policy
without refund.

### Planet capture and population

A planet may be captured only after its defences are defeated and the existing
invasion succeeds. Capture transfers planet ownership while the planet's
resources and treasury remain with the planet; it does not transfer the
previous owner's personal bank balance or unrelated corporate funds. Preserve
other planet assets unless an explicit combat rule destroys them.

Population conversion is part of the same capture transaction. An evil captor
must explicitly choose whether to enslave a colonist population. A good
captor automatically liberates a slave population. Neutral captors retain an
existing slave population; evil captors may also retain it. A good captor's
liberation converts the full population to colonists. Capture does not itself
permit partial population conversion. Do not invent corporate-capture rules.

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
