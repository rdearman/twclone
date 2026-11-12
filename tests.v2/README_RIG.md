# Test Rigging System (v2)

This directory contains the regression test suite for the TWClone server. The tests are designed to be database-agnostic and run against a PostgreSQL database.

To ensure consistent and deterministic test results, the database must be "rigged" (seeded with specific data) before the tests run. This rigging process replaces the previous method of inline SQL setup within individual test suites.

## Components

1.  **`test_rig.json`**: The canonical definition of the test environment. It defines:
    *   Configuration variables (e.g., `startingcredits`).
    *   Sectors and Ports.
    *   Users (Players) and their initial states (credits, location, type).
    *   Ships (types, ownership, location, cargo).
    *   Planets (ownership, citadels).
    *   Corporations.
    *   Deployed Assets (fighters, mines).
    *   Shipyard Inventory.

2.  **`rig_db.py`**: The python script that reads `test_rig.json` and applies it to the database using `psql`. It performs "upserts" (INSERT ... ON CONFLICT UPDATE) to ensure idempotency. It can also reset the test data if the `--reset` flag is used (currently implemented as upserts/overwrites).

3.  **`run_suites_all.py`**: The canonical all-JSON runner. Database rigging is
    explicit and requires an isolated QA database; the runner never guesses a
    database target.

## How to Run Tests

The normal entry point provisions a disposable PostgreSQL database, runs the
real BigBang binary, loads this fixture, starts the real V3 server on a
dedicated port, executes the JSON suites, and tears the server and database
down afterward:

```bash
make regression-full
```

`make regression-full` uses `tests.v2/run_regression_instance.py`. It runs the
checked-in `bin/bigbang` and `bin/server` binaries against PostgreSQL; there is
no mock server or substitute database. The generated database name must begin
with `twclone_v3qa_` and is dropped after the run. Regression database access
defaults to the dedicated QA cluster on loopback port `55438`; port `5432` is
explicitly rejected to prevent accidental use of the live/default instance.
Set `QA_PGPORT` only to another verified disposable QA endpoint, never to 5432.
The default database role is `twclone_qa_runner` (`QA_PGUSER` overrides it).
Credentials should be supplied through the protected QA `.pgpass` entry or
`QA_PGPASSWORD`; the runner removes an unrelated inherited `PGPASSWORD` so it
cannot override `.pgpass`. Before provisioning, the runner verifies loopback,
port, authenticated QA role and the presence of the `twclone_qa_*` or
`twclone_v3qa_*` database namespace using a read-only query. It uses the
versioned PostgreSQL client directly for identity checks, rigging and cleanup.
BigBang startup output is retained in `bigbang.log` when setup fails.

The instance runner accepts `--keep-artifacts` when logs and its temporary
bootstrap configuration need to be retained for diagnosis. Configure the
PostgreSQL administrator connection with `TWCLONE_TEST_PG_ADMIN` when the
default `bin/bigbang.json` connection is not appropriate.

The JSON runner then:
1. Discovers every direct `suite_*.json` suite.
2. Executes every suite compatible with `json_runner.py` in alphabetical order.
3. Records per-case responses, failures, infrastructure blocks and unsupported
   suite definitions in the JSON report.

The two intentionally non-direct JSON files are reported as unsupported:
`suite_regression_full.json` is a historical manifest, while
`suite_phase10_market_decay.json` uses a nested DSL not understood by the
authoritative runner. Python, PostgreSQL and C tests are separate checks.

To rig a disposable database explicitly before running suites:

```bash
python3 tests.v2/run_suites_all.py --rig \
  --dbname twclone_v3qa_<unique_name> \
  --pg-host 127.0.0.1 --pg-port 55439 --pg-user <user> \
  --host 127.0.0.1 --port 12483
```

The `--rig` guard accepts only database names beginning with `twclone_qa_` or
`twclone_v3qa_` and only loopback PostgreSQL hosts.

Normal verification is split as follows:

```bash
make check                 # static/architecture checks; no database reset
make regression-full       # all compatible JSON suites; server must be running
./tests.v2/run_engine_regressions.sh  # existing C regression checks
```

## How to Add New Test Data

If you write a new test that requires specific database state (e.g., a specific user, ship, or planet):

1.  **DO NOT** write SQL `INSERT` or `UPDATE` statements in your test suite JSON or Python file.
2.  **DO** edit `tests.v2/test_rig.json`.
3.  Add the required entity to the appropriate section (`users`, `ships`, `planets`, etc.).
4.  Ensure you use unique IDs or usernames to avoid conflicts with existing test data.
5.  Your test can then assume this data exists.

**Example: Adding a new user**

Edit `tests.v2/test_rig.json`:

```json
"users": [
  ...
  {
    "username": "my_new_tester",
    "password": "password",
    "credits": 50000,
    "sector_id": 1
  }
]
```

## Troubleshooting

*   **Database Connection**: The rigging script tries to read `bin/bigbang.json` for connection details but defaults to `localhost`, user `postgres`, db `twclone`. Ensure your environment matches or update the script/json.
*   **"Column does not exist"**: If you see SQL errors during rigging, check `rig_db.py`. It constructs SQL strings manually and might need updating if the database schema changes.
