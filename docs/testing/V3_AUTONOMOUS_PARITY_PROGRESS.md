# V3 autonomous parity progress

Updated: 19 November 2026

## Batch 18 — regression isolation and notice concurrency

Starting parent classification: **58 VERIFIED, 2 IMPLEMENTED_UNVERIFIED, 6
PARTIAL, 1 BLOCKED**. The five named first-batch requirements (`CFG-01`,
`CFG-02`, `CFG-04`, `EXC-04`, and `V3-PHOTON-DURATION-01`) were already
verified in the committed matrix; no evidence was duplicated and no status
changed.

The two prior Godot background tests were not running. Their exact scripts
passed again (`persisted_planet_restore_test.gd`: 8/8;
`planet_command_response_test.gd`: 117 checks). No process was terminated.

The full disposable server run discovered 174 suites, executed 173, and
reported **2,857 passed, 8 failed, 4 skipped** (2,865 executed cases). Four
market-suite failures were test-environment defects: the test hard-coded
parallel ports 12530/12531 instead of using runner ports, and Exchange
ring-fenced ORE inventory could outrank the intended port sell order. The JSON
runner now accepts a second server port and a per-request `server: "second"`
selector; the market acceptance snapshots and temporarily clears Exchange
ORE, then restores it. Its real-port and exact stock/credit assertions remain
intact. The focused fresh-PostgreSQL run passed
`suite_planet_market_buy_order_lifecycle.json` **54/54**, including both
actual V3 server processes in the concurrent settlement case. A sequential
market then fixed-port run passed **54/54 and 28/28**, confirming fixture
isolation.

The other four failed cases are known context/order problems in the generic
all-suites invocation: the BigBang roster case runs after the generic rig has
replaced BigBang data, and the restart-verification suite runs before its
population-preparation suite and without a serving-process restart. Each
scenario previously passed in its required isolated context; they were counted
as separate ordered acceptance phases in the comprehensive gate below.

The corrected regression ran in four contexts: **170/170 general suites,
2,834/2,834 cases; BigBang roster, 1/1 suite and 2/2 cases; NPC target plus
serving-server restart verification, 2/2 suites and 36/36 cases; TLS, 1/1 suite
and 4/4 cases**. Combined result: **174/174 suites, 2,876 passed, 0 failed,
0 skipped**. The contexts are necessary because the BigBang roster must run
before the generic fixture rig, NPC verification must follow its preparation
and a real game-server restart, and TLS uses a dedicated configuration.

The full-run server log also exposed a real concurrent-notice defect:
`db_notice_mark_seen` caught a duplicate-key error from a competing system
notice delivery but PostgreSQL had already aborted the caller's transaction.
It now uses the database driver's conflict-safe `INSERT ... DO NOTHING`
fragment. The live `suite_chat_mail_news.json` passes **30/30**, including
simultaneous acknowledgements from two authenticated sessions and the
single-row persistence assertion. No duplicate-key or aborted-transaction
error appeared in that focused server log. `make -j4` passed; `pytest -q
tests.v2` passed **277**, skipped **7**, and passed **227 subtests** when run
with local socket access. The full Python/Godot client runner passed Python
**834/834** and Godot **106 passed, 0 failed, 7 skipped** (the seven live
server scripts require explicit live acceptance mode); all 113 Godot scripts
were accounted for. Python/Godot gameplay classifications did not change.

No parent requirement is promoted by this regression work. Next: close the
remaining Orion safe-zone/protection/race acceptance without inventing a
player protection rule absent from the approved contract.

## Starting checkpoint

- Branch: `v3-development`
- Starting commit: `a0cf9339a5dcfcf8d0105362ac0ea522b852dfc9`
- Starting parent classification: 27 VERIFIED, 32 IMPLEMENTED_UNVERIFIED, 7
  PARTIAL, 1 BLOCKED.
- Existing worktree changes were preserved. The pre-existing changes to
  `Makefile`, `config.status`, the deletion of
  `tests.v2/regression_report.json.tmp`, and untracked
  `docs/V3_PLAYABILITY_AUDIT_20261113.md` were not staged or edited.
- No Godot process for `persisted_planet_restore_test.gd` or
  `planet_command_response_test.gd` was running when checked; no process was
  terminated. The previous test backtrace was a real GDScript parser failure:
  Godot could not infer the type of `sale_receipt_valid` in `Main.gd`. An
  explicit `bool` annotation fixed it.

## Batch 1 — complete

Requirements attempted and promoted:

- CFG-01 — ship-sale configuration.
- CFG-02 — fixed slave production multiplier.
- CFG-04 — hull-only insurance configuration and payout.
- EXC-04 — immediate Exchange delivery without physical virtual vessels.
- V3-PHOTON-DURATION-01 — configurable photon suppression duration.

Executable evidence:

- `suite_ship_sale_v3.json`: 19/19 passed.
- `slave_unrest_production_prepare.json`: 15/15 passed.
- `slave_unrest_production_verify.json`: 8/8 passed.
- `suite_economy_insurance.json`: 22/22 passed, including injected transaction
  rollback and duplicate-claim retry.
- `suite_planet_taxable_activity_e2e.json`: 44/44 passed, including a database
  snapshot proving no physical ship state changed during Exchange delivery.
- `suite_photon_duration_acceptance.json`: 15/15 passed.
- Latest focused disposable-PostgreSQL runner artifact:
  `/tmp/twclone_autonomous_batch1_final_norestart.json` reports 118 passed,
  zero failed. Later isolated reruns of insurance and Exchange passed 22/22 and
  44/44 respectively after adding rollback and physical-ship snapshots.
- Python: 810 passed.
- Godot 4.5.1: 106 passed, 0 failed, 6 skipped (live-server acceptance only).
- `persisted_planet_restore_test.gd`: 8 passed; `planet_command_response_test.gd`:
  117 checks passed.

## Godot background-process check

The two named Godot scripts were not running at the start of this continuation.
No processes were terminated. The prior parser backtrace was already resolved:
the progress record above shows `persisted_planet_restore_test.gd` 8/8 and
`planet_command_response_test.gd` 117 checks passed. They are not outstanding
failures; both remain included in the Godot regression suite.

The `--restart-between-suites` attempt is not counted as passing: after the
harness restarted the serving server, AddressSanitizer reported a startup
failure in `load_bootstrap_config` at `getenv`. Photon expiry restart evidence
from the earlier dedicated acceptance remains valid; this batch did not
complete restart-between-suites acceptance. A subsequent broad client command
was accidentally invoked without the required runner environment and piped to
`head`; its partial output is not a valid test result and is not counted.

Classification after batch 1: **32 VERIFIED, 27 IMPLEMENTED_UNVERIFIED, 7
PARTIAL, 1 BLOCKED**. Clause counts: 89 VERIFIED and 138 still
IMPLEMENTED_UNTESTED across 227 base rows.

No gameplay server defect was found in these five requirements. The Godot
parser failure was corrected. The JSON assertion runner gained a
`not_contains` operator for the fixed-setting negative assertion.

## Batch 17 — StarPort acceptance closure

Starting classification: **55 VERIFIED, 5 IMPLEMENTED_UNVERIFIED, 6 PARTIAL,
1 BLOCKED**. Ending classification: **58 VERIFIED, 2 IMPLEMENTED_UNVERIFIED,
6 PARTIAL, 1 BLOCKED**. Base clause rows moved from 164/63 to **169 VERIFIED / 58
IMPLEMENTED_UNTESTED**.

Promoted `PARITY-PORT-ATTACK-01`, `V3-PORT-BUILD-01`, and
`V3-PORT-COMBAT-01`. Evidence:

