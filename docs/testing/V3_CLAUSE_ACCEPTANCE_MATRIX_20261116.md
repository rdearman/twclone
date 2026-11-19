# V3 clause-level acceptance matrix

Working matrix derived from the approved decision register and the 67-row parity closure. Parent requirement status is separate from clause evidence: passing one clause does not promote its parent. “Implemented but untested” means an implementation path exists but no clause-specific executed acceptance is recorded.

Reviewed: 19 November 2026, after autonomous parity batches 21–25.

## Current measures

- Parent requirements: 67 (60 VERIFIED, 1 IMPLEMENTED_UNVERIFIED, 5 PARTIAL, 1 BLOCKED). Autonomous batch 24 reran the complete Ferengi wanted-list/pursuit/reward journey and restart verification; `NPC-FER-01` is now VERIFIED. `POP-07` remains the only IMPLEMENTED_UNVERIFIED parent due its documentary scope clause.
- Base clause rows: 227 (175 VERIFIED, 52 IMPLEMENTED_UNTESTED). Addenda identify further independently verified behavior slices, concrete incomplete behaviors, and the exact blocked decisions. These clause counts do not replace the 67 parent requirement counts.
- Batch 23 added executable subclause evidence for all seven `COMBAT-01` destruction/recovery clauses without changing the 67-parent or 227-base-clause arithmetic. The acceptance was ordinary-player combat against disposable PostgreSQL, followed by an actual serving-server restart and authenticated recovery.
- The initial 227 rows split the approved statements by sentence and semicolon. Compound statements are further separated where acceptance evidence distinguishes behavior; remaining compound rows are explicitly a working decomposition and should be refined when their acceptance tests are selected.
- The earlier ten arithmetic/date checks did not promote their parents; the later BANK-03 monthly-retention acceptance below completed that requirement.

## BANK-03 monthly retention acceptance

The monthly recurrence and configured-day retention clauses were exercised
against disposable PostgreSQL using the actual bank scheduler and a serving
game-server restart. `bank_monthly_retention_prepare.json` and
`bank_monthly_retention_restart_verify.json` passed 24/24 cases. An ordinary
player created a monthly order for day 31, funded its bank account, advanced
the registered scheduler to the actual due date, and authenticated again after
restart. PostgreSQL assertions confirmed exactly one paid occurrence, two
ledger legs in its transaction group, one scheduled occurrence record, and a
next run on day 31. The authenticated order list also returned `month_day=31`.

This acceptance exposed two defects and they were fixed: the bank-order cron
handler discarded the supplied scheduler timestamp and used wall-clock time;
the PostgreSQL result-type mapper omitted `smallint` (OID 21), making saved
monthly date fields appear as JSON null. The handler now forwards the scheduler
timestamp, and PostgreSQL `smallint` results map to the integer database type.

## V3-PLANET-RATE-01 remainder acceptance

The existing PostgreSQL migration test executed all 21 class/commodity default
assertions. `planet_rate_remainder_prepare.json` and
`planet_rate_remainder_restart_verify.json` passed 17/17 cases against the
actual registered `planet_growth` handler. Three assigned workers per unit
across ORE, ORG and EQU accumulated independent 1 and 2 worker remainders,
persisted those values across an actual serving-server restart, and produced
exactly one unit of each commodity on the third tick. Database state confirmed
the three remainders returned to zero and exactly one production event per
commodity existed. This closes the final configured-rate/remainder clause;
the values remain noncanonical twclone defaults.

## SYS-07 kick-token acceptance

`tests.v2/test_sysop_players.json` passed against disposable PostgreSQL: 15/15
cases, including the target's original token replayed on a fresh connection
after a SysOp kick and a database assertion that no durable session remained.
The initial check exposed two test-harness issues rather than server defects:
the player search used a non-unique prefix and selected another user, and the
JSON runner silently performed a fresh credential login after a forced socket
close. The fixture now narrows the search to `target_user`; the runner has an
explicit acceptance option to replay the cached token without auto-login.

## SHIP-01 facility acceptance

The focused live PostgreSQL run executed `suite_ship_sale_v3.json` (18/18) and
`manual_acceptance/ship_sale_locations_v3.json` (19/19): 37 cases across two
suites, zero failures. The first suite sold through a DB-classified StarDock,
checked proceeds and durable used inventory, exercised the alias and rejected a
duplicate payout. The second asserted the black-market and inventory-only
facility flags directly, then sold ordinary owned hulls through each authorized
facility and verified the sale audit and used-inventory rows. The initial
combined facility-flag assertion failed because the SQL fixture obscured which
flag was false; the replacement asserts each flag independently. Both sale
paths themselves passed before and after that fixture correction. No gameplay
defect was found.

## Ten smallest executable gaps closed in this run

| Clause | Acceptance condition | Executed evidence | Result |
|---|---|---|---|
| `POP-03/ORE output` | floor(normal_output * 50 / 100) for ORE | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` | VERIFIED |
| `POP-03/ORG output` | floor(normal_output * 50 / 100) for ORG | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` | VERIFIED |
| `POP-03/EQU output` | floor(normal_output * 50 / 100) for EQU | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` | VERIFIED |
| `BANK-03/2023 February clamp` | configured day 31 clamps to 2023-02-28 at the requested UTC time | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` | VERIFIED |
| `BANK-03/2024 February clamp` | configured day 31 clamps to leap day 2024-02-29 | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` | VERIFIED |
| `BANK-03/2100 century rule` | 2100 is not a leap year; day 31 clamps to 2100-02-28 | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` | VERIFIED |
| `BANK-03/2000 century rule` | 2000 is a leap year; day 31 clamps to 2000-02-29 | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` | VERIFIED |
| `BANK-03/30-day month clamp` | April day 31 clamps to April 30 without changing the UTC time | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` | VERIFIED |
| `BANK-03/invalid month rejection` | month 13 returns invalid schedule (epoch 0) | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` | VERIFIED |
| `BANK-03/invalid day rejection` | day 0 returns invalid schedule (epoch 0) | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` | VERIFIED |

## Ten smallest remaining behavior gaps closed in this run

These are independently testable behavior slices. They do not promote a parent requirement unless every parent clause has evidence.

| Clause slice | Acceptance condition | Executed evidence | Result |
|---|---|---|---|
| `PARITY-TOLL-01/transwarp offer` | Transwarp arrival offers the configured toll before movement | `suite_movement_transwarp_tolls.json::case-0012` | VERIFIED |
| `PARITY-TOLL-01/transwarp payment` | Accepting the offer pays once and permits passage | `suite_movement_transwarp_tolls.json::case-0013`, `case-0015` | VERIFIED |
| `PARITY-TOLL-01/transwarp replay` | Repeated accepted request returns idempotent result without second charge | `suite_movement_transwarp_tolls.json::case-0014`, `case-0015` | VERIFIED |
| `PARITY-TOLL-01/transwarp insufficient funds` | Payment refusal preserves pending offer, turns, sector, credits and fuel | `suite_movement_transwarp_toll_edges.json::case-0014`, `case-0015` | VERIFIED |
| `PARITY-TOLL-01/transwarp funded retry` | A pending offer succeeds after exact funding, with one settlement | `suite_movement_transwarp_toll_edges.json::case-0017`, `case-0018` | VERIFIED |
| `PARITY-TOLL-01/self protection` | A player's own toll fighters do not stop or charge that player | `suite_movement_transwarp_toll_edges.json::case-0029`, `case-0030` | VERIFIED |
| `PARITY-TOLL-01/corporation protection` | Same-corporation toll fighters do not stop or charge a member | `suite_movement_transwarp_toll_edges.json::case-0043`, `case-0044` | VERIFIED |
| `PARITY-TOLL-01/refusal encounter` | Refusal follows hostile-arrival combat, does not pay, and applies damage once | `suite_movement_transwarp_toll_edges.json::case-0055`–`case-0057` | VERIFIED |
| `PARITY-TOLL-01/mixed-owner split` | Payment is split by toll-fighter owner; retry cannot duplicate credits, fuel, or turns | `suite_movement_transwarp_toll_mixed_owners.json::case-0027`–`case-0031` (42/42 suite cases passed) | VERIFIED |
| `PARITY-TOLL-01/lethal refusal recovery` | Refusal pays nothing, hostile toll damage destroys the hull, and an owned escape pod survives reauthentication | `suite_movement_transwarp_toll_mixed_owners.json::case-0039`–`case-0041` | VERIFIED |
| `PARITY-TOLL-01/restart persistence` | Accepted mixed-owner settlement and lethal refusal remain persisted after a serving-server restart | `suite_movement_transwarp_toll_mixed_owners_restart.json::case-0001`–`case-0003` (3/3 passed) | VERIFIED |
| `PARITY-TOLL-01/Godot UI` | Actual Godot navigation form presents the server toll offer, accepts/refuses, refreshes state, and reconnects after process restart | `client/godot_client/godot/tests/live_transwarp_toll_test.gd`; Godot 4.5.1 exit 0; disposable PG offers `godot-0011`/`godot-0024`, split payees 159/160, credits 996, final sector 21; only refusal produced `combat.hit.fighters` | VERIFIED |
| `POP-02/remote population` | Remote colonist transfer is refused without changing either population pool | `suite_planet_colonist_transfers.json::case-0010`, `case-0011` | VERIFIED |
| `POP-01/transfer rollback` | Failed colonist-to-slave transfer rolls back ship cargo and planet population | `suite_planet_colonist_transfers.json::case-0025`, `case-0026` | VERIFIED |

Additional population acceptance slices executed in the same work batch:

| Clause slice | Acceptance condition | Executed evidence | Result |
|---|---|---|---|
| `POP-01/transfer bounds` | Zero quantity, insufficient source population, and no remaining hold space are refused without changing either pool | `suite_planet_colonist_transfers.json::case-0013`–`case-0018` | VERIFIED |
| `POP-01/transfer restart` | A committed ship/planet population transfer survives a serving-game-process restart and further transfers conserve the same total | `suite_planet_colonist_restart_prepare.json::case-0011`–`case-0012`; after actual server restart, `suite_planet_colonist_restart_verify.json::case-0005`–`case-0007` | VERIFIED |

## Clause ledger

## Autonomous parity batch 2 — 19 November 2026

The five selected parents were exercised against disposable PostgreSQL; four
were promoted. EXC-05 has passing sequential placement/fill/retry evidence but
remains IMPLEMENTED_UNVERIFIED until concurrent settlement/order-lock
acceptance passes. The first run
of `suite_planet_market_buy_order_lifecycle.json` exposed order-book fixture
contamination: unrelated higher-priority open orders could consume the seeded
port stock. The test now isolates the ORE book, disables unrelated scheduled
work in the disposable database, and advances the actual settlement handler
through the deterministic QA scheduler. It passed 36/36 cases, including the
unreserved open order, exact two-unit partial fill, cancellation of the
unfunded remainder, stock/credit/trade assertions and retry without a second
settlement. No server behavior or expected gameplay result was changed.

`suite_ship_sale_v3.json` passed 27/27: the five live hardware prices match the
approved catalogue, the exact integer-floor sale formula excludes cargo,
active replacement is required, and a sole-ship sale is refused without
changing the active ship, ownership, credits or audit. The separate
`ship_sale_atomic_rollback.json` passed 19/19 and verifies atomic active-ship
switch/sale rollback and exact-once retry. `suite_slave_unrest_config_defaults.json`
passed 12/12: all three POP-05 values are zero by default; the actual daily
handler creates no unrest event, stoppage or population loss. The production
morale pair passed 19/19 setup and 10/10 verification cases: owner-facing
planet detail reports slave morale `LOW` before and after a production cycle,
colonist detail invents no morale status, colonists retain the baseline output,
and slaves receive only the approved 50% production result.

Total for these five selected requirements: 123/123 live PostgreSQL cases across six suites,
plus 9 Python source/unit assertions and 17 subtests. No gameplay server
defect was found. The market test fixture was made deterministic; its prior
wall-clock delay and unrelated-order dependency were removed.

Parent status after batch 2: **36 VERIFIED, 23 IMPLEMENTED_UNVERIFIED, 7
PARTIAL, 1 BLOCKED**. The 18 touched base clauses are verified; additional
EXC-05 concurrent settlement evidence is complete: two actual V3 server
processes shared one disposable PostgreSQL database and simultaneously entered
the scheduler settlement path for the same due orders. Both selected the same
candidate buy/sell rows; PostgreSQL row locking and in-transaction state
revalidation allowed exactly one trade to commit. Final SQL assertions proved
single settlement and exact order, planet/port stock and credit conservation.
`suite_planet_market_buy_order_lifecycle.json` passed 53/53 cases using the
cross-server runner. The parent is promoted only after this concurrency case
was added to the earlier placement, fill, retry and conservation evidence.

