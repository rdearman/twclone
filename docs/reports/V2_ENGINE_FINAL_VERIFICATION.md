CODEX NUMBER: CODEX #3
ASSIGNMENT: V2 RC final QA fixes only
CURRENT STATUS: PASS for the targeted hazard, restart, cron, and S2S checks; representative pre-v2 restore remains blocked

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
| Hazard response type fields | PASS | Added the established `type` field alongside `hazard_type` in each player-visible event. The live hazard suite now asserts both names for warp and transwarp and passed. |
| `planet_growth` fighter production | PASS | The live `suite_planet_fighter_production_e2e.py` passed through server and engine, including no-citadel gating, citadel production, EQU use, cap, shortage, and same-interval retry. |
| Server restart and database reconnect | PASS | The new deterministic rehearsal stopped the server/engine process group cleanly, restarted the server against the same disposable database, and waited for the new listener and S2S handshake. Post-restart System login, SQL fixture creation, and integration tests succeeded. |
| Engine cron after restart | PASS | After restart, `suite_planet_fighter_production_e2e.py` created uniquely named planet fixtures and verified production state changed through `planet_growth`. The test passed, including citadel gating, EQU consumption, capacity, shortage, and repeated-tick protection. This uses newly created fixtures, not a preexisting cron timestamp. |
| S2S health handshake | PASS | Both server boots logged `accepted hello` and `ack send rc=0`; the evidence excerpt is retained below. The same run also completed the real engine `command.push` smoke path (`notice.publish player:42:hello:001`, engine ACK). |
| Representative pre-v2 restore | BLOCKED | No representative pre-v2 backup was available. Upgrade evidence is limited to isolated migration fixtures and the documented synthetic schema path. |

The initial fresh-schema check found a test-only column-name casing error (`fighterProduction` folds to `fighterproduction` in PostgreSQL); it was corrected and passed on rerun. The live hazard suite found a response field mismatch; the response now preserves both field names. No gameplay rules or migrations were changed.

## Final QA fix evidence

The runtime fix is additive: each hazard item contains both `type` (the
established contract) and `hazard_type` (the explicit name already in use).
`tests.v2/suite_sector_environmental_hazards.json` checks both fields for all
three hazard types on both warp and transwarp.

The repeatable disposable restart test is
`tests.v2/restart_engine_cron_rehearsal.py`. It requires a local PostgreSQL URL
whose database name starts with `twclone_qa_`, a migrated/seeded QA database,
and `bin/server`. It sets private listener ports in that QA database, starts
the server/engine twice, retains S2S evidence, then runs new fighter fixtures
and the live hazard suite after the second boot. Run it with:

```sh
QA_DATABASE_URL='postgresql://user@127.0.0.1:55439/twclone_qa_restart' \
QA_EVIDENCE_DIR=/tmp/twclone-qa-evidence \
python3 tests.v2/restart_engine_cron_rehearsal.py
```

Run evidence on 2026-09-28 used PostgreSQL 16.15 and the disposable database
`twclone_qa_restart` in a temporary cluster under `/tmp`. Both fresh boots
completed the S2S hello/ack. The retained log lines were:

```text
2026-09-28 11:48:02 [server]  accepted hello
2026-09-28 11:48:02 [server]  ack send rc=0
2026-09-28 11:48:03 [server]  accepted hello
2026-09-28 11:48:03 [server]  ack send rc=0
```

The post-restart test output was `planet_growth fighter production
integration checks passed`; the hazard response suite ended with `ALL SUITES
PASSED.` Evidence files were retained at
`/tmp/twclone-qa-restart-evidence` for this run.

## Environment and commands

- PostgreSQL 16.15 on an isolated cluster and database under `/tmp`; it was stopped and discarded after validation.
- The sandbox initially denied local socket binding. The validated run used the isolated temporary directory for PostgreSQL sockets and loopback only; no shared database was contacted.
- The application/engine integration cluster used `fsync=off` for disposable test speed. Migration rehearsal ran on a separate disposable cluster with PostgreSQL defaults.
- Fresh schema path used `sql/pg/000_tables.sql`, supporting schema/functions, seeds 090–095, then migration scripts 100–114.
- Replay path applied migration scripts 100–114 a second time on the same database.
- SQL assertions included `test_pg_migration_100_ship_cargo_upgrade.sql`, `test_migration_102_porttypes.sql`, `test_migration_107_illegal_stock.sql`, `test_migration_108_planet_entity_stock.sql`, `test_pg_migration_109_sector_notices_trade_offers.sql`, `test_pg_sector_environmental_hazards.sql`, `test_pg_migration_111_ship_personalities.sql`, `test_pg_migration_112_economy_commerce.sql`, `test_pg_migration_113_ferengi_trader_configuration.sql`, `test_pg_migration_110_114_fresh_schema.sql`, and `test_pg_migration_114_planet_fighter_production.sql`.
- Unit checks: `tests.v2/run_engine_regressions.sh`, `tests.v2/run_server_s2s_dispatch_test.sh`, `python3 -m py_compile tests.v2/suite_planet_fighter_production_e2e.py`, and `git diff --check` on the regression/report files.
- Final QA checks: `make -j2 -C bin server`, `python3 -m py_compile tests.v2/restart_engine_cron_rehearsal.py`, `python3 -m json.tool tests.v2/suite_sector_environmental_hazards.json`, and the disposable `restart_engine_cron_rehearsal.py` live run.

## Release assessment

Migration behavior is PASS for the exercised fresh install, ordered chain, replay, and synthetic upgrade fixtures. The hazard response contract, server restart, engine reconnect, post-restart `planet_growth`, and S2S hello/ack now have live disposable evidence. A representative pre-v2 backup restore rehearsal remains BLOCKED because no production-shaped pre-v2 backup was available. The missing player ID 42 encountered on the first startup attempt was a fixture omission documented by the upgrade guide; the final disposable rehearsal seeded it before startup.

The restart rehearsal also exercised S2S `command.push`: the server logged
`notice.publish player:42:hello:001`, and the engine logged `ack
duplicate=true`. This verifies delivery and acknowledgement for the seeded
idempotency probe; it does not verify first-time insertion of a new command.