- `manual_acceptance/starport_godot_event_acceptance.json`: 3/3, plus the
  authenticated Godot attack UI journey: 8 checks and authoritative PostgreSQL
  receipt/event/inactive-port/stock assertions.
- `suite_starport_class_directions.json`: 7/7 against disposable PostgreSQL;
  all eight fixed port classes completed by the actual deterministic scheduler.
- `suite_starport_combat_numeric.json`: 12/12 against disposable PostgreSQL;
  defence strength, attack formula, turn charge, retaliation layers and
  persisted durability were checked.
- `manual_acceptance/starport_godot_build_acceptance.json`: 3/3, plus
  `live_starport_build_test.gd`: 8 Godot UI/network checks. The test waited for
  authoritative planet reads and verified class-8 construction and exact
  credit/material deductions in PostgreSQL.
- `command_menu_test.gd`: 244 checks; `planet_landing_response_test.gd`: 91
  checks; `tests.v2/test_deterministic_scheduler_contract.py`: 3 passed;
  `make -j4` passed.

One genuine defect was fixed: the successful port-construction cron handler
returned its processed-row count, which the scheduler treated as failure after
commit. It now returns the documented success value. The initial Godot build
failure was test timing: the client correctly disabled menu actions while
planet detail requests were outstanding; the live test now waits for them.
No gameplay assertion was relaxed.

Detailed acceptance: `docs/testing/V3_STARPORT_GODOT_ACCEPTANCE_20261119.md`.

### Evidence policy item

POP-07's corporation-capture sentence is a documentary scope constraint; it
does not define a forbidden runtime action. No behavior-inventing negative
test was added. The matrix requires runtime evidence for VERIFIED, so POP-07
remains IMPLEMENTED_UNVERIFIED. Proposed minimal policy: allow explicitly
scope-exclusion clauses to cite approved contract evidence labeled `CONTRACT`,
separate from `RUNTIME`; do not silently promote until the policy is accepted.
SHIP-01 remains VERIFIED; its previous Godot menu and Python sale workflow
evidence covers the contract's player-facing access requirement.

## Batch 2 — completed 19 November 2026

Requirements selected from the smallest remaining gaps:

- CFG-03 — VERIFIED.
- SHIP-02 — VERIFIED.
- SHIP-04 — VERIFIED.
- POP-04 — VERIFIED.
- EXC-05 — sequential clauses VERIFIED, parent remains
  IMPLEMENTED_UNVERIFIED pending concurrent PostgreSQL settlement/order-lock
  acceptance required in `V3_GAMEPLAY_DECISIONS.md`.

Executed evidence:

- `suite_planet_market_buy_order_lifecycle.json`: 36/36.
- `suite_ship_sale_v3.json`: 27/27.
- `manual_acceptance/ship_sale_atomic_rollback.json`: 19/19.
- `suite_slave_unrest_config_defaults.json`: 12/12.
- `slave_unrest_production_prepare.json`: 19/19.
- `slave_unrest_production_verify.json`: 10/10.
- Total: 123/123 disposable-PostgreSQL cases across six suites.
- `test_ship_sale_equipment_catalog.py` plus JSON assertion runner tests:
  9 passed, 17 subtests passed.

The first EXC-05 run failed because other open ORE orders could consume the
seeded port stock before the intended player buyer. This was a fixture
isolation failure. The test now cancels unrelated ORE orders, disables
unrelated cron tasks in the disposable database, and advances the actual
settlement handler through the deterministic test scheduler. A second
acceptance run proved the exact two-unit fill, remainder cancellation, balance
and stock conservation, and no duplicate trade after scheduler retry. No
server behavior or expected result was relaxed.

Classification moved from 32 VERIFIED / 27 IMPLEMENTED_UNVERIFIED / 7
PARTIAL / 1 BLOCKED to **36 VERIFIED / 23 IMPLEMENTED_UNVERIFIED / 7 PARTIAL /
1 BLOCKED**. All 18 base clauses for the five selected requirements have
executed evidence; EXC-05 parent promotion awaits concurrency acceptance.

## Next batch candidates

Next candidate set, pending exact-clause and evidence review:

1. EXC-05 — concurrent order settlement and order-lock acceptance.
2. NPC-POP-01 — Ferengi population target, stable roster and reconciliation.
3. NPC-POP-02 — Orion population target, stable roster and personality/class
   preservation.
4. NPC-POP-03 — NPC-only Ferengi ship classes and deterministic allocation.
5. BANK-05 — standing-order execution/cancel races and uncapped order count.

These are candidate selections, not verified results. Existing evidence will
be reused only if it exercises each mandatory clause. No deployment,
production or release work is in scope.

## Batch 8 — SHIP-05 used-hull purchase

`SHIP-05` is VERIFIED. The live disposable-PostgreSQL
`suite_ship_sale_v3.json` passed **35/35**. It confirmed the default 8000-bp
used resale factor, formula over hull plus retained-equipment valuations,
ordinary-player list and purchase, exact credit deduction, original ship ID,
hull condition and installed equipment retention, ownership/active-ship
transfer, inventory removal, and refusal of a duplicate purchase. It also
confirmed the separate 6000-bp standalone-sale payout and its exact ledger/audit
valuation.

The live test exposed a server defect: purchase transferred primary ownership
but left `players.ship_id` pointing at the old hull. The deferred active-owner
constraint rolled the transaction back. `repo_stardock_purchase_used_ship`
now updates the active ship in the same transaction. After this fix the full
sale suite passed. Python workflow/menu regressions passed **71/71**; the
Godot command-menu suite passed **231 checks**; `make -j4` and
`tests.v2/test_planetary_server_contract.py` passed (**54/54**). A first live
attempt before the server fix failed at the invariant and is recorded as defect
discovery, not acceptance evidence.

Classification moved from 43 VERIFIED / 16 IMPLEMENTED_UNVERIFIED / 7 PARTIAL
/ 1 BLOCKED to **44 VERIFIED / 15 IMPLEMENTED_UNVERIFIED / 7 PARTIAL / 1
BLOCKED**. No database migration was needed. Python and Godot both expose the
used inventory at the shipyard, require player confirmation, submit the
inventory identifier, and rely on an authoritative matching server receipt.

## Batch 9 — BANK-01 standing-order schedules and client access

`BANK-01` is VERIFIED. `suite_economy_and_bank.json` passed **41/41** against
disposable PostgreSQL. It exercised daily, weekly and monthly order creation
between two ordinary players; SQL confirmed the exact persisted UTC hour and
minute, recipient, ISO Monday=1 and Sunday=7 values, rejection of weekdays 0
and 8, and monthly `month_day=31`. Existing `bank_monthly_retention_prepare`
and `bank_monthly_retention_restart_verify` evidence passed **24/24** across a
serving-server restart, confirming day 31 remains configured after a
short-month settlement.

Client inspection found real access defects: Python offered order listing but
no create workflow, while Godot sent `recipient_id` and omitted required
`time_utc`, weekday and month-day fields. Python now has an interactive,
validated standing-order flow and menu entry. Godot sends canonical
`recipient_player_id` and `time_utc`, conditionally requires ISO weekday or
month day, and omits the irrelevant field. Python tests passed **31/31** and
Godot command-menu tests passed **236 checks**.

Classification moved from 44 VERIFIED / 15 IMPLEMENTED_UNVERIFIED / 7 PARTIAL
/ 1 BLOCKED to **45 VERIFIED / 14 IMPLEMENTED_UNVERIFIED / 7 PARTIAL / 1
BLOCKED**. No server behavior defect or schema migration was required.

## Batch 3 — NPC population and Ferengi fleet acceptance