## Autonomous parity batch 1 — 19 November 2026

Five complete parents were verified through focused disposable-PostgreSQL
acceptance. `suite_ship_sale_v3.json` passed 19/19 and confirmed the standalone
sale factor defaults to 6000 basis points, is range-checked, and remains
separate from the trade-in factor. `manual_acceptance/slave_unrest_production_prepare.json`
and `slave_unrest_production_verify.json` passed 15/15 and 8/8: fixed slave
output is absent from the configurable settings, colonist and slave production
use the approved 50% result (integer floor), the zero-unrest slave control
matches, and retry preserves population/output. `suite_economy_insurance.json`
passed 22/22, including hull-only coverage, bank payout, exact accounting,
duplicate-claim idempotency, and injected rollback. `suite_planet_taxable_activity_e2e.json`
passed 44/44 with Exchange virtual delivery as an immediate economic transfer;
before/after SQL snapshots confirmed no physical ship was created, moved, or
modified. `suite_photon_duration_acceptance.json` passed 15/15: migrated
default 600 seconds, configurable 1-second minimum, rejection of zero and
over-maximum settings, exact persisted expiry, and one ammunition debit per
launch. Godot 4.5.1 parser regressions passed 8/8 and 117/117 after an explicit
boolean annotation fixed type inference in the sale-receipt UI path.

The focused server cases total 123 passing assertions across the latest
acceptance artifacts (the separately rerun insurance rollback and Exchange
state-snapshot suites supersede their earlier 16/16 and 43/43 snapshots).
Client regression: Python 810/810; Godot 106 passed, 0 failed, 6 skipped
(live-server-only scripts). The restart-between-suites runner also exposed an
AddressSanitizer startup crash in `load_bootstrap_config` after its restart;
this is retained as an unresolved harness/server-startup acceptance issue and
is not counted as a passing restart case. Existing explicit restart evidence
for photon expiry remains in the prior photon acceptance record.

The documentary evidence policy question for `POP-07` remains open: its final
clause excludes an inferred corporation-capture rule but does not define a
runtime prohibition. The matrix's current VERIFIED policy requires runtime
evidence, so the row is not promoted and no behavior-inventing negative test
was added. A minimal policy proposal is to permit traceable approved-contract
evidence for explicit scope-exclusion clauses while labeling the evidence
`CONTRACT` rather than `RUNTIME`; until that policy is approved, POP-07 remains
unverified.

