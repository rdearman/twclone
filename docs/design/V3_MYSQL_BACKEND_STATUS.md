# V3 MySQL Backend Readiness and Event Journal Compatibility

Status date: 2026-09-29

## Release status

MySQL is not yet a supported game-server backend. The C driver is present and
has a low-level integration suite, and this work adds bootstrap configuration
and per-thread connection selection. The application cannot yet run its full
repository and migration workload on MySQL.

No server version is certified. The installed client tooling reports MySQL
8.0.46, but there is no reachable server in this environment. Once runtime
support is implemented, MySQL 8.0 should be the first certification target;
MariaDB compatibility must be tested and declared separately.

## What currently works

- `src/db/mysql/db_mysql.c` implements connection lifecycle, `$N` parameter
  rewriting, bound parameters, transactions, query result accessors, affected
  row counts, and generated insert IDs.
- The driver sets the connection timezone to UTC and character set to
  `utf8mb4`.
- `tests/mysql` exercises the generic DB API with a temporary table. It covers
  binds, result accessors, generated IDs, commit, and rollback. It does not
  exercise the game schema or game repositories.
- `db_config_t` selects a backend. Game bootstrap now also has a `backend`
  selector and MySQL connection settings; PostgreSQL remains the default.

Example MySQL fields in `bin/bigbang.json`:

```json
{
  "backend": "mysql",
  "db": "twclone",
  "mysql_host": "127.0.0.1",
  "mysql_port": 3306,
  "mysql_user": "twclone",
  "mysql_password": "set-through-a-secret-manager",
  "mysql_unix_socket": ""
}
```

Do not put deployable credentials into a tracked configuration file. The
current bootstrap parser reads these settings from JSON; environment-backed
secret loading still needs a deliberate configuration contract.

## Server startup blockers

- The main DB wrapper selects MySQL connections, but the normal server path
  subsequently invokes PostgreSQL-oriented repository SQL.
- Universe creation (`bigbang`) has not been converted to a backend-neutral
  path. MySQL startup must use a schema that has already been provisioned and
  seeded by an external process until that work is completed.
- Startup-level tests that prove a MySQL server can boot and serve commands
  have not been run.

## Repository and DB abstraction gaps

The abstraction handles core operations, but some operations remain backend
specific. MySQL's `db_exec_returning` rejects PostgreSQL `RETURNING`, and
`db_ship_repair_atomic` is explicitly unimplemented. Repository SQL also uses
PostgreSQL syntax, including `ON CONFLICT`, `RETURNING`, casts (`::type`),
`DISTINCT ON`, `FILTER`, `ILIKE`, data-modifying CTEs, PostgreSQL timestamp
functions, and `FOR UPDATE SKIP LOCKED`. A source scan found these patterns in
multiple repository files. They need focused query rewrites or narrowly scoped
dialect helpers plus backend conformance tests; a global string-rewrite layer
would be unsafe.

## Schema and migration gaps

- The MySQL SQL tree currently ends at migration 108. PostgreSQL has later V2
  migrations and new V3 migrations, so feature parity is incomplete.
- There is no shared migration runner/version ledger. Fresh schema setup,
  ordered migration application, and replay tracking are manual SQL workflows.
- `sql/my/000_tables.sql` and its seeds have not been validated as a complete
  server schema against a disposable MySQL server in this environment.

## Player event journal compatibility

The current player event journal work is being developed in shared files by
another agent, so this task does not edit its repository implementation or
PostgreSQL migration. The current PostgreSQL design uses UUID identifiers,
`jsonb`, PostgreSQL casts, `ON CONFLICT`, timestamp formatting, and
`FOR UPDATE SKIP LOCKED`. Those details are not directly portable.

Before declaring the journal cross-backend, agree on these mappings and test
the same invariants on both databases:

| Concern | PostgreSQL form | MySQL-compatible form to decide/test |
| --- | --- | --- |
| Stream/event IDs | `uuid` | `CHAR(36)` or `BINARY(16)` with matching repository representation |
| Event payload | `jsonb` | MySQL `JSON`, with result access returned in the repository's expected text form |
| Conflict-safe stream creation | `ON CONFLICT` | MySQL duplicate-key handling with identical idempotency semantics |
| Atomic claim/retention | `FOR UPDATE SKIP LOCKED` | MySQL 8 locking behavior and transaction isolation, verified under concurrency |
| UTC timestamps | `timestamptz` / timezone formatting | `DATETIME(6)` stored as UTC or a tested `TIMESTAMP` policy |
| Replay cursor | PostgreSQL casts and sequence constraints | portable integer/cursor binding with the same ordering and pruning guarantees |

The repository should continue to expose one event-journal contract. Dialect
differences belong in that repository's SQL branches or explicit DB helpers,
not in player event transport/protocol behavior.

## Required verification before support is declared

1. Build with `--enable-postgres --enable-mysql` and run the existing driver
   integration test against a disposable MySQL 8.0 server.
2. Bring `sql/my` to the same supported migration endpoint as PostgreSQL and
   test fresh install, upgrade, and replay against a disposable database.
3. Run repository conformance tests for common operations, transactions,
   generated IDs, error mapping, locking/idempotency, and result types.
4. Start server and engine with MySQL, prove DB reconnect behavior, and run a
   small authenticated gameplay smoke path.
5. Add cross-backend player event journal tests for append deduplication,
   monotonic sequence assignment, replay pagination, pruning watermarks, and
   concurrent consumer claims.

## Validation in this environment

- `mysql_config --version`: MySQL client tools report `8.0.46`.
- `mysqladmin ping`: blocked; no local MySQL server socket is available.
- `docker version --format '{{.Server.Version}}'`: blocked by denied access to
  the Docker API socket.
- `tests/mysql/run.sh`: skipped because disposable connection settings are
  absent; the runner does not claim server integration coverage in that case.
- C syntax-only compilation of the MySQL driver and changed startup sources
  passed. This is not a full link or runtime test.

