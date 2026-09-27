# MySQL/MariaDB backend validation

This opt-in suite tests only the database driver through the public DB API. It
does not run game migrations, universe creation, or gameplay SQL. Use a local
throwaway database; do not point it at a development or production game DB.

## Dependencies

On Debian or Ubuntu, install one client development package:

```sh
sudo apt install build-essential autoconf automake libtool \
  libmariadb-dev mariadb-server
```

For Oracle MySQL, use `libmysqlclient-dev` and `mysql-server` instead. Other
distributions need the equivalent C client development package and a MySQL or
MariaDB server. The development package must provide `mysql_config` or
`mariadb_config`; the test runner uses it for compiler and linker flags. A
running server is needed only for integration tests, not for configure/build.

## MySQL-enabled build

From the repository root:

```sh
autoreconf -fi                 # only needed if configure is absent/stale
./configure --enable-postgres --enable-mysql
make -C bin server bigbang
```

The MySQL option is opt-in. Configure accepts `MYSQL_CONFIG=/path/to/mysql_config`
or `MYSQL_CONFIG=/path/to/mariadb_config` if the tool is not on `PATH`. To
build only the MySQL driver test without changing the regular build:

```sh
MYSQL_CONFIG=/path/to/mariadb_config tests/mysql/run.sh
```

The ordinary runtime selector is `db_config_t.backend = DB_BACKEND_MYSQL`.
Provide `mysql_host`, `mysql_user`, `mysql_password`, `mysql_database`, and
optionally `mysql_port` or `mysql_unix_socket`; zero port and null socket use
client defaults. The current game-server startup configuration still selects
PostgreSQL and does not yet expose MySQL settings. The driver initializes
sessions to UTC and `utf8mb4`.

## Disposable integration database

Create a database and a dedicated account on a local test server. Example for
MariaDB/MySQL using an administrative local account (adjust host and password
for your server):

```sql
CREATE DATABASE twclone_mysql_test CHARACTER SET utf8mb4;
CREATE USER 'twclone_test'@'127.0.0.1' IDENTIFIED BY 'replace-this-password';
GRANT ALL PRIVILEGES ON twclone_mysql_test.* TO 'twclone_test'@'127.0.0.1';
```

The suite creates a connection-local temporary table, so it does not leave
test tables behind. Export the connection settings and run it:

```sh
export MYSQL_TEST_HOST=127.0.0.1
export MYSQL_TEST_USER=twclone_test
export MYSQL_TEST_PASSWORD='replace-this-password'
export MYSQL_TEST_DATABASE=twclone_mysql_test
export MYSQL_TEST_PORT=3306          # optional
# MYSQL_TEST_SOCKET=/run/mysqld/mysqld.sock  # optional; takes precedence in client connection
tests/mysql/run.sh
```

The runner exits `77` with a `SKIP` reason if client development tools or
required connection settings are missing. With settings present, connection,
compile, or assertion failures are failures (nonzero status), not skips.
`MYSQL_CONFIG` can select a nonstandard `mysql_config`/`mariadb_config` path.

## Covered behavior

The C integration test covers connection selection, placeholder binding,
typed result reading, nulls, insert IDs, transaction commit, and rollback. It
does not certify either server family/version until run against that server in
CI or a dedicated validation environment.