| Requirement | Clause | Acceptance condition | Clause status | Evidence / next action |
|---|---|---|---|---|
| `POP-01` | clause 1 | A planet is `COLONIST`, `SLAVE`, or `UNPOPULATED` | VERIFIED | `tests.v2/test_pg_migration_171_population_state.sql` ran against disposable PostgreSQL; assertions exercised all three classifications and passed. |
| `POP-01` | clause 2 | never mixed | VERIFIED | Migration trigger test in `tests.v2/test_pg_migration_171_population_state.sql` preserved the single enum classification on populated, empty, and slave updates. |
| `POP-01` | clause 3 | Preserve counts and allocations | VERIFIED | `tests.v2/test_pg_migration_171_population_state.sql` asserts population/allocation equality on migration and update; `suite_combat_advanced.json::case-0024`, `case-0029`, `case-0034`, `case-0039` assert acquisition preservation. |
| `POP-01` | clause 4 | Existing populated planets migrate as colonists | VERIFIED | `tests.v2/test_pg_migration_171_population_state.sql` migration assertion: existing population 100 becomes `COLONIST`, unchanged. |
| `POP-01` | clause 5 | empty planets remain unpopulated | VERIFIED | `tests.v2/test_pg_migration_171_population_state.sql` migration assertion: empty planet remains `UNPOPULATED`, population 0. |
| `POP-02` | clause 1 | Evil players may buy slaves or choose enslavement on successful conquest | VERIFIED | `suite_combat_advanced.json::case-0028` and `case-0029`: an evil player chose enslavement on successful capture; the response reported `enslaved_colonists=true` and SQL confirmed the population transition and unchanged count/allocations. Suite passed 47/47 after adding the neutral refusal cases. Python `test_planet_workflows.py -k planet_attack` passed 9/9; Godot command-menu choice dispatch passed in 238 checks. |
| `POP-02` | clause 2 | Neutral may inherit but cannot buy/enslave | VERIFIED | `suite_combat_advanced.json::case-0039`–`case-0042` refused neutral enslavement before combat mutation, then allowed neutral inheritance with `enslaved_colonists=false`. `suite_neutral_slave_trade.json::case-0013`–`case-0014` refused an ordinary neutral player's slave purchase and verified credits, turns, cargo and stock unchanged; suite passed 14/14. Python/Godot client tests passed and exercise the explicit choice field. |
| `POP-02` | clause 3 | Good cannot retain slaves after qualifying capture | VERIFIED | `suite_combat_advanced.json::case-0033`–`case-0034`: successful good capture liberates slaves and preserves population, allocations, treasury, and planetary stock. |
| `POP-02` | clause 4 | Never create remote planetary population without safe transport | VERIFIED | Remote pickup refusal and unchanged DB state: `suite_planet_colonist_transfers.json::case-0010`–`case-0011`; landed pickup/drop-off and conservation: `case-0020`–`case-0023`; current landed slave delivery and cargo/population state: `suite_planet_fighter_transfers.json::case-0043`–`case-0044`. |
| `POP-03` | ORE output | floor(normal_output * 50 / 100) for ORE | VERIFIED | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` PASS; live scheduler `manual_acceptance/slave_unrest_production_verify.json::case-0001`–`case-0004` passed across a serving-server restart (8/8 verification cases). SQL confirmed colonist ORE output 5, slave output 2 or 0 for the accepted deterministic disruption, and same-tick retry did not duplicate output. |
| `POP-03` | ORG output | floor(normal_output * 50 / 100) for ORG | VERIFIED | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` PASS. Live scheduler acceptance uses ORE to exercise the shared slave efficiency path; commodity-specific ORG runtime output remains covered by the engine unit test. |
| `POP-03` | EQU output | floor(normal_output * 50 / 100) for EQU | VERIFIED | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` PASS. Live scheduler acceptance uses ORE to exercise the shared slave efficiency path; commodity-specific EQU runtime output remains covered by the engine unit test. |
| `POP-04` | clause 1 | Slave morale is permanently low and cannot improve | VERIFIED | `slave_unrest_production_prepare.json` and `slave_unrest_production_verify.json`: ordinary SysOp-owned `planet.info` returns `morale_status=LOW` before and after scheduler execution; 19+10 PostgreSQL acceptance cases passed. |
| `POP-04` | clause 2 | No additional continuous output modifier | VERIFIED | The actual production handler yields floor(normal ORE output × 50/100); `slave_unrest_production_verify.json` asserts the slave output remains 2 when colonist baseline is 5 and separately accounts for the approved disruption event. |
| `POP-04` | clause 3 | Preserve existing colonist rules | VERIFIED | `slave_unrest_production_verify.json` asserts the colonist control produces 5 while the matching slave control produces 2; persisted population/allocation snapshots pass. |
| `POP-04` | clause 4 | invent no dynamic colonist morale | VERIFIED | `slave_unrest_production_prepare.json` and `slave_unrest_production_verify.json` assert colonist `planet.info` has no `morale_status`; no dynamic colonist morale field or numeric morale rule was introduced. |
| `POP-05` | clause 1 | Evaluate daily | VERIFIED | `slave_unrest_daily_prepare.json` configured a one-day interval and ran the production handler against due and future events; 18/18 cases passed. |
| `POP-05` | clause 2 | deterministic, persisted, restart/idempotency-safe | VERIFIED | `slave_unrest_daily_prepare.json` plus `slave_unrest_daily_restart_verify.json` passed 30/30 cases against disposable PostgreSQL, with a real serving-server restart. Two authenticated `sys.cron.slave_unrest_once` requests ran concurrently; SQL verified one event, one exact population loss and the expected assignment totals. Sequential replay, rollback, recovery and persistence also passed. The separately unspecified daily policy values still block the parent. |
| `POP-05` | clause 3 | May stop production and reduce slave population | VERIFIED | `slave_unrest_daily_prepare.json` asserts configured four-person loss, persistent stoppage and exact assigned-population result; restart suite verifies recovery without repeated loss. 30/30 combined. |
| `POP-05` | clause 4 | may not change ownership, destroy planets, liberate or convert population | VERIFIED | Both POP-05 daily fixtures assert the planet remains present, player-owned and `SLAVE`-typed after loss and restart; 30/30 combined. |
| `POP-05` | clause 5 | Configured loss is allocated proportionally across production assignments using deterministic largest-remainder integer allocation | VERIFIED | `slave_unrest_daily_prepare.json` asserts 4 lost from assignments 10/5/3/2 produces 8/4/2/2; `slave_unrest_daily_restart_verify.json` confirms persisted allocation and successful retry. |
| `POP-05` | clause 6 | exact total loss is preserved | VERIFIED | `slave_unrest_daily_prepare.json` checks population 20→16 and one event with `applied_population_loss=4`; restart/retry suite checks exact single settlement. |
| `POP-06` | clause 1 | Owner or authorized corporation management (Leader/Officer) converts all slaves to colonists atomically, preserving population and allocations | VERIFIED | `suite_planet_fighter_transfers.json::case-0040` (player owner), `case-0047` (corporation Officer), `case-0048` (corporation Leader), `case-0045` (Member denial), and `case-0049` (population/allocation conservation); controlled AFTER UPDATE fault `case-0052`/`case-0053` proved rollback. The ordinary landed-player menu workflow is covered by Python `test_emancipation_requires_landing_and_refreshes_confirmed_planet` and Godot `command_menu_test.gd` confirmed dispatch for selected planet ID 19. Full disposable PostgreSQL suite: 67/67. |
| `POP-06` | clause 2 | no turn/credit cost | VERIFIED | `suite_planet_fighter_transfers.json::case-0040` and `case-0047` assert `turns_charged=0`, `credits_charged=0`; Python client test verifies the server-reported values and refresh. Full disposable PostgreSQL suite: 67/67. |
| `POP-06` | clause 3 | no partial conversion | VERIFIED | `suite_planet_fighter_transfers.json::case-0040`/`case-0041`, `case-0047`–`case-0049`, and injected rollback `case-0052`/`case-0053` assert full population conversion or unchanged full slave state; no partial population/allocation changes. Full disposable PostgreSQL suite: 67/67. Python workflow unit tests: 82 passed; Godot command menu: 218 checks passed. |
| `POP-07` | clause 1 | Capture requires defeated defenses and successful invasion | VERIFIED | `suite_combat_advanced.json::case-0023`–`case-0024`: successful invasion followed by DB ownership/population/asset assertions. |
| `POP-07` | clause 2 | Evil explicitly chooses retain/enslave colonists | VERIFIED | `suite_combat_advanced.json::case-0025`–`case-0029`: evil player explicitly selects enslavement; DB checks population type and ownership/defenses. |
| `POP-07` | clause 3 | good automatically liberates slaves | VERIFIED | `suite_combat_advanced.json::case-0030`–`case-0034`: good capture converts slaves and conserves population/assets. |
| `POP-07` | clause 4 | neutral retains inherited slaves | VERIFIED | `suite_combat_advanced.json::case-0035`–`case-0039`: neutral capture retains slave classification and planetary assets. |
| `POP-07` | clause 5 | evil may retain them | VERIFIED | `suite_combat_advanced.json::case-0028`–`case-0029`: explicit evil enslavement succeeds and is persisted. |
| `POP-07` | clause 6 | Planet resources/treasury remain with captured planet | VERIFIED | `suite_combat_advanced.json::case-0024`, `case-0034`, `case-0039` assert captured-planet assets, treasury, and population allocations remain attached. |
| `POP-07` | clause 7 | No corporation-capture rule is inferred | IMPLEMENTED_UNTESTED | Documentary trace: `docs/V3_GAMEPLAY_DECISIONS.md` POP-07 explicitly excludes an inferred corporation-capture rule; `docs/PROTOCOL.v3/24_Planets_Outposts_Stations.md` defines no such mechanic. This is a design/scope constraint, not a prohibited runtime action. Documentary evidence does not satisfy this matrix's executable-runtime VERIFIED policy; no negative test is added because that would invent behavior. |
| `HARV-01` | clause 1 | `planet.harvest` transfers accumulated ore/organics/equipment only to currently landed ship, capped to available holds | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` and disposable PostgreSQL harvest journey in `V3_GAMEPLAY_COMPLETION_TRACKER.md`. |
| `HARV-01` | clause 2 | remainder stays on planet | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` and disposable PostgreSQL harvest journey in `V3_GAMEPLAY_COMPLETION_TRACKER.md`. |
| `HARV-01` | clause 3 | zero turns | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` and disposable PostgreSQL harvest journey in `V3_GAMEPLAY_COMPLETION_TRACKER.md`. |
| `HARV-01` | clause 4 | no generation | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` and disposable PostgreSQL harvest journey in `V3_GAMEPLAY_COMPLETION_TRACKER.md`. |
| `HARV-01` | clause 5 | atomic | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` and disposable PostgreSQL harvest journey in `V3_GAMEPLAY_COMPLETION_TRACKER.md`. |
| `HARV-02` | clause 1 | Use existing authoritative ownership/corporation permissions | VERIFIED | `manual_acceptance/harvest_permissions_atomic.json` 40/40 on disposable PostgreSQL: owner harvest and corporation Officer harvest succeed; ordinary visitor and corporation Member are refused; SQL reads confirm stock/cargo state. Cases 0016–0021, 0029–0033. |
| `HARV-02` | clause 2 | no unrestricted withdrawal | VERIFIED | `manual_acceptance/harvest_permissions_atomic.json::case-0032`–`case-0033` deny Member harvest and preserve stock/cargo. Injected `ship_cargo` update failure is refused and rolls stock/cargo back (`case-0035`–`case-0039`). |
| `HARV-03` | clause 1 | Planet surplus may be sold directly to Exchange | VERIFIED | `manual_acceptance/planet_exchange_sale_harvest_atomic.json` 27/27: ordinary player sale settles exact Exchange stock and credits; injected taxable-activity failure rolls stock/credits/transaction back (`case-0019`–`0023`). |
| `HARV-03` | clause 2 | sale immediately removes available planetary stock, so sold goods cannot later be harvested | VERIFIED | `planet_exchange_sale_harvest_atomic.json::case-0023`–`0026`: sale reduces stock to zero; subsequent harvest returns zero. Restart pair `planet_exchange_sale_harvest_restart_verify.json::case-0007`–`0008` confirms zero harvest, one durable sale, stock/cargo and owner credits after serving-server restart. Python surface menu workflow: `test_planet_workflows.py::test_planet_exchange_sale_menu_confirms_receipt_and_refreshes`; Godot menu dispatch: `tests/command_menu_test.gd` (212 checks). |
| `EXC-01` | clause 1 | Planets buy/sell Exchange goods | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus PostgreSQL sale→port-purchase readback cited by the parity closure. |
| `EXC-01` | clause 2 | ports buy from Exchange | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus PostgreSQL sale→port-purchase readback cited by the parity closure. |
| `EXC-01` | clause 3 | preserve compatible port, order and NPC trade paths | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus PostgreSQL sale→port-purchase readback cited by the parity closure. |
| `EXC-02` | clause 1 | Validate source stock and destination funds/capacity, transfer inventory and credits, record durable transaction atomically | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus PostgreSQL ledger settlement assertions cited by the parity closure. |
| `EXC-02` | clause 2 | completed goods arrive immediately | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus PostgreSQL ledger settlement assertions cited by the parity closure. |
| `EXC-03` | clause 1 | Sold goods leave seller ownership and cannot be harvested, resold, built with, consumed or counted twice | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus second-tick stock readback cited by the parity closure. |
| `EXC-04` | clause 1 | Deliveries are immediate economic transfers | VERIFIED | `suite_planet_taxable_activity_e2e.json::case-0035`–`case-0040`, 44/44 live disposable-PostgreSQL cases; actual scheduler settlement transfers goods/credits once and retry is idempotent. |
| `EXC-04` | clause 2 | no physical NPC ships, movement, fuel, holds, docking or delay | VERIFIED | `suite_planet_taxable_activity_e2e.json::case-0031`, `case-0036`–`case-0040`; database snapshot compares physical ship count and ship state before/after settlement. |
| `EXC-05` | clause 1 | Keep placement, reservation, execution and settlement distinct | VERIFIED | `suite_planet_market_buy_order_lifecycle.json::case-0016`–`case-0027`: zero-balance/full-storage order is accepted unreserved, remains open until matching; real scheduler later settles after funding/storage. |
| `EXC-05` | clause 2 | Open orders wait for matches | VERIFIED | `suite_planet_market_buy_order_lifecycle.json::case-0018`–`case-0025`: unfilled order remains open until matching inventory and eligible sell order exist. |
| `EXC-05` | clause 3 | partial fills preserve remainder | VERIFIED | `suite_planet_market_buy_order_lifecycle.json::case-0030`–`case-0031`: two of ten fill; eight remain and the unfunded remainder is cancelled with `insufficient_credits`; balances and stock conserve. |
| `EXC-05` | clause 4 | no duplicate settlement | VERIFIED | `suite_planet_market_buy_order_lifecycle.json::case-0032`–`case-0036` proves rearmed scheduler retry idempotency; new concurrent case runs actual settlement on two live servers against one disposable database, verifies both selected the same due orders, then asserts one trade and exact stock/credit/order conservation. Full focused suite: 53/53. |
| `EXC-06` | clause 1 | Port demand follows current target/pricing/capacity rules | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` port replenishment case and PostgreSQL settlement readback. |
| `EXC-06` | clause 2 | ports purchase Exchange goods to replenish | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` port replenishment case and PostgreSQL settlement readback. |
| `EXC-06` | clause 3 | Do not bypass Exchange with artificial supply unless a separate source is documented | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` port replenishment case and PostgreSQL settlement readback. |
| `EXC-07` | clause 1 | Every trade has a source and destination | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` quantity movement and conservation assertions. |
| `EXC-07` | clause 2 | seller decrease equals exchange increase or buyer increase | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` quantity movement and conservation assertions. |
| `EXC-07` | clause 3 | no trade creates/destroys goods | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` quantity movement and conservation assertions. |
| `EXC-07` | clause 4 | Production/consumption are separate | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` quantity movement and conservation assertions. |
| `EXC-08` | clause 1 | Atomic credits, durable records, retry/idempotency/concurrency safety and no stale/cancelled settlement | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` settlement receipt/idempotency assertions. |
| `EXC-08` | clause 2 | A virtual clearing account may receive explicit, configurable game-engine liquidity injections | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` settlement receipt/idempotency assertions. |
| `EXC-08` | clause 3 | the game need not be financially self-sufficient | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` settlement receipt/idempotency assertions. |
| `EXC-08` | clause 4 | Player assets/transactions remain protected | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` settlement receipt/idempotency assertions. |
| `EXC-09` | clause 1 | Favor enjoyable, sustainable gameplay over real-world financial realism | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `EXC-09` | clause 2 | Deliberate credit, commodity or liquidity injections are permitted only as explicit/configurable engine mechanisms distinguishable from ordinary player transactions | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `SHIP-05` | clause 1 | Used hull is acquired through existing shipyard workflow at floor((original hull value + retained equipment value) * configured percentage), default 80% | VERIFIED | `suite_ship_sale_v3.json` 35/35 against disposable PostgreSQL: ordinary authenticated `shipyard.list` exposes the calculated price, then `shipyard.upgrade` settles at the exact listed 80% floor price; `shipyard_used_resale_factor_bp=8000`. Python menu/workflow coverage: `test_ship_workflows.py::test_used_shipyard_purchase_uses_listed_identity_price_and_refreshes`; Godot shipyard UI dispatch: `tests/command_menu_test.gd` (231 checks). |
| `SHIP-05` | clause 2 | preserve hull identity/equipment and do not create a new hull | VERIFIED | `suite_ship_sale_v3.json` 35/35 against disposable PostgreSQL: purchase receipt returns sold ship ID; SQL confirms one original `ships` row, unchanged hull 73 and all retained equipment, listing removed, buyer active ship/primary ownership transferred, former primary hull retained as non-primary, exact credit deduction, and retry refusal with no second transfer. Python workflow asserts matching server receipt and refreshes `player.my_info`, `ship.status`, and sector; Godot exposes confirmation and sends the fixed `used_inventory_id`. |
| `SHIP-05` | clause 3 | Standalone ship-sale payout remains 60% | VERIFIED | `suite_ship_sale_v3.json` 35/35 against disposable PostgreSQL: configured 6000-bp standalone payout remained independent of 8000-bp used resale factor; initial sale returned exactly 42,090 credits and its audit matched the integer-floor 60% valuation formula. |
| `INS-02` | clause 1 | Every qualifying hull claim pays the covered replacement value directly to the player's bank regardless of insurer reserves | VERIFIED | `suite_combat_and_crime.json` passed 22/22 on disposable PostgreSQL. Two ordinary-player policies paid 300 and 700 CRD directly to bank against the 1,000 hull replacement cap despite an empty insurer fund. |
| `INS-02` | clause 2 | Explicit system credit creation/virtual insurer funding is allowed and must be identifiable | VERIFIED | Same 22-case suite checks two `INSURANCE_GUARANTEE/CREDIT` ledger rows, unique policy/hull idempotency keys, exact 1,000 CRD total and unchanged zero insurer balance. |
| `INS-02` | clause 3 | Exactly once | VERIFIED | Same suite confirms exactly one paid destruction claim and one guaranteed ledger transaction per qualifying policy, with total payout capped at hull value; `suite_economy_insurance.json` 22/22 also verifies manual claim replay retains a single payout row. |
| `INS-02` | clause 4 | cargo/equipment excluded | VERIFIED | Actual insured hull destruction paid only the configured 300 + capped 700 policy amounts; aggregate credit equaled the seeded 1,000 hull value despite the payout schedule requesting 1,300. No cargo/equipment valuation was credited. |
| `INS-02` | clause 5 | escape pod independent | VERIFIED | In the same insured destruction journey the original ship was destroyed and the authenticated player's valid Escape Pod became the active owned ship; coverage did not replace or suppress the pod lifecycle. |
| `TRADE-01` | clause 1 | No haggling | VERIFIED | `manual_acceptance/trade_fixed_quote_no_haggle.json` 9/9: ordinary player-facing quote is returned; `trade.haggle` is refused and SQL confirms no offer/trade side effects (case-0007–0009). |
| `TRADE-01` | clause 2 | Existing fixed-at-quote authoritative price and configured dynamic supply/demand/black-market pricing remain | VERIFIED | `trade_fixed_quote_no_haggle.json::case-0007` verifies server quote values; `suite_market_shocks_e2e.json` 12/12 verifies scoped dynamic price change and return to baseline after scheduled expiry; `suite_planet_taxable_activity_e2e.json` 43/43 verifies deterministic market settlement, exact stock/credit transfer and idempotent replay. |
| `BANK-01` | clause 1 | Daily, weekly, monthly at specified UTC time | VERIFIED | `suite_economy_and_bank.json` 41/41 against disposable PostgreSQL: ordinary player creates daily 23:59, weekly 12:30, and monthly 08:15 orders; SQL confirms persisted UTC hour/minute for each. Python create-order workflow tests cover all three schedule forms; Godot `command_menu_test.gd` confirms exact weekly and daily V3 request fields (236 checks). |
| `BANK-01` | clause 2 | any existing player may be recipient | VERIFIED | `suite_economy_and_bank.json` 41/41 uses two independently registered ordinary players; daily, weekly and monthly orders persist `to_entity='player'` and the other existing player's ID. The authenticated Python menu exposes the create workflow and validates the server receipt. |
| `BANK-01` | clause 3 | ISO weekday numbering is used on the wire | VERIFIED | `suite_economy_and_bank.json` 41/41: weekly Monday=1 and Sunday=7 requests persist as 1 and 7; 0 and 8 are refused. Python workflow tests cover ISO endpoints and Godot emits the protocol's `weekday` key. |
| `BANK-01` | clause 4 | monthly day is retained as configured | VERIFIED | `suite_economy_and_bank.json` 41/41 confirms `month_day=31` is stored for monthly orders. The independent scheduled recurrence/restart acceptance `bank_monthly_retention_prepare.json` + `bank_monthly_retention_restart_verify.json` (24/24, disposable PostgreSQL and serving-server restart) confirms the configured day 31 remains after a short-month settlement and reconnect. |
| `BANK-02` | clause 1 | Skip that occurrence without debt or same-occurrence retry | VERIFIED | `manual_acceptance/bank_insufficient_funds_prepare.json::case-0011`–`case-0017`: authenticated zero-balance order, actual registered scheduler run, one `skipped_insufficient_funds` occurrence, no ledger transaction/debt; 17/17 cases passed on disposable PostgreSQL. |
| `BANK-02` | clause 2 | next occurrence remains active | VERIFIED | `manual_acceptance/bank_insufficient_funds_restart_verify.json::case-0006`–`case-0011`: after game-server restart owner listed still-active order, funded it through `bank.deposit`, advanced actual scheduler and verified one paid next occurrence and ledger legs; 11/11 passed. |
| `BANK-03` | 2023 February clamp | configured day 31 clamps to 2023-02-28 at the requested UTC time | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | 2024 February clamp | configured day 31 clamps to leap day 2024-02-29 | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | 2100 century rule | 2100 is not a leap year; day 31 clamps to 2100-02-28 | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | 2000 century rule | 2000 is a leap year; day 31 clamps to 2000-02-29 | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | 30-day month clamp | April day 31 clamps to April 30 without changing the UTC time | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | configured-day retention | retain the originally configured day for future recurrence | VERIFIED | `bank_monthly_retention_prepare.json` + `bank_monthly_retention_restart_verify.json`, disposable PostgreSQL, 24/24 cases, actual server restart; SQL confirms `month_day=31`, one paid occurrence, exactly two bank ledger legs, and next occurrence on day 31. Authenticated `bank.orders.list` returns day 31 after restart. |
| `BANK-03` | invalid month rejection | month 13 returns invalid schedule (epoch 0) | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | invalid day rejection | day 0 returns invalid schedule (epoch 0) | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-04` | clause 1 | If recipient no longer exists, cancel order automatically | VERIFIED | `manual_acceptance/bank_invalid_recipient_prepare.json` 17/17 and `bank_invalid_recipient_restart_verify.json` 11/11 with `--restart-between-suites`: after a completed payment, next actual scheduler occurrence detects missing recipient and disables the order; transaction/occurrence state is read from PostgreSQL. |
| `BANK-04` | clause 2 | completed transfers remain final | VERIFIED | Restart verification `bank_invalid_recipient_restart_verify.json::case-0009`–`0011` confirms the paid occurrence, both ledger legs and balances remain unchanged; one cancellation is recorded and retry does not reverse or duplicate the transfer. |
| `BANK-05` | clause 1 | Create/list/cancel | VERIFIED | Disposable PostgreSQL `suite_economy_and_bank.json` passed 41/41, covering authenticated create/list/cancel and another-player refusal. Python client workflow/menu tests passed 14/14 filtered; Godot `command_menu_test.gd` passed 238 checks with create dispatch. |
| `BANK-05` | clause 2 | durable exact-once occurrence, atomic bank debit/credit/ledger | VERIFIED | `bank_monthly_retention_prepare.json` + `bank_monthly_retention_restart_verify.json` passed 24/24 across an actual serving-server restart; SQL verified one paid occurrence and exactly two ledger legs. `bank_order_settlement_rollback.json` passed 24/24: injected occurrence-write failure rolled payer/recipient balances, occurrence, ledger, and order schedule back; requeued retry settled once with exact balances and two ledger entries. |
| `BANK-05` | clause 3 | cancellation serializes with execution | VERIFIED | `bank_order_cancel_race.json` passed 21/21 with parallel authenticated cancellation and actual deterministic scheduler execution. PostgreSQL accepted either legal serialization: zero payment if cancellation won, or one complete payment and two ledger legs if settlement won. Same-occurrence retry retained the same state. |
| `BANK-05` | clause 4 | no arbitrary order cap | VERIFIED | `bank_order_no_cap.json` passed 15/15: fixture seeded 105 active orders; ordinary player successfully created the 106th and listed it on page 2. PostgreSQL confirmed all 106 remained enabled. |
| `PLAYER-01` | clause 1 | 1-based page/limit | VERIFIED | `suite_player_status.json::case-0007`–`case-0010`: pages 1/2/3 with limit 1 return correct pagination, empty terminal page and reject page 0. Live disposable PostgreSQL suite: 12/12. |
| `PLAYER-01` | clause 2 | deduplicate authenticated players before pagination | VERIFIED | `suite_player_status.json::case-0005` issues two simultaneous list requests using separate TCP connections with the same authenticated player session; each result has unique player IDs and reports the online total after de-duplication. |
| `PLAYER-01` | clause 3 | deterministic ordering | VERIFIED | `suite_player_status.json::case-0005`, `case-0007`, `case-0008`: live response IDs are ascending; page 1 returns the lowest online ID and page 2 the next ID. |
| `PLAYER-01` | clause 4 | return only ID, username, corporation (null if none), alignment | VERIFIED | `suite_player_status.json::case-0005` asserts exact keys on each profile; `case-0008` confirms unaffiliated corporation is JSON null. Python `test_online_player_directory_is_reachable_in_normal_play` passed; Godot command menu `player.list_online` dispatch passed in the 221-check menu regression. |
| `PLANET-01` | clause 1 | `planet.view` read-only | VERIFIED | `suite_planet_fighter_transfers.json::case-0042` snapshots population, allocations, defenses, treasury and stocks before the view; `case-0043` calls the read; `case-0044` compares all authoritative state after. Full disposable PostgreSQL suite passed 78/78. Python and Godot landed-planet view workflows are separately exercised. |
| `PLANET-01` | clause 2 | `planet.info` detailed management | VERIFIED | `suite_planet_fighter_transfers.json::case-0028`–`case-0029` verifies owner details and treasury; `case-0041` verifies a landed visitor lacks treasury. Python detailed overview and Godot `planet.info` action remain available; the new public overview is a separate command. |
| `PLANET-01` | clause 3 | appropriate population/production/morale | VERIFIED | `suite_planet_fighter_transfers.json::case-0043` verifies the response's population against the pre-read PostgreSQL snapshot, slave type, LOW morale, goods and production; `case-0044` proves read-only behavior. Production output uses the actual scheduler overview function. |
| `PLANET-01` | clause 4 | treasury only owner/authorized corporation member | VERIFIED | Owner treasury in `case-0028`–`case-0029`; unauthorized landed visitor omission `case-0041`/`case-0043`; corporation Member treasury `case-0058`; unrelated remote player refusal `case-0059`. Python/Godot menus expose the view/details actions. |
| `NPC-POP-01` | clause 1 | Active target is 2% of universe sectors, minimum 3 | VERIFIED | `suite_npc_population_targets_v3.json::case-0013`; actual `npc_step` on disposable PostgreSQL matched the configured formula against sector and active-trader counts. |
| `NPC-POP-01` | clause 2 | canonical roster has at least 250 stable, individually named identities | VERIFIED | `suite_npc_population_targets_v3.json::case-0014`; 250 distinct display names and trader codes read from the migrated canonical roster. |
| `NPC-POP-01` | clause 3 | reconciliation is idempotent and preserves established NPC state | VERIFIED | `suite_npc_population_targets_v3.json::case-0029`–`case-0032`, `suite_npc_population_restart_verify_v3.json::case-0002`–`case-0003`; a second actual scheduler run and serving-server restart retained target counts, unique ship IDs and sentinel hull/shield/fighter values. |
| `NPC-POP-02` | clause 1 | Active target is 0.5% of sectors, minimum 5 | VERIFIED | `suite_npc_population_targets_v3.json::case-0020`, `suite_npc_population_restart_verify_v3.json::case-0002`; the configured formula matched live active Orion ships before and after serving-server restart. |
| `NPC-POP-02` | clause 2 | canonical roster has at least 100 distinct stable names | VERIFIED | `suite_npc_population_bigbang_roster_v3.json::case-0002` passed against the complete migrated BigBang world after a serving-server restart; SQL confirmed at least 100 distinct named Orion NPC members. |
| `NPC-POP-02` | clause 3 | existing personalities and ship classes remain | VERIFIED | `suite_npc_population_targets_v3.json::case-0021`–`case-0023`, `suite_npc_population_restart_verify_v3.json::case-0004`; acceptance checked distinct configured Orion classes and effective personality precedence (ship override, corporation default, ship-type default, balanced fallback), then confirmed captain/ship sector consistency after restart. |
| `NPC-POP-03` | clause 1 | Five NPC-only classes: Trade Master/Cruiser/Interceptor/Warship/Dreadnought | VERIFIED | `suite_npc_population_targets_v3.json::case-0018`; all five catalogue entries were present and each was non-purchasable with no escape pod. |
| `NPC-POP-03` | clause 2 | allocation 40/30/16/10/4 percent with deterministic counts summing exactly to target population | VERIFIED | `suite_npc_population_targets_v3.json::case-0016`; the 250-entry active roster returned exact deterministic counts 100/75/40/25/10, summing to target. |
| `NPC-POP-03` | clause 3 | retain existing Warship values | VERIFIED | `suite_npc_population_targets_v3.json::case-0017`; authoritative SQL confirmed the approved Ferengi Warship costs, attack, holds, fighter, turn, mine and photon values. |
| `FED-01` | clause 1 | Overt aggression in sectors 1–10 triggers immediate Captain Z intervention, destroying attacker ship and podding player | VERIFIED | `tests.v2/suite_fedspace_aggression_captainz.json`. |
| `FED-01` | clause 2 | no travel delay or duplicate ISS dispatch | VERIFIED | `tests.v2/suite_fedspace_aggression_captainz.json`. |
| `FED-02` | clause 1 | BigBang-generated `msl_sectors` is authoritative and unchanged during game | VERIFIED | `tests.v2/suite_fedspace_msl_asset_enforcement.json`. |
| `FED-02` | clause 2 | outside sectors 1–10, deployable assets are cleared by the once-daily UTC Federation sweep | VERIFIED | `tests.v2/suite_fedspace_msl_asset_enforcement.json`. |
| `FED-02` | clause 3 | Geography comes from generated warp topology, never sector numbering | VERIFIED | `tests.v2/suite_fedspace_msl_asset_enforcement.json`. |
| `FED-03` | clause 1 | ISS is distinct from Captain Z and may act only under explicit Federation jurisdiction/protected-asset rules | VERIFIED | Disposable PostgreSQL `suite_fedspace_aggression_captainz.json` passed 49/49. `case-0036` asserts an ordinary ship target outside FedSpace creates no incident; after the fixture explicitly registers that target as a protected Federation asset, `case-0038`–`case-0039` verify an ordinary-player attack records exactly one ISS incident with that asset and origin sector. |
| `FED-03` | clause 2 | Captain Z violations do not also dispatch ISS | VERIFIED | Same disposable PostgreSQL suite, `case-0008`–`case-0011` and new `case-0018`: Captain Z destroys/pods the aggressive player in sector 1, leaves the target unharmed, and creates no ISS incident. |
| `FED-03` | clause 3 | ISS pursuit is not part of the bounded approved work | IMPLEMENTED_UNTESTED | Contract conflict remains: `V3_GAMEPLAY_DECISIONS.md` says pursuit is outside this bounded work, while `docs/PROTOCOL.v3/27_NPC_and_Ferengi_AI.md` defines ISS movement/attack against active incidents and `src/server_universe.c::iss_patrol_step` implements it. The clause is scope documentation, not an executable behavior; the runtime pursuit was not changed or counted as evidence for this exclusion. |
| `NPC-FER-01` | clause 1 | Attacking or destroying a Ferengi places offender on the Ferengi Empire wanted list with a bounty | VERIFIED | `manual_acceptance/ferengi_destruction_reward.json` 45/45: genuine NPC nonlethal attack creates the Ferengi bounty, repeated attack creates no second bounty, destruction/reward and human-owned Ferengi-corporation exclusion are verified; authenticated balances and SQL ledger are checked. Cases 0030–0039, 0045. |
| `NPC-FER-01` | clause 2 | the most powerful Ferengi captain hunts that offender | VERIFIED | `ferengi_destruction_reward.json::case-0029`–`0033` snapshots the strongest active Ferengi captain before an ordinary player attack and verifies its `ferengi_hunt` intelligence, with no duplicate bounty/action on repeat. `ferengi_destruction_reward_restart.json` 6/6 confirms destruction/reward persistence and retry refusal after server restart. |
| `NPC-ORI-01` | clause 1 | Orion NPCs attack eligible non-allied players entering an Orion-controlled sector through supported travel, subject to established safe-zone and combat protections | VERIFIED | `orion_entry_attack_prepare.json` + `orion_entry_attack_verify.json` passed 80/80 live cases against disposable PostgreSQL. Allied warp and hostile transwarp ran concurrently; SQL confirmed one hostile hit, no allied hit, exactly one action receipt, and no duplicate damage on scheduler retry. A player warped through sectors 1→2→10 where sector 10 was deliberately both FedSpace and Orion-controlled: hull remained unchanged and no Orion attack action was recorded. The NPC was then marked destroyed before settlement and caused no hit/action. Serving-server restart and reconnect were exercised. |
| `NPC-ORI-01` | clause 2 | other personalities retain behavior | VERIFIED | `manual_acceptance/orion_personality_runtime.json` passed 9/9 against the actual registered Orion/NPC scheduler on disposable PostgreSQL. SQL readback confirms balanced movement, offensive movement to persisted intelligence, defensive movement to a warp neighbor, looter movement to a port sector, and exactly one completed durable movement receipt for each personality. Blockader targeting and attacks are verified under clause 1. |
| `SHIP-01` | clause 1 | Sell at StarDock, authorized shipyards and evil/Orion shipyards using DB facility classifications and existing access rules | VERIFIED | `suite_ship_sale_v3.json` (18/18) plus `manual_acceptance/ship_sale_locations_v3.json` (19/19) against disposable PostgreSQL: StarDock, black-market and inventory-authorized facilities, sale audit, proceeds, used-inventory persistence and duplicate-sale rejection. Player-facing access: `client/python_client/tests/test_v3_shipyard_sale_request.py` (2/2); Godot `tests/command_menu_test.gd` emitted the selected `ship.sell` request from the roster sale action (206 checks passed, Godot 4.5.1). |
| `SHIP-02` | clause 1 | `floor((hull base + installed equipment) * percentage)` | VERIFIED | `suite_ship_sale_v3.json`: sale response and authoritative audit valuation are asserted against the integer-floor formula; 27/27 PostgreSQL cases passed. |
| `SHIP-02` | clause 2 | default 60%, integer basis points | VERIFIED | `suite_ship_sale_v3.json` asserts default `shipyard_sale_factor_bp=6000`, separately asserts trade-in remains 5000, and asserts response uses 6000 basis points. |
| `SHIP-02` | clause 3 | Cargo excluded | VERIFIED | `suite_ship_sale_v3.json` includes legacy ORE cargo and a sale receipt declaring cargo forfeiture; proceeds equal hull plus installed equipment valuation only, with exact SQL audit formula. |
| `SHIP-02` | clause 4 | Use classic TradeWars 2002/TWGS 3.13 reference prices for Armid mines (1000), Limpets (10000), Ether probes (3000), photons (40000), and beacons (100) credits | VERIFIED | Live PostgreSQL query in `suite_ship_sale_v3.json` checks MINES 1000, LIMPETS 10000, PROBE 3000, PHOTONS 40000 and BEACONS 100. Historical catalogue remains subject to the documented reference/version caveat. |
| `SHIP-03` | clause 1 | Cargo forfeited, equipment accompanies hull, insurance ends without refund, persistent used inventory, atomic credit/ownership/audit | VERIFIED | `manual_acceptance/ship_sale_atomic_rollback.json::case-0006`–`case-0019`: ordinary player sale at DB-classified StarDock; injected audit failure proved rollback of credits, ship selection, cargo, ownership, insurance, equipment and inventory; retry settled exactly once, retained equipment in used inventory and forfeited cargo/insurance; duplicate sale rejected. 19/19 passed against disposable PostgreSQL. `SHIP-01` client-access tests provide the same public sale workflow through Python and Godot. |
| `SHIP-04` | clause 1 | Cannot sell last eligible ship | VERIFIED | `suite_ship_sale_v3.json` registers a separate player with exactly one owned active hull, docks it at a real StarDock, refuses the sale and checks active identity, ownership, credits and sale audit remain unchanged (27/27). |
| `SHIP-04` | clause 2 | Selling active hull requires explicit eligible replacement | VERIFIED | `suite_ship_sale_v3.json` refuses an active-hull request missing `replacement_ship_id`, then accepts a separately owned eligible replacement in the same facility. |
| `SHIP-04` | clause 3 | switch and sale atomic | VERIFIED | `manual_acceptance/ship_sale_atomic_rollback.json` passed 19/19: injected audit failure rolls back credit, active ship, ownership, cargo, insurance and used inventory; retry switches and settles once. |
| `COMBAT-01` | clause 1 | Every player ship destruction yields escape-pod respawn, independent of installed pod | VERIFIED | `manual_acceptance/combat_ship_destruction_prepare.json` 34/34 against disposable PostgreSQL: ordinary-player combat destroyed a ship type with `has_escape_pod=false`; SQL verified one live Escape Pod active in sector 1. Restart suite `combat_ship_destruction_restart_verify.json` 9/9 confirmed recovery after the serving server restarted. Existing Godot live recovery journey `live_combat_recovery_test.gd` verifies client-visible recovery. |
| `COMBAT-01` | clause 2 | no starter ship | VERIFIED | Same prepare suite asserts the destroyed hull is destroyed and the sole primary ownership is the escape pod; the restart suite reauthenticates the ordinary player and confirms `ship.status` restores the pod. Reserve hull remains non-primary. |
| `COMBAT-01` | clause 3 | Cargo/equipment lost | VERIFIED | Same prepare/restart suites verified legacy cargo and authoritative `ship_cargo` are both empty, hull/fighters/shields/installed shields/transwarp/photons/ORE are cleared. Initial failure exposed authoritative `ship_cargo` surviving destruction; `repo_ships_handle_destruction()` now deletes those rows inside the destruction transaction. Restart SQL confirmed the deletion persisted. |
| `COMBAT-01` | clause 4 | bank, planets and corporate assets persist | VERIFIED | Same suites asserted exact bank and corporation treasury balances, owned planet population/resources, secondary hull and corporation defense assets survive both ordinary combat destruction and Terra enforcement. The independent corporate assets are located outside the battle sector. |
| `COMBAT-01` | clause 5 | exactly one valid active ship | VERIFIED | Same prepare and restart suites assert exactly one primary ownership, it resolves to one undestroyed Escape Pod, and the non-primary reserve hull remains distinct. |
| `COMBAT-01` | clause 6 | idempotent loss | VERIFIED | Same prepare suite retries the attack against the destroyed historical hull and observes no additional pod/event. Restart suite retries after server restart and confirms a single destruction event, one pod and no cargo reappearance. |
| `COMBAT-01` | clause 7 | Terra enforcement is not an exception and must not seize unrelated ships, credits, bank balances, planets or faction assets | VERIFIED | Same prepare suite's ordinary Terra-sanctioned player invokes `combat.attack_planet` against Terra, receives Captain Z refusal (402), and SQL confirms the common destruction consequence plus preserved bank, planet, secondary ship, corp treasury and corp assets. Both suites passed on disposable PostgreSQL with a serving-server restart between them. |
| `COMBAT-02` | clause 1 | Defeated defenses plus successful invasion | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `COMBAT-02` | clause 2 | planet resources and entire treasury stay with transferred planet | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `COMBAT-02` | clause 3 | personal/corporate unrelated assets remain | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `INS-01` | clause 1 | Only ship hulls may be insured | VERIFIED | `suite_economy_insurance.json` passed 22/22 and refused `subject_type=cargo`; `suite_combat_and_crime.json` confirms ordinary-player purchase/list for the player's actual ship. Python/Godot menu regression dispatches only the documented `ship` subject. |
| `INS-01` | clause 2 | Coverage is hull replacement only | VERIFIED | Actual destruction in `suite_combat_and_crime.json` paid the configured policy settlement, capped against the shiptype hull basecost. No cargo or equipment value was included. |
| `INS-01` | clause 3 | qualifying destruction payout is guaranteed to the player's bank | VERIFIED | Same suite verified bank credit from an empty insurer fund, with durable paid claims and explicit guarantee ledger source. |
| `INS-01` | clause 4 | no cargo/equipment cover, direct ship replacement or duplicate payout | VERIFIED | Same destruction receipt preserved the Escape Pod as the player's active ship and paid only the two covered policy amounts, one ledger entry per policy; separate claim retry in `suite_economy_insurance.json` did not duplicate payout. |
| `INS-01` | clause 5 | voluntary sale terminates coverage without refund | VERIFIED | `suite_ship_sale_v3.json` passed 40/40. It bought coverage through the ordinary player command, sold the insured hull, then SQL confirmed policy inactive and bank balance unchanged from post-premium balance. |
| `INS-03` | clause 1 | The policy's configured payout may be below hull replacement value | VERIFIED | `suite_combat_and_crime.json` requested 300 CRD on a hull with basecost 1,000 and asserted the underinsured policy's paid claim was exactly 300. |
| `INS-03` | clause 2 | pay that configured amount, subject to the aggregate hull replacement cap | VERIFIED | Same suite settled the first 300 policy and clamped the second 1,000 policy to the remaining 700; the two payments exactly equaled the 1,000 hull cap. |
| `INS-03` | clause 3 | A separate global valuation setting is not required | VERIFIED | The two policies settled using each persisted configured payout and the hull basecost cap; no global valuation parameter was read or changed in the live acceptance. |
| `MOVE-03` | clause 1 | Interrupted transwarp is excluded from V3 acceptance unless a canonical trigger is subsequently established | VERIFIED | V3 protocol `21_Sector_and_Movement_Commands.md` excludes a noncanonical interruption trigger; `suite_movement_move_transwarp.json` live disposable-PostgreSQL run passed 24/24 and confirmed completed arrival and no interruption event. `test_transwarp_server_contract.py` passed 6/6. |
| `MOVE-03` | clause 2 | No arbitrary random interruption | VERIFIED | Same live 24-case suite asserts no `engine_events` interruption record for the transwarping player; protocol and server contract tests reject arbitrary interruption behavior. |
| `MOVE-01` | clause 1 | Eligibility, fuel and destination checks precede movement | VERIFIED | `suite_movement_move_transwarp.json` 20/20: invalid destination and insufficient fuel leave turns/location/fuel unchanged; successful one-hop movement consumes exactly 3 ORE. `suite_combat_sector_hazards.json` 58/58 adds ordinary warp refusal without turn or state changes. |
| `MOVE-01` | clause 2 | Accepted movement completes at the destination | VERIFIED | `suite_movement_move_warp.json` 13/13 and `suite_movement_move_transwarp.json` 20/20 against disposable PostgreSQL; ordinary warp and transwarp immediately return/persist the requested destination. Python navigation tests passed 36/36; Godot `command_menu_test.gd` verifies both warp and transwarp dispatch (238 checks). |
| `MOVE-01` | clause 3 | destination-entry hazards resolve after arrival in the same transaction | VERIFIED | `suite_combat_sector_hazards.json` 58/58 verifies normal and transwarp arrival damage, consumed hostile assets, persistent events, and lethal escape-pod recovery. Injected DB failure during transwarp mine damage proves location, turn, fuel, hull and defenses all roll back together. |
| `MOVE-01` | clause 4 | Mid-flight interruption is absent and excluded from V3 acceptance unless a canonical trigger is later established | VERIFIED | Published V3 transwarp contract defines synchronous completion and no interruption trigger. `test_transwarp_server_contract.py` 4/4 executes the response/handler guard; live `suite_movement_move_transwarp.json` 20/20 and hazard suite 58/58 exercise completed movements without interruption. No random interruption behavior is accepted. |
| `MOVE-04` | clause 1 | Transwarp is synchronous and atomic | VERIFIED | `suite_movement_move_transwarp.json` passed 24/24 against disposable PostgreSQL, including immediate completed response, persisted destination/fuel state, and refusal cases with no mutation. `suite_combat_sector_hazards.json` acceptance recorded above proves arrival hazards and movement state roll back together on injected database failure. |
| `MOVE-04` | clause 2 | no interruption event is defined in V3 | VERIFIED | Live suite checks zero transwarp interruption events; V3 protocol defines synchronous destination completion without an interruption event. |
| `MOVE-04` | clause 3 | Future acceptance requires a canonical trigger, not arbitrary randomness | VERIFIED | Contract in `21_Sector_and_Movement_Commands.md`; `test_transwarp_server_contract.py` 6/6 verifies complete response and excludes interruption behavior from the handler; no random trigger is implemented. |
| `MOVE-02` | clause 1 | Apply hazards immediately on normal/transwarp sector entry | VERIFIED | `suite_combat_sector_hazards.json` 58/58: ordinary warp and transwarp resolve hostile mine, fighter and limpet hazards in the movement response; friendly/owner assets do not damage the entering ship. |
| `MOVE-02` | clause 2 | persist damage/destruction, trigger pod lifecycle | VERIFIED | The same 58-case disposable-PostgreSQL suite checks hull damage, persistent mine/fighter/limpet events, consumed assets, lethal transwarp sector reporting and exactly one active owned Escape Pod. Injected damage failure confirms all entry state rolls back. |
| `MOVE-02` | clause 3 | no disconnect/prompt bypass | VERIFIED | Live authenticated warp/transwarp requests return only after their entry hazards settle; SQL immediately reads the damage/events/pod state. Python navigation/hazard refresh tests passed 36/36; Godot menu suite verifies both movement requests (238 checks). |
| `PTA-V3-01` | clause 1 | A player with ownership or existing trading authority may bulk-sell planetary ORE/ORG/EQU to a StarPort in the same sector | VERIFIED | `tests.v2/suite_planet_trade_port_pta.json`. |
| `PTA-V3-01` | clause 2 | The port must buy each commodity | VERIFIED | `tests.v2/suite_planet_trade_port_pta.json`. |
| `PTA-V3-01` | clause 3 | Apply authoritative dynamic pricing, stock, capacity, funding and existing access rules | VERIFIED | `tests.v2/suite_planet_trade_port_pta.json`. |
| `PTA-V3-01` | clause 4 | Direct port settlement is separate from Galactic Exchange `planet.market.*` | VERIFIED | `tests.v2/suite_planet_trade_port_pta.json`. |
| `PTA-V3-01` | clause 5 | no ship/cargo is required | VERIFIED | `tests.v2/suite_planet_trade_port_pta.json`. |
| `PTA-V3-01` | clause 6 | zero turns | VERIFIED | `tests.v2/suite_planet_trade_port_pta.json`. |
| `PTA-V3-01` | clause 7 | no port ownership model | VERIFIED | `tests.v2/suite_planet_trade_port_pta.json`. |
| `STARPORT-V3-01` | clause 1 | V3 parity includes constructing a port in an eligible sector, selecting its trading class at construction, upgrading commodity trading capacity and destroying ports under original TradeWars mechanics | VERIFIED | `docs/testing/V3_STARPORT_ACCEPTANCE_20261114.md` and `docs/testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`. |
| `STARPORT-V3-01` | clause 2 | The eight buy/sell combinations are specified | VERIFIED | `docs/testing/V3_STARPORT_ACCEPTANCE_20261114.md` and `docs/testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`. |
| `STARPORT-V3-01` | clause 3 | class selection is only at construction, not reclassification | VERIFIED | `docs/testing/V3_STARPORT_ACCEPTANCE_20261114.md` and `docs/testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`. |
| `STARPORT-V3-01` | clause 4 | At most one active port per sector | VERIFIED | `docs/testing/V3_STARPORT_ACCEPTANCE_20261114.md` and `docs/testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`. |
| `STARPORT-V3-01` | clause 5 | Destroyed port records are retained as inactive history because trade/audit tables hold port foreign keys | VERIFIED | `docs/testing/V3_STARPORT_ACCEPTANCE_20261114.md` and `docs/testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`. |
| `STARPORT-V3-01` | clause 6 | a replacement port may use the sector | VERIFIED | `docs/testing/V3_STARPORT_ACCEPTANCE_20261114.md` and `docs/testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`. |
| `STARPORT-V3-01` | clause 7 | No persistent player/corporation port ownership is introduced | VERIFIED | `docs/testing/V3_STARPORT_ACCEPTANCE_20261114.md` and `docs/testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`. |
| `PARITY-PORT-ATTACK-01` | clause 1 | V3 names `combat.attack.port` and `player.port_strike.v1` | VERIFIED | `tests.v2/manual_acceptance/starport_godot_event_acceptance.json` passed 3/3 and Godot `live_starport_attack_test.gd` passed 8 checks. The authenticated player attack returned `combat.port_strike.v1`; PostgreSQL readback confirmed one durable strike receipt and exactly one correlated `player.port_strike.v1` event. |
| `PARITY-PORT-ATTACK-01` | clause 2 | `ports.size` is capacity, not hull strength | VERIFIED | Disposable PostgreSQL StarPort acceptance showed capacity upgrades tracked independently from the `durability` field; the live attack reduced persisted durability while commodity capacity remained a separate per-commodity upgrade (`V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`, concurrent upgrade/attack; `V3_STARPORT_ACCEPTANCE_20261114.md`, attack/destruction). `tests.v2/test_starport_sector_and_pta_contract.py` passed 5/5 and confirms the separate durable schema fields. |
| `PARITY-PHOTON-01` | clause 1 | Original manual: only Missile Frigates/Imperial StarShips use photons | VERIFIED | `suite_photon_defense_suppression.json::case-0020` passes the shield check with an Imperial Starship; `case-0023` refuses a Merchant Cruiser without ammunition/effect mutation; `case-0028` succeeds with a Missile Frigate. Disposable PostgreSQL, 37/37 total cases. |
| `PARITY-PHOTON-01` | clause 2 | ship/planet shields protect against impact | IMPLEMENTED_UNTESTED | Planetary shield protection is verified by `suite_photon_defense_suppression.json::case-0020` and `case-0026`–`case-0027`: both approved firing classes are refused before ammunition debit or effect creation. The contract's separate ship-shield impact case is not represented by the sector-targeting protocol and remains unverified; do not infer it from the planet case. |
| `PARITY-PHOTON-01` | clause 3 | after shields are defeated specified citadel systems are disabled | VERIFIED | With no active planetary shield and a level-2 citadel reaction configured, `suite_photon_defense_suppression.json::case-0032`–`case-0033` captures the planet during the active photon window, where its reaction multiplier would otherwise prevent capture; SQL confirms the stored reaction level remains 2. The temporary effect suppresses it without destroying configuration. |
| `PARITY-PHOTON-01` | clause 4 | sector mines/fighters are neutralized temporarily | VERIFIED | `suite_photon_defense_suppression.json::case-0030`–`case-0031` preserves hull and both hostile Armid/fighter assets on arrival during suppression; after fixture-expiring the persisted effect and re-entering, `case-0036`–`case-0037` verifies damage/event and consumption. Disposable PostgreSQL, 37/37. |
| `PARITY-TOLL-01` | clause 1 | Original manual: toll fighters stop non-hostile passers, request payment based on deployed quantity, and fight if attacked | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `PARITY-TW-HOMING-01` | clause 1 | A deployed destination-sector fighter is the homing reference | VERIFIED | `suite_movement_move_transwarp.json::case-0021` completes a one-hop transwarp after a destination fighter is deployed; `case-0022` SQL readback confirms arrival and exact 3 ORE debit. The suite passed 23/23. |
| `PARITY-TW-HOMING-01` | clause 2 | validate and lock it inside the movement transaction before turn/location mutations | VERIFIED | `suite_movement_move_transwarp.json::case-0018` refuses absent homing fighter (error 1458) and `case-0019` confirms fuel, turns and sector unchanged. `tests.v2/test_transwarp_server_contract.py::test_destination_validation_precedes_drive_and_turn_mutation` verifies lock/validation order; all focused tests passed. |
| `MOVE-05` | clause 1 | TWGS 3.13 baseline: 3 Fuel Ore per sector of distance, measured as the shortest directed warp-hop count in the authoritative universe graph | VERIFIED | `tests.v2/suite_movement_move_transwarp.json`. |
| `V3-PLANET-RATE-01` | clause 1 | Per 10-minute production tick, workers required per unit (ORE/ORG/EQU): M 100/100/100 | VERIFIED | All 21 built-in class/commodity rows read from migrated PostgreSQL and asserted individually by `suite_planet_production_rate_values.json` (21 assertions). Values remain explicitly noncanonical twclone defaults. |
| `V3-PLANET-RATE-01` | clause 2 | L 50/100/100 | VERIFIED | `suite_planet_production_rate_values.json` PostgreSQL migration acceptance (21 assertions). Noncanonical twclone default. |
| `V3-PLANET-RATE-01` | clause 3 | O 100/50/100 | VERIFIED | `suite_planet_production_rate_values.json` PostgreSQL migration acceptance (21 assertions). Noncanonical twclone default. |
| `V3-PLANET-RATE-01` | clause 4 | K 50/100/100 | VERIFIED | `suite_planet_production_rate_values.json` PostgreSQL migration acceptance (21 assertions). Noncanonical twclone default. |
| `V3-PLANET-RATE-01` | clause 5 | H 50/100/50 | VERIFIED | `suite_planet_production_rate_values.json` PostgreSQL migration acceptance (21 assertions). Noncanonical twclone default. |
| `V3-PLANET-RATE-01` | clause 6 | C 100/100/100 | VERIFIED | `suite_planet_production_rate_values.json` PostgreSQL migration acceptance (21 assertions). Noncanonical twclone default. |
| `V3-PLANET-RATE-01` | clause 7 | U disabled | VERIFIED | `suite_planet_production_rate_values.json` PostgreSQL migration acceptance (21 assertions). Noncanonical twclone default. |
| `V3-PLANET-RATE-01` | clause 8 | Integer remainder carries per planet and commodity | VERIFIED | `planet_rate_remainder_prepare.json` + `planet_rate_remainder_restart_verify.json`, actual `planet_growth` handler, disposable PostgreSQL, 17/17 cases, real server restart; ORE/ORG/EQU each carry 1 then 2 assigned workers, then create one unit and clear remainder. Existing `suite_planet_production_rate_values.json` verifies all 21 migrated defaults. |
| `V3-TOLL-RATE-01` | clause 1 | Default 1 credit per deployed toll fighter, charged once per attempted sector entry | VERIFIED | Three deployed toll fighters produced a 3-credit offer and single payment: `suite_movement_transwarp_tolls.json::case-0012`–`case-0015`. Noncanonical twclone default. |
| `V3-TOLL-RATE-01` | clause 2 | configurable integer credits per fighter, minimum 0 | VERIFIED | `manual_acceptance/toll_rate_configurable.json::case-0002`–`case-0003`, `case-0019`–`case-0022`, and `case-0023`–`case-0031`: configure 7 credits/fighter, ordinary transwarp payer settles a 21-credit offer for three fighters once; zero is accepted and allows toll-free arrival; original setting restored. 31/31 passed against disposable PostgreSQL. Configurable value remains a noncanonical twclone default. |
| `V3-PHOTON-DURATION-01` | clause 1 | Default 600 seconds from successful launch | VERIFIED | `suite_photon_duration_acceptance.json::case-0003`, `case-0006`, `case-0011`; disposable PostgreSQL migration/config/fire result records exact expiry from successful request creation. Noncanonical twclone default; not a TWGS parity claim. |
| `V3-PHOTON-DURATION-01` | clause 2 | configurable duration in seconds, minimum 1 | VERIFIED | `suite_photon_duration_acceptance.json::case-0007`–`case-0011`; one-second duration accepted, zero and above-maximum rejected, exact expiry and one ammunition debit asserted. Noncanonical twclone setting. |
| `V3-PORT-BUILD-01` | clause 1 | Initial construction: 100,000 credits plus 1,000 each ORE/ORG/EQU | VERIFIED | Disposable PostgreSQL construction replay after a lost reply and serving-server restart returned the same construction ID; SQL balances showed 500,000→400,000 credits and 2,000→1,000 of each material exactly once (`V3_STARPORT_ACCEPTANCE_20261114.md`, construction/replay). |
| `V3-PORT-BUILD-01` | clause 2 | three daily deliveries of 100 each commodity (1,300 total of each material) | VERIFIED | `V3_STARPORT_ACCEPTANCE_20261114.md`, three scheduled deliveries across serving-server restarts: each consumed 100 ORE/ORG/EQU once; completion persisted and a further restart did not replay delivery. Injected delivery failure rolled back before retry. |
| `V3-PORT-BUILD-01` | clause 3 | class-specific exchange directions fixed at completion | VERIFIED | `tests.v2/suite_starport_class_directions.json` passed 7/7 against disposable PostgreSQL after advancing the actual construction scheduler. The completed records and each port's initialized stock established all eight fixed buy/sell class directions. Godot `live_starport_build_test.gd` separately passed 8 UI/network checks and verified a class-8 build through the Planet menu. |
| `V3-PORT-BUILD-01` | clause 4 | Capacity upgrades: 10,000 credits and 100 units of the selected commodity per level, increasing that commodity's capacity by 1,000 per level, with maximum level 10 | VERIFIED | Disposable PostgreSQL StarPort upgrade acceptance verified selected ORE debit, 10,000-credit charge per level, +1,000 capacity per level, insufficient-material refusal, retry idempotency, two concurrent serialized upgrades, and persistence after server restart (`V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`, concurrency/recovery). |
| `V3-PORT-BUILD-01` | clause 5 | The construction planet supplies materials | VERIFIED | Construction retry and three-delivery PostgreSQL acceptance observed ORE/ORG/EQU debits in the source planet; rollback preserved the planet balances (`V3_STARPORT_ACCEPTANCE_20261114.md`, construction/delivery). |
| `V3-PORT-BUILD-01` | clause 6 | the upgrading ship supplies the selected commodity and the active port receives the credit cost | VERIFIED | PostgreSQL upgrade acceptance checked the upgrading player's ship cargo debit and active port petty-cash credit, once per committed level; concurrent and same-key retries preserved exact totals (`V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`). |
| `V3-PORT-COMBAT-01` | clause 1 | 10,000 durability | VERIFIED | Disposable PostgreSQL attack/destruction acceptance observed a new tested port at 10,000 durability, reduced to 1,000 after the first strike and tombstoned by the next strike (`V3_STARPORT_ACCEPTANCE_20261114.md`, unprotected-sector attack). |
| `V3-PORT-COMBAT-01` | clause 2 | 1,000 defensive strength | VERIFIED | `tests.v2/suite_starport_combat_numeric.json` passed 12/12 against disposable PostgreSQL. Its first strike against a ship with 999 shields and 5 fighters left 0 shields, 4 fighters and unchanged hull, demonstrating exactly 1,000 retaliation damage. |
| `V3-PORT-COMBAT-01` | clause 3 | each attack consumes one turn and deals `max(1, shiptypes.maxattack - 1,000)` durability damage | VERIFIED | `tests.v2/suite_starport_combat_numeric.json` passed 12/12. Two authenticated attacks used live `shiptypes.maxattack`; SQL confirmed formula-derived durability loss and turn balance 1,000→999→998. |
| `V3-PORT-COMBAT-01` | clause 4 | retaliation deals 1,000 damage in the existing shield→fighter→hull order | VERIFIED | `tests.v2/suite_starport_combat_numeric.json` passed 12/12. PostgreSQL-backed player state showed the first 1,000 damage consumed shields then fighters; a second attack with no shields and 999 fighters lost one hull point after all fighters were consumed. |
| `V3-PORT-COMBAT-01` | clause 5 | Remaining commodity stock is destroyed on port destruction | VERIFIED | Same disposable PostgreSQL destruction acceptance asserted the inactive historical port row remained and its remaining stock became zero with no salvage (`V3_STARPORT_ACCEPTANCE_20261114.md`). Injected destruction-stock failure rolled the port, stock, turn and receipt back (`V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`). |
| `V3-PORT-COMBAT-01` | clause 6 | FedSpace Captain Z enforcement applies | VERIFIED | `suite_fedspace_aggression_captainz.json` passed 49/49 on disposable PostgreSQL; the live ordinary-player port-attack refusal and escape-pod recovery are recorded in `V3_STARPORT_ACCEPTANCE_20261114.md`. |
| `CORP-XFER-01` | clause 1 | Restore same-sector corporation-associate transfer for on-hand credits and active-ship fighters, mines, and current shield charge | VERIFIED | `tests.v2/suite_corp_member_asset_transfers.json`. |
| `CORP-XFER-01` | clause 2 | Sender and recipient must be current members of the same corporation and sector | VERIFIED | `tests.v2/suite_corp_member_asset_transfers.json`. |
| `CORP-XFER-01` | clause 3 | Enforce source holdings, recipient hull capacities, and atomic conservation | VERIFIED | `tests.v2/suite_corp_member_asset_transfers.json`. |
| `CORP-XFER-01` | clause 4 | no turn/credit fee | VERIFIED | `tests.v2/suite_corp_member_asset_transfers.json`. |
| `SYS-07` | clause 1 | `sysop.player.kick` must invalidate every durable session for the target and disconnect active connections | VERIFIED | `tests.v2/test_sysop_players.json`: disposable PostgreSQL, target session count zero and forced connection closed; 15/15 cases. |
| `SYS-07` | clause 2 | a subsequent request using an old token must be unauthenticated | VERIFIED | `tests.v2/test_sysop_players.json`: runner reconnects without reauthentication and replays the cached token; server returns 401; 15/15 cases. |
| `CFG-01` | clause 1 | `shipyard_sale_factor_bp`, integer 0–10000, default 6000 | VERIFIED | `suite_ship_sale_v3.json::case-0003`–`case-0006`, 19/19; live setting reads 6000, accepts valid value and rejects out-of-range input. |
| `CFG-01` | clause 2 | distinct from trade-in | VERIFIED | `suite_ship_sale_v3.json::case-0004`–`case-0005`, 19/19; changing standalone factor leaves trade-in factor at 5000. |
| `CFG-02` | clause 1 | Fixed 50% | VERIFIED | `manual_acceptance/slave_unrest_production_prepare.json::case-0007`–`case-0013` and `slave_unrest_production_verify.json::case-0001`–`case-0004`; actual production scheduler comparison confirms floor(normal output × 50/100). |
| `CFG-02` | clause 2 | not an adjustable setting under current policy | VERIFIED | `slave_unrest_production_prepare.json::case-0002` asserts the authenticated SysOp config listing does not expose a slave-output setting; 15/15 preparation and 8/8 verification cases passed. |
| `CFG-03` | clause 1 | `planet.slave_unrest_interval_days`, `planet.slave_unrest_stoppage_cycles`, `planet.slave_unrest_population_loss` | VERIFIED | `suite_slave_unrest_config_defaults.json` reads all three approved settings via authenticated `sysop.config.get`; 12/12 cases passed against disposable PostgreSQL. |
| `CFG-03` | clause 2 | zero is an unset/disabled sentinel | VERIFIED | Same suite asserts each fresh migration default is zero and runs the authoritative handler against a due slave planet without activating an event. |
| `CFG-03` | clause 3 | no active gameplay defaults | VERIFIED | `suite_slave_unrest_config_defaults.json` confirms no event, stoppage, loss or allocation change when fresh defaults remain zero; numeric POP-05 policy remains separately unresolved. |
| `CFG-04` | clause 1 | Existing eligibility and coverage parameters retained | VERIFIED | `suite_economy_insurance.json::case-0001`–`case-0006`, `case-0014`–`case-0015`, 22/22; ordinary authenticated purchase, cargo exclusion and eligibility path exercised. |
| `CFG-04` | clause 2 | hull-only payout to bank | VERIFIED | `suite_economy_insurance.json::case-0007`–`case-0018`, 22/22; exact hull payout credited to bank once, retry does not duplicate, injected ledger failure rolls policy/bank/fund/ledger state back. |


