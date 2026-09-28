# PostgreSQL v2 database upgrade

This guide covers the PostgreSQL schema/data migrations through 114. Run it
against a restorable backup or disposable clone before production. Stop game
and engine writes for the migration window, especially while migrations 107,
108 and 109 update stock, planet balances, and notices; 110 adds typed sector
environmental hazards, 111 adds ship personalities, 112 adds economy and
commerce data, 113 adds Ferengi configuration, and 114 enables planetary
fighter production.

`sql/pg/000_tables.sql` is the fresh-install schema, not an upgrade script. It
already creates `ship_cargo`; migration 100 creates that table if it is absent
on an older installation, then copies legacy ship cargo. Do not use the base
schema file to upgrade an existing database.

## Existing database upgrade

Run from the repository root with `psql` and stop at the first SQL error. On
installations whose commodity seed predates `COL`, replay the idempotent
essential seed first:

```sh
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/091_seed_essential.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/100_init_ship_cargo.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/101_phase4_shiptypes_restrictions.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/102_phase5_porttypes.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/103_phase6_items_legality.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/104_phase8_port_behaviour.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/105_phase9_dynamic_pricing.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/106_phase9_2_cluster_pressure.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/107_seed_illegal_commodities.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/108_planet_entity_stock.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/109_v2_sector_notices_trade_offers.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/110_sector_environmental_hazards.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/111_ship_personalities.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/112_v2_economy_commerce.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/113_ferengi_trader_configuration.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f sql/pg/114_planet_fighter_production.sql
```

The sequence assumes the existing database has the v2 baseline tables and
lookup data, including `ships`, `commodities`, `shiptypes`, `ports`,
`hardware_items`, `clusters`, `cluster_sectors`, `planettypes`, and `planets`.
Run 107 only after clusters have sector assignments. Run 108 after planets
exist. If world generation is performed as a separate deployment step, keep
writes stopped until both data migrations have completed.

The `COL` seed represents internal ship cargo and is excluded from the legacy
port-trading commodity constraints/rules. The current fresh-install seed
contains it as well.

Migration 109 adds sector/player notice scope and the player trade-offer
storage used by the corresponding server features. Apply it only with the
matching server release. It passed both its isolated replay fixture and the
combined 100–109 run against a restored synthetic backup. A representative
production backup rehearsal is still required for release timing and locks.

Migration 110 creates the `sector_hazards` table used by movement/combat entry
resolution. It is safe to run on a fresh install where `000_tables.sql` has
already created the table. It does not backfill existing `nebulae` or `navhaz`
values because those columns do not encode a reliable hazard type or severity.
Add configured hazards explicitly after deployment if desired.

Migration 111 adds nullable ship/corporation personality overrides and
ship-type defaults. It assigns defaults to the existing Orion ship types only
where no default is already configured; replay preserves explicit non-NULL
values. Fresh installs already have these columns in `000_tables.sql`.

Migration 112 creates economy/commerce tables for special-port hardware,
Ferengi traders, market shocks, and planet tax activity, and seeds their cron
tasks. Migration 113 adds the Ferengi roster, cargo, and offer configuration.
Migration 114 adds weapons-worker fighter production state, maps legacy
`colonists_mil` assignments to weapons workers, and initializes class factors.
All three files are transaction-wrapped; 114's guarded column additions,
legacy-field clearing, and nonzero-factor preservation make its data conversion
safe to replay.

These scripts can be replayed after success. Migrations 100–114 use
`ON CONFLICT`, `IF NOT EXISTS`, or guarded updates for replay safety. The
multi-statement PostgreSQL scripts are transaction-wrapped, so `ON_ERROR_STOP`
causes a failed script to leave its transaction uncommitted. Migration 108
adds legacy balances to `entity_stock`, then zeros their old locations in the
same transaction; rerunning after success does not add them again.

## Fresh installation

For a fresh world, configure `bigbang.json`, build the project, and run
`./bin/bigbang` from the repository root. Big Bang loads the SQL files in
numeric filename order, then generates sectors, ports, planets, clusters, and
warps. It applies migration 107 after cluster generation. The current SQL
directory therefore applies 100–114 as part of this fresh-install path.

**Big Bang drops and recreates the `public` schema.** Run it only against a
new, disposable database. Never use it to upgrade an existing game database.
Check its output for successful SQL loading and universe validation before
starting the server or engine.

The repository's fresh-install command is Big Bang's sorted SQL loader plus
world generation, rather than a separate schema-only command. Keep the
release's SQL directory intact: the loader discovers every `.sql` file there.

## Isolated migration fixtures

The test list includes both session-local fixtures and scripts that apply
migrations to the initialized database. In particular, tests 112–114 execute
the migration files, and test 114 updates seeded planet rows to check legacy
assignment conversion and replay. Use only a disposable initialized database
through `TEST_DATABASE_URL`; do not point these tests at a shared or production
database:

