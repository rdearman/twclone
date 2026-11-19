# V3 gameplay and acceptance status

Reviewed: 19 November 2026. This is a concise current summary; detailed test
logs and transactions belong in the linked acceptance reports. Do not infer
that an entire subsystem is verified from a focused scenario.

**Parity refresh — 19 November 2026:** 55 VERIFIED, 5
IMPLEMENTED_UNVERIFIED, 6 PARTIAL, 1 BLOCKED, 0 MISSING. Autonomous batch 15
verified `INS-01`, `INS-02`, and `INS-03`. Disposable PostgreSQL acceptance
passed 22/22 for policy eligibility, insured ship destruction, guaranteed
underinsured settlement capped at hull replacement value, escape-pod
independence, and claim idempotency; 40/40 for sale termination without a
premium refund; and 22/22 for the separate economy/insurance rollback and
retry workflow. Python insurance workflow tests passed 4/4, the combined
Python client suite passed 834/834, and Godot 4.5.1 client regressions passed
106 with 6 live-server-only scripts skipped. The Godot command menu passed
240 checks, including insurance purchase/list/claim dispatch. Detailed clause
evidence is in the matrix. Autonomous batch 14
verified `BANK-05`. Disposable PostgreSQL acceptance passed 41/41 for ordinary
create/list/cancel; 21/21 for cancellation racing settlement; 24/24 for
injected settlement rollback and retry; 15/15 for 106 active orders without a
cap; and 24/24 across monthly settlement and serving-server restart. Python
standing-order workflow/menu checks passed 14/14 and Godot command-menu
regression passed 238 checks. The four exact acceptance conditions and
persisted-state assertions are recorded in the clause matrix. Autonomous
batch 13 verified `MOVE-03` and `MOVE-04`: a live disposable-PostgreSQL transwarp
journey returned `transwarp_complete`, persisted destination and exact fuel
debit, and created no interruption event. The response and interruption
assertions passed in `suite_movement_move_transwarp.json` (24/24); transaction
atomicity is backed by the prior movement-hazard injected rollback acceptance.
`test_transwarp_server_contract.py` passed 6/6. Exact clause evidence is in
the matrix. Autonomous batch 12
verified `POP-02`. Live disposable PostgreSQL suites passed 47/47 capture
cases and 14/14 neutral slave-trade cases. They confirmed evil enslavement,
good liberation, neutral inheritance, neutral enslavement refusal without
combat/population mutation, and neutral slave-purchase refusal without
changing credits, turns, cargo or port stock. This exposed a server response
defect: the capture event recorded `enslaved_colonists`, but the combat
response omitted it. The server now returns the authoritative outcome;
Python and Godot validate/present it, and both expose the player's choice.
Python planet-attack tests passed 9/9, Godot command menu passed 238 checks,
planet response passed 117, and presenter passed 13. Exact acceptance cases
are in the clause matrix. Autonomous batch 11
verified `PARITY-TW-HOMING-01`. Live disposable PostgreSQL acceptance passed
23/23 cases: absent destination fighter refusal left fuel, turns and location
unchanged; transwarp succeeded after a destination fighter was deployed, with
the exact 3 ORE debit and persisted arrival. Python navigation/server tests
passed 30/30 and Godot's command-menu regression passed 238 checks. The
transaction-order contract test confirms the reference lock precedes fuel,
turn and location mutation. Exact case references are in the clause matrix.
Autonomous batch 10 verified `MOVE-01` and `MOVE-02`. Three live disposable-PostgreSQL suites
passed 91 cases across normal warp, transwarp and arrival hazards. The expanded
hazard suite verifies hostile mine/fighter/limpet damage and event persistence,
friendly-asset exclusion, immediate turn/state effects and lethal escape-pod
recovery. A controlled database fault during transwarp arrival proves that
location, turn, fuel, hull and hostile assets roll back together. Python
navigation/hazard tests passed 36/36; the Godot command menu passed 238 checks
for warp and transwarp dispatch; the transwarp contract suite passed 4/4. The
acceptance exposed only a fixture limitation: raw SQL permits one prepared
statement per request, so the rollback fixture was split into separate setup
actions. No gameplay assertion was relaxed. Exact evidence is in the matrix
and autonomous progress log. Autonomous batch 9
verified `BANK-01`. A 41-case live PostgreSQL suite demonstrated daily,
weekly and monthly schedule persistence at their specified UTC times, existing
player recipients, ISO weekday endpoints/rejection, and monthly day-31
retention. The existing 24-case scheduled monthly restart acceptance confirms
day 31 survives a short-month execution and server restart. The client checks
found standing-order creation inaccessible in Python and incorrectly wired in
Godot; both now provide the full V3 schedule fields, with validation for
weekly/monthly conditional fields. Python workflow tests passed 31/31 and
Godot's command-menu suite passed 236 checks. Exact evidence is in the clause
matrix and autonomous progress log. Autonomous batch 8
verified `SHIP-05` through a 35-case live PostgreSQL used-hull and sale
acceptance. The purchase retained the original hull identity, condition and
equipment, transferred primary ownership and active-ship identity atomically,
deducted the exact 80% configured valuation once, removed the inventory row,
and rejected a retry. The same suite confirmed standalone sales still pay the
independent 60% rate. The acceptance exposed and fixed a server transaction
defect: used-hull purchase transferred primary ownership but did not update
the player's active `ship_id`, violating the deferred ship-owner invariant.
Python menu/workflow and Godot shipyard UI regressions pass; exact references
are in the clause matrix and autonomous progress log. Autonomous batch 7
verified `PLANET-01` through a 78-case live PostgreSQL suite. It covers the
read-only public overview, detailed owner view, dynamic population/production/
morale data, treasury visibility for owners and corporation members, refusal
for an unrelated remote player, and state snapshots proving the view changes
no planetary accounting. Python and Godot now expose the public `planet.view`
separately from detailed `planet.info`. Exact references are in the matrix.
Autonomous batch 6
verified `PLAYER-01`: live acceptance covered duplicate sessions, ascending
pagination, invalid/empty pages, exact public profile fields and JSON-null
corporation state. It exposed and fixed a server response defect: PostgreSQL
NULL was serialized as an empty string. The Python communications menu now
exposes Online Players, and Godot menu dispatch is tested. Exact evidence is
in the clause matrix. Autonomous batch 5
verified `POP-06` after exposing the already implemented emancipation command
through landed-planet workflows in Python and Godot. Disposable PostgreSQL
acceptance covered player ownership, corporation Leader/Officer access, Member
refusal, whole-population and allocation conservation, zero turn/credit cost,
and injected after-update rollback (67/67 suite cases). Python workflow tests
passed 82/82 and Godot command-menu tests passed 218 checks. Exact evidence is
in the clause matrix and autonomous progress log. Autonomous batch 4 verified
concurrent Exchange settlement (`EXC-05`) with two live servers sharing a
disposable PostgreSQL database. Autonomous batch 3
verified Ferengi and Orion NPC population reconciliation and the Ferengi fleet
catalogue (`NPC-POP-01` through `NPC-POP-03`) against disposable PostgreSQL,
including full BigBang roster evidence and serving-server restart. The focused
run found and fixed Orion initialization defects: the routine passed the owner
player ID where a corporation ID was required, and it assigned the new ship
without updating the captain's sector. Exact clause evidence is in the matrix.
Autonomous batch 2 promoted
`CFG-03`, `SHIP-02`, `SHIP-04`, and `POP-04` from focused
disposable-PostgreSQL acceptance. The market test exposed fixture contamination
from unrelated higher-priority ORE orders; its fixture now isolates the book
and invokes the actual settlement handler through deterministic QA scheduling.
Partial fill, insufficient-credit cancellation and retry idempotency passed.
Ship valuation/catalogue and last-ship checks passed; injected sale-audit
failure confirmed atomic switch/sale rollback. POP-04 detail responses and
production preserve LOW slave morale, the 50% output rule, and no invented
colonist morale. Autonomous batch 4 verified EXC-05 by concurrently running
the actual market settlement handler on two isolated game-server processes
against one disposable PostgreSQL database. Both selected the same due orders;
row-lock revalidation committed one trade, with exact stock, credit and order
state assertions. The live acceptance passed 53/53 cases. Exact evidence is in
the matrix and progress log.
Autonomous batch 1 promoted
`CFG-01`, `CFG-02`, `CFG-04`, `EXC-04`, and `V3-PHOTON-DURATION-01` from
focused disposable-PostgreSQL acceptance. The clause matrix records exact
case references; test evidence includes ship-sale configuration separation,
fixed slave-output policy and production, insurance hull payout/rollback,
Exchange settlement without physical ships, and persisted photon expiry.
Godot 4.5.1 parser failures in `persisted_planet_restore_test.gd` and
`planet_command_response_test.gd` were traced to an uninferred Boolean in
`Main.gd`; after explicit typing they passed 8/8 and 117/117 checks.
Batch 3 promoted
`HARV-02`, `HARV-03`, `BANK-04`, `TRADE-01`, and `NPC-FER-01` from focused
disposable-PostgreSQL acceptance. Batch 2 promoted
`POP-03`, `SHIP-03`, `BANK-02` and `V3-TOLL-RATE-01` from executed disposable
PostgreSQL acceptance. `POP-07` remains IMPLEMENTED_UNVERIFIED: its final
clause is a traceable design/scope constraint (“No corporation-capture rule
is inferred”), not an executable prohibition. The acceptance matrix requires
runtime evidence for VERIFIED, so documentary evidence does not promote it and
a negative test would invent a runtime rule. The completed batch and exact
test references are recorded in the clause matrix.
`BANK-03` is now
VERIFIED: a day-31 monthly standing order settled once through the registered
database scheduler, retained day 31 for its next recurrence, and remained
visible with its original day after a serving-server restart. This acceptance
also fixed scheduler-time propagation and PostgreSQL `smallint` JSON typing.
`V3-PLANET-RATE-01` is also VERIFIED after migrated ratio defaults were
asserted and per-commodity worker remainders persisted across restart and
produced exactly one unit per ORE/ORG/EQU after three actual scheduler ticks.
`SYS-07` is VERIFIED by a PostgreSQL-backed player-kick test that confirms the
durable session is deleted, the active connection is closed, and the old token
fails on a new connection without automatic reauthentication.
`SHIP-01` is VERIFIED by 37 live PostgreSQL cases across StarDock,
black-market and inventory-authorized ship-sale facilities, including durable
sale accounting and duplicate-payout rejection.
The Orion targeting
decision remains PARTIAL: its 60-case PostgreSQL run passed, while safe-zone,
protected-player, NPC-removal and genuine concurrent-arrival cases remain open.
The approved ruleset 1.2 cycle unrest and Ferengi destruction reward passed
focused PostgreSQL acceptance. `NPC-FER-01` is VERIFIED by authenticated
attack/wanted-list/pursuit/retry/destruction tests (45/45) and a serving-server
restart/reconnect replay (6/6).
POP-05's daily recurrence, stoppage-duration and population-loss numeric policy
values remain the sole rules blocker. Exact evidence and limits are in the
clause matrix and [`V3_RULESET_1_2.md`](V3_RULESET_1_2.md).


