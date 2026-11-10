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

The Exchange is a game-system participant, not a self-financing real-world
institution. It uses a virtual system clearing account (`system/70000001`).
`galactic_exchange_liquidity_credits` is a SysOp-configured liquidity target;
it defaults to zero, so the engine does not silently create credits. When
configured liquidity is injected, the bank ledger records it as
`SYSTEM_LIQUIDITY` with the `GALACTIC_EXCHANGE_LIQUIDITY` group. Such explicit
engine funding is distinct from player trades. Liquidity injections may keep
the market usable, but may not duplicate player credits, goods or completed
settlements.

Exchange inventory uses the virtual `entity_stock` location `exchange/1`.
Planet sales move exact quantities into this inventory and debit the clearing
account in the same transaction. The daily matcher exposes only current
Exchange inventory as an Exchange sell order; fills conditionally decrement
that inventory and transfer goods directly to the buyer's planet or port.
No ship or delivery delay is involved.

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
| Planet immediate sale | `src/server_planets.c::cmd_planet_market_sell` debits planet stock, adds Exchange stock, records a unique Exchange transaction, pays from the system clearing account and records taxable activity in one transaction. | Goods are ring-fenced in `exchange/1`; player receives existing petty-credit proceeds; funding is capped/replenished to the configured liquidity target. | Source implementation added; verify migration, credit/inventory conservation and duplicate request behavior in DB runtime. |
| Market order settlement | `src/server_cron.c::h_daily_market_settlement` exposes Exchange inventory as a refreshed sell order, locks the virtual stock location, conditionally decrements stock, then transfers goods and credits through existing order settlement. | Buyers can acquire only persisted Exchange quantity; existing bilateral matching remains intact. | Source implementation added; test partial fills, retry/restart and concurrent port/planet buys in DB runtime. |
| Port demand | `src/server_cron.c::h_port_economy_tick` creates shortage BUY orders; matcher now offers available Exchange inventory. | Port purchases use existing account, stock and capacity checks; no artificial goods are created. | Source path connected; demonstrate configured port account liquidity and stock flow at runtime. |
| Planet supply | `src/server_cron.c::h_planet_market_tick` generates planet orders based on stock targets; `cmd_planet_market_sell` is a separate immediate operation. | Order book handles location actors; immediate sale is distinct and credits the player. | Ensure these paths do not offer the same quantity both as an open sell order and an immediate sale; inspect reservation/revalidation behavior during implementation. |
| Persisted model | Migration `174_galactic_exchange_accounting.sql` allows the virtual Exchange location/actor, records immediate sales with an idempotency key, and adds a distinct system-liquidity bank transaction type. | Exchange stock is held in existing `entity_stock`; the clearing account is an existing system bank account. | PostgreSQL migration has not been executed in this session. Equivalent MySQL migration/backend behavior remains to be assessed. |

Source now implements the bounded Exchange inventory and liquidity path above.
This does not establish runtime behavior: no database-backed test was run in
this session.
