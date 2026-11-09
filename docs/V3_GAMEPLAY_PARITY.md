# V3 gameplay parity checklist

Canonical product decisions: [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md); Galactic Exchange contract: [`V3_GALACTIC_EXCHANGE_CONTRACT.md`](V3_GALACTIC_EXCHANGE_CONTRACT.md).

Scope: approved twclone V3 requirements and the project's pinned TradeWars
player-facing scope (see `docs/V3_STATUS.md` and issue #491). This is a source
and contract inventory, not runtime certification. “Implemented” means a code
path exists; end-to-end behavior still needs focused protocol verification.

| Subsystem | Required | Implemented | Partial | Missing | Status |
|---|---|---|---|---|---|
| Player lifecycle | Register, authenticate, persist identity/session, recover state; list currently connected players | `src/server_auth.c`, `src/server_players.c`, `src/db/repo/repo_auth.c` | Online listing deduplicates authenticated players, orders by player ID and exposes only ID/username/corporation/alignment via page/limit; runtime verification pending | Session replay/client recovery remains unverified | Partial |
| Universe and navigation | Generated connected sectors/warps; movement, scans and route assistance | `src/bigbang_pg_main.c`, `src/server_universe.c`, `src/server_autopilot.c` | Interrupted-transwarp fuel/origin/no-destination-hazard rule is approved; implementation/runtime verification outstanding | No new policy blocker | Partial; see `V3_STATUS.md` |
| Ships and lifecycle | Ownership, acquisition, status, upgrades/equipment, repair, cargo, claim/tow, sale | `src/server_ships.c`, `src/server_stardock.c`, `src/db/repo/repo_ships.c`, `src/db/repo/repo_stardock.c` | Standalone sale now atomically settles credits, cargo forfeiture, insurance termination, active replacement, audit and persistent used-hull inventory; runtime verification pending | Used-hull purchase has no approved request, valuation or settlement contract | Partial |
| Trading/economy | Directional port trade, authoritative quotes, cargo/credits settlement, persistent market orders | `src/server_ports.c`, `src/server_planets.c`, `src/server_cron.c`, `src/db/repo/repo_ports.c`, `src/db/repo/repo_market.c` | Planet buy orders use unreserved best-effort matching, revalidate funds/capacity, permit partial fills, cancel unexecutable remainder and support cancellation/expiry; fixed current authoritative port prices | Price haggling is explicitly not part of V3; configured dynamic pricing remains authoritative | Implemented in source; runtime unverified |
| Planets/colonies | Genesis, landing, transfers, worker assignment, production, harvest, population, slavery, emancipation and citadels | `src/server_planets.c`, `src/db/repo/repo_planets.c`, `src/db/repo/repo_cron.c` | `planet.view_v1` is allow-listed, includes current per-cycle ORE/ORG/EQU output and owner/corporation-only treasury; emancipation is transactional; harvest is capacity-aware and transactional under existing withdrawal permissions; population migration and slave output logic present; capture conversion is transactional | Runtime verification; slave purchase/transport; unrest parameters and effects remain outstanding | Partial |
| Combat/defences | Ship/planet combat, fighters, mines, shields, destruction and recovery | `src/server_combat.c`, `src/server_planets.c`, `src/db/repo/repo_combat.c`, `src/db/repo/repo_ships.c` | All destruction callers converge on an atomic transaction that destroys hull/cargo/equipment, creates and assigns an Escape Pod at sector 1, records loss/podded state and a per-hull idempotency event; funded eligible player insurance pays configured policy payout capped at shiptype basecost to bank; lethal movement reports pod sector | Insolvent/missing insurance fund leaves an open claim; guarantee under insolvency needs policy. Runtime verification pending | Partial |
| Corporations | Membership, roles, shared assets, treasury and governance | `src/server_corporation.c`, `src/db/repo/repo_corporation.c` | Membership events and advanced settlement require verification | Tax/dividend RPCs are explicitly not registered; stock subcommands have a stub fallback | Partial |
| Banking/finance | Balances, deposits, withdrawals, transfers, statements, interest and standing orders | `src/server_bank.c`, `src/server_engine.c`, migrations 172/124 | `bank.orders.create/list/cancel` and durable due processing are implemented in source; monthly day clamps to month end while retaining configured day; insufficient funds skip, invalid recipients cancel, and each occurrence has a unique journal row | JSON cases authored; migration, scheduler restart, concurrency and ledger runtime verification pending | Partial |
| Stations/services | StarDock, shipyard, tavern, hardware and police interactions | `src/server_stardock.c`, `src/server_police.c`, `src/server_communication.c` | Atomic sale persists the used hull and audit; runtime verification pending; police/station journeys need end-to-end evidence | Used-hull purchase lifecycle awaits request and valuation contract | Partial |
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

Continue with source-confirmed gameplay gaps whose behavior is explicit in the
approved protocol. Runtime verification of recent transactions remains pending.

## Explicit specification blockers

- `V3-CAN-00` / issue #491 still requires a pinned TradeWars edition for any
  parity question not resolved by approved V3 policy. V3 has now explicitly
  ruled out haggling and defined hull-only insurance, escape-pod destruction,
  planet-capture population policy and interrupted-transwarp behavior.
- Planet harvest permission for landed non-owners remains unspecified; partial
  transfer to available ship capacity is approved.
- Slave unrest interval, stoppage duration and fixed loss values require
  explicit SysOp configuration; deterministic allocation of losses among
  unassigned/working populations is still undefined. Unrest must remain
  inactive until resolved.
- Used-hull acquisition request, price and settlement remain undefined.

## Implemented in the current work batch

- `planet.view` is registered as a read-only, allow-listed overview. It reuses
  existing visibility/landing authorization; only the owner or an authorized
  corporation member receives the planet treasury. JSON acceptance cases
  cover owner treasury visibility and landed-visitor redaction. Runtime
  verification remains pending.
- `planet.emancipate` performs an owner/authorized corporation management
  conversion under a locked planet row and locked membership role, preserves
  population/allocation columns, and clears outstanding slave unrest fields in
  the same transaction. Focused behavioral contract assertions were added to
  the existing planetary JSON suite; actual protocol/database execution is
  pending.

## Gameplay contract amendments awaiting approval

- **Used-hull purchase — SPECIFICATION REQUIRED:** Define request, price,
  eligibility and settlement before used inventory can be acquired.
- **Planet harvesting — PARTIAL CONTRACT:** Resources, landed-ship destination,
  zero turns, conservation, atomicity and partial transfer up to available
  ship capacity are approved. Permission for landed non-owner/corporation
  actors remains unspecified and must follow existing authorization rules.
- **Slave unrest — POLICY VALUES/LOSS ALLOCATION REQUIRED:** No interval,
  stoppage duration or fixed loss quantity is selected. The deterministic rule
  for reducing assigned versus unassigned workers is also unspecified; keep
  unrest inactive.
- **Planet morale display — PARTIAL CONTRACT:** Slave morale is permanently
  low, but numeric encoding and colonist morale source are not defined. Do not
  invent numeric morale. Planet overview treasury visibility is owner or
  authorized corporation-member only.
