# V3 clause-level acceptance matrix

Working matrix derived from the approved decision register and the 67-row parity closure. Parent requirement status is separate from clause evidence: passing one clause does not promote its parent. “Implemented but untested” means an implementation path exists but no clause-specific executed acceptance is recorded.

Reviewed: 16 November 2026, following commits `97f32ceb`, `0f8c9b21`, and `1c42d647`.

## Current measures

- Parent requirements: 67 (13 VERIFIED, 44 IMPLEMENTED_UNVERIFIED, 7 PARTIAL, 3 BLOCKED; unchanged).
- Base clause rows: 227 (56 VERIFIED, 171 IMPLEMENTED_UNTESTED). Addenda identify 4 additional verified behavior slices, 3 concrete incomplete behaviors, and the 3 exact blocked decisions. These clause counts do not replace the 67 parent requirement counts.
- The initial 227 rows split the approved statements by sentence and semicolon. Compound statements are further separated where acceptance evidence distinguishes behavior; remaining compound rows are explicitly a working decomposition and should be refined when their acceptance tests are selected.
- Ten simple arithmetic/date clauses were executed in this run. They do not promote POP-03 or BANK-03 as whole requirements.

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

## Clause ledger

| Requirement | Clause | Acceptance condition | Clause status | Evidence / next action |
|---|---|---|---|---|
| `POP-01` | clause 1 | A planet is `COLONIST`, `SLAVE`, or `UNPOPULATED` | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-01` | clause 2 | never mixed | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-01` | clause 3 | Preserve counts and allocations | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-01` | clause 4 | Existing populated planets migrate as colonists | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-01` | clause 5 | empty planets remain unpopulated | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-02` | clause 1 | Evil players may buy slaves or choose enslavement on successful conquest | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-02` | clause 2 | Neutral may inherit but cannot buy/enslave | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-02` | clause 3 | Good cannot retain slaves after qualifying capture | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-02` | clause 4 | Never create remote planetary population without safe transport | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-03` | ORE output | floor(normal_output * 50 / 100) for ORE | VERIFIED | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `POP-03` | ORG output | floor(normal_output * 50 / 100) for ORG | VERIFIED | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `POP-03` | EQU output | floor(normal_output * 50 / 100) for EQU | VERIFIED | `tests.v2/test_slave_population_split.c::main`; `tests.v2/run_engine_regressions.sh` PASS. |
| `POP-04` | clause 1 | Slave morale is permanently low and cannot improve | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-04` | clause 2 | No additional continuous output modifier | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-04` | clause 3 | Preserve existing colonist rules | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-04` | clause 4 | invent no dynamic colonist morale | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-05` | clause 1 | Evaluate daily | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-05` | clause 2 | deterministic, persisted, restart/idempotency-safe | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-05` | clause 3 | May stop production and reduce slave population | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-05` | clause 4 | may not change ownership, destroy planets, liberate or convert population | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-05` | clause 5 | Configured loss is allocated proportionally across production assignments using deterministic largest-remainder integer allocation | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-05` | clause 6 | exact total loss is preserved | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `POP-06` | clause 1 | Owner or authorized corporation management (Leader/Officer) converts all slaves to colonists atomically, preserving population and allocations | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-06` | clause 2 | no turn/credit cost | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-06` | clause 3 | no partial conversion | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-07` | clause 1 | Capture requires defeated defenses and successful invasion | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-07` | clause 2 | Evil explicitly chooses retain/enslave colonists | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-07` | clause 3 | good automatically liberates slaves | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-07` | clause 4 | neutral retains inherited slaves | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-07` | clause 5 | evil may retain them | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `POP-07` | clause 6 | Planet resources/treasury remain with captured planet | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
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
| `BANK-03` | configured-day retention | retain the originally configured day for future recurrence | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
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
| `NPC-FER-01` | clause 1 | Attacking or destroying a Ferengi places offender on the Ferengi Empire wanted list with a bounty | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `NPC-FER-01` | clause 2 | the most powerful Ferengi captain hunts that offender | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `NPC-ORI-01` | clause 1 | Blockader holds assigned home sector, respects target/faction/deployment restrictions, attacks eligible hostile ships and may reinforce using existing fighter/mine systems | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `NPC-ORI-01` | clause 2 | other personalities retain behavior | IMPLEMENTED_UNTESTED | Implementation is present, but this clause lacks its own executed acceptance evidence. |
| `SHIP-01` | clause 1 | Sell at StarDock, authorized shipyards and evil/Orion shipyards using DB facility classifications and existing access rules | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
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
| `V3-PLANET-RATE-01` | clause 1 | Per 10-minute production tick, workers required per unit (ORE/ORG/EQU): M 100/100/100 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PLANET-RATE-01` | clause 2 | L 50/100/100 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PLANET-RATE-01` | clause 3 | O 100/50/100 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PLANET-RATE-01` | clause 4 | K 50/100/100 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PLANET-RATE-01` | clause 5 | H 50/100/50 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PLANET-RATE-01` | clause 6 | C 100/100/100 | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PLANET-RATE-01` | clause 7 | U disabled | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-PLANET-RATE-01` | clause 8 | Integer remainder carries per planet and commodity | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `V3-TOLL-RATE-01` | clause 1 | Default 1 credit per deployed toll fighter, charged once per attempted sector entry | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
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
| `SYS-07` | clause 1 | `sysop.player.kick` must invalidate every durable session for the target and disconnect active connections | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
| `SYS-07` | clause 2 | a subsequent request using an old token must be unauthenticated | IMPLEMENTED_UNTESTED | No executed clause-specific acceptance reference recorded yet. |
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
| `POP-01` | Complete capture/acquisition transitions among colonist, slave, and unpopulated states with counts and allocations preserved | DEFECTIVE_OR_INCOMPLETE | Decision register says complete acquisition/capture transitions are required; no end-to-end transition acceptance identified. |
| `EXC-09` | Demonstrate sustainable NPC/commodity balancing under normal production and market depletion | DEFECTIVE_OR_INCOMPLETE | Decision register explicitly leaves broader balancing required; no acceptance threshold is approved for a full balance pass. |
| `PARITY-TOLL-01` | Toll offer and settlement on transwarp sector entry | DEFECTIVE_OR_INCOMPLETE | `docs/testing/V3_TOLL_FIGHTER_ACCEPTANCE_20261114.md` records that transwarp arrivals do not present toll offers. |
| `POP-05` | Approve daily recurrence interval, stoppage duration, and population-loss amount | BLOCKED | The V3 decision register leaves these three numeric policy values unset. |
| `NPC-FER-01` | Approve the canonical bounty amount | BLOCKED | Existing 5,000-credit source value is not an approved canonical rule. |
| `NPC-ORI-01` | Define eligible hostile targets and alliance/faction targeting restrictions | BLOCKED | No approved targeting rule resolves this boundary. |
| `COMBAT-01` | Ordinary-player ship destruction, escape-pod respawn, and recovery after server restart | VERIFIED | Godot live acceptance `live_two_player_combat_test.gd` and `live_combat_recovery_test.gd`; disposable PostgreSQL evidence recorded in the 16 Nov completion tracker. This does not verify all destruction side effects or independence from installed escape-pod equipment. |
| `PARITY-PORT-ATTACK-01` | One ordinary-player StarPort attack destroys the seeded port, applies retaliation, consumes a turn, clears stock, and persists the attack receipt | VERIFIED | Godot `live_starport_attack_test.gd`; Python menu path and authoritative PostgreSQL readback recorded in the 16 Nov completion tracker. This does not cover every durability/defence or FedSpace case. |
| `PARITY-PHOTON-01` | Adjacent eligible firing, ammo debit, same-request/concurrent retry, and expiry persistence | VERIFIED | `docs/testing/V3_PHOTON_ACCEPTANCE_20261114.md`, ordinary Python menu journey against disposable PostgreSQL. Shield/citadel suppression, defense neutralization at arrival, effect-expiry behavior, and fault rollback remain unverified. |
| `PARITY-TOLL-01` | Normal-warp acceptance/refusal, atomic payment, insufficient-funds refusal, and duplicate/concurrent retry | VERIFIED | `docs/testing/V3_TOLL_FIGHTER_ACCEPTANCE_20261114.md`, ordinary Python warp workflow and PostgreSQL balances/receipts. Transwarp, mixed ownership, and destruction during refusal remain incomplete/unverified. |

## Exact remaining specification blockers

- `POP-05`: numerical daily recurrence interval, stoppage duration, and population-loss amount.
- `NPC-FER-01`: approved bounty amount.
- `NPC-ORI-01`: targeting/faction eligibility rules for the Orion Blockader.

No defaults were added for these blockers.

## Executed focused gates

- `tests.v2/run_engine_regressions.sh` — PASS; slave-output/loss allocation, fighter-production arithmetic, and bank schedule runners completed.
- `python3 -m pytest -q tests.v2/test_v3_planet_production_rates.py tests.v2/test_planetary_server_contract.py tests.v2/test_bank_standing_order_contract.py tests.v2/test_sysop_session_kick_contract.py tests.v2/test_ship_sale_equipment_catalog.py tests.v2/test_insurance_server_contract.py` — **72 passed, 17 subtests passed**.
- These checks do not claim database-backed acceptance for broader production or standing-order requirements. Next, select the smallest remaining `IMPLEMENTED_UNTESTED` clauses and run their exact contract, then PostgreSQL persistence tests where state is involved.
