# Planet fighter production

## Model

Weapons-assigned colonists manufacture spacecraft fighters through the existing
10-minute `planet_growth` production tick. The existing class field
`planettypes.fighterProduction` is interpreted as weapons worker-ticks required
per fighter:

| Planet class | Worker-ticks per fighter |
| --- | ---: |
| M | 10 |
| K | 15 |
| O | 15 |
| L | 12 |
| C | 25 |
| H | 50 |
| U | 0 (disabled) |

At least a level 1 citadel is required. Each fighter costs one EQU, deducted
from the planet's post-production equipment stock. A fighter is produced only
when both enough weapons worker-ticks and EQU are available. Any fractional
worker labor carries into the next tick. Labor cycles that cannot be armed for
lack of EQU or planet fighter capacity are not banked as a growing queue.

New fighters are credited to `planets.fighters`, respecting the existing
`planettypes.maxfighters` capacity. Citadel progression and costs do not change.
No soldier pool or separate military-colonist role is introduced; the
`colonists_weapons` field is a production assignment. Legacy
`colonists_mil` assignments are migrated to it and that compatibility field is
zeroed.

The production interval id is `floor(epoch_seconds / 600)`. The planet stores
the last processed id so an accidental replay cannot credit fighters or debit
their EQU twice. Ship fighters (`ships.fighters`), planet fighters
(`planets.fighters`), and sector fighters (`sector_assets`) remain separate.

## Existing paths and limits

- Planet fighter storage cap: `planettypes.maxfighters`.
- Sector deployed-fighter cap: `SECTOR_FIGHTER_CAP` (10,000), enforced by the
  existing ship-to-sector deploy command.
- Ship fighter purchase uses `shiptypes.maxfighters`.
- Ship-to-sector deployment exists. No ship-to-planet transfer path was found;
  planet/ship combat is not a transfer path. Transfer work remains outside this
  production implementation.

## Schema and compatibility

Fresh PostgreSQL installs get `colonists_weapons`,
`fighter_production_remainder`, and `fighter_production_last_tick` from
`sql/pg/000_tables.sql`. Upgrades apply
`sql/pg/114_planet_fighter_production.sql` after migration 113. The migration
preserves the headcount and assignment by moving `colonists_mil` into
`colonists_weapons`; replay adds no further workers because the old field is
zero after the first pass. Nonzero custom planet-class factors are preserved.

The existing `planet.colonists.allocate` command accepts an optional
`weapons` integer target. Omitting it preserves the current weapons assignment
for older callers. Production accounting includes weapons workers in planet
population totals.

## Validation

`tests.v2/test_planet_fighter_production.c` covers citadel gating, level 1
production, EQU shortages, planet capacity, fractional labor, and interval
replay. `tests.v2/test_pg_migration_114_planet_fighter_production.sql` validates
legacy-assignment preservation, class factor initialization, and migration
replay on a disposable PostgreSQL database.