Batch 3 evidence: `HARV-02` 40/40 confirms owner/Officer harvest, visitor/Member
refusal, unchanged unauthorized state, and injected rollback. `HARV-03` 27/27
confirms Exchange sale, rollback and sold-stock exclusion from harvest; a server
restart/reconnect phase passed 8/8. `BANK-04` scheduler phases passed 17/17 and
11/11 across a real serving-process restart. `TRADE-01` no-haggle/fixed quote
passed 9/9, market-shock expiry passed 12/12, and deterministic settlement
passed 43/43. Python planet workflow tests passed 79/79; Godot command menu
passed 212 checks with Exchange sale dispatch covered. `POP-07` stays
IMPLEMENTED_UNVERIFIED because its no-corporation-capture statement documents
scope but does not define executable behavior; `SHIP-01` remains VERIFIED with
its prior client-menu evidence. See the clause matrix for exact case IDs.

The current requirement-by-requirement reconciliation is
[`V3_FINAL_GAMEPLAY_PARITY_CLOSURE_20261116.md`](testing/V3_FINAL_GAMEPLAY_PARITY_CLOSURE_20261116.md).
It supersedes stale implementation-status remarks in older focused reports,
while preserving their historical test evidence.

## Release-level regression results

The latest recorded gates are:

| Area | Result | Scope |
|---|---:|---|
| Server protocol regression | 2,625 passed, 0 failed, 0 skipped | All 166 suites; disposable PostgreSQL; TLS suite separately configured and run |
| Python client | 810 passed | `python3 -m pytest -q tests` in `client/python_client` |
| `tests.v2` | 274 passed, 7 skipped, 227 subtests passed | `python3 -m pytest -q tests.v2` |
| Godot 4.5.1 | 106 passed, 0 failed, 6 skipped | 112 scripts discovered by `suite_client_regressions.py`; skipped scripts require live acceptance mode; includes explicit typing repair |
| Godot command menu | 206 checks passed | Menu regression |
| Planet response | 117 checks passed | Response, planet and sector identity regression |