The live repository already recorded completion of batches 1 and 2. The
current batch closed the five next candidates from that queue, including the
three population/fleet requirements. Starting from the recorded 36 VERIFIED /
23 IMPLEMENTED_UNVERIFIED / 7 PARTIAL / 1 BLOCKED, the ending classification is
**39 VERIFIED / 20 IMPLEMENTED_UNVERIFIED / 7 PARTIAL / 1 BLOCKED**.

### Requirements verified

- `NPC-POP-01`: target formula and minimum, 250 distinct stable Ferengi names,
  repeated reconciliation without duplicate identities, sentinel combat-state
  preservation, and state after serving-server restart.
- `NPC-POP-02`: Orion target and minimum, 100 distinct stable names in the full
  BigBang world after a server restart, five configured Orion ship classes,
  effective personality precedence, and captain/ship sector consistency.
- `NPC-POP-03`: five NPC-only Ferengi ship classes, exact deterministic
  100/75/40/25/10 allocation over 250 traders, and preserved Warship catalogue
  values.

### Executed evidence

- Ordinary isolated PostgreSQL fixture: `suite_npc_population_targets_v3.json`
  **32/32** and `suite_npc_population_restart_verify_v3.json` **4/4**. The
  serving game process restarted between suites.
- Full migrated BigBang isolated PostgreSQL world:
  `suite_npc_population_targets_v3.json` **32/32**,
  `suite_npc_population_restart_verify_v3.json` **4/4**, and
  `suite_npc_population_bigbang_roster_v3.json` **2/2**, with a server restart
  between suites.
- Python focused regressions: `test_v3_faction_rosters.py` and
  `test_mysql_migration_portability.py`: **18 passed, 206 subtests passed**.
- `make -j4`: passed.

### Defects fixed

- Orion initialization passed the owner player's ID to a repository routine
  that requires the Orion corporation ID, causing member-link failure. The
  initializer now resolves and passes the corporation ID separately.
- Orion ship deployment updated the captain's active ship but not the
  captain's sector. The active-sector consistency trigger exposed the mismatch;
  deployment now updates both fields in the same transaction.
- A diagnostic acceptance query treated SQL `NULL` text as integer `0` through
  the SysOp raw-SQL serializer. The acceptance now checks effective personality
  using the approved ship override, corporation default, ship-type default,
  balanced fallback precedence. No migration was added for a defect that the
  database evidence did not show.

Changes remain uncommitted at this progress update. The three newly verified
clauses are recorded in the matrix and decision register. The next candidates
are `EXC-05` concurrent settlement, then the smallest outstanding bank/economy
and NPC behavior clauses; the separate Orion safe-zone/protected-player,
removal-race, and concurrent-arrival clauses remain open.

## Batch 4 — EXC-05 concurrent market settlement

`EXC-05` is now VERIFIED. Its existing sequential acceptance covered distinct
placement/reservation/execution, open orders, partial fills, conservation and
retry idempotency. The new acceptance runs two real V3 server processes on
separate loopback ports against one fresh disposable PostgreSQL database. A
controlled trigger holds the first settlement transaction after candidate
selection; the second server enters the same scheduler path while that
transaction is open. Both logs selected the same due buy/sell orders. Final
authoritative SQL checks confirmed exactly one trade, one fill, the expected
order completion, port/planet quantities and credit balances.

Evidence: `suite_planet_market_buy_order_lifecycle.json` passed **53/53**
cases, including the two-server race and final SQL assertions. Focused Python
runner tests passed **31/31**; `py_compile`, `jq empty`, and `git diff --check`
passed. The ordinary sandbox attempt to start disposable loopback PostgreSQL
was denied before cluster identity verification; the same isolated acceptance
was then run with the authorized local-test access and passed. No persistent
server changes were required.

The batch adds opt-in `parallel_requests` support to the JSON acceptance runner
and optional second isolated server startup to `run_regression_instance.py`.
Default single-server test behavior is unchanged. Parent classification after
the batch is **40 VERIFIED, 19 IMPLEMENTED_UNVERIFIED, 7 PARTIAL, 1 BLOCKED**.

The next candidates are the smallest remaining runtime evidence gaps visible
in the matrix: `POP-06` slave-to-colonist conversion/access/accounting, then
`PLAYER-01` pagination and privacy, followed by other small behavior slices.
`BANK-05` order-execution/cancel concurrency and Orion safe-zone/protected,
removal-race, and concurrent-arrival clauses remain larger targeted items.

## Batch 5 — POP-06 emancipation and player-facing workflows

The server already implemented the approved owner/Leader/Officer emancipation
transaction, but neither client exposed the action. Added a confirmation-based
Python surface menu workflow and a Godot landed-planet action. Both bind only
the current server-confirmed planet; Python validates the response type and
planet identity and refreshes authoritative planet state after success.

Disposable PostgreSQL acceptance: `suite_planet_fighter_transfers.json`
passed **67/67**, including owner, corporation Leader and Officer success,
Member and visitor refusal, full population/allocation conservation, zero turns
and credits, and an AFTER UPDATE injected failure that rolled the population
type and allocations back. Python planetary workflow tests passed **82/82**.
Godot 4.5.1 command-menu regression passed **218 checks**, including confirmed
emancipation dispatch with the selected planet ID. An initial attempt to drive
multiple DDL commands in one prepared SQL request failed because PostgreSQL
rejects multiple commands in a prepared statement; the fixture was corrected
to execute each setup statement separately. A subsequent fixture attempt
violated the corporation's single-Leader constraint; the test now preserves
the corporation-created Leader and seeds only Officer/Member roles. The final
run is fully green; no server defect was found.

POP-06 was promoted, moving the count from 40 VERIFIED / 19
IMPLEMENTED_UNVERIFIED to **41 VERIFIED / 18 IMPLEMENTED_UNVERIFIED**, with 7
PARTIAL and 1 BLOCKED. The base clause ledger is now 119 VERIFIED / 108
IMPLEMENTED_UNTESTED. Next I am checking the small online-player listing
requirement (`PLAYER-01`); prior regression notes record conflicting page/limit
and offset/fields schemas, so promotion depends on reconciling that published
contract rather than assuming the old fixture or implementing a guess.

## Batch 6 — PLAYER-01 online player directory

The current V3 protocol, request/response schemas and server implementation
agree on one-based `page`/`limit`, unique authenticated players, ascending
player-ID order, and a four-field public profile. Old recovery notes claiming
an offset/fields mismatch were stale. Live acceptance found one genuine defect:
the PostgreSQL driver returned a non-NULL empty string for a SQL NULL
corporation value unless the result null bitmap was checked. The server now
uses `db_res_col_is_null()` and emits JSON null for unaffiliated players.

`suite_player_status.json` passed **12/12** against disposable PostgreSQL.
It issued concurrent same-session requests on separate sockets and asserted
de-duplication, ordering, exact allowlisted keys, one-based pagination, empty
terminal page, invalid page refusal and null corporation. Python normal-menu
reachability passed in `test_menu_gating.py` (**17 passed**); Godot
communications menu dispatch passed in `command_menu_test.gd` (**221 checks**).
`make -j4` passed, and focused JSON runner tests passed **32/32**.

`PLAYER-01` is promoted to VERIFIED. Classification moved from 41 VERIFIED /
18 IMPLEMENTED_UNVERIFIED to **42 VERIFIED / 17 IMPLEMENTED_UNVERIFIED**, with
7 PARTIAL, 1 BLOCKED; base clause rows now stand at 123 VERIFIED / 104
IMPLEMENTED_UNTESTED. The next small candidate is `PLANET-01` visibility and
response privacy, then `SHIP-05` used-hull pricing/identity and insurance
policy clauses. Any runtime result will be interpreted against the current
protocol and code rather than stale recovery summaries.

