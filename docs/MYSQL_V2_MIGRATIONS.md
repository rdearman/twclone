# MySQL v2 migrations

MySQL support requires MySQL 8.0.16 or newer, InnoDB tables, and a client that
supports `DELIMITER` for the stored procedures in migrations 098 and 099.
Do not run migrations against a shared database during validation.

## Fresh database setup

1. Create an empty database and apply `sql/my/000_tables.sql`.
2. Apply `sql/my/091_seed_essential.sql` for core lookup and economy data. It
   includes rows also present in `090_seed_lookup.sql`; do not apply both.
3. Generate the initial sectors, then apply
   `sql/my/091_seed_fedspace_warps.sql` once sectors 1–10 exist.
4. Apply `sql/my/092_seed_gameplay.sql` and `sql/my/093_seed_cron.sql`.
5. Apply migrations 101–106 in numeric order. After clusters and their sector
   assignments exist, apply 107. Apply 108 after planets have been populated.

Fresh installs already have session timestamps, normalized key columns, and
`ship_cargo` from the base schema, so skip 098, 099, and 100. MySQL seed 092
installs replayable triggers; it is not the PostgreSQL PL/pgSQL file.

## Existing database upgrade

Use a restorable backup and a disposable clone first. Stop game writes during
the migration window. Run scripts from the repository root with the `mysql`
command-line client, in this order:

1. `sql/my/098_migrate_sessions_expires_to_timestamp.sql` (skip if the column
   is already a `TIMESTAMP`).
2. `sql/my/099_mysql_key_portability.sql`.
3. `sql/my/100_init_ship_cargo.sql`.
4. `sql/my/101_phase4_shiptypes_restrictions.sql`.
5. `sql/my/102_phase5_porttypes.sql`.
6. `sql/my/103_phase6_items_legality.sql`.
7. `sql/my/104_phase8_port_behaviour.sql`.
8. `sql/my/105_phase9_dynamic_pricing.sql`.
9. `sql/my/106_phase9_2_cluster_pressure.sql`.
10. Populate `cluster_sectors`, then run `sql/my/107_seed_illegal_commodities.sql`.
11. `sql/my/108_planet_entity_stock.sql`.

Example: `mysql --database=twclone < sql/my/099_mysql_key_portability.sql`.
Migration 099 checks key lengths before narrowing, then repairs key types and
restores foreign keys. It stops on overlength key values or orphan references;
inspect the error and source data before retrying. MySQL DDL commits
independently, so leave the game offline until every migration completes.
Migration 100 now creates `ship_cargo` if absent before copying legacy cargo.

Fresh installs should use the corrected `sql/my/000_tables.sql`; they do not
need migration 099. Follow the release seed-data instructions for the other
migrations. There is no automated runner that chooses fresh-install versus
upgrade scripts.

## Timestamp limit

MySQL `TIMESTAMP` supports 1970 through 2038, unlike PostgreSQL `timestamptz`.
Migration 098 converts epoch seconds into a staging `TIMESTAMP` and aborts
without dropping the source if any value is null or outside MySQL's range.
Check the existing expiry values before deployment. Values beyond that range
remain a backend limitation until the session-expiry representation is changed
consistently in the MySQL schema and database driver.

## Portability decisions

Indexed and referenced MySQL text fields use bounded `VARCHAR` columns with
`utf8mb4_bin`, retaining case-sensitive key comparisons. Long unindexed text
remains `TEXT`. MySQL-only schema and migration changes leave PostgreSQL SQL
unchanged. MySQL accepts but ignores inline `REFERENCES`, so the corrected base
schema and migration 099 declare those relationships as table-level foreign
keys. Migration 105 uses `BIGINT` for `port_id`, matching `ports.port_id`, and
commodity key columns use the same bounded type and collation as
`commodities.code`.

## Runtime support status

Passing schema and migration checks does not currently make the game server
usable with MySQL. `src/db/mysql/db_mysql.c` still returns “not implemented” for
core transactions, queries, and result access. The MySQL driver must be
completed before claiming end-to-end MySQL backend support.
