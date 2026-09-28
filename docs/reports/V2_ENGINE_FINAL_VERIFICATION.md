CODEX NUMBER: CODEX #3  
ASSIGNMENT: V2 RC engine and database final verification  
CURRENT STATUS: PARTIAL — database migration path passed; live server/engine restart verification is blocked

# V2 Engine and Database Final Verification

## Results

| Check | Result | Evidence |
| --- | --- | --- |
| Fresh PostgreSQL install | PASS | PostgreSQL 16.15 disposable cluster; loaded the fresh schema, lookup/gameplay seeds, procedures, then migration sequence through 114. |
| Migration ordering 090+ through 114 | PASS | Applied base/schema support and seeds 090–095, then migration scripts 100–114 in documented order. |
| Migration replay | PASS | Reapplied each migration 100–114 on the same fresh disposable database. No DDL or data script failures. |
| Upgrade fixture coverage | PASS | Existing isolated fixtures for migrations 100, 102, 107, 108 and 109 passed, covering cargo copy, port-type backfill, illegal stock replay, planet balance preservation, and notice/trade-offer schema. |
| Migrations 110–114 assertions | PASS | Hazard, personality, commerce, Ferengi configuration, fighter migration, and fresh-schema assertions passed. Migration 114 legacy conversion fixture retained population/assignment balances and checked replay/configuration. |
| Ship personality logic | PASS | `tests.v2/run_engine_regressions.sh` passed its personality rules and fighter production arithmetic tests. |
| S2S dispatcher unit check | PASS | `tests.v2/run_server_s2s_dispatch_test.sh` passed the isolated `command.push` dispatcher regression. |
| Hazard resolution in a running game | BLOCKED | Hazard schema test passed. The warp/transwarp entry suite was not run against a live server in this rehearsal. |
| `planet_growth` fighter production in a running engine | BLOCKED | The live cron suite is present but was not run against a server/engine process. |
| Server/engine restart and reconnect | BLOCKED | No server/engine process was started against this disposable database, so process restart, S2S handshake after restart, and application database reconnection remain unverified. |
| Representative pre-v2 restore | BLOCKED | No representative pre-v2 backup was available. Upgrade evidence is limited to isolated migration fixtures and the documented synthetic schema path. |

No current regression assertion failed on the successful migration rerun. The initial fresh-schema check found a test-only column-name casing error (`fighterProduction` folds to `fighterproduction` in PostgreSQL); the assertion was corrected and passed on rerun. No migration or gameplay rules were changed.

## Environment and commands

- PostgreSQL 16.15 on an isolated cluster and database under `/tmp`; it was stopped and discarded after validation.
- The sandbox initially denied local socket binding. The validated run used the isolated temporary directory for PostgreSQL sockets and loopback only; no shared database was contacted.
- Fresh schema path used `sql/pg/000_tables.sql`, supporting schema/functions, seeds 090–095, then migration scripts 100–114.
- Replay path applied migration scripts 100–114 a second time on the same database.
- SQL assertions included `test_pg_migration_100_ship_cargo_upgrade.sql`, `test_migration_102_porttypes.sql`, `test_migration_107_illegal_stock.sql`, `test_migration_108_planet_entity_stock.sql`, `test_pg_migration_109_sector_notices_trade_offers.sql`, `test_pg_sector_environmental_hazards.sql`, `test_pg_migration_111_ship_personalities.sql`, `test_pg_migration_112_economy_commerce.sql`, `test_pg_migration_113_ferengi_trader_configuration.sql`, `test_pg_migration_110_114_fresh_schema.sql`, and `test_pg_migration_114_planet_fighter_production.sql`.
- Unit checks: `tests.v2/run_engine_regressions.sh`, `tests.v2/run_server_s2s_dispatch_test.sh`, `python3 -m py_compile tests.v2/suite_planet_fighter_production_e2e.py`, and `git diff --check` on the regression/report files.

## Release assessment

Migration behavior is PASS for the exercised fresh install, ordered chain, replay, and synthetic upgrade fixtures. Full engine/database release verification is BLOCKED until a disposable application stack is started and the following are captured: hazard warp/transwarp results, real `planet_growth` fighter production and retry results, persisted state after server/engine restart, and successful S2S/database reconnection. A representative pre-v2 backup rehearsal is also still needed for production-shaped upgrade confidence.

The existing [PostgreSQL upgrade guide](../POSTGRESQL_V2_UPGRADE.md) records a known integration risk: the repository's real S2S command-push path still needs retesting after its dispatcher uses the S2S validator. The standalone dispatcher unit test passing does not clear that runtime risk.