## Batch 7 — PLANET-01 public and detailed read workflows

The prior clients exposed only detailed `planet.info`; the approved public
read-only `planet.view` was absent from Python and Godot surface menus. Added a
Python surface workflow that validates response type/planet identity and
prints only the public field allowlist, and a distinct Godot read-only overview
action. Detailed `planet.info` remains available as its own action. Python menu
and response tests passed **101/101**; Godot command-menu regression passed
**226 checks**.

`suite_planet_fighter_transfers.json` passed **78/78** against disposable
PostgreSQL. It proves the view does not change stocks, population, allocations,
planet defenses or treasury using before/after SQL snapshots; owner/Leader and
corporation Member access; landed visitor's no-treasury projection; unrelated
remote player's refusal; current population and production values; slave LOW
morale; and detailed `planet.info` ownership response. The fixture's daily
planet growth changed population between earlier read and emancipation checks,
so read assertions now compare with an immediate authoritative snapshot
instead of a hard-coded stale population value. A second setup issue was a
citadel fixture owner ID incorrectly using a corporation ID where the schema
requires a player ID; the QA fixture was corrected. Neither required a
production server change.

`PLANET-01` is promoted to VERIFIED. Classification moved from 42 VERIFIED /
17 IMPLEMENTED_UNVERIFIED to **43 VERIFIED / 16 IMPLEMENTED_UNVERIFIED**, with
7 PARTIAL, 1 BLOCKED; base clauses are now 127 VERIFIED / 100
IMPLEMENTED_UNTESTED. Next I am selecting another minimal gap from the matrix;
candidate `SHIP-05` covers used-hull identity/equipment and pricing, with
`INS-01`/`INS-02` settlement clauses also available for focused acceptance.

## Batch 10 — synchronous movement and arrival hazards

`MOVE-01` and `MOVE-02` are VERIFIED. Live disposable-PostgreSQL acceptance
passed `suite_movement_move_warp.json` **13/13**,
`suite_movement_move_transwarp.json` **20/20**, and the expanded
`suite_combat_sector_hazards.json` **58/58**. It demonstrated pre-movement
destination/fuel refusals, exact one-hop transwarp fuel and turn accounting,
arrival at destination, normal and transwarp entry mine/fighter/limpet effects,
persistent combat events, hostile asset consumption, friendly/owner exclusion,
and lethal transwarp escape-pod lifecycle.

The rollback case injects a database error during transwarp mine damage and
asserts location, turns, ORE fuel, hull and destination assets all remain at
their pre-request values. Python navigation/hazard tests passed **36/36**;
Godot `command_menu_test.gd` passed **238 checks**, including warp and
transwarp dispatch; `test_transwarp_server_contract.py` passed **4/4**. The
first rollback-fixture attempt sent multiple SQL statements through one
prepared request; it was split into single-statement setup actions and rerun
green. This was a fixture correction, not a gameplay change.

Classification moved from 45 VERIFIED / 14 IMPLEMENTED_UNVERIFIED / 7 PARTIAL
/ 1 BLOCKED to **47 VERIFIED / 12 IMPLEMENTED_UNVERIFIED / 7 PARTIAL / 1
BLOCKED**. `MOVE-03` and `MOVE-04` remain unverified because their remaining
mandatory clauses describe future interruption/scope policy rather than
independent runtime gameplay actions; source inspection alone does not promote
them.

## Batch 11 — transwarp homing reference

`PARITY-TW-HOMING-01` is VERIFIED. The transwarp suite was extended to begin
with a valid destination and fuel but no homing fighter. The ordinary player
was refused with error 1458; PostgreSQL readback confirmed that ORE, turns and
sector were unchanged. After deterministic fixture setup deployed a fighter
at the destination, the same player's one-hop transwarp succeeded and consumed
exactly 3 ORE. The expanded disposable-PostgreSQL suite passed **23/23**.
`tests.v2/test_transwarp_server_contract.py` and Python navigation workflows
passed **30/30**; the Godot 4.5.1 command-menu suite passed **238 checks**,
including transwarp action dispatch. The contract assertion verifies the
reference is locked before fuel, turn and location mutations. No gameplay
server defect was found.

The insurance suite was also executed (**22/22**), but this evidence is not
enough to promote INS-01/02/03: it uses a fixture-inserted claim rather than a
qualifying hull destruction and does not establish complete client-accessible
insurance purchase/claim workflows. Those parents remain unverified pending
the missing acceptance, rather than being promoted from a partial scenario.

Classification moved from **47 VERIFIED / 12 IMPLEMENTED_UNVERIFIED / 7
PARTIAL / 1 BLOCKED** to **48 VERIFIED / 11 IMPLEMENTED_UNVERIFIED / 7
PARTIAL / 1 BLOCKED**. Clause totals are **129 VERIFIED / 98
IMPLEMENTED_UNTESTED**. No unrelated files were staged.

## Batch 12 — population capture choices and neutral acquisition limits

`POP-02` is VERIFIED. The existing live capture suite was extended with a
neutral player's explicit `enslave_colonists=true` refusal and a before/after
snapshot of planet owner, slave count/type, planet fighters, player turns and
ship fighters. The refusal left all values unchanged; the same neutral player
then captured the slave planet without conversion. Evil enslavement and good
liberation assertions remain in the same ordinary-player server flow.
`suite_combat_advanced.json` passed **47/47** against disposable PostgreSQL.
A new suite, `suite_neutral_slave_trade.json`, passed **14/14** and verified
that a neutral player's slave purchase is refused while credits, turns, ship
cargo and port stock remain unchanged. It is included in
`suite_regression_full.json`.

The acceptance exposed a genuine protocol defect: the capture event already
recorded `enslaved_colonists`, but the successful response omitted the field.
The server now returns the authoritative Boolean. Python's attack flow asks
for the choice and validates/presents the result. Godot exposes a checkbox,
validates the response field and presents it. The published V3 response schema
was updated. Python planet-attack tests passed **9/9**; the full Python client
suite passed **830/830** with loopback socket permission. Godot command menu
passed **238 checks**, planet response passed **117**, presenter passed **13**;
the full Godot 4.5.1 client suite passed **106**, **0 failed**, **6 skipped**
out of 112 scripts. The six skips are live database journeys not enabled by
that offline runner.

An initial neutral-purchase fixture used an invalid raw SQL CTE/update shape;
it was corrected to choose the port first, then move player and ship using the
saved sector. This was a fixture issue; the final acceptance rerun passed.
Classification moved from **48 VERIFIED / 11 IMPLEMENTED_UNVERIFIED / 7
PARTIAL / 1 BLOCKED** to **49 VERIFIED / 10 IMPLEMENTED_UNVERIFIED / 6
PARTIAL / 1 BLOCKED**. Base clause totals are **131 VERIFIED / 96
IMPLEMENTED_UNTESTED**.

## Batch 13 — synchronous transwarp interruption contract

Verified `MOVE-03` and `MOVE-04`. The contract specifies synchronous
transwarp completion and no interruption unless a canonical trigger is
approved; the current V3 implementation has no arbitrary random interruption
path. Extended the disposable PostgreSQL transwarp suite to require the
`transwarp_complete` response and assert that the successful movement creates
no interruption event. The suite passed **24/24**, including prior fuel,
destination, missing-homing-reference and persisted-arrival checks. The
movement hazard rollback acceptance remains the executable transaction
atomicity evidence. `tests.v2/test_transwarp_server_contract.py` passed **6/6**.
No gameplay defect was found. Classification moved from **49 VERIFIED / 10
IMPLEMENTED_UNVERIFIED / 6 PARTIAL / 1 BLOCKED** to **51 VERIFIED / 8
IMPLEMENTED_UNVERIFIED / 6 PARTIAL / 1 BLOCKED**; clause totals moved from
**131 VERIFIED / 96 IMPLEMENTED_UNTESTED** to **136 VERIFIED / 91
IMPLEMENTED_UNTESTED**. The policy-dependent POP-05 blocker is unchanged.

