# v2 database readiness review

## Readiness summary

PostgreSQL is the only implemented runtime backend. Its v2 migration set is
replay-oriented and the multi-statement scripts are now transaction-wrapped.
Migration 100 now creates `ship_cargo` on older installations before copying
legacy cargo. These changes still need a full rehearsal against a disposable
PostgreSQL clone before release.

MySQL schema and migration portability have improved, but its C driver remains
a stub. Treat MySQL as a v3 backend project; do not list it as a v2 runtime
option. See `MYSQL_BACKEND_COMPLETION_PLAN.md` for the driver/API plan and
`MYSQL_V2_MIGRATIONS.md` for the MySQL schema upgrade sequence.

## Completed in this review

- Added missing `CREATE TABLE IF NOT EXISTS ship_cargo` and its lookup index to
  PostgreSQL migration 100. This makes the migration usable when upgrading a
  database that predates the table in `000_tables.sql`.
- Added a separate PostgreSQL temporary-table test for the missing-table
  upgrade case, foreign keys, index, data copy, and replay.
- Wrapped PostgreSQL migrations 101 and 103–107 in transactions. Migrations
  100, 102, and 108 already have explicit transaction boundaries in the
  current worktree.
- Added `POSTGRESQL_V2_UPGRADE.md` with the existing-database order, fresh
  install dependencies, and isolated fixture commands.

## Remaining v2 release blockers

1. **No complete migration rehearsal.** Run the PostgreSQL fixtures and a full
   100–108 upgrade on a disposable database restored from a representative
   older backup. Include a second application of the chain, preserved balances,
   foreign keys, and indexes. Do not substitute static review for this.
2. **Fresh-install recipe is incomplete.** The repository has no single,
   release-owned command that enumerates the exact seed files and world
   generation steps. The PostgreSQL guide calls out the dependencies, but the
   release package still needs to pin the seed set and ordering.
3. **No tracked migration state.** Operators must record completion manually;
   the repository has no migration version/checksum table or runner. This is
   operational risk for release and is a priority v3 platform task.
4. **Changes need integration before release.** The current worktree contains
   edits from multiple workers. Review and merge each migration/test change,
   then validate the resulting clean commit rather than deploying directly
   from a shared dirty worktree.

## DB abstraction audit

The PostgreSQL driver supports the currently common signed integer, boolean,
text/JSON, timestamp, query, transaction, and generated-ID paths. The declared
abstraction API is broader than the implementation:

- `DB_BIND_U64`, `DB_BIND_U32`, and `DB_BIND_BLOB` are not converted by
  `pg_bind_param_to_string`; `db_bind_text_n` length is also ignored. These
  types currently have no application call sites, so this is API conformance
  debt rather than an observed v2 gameplay failure.
- The PostgreSQL vtable does not provide unsigned or blob result accessors.
  The public wrappers report a missing callback as `ERR_DB_CLOSED`, which
  misstates the failure. These accessors also have no current application
  call sites.
- `pg_res_col_text` does not test SQL NULL before returning `PQgetvalue`,
  although the public contract says SQL NULL returns `NULL`. Numeric accessors
  use `atoi`/`atoll`/`atof` without range or parse checks despite the API
  promising `DB_ERR_TYPE` on conversion failure.
- PostgreSQL error mapping recognizes deadlocks but does not map SQLSTATE
  `40001` to `DB_ERR_CAT_SERIALIZATION`; connection-class SQLSTATEs returned
  with a query result also need consistent connection-category mapping.
- `db_exec_insert_id` assumes a successful `RETURNING` query has at least one
  row before reading row zero. Current inspected insert call sites are plain
  inserts, but the API needs an explicit zero-row contract and a regression
  test.
- Transaction nesting is a flat nesting counter, not savepoints: only the
  outer begin/commit reaches PostgreSQL, while any nested rollback rolls back
  the entire transaction. `DB_TX_IMMEDIATE` is ignored by PostgreSQL. Document
  this contract and test it before adding another backend.
- The public header describes `$1` placeholders, while most repository calls
  use `{1}` templates rendered by `sql_build()`. Clarify the public convention
  without changing existing PostgreSQL call sites.

These issues do not currently block the PostgreSQL game paths found in the
source search, but the API must not be advertised as fully backend-neutral
until tests define the supported types, NULL behavior, overflow behavior,
transaction nesting, and generated-ID outcomes.

## Test coverage gaps

The current PostgreSQL migration fixtures cover migrations 100, 102, 107,
and 108 in temporary schemas/tables. Add isolated tests for 101, 103–106 and
one combined clean-install/upgrade/replay path. Add DB API contract tests for
the gaps above, using a disposable PostgreSQL service in CI. MySQL needs its
own disposable service and conformance job after its driver project is
implemented.

## v3 roadmap

1. **Migration metadata and runner:** store ordered migration IDs and applied
   checksums per backend; reject drift; support status, dry-run, preflight,
   and explicit `fresh`/`upgrade` modes.
2. **Upgrade automation:** define backend-specific dependency manifests,
   stop-on-error execution, resumable step state for MySQL DDL, backup/restore
   guidance, and post-migration schema/data assertions.
3. **Conformance framework:** run one DB API test suite against disposable
   PostgreSQL and MySQL services, covering binding, conversion, transaction,
   result lifetime, generated IDs, and backend error classification.
4. **MySQL runtime driver:** implement the MySQL connector only after the
   configuration and placeholder contracts are settled; gate release on the
   conformance suite plus application repository smoke tests.
5. **Abstraction cleanup:** make backend-neutral connection settings explicit,
   preserve parameter indexes through dialect rendering, define write-return
   behavior, and align result/error semantics without changing PostgreSQL
   behavior unexpectedly.