The full server regression reported **2,621 passed, 0 failed** across 165
suites; the aggregate runner skipped its four TLS cases because TLS listener
configuration is enabled only for explicitly selected TLS suites. The separate
TLS-enabled run passed all 4 cases, accounting for **166/166 suites and 2,625
passed cases with zero failures**. A
focused live test verifies one suspicion point per successful lawful robbery,
wanted promotion at the contract threshold, persistence after reauthentication,
and persistence after restarting the serving game process. See
[`V3_POLICE_ROBBERY_ACCEPTANCE_20261116.md`](testing/V3_POLICE_ROBBERY_ACCEPTANCE_20261116.md).

The same run passed the slave production-cycle unrest suites (20/20), POP-05
daily stoppage/loss/recovery and rollback suites (30/30 across a serving-server
restart), Ferengi destruction reward and restart suites (41/41), Orion
arrival/restart suites (60/60), and deterministic black-market restock
acceptance (34/34). POP-05 concurrent scheduler invocation remains untested. The
30-day actual-scheduler simulation passed 64/64 invariant checks; it does not
establish a quantitative economy balance result.

## Player-facing acceptance

The 16 November live Godot journey against disposable PostgreSQL exercised
authentication, navigation and scanning, port trading, hardware purchase,
shipyard upgrade, planet landing/harvest/launch, a corporation operation, and
logout/reconnect. SQL confirmed persisted player, ship, planet and corporation
state. It fixed and regression-tested stale cargo in an open port workflow.
See [`testing/V3_GODOT_LIVE_JOURNEY_ACCEPTANCE_20261116.md`](testing/V3_GODOT_LIVE_JOURNEY_ACCEPTANCE_20261116.md).
Earlier Godot journeys exercised registration and feedback queueing. Python journeys exercised
ordinary-player workflows, including StarPort construction replay after a
server restart. Refer to
[`testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`](testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md),
[`testing/V3_STARPORT_ACCEPTANCE_20261114.md`](testing/V3_STARPORT_ACCEPTANCE_20261114.md),
and [`testing/V3_PLAYER_FEEDBACK_ACCEPTANCE_20261114.md`](testing/V3_PLAYER_FEEDBACK_ACCEPTANCE_20261114.md)
for exact evidence and limits.