The next small acceptance candidates are `INS-01`, `INS-02` and `INS-03`
insurance settlement requirements, followed by `BANK-05`. Existing insurance
evidence uses a fixture-inserted claim and does not prove ordinary-client
purchase plus qualifying destruction, so it cannot promote the insurance
parents by itself.

## Batch 14 — BANK-05 standing-order settlement

Verified all four BANK-05 clauses using ordinary players and disposable
PostgreSQL. The existing `suite_economy_and_bank.json` passed **41/41** for
create/list/cancel and owner checks. A new actual parallel-request scenario
`bank_order_cancel_race.json` passed **21/21**; it raced authenticated
cancellation against the registered scheduled settlement and verified either
legal serialization, exact balances and ledger legs, then replayed the same
occurrence to check idempotency. `bank_order_settlement_rollback.json` passed
**24/24**, injecting a failure while writing the occurrence after account
mutations; SQL confirmed full rollback, after which the deterministic task was
requeued and the same occurrence settled exactly once. `bank_order_no_cap.json`
passed **15/15**, with 105 seeded active orders, ordinary creation of a 106th,
page-two visibility and authoritative count. Existing monthly prepare/restart
acceptance passed **17/17 + 7/7** across an actual serving-server restart and
confirmed one paid occurrence and two ledger legs.

Python standing-order/menu checks passed **14/14** and Godot 4.5.1
`command_menu_test.gd` passed **238 checks**, including standing-order create
dispatch. The first rollback run exposed two acceptance-harness issues rather
than a game defect: the test correctly received scheduler error 1004 on the
injected fault, and the scheduler advanced its task cursor after reporting
that failed run. The final case now expects that error, requeues the still
unsettled deterministic occurrence, and verifies success on retry. A stale
Python source-contract anchor also referenced an obsolete C function
signature; it was updated to the current signature, after which the focused
contract test passed **1/1**. No gameplay code was changed.

Classification moved from **51 VERIFIED / 9 IMPLEMENTED_UNVERIFIED / 6
PARTIAL / 1 BLOCKED** to **52 VERIFIED / 8 IMPLEMENTED_UNVERIFIED / 6
PARTIAL / 1 BLOCKED**. Base clause totals are **140 VERIFIED / 87
IMPLEMENTED_UNTESTED**. Next, close another independently executable narrow
gap; insurance remains a candidate only after qualifying destruction and
client-accessible policy purchase/claim are exercised, rather than relying on
fixture-inserted claims.

## Batch 15 — insurance lifecycle and both client workflows

Verified `INS-01`, `INS-02`, and `INS-03`. The actual disposable-PostgreSQL
`suite_combat_and_crime.json` passed **22/22**: ordinary players bought two
hull policies, underwent a real ship destruction, received the exact 300 CRD
underinsured payout plus 700 CRD up to the 1,000 CRD hull cap, retained the
independent Escape Pod recovery, and produced identifiable once-only
`INSURANCE_GUARANTEE` ledger entries despite an empty insurer reserve.
`suite_economy_insurance.json` passed **22/22**, including invalid eligibility,
atomic rollback, and retry idempotency. `suite_ship_sale_v3.json` passed
**40/40** and confirmed voluntary sale deactivates the policy without refund.

Python now exposes list, hull-policy purchase, and claim actions; focused
workflow tests passed **4/4**. Godot exposes the same menu actions and dispatch
coverage; `command_menu_test.gd` passed **240 checks**. Full Python client
regressions passed **834/834**. The Godot 4.5.1 regression runner passed
**106**, failed **0**, skipped **6** live-server-only scripts; catalogue
validation reported 13 assets and zero warnings. The first sandboxed Python
run showed six socket `EPERM` failures; rerunning with local socket permission
passed the affected 12/12 tests and the complete client suite. This was an
execution-environment restriction, not a code defect.

No gameplay server defect was found. Classification moved from **52 VERIFIED
/ 8 IMPLEMENTED_UNVERIFIED / 6 PARTIAL / 1 BLOCKED** to **55 VERIFIED / 5
IMPLEMENTED_UNVERIFIED / 6 PARTIAL / 1 BLOCKED**. Base clause totals moved
from **140 VERIFIED / 87 IMPLEMENTED_UNTESTED** to **153 VERIFIED / 74
IMPLEMENTED_UNTESTED**. Exact evidence is recorded against the thirteen
insurance clauses in the matrix.

## Batch 16 — Federation ISS boundaries

Extended `suite_fedspace_aggression_captainz.json` with ordinary-player and
authoritative PostgreSQL checks for ISS jurisdiction. Against a fresh
disposable PostgreSQL/server on unique local game/S2S ports, the suite passed
**49/49**; `suite_port_operations.json` passed **9/9** in the same run. The
full focused run completed **58/58**, with zero failed suites. Assertions
prove Captain Z handles a FedSpace attack without creating an ISS incident,
an unprotected outside-FedSpace target creates none, and a target registered
as an enabled protected Federation ship creates one incident naming the exact
target and origin. The first attempt failed before auth because the default
S2S listener port could not bind; rerunning on unique ports resolved the
environment collision. A separate attempt with an invalid full-path suite
argument did not execute tests.

`tests.v2/test_starport_sector_and_pta_contract.py` passed **5/5**. No
gameplay defect was found. `FED-03` clauses 1 and 2 now have live acceptance
evidence; clause 3 remains unverified because the approved decision register
excludes ISS pursuit from this bounded work while Protocol V3 and the running
implementation explicitly define pursuit. No rule was changed or inferred.
Parent classifications are unchanged.

### Reconciled existing StarPort acceptance evidence

The already executed StarPort reports were mapped to clause rows instead of
rerunning unchanged gameplay. `PARITY-PORT-ATTACK-01` clause 2 is VERIFIED:
the live PostgreSQL checks separate capacity upgrades from port durability.
Clause 1 remains open because the preserved live transcript contains the
canonical request and response but not delivery of `player.port_strike.v1`.
The parent remains IMPLEMENTED_UNVERIFIED.

`V3-PORT-BUILD-01` clauses 1, 2, 4, 5 and 6 now cite existing live PostgreSQL
evidence for initial costs, scheduled material deliveries, capacity upgrades,
planet source materials, ship cargo deductions and port credit receipts. Its
class-specific exchange-direction-at-completion clause remains unverified, so
the parent is not promoted.

`V3-PORT-COMBAT-01` now has evidence for the 10,000 initial durability,
destruction stock clearing/inactive history, and FedSpace Captain Z enforcement.
The detailed default defence/damage and exact 1,000 retaliation clauses remain
open. The previous concurrent report recorded a 100 retaliation value for the
test port, while the current approved ruleset says 1,000; this may be an
explicit fixture override, but the acceptance evidence does not establish
that. No rule was changed and the parent remains unverified.

Classification remains **55 VERIFIED / 5
IMPLEMENTED_UNVERIFIED / 6 PARTIAL / 1 BLOCKED**; clause coverage advances to
**164 VERIFIED / 63 IMPLEMENTED_UNTESTED**.

## Batch 17 — StarPort strike event presentation

