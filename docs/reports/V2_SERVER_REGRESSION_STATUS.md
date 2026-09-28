# V2 Server Regression Coverage Status

CODEX NUMBER: #2  
ASSIGNMENT: V2 Release Candidate — Server regression coverage  
CURRENT STATUS: Coverage reviewed and hardened; integration execution is blocked in this environment.

## Coverage review

| Feature | Regression coverage in `tests.v2` | Remaining test limitation |
|---|---|---|
| #247 Black-market hardware | `suite_black_market_hardware.json` checks mapped items, stock-dependent quotes, stable base price, empty-stock refusal, purchase stock decrement, scheduled restock, and unmapped-item refusal. Migration 112 test checks unique inventory rows. | Integration suite was not executable here. Invalid quantity, insufficient credits, and a player outside the port are not explicitly covered by this suite. |
| #250 Sector notices | `suite_subscriptions_e2e.py` creates a durable beacon notice, checks its event payload and stored notice, and checks overlapping exact/scoped/wildcard subscriptions do not duplicate delivery. Migration 109 test checks sector scope, legacy global notice compatibility, invalid sector scope, and notice-key index. | Integration suite was not executable here. Notice expiry and unauthorized notice listing are not explicitly asserted. |
| #253 Trade offers | `suite_trade_offers_e2e.json` checks offer retry identity, one-time settlement, repeated acceptance/cancellation, sender cancellation, expiry state, and persisted cargo results. Added a negative case proving a non-recipient cannot accept. Migration 109 covers offer FKs, pending-expiry index, and duplicate idempotency key rejection. | Integration suite was not executable here. Invalid quantities and unauthorized cancellation are not explicit cases. |
| #285 Ferengi traders | `suite_ferengi_travelling_traders.json` checks named persistent trader identity, offers after movement, accepted/rejected interaction memory, repeated acceptance, cron expiry, and data-only trader configuration/roster provisioning. Migration 113 coverage exists for configuration schema. | Integration suite was not executable here. Reputation value changes and player isolation between traders are not directly asserted. |
| #294 Route recommendations | `suite_computer_loops.json` checks empty and populated results, connected two-way route fields, port names, commodity, direction data, profit hints, and response schema types. | Integration suite was not executable here. |
| Market shocks | `suite_market_shocks_e2e.json` checks scoped price effects, stored scope/duration, automatic expiry cleanup, and quote restoration. Migration 112 test now checks a valid random universe shock persists and invalid trigger source, incomplete scope, out-of-range multiplier, and non-positive duration are rejected. | Integration suite and migration SQL test were not executable here. Stochastic server generation itself is not asserted because it is nondeterministic. |
| Taxation | `suite_planet_taxation_e2e.json` checks player and corporate tax receipts, taxable totals, daily task execution, and idempotent retry. The planet operations suite now verifies `planet.market.sell` writes exactly one persisted taxable activity row. Migration 112 checks unique daily assessment and invalid tax-rate rejection. | Integration suite and migration SQL test were not executable here. Production activity generation is not directly asserted by an integration case. |

## Changes in this pass

- Added a `planet.market.sell` regression assertion for exactly one durable `planet_economic_activity` row.
- Added a trade-offer permission regression: an unrelated player cannot accept another player's offer.
- Extended the migration 112 regression SQL with valid random-shock persistence and invalid shock/tax constraint cases.

No gameplay implementation, protocol, migration, or schema files were changed.

## Verification

- `make clean && make -j1` — passed. The build emitted existing warnings, but completed successfully.
- JSON parsing for the seven relevant JSON suites — passed.
- Python AST syntax check for `suite_subscriptions_e2e.py` — passed.
- `python3 tests.v2/run_suites.py --suite tests.v2/suite_computer_loops.json --host 127.0.0.1 --port 1234` — blocked before the suite could connect: Python socket creation returned `PermissionError: [Errno 1] Operation not permitted`.
- PostgreSQL readiness check — no server was available at `localhost:5432`. A disposable PostgreSQL cluster could not start because this execution environment also denied local socket creation. The checked-in `bin/bigbang.json` points at a remote database, so it was not used for rigging or integration tests.
- Migration 109/112/113 SQL tests and feature integration suites remain unverified in this execution environment.

## Release verification state

The regression definitions cover the main persistence, duplicate-request, expiry, permission, and schema constraints for the listed features. Build and fixture syntax checks pass. Release verification is still **implemented but needs integration verification** because the sandbox prevents starting the required local PostgreSQL/server sockets. The explicit remaining coverage gaps are listed in the table above; none required gameplay changes for this assignment.
