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

**Cargo Field**:
- Returns an array of objects with `commodity` (string) and `quantity` (integer)
- Only includes commodities with quantity > 0
- Possible commodities: `ORE`, `EQU`, `ORG`, `COL`, `SLV`, `DRG`, `WPN`
- Empty cargo holds are not included in the array

## 2. Towing status

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
Inspect another player's ship in the same sector.

### `ship.rename`
Change the name of your ship.

### `ship.repair`
Repair ship damage at a port.

### `ship.upgrade`
Upgrade ship equipment or capabilities.

### `ship.transfer_cargo`
Transfer cargo between ships or to/from planets.

### `ship.jettison`
Dump cargo into space.

### `ship.self_destruct`
Destroy your own ship (irreversible).

### `shipyard.list`
With no arguments, list hulls available at the shipyard where the player is
docked. The `shipyard.list_v1` response includes `sector_id`, `is_shipyard`,
`current_ship` (`type`, `base_price`, `trade_in_value`), and an `available`
array with each hull's prices, eligibility, and any restriction reasons.

### `shipyard.upgrade`
Upgrade the active ship while docked at a shipyard. Args:
`{ "new_type_id": 2, "new_ship_name": "Merchant Vessel" }`.
On success, `shipyard.upgraded_v1` acknowledges `ship_id`, `new_type_id`,
`new_ship_name`, and `credits_spent`. Failures retain the existing error codes.

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