Fixed a client integration defect in the `player.port_strike.v1` event path.
Python and Godot had no event-category mapping for the server's `player.*`
StarPort strike event; Godot therefore discarded the event from its typed
event presentation path. Both clients now classify it as combat, and Godot
shows only authoritative damage, retaliation, remaining durability and the
server's destroyed flag. The live Godot acceptance script now also requires
the resulting activity-feed entry.

Focused Python protocol-event tests passed **11/11**; Godot event-presenter
regression passed **25 checks**; Godot 4.5.1 project import completed without
errors. Full Python client regression passed **834/834**. Full Godot regression
passed **106**, failed **0**, skipped **6** live-server scripts. The live
StarPort event assertion was not run against a serving server in this batch,
so the relevant `PARITY-PORT-ATTACK-01` event clause remains
IMPLEMENTED_UNTESTED. Parent and base-clause counts are unchanged from batch 16.

## Batch 19 — S2S shutdown safety and photon effect acceptance

Starting commit: `cd533c1168195df627abbdfc80ebe807f74a753b`.
Parent classification remains **58 VERIFIED, 2 IMPLEMENTED_UNVERIFIED, 6
PARTIAL, 1 BLOCKED**. The five first-batch IDs (`CFG-01`, `CFG-02`, `CFG-04`,
`EXC-04`, and `V3-PHOTON-DURATION-01`) were already VERIFIED in the committed
matrix; no duplicate work was counted.

The two named Godot test processes were absent. `persisted_planet_restore_test.gd`
passed 8/8 and `planet_command_response_test.gd` passed 117 checks; their prior
backtrace is not a current failing test.

The first focused-server invocation used the runner's default game port, which
was already occupied. The server's game listener failed to bind and ASan
exposed a genuine shutdown use-after-free: `s2s_close` freed a connection
still borrowed by the S2S reader thread. `server_main` now shuts down the
socket, joins its reader, then frees the connection; `s2s_transport` exposes
the shutdown-only operation for this ordered teardown. A controlled
disposable-PostgreSQL reproduction with an intentionally occupied, unique
loopback game port passed its lifecycle check: expected bind failure, normal
engine reap and no ASan/UBSan report. Five focused gameplay suites then passed
154/154 on unique loopback ports with no sanitizer report:
`suite_combat_advanced.json` 47/47, `suite_combat_citadel.json` 15/15,
`suite_combat_mines.json` 14/14, `suite_photon_duration_acceptance.json`
15/15, and `suite_planet_landing_defense.json` 63/63.

Added `suite_photon_defense_suppression.json`; it passed **37/37** against a
fresh disposable PostgreSQL server. It verifies both approved photon-firing
ship classes, refusal of an ineligible class without ammunition/effect
mutation, refusal against a planetary shield without ammunition debit,
temporary sector mine/fighter suppression, re-engagement after the persisted
effect expires, and citadel military-reaction suppression during an active
effect without mutating stored citadel configuration. The matrix now marks
clauses 1, 3 and 4 VERIFIED; clause 2 remains unverified as a whole because its
separate ship-shield impact behavior has not been established. The photon
parent remains PARTIAL; no parent was promoted.

The all-general-suite disposable PostgreSQL regression passed **171/171
suites, 2,871/2,871 cases**, with zero failures or skips using two isolated
game servers and distinct loopback game/S2S ports. Contextual acceptance also
passed: BigBang roster 2/2, NPC population plus real serving-server restart
36/36, and TLS 4/4. Combined server result: **175/175 discovered suites,
2,913 passed, 0 failed, 0 skipped**. Neither game-server log reported
ASan/UBSan errors. The test runner's first concurrent-server port selection
collided because its derived second S2S port equalled the second game port;
the corrected rerun used explicit distinct ports and passed. No gameplay
assertion or fixture expectation was changed.

Client regressions passed: Python **834/834**; `pytest -q tests.v2` **277
passed, 7 skipped, 227 subtests passed**; Godot 4.5.1 **106 passed, 0 failed,
7 skipped** across 113 scripts. The seven Godot skips require a live acceptance
fixture, not the mock/unit regression command. `git diff --check` passed.

No parent classification changed. The photon test adds three verified base
clauses and this batch verifies one POP-05 clause; current matrix totals are
173 VERIFIED / 54
IMPLEMENTED_UNTESTED. Remaining photon gaps are ship-shield impact semantics
and injected firing rollback. The S2S shutdown fix and photon suite were
committed separately as `97efd7fa` and `10004f56`.

### Batch 20 — Orion arrival and POP-05 scheduler concurrency

The two named Godot scripts had no associated active processes at the start of
the session. Their prior traces were not current failures: the full Godot
regression passed them again (`persisted_planet_restore_test.gd` 8/8 and
`planet_command_response_test.gd` 117 checks).

Expanded the Orion arrival acceptance so an allied player's ordinary warp and
a hostile player's transwarp execute concurrently through the serving V3
server. On a fresh disposable PostgreSQL database, the prepare/verify pair
passed **59/59 cases**. SQL readback confirmed both arrived, the eligible
hostile player received one 10-point hull loss, the allied player received no
damage, one blockade attack receipt completed, and retry in the same window
caused no second hit. The suites include authenticated re-login after a real
server restart. Safe-zone arrival, the precise scope of any additional
protected-player exclusion, and removal during combat settlement remain
unresolved; no new protection mechanic was inferred.

Expanded POP-05 daily acceptance to invoke `sys.cron.slave_unrest_once`
simultaneously from two authenticated sessions. The prepare and post-restart
verification suites passed **30/30 cases** on a fresh disposable PostgreSQL
database. SQL assertions confirmed one event, one four-person population
loss, proportional allocation conservation, idempotent replay, injected
rollback, and recovery after the serving server restarted. Clause
`POP-05`/2 is now VERIFIED; the parent remains BLOCKED by its still-unspecified
recurrence interval, stoppage duration, and population-loss policy values.

No server implementation defect was exposed and no gameplay code changed in
this batch. Clause evidence was updated without changing parent counts. The
Orion and POP-05 test/evidence edits are pending their own commit. Unrelated
worktree modifications remain untouched.

### Batch 21 — Concurrent sector-entry deadlock and Orion protections

Two real concurrent ordinary-player arrivals (warp and transwarp) against a
defended destination exposed PostgreSQL deadlocks. The entry paths acquired
the destination, transwarp homing fighter, deployed assets, and ship rows in
different orders; combat row loading also took `FOR UPDATE` locks on shared,
read-only ship-type catalogue rows. Movement now acquires a transaction-scoped
destination advisory lock before any mutable ship/asset locks. Entry-hazard
settlement uses the same lock inside its transaction, and combat locks only
mutable ship/ownership rows. MySQL lock-shape regression fixtures were kept in
sync; MySQL runtime support is not claimed.

The expanded live Orion arrival prepare/verify pair passed **80/80** against a
fresh disposable PostgreSQL server, including simultaneous warp/transwarp,
one hostile damage settlement, allied exclusion, replay idempotency, a
FedSpace/Orion overlap traversed through ordinary warp, and an NPC marked
destroyed before settlement. The ordinary player retained hull and no Orion
attack receipt was recorded for the safe-zone or removed-NPC cases. Serving
server restart/reconnect was exercised. Focused combat/movement/hazard
regressions passed **157/157** across five suites. Focused Python lock/schema
tests passed **7/7**.

The full 175-suite disposable PostgreSQL gate was executed after this server
change. Its initial generic-fixture phase ran 174 suites and reported 2,905
passed plus four failures in the known context-only BigBang roster and NPC
restart-verifier suites; TLS was skipped in that non-TLS listener context.
The exact cases were rerun in their required contexts without changing
assertions: BigBang roster **2/2** on the untouched BigBang world;
NPC-population preparation **32/32** followed by a real game-server restart
and restart verification **4/4**; TLS **4/4** on the dedicated TLS listener.
Aggregated by distinct suite/case after replacing those context-invalid
results, all **175/175 suites** are accounted for with **2,913 passed, 0
failed, 0 skipped**. The initial report is
`/tmp/twclone-parity-batch21-full.json`; context reports are under
`/tmp/twclone-parity-batch21-{bigbang,npc-restart,tls}*.json`.