```sh
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_pg_migration_100_ship_cargo_upgrade.sql
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_migration_102_porttypes.sql
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_migration_107_illegal_stock.sql
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_migration_108_planet_entity_stock.sql
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_pg_migration_109_sector_notices_trade_offers.sql
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_pg_sector_environmental_hazards.sql
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_pg_migration_111_ship_personalities.sql
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_pg_migration_112_economy_commerce.sql
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_pg_migration_113_ferengi_trader_configuration.sql
psql "$TEST_DATABASE_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_pg_migration_114_planet_fighter_production.sql
```

The fixtures validate schema/data behavior and replay safety; they do not
substitute for a full upgrade rehearsal against a disposable database restored
from a representative backup.

After seeding, run the COL seed assertion against both an isolated fresh
database (after 090 and 091) and an isolated upgrade database (after 091):

```sh
psql "$FRESH_SEED_TEST_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_pg_col_seed_consistency.sql
psql "$UPGRADE_SEED_TEST_URL" -v ON_ERROR_STOP=1 -f tests.v2/test_pg_col_seed_consistency.sql
```

This confirms COL exists and the serial sequence is ahead of all seeded IDs
before migration 100 copies legacy colonist cargo. On the canonical six-good
baseline both seed paths assign COL ID 7; 091 obtains its ID from the sequence
so existing additional commodities cannot be overwritten by a fixed ID.

## Backup, rollback, and lock planning

- Before release, make and restore a custom-format backup using separate
  source and disposable QA connection strings. Never point the restore URL at
  the live database:

  ```sh
  pg_dump --format=custom --file=pre-v2.dump "$SOURCE_DATABASE_URL"
  pg_restore --list pre-v2.dump > pre-v2.contents
  createdb --maintenance-db="$QA_ADMIN_URL" "$QA_DATABASE_NAME"
  pg_restore --exit-on-error --dbname="$QA_DATABASE_URL" pre-v2.dump
  ```

  Confirm the restored database name and contents before applying migrations.
  Record the backup identifier and keep game/engine writes stopped until
  post-migration checks pass.
- These migrations have no down scripts. Per-file transactions prevent a
  partially committed file, but do not reverse migrations that already
  committed. If validation fails after any file commits, keep writes stopped
  and restore the pre-upgrade backup; do not improvise inverse SQL.
- Migration 108 scans and updates planet stock and legacy balance rows in one
  transaction. Migration 107 can seed stock for many ports/clusters. Migration
  102 backfills ports, and migration 109 adds validated foreign keys and
  indexes. Their lock and WAL duration depends on table size, indexes, storage,
  and PostgreSQL version; no fixed duration is safe to promise.
- Rehearse the exact release sequence on a production-shaped copy. Record each
  script's duration, peak WAL/disk use, and blocking time. Ensure free disk for
  WAL and temporary index work. Keep writes disabled during the sequence, then
  verify row counts/balances, constraints, indexes, and application startup
  before reopening traffic.

Run this from a second `psql` session while migrations execute to identify
queries waiting on locks or blocking another session:

```sql
SELECT pid, application_name, now() - query_start AS age, wait_event_type,
       wait_event, pg_blocking_pids(pid) AS blocked_by, left(query, 160) AS query
FROM pg_stat_activity
WHERE datname = current_database()
  AND pid <> pg_backend_pid()
  AND (wait_event_type = 'Lock' OR cardinality(pg_blocking_pids(pid)) > 0)
ORDER BY query_start;
```

An empty result means this query observed no current lock wait; it does not
prove there was no brief wait between samples. Sample throughout each script
and record the largest wait and longest blocking transaction.

There is no migration version/checksum table or automatic runner. Operators
must record the last completed script manually. The PostgreSQL driver is the
only implemented runtime backend; MySQL remains a separate v3 project as
described in `MYSQL_BACKEND_COMPLETION_PLAN.md`.

## Release checklist

1. Pin the exact PostgreSQL and server release versions; confirm MySQL is not
   advertised as a supported runtime.
2. Take a restorable backup and rehearse the ordered 100–114 upgrade on a
   production-shaped clone with game and engine writes disabled.
3. Confirm all scripts finish with `ON_ERROR_STOP=1`; record completion order
   and duration. Do not skip a numbered migration or run `000_tables.sql` on
   an existing database.
4. Verify ship cargo and planet balances against pre-upgrade values, plus the
   migration-specific rows, foreign keys, and indexes. Run the isolated
   migration fixtures listed above.
5. Start the server/engine in a controlled window, inspect startup and database
   logs, then reopen player traffic after smoke checks pass.
6. If any committed migration or smoke check fails, leave writes stopped and
   restore the verified backup. Record the outcome and the first failed step.

## Repeatable disposable application smoke

Run this only in a new disposable database on an isolated PostgreSQL instance.
The focused suite bootstraps with `System`/`BOT`, registers a regular user, and
checks login, `system.hello`, and `player.my_info`. The engine startup probe
also targets player ID 42, so the fixture needs that ID to let the queued
notice finish processing.

Create an empty test database, set `QA_URL` to its connection string, and apply
the fresh schema and seeds in this order. The loop is the sequence used for
the passing smoke rehearsal; it is for an empty QA database, not an upgrade
recipe:

```sh
createdb twclone_qa_smoke
export QA_URL='host=127.0.0.1 dbname=twclone_qa_smoke user=postgres'
for f in \
  sql/pg/000_tables.sql \
  sql/pg/005_namegen.sql \
  sql/pg/010_indexes.sql \
  sql/pg/020_views.sql \
  sql/pg/040_functions.sql \
  sql/pg/088_generate_tunnels.sql \
  sql/pg/090_seed_lookup.sql \
  sql/pg/091_seed_essential.sql \
  sql/pg/091_seed_fedspace_warps.sql \
  sql/pg/092_seed_gameplay.sql \
  sql/pg/094_gameplay_procs.sql \
  sql/pg/095_player_registration.sql \
  sql/pg/100_init_ship_cargo.sql \
  sql/pg/101_phase4_shiptypes_restrictions.sql \
  sql/pg/102_phase5_porttypes.sql \
  sql/pg/103_phase6_items_legality.sql \
  sql/pg/104_phase8_port_behaviour.sql \
  sql/pg/105_phase9_dynamic_pricing.sql \
  sql/pg/106_phase9_2_cluster_pressure.sql \
  sql/pg/107_seed_illegal_commodities.sql \
  sql/pg/108_planet_entity_stock.sql \
  sql/pg/109_v2_sector_notices_trade_offers.sql \
  sql/pg/110_sector_environmental_hazards.sql \
  sql/pg/111_ship_personalities.sql \
  sql/pg/112_v2_economy_commerce.sql \
  sql/pg/113_ferengi_trader_configuration.sql \
  sql/pg/114_planet_fighter_production.sql; do
  psql "$QA_URL" -v ON_ERROR_STOP=1 -f "$f" || exit 1
done
```

The seed order provides the `register_player` function before the `System` /
`BOT` account and other player seeds. For a fresh universe, use Big Bang so
world generation occurs and migration 107 is rerun after cluster generation;
the minimal smoke fixture below only needs a nonempty world that prevents the
server from invoking Big Bang automatically.

Add the minimum startup world and the engine probe's target player to this QA
database. The clean seed path above creates ten players, so the loop assigns
the required fixture player ID 42:

```sh
psql "$QA_URL" -v ON_ERROR_STOP=1 <<'SQL'
INSERT INTO sectors (sector_id, name) VALUES (11, 'QA Smoke Sector');
INSERT INTO sector_warps (from_sector, to_sector) VALUES (1,11), (11,1);
INSERT INTO ports (name, sector_id, type, economy_curve_id)
VALUES ('QA Smoke Port', 1, 1, 1);
INSERT INTO config (key, value, type) VALUES
  ('server_port', '27811', 'int'), ('s2s_port', '27812', 'int'),
  ('turnsperday', '500', 'int'), ('startingcredits', '5000', 'int'),
  ('max_total_planets', '300', 'int')
ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value, type = EXCLUDED.type;
DO $$
DECLARE i integer;
BEGIN
  FOR i IN 1..32 LOOP
    PERFORM register_player('qa_fill_' || i, 'qa', 'QA filler ' || i,
                            FALSE, 1, 2);
  END LOOP;
END $$;
SQL
```

Use an app config that points only at this test database and set a private
S2S key for the local instance. Do not reuse production credentials or ports.

1. Build the server from a clean release checkout so changed DB headers cannot
   leave stale objects:

   ```sh
   make clean
   ./configure
   make -j2 -C bin server
   ```

2. Point a copied server config at the disposable database and use an S2S key
   known only to this clone. Start the
   server and confirm the log shows the server listener, successful engine
   S2S hello/ack, and a database-backed cron entry such as
   `planet_id_sequence_reconcile`.
3. From the repository root, run the focused authenticated smoke suite against
   that listener:

   ```sh
   python3 tests.v2/run_suites.py --host 127.0.0.1 --port "$QA_SERVER_PORT" \
     --suite tests.v2/suite_smoke.json
   ```

   It checks System login/bootstrap, regular-player registration/login,
   `system.hello`, and `player.my_info`. Verify in server logs that no
   migration-related errors occurred. Verify the engine startup probe receives
   an S2S acknowledgment; check `engine_commands` for its completed
   `player:42:hello:001` row and `system_notice` for the corresponding player
   notice.
4. Stop the server and engine cleanly, then discard the clone or preserve it
   with the rehearsal logs. Record build revision, PostgreSQL version, seed
   set, migration timings, and smoke-suite output together.

The engine command probe helped validate the PostgreSQL generated-ID fix:
`db_exec_insert_id` now removes a trailing SQL semicolon before appending
`RETURNING`. In the passing runtime check, a QA-only S2S dispatch correction
was needed because the integrated dispatcher used the C2S schema validator
and rejected `s2s.command.push` before DB access. The repo dispatcher itself
still needs its protocol owner's correction and a retest; do not count the
temporary QA dispatcher change as release validation of the integrated S2S
command path.
