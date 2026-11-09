# V3 gameplay parity checklist

Scope: approved twclone V3 requirements and the project's pinned TradeWars
player-facing scope (see `docs/V3_STATUS.md` and issue #491). This is a source
and contract inventory, not runtime certification. “Implemented” means a code
path exists; end-to-end behavior still needs focused protocol verification.

| Subsystem | Required | Implemented | Partial | Missing | Status |
|---|---|---|---|---|---|
| Player lifecycle | Register, authenticate, persist identity/session, recover state; list currently connected players | `src/server_auth.c`, `src/server_players.c`, `src/db/repo/repo_auth.c` | `player.list_online` now snapshots authenticated sessions and returns public id/username with offset pagination; session replay/client recovery needs protocol verification | Ranking key and online-list pagination/field allowlist prose conflict with executable schemas | Partial; online listing runtime verification pending |
| Universe and navigation | Generated connected sectors/warps; movement, scans and route assistance | `src/bigbang_pg_main.c`, `src/server_universe.c`, `src/server_autopilot.c` | Transwarp fuel/interdiction and entry-hazard aftermath are unresolved | None established from source alone | Partial; see `V3_STATUS.md` |
| Ships and lifecycle | Ownership, acquisition, status, upgrades/equipment, repair, cargo, claim/tow | `src/server_ships.c`, `src/server_stardock.c`, `src/db/repo/repo_ships.c` | Transfer and shipyard journeys lack full runtime evidence; cargo mutations now serialize by ship row | `ship.sell` / `shipyard.sell` explicitly stubbed; settlement policy unresolved | Partial |
| Trading/economy | Directional port trade, authoritative quotes, cargo/credits settlement, persistent market orders | `src/server_ports.c`, `src/server_planets.c`, `src/server_cron.c`, `src/db/repo/repo_ports.c`, `src/db/repo/repo_market.c` | Planet buy orders now place without escrow, validate funds/capacity during locked matching, partially fill, cancel unexecutable remainder, support player cancellation and persistent expiry/configuration; database-backed lifecycle verification pending | Haggling and canonical trade-policy details remain unresolved | Implemented in source; runtime unverified |
| Planets/colonies | Genesis, landing, transfers, worker assignment, production and citadels | `src/server_planets.c`, `src/db/repo/repo_planets.c`, `src/db/repo/repo_cron.c` | Production, treasury, citadel lifecycle and combat aftermath are incomplete/unverified | `planet.harvest` is stubbed; `planet.view` is marked NYI and its morale field has no schema source | Partial |
| Combat/defences | Ship/planet combat, fighters, mines, shields, destruction and recovery | `src/server_combat.c`, `src/server_planets.c`, `src/db/repo/repo_combat.c` | Combat odds, destruction/podding and hazard aftermath need canonical policy/runtime proof | No general gap declared without resolving issue #396 / `V3-CAN-00` | Partial |
| Corporations | Membership, roles, shared assets, treasury and governance | `src/server_corporation.c`, `src/db/repo/repo_corporation.c` | Membership events and advanced settlement require verification | Tax/dividend RPCs are explicitly not registered; stock subcommands have a stub fallback | Partial |
| Banking/finance | Balances, deposits, withdrawals, transfers, statements and interest | `src/server_bank.c`, `src/db/repo/repo_bank.c`, `src/server_engine.c` | Interest schedule/replay and ledger/restart invariants unresolved | Standing-order processing is a TODO (`db_bank_process_orders`); no CRUD RPC is registered | Partial |
| Stations/services | StarDock, shipyard, tavern, hardware and police interactions | `src/server_stardock.c`, `src/server_police.c`, `src/server_communication.c` | Tavern rules and police/station journeys need verified end-to-end evidence | Shipyard sale is stubbed; police interaction runtime evidence remains pending | Partial |
| Federation/law | Alignment, protected space/assets, penalties and enforcement | `src/server_combat.c`, `src/server_police.c`, `src/server_cron.c` | Police bribe/surrender rules are implemented; Captain Z/MSL jurisdiction and daily sweep still need approved-contract runtime verification | Do not infer pursuit or enforcement outside explicit contracts | Partial; decisions in `docs/design/V3_NPC_FEDERATION_PHASES.md` |
| NPC universe | Persistent faction ships, trading, movement, combat and consequences | `src/server_engine.c`, `src/server_cron.c`, `src/db/repo/repo_cron.c` | Ferengi/Orion autonomous behavior and security responses are incomplete/unverified | Blockade, hunting and pursuit claims require behavior-level verification | Partial; see `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md` |
| Computer/intelligence | Scanning, plots, recommendations and information boundaries | `src/server_universe.c`, `src/server_players.c`, `src/server_autopilot.c` | Knowledge persistence and deterministic route recommendations need runtime verification | Ranking/information scope has open issue #423 | Partial |
| Turns/progression | Turn costs, experience, alignment, daily work and durable progression | `src/server_universe.c`, `src/server_players.c`, `src/server_engine.c`, `src/db/repo/repo_cron.c` | XP/turn restart and idempotency need end-to-end verification | No separate victory condition is established by the available V3 authority | Partial |
| Events/persistence | Atomic state transitions, durable ledger/events and restart recovery | Domain handlers plus `src/db/repo/` and `src/server_engine.c` | Event replay/ACK/resume and several multi-step journeys lack runtime evidence | No universal gap asserted without a protocol-specific trace | Partial |

## Additional correction in this pass

- Citadel construction now re-locks the planet's current owner and current
  corporation membership inside the same transaction that reserves upgrade
  resources and starts construction. Ownership/permission changes between the
  initial request checks and settlement can no longer spend resources for an
  unauthorized player. Database-backed concurrency verification remains
  pending.
- The citadel completion worker now treats its initial scan as advisory,
  re-locks and revalidates each due construction inside its completion
  transaction, checks the conditional state update affected exactly one row,
  and propagates transaction/commit failures. This preserves the documented
  due-time completion lifecycle under concurrent workers. Runtime cron
  verification remains pending.

- `planet.info` now includes `qCannonSector` and `qCannonAtmosphere` for an
  existing citadel. Nullable database values are exposed as zero; missing
  citadel configuration is distinguishable from a database query failure.
  See `repo_citadel_get_qcannon_state` and `cmd_planet_info`. Build and source
  contract tests are the available verification here; database-backed runtime
  verification remains outstanding.

- Police bribe and surrender now lock the current incident row inside a
  transaction, fail closed on read/write/commit errors, and include banned-only
  incidents in the active-state check. Rejected bribes preserve their
  documented response payload. See `src/server_police.c`,
  `src/db/repo/repo_clusters.c`, and `src/server_envelope.c`. Sanitized build
  and focused source-contract tests pass; socket/database runtime verification
  remains pending.
- `police.status` now reports a database error instead of incorrectly returning
  `banned: false` when the ban-state read fails. `ship.status` now returns the
  documented authentication error for unauthenticated callers and reports an
  unavailable database before making repository calls. These paths are covered
  by focused source-contract tests; protocol runtime verification remains
  pending.
- Planet market sales and immediate illegal-commodity purchases now use a
  guarded atomic inventory decrement. Sale proceeds, purchase cargo/credits,
  planet treasury changes, and taxable activity are committed together; any
  failed settlement leg rolls back instead of returning a false success.
  Immediate purchases now check available inventory rather than wrongly
  requiring free storage on the planet; only standing orders require free
  planet capacity.
  Build and focused source-contract verification pass; database-backed protocol
  verification remains pending.
- Corporate planet-market commands now revalidate current corporation
  membership while holding the planet and membership rows in the transaction.
  Buy-order placement repeats ownership, cash and capacity checks before
  inserting the order. The current protocol/schema does not escrow funds or
  reserve planet capacity; settlement rechecks destination capacity, so
  cancellation and expiry have no reserved balance or space to release. There
  is no client-facing commodity-order cancellation request in the V3 protocol;
  existing cancellation is the internal market-profile cleanup path.
- Cargo writes lock the authoritative ship row before validating total holds
  and mutating `ship_cargo`; ship transfers, player trade offers and Ferengi
  deals lock both hulls in ascending ID order. Cargo transfers revalidate
  active ship/ownership/location and sync both legacy cargo views in one
  transaction. Backend commit failure leaves rollback available.
- Market matching retains unfinished fills as `open` (the persisted status
  constraint has no `partial` value), restores in-memory fill counters on
  rollback, handles commit failures, carries inventory location separately
  from account actor, and bounds fills by destination capacity. Port/planet
  stock changes serialize on entity rows; decrements are conditional to prevent
  overselling. Build and focused contract tests pass; database-backed matching
  verification remains outstanding.
- Planet-market buy orders are unreserved, best-effort orders. Placement stores
  a configured expiry without debiting credits or reserving capacity. Matching
  locks and revalidates source/destination inventory and buyer balance, limits
  each fill by stock, price, credits and capacity, settles inventory/credits/
  fills atomically, permits partial fills, and cancels an unexecutable remainder.
  Players can cancel their own open orders while retaining control of the
  destination planet. Expiry is persisted and enforced across restarts. The
  SysOp setting defaults to 86400 seconds and accepts 60–604800 seconds. Focused
  source-contract tests and a JSON acceptance scenario cover placement,
  cancellation, configuration, partial settlement and expiry; database-backed
  runtime verification remains outstanding because it was explicitly out of
  scope for this work.

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

The source changes compile in the PostgreSQL build (`make -C bin -j2`) with
ASan/UBSan enabled, and scoped `git diff --check` is clean. Focused
runtime/state-transition verification remains outstanding. A bounded check of
the documented QA environment found no PostgreSQL response on loopback port
55438 and no game listener on 12483; the documented PostgreSQL data directory
`/tmp/twclone-v3-qa-20261028-c2/pgdata` is absent in this execution environment.
No database startup, migration, fixture load, or regression suite was run.

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

## Gameplay contract amendments awaiting approval

- **Ship sales — AWAITING APPROVAL:** Define sale location and price/fee formula;
  treatment of cargo and installed equipment; whether active/last ships may be
  sold; whether sale destroys the hull or returns it to stock; and insurance or
  ownership-record consequences. Until then, the existing sale stubs cannot
  safely settle credits or ship state.
- **Planet harvesting — AWAITING APPROVAL:** Define harvestable resources,
  amount/yield formula, costs/turns, storage destination and capacity rules,
  eligibility, and whether harvesting changes planet production or ownership.
  `planet.harvest` currently supplies only `planet_id`; canon #491/V3-CAN-00
  does not resolve these rules.
- **`planet.view` — AWAITING APPROVAL:** Define morale source and production
  units/meaning, and resolve whether landing grants access to treasury fields
  that `planet.info` currently restricts to owners/corporation members. The
  existing NYI example does not settle these visibility semantics.
- **Online-player listing — AWAITING APPROVAL:** Reconcile prose `{page,limit}`
  and `{players,pagination}` with executable `{offset,limit,fields}` and
  `{total_online,returned_count,offset,limit,players}` schemas. Define the
  allowed `fields` values and which profile/location/economic details are
  public. The implemented listing therefore exposes only session presence,
  player id and username pending that decision.
- **Bank standing orders — AWAITING APPROVAL:** Define schedule representation
  and timezone, permitted recipients and authorization, execution cadence,
  failure/retry/cancellation semantics, and idempotency. The existing table
  and processing TODO do not define these player-visible rules.