That journey is a verified subset, not a complete continuous V3 gameplay loop.
Combat/destruction/recovery, planetary capture and citadel progression, and the
full corporation transfer workflow still lack live UI evidence through both
clients. The latest parity-closure report lists the journey gaps separately.
The robbery fix is verified at the server protocol/database level but does not
by itself close any of the 67 decision-register requirements.

## Mechanics with focused acceptance evidence

### Additional clause-level acceptance from the `b3fcb7d9` work batch

- **Transwarp toll arrival:** on the disposable PostgreSQL V3 server, normal
  toll quotes were reused for transwarp arrival. Acceptance covered offer-before-
  movement, one-time payment/fuel/turn settlement, accepted-request replay,
  insufficient-credit rollback and funded retry, self/same-corporation
  exemptions, and refusal combat with one-time damage. Test IDs are listed in
  [`V3_CLAUSE_ACCEPTANCE_MATRIX_20261116.md`](testing/V3_CLAUSE_ACCEPTANCE_MATRIX_20261116.md).
- **Planet population transport:** `planet.colonists.set` now requires the
  active ship landed at the target and applies ship cargo and planet population
  changes atomically through the authoritative cargo layer. A PostgreSQL suite
  confirms remote refusal, pickup/drop-off conservation, and full rollback when
  a drop-off would mix colonists with slave population. This does not verify
  unrelated acquisition policy in `POP-02`. A separate same-database
  test stopped and restarted the serving V3 server between suites, then
  authenticated the ordinary player, read the persisted population and
  completed another conserving transfer.