## Autonomous parity batch 17 — StarPort acceptance closure

`PARITY-PORT-ATTACK-01`, `V3-PORT-BUILD-01`, and `V3-PORT-COMBAT-01` are now
VERIFIED. Batch evidence and the actual Godot construction journey are in
`docs/testing/V3_STARPORT_GODOT_ACCEPTANCE_20261119.md`. The deterministic
construction tick's successful handler return was corrected in
`src/server_cron.c`; a nonzero processed-row count had been interpreted by the
cron dispatcher as a handler failure even after commit.

## Clause-level findings beyond the ten unit clauses

The parent status is intentionally not copied onto every child clause. In particular, PARTIAL/BLOCKED parent rows are now split into implemented-but-untested behavior and the exact known gap/decision below.

| Requirement | Additional independently testable clause | Status | Evidence |
|---|---|---|---|
| `POP-01` | Complete capture/acquisition transitions among colonist, slave, and unpopulated states with counts and allocations preserved | VERIFIED | Existing populated-world transition cases `suite_combat_advanced.json::case-0023`–`case-0039`, together with disposable PostgreSQL `suite_planet_unpopulated_acquisition_prepare.json::case-0021`–`case-0044` and actual serving-server restart verification `suite_planet_unpopulated_acquisition_restart_verify.json::case-0008`–`case-0013`. The prepare/restart suites executed 57 cases, 0 failed. The prepare suite verifies empty population/allocations, planetary stock and treasury conservation, database-failure rollback, player↔corporation ownership transfer, former-owner denial, and an injected competing-claim conflict at capture settlement with complete transaction rollback. The restart suite reauthenticates ordinary players, checks authoritative `planet.info`, confirms former-owner access denial, and verifies persisted state and exactly one capture event. Python attack workflow tests: `client/python_client/tests/test_planet_workflows.py -k 'planet_attack'` (`9 passed, 69 deselected`); Godot `planet_command_response_test.gd` (`117 checks passed`). |
| `EXC-09` | Demonstrate sustainable NPC/commodity balancing under normal production and market depletion | DEFECTIVE_OR_INCOMPLETE | Decision register explicitly leaves broader balancing required; no acceptance threshold is approved for a full balance pass. |
| `PARITY-TOLL-01` | Toll offer and settlement on transwarp sector entry | VERIFIED | `suite_movement_transwarp_tolls.json` and `suite_movement_transwarp_toll_edges.json`; ordinary authenticated server requests and PostgreSQL state assertions listed above. |
| `POP-05` | Approve daily recurrence interval, stoppage duration, and population-loss amount | BLOCKED | The V3 decision register leaves these three numeric policy values unset. |
| `COMBAT-01` | Ordinary-player ship destruction, escape-pod respawn, and recovery after server restart | VERIFIED | Godot live acceptance `live_two_player_combat_test.gd` and `live_combat_recovery_test.gd`; disposable PostgreSQL evidence recorded in the 16 Nov completion tracker. This does not verify all destruction side effects or independence from installed escape-pod equipment. |
| `PARITY-PORT-ATTACK-01` | One ordinary-player StarPort attack destroys the seeded port, applies retaliation, consumes a turn, clears stock, and persists the attack receipt | VERIFIED | Godot `live_starport_attack_test.gd`; Python menu path and authoritative PostgreSQL readback recorded in the 16 Nov completion tracker. This does not cover every durability/defence or FedSpace case. |
| `PARITY-PHOTON-01` | Adjacent firing, approved ship classes, planetary shield refusal, citadel reaction suppression, defense neutralization and expiry | PARTIAL | `suite_photon_defense_suppression.json` (37/37) verifies both approved classes, ineligible-class refusal, planetary shield protection, temporary citadel reaction suppression, mine/fighter neutralization and expiry recovery. `suite_photon_atomic_rollback.json` (15/15) injects a failure after ammunition/effect mutation and verifies the ammo, effect, request and event all roll back, then retries successfully with one debit. The V3 command only accepts `sector_id`; the historical manual says ship and planetary shields protect against impact but does not specify a ship-target selection/wire contract for this sector effect. Ship-shield impact semantics therefore remain unresolved; no new targeting behavior was inferred. Parent remains PARTIAL. |
| `PARITY-TOLL-01` | Normal/transwarp acceptance/refusal, owner-split atomic payment, insufficient-funds refusal, idempotent retry, and destruction recovery | VERIFIED | Normal toll suite plus the focused transwarp suites below; the new mixed-owner/restart tests and live Godot UI journey verify both settlement and recovery. |
| `PARITY-TW-HOMING-01` | Destination fighter eligibility, refusal without state mutation, atomic reference lock and successful movement | VERIFIED | Disposable PostgreSQL `suite_movement_move_transwarp.json` passed 23/23. It refused missing reference with no ORE/turn/location change, then succeeded after a fighter was deployed and charged exactly 3 ORE. Python focused navigation/server tests passed 30/30; Godot 4.5.1 command menu passed 238 checks including transwarp dispatch. `tests.v2/test_transwarp_server_contract.py` asserts lock validation precedes fuel/turn/location mutation. |
| `POP-02` | Evil/neutral slave acquisition and capture disposition | VERIFIED | Disposable PostgreSQL `suite_combat_advanced.json` passed 47/47, including evil enslavement, good liberation, neutral enslavement refusal without any state change, and neutral inheritance. `suite_neutral_slave_trade.json` passed 14/14 and confirmed neutral purchase refusal conserves credits, turns, cargo and port stock. Python attack workflow tests passed 9/9; Godot command-menu dispatch passed 238 checks, planet response passed 117, and presenter passed 13. |

