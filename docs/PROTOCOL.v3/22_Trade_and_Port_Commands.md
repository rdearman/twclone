# 22. Trade & Port Commands

## 1. Port Interaction

### `port.dock`
Dock at a port in the current sector.
**Events**: Emits `player.dock.v1`.
**Note**: Often implies a check for `sector.info` or `port.info`.

### `trade.buy` / `trade.sell`
Trade commodities.

**Args**:
*   `port_id`: ID of the port.
*   `items`: Array of objects containing:
    *   `commodity`: Canonical 3-character code (e.g., "ORE", "ORG", "EQU").
    *   `quantity`: Integer amount.
*   `account`: 0 for Petty Cash (default), 1 for Bank.
*   `idempotency_key`: Optional UUID.

**Response type**: `trade.buy_receipt_v1` / `trade.sell_receipt_v1`

**Response data**:
```json
{
  "port_id": 501,
  "sector_id": 42,
  "player_id": 14,
  "total_item_value": "4500.00",
  "fees": "45.00",
  "total_cost": "4545.00",
  "credits_remaining": "5455.00",
  "lines": [
    {
      "commodity": "ORE",
      "quantity": 100,
      "unit_price": 45,
      "value": "4500.00"
    }
  ]
}
```

**Quantity Constraints**:

The server enforces two optional quantity restrictions on trades:

1. **Per-Ship Maximum Hold Limit** (hard cap on buy only)
   - Enforced if `commodities.max_holds_per_ship` is set (non-NULL)
   - Prevents a ship from carrying more than the limit of a specific commodity
   - Check: `current_holds + qty_requested <= max_holds_per_ship`
   - Rejects with `ERR_COMMODITY_MAX_HOLDS_EXCEEDED` if violated
   - Example: "Alien Technology" limited to 1 per ship

2. **Per-Transaction Maximum** (soft cap on buy and sell)
   - Enforced if `market.trade_qty_base_max` config > 0
   - Uses multiplier from `porttype_commodity_rules.qty_max_mul` (integer scaling, 100 = 1.0x)
   - Formula: `tx_max = (market.trade_qty_base_max * qty_max_mul) / 100`
   - Check: `qty_requested <= tx_max`
   - Rejects with `ERR_COMMODITY_TX_QTY_EXCEEDED` if violated
   - Example: Base 10, multiplier 50 → per-transaction max of 5

**Default Behavior** (backward compatible):
- `max_holds_per_ship = NULL` → no per-ship limit
- `market.trade_qty_base_max = 0` → no per-transaction limit
- Existing commodities and ports unaffected

**Events**: Emits `player.trade.v1` and `trade.deal.matched`.

## 2. Hardware & Services (Stardock)

### `hardware.list`
List available upgrades (ships, equipment) at the current port.

### `hardware.buy`
Purchase equipment.

### `shipyard.*`
Commands for buying/selling ships (specifics usually covered under `hardware` or specific shipyard commands).

## 3. Events

*   **`trade.deal.matched`**: Broadcast when a trade occurs.
    ```json
    {
      "type": "trade.deal.matched",
      "data": {
        "player_id": 14, "port_id": 501, "commodity": "ORE",
        "quantity": 100, "price_per_unit": 45, "total_price": 4500
      }
    }
    ```
