# PostgreSQL v2 database upgrade

This guide covers the PostgreSQL schema/data migrations through 108. Run it
against a restorable backup or disposable clone before production. Stop game
and engine writes for the migration window, especially while migration 108
moves planet balances.

`sql/pg/000_tables.sql` is the fresh-install schema, not an upgrade script. It
already creates `ship_cargo`; migration 100 creates that table if it is absent
on an older installation, then copies legacy ship cargo. Do not use the base
schema file to upgrade an existing database.

## Existing database upgrade

Run from the repository root with `psql` and stop at the first SQL error:

```sh
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/100_init_ship_cargo.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/101_phase4_shiptypes_restrictions.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/102_phase5_porttypes.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/103_phase6_items_legality.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/104_phase8_port_behaviour.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/105_phase9_dynamic_pricing.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/106_phase9_2_cluster_pressure.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/107_seed_illegal_commodities.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/108_planet_entity_stock.sql
```

The sequence assumes the existing database has the v2 baseline tables and
lookup data, including `ships`, `commodities`, `shiptypes`, `ports`,
`hardware_items`, `clusters`, `cluster_sectors`, `planettypes`, and `planets`.
Run 107 only after clusters have sector assignments. Run 108 after planets
exist. If world generation is performed as a separate deployment step, keep
writes stopped until both data migrations have completed.

These scripts can be replayed after success. Migrations 100–108 use
`ON CONFLICT`, `IF NOT EXISTS`, or guarded updates for replay safety. The
multi-statement PostgreSQL scripts are transaction-wrapped, so `ON_ERROR_STOP`
causes a failed script to leave its transaction uncommitted. Migration 108
adds legacy balances to `entity_stock`, then zeros their old locations in the
same transaction; rerunning after success does not add them again.

## Fresh installation

1. Apply `sql/pg/000_tables.sql`, then `010_indexes.sql`, `020_views.sql`, and
   `040_functions.sql`.
2. Apply the release's lookup/economy seed set and generate the initial
   sectors, clusters, ports, and planets.
3. Apply migrations 100–106 in order so restrictions, port types, rules, and
   dynamic pricing state are present and existing ports are backfilled.
4. Apply 107 after `cluster_sectors` has been populated, then 108 after planets
   have been inserted. On a genuinely fresh world, legacy balances should be
   zero; these scripts also safely initialize the corresponding new tables.

The repository currently has no single release-owned fresh-install command
that selects the seed files and world-generation steps. Confirm that list with
the release package before provisioning a new database.

## Isolated migration fixtures

The SQL fixtures below create temporary tables in the test session and shadow
the application relations; they do not alter persistent tables:

```sh
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_pg_migration_100_ship_cargo_upgrade.sql
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_migration_102_porttypes.sql
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_migration_108_planet_entity_stock.sql
```

Use the tests only with a PostgreSQL database where the test account may create
temporary tables. They validate data preservation and replay behavior; they
do not substitute for a full upgrade rehearsal against a disposable database
restored from a representative backup.

## Remaining operational limits

There is no migration version/checksum table or automatic runner. Operators
must track which scripts completed and retain a backup for recovery. The
PostgreSQL driver is the only implemented runtime backend; MySQL remains a
separate v3 project as described in `MYSQL_BACKEND_COMPLETION_PLAN.md`.
