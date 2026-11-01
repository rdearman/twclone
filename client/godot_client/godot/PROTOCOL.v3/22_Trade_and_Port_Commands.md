# 22. Trade & Port Commands

## 1. Port Interaction

### `port.dock`
Dock at a port in the current sector.
**Events**: Emits `player.dock.v1`.
**Note**: Often implies a check for `sector.info` or `port.info`.

### `trade.quote`
Request the authoritative quote for one commodity at the current port. The
response data identifies the server-owned `port_id`, canonical `commodity`,
and requested integer `quantity`; buy/sell price fields are optional.

**Args**:

*   `port_id`: ID of the port.
*   `commodity`: canonical commodity code.
*   `quantity`: positive integer amount.

The client must correlate the quote with the requested port, commodity, and
quantity before showing confirmation. A quote does not mutate credits, cargo,
stock, or turns.

### `trade.buy` / `trade.sell`
Settle one quoted commodity transaction. The server owns fixed port direction,
stock, holds, credits, fees, and turn/accounting rules; the client must not
derive those rules from cached port data.

**Args**:

*   `port_id`: ID of the port.
*   `sector_id`: confirmed current sector ID.
*   `items`: array containing `{ "commodity": <code>, "quantity": <integer> }`.
*   `idempotency_key`: non-empty key used for safe replay of the same request.
*   `account`: optional `0` for petty cash or `1` for bank; omit it to use the
    server-saved trade-account preference.

The response type is `trade.buy_receipt_v1` or `trade.sell_receipt_v1` and the
response data is the authoritative settlement receipt, including `port_id`,
`sector_id`, `player_id`, totals/fees, and settlement lines. After either a
confirmed settlement or a refusal, refresh player, ship/cargo, and port state
from the server. A lost reply is uncertain: do not retry locally or display a
synthetic receipt; reconnect and reconcile authoritative state first.

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
        "player_id": 14, "port_id": 501, "commodity": "ore",
        "quantity": 100, "price_per_unit": 45, "total_price": 4500
      }
    }
    ```
