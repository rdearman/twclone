# V3 MySQL backend: preparation and validation guide

Status: the optional driver implementation and isolated integration test are
implemented and validated at the DB API level. Full game-server MySQL runtime
support remains deferred until server startup exposes MySQL configuration and
the game SQL surface is portable.

## Dependencies and build

The enabled build needs a C compiler, Autoconf/Automake, and either Oracle's
MySQL client development package (`libmysqlclient-dev` on Debian/Ubuntu) or
MariaDB Connector/C development package (`libmariadb-dev`). The package must
provide `mysql_config` or `mariadb_config`; the build also needs the matching
client library. A database server is required for integration tests only.
See [the MySQL test README](../tests/mysql/README.md) for Debian/Ubuntu
installation, disposable database setup, and test environment variables.

From the repository root:

```sh
autoreconf -fi                 # if configure is absent or stale
./configure --enable-postgres --enable-mysql
make -C bin server bigbang
```

Use `MYSQL_CONFIG=/path/to/mysql_config` (or `mariadb_config`) if the helper
is not on `PATH`. Omitting `--enable-mysql` deliberately builds the disabled
stub and does not validate MySQL client code. The current game-server runtime
configuration remains PostgreSQL-only; the driver API is selected directly
through `db_config_t.backend = DB_BACKEND_MYSQL` and the `mysql_host`,
`mysql_user`, `mysql_password`, `mysql_database`, `mysql_port`, and
`mysql_unix_socket` fields.

Run the disposable integration suite with:

```sh
MYSQL_TEST_HOST=127.0.0.1 \
MYSQL_TEST_USER=twclone_test \
MYSQL_TEST_PASSWORD='local-test-password' \
MYSQL_TEST_DATABASE=twclone_mysql_test \
tests/mysql/run.sh
```

The runner exits 77 with a reason when client development tools or required
test connection settings are absent. A configured but unreachable server,
failed compile, or failed assertion is a real test failure. The suite uses a
connection-local temporary table and must only use a disposable test DB.

## Driver contract and implementation

The DB abstraction requires connection lifecycle, transactions, parameterized
exec/query, affected-row and insert-ID results, result iteration and typed
column access, error translation, and the internal `ship_repair_atomic`
operation. The MySQL implementation uses native prepared statements, translates
the API's `$N` positional markers to MySQL `?` markers, and supports text,
numeric, boolean, JSON-as-text, blob, and timestamp binds. Session timezone is
set to UTC and character set to `utf8mb4`.

## Known API gaps and semantics

- **`ship_repair_atomic`:** currently returns `ERR_NOT_IMPLEMENTED`. The
  PostgreSQL implementation depends on a data-modifying CTE and `RETURNING`.
  A MySQL implementation needs a reviewed, transaction-safe equivalent with
  row locking and matching error behavior; this remains deliberately deferred.
- **`exec_returning`:** MySQL does not accept PostgreSQL `INSERT/UPDATE ...
  RETURNING`. The driver can run a query that is already valid MySQL and
  `exec_insert_id` reads the auto-increment ID. It does not rewrite arbitrary
  PostgreSQL `RETURNING` statements or emulate their returned columns.
- **Transactions:** public `db_tx_*` tracks nesting in the shared DB wrapper,
  issuing backend begin/commit only at the outer boundary and rollback for the
  whole transaction. MySQL maps this to `START TRANSACTION`, `COMMIT`, and
  `ROLLBACK`; `DB_TX_IMMEDIATE` is ignored because MySQL has no matching
  SQLite-style begin mode. Savepoints/nested independent rollback are not
  provided by the public wrapper.
- **Results:** prepared-statement results are buffered with
  `mysql_stmt_store_result`; fetched text/blob memory is driver-owned until the
  next step or finalize. `db_res_cancel` is currently a no-op, like the
  PostgreSQL implementation; it does not interrupt a running query. Error
  handling for row fetch and truncation is implemented but needs live tests.
- **Portability beyond the driver:** server startup does not yet expose MySQL
  connection settings. Repository SQL, schema, migrations, JSON operations,
  conflict handling, casts, and CTEs have not been converted or validated for
  MySQL. No game-server-on-MySQL claim should be made from driver tests alone.
- **Process model:** child-process close behavior currently has a MariaDB
  socket-close path. Oracle MySQL client behavior and the server's fork/thread
  lifecycle need dedicated verification before production use.

PostgreSQL behavior and v2 migration/gameplay SQL are outside this V3
preparation work and remain unchanged by this guide and its isolated tests.
