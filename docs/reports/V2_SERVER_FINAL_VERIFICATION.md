# V2 Server Final Verification

CODEX NUMBER: #2  
ASSIGNMENT: V2 Release Candidate — Server regression integration  
CURRENT STATUS: PASS for the requested focused server verification; a supplemental legacy planet-operation suite has unrelated fixture/API mismatches.

## Verification environment

- Built from the current shared checkout with `make clean && make -j1` — **PASS**. Existing compiler warnings remain; the server and `bigbang` linked successfully.
- Started a disposable PostgreSQL 16 cluster under `/tmp/twclone-v2-rc-pg` on `127.0.0.1:55432` — **PASS**.
- Created the disposable `twclone` database and applied the PostgreSQL SQL directory with `bin/bigbang` — **PASS**, through migrations 109–114.
- Loaded the `tests.v2` test rig with `PGPORT=55432 python3 tests.v2/rig_db.py --config tests.v2/test_rig.json --reset` — **PASS**.
- Started the rebuilt server from `/tmp/twclone-v2-rc` using its temporary configuration; the integration suites connected to its local test socket on port 1234 — **PASS**.
- Re-ran migration regression SQL using `psql 'dbname=twclone user=postgres host=127.0.0.1 port=55432' -v ON_ERROR_STOP=1 -f <test-file>` — **PASS** for migrations 109, 112, and 113.

## Feature verification

| Feature | Result | Evidence |
|---|---|---|
| #247 black-market hardware | **PASS** | `python3 tests.v2/run_suites.py --suite tests.v2/suite_black_market_hardware.json --host 127.0.0.1 --port 1234`; verifies inventory, stock-based quotes and purchases, empty stock, and scheduled restock. The fixture waits five seconds for cron restock. |
| #250 sector notices and subscription routing | **PASS** | `python3 tests.v2/suite_subscriptions_e2e.py`; verifies durable scoped notices and subscription delivery without duplicate overlap delivery. Migration 109 SQL regression also passed. |
| #253 trade offers | **PASS** | `python3 tests.v2/run_suites.py --suite tests.v2/suite_trade_offers_e2e.json --host 127.0.0.1 --port 1234`; verifies idempotency, settlement, expiry, cancellation, and recipient permissions. Migration 109 SQL regression also passed. |
| #285 Ferengi traders | **PASS** | `python3 tests.v2/run_suites.py --suite tests.v2/suite_ferengi_travelling_traders.json --host 127.0.0.1 --port 1234`; verifies named persistent traders, offer interactions, remembered decisions, expiry, and data-driven configuration. The suite provisions the required Ferengi homeworld fixture. Migration 113 SQL regression passed. |
| #294 route recommendations | **PASS** | `python3 tests.v2/run_suites.py --suite tests.v2/suite_computer_loops.json --host 127.0.0.1 --port 1234`; verifies empty/populated recommendations and response details including route, port, commodity, direction, and profit fields. |
| Market shocks | **PASS** | `python3 tests.v2/run_suites.py --suite tests.v2/suite_market_shocks_e2e.json --host 127.0.0.1 --port 1234`; verifies price effects, persisted scope/duration, expiry cleanup, and price restoration. Migration 112 SQL regression passed. |
| Taxation | **PASS** | `python3 tests.v2/run_suites.py --suite tests.v2/suite_planet_taxation_e2e.json --host 127.0.0.1 --port 1234`; verifies player/corporate receipts and retry idempotency. `python3 tests.v2/run_suites.py --suite tests.v2/suite_planet_taxable_activity_e2e.json --host 127.0.0.1 --port 1234` verifies a successful `planet.market.sell` records exactly one durable taxable activity row. Migration 112 SQL regression passed. |

All altered and new JSON suites parsed successfully. Migration outputs explicitly reported success for 109, 112, and 113.

## Supplemental failure

**FAIL (outside the focused required suites):** `tests.v2/suite_bucket1_remainder_planet.json` still has legacy planet-operation cases that fail due to fixture/API mismatch: duplicate entity stock after `planet.create` pre-creates stock, a dependent sell then lacks seeded inventory, the rename case sends the wrong field (`name` is required), the withdraw case omits required `commodity`, and unauthenticated variants fail earlier with error 1301. The focused taxable activity coverage was separated into `suite_planet_taxable_activity_e2e.json` and passed. No server API changes were made for these unrelated failures.

## Release classification

- **PASS:** clean build; disposable PostgreSQL setup; migrations applied; migration regressions 109/112/113; all seven requested feature areas and their focused integration suites.
- **FAIL:** supplemental legacy `suite_bucket1_remainder_planet.json` cases listed above.
- **BLOCKED:** none for the requested focused server verification.

Verification covered focused regression paths, not every `tests.v2` suite. No gameplay/API implementation was changed during this release verification. The original regression coverage checkpoint is `513f1d5b`; fixture stabilization and this report are committed separately. No push was performed.
