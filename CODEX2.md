# Codex #2 Handover

## Assignment

Server/API work only. Do not change client UI, Godot, menus, HUD rendering,
key bindings, or player-client behavior. Current focus is the Ferengi travelling
trader system (#285), including its configuration polish. Keep work scoped to
the approved feature; do not add haggling, dynamic pricing, diplomacy, market
shocks, or taxation.

Before editing shared files, inspect `git status --short` and the relevant
recent diff. Multiple agents have left unrelated work in this shared checkout.
Do not stage, revert, or overwrite changes outside the owned hunk.

## Implemented Ferengi behavior

- Stable named traders have persistent identity, ships, sector location, and
  visit state.
- The NPC processing pass provisions configured traders, records encounters,
  generates durable per-player offers, emits targeted `ferengi.trader.offer_v1`
  events, and moves traders through connected sectors.
- `ferengi.traders`, `ferengi.deal.accept`, and `ferengi.deal.reject` expose the
  player API. Open offers remain discoverable after the trader moves.
- Deal settlement and state changes are transactional and idempotent. Expired
  deals are cleaned up through the NPC processing path and checked on action.
- Per-player reputation is stored in `ferengi_player_relationships`; remembered
  encounter and deal outcomes are stored in `ferengi_trader_interactions`.
- Acceptance adds 5 reputation; rejection subtracts 1. Current fixed terms use
  110% of base price for trader sales and 90% for trader purchases. No haggling,
  dynamic pricing, or diplomacy is implemented.
- Trader purchases are offered only when the Ferengi faction account can cover
  them.

## Configuration polish

Migration `sql/pg/113_ferengi_trader_configuration.sql` adds:

- `ferengi_trader_definitions`: stable code, display name, ship type, and active
  state. Active rows determine the trader count.
- `ferengi_trader_definition_rotation`: ordered commodity rotation with
  commodity foreign keys.
- `ferengi_trader_definition_cargo`: initial per-trader inventory.
- Integer config setting `ferengi.offer_lifetime_seconds`, seeded to 21,600
  seconds (six hours). Valid configured values are 60 through 604,800 seconds;
  invalid or missing values fall back to six hours.

The repository provisions a missing active definition on startup and on the
15-minute trader processing pass. A forced sysop Ferengi tick runs the pass
immediately. Adding a trader requires data rows for its definition and
commodity rotation, plus optional starting cargo; it does not require C source
changes. Marking a definition inactive disables its existing trader during
provisioning. Existing trader identity, location, and visit history remain
persistent.

The future pricing extension point is offer generation in
`repo_universe_create_ferengi_offer`; persisted deal terms should remain fixed
after creation.

## Main files

- `src/server_universe.c`: Ferengi initialization, RPC handlers, settlement,
  event delivery, and NPC processing.
- `src/db/repo/repo_universe.c` and `.h`: persistence, provisioning, offer
  generation, interaction history, expiry, and movement.
- `src/server_loop.c`: command registration.
- `src/schemas.c`: response and event schemas.
- `src/server_communication.c`: `npc.*` subscription topic support.
- `sql/pg/112_v2_economy_commerce.sql`: core Ferengi persistence tables, as
  part of the combined economy/commerce migration.
- `sql/pg/113_ferengi_trader_configuration.sql`: configurable roster and
  settings.
- `tests.v2/suite_ferengi_travelling_traders.json`: lifecycle integration suite,
  including a temporary data-only trader provisioning case.
- `tests.v2/test_pg_migration_112_economy_commerce.sql` and
  `tests.v2/test_pg_migration_113_ferengi_trader_configuration.sql`: migration
  coverage.
- `docs/design/V2_ECONOMY_COMMERCE_IMPLEMENTATION.md` and
  `docs/PROTOCOL.v3/22_Trade_and_Port_Commands.md`: design and protocol notes.

## Verification and remaining work

- `make -j2` passed after the configuration changes.
- `python3 -m json.tool tests.v2/suite_ferengi_travelling_traders.json` passed.
- `git diff --check` passed for the Ferengi files.
- Integration command:
  `python3 tests.v2/run_suites.py --suite tests.v2/suite_ferengi_travelling_traders.json`
- The integration runner could not start because sandbox socket creation failed
  with `PermissionError: [Errno 1] Operation not permitted`. PostgreSQL-backed
  migration tests were not run because a usable PostgreSQL/server environment
  was unavailable.

Remaining verification: apply migrations 112 and 113 to a disposable initialized
PostgreSQL database; run both migration SQL tests; then run the Ferengi
integration suite against the rebuilt server. Do not close #285 as release
verified until this passes.

## Shared checkout caution

The working tree contains substantial pre-existing changes from other workers,
including edits in several server and schema files listed above. The repository
and server universe files also contain unrelated work (for example ship
personality and sector hazard changes). Check each target's current diff before
continuing and preserve those hunks.