Client regressions passed: Python **834/834**; `pytest -q tests.v2` **277
passed, 7 skipped, 227 subtests passed**; Godot 4.5.1 **106 passed, 0 failed,
7 skipped** across 113 scripts (the seven scripts require a live gameplay
server). The two requested historical Godot tests passed again in that run:
`persisted_planet_restore_test.gd` 8/8 and `planet_command_response_test.gd`
117 checks. `make -j4`, focused JSON parsing, and `git diff --check` passed.

`NPC-ORI-01` clause 1 is now VERIFIED, bringing the 227 base clause rows to
**174 VERIFIED / 53 IMPLEMENTED_UNTESTED**. Clause 2 (full retention of other
Orion personalities) remains unverified, so the parent is not promoted.
Parent counts are unchanged at **58 VERIFIED, 2 IMPLEMENTED_UNVERIFIED, 6
PARTIAL, 1 BLOCKED**. No policy decision was inferred. The implementation
and acceptance change is committed as `4e01e836`; this evidence update is
pending a separate documentation commit. Unrelated worktree modifications
remain untouched.

### Batch 22 — Orion personality runtime evidence

Added `manual_acceptance/orion_personality_runtime.json` and ran it against
the actual registered Orion scheduler on a fresh disposable PostgreSQL server.
It passed **9/9**. Authoritative SQL readback verified a balanced NPC moved
from its origin, an offensive NPC moved to its seeded intelligence target, a
defensive NPC moved along an existing outgoing warp, and a looter moved to a
seeded port sector. Each ship had exactly one completed durable movement
receipt. This is behavior-level acceptance in addition to the existing unit
tests for personality resolution/target selection; no personality rules were
changed.

Both `NPC-ORI-01` clauses are now VERIFIED. The parent classification advances
to **59 VERIFIED, 2 IMPLEMENTED_UNVERIFIED, 5 PARTIAL, 1 BLOCKED**. The base
clause count advances to **175 VERIFIED / 52 IMPLEMENTED_UNTESTED** of 227.
The suite
report is `/tmp/twclone-orion-personality.json`. Implementation/focused
acceptance is `4e01e836`; batch-21 evidence is `72b1aa60`; this classification
update and new suite are pending their bounded commit. No unresolved gameplay
policy was inferred.

### Batch 23 — Ship destruction side effects and recovery

Inspected only the two named stale Godot commands; neither process existed.
Reran `persisted_planet_restore_test.gd` (8/8) and
`planet_command_response_test.gd` (117 checks), both passing.

Extended `COMBAT-01` acceptance with ordinary-player PostgreSQL preparation
and post-restart verification suites. The first fixture run was invalid:
sector 11 contained pre-existing defenses that destroyed the attacker during
arrival, and corporation assets seeded in that same sector were consumed by
the encounter. The fixture now selects a sector without hazards/defenses and
seeds unrelated corporate assets outside it; no gameplay assertion was
relaxed. The corrected suites passed **34/34** and **9/9** cases, with a real
serving-server restart between them. They verify combat destruction without
escape-pod ship equipment, equipment/cargo loss, exact preservation of bank,
planet, secondary-hull, corporation treasury and defense assets, one valid
active escape pod, retry idempotency, and Terra enforcement. This acceptance
found a real defect: authoritative `ship_cargo` survived destruction.
`repo_ships_handle_destruction()` now deletes that inventory atomically with
the destroyed hull and pod transition.

Focused Python combat/client recovery tests passed **20/20**. Disposable
PostgreSQL combat regressions `suite_combat_advanced.json` and
`suite_combat_and_crime.json` passed **71/71**. A full generic-context server
run discovered 175 suites, executed 174, and reported **2,904 passed, 5
failed, 4 TLS cases skipped**. The five failures grouped into one scheduler
timing fixture (corporation shield charge transfer) and two suites requiring
their documented untouched-BigBang/restart contexts. The corporation fixture
now disables and restores shield regeneration around its exact charge tests,
and verifies the persisted charge/capacity after transfer; it passes **36/36**.
The NPC BigBang roster, initialization and actual restart verifier pass
**2/2**, **32/32** and **4/4** with a BigBang world and a real serving-server
restart. The dedicated TLS suite passes **4/4**. Aggregating each suite once,
all **175/175 suites** are covered with **2,912 passed, 0 failed, 0 skipped**;
the unmodified generic run and context replacements are retained separately
in `/tmp/twclone-parity-batch23-{full,corp,npc-context,tls}*`.

The specified first batch
(`CFG-01`, `CFG-02`, `CFG-04`, `EXC-04`, `V3-PHOTON-DURATION-01`) was already
fully VERIFIED with executable evidence in the matrix and requires no repeat
or implementation. `COMBAT-01` remains parent-VERIFIED; its seven detailed
clause rows now have explicit executable evidence. Parent counts are unchanged.

After enabling loopback access required by the test-local sockets, the full
Python client suite passed **834/834**. `pytest -q tests.v2` passed **277**,
with **7 skipped** and **227 subtests passed**. Godot 4.5.1 client regressions
passed **106**, failed **0**, with **7 live-server-only scripts skipped** by
the non-live runner. The two historical planet scripts passed again as noted
above. The full server was built with `make -j4`; `git diff --check` passed.

### Batch 24 — Photon rollback acceptance

Added `suite_photon_atomic_rollback.json` with a controlled PostgreSQL
failure on `photon_fire_requests` insertion, after the transaction has debited
ammunition and upserted the temporary sector effect. The duration, defense,
and new rollback suites passed **67/67** against a disposable PostgreSQL
database and the actual V3 server. The injected failure left ammunition
unchanged and created no effect, request, or event; after removing the fault,
the action succeeded with one debit and one durable effect/request/event.
This closes the transaction rollback evidence gap for photon firing. Together
with the prior 175-suite contextual regression result, the newly added photon
rollback suite brings distinct coverage to **176/176 suites and 2,927 passing
cases, zero failures or skips**; its two existing photon suites were rerun as
part of the same 67-case acceptance.

`PARITY-PHOTON-01` remains PARTIAL because the historical manual's ship-shield
protection statement lacks a corresponding V3 targeting contract: the
published `combat.fire_photon` request selects an adjacent sector and does not
identify a ship to hit. No ship-targeting behavior was invented. The exact
limitation is recorded in the clause matrix; no parent status changed. The
specified first batch is already VERIFIED, and the unresolved `POP-07` and
`FED-03` parent statuses remain scope-evidence and protocol-contract questions,
respectively, rather than safe implementation tasks.

### Batch 25 — Ferengi retaliation and reward parent verification

The decision register disagreed with both the clause matrix and completion
tracker: it still marked wanted-list/pursuit as untested although the prior
acceptance claimed those clauses passed. Reran the ordinary-player acceptance
against a fresh disposable PostgreSQL database: `ferengi_destruction_reward.json`
passed **45/45**, and after restarting the serving game server,
`ferengi_destruction_reward_restart.json` passed **6/6**. The live cases
demonstrated one wanted bounty on a nonlethal hostile attack, strongest-captain
hunt intelligence, no duplicate bounty/action on retry, exact 5,000-credit
system-generated reward to the authenticated eligible player, exclusion of a
human-owned Ferengi-corporation hull, and persisted once-only settlement after
restart. The existing Python/Godot full client runs include the ordinary
combat attack workflows.

