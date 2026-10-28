# V3 database backend compatibility findings

Status reflects the source tree inspected on 2026-10-28. PostgreSQL remains the
qualified production backend. MySQL/MariaDB have driver and startup paths, but
must be qualified against the combined release tree. SQL Server is not an
implemented backend.

## Concrete backend state

| Area | PostgreSQL | MySQL/MariaDB | Microsoft SQL Server |
| --- | --- | --- | --- |
| Backend enum and `db_open()` | Implemented | Implemented behind `HAVE_MYSQL` | No backend enum, vtable or open hook |
| Server selection/configuration | Supported | Config supports `mysql` and `mariadb`; requires MySQL build and credentials | Config explicitly rejects it |
| Schema and migrations | Canonical `sql/pg` path | Separate `sql/my` path; feature parity remains release-dependent | No schema/migration tree |
| Repository SQL | Native PostgreSQL syntax | Requires dialect branches/helpers and MySQL schema parity | No T-SQL dialect or repository adaptation |
| Parameter markers | `$N` | `sql_build()` emits ordinal markers which the MySQL prepared-statement layer remaps | Not implemented |
| Transactions/locking | PostgreSQL semantics | MySQL transactions and `FOR UPDATE` helpers exist; transaction behavior must be checked per repository operation | No locking or transaction adapter |
| Native types | `timestamptz`, JSONB, sequences/identity | Native temporal/JSON/auto-increment support in schema and driver | No type mapping or generated-ID strategy |
| Test/conformance suite | PostgreSQL repository integration scripts | MySQL integration suite exists; full release matrix still needed | None |

## Verified incompatibilities and risks

1. `DB_BACKEND_ORACLE` remains in `db_api.h`, and some generic SQL helper
   comments/functions mention Oracle, but `db_open()` has no Oracle case. This
   enum value cannot open a connection and should not be represented as
   supported. SQL Server has no corresponding enum value at all.
2. The source currently supports only PostgreSQL and conditionally compiled
   MySQL/MariaDB startup. The `--enable-mysql` build is a hard prerequisite for
   MySQL selection. No SQL Server configuration or driver exists.
3. The repository layer is not uniformly dialect-neutral. PostgreSQL-only
   `ON CONFLICT`, `RETURNING`, casts, sequences, JSON constructors and
   `FOR UPDATE OF` remain in repository/SQL paths. MySQL compatibility depends
   on explicit helper use and alternate migration/view/function definitions;
   the helpers do not translate arbitrary repository SQL.
4. `sql_driver.h` still describes SQLite placeholder behavior and generic
   non-PostgreSQL `?` output. SQLite is prohibited for twclone and is not a
   selectable `db_open()` backend. This documentation is historical, not a
   supported test or development target.
5. The DB vtable exposes a small common primitive set. It does not itself
   guarantee equivalent isolation, row-lock scope, affected-row semantics,
   generated identifiers, JSON conversion, or transaction nesting. These
   properties need repository-level conformance tests for every operation that
   claims multi-backend support.

## Recommended scoped follow-ups

- SQL Server backend: separate driver/configuration/schema/migration project;
  begin with a concrete version target, native type mapping, generated IDs,
  transaction/locking semantics and core auth/player conformance. Do not add
  SQL Server fragments to generic helpers before the driver exists.
- Backend conformance: define a reusable operation-level suite for transaction
  rollback, affected rows, nullable values, generated IDs, parameter reuse,
  timestamps/JSON, lock conflicts and persistence after reconnect; run against
  disposable PostgreSQL and MySQL instances.
- MySQL release qualification: rerun schema install, migration replay, server
  startup and representative gameplay after all repository changes are merged.

## Conformance test starter

`tests/pg/run_db_pg_conformance.sh` builds and runs an isolated PostgreSQL
driver test. It checks typed large-integer/text reads, SQL NULL handling,
reused and sparse numbered parameters, native timestamp/JSON text reads,
generated IDs, affected-row counts, transaction commit/rollback, a
two-connection row-lock timeout, and persistence after reconnect. It requires
`PGDATABASE` to match `twclone_rc2_*` or `twclone_qa_*`; the test creates and
drops a PID-named QA table inside that isolated database so it can verify data
persistence after reconnect.

Native PostgreSQL timestamp and JSON values currently pass through the common
text accessor. A normalized cross-backend representation still needs explicit
agreement.

The PostgreSQL harness compiled and linked in this workspace, but its runtime
was blocked because no isolated `PGDATABASE` was supplied. This is not live
database validation. The existing MySQL driver test in `tests/mysql/run.sh`
currently covers basic bindings, typed reads, generated IDs and commit/rollback;
the two backend tests still need to be reconciled and expanded to cover the
full issue #494 matrix, including native timestamp/JSON handling and lock
behavior on each supported MySQL/MariaDB version.

## Scope note

SQLite is permanently out of scope and must not be used for production,
development, tests, CI or temporary databases. PostgreSQL behavior remains the
baseline and must not be changed merely to simplify another dialect.