## Exact remaining specification blockers

- `POP-05`: numerical daily recurrence interval, stoppage duration, and population-loss amount remain unset. The per-production-cycle values in ruleset 1.2 are a compatible separate rule and do not replace POP-05.
- `EXC-09`: the 30-day evaluation horizon is approved, but no numeric sustainability/balance criterion is approved. The qualitative principle cannot be used as a balancing pass/fail rule; no economic constant was tuned.

## Ruleset 1.2 acceptance — 18 November 2026

| Clause slice | Acceptance condition | Executed evidence | Result |
|---|---|---|---|
| `POP-05/production-cycle transition` | Slave cycles add 2 unrest; non-slave cycles subtract 1; level stays in 0–100; disruption chance is `min(floor(level/10),10)%` | `slave_unrest_production_prepare.json` + `slave_unrest_production_verify.json`, disposable PostgreSQL, 20/20 cases | VERIFIED |
| `POP-05/production-cycle accounting` | Disruption skips current-cycle output without population loss; duplicate tick does not repeat event, output or unrest | `slave_unrest_production_verify.json::case-0002`–`case-0004`, disposable PostgreSQL | VERIFIED |
| `NPC-FER-01/eligible destruction reward` | Genuine Ferengi NPC destruction credits exactly 5,000 system-generated gameplay credits once; player-owned Ferengi-corporation ship is ineligible | `ferengi_destruction_reward.json`, disposable PostgreSQL, 35/35 cases | VERIFIED |
| `NPC-FER-01/reward restart` | Destruction sequence and reward persist after serving-server restart; retry cannot pay twice | `ferengi_destruction_reward_restart.json`, disposable PostgreSQL, 6/6 cases | VERIFIED |

