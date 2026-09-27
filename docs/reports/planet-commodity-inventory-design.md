# Planet commodity inventory and colony economy

## Why this is changing

Planets currently have three overlapping representations of commodity state:

- `planets.ore_on_hand`, `organics_on_hand`, and `equipment_on_hand` are read and written by ship transfers and citadel construction.
- `planet_goods` has per-planet commodity rows with `quantity`, `max_capacity`, and `production_rate`; its rows are also used to decide which commodities a planet accepts.
- `entity_stock` stores quantities for both planets and ports. The planet production tick, planetary market orders, and planetary market commands use this table.

The same planet can therefore have different apparent balances depending on whether the player is transferring goods, upgrading a citadel, producing goods, or using the planet market. The current `planet_goods` commodity check also prevents the data-driven commodity model from accepting new commodity codes.

## Data ownership

Each table will have one runtime responsibility:

| Data | Owner | Meaning |
|---|---|---|
| Commodity balance | `entity_stock` | The authoritative amount held by a planet or port, keyed by entity and commodity code. |
| Planet acceptance and storage limit | `planet_goods` | A row means the planet accepts that defined commodity; `max_capacity` is its storage limit. |
| Base production and consumption | `planet_production` | Per-planet-type rates for each commodity. |
| Colonists and assigned jobs | `planets` | Population is separate from goods. Unassigned colonists can be moved into ore, organics, or equipment jobs. |
| Class capacity defaults | `planettypes` | Existing `maxore`, `maxorganics`, and `maxequipment` values seed the initial `planet_goods.max_capacity` for those goods. They are not consulted as a second live stock limit. |

`planet_goods.quantity` and `production_rate` are legacy fields. Runtime code will stop reading and writing them. They remain in the schema during this transition so old installs and tools do not break; a later cleanup can remove them after all deployments have migrated.

For the built-in ORE, ORG, and EQU goods, a planet's initial capacity comes from its class defaults. Other commodities require an explicit `planet_goods` row and capacity. This supports the commodity catalogue without assigning arbitrary capacities to every commodity.

## Runtime flows

### Production

On the planet growth tick, the server combines the planet type's base production/consumption rates with the planet's assigned workers and updates `entity_stock`. The amount is clamped to the matching `planet_goods.max_capacity`. Unassigned colonists remain available for later assignment, transfer, or citadel construction. Commodity records and capacities stay separate from worker counts.

### Ship transfer

Depositing and withdrawing a commodity changes ship cargo and the matching planet `entity_stock` balance in one transaction. The command is available only for commodities accepted by that planet and refuses transfers that exceed available stock, ship cargo, or planet capacity. Colonist transfer continues to update the separate population fields.

### Citadel construction

The server checks and deducts ORE, ORG, and EQU from the same `entity_stock` rows used by production and transfer. Colonist costs are deducted from the unassigned population. The existing construction time and level rules remain authoritative on the server.

### Planet markets

Planet market sell orders, buy orders, and settlement read and update the same `entity_stock` balances. A trade cannot create a second copy of an item's stock. Market and transfer paths respect the planet's `planet_goods` acceptance and capacity configuration.

### Client display

The planet screen displays server-confirmed balances and capacities from the commodity records. It displays colonist assignments and estimated production separately from confirmed inventory. Pending mutations remain distinct from server-confirmed results.

## Existing database transition

The PostgreSQL and MySQL migrations will:

1. Remove the fixed five-code restriction from `planet_goods.commodity`.
2. Create missing ORE, ORG, and EQU capacity rows for existing planets, using their planet class defaults.
3. Move existing legacy `planet_goods.quantity` and `planets.*_on_hand` balances into `entity_stock`.
4. Clear the migrated legacy balances so rerunning the migration cannot duplicate stock.

The migration is a schema/data migration for the running database. It does not edit Big Bang, universe generation, or seeded-world creation SQL. New Genesis planets get their built-in capacity rows from server code. Legacy seeded planets are covered by the migration; the server's reconciliation path also fills missing built-in capacity records idempotently.

## Ticket relationship

- [#263 Full Planet Colonization Flow](https://github.com/rdearman/twclone/issues/263): this work aligns settlement, initial stock, and resource display, but does not by itself complete every colonization feature in that ticket.
- [#264 Planetary Trade Agreements](https://github.com/rdearman/twclone/issues/264): remains open; automated planet-to-port agreements are separate from inventory consistency.
- [#265 Planet Factory Construction](https://github.com/rdearman/twclone/issues/265): remains open; factories are separate production/defense structures.
- [#266 Manual Planet Mining and Abandoning](https://github.com/rdearman/twclone/issues/266): remains open; scheduled worker production is not the proposed manual-mining command.
- [#416 Canon Planets, Colonists & Citadels](https://github.com/rdearman/twclone/issues/416) and [#417 Colonist morale and job specialisation](https://github.com/rdearman/twclone/issues/417): broader canon systems remain beyond this inventory correction and basic worker allocation.

The smaller work to unify commodity capacity and balances does not fully resolve any of those larger feature tickets, so they should not be closed as a side effect.

## Implemented behavior

The server now creates ORE, ORG, and EQU capacity rows for newly created Genesis planets, and creates their zero-balance `entity_stock` rows. The planet growth tick uses `planet_goods.max_capacity` and upserts into `entity_stock`. Planet commodity deposits and withdrawals, market movement, and citadel resource checks/deductions use that same balance. Citadel construction also consumes its colonist requirement from the unassigned population. Planet market orders and transfers reject goods without a planet capacity row, and transfers cannot exceed the configured limit.

`planet.info` now includes a `goods` array with confirmed quantity, capacity, and production rate. The Godot planet surface displays those inventory values and builds its transfer list from the server's accepted commodities. Colonists and citadel credits remain separate systems.

Apply `sql/pg/108_planet_entity_stock.sql` to a PostgreSQL installation or `sql/my/108_planet_entity_stock.sql` to a MySQL installation before deploying the server. These migration files reconcile the existing running database; they are not run automatically by the server. The new-install table definitions also allow additional commodity codes. No Big Bang or universe-generation SQL was changed.

Existing seeded planets receive class-based ORE/ORG/EQU capacity defaults from the migration. Later commodity types must be explicitly configured by inserting a `planet_goods` row with a capacity. Existing planetary production is still limited to commodities configured in `planet_production`.
