# V3 Local Integration QA Environment

This procedure uses the existing `tests.v2` JSON runner and rig. It does not
create a second test framework. The database, PostgreSQL data directory, server
configuration, logs, and response capture are all under one disposable `/tmp`
directory.

## Current isolated environment

- PostgreSQL: 16.15, loopback only at `127.0.0.1:55438`.
- Database: `twclone_v3qa_20261028_c2`.
- PostgreSQL data directory: `/tmp/twclone-v3-qa-20261028-c2/pgdata`.
- BigBang config: `/tmp/twclone-v3-qa-20261028-c2/bigbang.json`; it points to
  the above database and canonical `sql/pg/`, with V3 migrations explicitly
  enabled.
- Generated universe: 680 sectors and 185 ports.
- Game endpoint for clients on this machine: `127.0.0.1:12483`.
- S2S endpoint: `127.0.0.1:43218`; the server forks its engine child.
- Response evidence: `/tmp/twclone-v3-qa-20261028-c2/responses.jsonl`.
- PostgreSQL, game server, and engine are separate from the development/live
  database. No connection to database `twclone` was used.

PostgreSQL listens only on loopback. The current game server implementation
binds its listener to `0.0.0.0`; the supported test endpoint is loopback on the
same machine. Do not expose port 12483 outside the host. The S2S listener is
loopback-only.

## Start

PostgreSQL was initialized once in the dedicated data directory. To start it:

```sh
/usr/lib/postgresql/16/bin/pg_ctl \
  -D /tmp/twclone-v3-qa-20261028-c2/pgdata \
  -l /tmp/twclone-v3-qa-20261028-c2/postgres.log \
  -o "-c listen_addresses=127.0.0.1 -c unix_socket_directories='' -p 55438" \
  start
```

The checked-in binary `bin/bigbang` uses the temporary `bigbang.json` in its
working directory. It applies the canonical SQL directory, includes V3
migrations 115 and 116 explicitly, and generates the universe:

```sh
cd /tmp/twclone-v3-qa-20261028-c2
/home/rick/Development/Active/twclone/bin/bigbang
```

After BigBang, configure the dedicated listener ports in this database:

```sh
psql -h 127.0.0.1 -p 55438 -U rick -d twclone_v3qa_20261028_c2 \
  -v ON_ERROR_STOP=1 -c \
  "INSERT INTO config(key,value,type) VALUES ('server_port','12483','int'),('s2s_port','43218','int') ON CONFLICT (key) DO UPDATE SET value=EXCLUDED.value,type=EXCLUDED.type"
```

Load the existing integration fixture without resetting the database:

```sh
cd /home/rick/Development/Active/twclone
python3 tests.v2/rig_db.py --config tests.v2/test_rig.json \
  --dbname twclone_v3qa_20261028_c2 --host 127.0.0.1 --port 55438 --user rick
```

The fixture helper now requires an explicit QA database name and loopback host;
it refuses the old implicit `twclone` target. Do not pass `--reset` unless the
database name and endpoint have been verified as this disposable environment.

Start the server from its isolated working directory so it reads that
directory’s bootstrap config and writes logs there. The server forks the engine:

```sh
cd /tmp/twclone-v3-qa-20261028-c2
/home/rick/Development/Active/twclone/bin/server
```

Check the log for `Listening on 0.0.0.0:12483`, `s2s listen on 127.0.0.1:43218`,
engine `hello send`, server `accepted hello`, and server `ack send` before tests.

## Run focused suites

Both the aggregate runner and standalone JSON runner accept host and port
arguments. The existing defaults remain `localhost:1234`; set
`TWCLONE_TEST_HOST` and `TWCLONE_TEST_PORT` to configure defaults for scripted
runs. `--capture-responses` appends JSONL response records and redacts session
tokens.

```sh
cd /home/rick/Development/Active/twclone
umask 077
python3 tests.v2/run_suites.py --host 127.0.0.1 --port 12483 \
  --capture-responses /tmp/twclone-v3-qa-20261028-c2/responses.jsonl \
  --suite tests.v2/suite_port_fixed_modes.json
python3 tests.v2/run_suites.py --host 127.0.0.1 --port 12483 \
  --capture-responses /tmp/twclone-v3-qa-20261028-c2/responses.jsonl \
  --suite tests.v2/suite_information_fog.json
```

The suites use public `auth.register`/`auth.login` for their regular test
identities and `System`/`BOT` only for the suite’s authorized test-fixture SQL.
Fixture accounts and game state live only in this QA database.

## QA account procedure for CODEX #4

Use the game endpoint `127.0.0.1:12483` from this host. Register a fresh,
uniquely named ordinary account through the public API:

```json
{"command":"auth.register","data":{"username":"<unique-qa-name>","passwd":"<new-random-password>"}}
```

Then authenticate through `auth.login` using the same fields. Store the QA
password in a mode-0600 private configuration outside the repository. Do not
use `System`/`BOT` for AI acceptance. Recreate the QA database to remove test
accounts after the acceptance window; do not insert or repair player rows
directly for the AI test.

The endpoint is intentionally loopback-addressed for a client on this host;
the game listener currently binds all interfaces, so do not publish or forward
port 12483 to another network.

## Reset and cleanup

Stop the server first with `SIGTERM` to the PID found from the listener on
12483. Confirm both server and engine child exit. Then stop only this PostgreSQL
cluster:

```sh
/usr/lib/postgresql/16/bin/pg_ctl \
  -D /tmp/twclone-v3-qa-20261028-c2/pgdata stop -m fast
```

To reset the universe, verify the database name and port, start PostgreSQL,
drop only `twclone_v3qa_20261028_c2`, then rerun BigBang and the fixture load
above. This clears QA accounts, world state, notices, and logs tied to the
database. Do not use this command against any other database.

To remove the environment after all acceptance work is complete, stop the game
server and this PostgreSQL cluster, verify the exact path, then remove only
`/tmp/twclone-v3-qa-20261028-c2`.

## Verification record

On 2026-10-28, both suites passed against the isolated database and live server:

- `suite_port_fixed_modes.json`: **PASS**. Rejects wrong-direction sales at
  zero and positive stock without changing player/port state; a valid port
  purchase consumes one turn and the configured `sell` mode remains unchanged.
- `suite_information_fog.json`: **PASS**. Verifies current and direct outgoing
  adjacency access, incoming-only refusal, scan output without port class,
  remote port/planet refusal, authorized landed planet details, no knowledge
  writes on denied requests, and pathfind refusal through an undiscovered
  intermediate sector.
- The response capture contains 83 records; successful login session tokens
  are redacted. The suite’s direct SQL checks verified player/ship remained in
  sector 1, sector 2 knowledge persisted, no fixture sectors remained visited,
  and remote port 2 was not recorded as known.
- Final port database check: port 2 ORE mode remained `sell`; stock was 99
  after the valid test purchase; the rejected sell operations created no ORE
  sell log rows.
- `tests.v2/test_qa_runner_config.py`: **PASS**, 5 tests for endpoint selection,
  response redaction, and the QA database guard.
- `python3 -m py_compile` for `json_runner.py`, `run_suites.py`, and `rig_db.py`:
  **PASS**.

The `sys.raw_sql_exec` test handler accepts one prepared SQL statement per
request. Fixture statements were split after the first integration run exposed
multi-statement SQL errors; the final runs above passed with the corrected
fixtures.
