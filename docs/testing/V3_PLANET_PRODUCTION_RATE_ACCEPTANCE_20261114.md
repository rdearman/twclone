# V3 Planet Production Rate Acceptance — 2026-11-14

## Scope

This acceptance covers the new noncanonical, versioned ORE/ORG/EQU
workers-per-unit rates and persisted labor remainders. The existing fighter
production rules were not changed. It does not certify the other four
mechanics or every planetary workflow.

## Isolated environment

- PostgreSQL 16 Docker container: `twclone-qa-prod182-20261114`, published on
  loopback port `55489`.
- Fresh game database: `twclone_rates_game`, built by the current V3 BigBang
  binary with V3 migrations enabled; migration 182 was applied during setup.
- Actual V3 server binary: built locally with ASan/UBSan and connected only to
  that database.
- Ordinary test account: `v3prod_player_20261114` (player 106). Its production
  planet and initial stock rows were deterministic SQL fixtures. Planet
  landing, allocation, view and harvest were executed as this ordinary player
  using the Python `TWClient` transport.

## Player/server transcript

Registration and login succeeded for the ordinary account. The Python client
then sent these operations and received the indicated responses:

```text
planet.land {"planet_id":4}
  -> status=ok, type=planet.land.success, data.planet_id=4

planet.colonists.allocate
  {"planet_id":4,"ore":1000,"organics":0,"equipment":0}
  -> status=ok, workers ore=1000, unassigned=0

planet.view {"planet_id":4}
  -> status=ok, type=planet.view_v1,
     production ORE gross_per_cycle=10, consumption=0, net=10

planet.harvest {"planet_id":4}
  -> status=ok, ore_harvested=10, turns_charged=0
```

The initial `planet.view` fixture lacked a citadel treasury row and failed
closed with `Could not retrieve planet treasury`. Adding the required treasury
fixture row allowed the normal read flow; no gameplay assertion was changed.

## Scheduled settlement and database evidence

The engine's normal `planet_growth` scheduler was accelerated only by moving
its due time in the disposable database. It produced 10 ORE, persisted the
current production tick, and retained zero labor remainder for exactly 1,000
workers at a rate of 100 workers per unit. The player then harvested that
stock. Authoritative rows showed planet ORE 0 and ship ORE 10, conserving all
10 units.

A same-tick scheduler retry left planet stock at 10 and the production tick
unchanged (`10|2991090`), demonstrating idempotent tick replay. Migration 182
also passed twice against PostgreSQL temporary tables: all 21 built-in
class/commodity rate rows were initialized, existing planet remainder columns
defaulted to zero, and a configured rate override survived replay.

The first scheduler run exposed an undersized C SQL buffer after the expanded
production query. The failure was reproduced against the disposable server;
the query buffer was enlarged, the server rebuilt, and the ordinary-player
journey plus scheduled settlement and retry were rerun successfully.

## Automated verification

- `make -C bin -j2`: passed with ASan/UBSan.
- `pytest -q tests.v2/test_v3_planet_production_rates.py tests.v2/test_planetary_server_contract.py`: **57 passed**.
- PostgreSQL migration 182 replay test: passed.
- `python3 -m json.tool sql/pg/migration_manifest.json`: passed.
- `git diff --check`: passed.

## Status and limits

Planet commodity production is **IMPLEMENTED_UNVERIFIED** pending broader
acceptance of ORE/ORG/EQU output, slave production, fractional remainder
carryover, capacity limits, transaction rollback, restart persistence and
ordinary Python client-menu execution. This run did not change the production
fighter contract. No beta or production database was contacted.
