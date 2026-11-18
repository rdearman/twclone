# V3 clause-level acceptance matrix

Working matrix derived from the approved decision register and the 67-row parity closure. Parent requirement status is separate from clause evidence: passing one clause does not promote its parent. “Implemented but untested” means an implementation path exists but no clause-specific executed acceptance is recorded.

Reviewed: 18 November 2026, after BANK-03 live monthly recurrence acceptance.

## Current measures

- Parent requirements: 67 (18 VERIFIED, 41 IMPLEMENTED_UNVERIFIED, 7 PARTIAL, 1 BLOCKED). `BANK-03`, `V3-PLANET-RATE-01`, `SYS-07` and `SHIP-01` are now VERIFIED. `POP-01` remains VERIFIED. `NPC-FER-01` remains IMPLEMENTED_UNVERIFIED because retaliation and pursuit clauses are unverified.
- Base clause rows: 227 (65 VERIFIED, 162 IMPLEMENTED_UNTESTED). Addenda identify further independently verified behavior slices, concrete incomplete behaviors, and the 3 exact blocked decisions. These clause counts do not replace the 67 parent requirement counts.
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

| Requirement | Clause | Acceptance condition | Clause status | Evidence / next action |
|---|---|---|---|---|
| `POP-01` | clause 1 | A planet is `COLONIST`, `SLAVE`, or `UNPOPULATED` | VERIFIED | `tests.v2/test_pg_migration_171_population_state.sql` ran against disposable PostgreSQL; assertions exercised all three classifications and passed. |
| `POP-01` | clause 2 | never mixed | VERIFIED | Migration trigger test in `tests.v2/test_pg_migration_171_population_state.sql` preserved the single enum classification on populated, empty, and slave updates. |
| `POP-01` | clause 3 | Preserve counts and allocations | VERIFIED | `tests.v2/test_pg_migration_171_population_state.sql` asserts population/allocation equality on migration and update; `suite_combat_advanced.json::case-0024`, `case-0029`, `case-0034`, `case-0039` assert acquisition preservation. |
| `POP-01` | clause 4 | Existing populated planets migrate as colonists | VERIFIED | `tests.v2/test_pg_migration_171_population_state.sql` migration assertion: existing population 100 becomes `COLONIST`, unchanged. |
| `POP-01` | clause 5 | empty planets remain unpopulated | VERIFIED | `tests.v2/test_pg_migration_171_population_state.sql` migration assertion: empty planet remains `UNPOPULATED`, population 0. |
| `POP-02` | clause 1 | Evil players may buy slaves or choose enslavement on successful conquest | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-02` | clause 2 | Neutral may inherit but cannot buy/enslave | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-02` | clause 3 | Good cannot retain slaves after qualifying capture | VERIFIED | `suite_combat_advanced.json::case-0033`–`case-0034`: successful good capture liberates slaves and preserves population, allocations, treasury, and planetary stock. |
| `POP-02` | clause 4 | Never create remote planetary population without safe transport | VERIFIED | Remote pickup refusal and unchanged DB state: `suite_planet_colonist_transfers.json::case-0010`–`case-0011`; landed pickup/drop-off and conservation: `case-0020`–`case-0023`; current landed slave delivery and cargo/population state: `suite_planet_fighter_transfers.json::case-0043`–`case-0044`. |
| `POP-03` | ORE output | floor(normal_output * 50 / 100) for ORE | VERIFIED | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `POP-03` | ORG output | floor(normal_output * 50 / 100) for ORG | VERIFIED | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `POP-03` | EQU output | floor(normal_output * 50 / 100) for EQU | VERIFIED | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `POP-04` | clause 1 | Slave morale is permanently low and cannot improve | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-04` | clause 2 | No additional continuous output modifier | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-04` | clause 3 | Preserve existing colonist rules | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-04` | clause 4 | invent no dynamic colonist morale | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-05` | clause 1 | Evaluate daily | VERIFIED | `slave_unrest_daily_prepare.json` configured a one-day interval and ran the production handler against due and future events; 18/18 cases passed. |
| `POP-05` | clause 2 | deterministic, persisted, restart/idempotency-safe | IMPLEMENTED_UNTESTED | `slave_unrest_daily_prepare.json` and `slave_unrest_daily_restart_verify.json` passed 30/30 cases for replay idempotency, restart persistence, recovery and rollback. Concurrent scheduler invocation remains untested, so this clause is not promoted. |
| `POP-05` | clause 3 | May stop production and reduce slave population | VERIFIED | `slave_unrest_daily_prepare.json` asserts configured four-person loss, persistent stoppage and exact assigned-population result; restart suite verifies recovery without repeated loss. 30/30 combined. |
| `POP-05` | clause 4 | may not change ownership, destroy planets, liberate or convert population | VERIFIED | Both POP-05 daily fixtures assert the planet remains present, player-owned and `SLAVE`-typed after loss and restart; 30/30 combined. |
| `POP-05` | clause 5 | Configured loss is allocated proportionally across production assignments using deterministic largest-remainder integer allocation | VERIFIED | `slave_unrest_daily_prepare.json` asserts 4 lost from assignments 10/5/3/2 produces 8/4/2/2; `slave_unrest_daily_restart_verify.json` confirms persisted allocation and successful retry. |
| `POP-05` | clause 6 | exact total loss is preserved | VERIFIED | `slave_unrest_daily_prepare.json` checks population 20→16 and one event with `applied_population_loss=4`; restart/retry suite checks exact single settlement. |
| `POP-06` | clause 1 | Owner or authorized corporation management (Leader/Officer) converts all slaves to colonists atomically, preserving population and allocations | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-06` | clause 2 | no turn/credit cost | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-06` | clause 3 | no partial conversion | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-07` | clause 1 | Capture requires defeated defenses and successful invasion | VERIFIED | `suite_combat_advanced.json::case-0023`–`case-0024`: successful invasion followed by DB ownership/population/asset assertions. |
| `POP-07` | clause 2 | Evil explicitly chooses retain/enslave colonists | VERIFIED | `suite_combat_advanced.json::case-0025`–`case-0029`: evil player explicitly selects enslavement; DB checks population type and ownership/defenses. |
| `POP-07` | clause 3 | good automatically liberates slaves | VERIFIED | `suite_combat_advanced.json::case-0030`–`case-0034`: good capture converts slaves and conserves population/assets. |
| `POP-07` | clause 4 | neutral retains inherited slaves | VERIFIED | `suite_combat_advanced.json::case-0035`–`case-0039`: neutral capture retains slave classification and planetary assets. |
| `POP-07` | clause 5 | evil may retain them | VERIFIED | `suite_combat_advanced.json::case-0028`–`case-0029`: explicit evil enslavement succeeds and is persisted. |
| `POP-07` | clause 6 | Planet resources/treasury remain with captured planet | VERIFIED | `suite_combat_advanced.json::case-0024`, `case-0034`, `case-0039` assert captured-planet assets, treasury, and population allocations remain attached. |
| `POP-07` | clause 7 | No corporation-capture rule is inferred | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `HARV-01` | clause 1 | `planet.harvest` transfers accumulated ore/organics/equipment only to currently landed ship, capped to available holds | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` and disposable PostgreSQL harvest journey in `V3_GAMEPLAY_COMPLETION_TRACKER.md`. |
| `HARV-01` | clause 2 | remainder stays on planet | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` and disposable PostgreSQL harvest journey in `V3_GAMEPLAY_COMPLETION_TRACKER.md`. |
| `HARV-01` | clause 3 | zero turns | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` and disposable PostgreSQL harvest journey in `V3_GAMEPLAY_COMPLETION_TRACKER.md`. |
| `HARV-01` | clause 4 | no generation | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` and disposable PostgreSQL harvest journey in `V3_GAMEPLAY_COMPLETION_TRACKER.md`. |
| `HARV-01` | clause 5 | atomic | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` and disposable PostgreSQL harvest journey in `V3_GAMEPLAY_COMPLETION_TRACKER.md`. |
| `HARV-02` | clause 1 | Use existing authoritative ownership/corporation permissions | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `HARV-02` | clause 2 | no unrestricted withdrawal | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `HARV-03` | clause 1 | Planet surplus may be sold directly to Exchange | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `HARV-03` | clause 2 | sale immediately removes available planetary stock, so sold goods cannot later be harvested | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `EXC-01` | clause 1 | Planets buy/sell Exchange goods | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus PostgreSQL sale→port-purchase readback cited by the parity closure. |
| `EXC-01` | clause 2 | ports buy from Exchange | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus PostgreSQL sale→port-purchase readback cited by the parity closure. |
| `EXC-01` | clause 3 | preserve compatible port, order and NPC trade paths | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus PostgreSQL sale→port-purchase readback cited by the parity closure. |
| `EXC-02` | clause 1 | Validate source stock and destination funds/capacity, transfer inventory and credits, record durable transaction atomically | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus PostgreSQL ledger settlement assertions cited by the parity closure. |
| `EXC-02` | clause 2 | completed goods arrive immediately | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus PostgreSQL ledger settlement assertions cited by the parity closure. |
| `EXC-03` | clause 1 | Sold goods leave seller ownership and cannot be harvested, resold, built with, consumed or counted twice | VERIFIED | `tests.v2/suite_bucket1_remainder_planet.json` plus second-tick stock readback cited by the parity closure. |
| `EXC-04` | clause 1 | Deliveries are immediate economic transfers | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `EXC-04` | clause 2 | no physical NPC ships, movement, fuel, holds, docking or delay | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `EXC-05` | clause 1 | Keep placement, reservation, execution and settlement distinct | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `EXC-05` | clause 2 | Open orders wait for matches | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `EXC-05` | clause 3 | partial fills preserve remainder | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `EXC-05` | clause 4 | no duplicate settlement | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
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
| `SHIP-05` | clause 1 | Used hull is acquired through existing shipyard workflow at floor((original hull value + retained equipment value) * configured percentage), default 80% | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `SHIP-05` | clause 2 | preserve hull identity/equipment and do not create a new hull | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `SHIP-05` | clause 3 | Standalone ship-sale payout remains 60% | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-02` | clause 1 | Every qualifying hull claim pays the covered replacement value directly to the player's bank regardless of insurer reserves | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-02` | clause 2 | Explicit system credit creation/virtual insurer funding is allowed and must be identifiable | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-02` | clause 3 | Exactly once | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-02` | clause 4 | cargo/equipment excluded | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-02` | clause 5 | escape pod independent | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `TRADE-01` | clause 1 | No haggling | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `TRADE-01` | clause 2 | Existing fixed-at-quote authoritative price and configured dynamic supply/demand/black-market pricing remain | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-01` | clause 1 | Daily, weekly, monthly at specified UTC time | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-01` | clause 2 | any existing player may be recipient | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-01` | clause 3 | ISO weekday numbering is used on the wire | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-01` | clause 4 | monthly day is retained as configured | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-02` | clause 1 | Skip that occurrence without debt or same-occurrence retry | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-02` | clause 2 | next occurrence remains active | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-03` | 2023 February clamp | configured day 31 clamps to 2023-02-28 at the requested UTC time | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | 2024 February clamp | configured day 31 clamps to leap day 2024-02-29 | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | 2100 century rule | 2100 is not a leap year; day 31 clamps to 2100-02-28 | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | 2000 century rule | 2000 is a leap year; day 31 clamps to 2000-02-29 | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | 30-day month clamp | April day 31 clamps to April 30 without changing the UTC time | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | configured-day retention | retain the originally configured day for future recurrence | VERIFIED | `bank_monthly_retention_prepare.json` + `bank_monthly_retention_restart_verify.json`, disposable PostgreSQL, 24/24 cases, actual server restart; SQL confirms `month_day=31`, one paid occurrence, exactly two bank ledger legs, and next occurrence on day 31. Authenticated `bank.orders.list` returns day 31 after restart. |
| `BANK-03` | invalid month rejection | month 13 returns invalid schedule (epoch 0) | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-03` | invalid day rejection | day 0 returns invalid schedule (epoch 0) | VERIFIED | `tests.v2/test_bank_schedule.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `BANK-04` | clause 1 | If recipient no longer exists, cancel order automatically | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-04` | clause 2 | completed transfers remain final | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-05` | clause 1 | Create/list/cancel | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-05` | clause 2 | durable exact-once occurrence, atomic bank debit/credit/ledger | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-05` | clause 3 | cancellation serializes with execution | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `BANK-05` | clause 4 | no arbitrary order cap | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `PLAYER-01` | clause 1 | 1-based page/limit | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `PLAYER-01` | clause 2 | deduplicate authenticated players before pagination | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `PLAYER-01` | clause 3 | deterministic ordering | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `PLAYER-01` | clause 4 | return only ID, username, corporation (null if none), alignment | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `PLANET-01` | clause 1 | `planet.view` read-only | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `PLANET-01` | clause 2 | `planet.info` detailed management | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `PLANET-01` | clause 3 | appropriate population/production/morale | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `PLANET-01` | clause 4 | treasury only owner/authorized corporation member | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `NPC-POP-01` | clause 1 | Active target is 2% of universe sectors, minimum 3 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `NPC-POP-01` | clause 2 | canonical roster has at least 250 stable, individually named identities | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `NPC-POP-01` | clause 3 | reconciliation is idempotent and preserves established NPC state | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `NPC-POP-02` | clause 1 | Active target is 0.5% of sectors, minimum 5 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `NPC-POP-02` | clause 2 | canonical roster has at least 100 distinct stable names | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `NPC-POP-02` | clause 3 | existing personalities and ship classes remain | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `NPC-POP-03` | clause 1 | Five NPC-only classes: Trade Master/Cruiser/Interceptor/Warship/Dreadnought | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `NPC-POP-03` | clause 2 | allocation 40/30/16/10/4 percent with deterministic counts summing exactly to target population | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `NPC-POP-03` | clause 3 | retain existing Warship values | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `FED-01` | clause 1 | Overt aggression in sectors 1–10 triggers immediate Captain Z intervention, destroying attacker ship and podding player | VERIFIED | `tests.v2/suite_fedspace_aggression_captainz.json`. |
| `FED-01` | clause 2 | no travel delay or duplicate ISS dispatch | VERIFIED | `tests.v2/suite_fedspace_aggression_captainz.json`. |
| `FED-02` | clause 1 | BigBang-generated `msl_sectors` is authoritative and unchanged during game | VERIFIED | `tests.v2/suite_fedspace_msl_asset_enforcement.json`. |
| `FED-02` | clause 2 | outside sectors 1–10, deployable assets are cleared by the once-daily UTC Federation sweep | VERIFIED | `tests.v2/suite_fedspace_msl_asset_enforcement.json`. |
| `FED-02` | clause 3 | Geography comes from generated warp topology, never sector numbering | VERIFIED | `tests.v2/suite_fedspace_msl_asset_enforcement.json`. |
| `FED-03` | clause 1 | ISS is distinct from Captain Z and may act only under explicit Federation jurisdiction/protected-asset rules | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `FED-03` | clause 2 | Captain Z violations do not also dispatch ISS | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `FED-03` | clause 3 | ISS pursuit is not part of the bounded approved work | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `NPC-FER-01` | clause 1 | Attacking or destroying a Ferengi places offender on the Ferengi Empire wanted list with a bounty | IMPLEMENTED_UNTESTED | Existing penalty implementation remains; no clause-specific live acceptance was run in this batch. |
| `NPC-FER-01` | clause 2 | the most powerful Ferengi captain hunts that offender | IMPLEMENTED_UNTESTED | Existing pursuit implementation remains; no clause-specific live acceptance was run in this batch. |
| `NPC-ORI-01` | clause 1 | Orion NPCs attack eligible non-allied players entering an Orion-controlled sector through supported travel, subject to established safe-zone and combat protections | IMPLEMENTED_UNTESTED | `orion_entry_attack_prepare.json` + `orion_entry_attack_verify.json`: 60/60 live cases passed, including warp/transwarp, allied exclusion, hostile damage, restart persistence and same-window idempotency. Safe-zone arrival, additional protected-player exclusions, NPC-removal race and genuinely concurrent arrivals remain unverified. |
| `NPC-ORI-01` | clause 2 | other personalities retain behavior | IMPLEMENTED_UNTESTED | No behavior-change regression is claimed; no complete runtime personality matrix has been executed. |
| `SHIP-01` | clause 1 | Sell at StarDock, authorized shipyards and evil/Orion shipyards using DB facility classifications and existing access rules | VERIFIED | `suite_ship_sale_v3.json` (18/18) plus `manual_acceptance/ship_sale_locations_v3.json` (19/19) against disposable PostgreSQL: StarDock, black-market and inventory-authorized facilities, sale audit, proceeds, used-inventory persistence and duplicate-sale rejection. |
| `SHIP-02` | clause 1 | `floor((hull base + installed equipment) * percentage)` | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `SHIP-02` | clause 2 | default 60%, integer basis points | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `SHIP-02` | clause 3 | Cargo excluded | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `SHIP-02` | clause 4 | Use classic TradeWars 2002/TWGS 3.13 reference prices for Armid mines (1000), Limpets (10000), Ether probes (3000), photons (40000), and beacons (100) credits | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `SHIP-03` | clause 1 | Cargo forfeited, equipment accompanies hull, insurance ends without refund, persistent used inventory, atomic credit/ownership/audit | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `SHIP-04` | clause 1 | Cannot sell last eligible ship | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `SHIP-04` | clause 2 | Selling active hull requires explicit eligible replacement | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `SHIP-04` | clause 3 | switch and sale atomic | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `COMBAT-01` | clause 1 | Every player ship destruction yields escape-pod respawn, independent of installed pod | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `COMBAT-01` | clause 2 | no starter ship | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `COMBAT-01` | clause 3 | Cargo/equipment lost | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `COMBAT-01` | clause 4 | bank, planets and corporate assets persist | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `COMBAT-01` | clause 5 | exactly one valid active ship | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `COMBAT-01` | clause 6 | idempotent loss | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `COMBAT-01` | clause 7 | Terra enforcement is not an exception and must not seize unrelated ships, credits, bank balances, planets or faction assets | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `COMBAT-02` | clause 1 | Defeated defenses plus successful invasion | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `COMBAT-02` | clause 2 | planet resources and entire treasury stay with transferred planet | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `COMBAT-02` | clause 3 | personal/corporate unrelated assets remain | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `INS-01` | clause 1 | Only ship hulls may be insured | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-01` | clause 2 | Coverage is hull replacement only | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-01` | clause 3 | qualifying destruction payout is guaranteed to the player's bank | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-01` | clause 4 | no cargo/equipment cover, direct ship replacement or duplicate payout | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-01` | clause 5 | voluntary sale terminates coverage without refund | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-03` | clause 1 | The policy's configured payout may be below hull replacement value | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-03` | clause 2 | pay that configured amount, subject to the aggregate hull replacement cap | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `INS-03` | clause 3 | A separate global valuation setting is not required | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-03` | clause 1 | Interrupted transwarp is excluded from V3 acceptance unless a canonical trigger is subsequently established | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-03` | clause 2 | No arbitrary random interruption | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-01` | clause 1 | Eligibility, fuel and destination checks precede movement | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-01` | clause 2 | Accepted movement completes at the destination | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-01` | clause 3 | destination-entry hazards resolve after arrival in the same transaction | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-01` | clause 4 | Mid-flight interruption is absent and excluded from V3 acceptance unless a canonical trigger is later established | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-04` | clause 1 | Transwarp is synchronous and atomic | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-04` | clause 2 | no interruption event is defined in V3 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-04` | clause 3 | Future acceptance requires a canonical trigger, not arbitrary randomness | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-02` | clause 1 | Apply hazards immediately on normal/transwarp sector entry | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-02` | clause 2 | persist damage/destruction, trigger pod lifecycle | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `MOVE-02` | clause 3 | no disconnect/prompt bypass | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
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
| `PARITY-PORT-ATTACK-01` | clause 1 | V3 names `combat.attack.port` and `player.port_strike.v1` | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `PARITY-PORT-ATTACK-01` | clause 2 | `ports.size` is capacity, not hull strength | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `PARITY-PHOTON-01` | clause 1 | Original manual: only Missile Frigates/Imperial StarShips use photons | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `PARITY-PHOTON-01` | clause 2 | ship/planet shields protect against impact | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `PARITY-PHOTON-01` | clause 3 | after shields are defeated specified citadel systems are disabled | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `PARITY-PHOTON-01` | clause 4 | sector mines/fighters are neutralized temporarily | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `PARITY-TOLL-01` | clause 1 | Original manual: toll fighters stop non-hostile passers, request payment based on deployed quantity, and fight if attacked | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `PARITY-TW-HOMING-01` | clause 1 | A deployed destination-sector fighter is the homing reference | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `PARITY-TW-HOMING-01` | clause 2 | validate and lock it inside the movement transaction before turn/location mutations | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
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
| `V3-TOLL-RATE-01` | clause 2 | configurable integer credits per fighter, minimum 0 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PHOTON-DURATION-01` | clause 1 | Default 600 seconds from successful launch | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PHOTON-DURATION-01` | clause 2 | configurable duration in seconds, minimum 1 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-BUILD-01` | clause 1 | Initial construction: 100,000 credits plus 1,000 each ORE/ORG/EQU | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-BUILD-01` | clause 2 | three daily deliveries of 100 each commodity (1,300 total of each material) | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-BUILD-01` | clause 3 | class-specific exchange directions fixed at completion | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-BUILD-01` | clause 4 | Capacity upgrades: 10,000 credits and 100 units of the selected commodity per level, increasing that commodity's capacity by 1,000 per level, with maximum level 10 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-BUILD-01` | clause 5 | The construction planet supplies materials | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-BUILD-01` | clause 6 | the upgrading ship supplies the selected commodity and the active port receives the credit cost | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-COMBAT-01` | clause 1 | 10,000 durability | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-COMBAT-01` | clause 2 | 1,000 defensive strength | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-COMBAT-01` | clause 3 | each attack consumes one turn and deals `max(1, shiptypes.maxattack - 1,000)` durability damage | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-COMBAT-01` | clause 4 | retaliation deals 1,000 damage in the existing shield→fighter→hull order | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-COMBAT-01` | clause 5 | Remaining commodity stock is destroyed on port destruction | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PORT-COMBAT-01` | clause 6 | FedSpace Captain Z enforcement applies | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `CORP-XFER-01` | clause 1 | Restore same-sector corporation-associate transfer for on-hand credits and active-ship fighters, mines, and current shield charge | VERIFIED | `tests.v2/suite_corp_member_asset_transfers.json`. |
| `CORP-XFER-01` | clause 2 | Sender and recipient must be current members of the same corporation and sector | VERIFIED | `tests.v2/suite_corp_member_asset_transfers.json`. |
| `CORP-XFER-01` | clause 3 | Enforce source holdings, recipient hull capacities, and atomic conservation | VERIFIED | `tests.v2/suite_corp_member_asset_transfers.json`. |
| `CORP-XFER-01` | clause 4 | no turn/credit fee | VERIFIED | `tests.v2/suite_corp_member_asset_transfers.json`. |
| `SYS-07` | clause 1 | `sysop.player.kick` must invalidate every durable session for the target and disconnect active connections | VERIFIED | `tests.v2/test_sysop_players.json`: disposable PostgreSQL, target session count zero and forced connection closed; 15/15 cases. |
| `SYS-07` | clause 2 | a subsequent request using an old token must be unauthenticated | VERIFIED | `tests.v2/test_sysop_players.json`: runner reconnects without reauthentication and replays the cached token; server returns 401; 15/15 cases. |
| `CFG-01` | clause 1 | `shipyard_sale_factor_bp`, integer 0–10000, default 6000 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `CFG-01` | clause 2 | distinct from trade-in | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `CFG-02` | clause 1 | Fixed 50% | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `CFG-02` | clause 2 | not an adjustable setting under current policy | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `CFG-03` | clause 1 | `planet.slave_unrest_interval_days`, `planet.slave_unrest_stoppage_cycles`, `planet.slave_unrest_population_loss` | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `CFG-03` | clause 2 | zero is an unset/disabled sentinel | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `CFG-03` | clause 3 | no active gameplay defaults | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `CFG-04` | clause 1 | Existing eligibility and coverage parameters retained | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `CFG-04` | clause 2 | hull-only payout to bank | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |


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
| `PARITY-PHOTON-01` | Adjacent eligible firing, ammo debit, same-request/concurrent retry, and expiry persistence | VERIFIED | `docs/testing/V3_PHOTON_ACCEPTANCE_20261114.md`, ordinary Python menu journey against disposable PostgreSQL. Shield/citadel suppression, defense neutralization at arrival, effect-expiry behavior, and fault rollback remain unverified. |
| `PARITY-TOLL-01` | Normal/transwarp acceptance/refusal, owner-split atomic payment, insufficient-funds refusal, idempotent retry, and destruction recovery | VERIFIED | Normal toll suite plus the focused transwarp suites below; the new mixed-owner/restart tests and live Godot UI journey verify both settlement and recovery. |

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
failure rollback. Concurrent scheduler invocations remain untested; the parent
POP-05 requirement remains BLOCKED because recurrence, stoppage duration and
population-loss policy values are still unset.

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
