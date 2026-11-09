# V3 gameplay parity checklist

Scope: approved twclone V3 requirements and the project's pinned TradeWars
player-facing scope (see `docs/V3_STATUS.md` and issue #491). This is a source
and contract inventory, not runtime certification. “Implemented” means a code
path exists; end-to-end behavior still needs focused protocol verification.

| Subsystem | Required | Implemented | Partial | Missing | Status |
|---|---|---|---|---|---|
| Player lifecycle | Register, authenticate, persist identity/session, recover state | `src/server_auth.c`, `src/server_players.c`, `src/db/repo/repo_auth.c` | Session replay/client recovery and public-player commands need protocol verification | `player.list_online` is an explicit stub; `player.rankings` contract conflicts on ranking key | Partial |
| Universe and navigation | Generated connected sectors/warps; movement, scans and route assistance | `src/bigbang_pg_main.c`, `src/server_universe.c`, `src/server_autopilot.c` | Transwarp fuel/interdiction and entry-hazard aftermath are unresolved | None established from source alone | Partial; see `V3_STATUS.md` |
| Ships and lifecycle | Ownership, acquisition, status, upgrades/equipment, repair, cargo, claim/tow | `src/server_ships.c`, `src/server_stardock.c`, `src/db/repo/repo_ships.c` | Transfer and shipyard journeys lack full runtime evidence | `ship.sell` / `shipyard.sell` explicitly stubbed; settlement policy unresolved | Partial |
| Trading/economy | Directional port trade, authoritative quotes, cargo/credits settlement | `src/server_ports.c`, `src/db/repo/repo_ports.c`, `src/db/repo/repo_market.c` | Retry/restart conservation and market matching require verification | Haggling and canonical trade-policy details are unresolved | Partial |
| Planets/colonies | Genesis, landing, transfers, worker assignment, production and citadels | `src/server_planets.c`, `src/db/repo/repo_planets.c`, `src/db/repo/repo_cron.c` | Production, treasury, citadel lifecycle and combat aftermath are incomplete/unverified | `planet.harvest` is stubbed; `planet.view` is marked NYI and its morale field has no schema source | Partial |
| Combat/defences | Ship/planet combat, fighters, mines, shields, destruction and recovery | `src/server_combat.c`, `src/server_planets.c`, `src/db/repo/repo_combat.c` | Combat odds, destruction/podding and hazard aftermath need canonical policy/runtime proof | No general gap declared without resolving issue #396 / `V3-CAN-00` | Partial |
| Corporations | Membership, roles, shared assets, treasury and governance | `src/server_corporation.c`, `src/db/repo/repo_corporation.c` | Membership events and advanced settlement require verification | Tax/dividend RPCs are explicitly not registered; stock subcommands have a stub fallback | Partial |
| Banking/finance | Balances, deposits, withdrawals, transfers, statements and interest | `src/server_bank.c`, `src/db/repo/repo_bank.c`, `src/server_engine.c` | Interest schedule/replay and ledger/restart invariants unresolved | Standing-order processing is a TODO (`db_bank_process_orders`); no CRUD RPC is registered | Partial |
| Stations/services | StarDock, shipyard, tavern, hardware and police interactions | `src/server_stardock.c`, `src/server_police.c`, `src/server_communication.c` | Tavern/police service rules and station journeys need verified end-to-end evidence | Shipyard sale is stubbed; some police actions are phase stubs | Partial |
| Federation/law | Alignment, protected space/assets, penalties and enforcement | `src/server_combat.c`, `src/server_police.c`, `src/server_cron.c` | Captain Z/MSL jurisdiction and daily sweep runtime behavior need approved-contract verification | Do not infer pursuit or enforcement outside explicit contracts | Partial; decisions in `docs/design/V3_NPC_FEDERATION_PHASES.md` |
| NPC universe | Persistent faction ships, trading, movement, combat and consequences | `src/server_engine.c`, `src/server_cron.c`, `src/db/repo/repo_cron.c` | Ferengi/Orion autonomous behavior and security responses are incomplete/unverified | Blockade, hunting and pursuit claims require behavior-level verification | Partial; see `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md` |
| Computer/intelligence | Scanning, plots, recommendations and information boundaries | `src/server_universe.c`, `src/server_players.c`, `src/server_autopilot.c` | Knowledge persistence and deterministic route recommendations need runtime verification | Ranking/information scope has open issue #423 | Partial |
| Turns/progression | Turn costs, experience, alignment, daily work and durable progression | `src/server_universe.c`, `src/server_players.c`, `src/server_engine.c`, `src/db/repo/repo_cron.c` | XP/turn restart and idempotency need end-to-end verification | No separate victory condition is established by the available V3 authority | Partial |
| Events/persistence | Atomic state transitions, durable ledger/events and restart recovery | Domain handlers plus `src/db/repo/` and `src/server_engine.c` | Event replay/ACK/resume and several multi-step journeys lack runtime evidence | No universal gap asserted without a protocol-specific trace | Partial |

## Implemented corrections in this pass

- `ship.jettison` now updates cargo on the authoritative active ship ID used by
  its validation/read, instead of the potentially stale `client_ctx.ship_id`.
  This prevents a ship switch followed by jettison from removing cargo from a
  different hull. See `src/server_ports.c::cmd_trade_jettison`.
- `h_decloak_ship` is no longer a silent no-op: it clears the persisted cloak
  marker through the ship repository, restoring the action-triggered decloak
  path required by the engine's cloak lifecycle. See
  `src/server_combat.c::h_decloak_ship` and `repo_ships_decloak`.
- `planet.colonists.allocate` locks the selected planet row before reading
  worker/population state, preventing concurrent allocations from validating
  against the same stale unassigned pool. Corporation control is rechecked
  against current membership instead of trusting cached session `corp_id`. See
  `src/server_planets.c::cmd_planet_colonists_allocate`.

All three source changes compile in the PostgreSQL build (`make -C bin -j2`)
with ASan/UBSan enabled, and scoped `git diff --check` is clean. Focused
runtime/state-transition verification remains outstanding: the documented QA
game-server port 12483 is not currently listening, and no regression/full-suite
run was started.

## Next actionable correction

Continue with the next source-confirmed defect whose behavior is explicit in the
approved protocol; do not implement the ambiguously specified sale, harvest,
standing-order, or `planet.view` policies below without resolving their
contracts.

## Explicit specification blockers

- `V3-CAN-00` / issue #491: canonical edition and SysOp policy needed before
  finalizing haggling, insurance, harvest, combat odds, capture, or planet
  destruction rules.
- `planet.view` describes morale but `planets` has no morale field; its
  response and relation to the existing `planet.info` command need resolution.
- `player.list_online` prose and schemas disagree about pagination/fields.
- `bank.orders.*` is only described as “manage recurring payments”; schedule,
  recipient authorization, execution timing and failure semantics are absent.
