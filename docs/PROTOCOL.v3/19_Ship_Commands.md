# 19. Ship Commands

## Overview
Ship commands provide information about the player's ship and allow ship management operations.

## 1. Ship Information

### `ship.status`
Get detailed status of the player's current ship, including cargo.

**Request**: `{}`

**Response type**: `ship.status`

**Response data**:
```json
{
  "ship": {
    "id": 5,
    "name": "Merchant Vessel",
    "type_id": 2,
    "hull": 100,
    "holds": 10,
    "fighters": 1,
    "shields": 1,
    "onplanet": 0,
    "ported": 0,
    "cargo": [
      {
        "commodity": "EQU",
        "quantity": 8
      },
      {
        "commodity": "ORG",
        "quantity": 2
      }
    ]
  }
}
```

`ship.hull` is the authoritative current hull of the authenticated player's
active ship. It is an integer from `0` through `100`; `100` is fully repaired
and `0` is destroyed. A failed hull read fails `ship.status` rather than
substituting a default value.

**Cargo Field**:
- Returns an array of objects with `commodity` (string) and `quantity` (integer)
- Only includes commodities with quantity > 0
- Possible commodities: `ORE`, `EQU`, `ORG`, `COL`, `SLV`, `DRG`, `WPN`
- Empty cargo holds are not included in the array

## 2. Ship list

`ship.list` takes an empty object and returns `ship.list.response` with a
`ships` array containing the caller's primary-owned ships. Entries include
`ship_id`, `name`, `type_id`, `sector_id`, `holds`, `fighters`, `shields`, and
an `active` flag derived from the authoritative player record.

## 3. Towing status

### `ship.tow.status`
Returns the towing links for the authenticated player's active ship without
changing them. This is safe to call after reconnecting and is the canonical way
to reconcile towing state.

**Request**: `{}`

**Response type**: `ship.tow.status`

```json
{
  "ship_id": 5,
  "towing_ship_id": 12,
  "towed_by_ship_id": null
}
```

`towing_ship_id` is the ship being towed by the active ship; `towed_by_ship_id`
is the ship towing the active ship. Each field is `null` when the relationship
is absent. Existing `ship.tow` engage/disengage requests and response types
are unchanged.

### `ship.tow`
Engage a tow using `{ "target_ship_id": 12 }`. When already towing, omit
`target_ship_id` (or send the current target) to disengage. The response is
`ship.tow.engaged` or `ship.tow.disengaged`, with `status` and
`towee_ship_id`. Target ships must be in the same sector, owned by the player
or their corporation, and not currently piloted or towed.

### `ship.info` (Deprecated)
**Legacy alias** for `ship.status`. Use `ship.status` instead.

**Note**: In Protocol v3, `ship.info` is maintained for backward compatibility but may be removed in future versions. All new clients should use `ship.status`.

## 2. Other Ship Commands

### `ship.inspect`
Inspect a visible ship in the player's current sector. Supply a positive
integer `ship_id`. The `ship.inspect` response contains `ship`, an object
with `ship_id`, `name`, and `is_derelict`. `is_derelict` is true when the
visible ship has no ownership record. It does not expose cargo or other
private ship state. A cloaked ship is inspectable only by its primary owner. Remote,
cloaked foreign, and nonexistent ships return the same not-found error.
Clients that previously consumed the sector-wide `ships` list must instead
send the selected `ship_id` and render `data.ship`; treat a refusal as an
unavailable target and do not infer undisclosed ship fields.

### `ship.rename`
Rename the active ship owned by the caller with `{ "name": "New name" }`.
On success, `ship.renamed` returns `ship_id` and `name`. The
`ship.reregister` command currently uses the same request and behavior.

### `ship.repair`
Repair ship damage at a port.

### `ship.upgrade`
Upgrade ship equipment or capabilities.

### `ship.transfer_cargo`
Transfer cargo between two ships owned by the caller. The ships must be in
the same sector. Request fields are `{ "commodity_id": 1, "quantity": 2,
"target_ship_id": 37 }`; the source is the caller's active ship. The server
resolves `commodity_id` through the commodity catalogue, checks source cargo
and target holds, and applies both changes atomically. Success returns
`ship.cargo_transferred_v1` with source/target IDs and resulting quantities.
Planetary stock transfers use the separate planetary commands. Idempotency
keys are not supported for this command.

### `ship.jettison`
Dump cargo into space.

`ship.jettison` and the legacy `trade.jettison` command accept all ship cargo
types, including `COL` and `SLV`. Jettisoning one colonist or slave reduces the
player's alignment by one point; the penalty is linear in the quantity, applies
at every starting alignment, and has no alignment floor, daily cap, cooldown,
or diminishing returns. Rank bands may stop changing below their lowest
configured range, but the underlying alignment value continues to decrease.
Ordinary commodities do not change alignment. Cargo removal, the
normal one-turn cost, and the population alignment penalty are committed
atomically. A successful response includes the player's updated `alignment`.

### `ship.self_destruct`
Destroy your own ship (irreversible).

### `shipyard.list`
With no arguments, list hulls available at the shipyard where the player is
docked. The `shipyard.list_v1` response includes `sector_id`, `is_shipyard`,
`current_ship` (`type`, `base_price`, `trade_in_value`), and an `available`
array with each hull's prices, eligibility, and any restriction reasons.
It also includes `used_available`, a list of persistent used hulls at the
facility. Entries identify `used_inventory_id`, the original `ship_id`, name,
type, original hull value, retained-equipment value and current resale price.
Used hulls are existing ships, not newly manufactured stock.

