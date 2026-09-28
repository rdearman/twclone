CODEX NUMBER: CODEX #3  
ASSIGNMENT: V2 RC engine and database final verification  
CURRENT STATUS: PARTIAL — migrations and fighter cron passed; hazard response contract failed and production-backup verification is blocked

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
| Server/player smoke | PASS | Against the disposable DB: System and `newguy` login, `system.hello` with required `client_version`, and `player.my_info` all succeeded. |
| Hazard damage and persistence | PASS | Live warp and transwarp applied damage; the suite's persisted-damage assertions passed. |
| Hazard response type field | FAIL | The live hazard suite expected `data.hazards[].type`, but the assertions found no value there. Engine source currently emits `hazard_type` in each player-visible event. The hazard damage itself resolved and persisted. |
| `planet_growth` fighter production | PASS | The live `suite_planet_fighter_production_e2e.py` passed through server and engine, including no-citadel gating, citadel production, EQU use, cap, shortage, and same-interval retry. |
| Server/engine restart and database reconnect | PASS (runtime evidence) | Stopped and restarted the server, which starts a new engine child. After restart, System login and `sys.raw_sql_exec` succeeded; the persisted `planet_growth.last_run_at IS NOT NULL` query returned `t`. The inline check returned exit 1 only because its assertion expected a Python boolean while `sys.raw_sql_exec` serializes PostgreSQL booleans as `t` strings. |
| S2S health handshake | BLOCKED | The server reached its client listener before and after restart, but the temporary logs were discarded before the hello/ack line could be retained. The isolated dispatcher test is not a substitute for the handshake evidence. |
| Representative pre-v2 restore | BLOCKED | No representative pre-v2 backup was available. Upgrade evidence is limited to isolated migration fixtures and the documented synthetic schema path. |

The initial fresh-schema check found a test-only column-name casing error (`fighterProduction` folds to `fighterproduction` in PostgreSQL); it was corrected and passed on rerun. The live hazard suite found a response field mismatch, and the restart wrapper had a boolean parsing false negative (`t` versus Python `True`). No gameplay rules or migrations were changed.

## Environment and commands

- PostgreSQL 16.15 on an isolated cluster and database under `/tmp`; it was stopped and discarded after validation.
- The sandbox initially denied local socket binding. The validated run used the isolated temporary directory for PostgreSQL sockets and loopback only; no shared database was contacted.
- The application/engine integration cluster used `fsync=off` for disposable test speed. Migration rehearsal ran on a separate disposable cluster with PostgreSQL defaults.
- Fresh schema path used `sql/pg/000_tables.sql`, supporting schema/functions, seeds 090–095, then migration scripts 100–114.
- Replay path applied migration scripts 100–114 a second time on the same database.
- SQL assertions included `test_pg_migration_100_ship_cargo_upgrade.sql`, `test_migration_102_porttypes.sql`, `test_migration_107_illegal_stock.sql`, `test_migration_108_planet_entity_stock.sql`, `test_pg_migration_109_sector_notices_trade_offers.sql`, `test_pg_sector_environmental_hazards.sql`, `test_pg_migration_111_ship_personalities.sql`, `test_pg_migration_112_economy_commerce.sql`, `test_pg_migration_113_ferengi_trader_configuration.sql`, `test_pg_migration_110_114_fresh_schema.sql`, and `test_pg_migration_114_planet_fighter_production.sql`.
- Unit checks: `tests.v2/run_engine_regressions.sh`, `tests.v2/run_server_s2s_dispatch_test.sh`, `python3 -m py_compile tests.v2/suite_planet_fighter_production_e2e.py`, and `git diff --check` on the regression/report files.

## Release assessment

Migration behavior is PASS for the exercised fresh install, ordered chain, replay, and synthetic upgrade fixtures. Fighter production, server/player smoke, and database-backed state after a server/engine restart passed in a disposable application run. Release verification remains BLOCKED on the hazard response field mismatch, retained S2S hello/ack evidence, and a representative pre-v2 backup rehearsal. The missing player ID 42 encountered on the first startup attempt was a fixture omission documented by the upgrade guide; the later run seeded it before startup.

The existing [PostgreSQL upgrade guide](../POSTGRESQL_V2_UPGRADE.md) records a known integration risk: the repository's real S2S command-push path still needs retesting after its dispatcher uses the S2S validator. The standalone dispatcher unit test passing does not clear that runtime risk.
