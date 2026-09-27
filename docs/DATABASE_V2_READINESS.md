# v2 database readiness review

## Readiness summary

PostgreSQL is the only implemented runtime backend. Its v2 migration set is
replay-oriented and the multi-statement scripts are transaction-wrapped.
Migration 100 creates `ship_cargo` on older installations before copying
legacy cargo. Fresh seed 090 and upgrade seed 091 both assign `COL` ID 7 on a
canonical six-good baseline. Seed 091 uses the sequence for COL so a database
with additional commodities does not hit a fixed-ID collision. A
disposable PostgreSQL 16.15 rehearsal restored a synthetic pre-migration
backup containing 5,001 ships, applied and replayed migrations 100–109, and
preserved 5,005 colonists. During 102 samples, `pg_locks` showed no ungranted
locks. Per-file apply timings totaled about 31 seconds; replay totaled about
2 seconds. This fixture had no ports or planets, so neither timing nor lock
samples predict their production duration. A production-backup rehearsal
remains required.

The repeatable fresh schema/seed/migration sequence completed in a new
disposable PostgreSQL 16.15 database. Server and engine startup passed; the
S2S hello/ack completed; the database-backed planet-sequence cron ran. The
focused authenticated smoke suite passed System login, test-player
registration/login, `system.hello`, and `player.my_info`.

The S2S command probe exposed a separate current integration blocker. The
unmodified `server_s2s_dispatch` sends S2S messages through the C2S
`schema_validate_payload`; it rejects `s2s.command.push` as “Unknown command
type” before `db_commands_accept` runs. In an isolated QA build only, I routed
that dispatch through the existing `s2s_validate_payload`; then
`db_commands_accept` inserted the command, the engine got an ACK, and the
command completed when the fixture included player ID 42. The latter is needed
because the startup probe's hard-coded notice targets player 42. The PG
generated-ID helper fix compiled and was exercised on this successful insert.
No protocol source was changed in the repository because that handler is
owned by the protocol workstream.

The first manually seeded boot omitted Big Bang's `turnsperday` config key;
the engine's daily turn reset then attempted to write NULL and logged a cron
error. The repeatable fixture recipe now seeds `turnsperday` (plus starter
credits and planet cap) before boot. Planet-sequence reconciliation did run
successfully. The corrected daily-turn configuration was not rerun in this
session, so that cron check remains outstanding; the failure was fixture
configuration, not a migration error.

Synthetic rehearsal measurements (PostgreSQL 16.15, isolated local cluster;
milliseconds; includes one `psql` process per migration):

| Migration | Apply | Replay |
| --- | ---: | ---: |
| 100 | 2615 | 104 |
| 101 | 302 | 93 |
| 102 | 4406 | 396 |
| 103 | 4482 | 232 |
| 104 | 8593 | 85 |
| 105 | 2457 | 115 |
| 106 | 2616 | 92 |
| 107 | 91 | 133 |
| 108 | 434 | 523 |
| 109 | 5183 | 216 |
| **Total** | **31179** | **1989** |

The lock sampler ran 102 times during initial application and observed zero
ungranted locks (maximum 91 total locks in a sample). It cannot rule out waits
between samples. WAL and disk deltas were not measured, so no production
capacity estimate is available from this run.

MySQL schema and migration portability have improved, but its C driver remains
a stub. Treat MySQL as a v3 backend project; do not list it as a v2 runtime
option. See `MYSQL_BACKEND_COMPLETION_PLAN.md` for the driver/API plan and
`MYSQL_V2_MIGRATIONS.md` for the MySQL schema upgrade sequence.

## Completed in this review

- Added missing `CREATE TABLE IF NOT EXISTS ship_cargo` and its lookup index to
  PostgreSQL migration 100. This makes the migration usable when upgrading a
  database that predates the table in `000_tables.sql`.
- Added the internal `COL` commodity to PostgreSQL seeds 090/091. The first
  integrated run exposed migration 100's legacy colonist copy violating the
  `ship_cargo.commodity_code` foreign key; seed 091 now also advances the
  serial sequence after its explicit seed IDs and obtains COL's ID from the
  sequence. Seed 090 places COL after the six canonical commodities so its
  fresh-install ID equals 7.
- Added `test_pg_col_seed_consistency.sql`, a post-seed assertion for the COL
  row and sequence contract. Fresh, legacy-upgrade, and custom-ID collision
  paths passed against the final seed logic; the custom commodity retained ID
  7 and COL received ID 8 without losing cargo during migration 100.
- Added a separate PostgreSQL temporary-table test for the missing-table
  upgrade case, foreign keys, index, data copy, and replay.
- Wrapped PostgreSQL migrations 101 and 103–107 in transactions. Migrations
  100, 102, and 108 already have explicit transaction boundaries in the
  current worktree.
- Added `POSTGRESQL_V2_UPGRADE.md` with the existing-database order, fresh
  install dependencies, and isolated fixture commands.
- Documented Big Bang's destructive fresh-install behavior, ordered 100–109
  commands, backup/restore rollback, lock/WAL risks, and a release checklist.
- Fixed PostgreSQL `db_exec_insert_id` SQL assembly to place generated-ID
  `RETURNING` before a trailing semicolon. It compiled in an isolated build
  and its DB insert path succeeded during runtime validation with a QA-only
  S2S dispatcher correction.
- Added a repeatable disposable app smoke procedure with exact schema/seed/
  migration order, minimal QA world rows, server/engine startup checks, and
  authenticated `suite_smoke.json` run.

## Remaining v2 release blockers

1. **Representative backup rehearsal.** Restore a sanitized, representative
   pre-v2 production backup into an isolated environment and repeat 100–109.
   No representative pre-v2 backup was available. The synthetic backup/restore
   rehearsal passed, but the combined fixture had no ports or planets and does
   not establish preservation of representative port, planet, cluster, or
   balance data. Its timings cannot be used for production planning. An earlier
   disposable fixture exercised port, planet, and cluster data checks, but it
   was not a pre-migration production-shaped backup.
2. **S2S command dispatch integration.** The unmodified dispatcher uses the
   C2S schema validator and rejects `s2s.command.push` before DB access. The
   database insert/ACK path passed only with a QA-only dispatcher correction;
   the integrated protocol handler must be corrected and retested by its
   owner before the engine command path is release-ready.
3. **Fresh install is destructive.** Big Bang is the documented fresh-install
   path and drops/recreates `public` before loading the sorted SQL directory.
   Require an empty disposable database, a pinned release SQL directory, and
   confirmation of the generated universe before starting the server.
4. **No tracked migration state.** Operators must record completion manually;
   the repository has no migration version/checksum table or runner. This is
   operational risk for release and is a priority v3 platform task.
5. **Changes need integration before release.** The current worktree contains
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

The PostgreSQL migration fixtures cover migrations 100, 102, 107, 108, and
109 in temporary schemas/tables. The combined 100–109 chain and replay have
passed on a restored synthetic backup. The COL seed assertion passed on fresh,
legacy-upgrade, and custom-ID fixtures. Add isolated tests for 101, 103–106
and retain the combined upgrade/replay rehearsal in CI. Add DB API contract tests for
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