POP-05 daily acceptance passed 30/30 live PostgreSQL cases across
`slave_unrest_daily_prepare.json` and
`slave_unrest_daily_restart_verify.json`, with a serving-server restart between
suites. It verified configured daily evaluation, proportional assignment loss,
persistent stoppage and recovery, unchanged player ownership and `SLAVE`
classification, idempotent replay, retry after rollback, and injected database
failure rollback. In the updated run, two authenticated scheduler calls were
issued concurrently; SQL confirmed exactly one event and population loss. The
parent POP-05 requirement remains BLOCKED because recurrence, stoppage duration
and population-loss policy values are still unset.

The deterministic 30-day simulation passed 64/64 cases in
`economy_30_day_simulation.json`, using the actual registered scheduler
handlers. Daily snapshots checked player/corporation/bank credits, planet and
port stock, population, production remainders, reward counts, bank/corporation
flows by mechanism, planetary output and bounded-state invariants. This does
not establish economy-wide balance. `suite_black_market_hardware.json` passed
34/34 after its restock deadline was advanced through the actual scheduler;
the stock assertion is unchanged and the wall-clock wait is removed.

Regression closure: the full disposable server run passed 2,621 cases in 165
suites with zero failures; its aggregate run skipped the TLS suite because TLS
configuration is enabled only when that suite is explicitly selected. The
separate TLS-enabled run passed 4/4, so all 166/166 discovered suites executed
with 2,625 passed and zero failed. Python client: 809 passed. `tests.v2` Python:
274 passed, 7 skipped, 227 subtests passed. Godot 4.5.1 imported successfully;
106 scripts passed, 0 failed and 6 live-mode scripts skipped (112 discovered).
Godot loopback mock-server tests ran with local network permission and a writable
`XDG_DATA_HOME` under `/tmp`.

