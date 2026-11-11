# 22. Trade & Port Commands

Canonical product decisions: [`../V3_GAMEPLAY_DECISIONS.md`](../V3_GAMEPLAY_DECISIONS.md); Galactic Exchange lifecycle: [`../V3_GALACTIC_EXCHANGE_CONTRACT.md`](../V3_GALACTIC_EXCHANGE_CONTRACT.md).

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

`port.info`, `port.describe`, `port.status`, and `trade.port_info` reveal a
port's stock and trading details only when the player is in that port's
sector. Supplying a remote sector or port ID does not grant port knowledge.

Each commodity in the returned port inventory includes the server-calculated
player-facing `buy_price` and `sell_price` values. `buy_price` is the unit
price when the player buys from the port; `sell_price` is the unit price when
the player sells to the port. These are distinct from the legacy generic
`price` stock field and must not be inferred from it. The `buys` and `sells`
boolean fields identify whether the port's configured trade direction permits
each action; stock, capacity, legality, and the authoritative `trade.quote`
response can still refuse a transaction.

Illegal commodities (`SLV`, `WPN`, and `DRG`) are visible and tradeable only
when the player meets the configured `illegal_min_experience` threshold and
is neutral or evil aligned. Neutral access is controlled by
`illegal_allowed_neutral`; both settings are live SysOp configuration values.

For port classes 1–8, each commodity's buy/sell direction is fixed by its
`port_trade` mode. Inventory and capacity can prevent an otherwise valid
transaction, but changing stock does not change the port's direction. Stardock
and black-market behavior follows their separately configured trade rows.

**Args**:
*   `port_id`: ID of the port.
*   `items`: Array of objects containing:
    *   `commodity`: Canonical 3-character code (e.g., "ORE", "ORG", "EQU").
    *   `quantity`: Integer amount.
*   `account`: Optional; 0 for petty cash, 1 for bank. If omitted, the
    saved `player.set_trade_account_preference` is used, or petty cash if no
    preference has been saved. An explicit account overrides the preference.

Trade buy/sell requests require a non-empty `data.idempotency_key`. This is
the established V2 trade contract. The server also accepts matching copies in
the envelope's `meta.idempotency_key` or at the request root; if multiple
placements are present, all values must match. A retry with the same key and
trade payload replays the original receipt without applying the trade again.

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

### Galactic Exchange settlement

Planet and port inventory traded through the Galactic Exchange follows the
authoritative ownership, funding, matching and conservation contract in
[`../V3_GALACTIC_EXCHANGE_CONTRACT.md`](../V3_GALACTIC_EXCHANGE_CONTRACT.md).
An order is not a completed trade. Executed transfers are immediate virtual
inventory movements, recorded and settled atomically; no physical Exchange
transport ship or travel delay is involved. No trade may create or discard
commodities. The Exchange clearing account may receive explicit SysOp-configured
system liquidity injections, recorded distinctly from player trades; see
`galactic_exchange_liquidity_credits` in Protocol 29. Price haggling is not
part of V3; current configured dynamic prices remain authoritative.

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

### StarPort construction and class (V3 parity)

V3 includes the original StarPort lifecycle: construct a StarPort in an
eligible sector, choose its trading class during construction, upgrade its
commodity trading capacity, and destroy it under the applicable combat rules.
Class selection configures a newly built port; it is not a way to reclassify
an existing port. The B/S pattern is from the port's perspective:

| Class | Fuel ore | Organics | Equipment |
|---|---|---|---|
| BBB | Buy | Buy | Buy |
| BBS | Buy | Buy | Sell |
| BSB | Buy | Sell | Buy |
| BSS | Buy | Sell | Sell |
| SBB | Sell | Buy | Buy |
| SBS | Sell | Buy | Sell |
| SSB | Sell | Sell | Buy |
| SSS | Sell | Sell | Sell |

At most one StarPort may occupy a sector. A sector must be eligible and have
no existing port before construction. Destroying the port makes that sector
eligible again, subject to the remaining construction restrictions. Basic V3
ports have no persistent player/corporation owner; owner-specific policies,
tariffs and access are V4 scope. Exact construction costs, timing, eligibility
exceptions and destruction consequences must follow the project's selected
historical reference and are not inferred from the class table.

### `planet.trade.port`

Execute a direct Planetary Trade Agreement sale to a StarPort in the same
sector. This is separate from `planet.market.sell`, which sells to the Galactic
Exchange. The caller must own the planet or have existing planet trading
authority. The destination port must be configured to buy every requested
commodity, and existing local police/faction restrictions apply. No ship or
cargo holds are required; this transaction consumes zero turns and does not
use Exchange matching.

Request:

```json
{
  "command": "planet.trade.port",
  "data": {
    "planet_id": 123,
    "port_id": 501,
    "items": [{"commodity": "ORE", "quantity": 100}],
    "idempotency_key": "planet-port-sale-123"
  }
}
```

`items` accepts one entry per commodity (`ORE`, `ORG`, `EQU`), with a positive
quantity. The requested set is settled atomically: planetary stock, port buy
direction and capacity, port funds, and player petty-cash settlement must all
permit the complete request. The server uses the authoritative dynamic port
purchase price. Success is `planet.trade.port_receipt_v1`, containing planet,
port, sector and player IDs; per-commodity quantity/unit-price/value lines;
total proceeds; updated player credits; and `turns_consumed: 0`. A retry with
the same key and identical request replays the receipt without transferring
stock or credits again. Reusing the key for a different request is rejected.

One transaction decrements planetary stock, adds the same quantity to port
stock, debits the port CRD account, credits the player and records the trade.
Failure rolls back all effects. Planet treasury visibility is unchanged.

Historical basis: the [TradeWars 2002 Player's Manual (1990–2000)](https://electraphysics.com/documents/tradewarsmanual.pdf)
describes PTA at lines 207–217 and construction/class, material, upgrade and
single-port-per-sector behavior at lines 434–455. That reference does not
establish exact numeric construction costs or a V3 port-owner/tariff model.

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

V3 trading is fixed-price: there is no haggling or random negotiation outcome.
The fixed price is still the server's current authoritative quote, including
applicable port-class, stock, supply/demand and black-market pricing rules; it
is not one universal static commodity price.

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