- **Unpopulated planet acquisition:** ordinary-player capture, database-failure
  rollback, conservation of empty population/allocation state, commodity stock
  and citadel treasury, player↔corporation ownership transfer, former-owner
  denial, and authenticated planet readback after a serving-server restart
  passed in disposable PostgreSQL. The prepare and restart verification suites
  executed **57 cases, 0 failed**, including a stale competing-claim conflict
  injected at the capture compare-and-set boundary. The failed capture rolled
  back the conflicting ownership update and fighter debit. Focused client
  response checks passed for
  Python (**9 tests**) and Godot (**117 checks**). This closes the `POP-01`
  acquisition transition clause; the separate `POP-02` purchase and capture
  disposition clauses remain independently classified.
- **Production numeric rules:** the disposable database confirmed all 21
  built-in class/commodity rate rows for the seven approved noncanonical V3
  defaults. This verifies migrated configuration values; it does not verify
  every scheduled production tick, persisted remainder, or class-rate override.
- Focused result: four PostgreSQL suites **136 passed, 0 failed**; production
  rate migration suite **1 passed, 21 assertions**; the expanded colonist
  transfer and rate retest **27 passed**, including quantity/hold boundaries;
  server restart population suites **20 passed**; Python client plus two
  focused server contracts **866 passed**; Godot 4.5.1 toll workflow **13
  checks passed**; existing slave-delivery/harvest/fighter/emancipation suite
  **49 passed**; engine regression script passed. These are focused gates, not
  the full server regression suite.
- Defects fixed in this batch: transwarp did not request/settle toll offers;
  colonist transfer lacked a non-Terra landed-ship check and split population
  mutation from cargo mutation; the old path wrote only legacy ship cargo and
  left authoritative `ship_cargo` stale.

- **StarPorts:** construction retry after lost reply and server restart,
  scheduled deliveries across serving-process restarts, selected rollback
  boundaries, concurrent upgrades, upgrade/attack serialization, and
  post-restart requests passed for the documented cases. This is not exhaustive
  fault-injection coverage. See the two StarPort reports above and the
  [release-candidate audit](testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md).
- **Photon missiles:** focused disposable PostgreSQL firing, eligibility,
  ammunition, retry and expiry cases are recorded in
  [`testing/V3_PHOTON_ACCEPTANCE_20261114.md`](testing/V3_PHOTON_ACCEPTANCE_20261114.md).
- **Toll fighters:** focused payment, refusal, insufficient funds and retry
  cases are recorded in
  [`testing/V3_TOLL_FIGHTER_ACCEPTANCE_20261114.md`](testing/V3_TOLL_FIGHTER_ACCEPTANCE_20261114.md).
- **Planet production:** ratio/default and production acceptance is recorded
  in [`testing/V3_PLANET_PRODUCTION_RATE_ACCEPTANCE_20261114.md`](testing/V3_PLANET_PRODUCTION_RATE_ACCEPTANCE_20261114.md).
- **Player feedback:** PostgreSQL queueing, ownership, redaction and mock
  publisher paths are recorded in
  [`testing/V3_PLAYER_FEEDBACK_ACCEPTANCE_20261114.md`](testing/V3_PLAYER_FEEDBACK_ACCEPTANCE_20261114.md).
  Live public GitHub publication has not been tested.

## Remaining limits

- The release-candidate audit does not clear external deployment. Credential
  rotation, host/database exposure ownership, designated QA authentication and
  deployment-specific checks remain blockers.
- Live GitHub issue publication is unverified; local queue-only reporting is
  available and tested.
- Focused subsystem evidence is not proof of every original TW2002 edge case.
  Consult [`V3_GAMEPLAY_PARITY.md`](V3_GAMEPLAY_PARITY.md) and the approved
  [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md) for scope and open
  parity gaps.
- Full V3 server runtime on MySQL is not supported or established; PostgreSQL
  is the verified runtime target.

## Evidence policy

Use `VERIFIED` only for the operation and conditions recorded in an executed
acceptance report. Keep implementation, test-fixture, focused runtime and
deployment claims separate. The report date and commit identify the source
baseline; newer source changes require relevant tests to be rerun.