## Acceptance update — 2026-11-18

- `NPC-ORI-01` hostile targeting was approved as a TWClone V3 project rule and
  exercised through a disposable PostgreSQL serving server. Ordinary warp and
  transwarp arrivals were both tested. The non-allied player's hull fell by
  exactly 10, the Orion corporation member was not attacked, and a same-window
  handler retry did not repeat combat damage. The serving game process started
  before Orion faction records were created; the live handler then retried
  initialization and successfully controlled the seeded NPC fleet. A
  secondary ship owned by the human Orion member generated no Orion NPC action.
  Both suites passed: 60 executed cases, 0 failed, including refusal of the
  SysOp-only scheduler hook to an ordinary player.
- The run exposed that Orion roster queries counted human corporation members
  as NPC fleet vessels. `repo_universe_get_orion_ships` and the population
  reconciler now restrict control/counting to ships owned by `is_npc=TRUE`
  players. No migration is required. Existing human member ships are preserved;
  on the next population reconciliation, only actual NPC vessels count toward
  the configured Orion target, so the reconciler may add missing NPC ships.
- The retry case exposed that `repo_npc_action_claim` treated a successful
  `ON CONFLICT DO NOTHING` with zero inserted rows as a new claim. It now
  returns a new claim only when `db_exec_rows_affected` reports an inserted row.
  The test observed one 10-point hull loss, one completed blockade-action row,
  and no additional hull loss or action after retry.