The stale decision-register status is corrected and `NPC-FER-01` is promoted
from IMPLEMENTED_UNVERIFIED to VERIFIED. Current parent classification:
**60 VERIFIED, 1 IMPLEMENTED_UNVERIFIED, 5 PARTIAL, 1 BLOCKED**. The only
IMPLEMENTED_UNVERIFIED parent is `POP-07`; its remaining item is a documentary
scope constraint with no runtime rule, so the matrix retains the proposed
contract-evidence policy distinction and does not promote it without that
policy being accepted.

### Batch 26 — Planet capture through both clients

Starting classification: **60 VERIFIED, 1 IMPLEMENTED_UNVERIFIED, 5 PARTIAL,
1 BLOCKED**. Ending classification: **61 VERIFIED, 1
IMPLEMENTED_UNVERIFIED, 4 PARTIAL, 1 BLOCKED**. Three base clauses moved from
IMPLEMENTED_UNTESTED to VERIFIED.

Before proceeding, checked the two Godot scripts named by the previous
background terminals. Neither process was running, so no process was stopped.
`persisted_planet_restore_test.gd` passed 8/8; `planet_command_response_test.gd`
passed 117 checks. Their reported backtraces are not unresolved failures.

On isolated disposable PostgreSQL, `planet_capture_godot_prepare.json` passed
33/33 and `planet_capture_godot_restart_verify.json` passed 13/13 with a real
serving-server restart between phases. The actual Godot `Main.tscn` journey
used the PLANET command menu and real transport; `live_planet_capture_test.gd`
passed 10 UI/network checks. It captured a populated, defended planet,
confirmed the refreshed fighter/population outcome, and verified exact planet
stock, citadel treasury, owner balances, corporation treasury, unrelated
corporate sector asset, and one capture event after restart. A database CHECK
fault injected during a second capture failed closed and left ownership,
population, fighters, turns, treasury and event state unchanged.

The actual Python client acceptance `planet_capture_python_prepare.json`
passed 27/27. `planet_capture_python_journey.py` authenticated an ordinary
player, loaded the real `DEPLOYMENT_MAIN` menu, selected its `(P) Attack Listed
Planet` action and dispatched the production `combat_attack_planet_flow`
handler; all 5 client workflow assertions passed. PostgreSQL confirmed
authoritative capture, exact remaining fighters, conserved population
allocations and planetary stock/treasury, unchanged attacker and former-owner
balances, and exactly one capture and attack event. Its injected CHECK failure
also proved rollback of turns, fighters and capture/event state.

The new Python acceptance harness initially called `load_menus()` without its
required path and copied the Godot journey's fighter-count expectation despite
using a different fixture. These were acceptance-harness defects, corrected to
load `client/python_client/menus.json` and assert the fixture's 90 surviving
fighters. The successful rerun used a fresh disposable regression database.
No gameplay server defect was found.

Evidence and statuses are updated in `V3_CLAUSE_ACCEPTANCE_MATRIX_20261116.md`,
`V3_GAMEPLAY_DECISIONS.md`, and the completion tracker. Next independently
actionable partial requirement: inspect the photon combat-protection gap and
its approved protocol targeting contract; do not invent ship-targeting
semantics. Remaining requirements include four PARTIAL, `POP-07`
IMPLEMENTED_UNVERIFIED, and one BLOCKED.

### Batch 27 — Toll fighters through normal warp and Python menu

Starting classification: **61 VERIFIED, 1 IMPLEMENTED_UNVERIFIED, 4 PARTIAL,
1 BLOCKED**. Ending classification: **62 VERIFIED, 1
IMPLEMENTED_UNVERIFIED, 3 PARTIAL, 1 BLOCKED**. `PARITY-TOLL-01` is promoted
after its final ordinary-warp acceptance clause was exercised.

The final process inventory later found three stale Godot executables running
only the two named prior scripts: one `planet_command_response_test.gd` process
and two `persisted_planet_restore_test.gd` processes, aged three days or eleven
hours. Those exact `gdot` PIDs were stopped; unrelated processes were untouched.
The scripts themselves had already passed in this run (117 checks and 8/8), so
their old backtraces are not unresolved test failures.

The updated `suite_combat_toll_fighters.json` passed **21/21** cases against a
fresh disposable PostgreSQL database. A normal warp displayed a 30-credit offer
for 30 toll fighters, acceptance transferred exactly 30 credits and allowed
passage, and a second normal-warp refusal applied one durable fighter hit,
consumed one turn and did not charge the payer again. `manual_acceptance/
toll_python_prepare.json` passed **20/20** cases. An ordinary Python player
authenticated, used the real `MOVE` menu's `(W) Warp to Sector` choice and the
production `warp_flow`, accepted the server's three-credit offer, and received
the authoritative sector refresh; this client journey passed **5/5** checks.
PostgreSQL readback confirmed payer 1000→997 credits, owner 2000→2003, 20→19
turns, an accepted 3-fighter/3-credit offer, one 3-credit payee row and sector
2. Existing Godot live transwarp toll acceptance covers offer display, accept,
refuse, damage and two reconnects; the mixed-owner/refusal/restart suite covers
atomic resource accounting and recovery.

The first normal-refusal test run destroyed its low-resource test ship; the
test fixture was raised to a durable but schema-valid shield/fighter loadout,
without relaxing the combat checks. A subsequent attempt exceeded the ship's
allowed hull constraint and was corrected by leaving its hull at the valid
default. The Python acceptance's initial 75-second pause allowed the real
`daily_turn_reset` cron to restore turns, making an exact fixture turn check
invalid. The fixture now disables that scheduler before its live-client window.
The temporary Python harness output check was also corrected to capture the
interactive prompt separately from printed client text. Final normal-warp and
Python acceptance runs passed; no gameplay server defect was found.

Evidence is in the clause matrix and decision register. The remaining five
non-verified parents are now all decision-bound rather than safe independent
implementation tasks:

- `POP-07` — IMPLEMENTED_UNVERIFIED: its corporation-capture statement is a
  documentary scope constraint; the evidence policy requires runtime evidence
  and no runtime prohibition was approved.
- `POP-05` — BLOCKED: daily recurrence interval, persistent stoppage duration,
  and population-loss amount are still unspecified. The separately approved
  per-production-cycle unrest rule is already implemented and tested.
- `FED-03` — PARTIAL: the decision register excludes ISS pursuit from this
  bounded scope, while Protocol V3 and `iss_patrol_step` implement pursuit;
  approval is needed on which contract controls.
- `EXC-09` — PARTIAL: the 30-day scheduler simulation verifies invariants, but
  there is no quantitative sustainability threshold for a pass/fail balance
  result; no economic rate was tuned.
- `PARITY-PHOTON-01` — PARTIAL: `combat.fire_photon` targets an adjacent sector
  without a ship target, so an approved wire/effect rule is needed to test the
  manual's ship-shield protection statement.

No independent code change can close these five without changing an approved
scope/policy or inventing a runtime mechanic. Focused acceptance results for
this run: normal toll suite **21/21**, Python toll fixture **20/20**, Python
toll menu **5/5**, Python planet-capture suite **27/27** and Python menu **5/5**;
Godot planet-capture UI **10 checks**, restart suite **13/13**, and targeted
planet regressions **8/8**, **117 checks**, and **244 menu checks**. Full
Python passed **834/834**; `tests.v2` passed **277**, skipped **7**, with **227
subtests**; Godot 4.5.1 passed **106**, failed **0**, skipped **8** (114
scripts accounted for). The broad server suite was not rerun because no server
implementation changed in these acceptance batches.
