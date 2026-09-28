# 22. Trade & Port Commands

## 1. Port Interaction

### `port.dock`
Dock at a port in the current sector.
**Events**: Emits `player.dock.v1`.
**Note**: Often implies a check for `sector.info` or `port.info`.

### `dock.status`
Read or change the active ship's docked state at the port in the current
sector. An empty data object reads the current state; use
`{ "action": "dock" }` or `{ "action": "undock" }` to change it.

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

### Player trade offers

`trade.offer` creates a persisted offer addressed to `to_player_id`. Required
fields are `commodity` (three-character commodity code), `quantity`, `price`
(integer credits per unit), and `mode` (`sell` or `buy`). In `sell` mode the
sender offers their cargo for the recipient's credits; in `buy` mode the
sender offers credits for the recipient's cargo. The sender must have the
offered cargo or total credits when creating the offer. Assets are not
reserved: the recipient's acceptance rechecks both ships and balances and
settles the exchange atomically, or returns an error without changing the
offer.

`expires_in` is optional (default 86400 seconds; valid range 60–604800).
`idempotency_key` is optional; a repeated key from the same sender returns the
original offer if recipient, commodity, mode, quantity, and price match, and
is rejected if they differ. The original expiry is retained. When the RPC
request has an ID, it is also used to deduplicate retries.

`trade.accept` takes `offer_id` (legacy `trade_id` is accepted) and can be
called only by the addressed recipient. `trade.cancel` takes the same ID and
can be called only by the sender. Both return the persisted offer fields and
state through `trade.offer.accepted_v1`, `trade.offer.cancelled_v1`, or
`trade.offer.expired_v1`; creation returns `trade.offer.created_v1`. Repeating
acceptance or cancellation by the authorized player is idempotent. Pending
offers expire automatically, and an accept/cancel request also detects and
records expiry immediately.

## 2. Hardware & Services (Stardock)

### `hardware.list`
List available upgrades (ships, equipment) at the current port. At a
configured special hardware port, `data.location_type` is `BLACK_MARKET` and
only hardware mapped by `porttype_items.can_buy` with positive per-port stock
is returned. Its `price` is the stock-adjusted unit price; additive
`base_price` and `stock_quantity` fields report the unchanged catalogue price
and remaining stock. Other port types retain their existing response and price
behavior.

### `hardware.buy`
Purchase equipment. At a configured special hardware port, a purchase is
accepted only for mapped hardware with enough stock. Credits, installed
hardware, and stock are updated atomically. Insufficient stock returns
`REF_PORT_OUT_OF_STOCK`.

### `shipyard.*`
Commands for buying/selling ships (specifics usually covered under `hardware` or specific shipyard commands).

## Ferengi travelling traders

Ferengi traders have stable named identities and persistent ships. Their
location is their ship's current sector. `ferengi.traders` returns active
traders in the caller's sector, their per-player reputation, and outstanding
offers. Traders move through connected sectors on the existing NPC schedule;
when a trader encounters a player it may create a durable offer and emit
`ferengi.trader.offer_v1` to that player. Returning traders retain the same
identity and relationship history. Open offers remain available through
`ferengi.traders` until accepted, rejected, or expired, even after the trader
has moved to another sector.

The seeded roster is configured by database definitions rather than compiled
into the command handler. An active definition includes a stable `trader_code`,
display name, and ship type. Ordered commodity rotation and starting stock are
stored separately with validated commodity codes. The server provisions new
active definitions on its next trader-processing pass (within 15 minutes), or
on a forced sysop tick. Offers last six hours by default;
operators can tune this with the
integer config key `ferengi.offer_lifetime_seconds` (60 to 604800 seconds).
Offer generation uses the first available commodity in the configured order,
up to 10 units, at 110% of base price. If the trader has no stock, it may offer
to buy 5 units at 90% of base price when its faction account can pay. These
terms remain fixed for an offer. Reputation is stored per player and trader
and currently does not affect pricing.

`ferengi.deal.accept` and `ferengi.deal.reject` take `deal_id`. Acceptance
settles the fixed commodity, quantity, and unit price from the trader's offer
after rechecking player credits/cargo, trader cargo/funds, and ship capacity.
Rejection records a declined deal. Both outcomes update per-player reputation
and interaction history. Actions are idempotent for already-settled,
declined, or expired deals. Open offers expire on the NPC cron pass and are
also checked when acted on. No haggling or price negotiation is defined.

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
