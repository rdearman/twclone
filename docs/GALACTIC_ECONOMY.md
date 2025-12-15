# Galactic Economy - Market-Driven Update

## Overview

The Phase 4 update introduces a **market-driven economy** where ports actively place orders to manage their inventory levels, rather than relying on static drift/replenishment mechanics. Phase 7 integrates active NPC traders (Ferengi) that perform real arbitrage between ports.

## Core Mechanics

### 1. Port Stock Targets
Each port now has a `max_capacity` calculated as `port.size * 1000`.
Ports aim to maintain a `desired_stock` level:
- **Stardock (Type 9):** 90% of capacity (`desired_level_ratio = 0.9`). High inventory ensures availability.
- **Standard Ports:** 50% of capacity (`desired_level_ratio = 0.5`). Balanced approach.

### 2. Order Generation (`h_port_economy_tick`)
Every hour (canonical tick), the system evaluates every port's stock for each commodity:
- **Shortage:** If `current < desired`, the port places a **BUY** order.
- **Surplus:** If `current > desired`, the port places a **SELL** order.
- **Quantity:** The order size is determined by `shortage * base_restock_rate` (from `economy_curve`).
- **Pricing:** Prices are calculated dynamically based on local scarcity/surplus using the existing pricing formulas.

### 3. Market Settlement (`h_daily_market_settlement`)
Once per day, the central market engine runs:
1.  **Matching:** It matches open BUY orders with OPEN SELL orders for the same commodity where `buyer_price >= seller_price`.
2.  **Settlement:**
    -   **Stock:** Physically moves goods between the seller port and buyer port using `h_market_move_port_stock`.
    -   **Credits:** Transfers credits from buyer to seller.
3.  **Constraints:**
    -   Trades are capped by the seller's available stock and the buyer's available credits.
    -   **Invariants:** Bank balances are never allowed to drop below zero. Port stock is strictly clamped between `0` and `max_capacity`.

## 4. NPC Arbitrage (Ferengi)

Ferengi traders act as **mobile arbitrage agents**. Unlike ports/planets, they do **not** place static orders in the order book. Instead, they:
1.  **Roam:** Travel between ports in their sector (and adjacent sectors).
2.  **Analyze:** Check local port prices against their cargo and known opportunities.
3.  **Instant Trade:** Execute immediate BUY/SELL actions against the port's inventory (just like a player).
    -   **Real Impact:** When a Ferengi buys ORE, the port's stock decreases, the port's cash increases, the Ferengi's cash decreases, and the Ferengi ship's cargo increases.
    -   **Persistence:** Ferengi cargo and bank balances are persistent in the DB (`ships` table and `bank_accounts`).

## 5. Faction-as-Corporation Model

NPC factions (Ferengi, Orion, Federation, etc.) are modeled using the standard corporation system:
-   **Identity:** Represented as a row in the `corporations` table (e.g., "Ferengi Alliance", tag "FENG").
-   **Banking:** They hold a standard corporate bank account (`owner_type='corp'`, `owner_id=corp_id`). All trade profits/expenses flow through this account.
-   **Ships:** NPC ships are owned by the faction's CEO player (or directly linked to the faction, depending on specific implementation), ensuring standard cargo and location tracking.

## 6. System Invariants

The economy system strictly enforces:
1.  **No Negative Balances:** Bank transactions (`h_bank_transfer`, `h_deduct_credits`) fail atomically if funds are insufficient.
2.  **Conservation of Mass:** Goods are never created/destroyed during trade. They move from Port -> Ship -> Port. (Production/Consumption happens separately via Planet logic).
3.  **Atomic Operations:** All trades occur within database transactions to prevent duping.

## Sysop Diagnostics

To monitor the market, sysop-only commands are available:

### `sys.econ.port_status`
Inspects a specific port's economic state.
**Request:** `{ "port_id": 1 }`
**Response:**
```json
{
  "id": 1,
  "name": "Earth Port",
  "size": 1000,
  "stock": [
    { "commodity_id": 1, "quantity": 50000 }
  ],
  "orders": [
    { "order_id": 101, "side": "buy", "commodity_id": 1, "quantity": 500, "price": 20, "status": "open" }
  ]
}
```

### `sys.econ.orders_summary`
Provides a global view of market depth.
**Request:** `{ "commodity_id": 1 }` (Optional filter)
**Response:**
```json
{
  "1": {
    "count_buy": 15,
    "total_qty_buy": 15000,
    "count_sell": 5,
    "total_qty_sell": 2000
  }
}
```

### `sys.npc.ferengi_tick_once` (Debug)
Manually triggers a single tick of the Ferengi AI logic for testing/debugging purposes.
**Request:** `{}`