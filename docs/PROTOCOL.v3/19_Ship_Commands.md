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