- The acceptance invokes `sys.cron.npc_step_once` as a SysOp-only test/operations
  hook to run the same authoritative `h_npc_step` handler deterministically;
  ordinary players still perform both travel operations.
- `NPC-ORI-01` is no longer specification-blocked. Its full parent requirement
  remains PARTIAL until safe-zone/protected-player, NPC-removal race, defense
  reinforcement and non-blockader behavior are covered.
- Ruleset 1.2's production-cycle unrest is compatible with POP-05: the former
  changes the 10-minute cycle level and may skip one cycle; POP-05 remains the
  separate persisted daily stoppage, recovery and configured loss mechanism.
  No conflict required overriding either rule. The 20/20 cycle acceptance is
  recorded above; POP-05 daily parameters remain unset and its full lifecycle is
  still unverified.
- The approved Ferengi reward is independent from the existing wanted-list
  penalty. Its 5,000 system-generated credits are recorded in a separate durable
  reward ledger and passed the 35-case authenticated acceptance plus 6-case
  restart acceptance above. The existing penalty and pursuit clauses remain
  unverified.
- No daily POP-05 policy defaults or economy balancing constants were added.

## Acceptance update — 2026-11-19 NPC population batch

- `NPC-POP-01` through `NPC-POP-03` are VERIFIED. Acceptance ran against both
  the ordinary disposable PostgreSQL fixture and the complete migrated
  BigBang world. Each world ran the 32-case target/fleet suite; serving-server
  restart verification passed 4/4 in each world, and the full BigBang roster
  check passed 2/2. The restart cases authenticated again through the restarted
  game server and read persisted authoritative rows.
- The target suite proves Ferengi 2%/minimum-3 and Orion 0.5%/minimum-5 active
  populations, 250 unique Ferengi identities, 100+ stable Orion identities,
  the 40/30/16/10/4 class allocation, NPC-only classes, retained Warship
  values, idempotent scheduler replay, and preservation of sentinel combat
  state. It also checks Orion class/personality precedence and captain/ship
  sector consistency.
- The run found two Orion initializer defects and fixed them: wrong corporation
  identifier passed when linking deployed captains, and captain sector not
  updated with the newly deployed ship. The raw-SQL diagnostic's display of a
  null text field as integer zero was a serializer quirk; acceptance now checks
  effective personality via the approved precedence chain. No unsupported
  legacy migration was retained.
- Focused Python checks: 18 passed, 206 subtests passed. `make -j4` passed.
- Parent classification after this batch: **39 VERIFIED, 20
  IMPLEMENTED_UNVERIFIED, 7 PARTIAL, 1 BLOCKED**. Clause classification:
  **116 VERIFIED, 111 IMPLEMENTED_UNTESTED** of the 227 base rows.


## Acceptance update — 2026-11-18 parity closure batch 3

- `HARV-02` is VERIFIED: `manual_acceptance/harvest_permissions_atomic.json` passed 40/40 on disposable PostgreSQL. Owner and corporation Officer permissions succeeded; visitor and corporation Member attempts were refused; authoritative stock/cargo reads confirmed no unauthorized withdrawal. A controlled `ship_cargo` update failure rolled harvest stock and cargo back.
- `HARV-03` is VERIFIED: `manual_acceptance/planet_exchange_sale_harvest_atomic.json` passed 27/27, including failed-sale transaction rollback, successful Exchange settlement and a later harvest returning zero sold units. `planet_exchange_sale_harvest_restart_verify.json` passed 8/8 after a serving-server restart and reconnect. The Python surface workflow test passed and Godot command-menu regression passed 212 checks with `planet.market.sell` dispatch coverage.
- `BANK-04` is VERIFIED: the first scheduler phase passed 17/17; after restarting the serving V3 process, the second phase passed 11/11. PostgreSQL confirms the paid transfer and its ledger legs remain final while a missing-recipient occurrence cancels once.
- `TRADE-01` is VERIFIED: no-haggle/fixed quote acceptance passed 9/9; market shock dynamic pricing and expiry passed 12/12; deterministic market settlement/idempotent replay passed 43/43. The existing taxable-market fixture's wall-clock race was removed by advancing only the real settlement task under the test-only scheduler, keeping its exact quantity and balance assertions.
- `NPC-FER-01` is VERIFIED: ordinary-player Ferengi attack/wanted-list/pursuit/retry/destruction acceptance passed 45/45; a separate real serving-server restart/reconnect phase passed 6/6. The fixture snapshots the highest-power captain before the attack so the target cannot accidentally be mistaken for its own pursuit vessel.
- Focused Python planet workflow regression: 79 passed. Godot command-menu regression: 212 checks passed. The game server implementation was already present; no gameplay server defect was found in these five parents. Python and Godot client coverage was added for the missing planetary Exchange sale UI workflow.
- `POP-07` remains IMPLEMENTED_UNVERIFIED: the corporation-capture sentence is a documentary scope constraint, not a runtime prohibition, and no behavior was invented to force a negative test. `SHIP-01` remains VERIFIED; the earlier Python/Godot sale-menu evidence already satisfies its required client access.
- Classification moves from 22 VERIFIED / 37 IMPLEMENTED_UNVERIFIED / 7 PARTIAL / 1 BLOCKED to 27 / 32 / 7 / 1.

## Acceptance update — 2026-11-16

- Full disposable PostgreSQL V3 regression: **2,462 passed, 37 failed, 4 skipped** across 161/162 executed suites (2,499 cases executed). The remaining failures are unrelated existing suite fixtures/timing: black-market restock timing (1), generated Quasar fixture absent (9), Police Phase B random-cluster fixture resolution (16), and Phase D1 lawful-cluster fixture resolution (8). Toll failures from this run were rerun and closed by the focused passes below; the full suite was not rerun after those fixture corrections.
- Toll fixture corrections: `suite_combat_toll_fighters.json` and `suite_movement_transwarp_toll_edges.json` now pass **75/75** cases in a fresh serving-server run.
- Mixed-owner transwarp toll acceptance: `suite_movement_transwarp_toll_mixed_owners.json` **42/42** and `suite_movement_transwarp_toll_mixed_owners_restart.json` **3/3**, with `--restart-between-suites`. SQL checks verify exact credit split, 3 Fuel Ore and one turn consumed, no duplicate settlement on replay, refusal without payment, ship destruction, escape-pod ownership, and post-restart persistence.
- Population: `suite_combat_advanced.json` **44/44** passed in the full live-server run, including evil enslavement, good liberation, neutral slave retention, and planetary asset/treasury conservation. `suite_planet_colonist_transfers.json` **27/27** passed. Serving-process restart pair `suite_planet_colonist_restart_prepare.json` **12/12** and `suite_planet_colonist_restart_verify.json` **8/8** passed with a real game-server restart between suites; transfer rollback remains covered by cases 25–26.
- Migration 171: `psql -f tests.v2/test_pg_migration_171_population_state.sql` passed in an isolated disposable schema, checking populated/empty migration classification, population/allocation conservation, legacy insertion classification, and slave state retention.
- Godot live transwarp toll workflow: `gdot --headless --path client/godot_client/godot --script res://tests/live_transwarp_toll_test.gd` exited 0 with the live mode and fixture/restart marker environment enabled. SQL readback confirmed paid offer 4 credits split 2/2 between two owners, no paid-arrival fighter event, refused offer with no payment, refusal fighter event, and persisted player balance/sector after the serving-process restart.

## Executed focused gates

- Disposable PostgreSQL / actual V3 server: four focused suites, **136 passed, 0 failed** (`suite_combat_advanced.json` 44; `suite_movement_transwarp_toll_edges.json` 57; `suite_movement_transwarp_tolls.json` 15; `suite_planet_colonist_transfers.json` 20). The run retained response capture under `/tmp/twclone-v3-toll-population-verified-transcript.json`.
- The colonist transfer suite was extended with zero, insufficient-population and full-hold boundaries and rerun with the production-rate suite: **27 passed, 0 failed** (26 colonist cases plus 1 rate suite; 21 rate assertions).
- Disposable PostgreSQL / actual V3 server: existing landed slave delivery, harvest, fighter transfer, emancipation and access-control regression `suite_planet_fighter_transfers.json` — **49 passed, 0 failed**.
- Disposable PostgreSQL / actual V3 server restart between suites using `--restart-between-suites`: population preparation **12 passed**, serving process stopped/restarted, then authenticated readback and another transfer **8 passed**. Both reports and sanitized protocol captures are under `/tmp/twclone-v3-colonist-restart-*`.
- Disposable PostgreSQL / actual V3 server: `suite_planet_production_rate_values.json` **1 passed, 0 failed; 21 assertions passed**, validating the approved versioned noncanonical class-rate rows after migration.
- `gdot --headless --path client/godot_client/godot --script res://tests/toll_offer_workflow_test.gd` — **13 checks passed** (Godot 4.5.1). `XDG_DATA_HOME=/tmp/twclone-godot-xdg` was used because the default Godot user log path is not writable in this sandbox.
- Python client plus focused server contracts: `python3 -m pytest -q client/python_client/tests tests.v2/test_planetary_server_contract.py tests.v2/test_v3_planet_production_rates.py` — **866 passed**.
- `bash tests.v2/run_engine_regressions.sh` — PASS (planet fighter production arithmetic, slave population loss allocation, gameplay engine regression unit tests).
- `tests.v2/run_engine_regressions.sh` — PASS; slave-output/loss allocation, fighter-production arithmetic, and bank schedule runners completed.
- `python3 -m pytest -q tests.v2/test_v3_planet_production_rates.py tests.v2/test_planetary_server_contract.py tests.v2/test_bank_standing_order_contract.py tests.v2/test_sysop_session_kick_contract.py tests.v2/test_ship_sale_equipment_catalog.py tests.v2/test_insurance_server_contract.py` — **72 passed, 17 subtests passed**.
- These checks do not claim database-backed acceptance for broader production or standing-order requirements. Next, select the smallest remaining `IMPLEMENTED_UNTESTED` clauses and run their exact contract, then PostgreSQL persistence tests where state is involved.
