# Port Behaviour Canon Audit Report

**Date:** 2026-01-31
**Subject:** TradeWars 2002 Canon Parity Audit — Port Realism

## 1. Port Model Summary

The port system in `twclone` is built on a hybrid data model that diverges significantly from the canonical "Port Class" system.

### Key Tables
- **`ports`**: Stores basic port metadata (name, sector, size, techlevel).
- **`port_trade`**: Defines the "Class" behavior (e.g., SBB, SSB) by mapping commodities to "buy" or "sell" modes. This table is populated during universe generation (Class 1-8).
- **`entity_stock`**: Stores the actual runtime state (quantity and dynamic price) for each port/commodity pair.
- **`economy_curve`**: Stores parameters for the dynamic pricing and restock engine (elasticity, volatility, target stock).

### Core Divergence
While the `port_trade` table exists and contains canon-style mappings, **the server's runtime logic (`server_ports.c`) almost entirely ignores it.** Instead, the server determines whether a port "buys" or "sells" based on the presence of a record in `entity_stock` and the current quantity relative to capacity. This results in ports that can effectively both buy and sell the same commodity if they have both stock and available space.

---

## 2. Canon Checklist Table

| Item | Requirement | Status | Evidence |
| :--- | :--- | :--- | :--- |
| 1 | **Stable Modes** (Class 1-8) | ❌ Missing | `h_port_sells_commodity` returns true if `qty > 0`. `h_port_buys_commodity` returns true if `qty < capacity`. Mode is not fixed. |
| 2 | **Finite Inventory** | ✅ Implemented | Enforced in `cmd_trade_buy` and `cmd_trade_sell` via `h_update_port_stock`. |
| 3 | **Deterministic Restock** | ✅ Implemented | `h_port_economy_tick` runs hourly via cron to generate market orders. |
| 4 | **Dynamic Pricing** | ⚠️ Partial | Implemented via `h_calculate_port_sell_price` (fill-ratio based), but diverges from canon's simple daily refresh to a complex inter-port economy. |
| 5 | **Scan Leakage** | ❌ Divergent | `cmd_trade_port_info` allows querying any `sector_id` without proximity checks, leaking stock/price for the whole universe. |
| 6 | **Alignment Gating** | ✅ Implemented | `h_can_trade_commodity` enforces alignment checks for illegal goods (slaves/drugs/weapons) server-side. |
| 7 | **Special Ports/Stardock** | ✅ Implemented | `cmd_hardware_buy` handles Stardock/Class-0 services, though stock is infinite (deliberate simplification). |
| 8 | **Anti-exploit Invariants** | ✅ Implemented | `h_market_move_port_stock` includes `__builtin_add_overflow` and clipping to `[0, max_capacity]`. |

---

## 3. What’s Missing / Divergent

### Mode Instability (Divergent)
In canon TW2002, a port has a fixed role (e.g., Class 1 sells Ore and buys Organics/Equipment). In `twclone`, if an Ore-selling port somehow gains enough credits and loses all its Ore, it could theoretically start buying Ore.
- **Location:** `src/server_ports.c`: `h_port_buys_commodity` and `h_port_sells_commodity`.
- **Impact:** Breaks the fundamental "trade loop" gameplay of TW2002 where players must navigate between specific supply and demand points.

### Global Port Info (Divergent)
The `port.info` / `port.status` commands (handled by `cmd_trade_port_info`) do not verify if the player is in the target sector or possesses specialized scanning hardware.
- **Location:** `src/server_ports.c`: `cmd_trade_port_info`.
- **Impact:** Players can build a perfect real-time map of universe-wide prices and stocks instantly, removing the exploration and "learning" aspect of the game.

### Inter-Port Economy (Divergent-by-Design)
The game implements a sophisticated "Settlement" system where ports place orders and trade with each other.
- **Location:** `src/server_cron.c`: `h_daily_market_settlement`.
- **Impact:** While realistic, this is "Non-Canon". It creates a living economy that behaves differently from the predictable periodic inventory resets players expect from TW2002.

---

## 4. Test Gaps

1. **Proximity Enforcement Test**: `test_port_info_distance_enforcement`
   - Assert that `port.info` returns an error if requested for a sector the player is not currently in.
2. **Fixed Mode Enforcement Test**: `test_port_mode_integrity`
   - Assert that a Class 1 port (SBB) refuses to buy Ore even if its quantity is 0. (Currently, it would likely allow it).
3. **Negative Stock/Overflow Test**: `test_trade_overflow_protection`
   - Assert that attempting to buy/sell quantities near `INT_MAX` or that would result in negative values are caught and clipped or refused.

---

## 5. Key Code Pointers

- **Trade Logic:** `cmd_trade_buy` and `cmd_trade_sell` in `src/server_ports.c`.
- **Pricing Engine:** `h_calculate_port_sell_price` and `h_calculate_port_buy_price` in `src/server_ports.c`.
- **Alignment Gating:** `h_can_trade_commodity` in `src/server_ports.c`.
- **Market Settlement:** `h_daily_market_settlement` in `src/server_cron.c`.
- **Universe Seeding:** `generate_ports` in `sql/pg/040_functions.sql`.
- **Port Visibility:** `db_ports_at_sector_json` in `src/db/repo/repo_cmd.c`.
