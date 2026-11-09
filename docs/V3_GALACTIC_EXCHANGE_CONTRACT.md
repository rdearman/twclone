# V3 Galactic Exchange Contract

This is the authoritative inventory, ownership and settlement contract for
the Galactic Exchange. It implements the approved EXC-01 through EXC-08
decisions in [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md), and
supersedes contradictory descriptions in `GALACTIC_ECONOMY.md`. This contract
does not assert that the current implementation already satisfies it.

## Participants and ownership

- Planets may sell surplus commodities to and buy commodities from the
  Exchange. Ports may buy Exchange inventory to replenish their stock.
- NPC traders continue to use existing authoritative trade paths where those
  paths conform to the same inventory and credit invariants.
- A seller owns goods until the sale transaction commits. On commit, ownership
  leaves seller inventory and enters Exchange inventory or an explicitly
  matched buyer's inventory. It may never remain available to both parties.
- Exchange goods are ring-fenced from the originating planet/port. The seller
  cannot subsequently harvest, sell, construct with, consume or count them.
- The virtual-vessel notion is an accounting/logistics abstraction only. No
  physical ship, warp, fuel, cargo hold, docking, or travel-time requirement is
  introduced. Completed destination transfers are immediately available.

## Immediate sale settlement

Within one transaction, validate seller inventory and Exchange/buyer financial
capacity; deduct the exact quantity from the seller; credit Exchange-owned
inventory or transfer to an eligible matched buyer; settle credits; record a
durable transaction; and commit. Any failed leg rolls back all legs. A retry
must not duplicate money or inventory.

An Exchange inventory increase must have an equal seller inventory decrease.
If the sale is matched directly, seller decrease equals buyer increase.
Available quantity is decremented at the authoritative source before it can be
harvested, sold again, used in construction, or consumed.

## Purchases and destination inventory

Validate Exchange stock, buyer credits, and destination capacity in the
settlement transaction. Deduct Exchange stock, credit the destination, debit
and credit the appropriate accounts, append a durable settlement record, and
commit atomically. Planet and port destinations receive goods immediately.
No transaction may cause negative inventory or unauthorized negative cash.

## Orders and matching

Order placement is not execution. The implementation must represent and
distinguish at least:

1. order placement;
2. any inventory/credit reservation (only if the current order contract
   explicitly reserves it);
3. match/fill quantities, including partial fills and remaining open amount;
4. completed financial and inventory settlement.

Only executable matches settle. Cancelled, expired, stale or already-settled
orders cannot settle again. Each executed fill is idempotent and durably
identifiable. Daily or hourly maintenance must not re-settle a completed
immediate transaction. Existing unreserved best-effort planet buy orders
remain unreserved and validate current inventory, buyer funds and capacity at
match time.

## Ports and market demand

Port target stock, capacity, dynamic prices, commodity restrictions and order
generation continue to use the existing configured economy rules. A port
shortage may result in a purchase from the Exchange; that purchase must remove
Exchange inventory and settle its funding rather than synthesize goods. Any
distinct production source must be documented and accounted for separately.
NPC trading remains a participant in the same authoritative stock/credit
constraints.

## Conservation and financial invariants

For every trade fill:

```text
seller_inventory_decrease = exchange_inventory_increase
```

or for an immediate direct transfer:

```text
seller_inventory_decrease = buyer_inventory_increase
```

and for an Exchange purchase:

```text
exchange_inventory_decrease = buyer_inventory_increase
```

Production and explicit consumption are separate sources/sinks, not trade.
All credit debits/credits and durable ledger or settlement records commit with
inventory mutations. Use locks/conditional updates and a consistent lock order
to prevent overselling. Retries, concurrency, process restarts, cancelled
orders and stale matches must not duplicate or lose settlement.

No unlimited Exchange liquidity is assumed. Before an Exchange pays sellers,
source its funding from the existing accounting model and verify that the
funds are available. If no authoritative clearing account or funded market
buyer exists, this is an implementation blocker requiring an explicit
accounting decision, not permission to create credits.

## Implementation audit checklist

Trace `cmd_planet_market_sell`, `cmd_planet_market_buy_order`,
`cmd_planet_market_buy_order_cancel`, `h_planet_market_tick`,
`h_port_economy_tick`, `h_daily_market_settlement`,
`h_market_move_planet_stock`, `h_market_move_port_stock`, the corresponding
repository functions, order tables and settlement migrations. Verify:

- where a planet sale's goods go after stock is decremented;
- whether any Exchange inventory is persisted and ring-fenced;
- how ports obtain replenishment and what account funds it;
- that immediate and scheduled paths cannot settle one trade twice;
- partial fill/remainder/cancel/restart behavior;
- conservation and credit ledger invariants under retries and concurrency.

The source audit is not itself runtime evidence. JSON/database-backed tests
must demonstrate conservation, settlement and restart behavior before these
requirements can be marked VERIFIED.

## Initial source audit (source inspection only)

| Requirement | Current path and evidence | Current behavior | Gap / next action |
|---|---|---|---|
| Planet immediate sale | `src/server_planets.c::cmd_planet_market_sell` (around lines 3066–3090) conditionally decrements planet stock and credits player petty cash in one transaction; it records taxable activity. | Seller inventory falls and seller receives credits. No Exchange stock increase or buyer-side debit appears in this settlement path. | Goods leave the planet and are not represented as Exchange inventory: a sink relative to EXC-02/03/07. The direct credit also lacks an identified Exchange-funded buyer. Must replace/extend this path with a funded, durable inventory transfer; do not create liquidity. |
| Market order settlement | `src/server_cron.c::h_daily_market_settlement` (around lines 1522–1733) locks seller/buyer locations, revalidates stock/capacity/funds, directly moves port/planet stock, posts buyer debit and seller credit, records `commodity_trades`, updates fills in a transaction. | Matched orders transfer directly between the order's seller and buyer locations, with partial fills and transactional records. There is no intermediate Exchange inventory. | Direct transfer conserves goods for a matched fill, but does not implement ring-fenced Exchange-owned stock or the required Exchange participant. Retain this working matching machinery where possible while adding an explicit accounted Exchange leg. |
| Port demand | `src/server_cron.c::h_port_economy_tick` (around lines 1364–1454) generates port BUY orders for shortages and SELL orders for surplus; buy orders carry the port as actor/location. | Ports can buy from matched seller orders and pay from their account. Orders match against existing port/planet SELL orders, not an Exchange stock source. | Port replenishment is seller-to-port direct, not a purchase from Exchange inventory. Must route through Exchange accounting without bypassing source stock or funding. |
| Planet supply | `src/server_cron.c::h_planet_market_tick` generates planet orders based on stock targets; `cmd_planet_market_sell` is a separate immediate operation. | Order book handles location actors; immediate sale is distinct and credits the player. | Ensure these paths do not offer the same quantity both as an open sell order and an immediate sale; inspect reservation/revalidation behavior during implementation. |
| Persisted model | `sql/pg/000_tables.sql` defines `commodity_orders` and `commodity_trades`; these identify actors, locations, fills and bilateral settlement records. No Exchange-owned inventory/account table or equivalent Exchange ledger was found in the inspected schema/migrations. | The model represents bilateral location transfers. | Define Exchange inventory/clearing representation using existing accounting where possible. Before paying immediate sales, identify a funded source of Exchange liquidity. |

This audit establishes a confirmed inventory-accounting gap in the immediate
planet sale path and an architectural mismatch between bilateral order fills
and the approved Exchange participant model. It does not establish runtime
behavior; no database-backed test was run during this source audit.
