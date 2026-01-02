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

3.  **`run_suites_all.py`**: The main test runner. It automatically calls `rig_db.py` before executing the test suites.

## How to Run Tests

Simply run the main test runner:

```bash
python3 tests.v2/run_suites_all.py
```

This will:
1.  Run `rig_db.py` to set up the database state.
2.  Execute all `suite_*.json` and `suite_*.py` tests in alphabetical order.

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