### `shipyard.upgrade`
Upgrade the active ship while docked at a shipyard. Args:
`{ "new_type_id": 2, "new_ship_name": "Merchant Vessel" }`.
On success, `shipyard.upgraded_v1` acknowledges `ship_id`, `new_type_id`,
`new_ship_name`, and `credits_spent`. Failures retain the existing error codes.
The same command also accepts `{ "used_inventory_id": 17 }` to purchase a
listed used hull. Price is
`floor((original_hull_value + retained_equipment_value) * factor_bp / 10000)`;
`shipyard_used_resale_factor_bp` defaults to 8000 (80%) and is independent of
the seller's 6000-bp payout and new-hull trade-in factor. The purchase preserves
the used ship's ID, condition and installed equipment, removes its listing and
makes it the active ship atomically. The former active hull remains owned but
non-primary. The response includes the consumed `used_inventory_id`.

### `ship.sell` and `shipyard.sell`
These command names invoke the same standalone sale operation. The authenticated
player must be docked at a StarDock, an enabled shipyard, or an authorized
black-market yard, and the sold hull must be docked at that same facility.
Shipyard eligibility is read from the database-backed port classification and
inventory, not from a numeric port type. Existing local jurisdiction bans apply
at black-market yards.

Request:
`{ "ship_id": 12, "replacement_ship_id": 14 }`.
Both fields are required. The replacement must be a different, eligible
primary-owned ship. The player cannot sell their last primary-owned ship. If
the sold hull is active, the selected replacement becomes active in the same
transaction.

Canonical base prices for sale valuation are listed in
[`V3_EQUIPMENT_CATALOGUE.md`](../V3_EQUIPMENT_CATALOGUE.md). In this
catalogue `PROBE` is a Subspace Ether Probe, not the separate Psychic Probe.
Mines, Limpets, probes, photons, and beacons are constrained by the selected
ship type's configured capacities during purchase. The classic TWGS 3.13
reference prices are Armid 1,000, Limpet 10,000, Ether Probe 3,000, Photon
40,000, and Marker Beacon 100 credits; local catalogue rows may retain
deployment-specific values.

The sale proceeds are
`floor((shiptypes.basecost + installed equipment value) * factor_bp / 10000)`.
Equipment unit values come from the hardware catalogue. The standalone factor
defaults to 6000 basis points and is controlled by the SysOp key
`shipyard_sale_factor_bp` (0–10000); it is separate from the upgrade trade-in
factor. Cargo is forfeited without compensation. Installed equipment and the
ship's hull identity/condition remain with the used hull. Any active insurance
policy on that ship is terminated without refund.

Success returns `ship.sold_v1` with `ship_id`, `replacement_ship_id`,
`shipyard_port_id`, `hull_valuation`, `installed_equipment_valuation`,
`sale_factor_bp`, `sale_proceeds`, `updated_credits`, `cargo_forfeited`,
`insurance_terminated`, `sale_id`, and `used_inventory_id`. The sale, cargo
forfeiture, insurance termination, ownership removal, active-ship replacement,
credit settlement, audit record, and used-inventory entry commit atomically.
Repeating a completed sale cannot pay twice because the sold hull is no longer
owned by the seller. A used-inventory hull is not an abandoned hull and cannot
be acquired through `ship.claim`.

Used-hull acquisition is implemented through the existing
`shipyard.upgrade` workflow, selecting `used_inventory_id` from
`used_available`. It retains the ship ID, equipment and current condition,
charges the configured used resale price, removes the inventory listing and
makes that existing hull active in the same transaction. New-hull purchasing
and upgrade trade-in pricing remain unchanged.

---

## 3. Cargo System Invariants (Phase 1+)

**Effective:** Protocol v3.1 (February 2026)

Ship cargo is now **commodity-agnostic**: the system no longer has special handling for individual commodity types.

### Storage Model
- Cargo is stored in a normalized `ship_cargo` table (one row per ship-commodity pair)
- All commodities (legal or illegal) count identically toward ship holds
- Empty cargo (0 quantity) does not create database rows

### Capacity Invariant
```
SUM(all_commodity_quantities) <= ship.holds
```

This invariant is **hard-enforced**:
- **All** cargo operations (buy, sell, transfer, deposit, withdraw) verify this invariant before committing
- **All** commodities count equally (no exemptions for any commodity)
- Attempts to exceed capacity return `ERR_HOLD_FULL` (1605)

### Quantity Invariant
```
FOR ALL commodities:
  quantity >= 0 AND quantity IS NOT NULL
```

This invariant is **hard-enforced**:
- Negative quantities are never allowed
- Attempts to create negative quantities return `ERR_DB_MISUSE` (1008)

### Commodity Codes
Valid commodity codes are:
- **Legal:** `ORE`, `EQU`, `ORG`, `COL`
- **Illegal:** `SLV`, `WPN`, `DRG`

All commodity codes in responses and protocol messages are **exactly 3 uppercase ASCII characters**. No full names (`ORE` not `ore`, `SLV` not `slaves`).

---

**See Also**:
- [22_Trade_and_Port_Commands.md](./22_Trade_and_Port_Commands.md) for cargo trading
- [23_Combat_and_Weapons.md](./23_Combat_and_Weapons.md) for ship combat
