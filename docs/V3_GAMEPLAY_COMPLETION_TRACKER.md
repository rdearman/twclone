# V3 Gameplay Completion Tracker

This cumulative tracker covers every requirement ID in the master V3 gameplay
completion assignment. Status is conservative: source presence is not test
verification. `VERIFIED` is reserved for tests actually executed against the
relevant behavior. The policy source is [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md);
Exchange requirements additionally use [`V3_GALACTIC_EXCHANGE_CONTRACT.md`](V3_GALACTIC_EXCHANGE_CONTRACT.md).

Status values: `NOT_STARTED`, `PARTIAL`, `IMPLEMENTED_UNVERIFIED`, `VERIFIED`,
`BLOCKED`. Verification listed here is evidence already executed, not a plan.
Database-backed evidence for the 2026-11-10 QA integration pass is appended below;
it does not convert source/contract coverage into runtime verification unless
the specific workflow was exercised.

## Current-state reconciliation (2026-11-11)

The earlier baseline contained 97 requirement rows. The current register has
105 unique requirement IDs: 91 `IMPLEMENTED_UNVERIFIED`, 7 verified entries
(four scoped-path variants plus three `VERIFIED`), 4 `PARTIAL` requirements,
and 3 `BLOCKED` requirements. Four former partials
(`EXC-003/004/006/011`) have source paths for
the approved behavior; their outstanding work is specifically runtime,
restart, or concurrency evidence, so they are not described as missing code.
`MOVE-001` now describes the approved synchronous, atomic transwarp behavior;
mid-flight interruption is explicitly excluded from V3 acceptance absent a
future canonical trigger. `SYS-004` is now
`IMPLEMENTED_UNVERIFIED`: policy permits below-replacement coverage, and source
honors the configured payout; DB settlement remains unverified. The 89
unverified items must not be read as 89 missing implementations; their
row-level source and evidence fields state
the known verification gap. This count is a source/tracker reconciliation,
not release certification.

| ID | Description | Status | Implementation location | Test location / executed result | Commit | Remaining action |
|---|---|---|---|---|---|---|
| SHIP-001 | Eligible sale locations | IMPLEMENTED_UNVERIFIED | `src/server_ships.c`, `src/server_stardock.c`, `src/db/repo/repo_stardock.c` | Sale JSON scenario authored and parsed; no runtime execution | `bcaf3e93` | Verify database classifications/police restrictions |
| SHIP-002 | Integer 60% hull+equipment valuation | IMPLEMENTED_UNVERIFIED | `repo_stardock.c`, `server_stardock.c`; migrations `178_ship_sale_equipment_catalogue.sql`, MySQL `130_ship_sale_equipment_catalogue.sql` | Historical classic TWGS 3.13 price reference recorded in `docs/V3_EQUIPMENT_CATALOGUE.md`; 62 focused tests passed (including catalogue, manifest, and ship-contract checks); JSON sale case asserts exact 54,100-credit equipment subtotal; ASan/UBSan build passed; DB/server runtime not executed | `b6c0777b` | Execute migration and `suite_ship_sale_v3.json` against disposable PostgreSQL; verify exact equipment subtotal and purchase limits |
| SHIP-003 | Cargo forfeiture | IMPLEMENTED_UNVERIFIED | used-sale transaction in `repo_stardock.c` | JSON scenario asserts legacy and normalized cargo cleared; not runtime-run | `bcaf3e93` | Verify loss in DB-backed flow |
| SHIP-004 | Equipment remains with used hull | IMPLEMENTED_UNVERIFIED | `repo_stardock.c`, migration 170/122 | JSON scenario checks persistent ship identity and used inventory reference; not runtime-run | `bcaf3e93` | Verify identifiable used-hull contents |
| SHIP-005 | Used hull is purchasable | IMPLEMENTED_UNVERIFIED | `cmd_shipyard_list`, `cmd_shipyard_upgrade`, `repo_stardock_purchase_used_ship`; migration 177 | `test_planetary_server_contract.py` static source test added; executed as part of 69 passing tests; no DB runtime | `6bf7dcd2` | DB verify identity/equipment/condition preservation, active ownership switch, price and rollback |
| SHIP-006 | Last/active ship restrictions and replacement | IMPLEMENTED_UNVERIFIED | `server_ships.c`, `server_stardock.c`, `repo_stardock.c` | Sale JSON scenario asserts active replacement; error cases authored, not runtime-run | `bcaf3e93`, `b7ff3720` | Runtime ownership/active-state checks |
| SHIP-007 | Sale terminates insurance without refund | IMPLEMENTED_UNVERIFIED | `repo_stardock.c` | Sale JSON scenario asserts response; DB insurance status not yet asserted at runtime | `bcaf3e93` | Runtime verify |
| SHIP-008 | Atomic sale settlement | IMPLEMENTED_UNVERIFIED | `repo_stardock.c`, migration 170/122 | Transactional repository source; JSON scenario asserts persisted sale, inventory and credits; not runtime-run | `bcaf3e93` | Runtime rollback/concurrency verification |
| SHIP-009 | `ship.sell`, `shipyard.sell`, separate sale factor | IMPLEMENTED_UNVERIFIED | `server_ships.c`, `server_stardock.c`, `server_config.c` | ASan/UBSan build passed; `python3 -m pytest -q client/python_client/tests/test_v3_shipyard_sale_request.py` 2 passed; JSON parsed, no server runtime | `bcaf3e93` | End-to-end server verification |
| HARV-001 | Transfer ORE/ORG/EQU | IMPLEMENTED_UNVERIFIED | `cmd_planet_harvest`, `repo_planets_harvest` | Harvest JSON cases authored; `test_planetary_server_contract.py`: 32 passed; runtime not run | — | Execute protocol scenario and verify DB result |
| HARV-002 | Only accumulated stock | IMPLEMENTED_UNVERIFIED | repository reads `entity_stock`; conditional decrement | Static contract suite 32 passed; no DB runtime | — | Runtime conservation verification |
| HARV-003 | Destination is currently landed ship | IMPLEMENTED_UNVERIFIED | repository revalidates active ship, lastplanet, onplanet and sector under locks | Static contract suite 32 passed; no DB runtime | — | Runtime verify stale-ship rejection |
| HARV-004 | Capacity-aware partial harvest | IMPLEMENTED_UNVERIFIED | deterministic ORE/ORG/EQU allocation to aggregate free holds | JSON case asserts five-hold partial transfer; not runtime-run; static contract suite 32 passed | — | Verify cargo limits and stock remainder against real DB |
| HARV-005 | Zero turns | IMPLEMENTED_UNVERIFIED | response sets zero; repository does not mutate turns | Static contract suite 32 passed; JSON case authored | — | Runtime verify player turns unchanged |
| HARV-006 | Existing owner/corporation withdrawal permissions | IMPLEMENTED_UNVERIFIED | owner or corporation Leader/Officer; active landing revalidated | Static contract suite 32 passed | — | Runtime verify permission matrix |
| HARV-007 | Locking and atomic stock/cargo transfer | IMPLEMENTED_UNVERIFIED | locks planet/player/ship; conditional stock debit and cargo repository updates inside outer transaction | Sanitised build and static contract suite 32 passed; no DB runtime | — | Exercise rollback/concurrency |
| HARV-008 | Exchange-sold goods unavailable to harvest | IMPLEMENTED_UNVERIFIED | `cmd_planet_market_sell` atomically debits planet stock and credits `entity_stock(exchange,1)`; `repo_planets_harvest` reads/debits only remaining planetary stock | Focused planetary source-contract suite passed; no DB concurrency/runtime | `6bf7dcd2` | Verify sale-versus-harvest race and conservation in DB-backed test |
| VIEW-001 | Read-only overview | IMPLEMENTED_UNVERIFIED | `cmd_planet_view` in `server_planets.c` | Planet suite authored; Python static suite 28 passed (not runtime) | — | Verify read-only response via JSON runtime |
| VIEW-002 | Keep detailed `planet.info` | IMPLEMENTED_UNVERIFIED | `cmd_planet_info` | Same | — | Verify management fields preserved |
| VIEW-003 | Population/type/status | IMPLEMENTED_UNVERIFIED | `cmd_planet_view`, `cmd_planet_info`; Godot `PlanetWorkflow.gd` displays population type and morale status | `suite_planet_fighter_transfers.json`; client assertions added, Godot runner unavailable | `b7ff3720` | Run Godot client test and protocol scenario |
| VIEW-004 | Current production disclosure | IMPLEMENTED_UNVERIFIED | `db_cron_planet_get_production_overview_json` in `repo_cron.c`; `cmd_planet_view` | Sanitised build; static contract suite 32 passed; no runtime test | — | Runtime compare overview values with production tick |
| VIEW-005 | Morale representation | IMPLEMENTED_UNVERIFIED | response reports slave `LOW` | Static tests only | — | Runtime verify; no dynamic recovery |
| VIEW-006 | Treasury privacy in view/info | IMPLEMENTED_UNVERIFIED | owner/corporation authorization in handlers | Static suite 28 passed; no DB runtime | — | Verify unauthorized response redaction |
| PLAYER-001 | `player.list_online` | IMPLEMENTED_UNVERIFIED | `src/server_players.c` | `suite_player_status.json` covers authenticated listing and invalid page; JSON parsed, not runtime-run | `a07b3bf4` | Run protocol case |
| PLAYER-002 | 1-based page/limit | IMPLEMENTED_UNVERIFIED | `server_players.c`, schema | Source validates page>=1 and limit 1..1000; JSON acceptance authored; no runtime | `a07b3bf4` | Verify edge pages |
| PLAYER-003 | Public field allowlist | IMPLEMENTED_UNVERIFIED | `server_players.c`, `schemas.c` | Exact public response keys asserted; no runtime | `a07b3bf4` | Verify no private fields |
| PLAYER-004 | Deduplicate authenticated users | IMPLEMENTED_UNVERIFIED | `server_players.c` | Session snapshot deduplicates IDs before pagination; no multi-session runtime case | `a07b3bf4` | Verify multi-session case |
| PLAYER-005 | Stable order | IMPLEMENTED_UNVERIFIED | `server_players.c` | Source sorts unique IDs ascending before slicing; runtime pagination test outstanding | `a07b3bf4` | Verify pagination stability |
| PLAYER-006 | Include corporation/alignment | IMPLEMENTED_UNVERIFIED | `server_players.c`, response schema | Query and schema include corporation (null when absent) and alignment; JSON exact-key check authored | `a07b3bf4` | Verify representations |
| BANK-001 | Create/list/cancel | IMPLEMENTED_UNVERIFIED | `cmd_bank_orders_create/list/cancel`, registry and request/response schemas | `suite_economy_and_bank.json` cases added; runtime not run | — | Run protocol flow and verify persistence/ownership |
| BANK-002 | Daily/weekly/monthly | IMPLEMENTED_UNVERIFIED | `bank_order_first_run`, recurrence advancement in `db_bank_process_orders` | Daily/weekly/monthly JSON cases added; scheduler runtime not run | — | Verify calendar edges and overdue occurrences |
| BANK-003 | Persisted specified UTC time | IMPLEMENTED_UNVERIFIED | migrations 172/124; UTC fields and `next_run_at`; one-minute durable cron task | Build passed; no database migration/runtime | — | Verify scheduler time behavior and restart |
| BANK-004 | Any existing-player recipient | IMPLEMENTED_UNVERIFIED | create command locks and validates player row; processor rechecks and locks recipient | JSON uses an existing non-corporate player; not runtime-run | — | Verify recipient eligibility and deletion race |
| BANK-005 | Skip insufficient occurrence | IMPLEMENTED_UNVERIFIED | processor records `skipped_insufficient_funds`, advances next run and uses no debt | No DB test | — | Verify account and next occurrence state |
| BANK-006 | Monthly last-day adjustment | IMPLEMENTED_UNVERIFIED | `bank_schedule_monthly_at` is shared by first-run and recurrence calculation; retains configured `month_day` | `tests.v2/run_engine_regressions.sh` verifies February 28/29, Gregorian century boundaries, 30-day month, and invalid inputs; sanitizer build passed; scheduler/DB runtime unverified | `8fa4d08e` | Verify persisted monthly schedule and occurrence advancement through server/DB runtime |
| BANK-007 | Deleted recipient cancellation | IMPLEMENTED_UNVERIFIED | processor disables order and records cancellation outcome when recipient absent | No DB test | — | Verify deleted-recipient scenario |
| BANK-008 | Immediate cancellation | IMPLEMENTED_UNVERIFIED | cancel and processor serialize on order row in transaction | JSON cancellation and wrong-owner cases added; not runtime-run | — | Verify concurrent cancel/execute order |
| BANK-009 | Exactly once per occurrence | IMPLEMENTED_UNVERIFIED | occurrence table unique `(bank_orders_id,scheduled_for)`; transfer and advancement in same transaction; stale due-ID snapshots now skip if the locked row has advanced | `python3 -m pytest -q tests.v2/test_bank_standing_order_contract.py tests.v2/test_planetary_server_contract.py tests.v2/test_sector_entry_server_contract.py`: 44 passed; `make -C bin -j2`: passed with ASan/UBSan; database retry/restart test absent | `1f24988f` | Verify concurrent workers and restart |
| BANK-010 | Atomic debit/credit | IMPLEMENTED_UNVERIFIED | locked bank accounts, existing debit/credit ledger functions within order transaction | No DB test | — | Verify rollback and ledger pair |
| BANK-011 | No arbitrary order limit | IMPLEMENTED_UNVERIFIED | create has no active-order cap | Source inspection; no protocol execution | — | Keep limit absent; validate large-account list pagination |
| POP-001 | Exclusive population type | IMPLEMENTED_UNVERIFIED | migrations 171/123; planet handlers | `test_planetary_server_contract.py`: 28 passed | — | DB migration/runtime constraint verification |
| POP-002 | Existing population migration | IMPLEMENTED_UNVERIFIED | migrations 171/123 | No DB migration test in this session | — | Validate migration on both backends |
| POP-003 | Evil acquisition rules | IMPLEMENTED_UNVERIFIED | `server_ports.c::cmd_trade_buy` checks the authoritative E-VIL band at preflight and again inside settlement; `cmd_planet_deposit`, `repo_planets_deposit_slaves` | `test_planetary_server_contract.py`: evil-only/race guard; focused planetary/hazard tests: 37 passed; no DB runtime | `477bc1ed`, `434cd2c8` | Runtime verify port stock/credit/capacity and neutral/good refusal |
| POP-004 | Explicit evil conquest choice | IMPLEMENTED_UNVERIFIED | `cmd_combat_attack_planet`, `db_planets_capture` | Sanitised build passed; combat JSON request updated; static tests passed, no DB runtime | — | Test alignment-specific capture outcomes and rollback |
| POP-005 | Neutral inheritance/no acquisition | IMPLEMENTED_UNVERIFIED | capture retains existing SLAVE type; delivery requires existing alignment-band evil flag | Static contract suite 32 passed; no runtime | — | Test neutral capture and attempted purchase |
| POP-006 | Good auto-liberation | IMPLEMENTED_UNVERIFIED | capture transaction conversion | Build passed; no dedicated executed test | — | JSON scenario and runtime validation |
| POP-007 | Evil may retain slave population | IMPLEMENTED_UNVERIFIED | capture path leaves SLAVE unchanged | No runtime test | — | JSON scenario |
| POP-008 | Slave production at 50% | IMPLEMENTED_UNVERIFIED | shared `slave_population_effective_output` used by both production tick and read-only overview | `tests.v2/run_engine_regressions.sh` tests floor arithmetic for ORE/ORG/EQU, zero/odd/even values, stoppage and no second colonist modifier; DB tick unverified | `6c790d40` | Verify persisted production quantities and stoppage against DB |
| POP-009 | Permanently low morale, no second penalty | IMPLEMENTED_UNVERIFIED | `server_planets.c`, `repo_cron.c` | Source/static only | — | Verify production and API |
| POP-010 | Deterministic daily unrest | IMPLEMENTED_UNVERIFIED | `h_daily_slave_unrest`, `db_cron_apply_slave_unrest`, engine registry; migrations 176/128 | Source contract checks daily handler, idempotent event, proportional split and zero-config disable; included in focused pytest run | `6bf7dcd2`, `b75e3403` | Database verify daily execution, stoppage duration, exact loss and no duplicate event |
| POP-011 | No unapproved defaults | IMPLEMENTED_UNVERIFIED | `server_config.c`, `server_sysop.c`; zero sentinel is unset/disabled, negative values rejected | JSON config scenario authored; sanitised build passed; 41 focused source/manifest tests passed; not runtime-run | `5f7bb98c` | Verify SysOp set/get and startup parsing in protocol runtime |
| POP-012 | Restart-safe unrest idempotency | IMPLEMENTED_UNVERIFIED | persisted next/last event state, unique `(planet_id, scheduled_at)` event, row-locked daily worker; migrations 171/176 and MySQL equivalents | Static contract test executed; no DB restart/replay execution | `6bf7dcd2` | Verify replay/restart and competing workers in DB runtime |
| POP-013 | Whole-population emancipation | IMPLEMENTED_UNVERIFIED | `cmd_planet_emancipate`, `repo_planets_emancipate` | Sanitised build; static contract suite 32 passed; no DB runtime | — | Test corp roles and transaction |
| POP-014 | Atomic acquisition/conversion/conquest | IMPLEMENTED_UNVERIFIED | `server_ports.c::cmd_trade_buy` repeats the evil-alignment gate within purchase settlement; `repo_planets_deposit_slaves`, `repo_planets_emancipate`, and `db_planets_capture` transact deposit, emancipation and capture conversions | `test_planetary_server_contract.py` asserts alignment recheck, cargo-based deposit, exclusive population and transaction paths; no DB runtime | Existing source; current focused test suite passed (51 tests) | Exercise concurrent purchase/deposit/capture and rollback in DB-backed protocol tests |
| EXC-001 | Persistent exchange market | VERIFIED | migration 174 extends `entity_stock` with virtual `exchange/1`; Exchange actor/location supported in orders | Clean disposable QA database applied migrations through 177; end-to-end planet sale and port purchase read/write Exchange stock | `6bf7dcd2` | Verify restart recovery and supported MySQL migration path |
| EXC-002 | Sale into ring-fenced inventory and funded payout | VERIFIED | `cmd_planet_market_sell`; virtual system clearing account `system/70000001`; unique Exchange transaction record; configurable SysOp liquidity; account lookup now handles repository `ERR_DB_NOT_FOUND` | `suite_planet_taxable_activity_e2e.json`: 23/23 passed against real V3 server/PostgreSQL; asserted seller stock -10, Exchange stock +10, one transaction | `6bf7dcd2`, current working change | Extend verification to concurrent requests, retries and liquidity boundaries |
| EXC-003 | Exchange purchases to planet/port | IMPLEMENTED_UNVERIFIED | `h_daily_market_settlement` handles both `port` and `planet` destinations, with destination-capacity checks and `h_market_move_*_stock` settlement | Disposable PostgreSQL JSON scenario verified Exchange-to-port transfer, ledger rows and account balances (35/35 cases); planet buyer path has source support but no runtime case | `6bf7dcd2`, `81a366ce` | Add/run planet-buyer success, capacity/funds refusal, and restart scenarios |
| EXC-004 | Ring-fenced stock | IMPLEMENTED_UNVERIFIED | Planet sale transfers exact quantity to `entity_stock(exchange,1)`; matching conditionally debits that stock; harvesting reads only remaining planet inventory | PostgreSQL JSON verified planet -10, Exchange +10, Exchange -10, port +10 and no duplicate on a second tick; sale-versus-harvest race not run | `6bf7dcd2`, `81a366ce` | Verify sale/harvest/construction contention and concurrent buyers |
| EXC-005 | Virtual transport | IMPLEMENTED_UNVERIFIED | Exchange is a virtual inventory location; no vessel entities | Source inspection/build | `6bf7dcd2` | Preserve no-ship behavior in runtime acceptance |
| EXC-006 | Open order lifecycle | IMPLEMENTED_UNVERIFIED | `repo_market.c`, `server_cron.c`; matcher locks and rereads open orders, records partial fills, expires/cancels non-executable orders, and commits fills atomically | Real matcher partially filled port demand and persisted a trade; second tick did not repeat it. Cancellation/expiry JSON scenarios exist but current-HEAD execution is absent | `6bf7dcd2`, `81a366ce` | Runtime-test partial remainder, cancellation, expiry, retry, and restart together |
| EXC-007 | Port demand purchases | VERIFIED (port path) | port shortage BUY orders match the Exchange offer; clearing account settles port debit/system credit | `suite_planet_taxable_activity_e2e.json`, focused disposable PostgreSQL run 35/35; port stock +10, port balance -1,000, clearing balance +1,000 | current working change | Verify other port classes and insufficient-balance behavior |
| EXC-008 | Commodity conservation | VERIFIED (tested path) | immediate sale transfers planet stock to Exchange; fill conditionally debits Exchange and credits port stock | Focused PostgreSQL JSON run verified planet 100→90, Exchange 0→10→0, port 0→10; all quantities conserved | current working change | Verify planet buyers and simultaneous sales/harvest |
| EXC-009 | Financial integrity and game liquidity | VERIFIED (tested settlement) | clearing account and explicit `SYSTEM_LIQUIDITY`; matcher records actual debit/credit `bank_transactions` IDs in `commodity_trades` | Focused PostgreSQL JSON run verified port debit and clearing credit, equal amounts, and no second debit on retry; ASan/UBSan build passed | current working change | Concurrent settlement and liquidity boundary/restart tests remain |
| EXC-010 | Audit named market paths | VERIFIED (targeted paths) | `cmd_planet_market_sell`, `h_daily_market_settlement`, `h_market_move_port_stock`, market/bank repositories | Actual V3 server/PostgreSQL exercised planet sale→Exchange→port purchase, credit/ledger settlement and retry | current working change | Extend runtime evidence to other buyer types and simultaneous requests |
| EXC-011 | No loss/duplication/oversell | IMPLEMENTED_UNVERIFIED | `h_daily_market_settlement` locks inventory/account locations in deterministic order, then locks and rereads both order rows in ascending order ID before settlement; repository handles disappeared candidates | Source/ASan tests pass; concurrent PostgreSQL matcher test is absent. A 36/36 sequential settlement run is prior evidence, not concurrency evidence | `c12668c2`, `81a366ce` | Run two-worker same-order and limited-inventory contention against isolated PostgreSQL; assert no duplicate receipt/credits, oversell, deadlock or stuck order |
| EXC-012 | Authoritative contract | VERIFIED | `docs/V3_GALACTIC_EXCHANGE_CONTRACT.md` | Documentation commit `434d81a6` | `434d81a6` | Keep source status synchronized; contract documentation only |
| COMBAT-001 | Universal escape-pod respawn | IMPLEMENTED_UNVERIFIED | `repo_ships_handle_destruction`; command, NPC/player combat, Captain Z, engine self-destruct and legacy repository entry points converge via `handle_ship_destruction` | Added lethal-NPC and lethal-PvP assertions, including NPC-owner exclusion; focused planetary/hazard tests: 41 passed; ASan/UBSan build passed; runtime unverified | `63169074`, `aaf8a02e`, `6d2e36f0` | Exercise NPC- and player-caused lethal combat through JSON and verify pod/insurance state |
| COMBAT-002 | No starter-ship respawn | IMPLEMENTED_UNVERIFIED | shared repository creates canonical Escape Pod at sector 1 and assigns it active | Source contract and ASan/UBSan build; no server/DB runtime | `63169074` | JSON state assertion / runtime test |
| COMBAT-003 | No duplicate hull/pod/loss | IMPLEMENTED_UNVERIFIED | player/hull locking, conditional destroyed update, ownership removal, one pod and per-hull event key | Source contract and ASan/UBSan build; no DB concurrency/runtime test | `63169074` | Verify retries and simultaneous lethal paths |
| COMBAT-004 | Cargo/equipment loss | IMPLEMENTED_UNVERIFIED | shared destruction update zeroes cargo and ship equipment/defence quantities | Source inspection/build only | `63169074` | Protocol/database assertion |
| COMBAT-005 | Preserve bank/planet/corporate assets | IMPLEMENTED_UNVERIFIED | shared destruction transaction; Terra enforcement no longer calls asset-seizure SQL, checks settlement result, and retains the existing `sanctioned` event field | `python3 -m pytest -q tests.v2/test_planetary_server_contract.py tests.v2/test_sector_entry_server_contract.py`: 43 passed; `make -C bin -j2`: passed with ASan/UBSan; runtime state preservation unverified | `63169074`, `0e81209c`, `5195480b`, `6027d878`, `ef07c811` | Runtime state-preservation assertion |
| INS-001 | Hull-only coverage | IMPLEMENTED_UNVERIFIED | purchase schema, handler, and `repo_ships_insurance_buy_policy` restrict subjects to ships; `settle_destruction_insurance` caps covered payout at shiptype replacement value, excluding cargo/equipment | Added non-hull JSON refusal case and repository-boundary source contract assertion; focused tests/build recorded below; server/DB runtime not run | `dfdcd791`, current source change | Execute protocol rejection and hull-payout/loss-exclusion flow; preserve existing per-policy premium and coverage inputs |
| INS-002 | Qualifying destruction | IMPLEMENTED_UNVERIFIED | player-held active policy selected only when start/expiry epoch bounds include destruction time | Source inspection/build; no runtime | `15821cd8` | Verify time bounds and voluntary/nonqualifying loss cases |
| INS-003 | Credit bank account | IMPLEMENTED_UNVERIFIED | locked CRD account credited in destruction and portable explicit-claim transactions; funded payout uses `ADJUSTMENT`, guaranteed system funding uses `INSURANCE_GUARANTEE` | ASan/UBSan build and focused source tests pass; no DB runtime | `6bf7dcd2`, working change | Verify player balance, ledger label and rollback on both backends |
| INS-004 | Pod independent of policy | IMPLEMENTED_UNVERIFIED | unconditional pod creation is independent of insurance result | Source contract/build only | `63169074`, `15821cd8` | Verify insured and uninsured runtime cases |
| INS-005 | Exactly-once payout | IMPLEMENTED_UNVERIFIED | destruction locks player/hull, conditional destroyed update and per-hull event key; explicit claim repository locks claim/policy/account and returns idempotently when paid | `test_insurance_server_contract.py` plus related focus: 51 passed; ASan/UBSan build passed; DB not tested | `6bf7dcd2`, working change | Verify retry/concurrency, account/ledger rollback on PostgreSQL and MySQL |
| INS-006 | Sale terminates without refund | IMPLEMENTED_UNVERIFIED | `repo_stardock.c` sale transaction | Ship-sale JSON suite authored, not run | `bcaf3e93` | DB-backed verify |
| CAP-001 | Defeat defenses | IMPLEMENTED_UNVERIFIED | `cmd_combat_attack_planet`; uses `db_planets_get_citadel_defenses_ex` and aborts on repository failure rather than treating unavailable defenses as absent | `python3 -m pytest -q tests.v2/test_planetary_server_contract.py tests.v2/test_sector_entry_server_contract.py`: 44 passed; `make -C bin -j2`: passed with ASan/UBSan; no DB runtime | `5195480b` | Run combat JSON scenario against disposable server; verify citadel/fighter settlement |
| CAP-002 | Successful invasion required | IMPLEMENTED_UNVERIFIED | existing fighter comparison determines success | Combat JSON suite authored, not run | — | Validate combat outcome tests |
| CAP-003 | Ownership transfer | IMPLEMENTED_UNVERIFIED | `db_planets_capture` compare-and-updates expected owner, population type and observed planetary fighters; capture and attack events share the combat transaction | `python3 -m pytest -q tests.v2/test_planetary_server_contract.py tests.v2/test_sector_entry_server_contract.py`: 45 passed; `make -C bin -j2`: passed with ASan/UBSan; DB runtime/concurrency unverified | `0abc2c09`, `212e6f34` | Run capture/concurrent reinforcement protocol scenario and verify state/event rollback |
| CAP-004 | Resources/entire treasury remain with planet | IMPLEMENTED_UNVERIFIED | capture updates ownership/population only | Source inspection; no DB runtime | — | Verify associated stock/accounts stay linked |
| CAP-005 | Unrelated assets stay put | IMPLEMENTED_UNVERIFIED | capture does not mutate external accounts/assets | Source inspection only | — | Verify via JSON/DB journey |
| CAP-006 | Population transition atomic | IMPLEMENTED_UNVERIFIED | `db_planets_capture` conditional owner/population update in combat tx | Build passed; no runtime test | — | Add full capture population matrix |
| MOVE-001 | Atomic synchronous transwarp; mid-flight interruption excluded | PARTIAL | `cmd_move_transwarp` validates destination, installed drive and locked destination fighter, then consumes a turn, relocates and resolves hazards transactionally; fuel availability/consumption remains absent | `run_server_transwarp_validation_test.sh`, `test_transwarp_server_contract.py` passed; ASan/UBSan build passed; DB runtime not executed | Current worktree | Obtain an authoritative fuel amount/formula; then implement atomic fuel validation/debit and verify success/refusal/hazards |
| MOVE-05 | Transwarp fuel quantity | BLOCKED | Handler does not validate or debit fuel. The selected 1990–2000 manual is qualitative only; later references state 3 units per sector but do not pin that formula to the approved TWGS 3.13 baseline | No fuel behavior test can be authoritative until formula is approved | Documentation update only | Product owner: approve 3 fuel units per sector for the classic TWGS 3.13 baseline or specify the intended version/formula; then implement and test atomic settlement |
| MOVE-002 | Rejected validation does not consume fuel | IMPLEMENTED_UNVERIFIED | preflight validation precedes transaction and turn consumption in `cmd_move_transwarp` | Existing transwarp JSON cases; not executed | — | Execute refusal turn/fuel assertions |
| MOVE-003 | Immediate entry hazards | IMPLEMENTED_UNVERIFIED | `cmd_move_warp`, `cmd_move_transwarp`, shared `server_combat_apply_entry_hazards_report` | Lethal transwarp JSON case added; JSON validation/build pending, runtime not run | — | Execute normal/transwarp hazard scenarios |
| MOVE-004 | Hazard destruction uses common lifecycle | IMPLEMENTED_UNVERIFIED | shared hazard resolver delegates lethal outcomes to `handle_ship_destruction`; session context reset to sector 1 | New JSON case verifies attempted/current sectors and Escape Pod state; runtime not run | — | Execute lethal movement case against disposable server |
| PARITY-TW-HOMING-01 | Destination fighter required for transwarp homing | IMPLEMENTED_UNVERIFIED | `repo_universe_lock_sector_fighter` locks one positive fighter asset inside the transwarp transaction before turn/location mutation | `sh tests.v2/run_server_transwarp_validation_test.sh`; `python3 -m pytest -q tests.v2/test_transwarp_server_contract.py` (3 passed); ASan/UBSan build passed | Current worktree | Run database-backed missing/present/concurrent fighter validation |
| PTA-V3-01 | Local planetary bulk sale to buying port | IMPLEMENTED_UNVERIFIED | `cmd_planet_trade_port`; schema/registration; transaction locks port and rechecks planet authority/location, then moves stock, settles port/player credits, writes trade log and idempotency receipt | `pytest -q tests.v2/test_starport_sector_and_pta_contract.py tests.v2/test_transwarp_server_contract.py` (7 passed); JSON suite and migration manifest parse; ASan/UBSan server build passed; PTA JSON/runtime not executed | Working tree | Execute acceptance against disposable PostgreSQL; verify stock/credit conservation and idempotent retry |
| STARPORT-V3-01 | Construct/select class, upgrade and destroy StarPort | PARTIAL | `ports.is_active` tombstone state; PG partial unique sector index; MySQL nullable generated active-sector key; migrations 180→181; BigBang placement ignores inactive history; no player lifecycle handlers found | `test_starport_sector_and_pta_contract.py` (5 passed) checks both schema/index models, migration guards and BigBang selection; migration runtime not executed | `b23ad634`, `e30dc1c0` | Validate migration on clean PG/MySQL schemas, then implement destroy/tombstone and construction/upgrade workflows after exact costs, eligibility, build timing and destruction consequences are pinned |
| PARITY-PORT-ATTACK-01 | `combat.attack.port` | BLOCKED | Documented command/event only; no handler or port durability model | No executable contract | — | Define request, damage/durability, retaliation and destruction/repair outcomes; do not use `ports.size` as HP |
| PARITY-PHOTON-01 | Photon firing/effects | BLOCKED | Purchase/storage only; no firing handler | Manual establishes eligible ships, shield protection, temporary defense suppression; it does not set duration or V3 wire behavior. `23_Combat_and_Weapons.md` records the source. | — | Approve/derive the V3 fire command and effect lifetime/configuration before implementation; no arbitrary timer/default |
| PARITY-TOLL-01 | Toll fighter payment workflow | BLOCKED | Mode is stored, but entry currently applies fighter damage; no payment interaction | Manual establishes passers are asked for payment, amount depends on deployed fighters, and toll fighters fight if attacked; no numeric formula or V3 interaction contract. Recorded in `23_Combat_and_Weapons.md`. | — | Define formula and authenticated synchronous accept/refuse/payment flow; do not invent automatic charges |
| TRADE-001 | No haggling | IMPLEMENTED_UNVERIFIED | fixed-price ports, dynamic pricing | Port tests not run here | — | Confirm no haggling command path |
| TRADE-002 | Dynamic pricing retained | IMPLEMENTED_UNVERIFIED | port price repositories | No test executed | — | Verify supply/demand prices |
| TRADE-003 | Direction, stock, cargo, policing | IMPLEMENTED_UNVERIFIED | `server_ports.c`, repositories | No test executed | — | Runtime contract verification |
| SYS-001 | 60% ship sale setting | IMPLEMENTED_UNVERIFIED | `server_config.c`, `server_sysop.c` | Build passed, no runtime | `bcaf3e93` | Confirm default and bounds via protocol |
| SYS-002 | Fixed 50% slave productivity | IMPLEMENTED_UNVERIFIED | `src/slave_population.h`, shared by `repo_cron.c` production and overview | Existing engine C runner tests exact integer floor and stoppage; DB runtime unverified | `6c790d40` | Verify actual production tick against persisted stock |
| SYS-003 | Configurable deterministic unrest | IMPLEMENTED_UNVERIFIED | config keys/parser, SysOp validation, daily scheduler, PG migrations 173/176 and MySQL 125/128 | Focused source suite (69 passed, 200 subtests) and ASan/UBSan build passed; protocol/DB runtime not run | `6bf7dcd2` | Verify SysOp setting and daily scheduler against DB; zero disables effects |
| SYS-004 | Insurance policy payout / hull replacement valuation | IMPLEMENTED_UNVERIFIED | `insurance.policies.buy` persists per-policy `premium` and `payout`; `settle_destruction_insurance` pays configured amounts in policy-ID order, capped at aggregate shiptype hull replacement value; no global valuation setting is required | `python3 -m pytest -q tests.v2/test_insurance_server_contract.py tests.v2/test_planetary_server_contract.py`: 57 passed, including underinsured payout source assertion; DB settlement not executed | Current source | Runtime-verify underinsured payout, overlapping policy cap, guarantee funding and exactly-once credit |
| SYS-005 | Economy config | IMPLEMENTED_UNVERIFIED | `galactic_exchange_liquidity_credits`, default zero, nonnegative configured target; explicit `SYSTEM_LIQUIDITY` ledger | ASan/UBSan build and focused source/migration suite (69 passed, 200 subtests); protocol/DB runtime unverified | `6bf7dcd2` | Verify migration and SysOp setting plus market top-up |
| SYS-006 | Config documentation | IMPLEMENTED_UNVERIFIED | `docs/SYSOP_MENU.md`, protocol 29, protocol 24 | Documentation commits `434d81a6`, `5f7bb98c` | `5f7bb98c` | Keep parameter descriptions synchronized with any approved policy changes |
| SYS-007 | SysOp kick invalidates all target sessions | IMPLEMENTED_UNVERIFIED | `cmd_sysop_player_kick` calls `repo_session_revoke_player_sessions` before closing active sockets | Existing JSON case `test_sysop_players.json::case-0014` observed post-kick `player.my_info` succeed; source-contract test 1/1 and ASan/UBSan build passed; runtime rerun blocked | `2f03808b` | Run the JSON case against isolated QA and verify the same session token is rejected |
| NPC-FER-01 | Ferengi wanted bounty and captain pursuit | PARTIAL | `src/server_universe.c::ferengi_handle_hostile_attack`, Ferengi scheduler; `src/server_combat.c::handle_ship_attack` | Added persistence-failure/rollback source-contract assertion; `test_planetary_server_contract.py`: 35 passed; ASan/UBSan build passed; no DB runtime | `e4954ef7` | Ferengi flow exists, but bounty amount is hardcoded 5000 without an approved amount and runtime pursuit is unverified; define amount and verify scheduler/action persistence |
| NPC-ORI-01 | Orion blockader hold/deploy/attack behavior | PARTIAL | `src/server_universe.c::ori_move_all_ships` | Blockade assets/attacks now gated to assigned post; source-contract assertions; `test_planetary_server_contract.py`: 35 passed; ASan/UBSan build passed; no DB runtime | `ed08c3be` | Current target exclusion is Orion ownership; clarify whether hostile/allied player targeting requires additional policy, then verify home-sector hold and deployment at runtime |

## Execution evidence log

- INS-005 ledger audit alignment: migration 179 updates
  `insurance_ledger_invariant_snapshot` to accept both funded
  `ADJUSTMENT/CREDIT` and guaranteed `INSURANCE_GUARANTEE/CREDIT` rows. The
  existing SQL invariant fixture now includes a guaranteed paid claim.
  `python3 -m pytest -q tests.v2/test_insurance_server_contract.py
  tests.v2/test_planetary_server_contract.py` passed (55); runtime SQL
  execution is still pending. Migration/view commit: `ea095363`; fixture
  search-path correction: `37d12e4f`; verification checkpoint: `f947e67b`.
- 2026-11-11 QA attempt: sandbox `pg_isready` reported no response on the
  isolated loopback QA port 55438. Following explicit user authorization, the
  host-access bridge returned exit 0 but no identity output and no new report
  artifact; retained report timestamps were unchanged. This is not evidence of
  successful DB access or regression execution. No connection to port 5432 or
  a remote host was made. Continue local work; retry QA only when the allowed
  path yields observable identity and test artifacts.
- SHIP-002 local validation on the current tree: `python3 -m pytest -q
  tests.v2/test_ship_sale_equipment_catalog.py
  tests.v2/test_bigbang_sql_selection.py
  tests.v2/test_mysql_migration_portability.py
  tests.v2/test_insurance_server_contract.py` passed (29 tests, 217 subtests);
  `tests.v2/run_engine_regressions.sh` passed all three C engine checks. This
  does not apply migrations or exercise ship-sale JSON against a server.

- Portable insurance command batch: `make -C bin -j2` passed with ASan/UBSan
  after adding shared C repository transactions for policy purchase and
  explicit claim settlement; focused
  `python3 -m pytest -q tests.v2/test_insurance_server_contract.py tests.v2/test_planetary_server_contract.py tests.v2/test_bank_standing_order_contract.py`
  passed (51 tests); `git diff --check` passed. No PostgreSQL/MySQL runtime.
  Committed as `a6290ad6`.
- Shared slave production policy follow-up: both production-tick settlement
  and overview now call the same pure integer output function. The existing
  engine runner passes exact floor checks for ORE/ORG/EQU, zero/odd/even values,
  stoppage and unchanged non-slave output; focused pytest passes (51 tests),
  ASan/UBSan build is up to date, and `git diff --check` passes. DB production
  persistence remains unverified; committed as `6c790d40`.
- Exchange lock-order correction: market matches now lock both inventory
  locations in deterministic `(type,id)` order, consistent with the clearing
  account-before-planet order used by immediate planet sales. The ASan/UBSan
  build passed; `python3 -m pytest -q tests.v2/test_planetary_server_contract.py tests.v2/test_bank_standing_order_contract.py`
  passed (47 tests); `tests.v2/run_engine_regressions.sh` passed; and
  `git diff --check` passed. No database concurrency test was run.
- Exchange stale-order concurrency correction (current source change, not yet
  committed): each candidate settlement now locks inventory/clearing-account
  locations in deterministic order, then locks and rereads both order rows in
  ascending ID order inside the transaction. It revalidates open status,
  expiry, remaining quantity and crossing price before settlement; disappeared
  or already-settled candidates are skipped, and expired rows are persisted as
  expired under lock. This closes the stale-snapshot double-settlement path at
  source level while preserving planet-before-order lock order used by order
  cancellation. Verification: `python3 -m pytest -q
  tests.v2/test_planetary_server_contract.py` passed (49);
  `tests.v2/run_engine_regressions.sh` passed; `make -C bin -j2` passed with
  ASan/UBSan; `git diff --check` passed. PostgreSQL concurrency/runtime remains
  unverified. No new full regression run was possible: sandbox QA access and
  game-server runtime are unavailable, and a prior access escalation was
  denied. The historical 74-failure inventory is unchanged, not a new run.
- Insurance hull-only contract correction: `insurance.policies.buy` schema and
  handler reject cargo/planet subjects; `suite_economy_insurance.json` now
  covers the non-hull refusal. JSON validation passed, focused
  `python3 -m pytest -q tests.v2/test_insurance_server_contract.py tests.v2/test_planetary_server_contract.py`
  passed (51 tests), ASan/UBSan build passed, and `git diff --check` passed.
  No live protocol/DB runtime was run.
- Bank monthly scheduling now shares one UTC calendar-clamping helper between
  initial run calculation and recurrence advancement. Existing engine runner
  verifies leap/non-leap February, Gregorian century rules, 30-day months and
  invalid input. `tests.v2/run_engine_regressions.sh`, focused bank/planet
  pytest (47 passed), ASan/UBSan build and `git diff --check` passed. No DB
  schedule execution was run.
- Current Exchange/unrest/insurance/used-hull batch: `make -C bin -j2` passed
  with ASan/UBSan after the ship-sale ownership-count correction;
  `python3 -m pytest -q tests.v2/test_planetary_server_contract.py tests.v2/test_insurance_server_contract.py tests.v2/test_bigbang_sql_selection.py tests.v2/test_mysql_migration_portability.py tests.v2/test_mysql_migration_110_hazards.py`
  passed (69 tests, 200 subtests); PostgreSQL manifest JSON validation and
  `git diff --check` passed. No database or server runtime was run.
- Unrest allocation was extracted to the production-used pure helper
  `src/slave_population.h`; `tests.v2/run_engine_regressions.sh` now executes
  deterministic proportional/largest-remainder, tie-order, zero, full-loss and
  invalid-input cases. The runner passed; the sanitized server build passed;
  focused pytest remained 69 passed / 200 subtests. This test/helper change is
  uncommitted as of this checkpoint; DB scheduling and restart behavior remain
  unverified.
- `python3 -m pytest -q tests.v2/test_insurance_server_contract.py tests.v2/test_transwarp_server_contract.py tests.v2/test_bank_standing_order_contract.py tests.v2/test_planetary_server_contract.py tests.v2/test_sector_entry_server_contract.py` — 50 passed after stale-defense compare-and-update; `make -C bin -j2` — passed with ASan/UBSan; `git diff --check` — passed. Tests are source/contract tests; no server/DB runtime.
- `python3 -m pytest -q tests.v2/test_insurance_server_contract.py tests.v2/test_transwarp_server_contract.py tests.v2/test_bank_standing_order_contract.py tests.v2/test_planetary_server_contract.py tests.v2/test_sector_entry_server_contract.py` — 49 passed after transactional capture-event change; `make -C bin -j2` — passed with ASan/UBSan; `git diff --check` — passed. No DB/server runtime.
- `python3 -m pytest -q tests.v2/test_insurance_server_contract.py tests.v2/test_transwarp_server_contract.py tests.v2/test_bank_standing_order_contract.py tests.v2/test_planetary_server_contract.py tests.v2/test_sector_entry_server_contract.py` — 48 passed; source/contract tests only, not DB or server runtime.
- Sanitised server build: `make -C bin -j2` — passed after unrest configuration changes (existing unused-variable warnings remain).
- Focused tests: `python3 -m pytest -q tests.v2/test_planetary_server_contract.py tests.v2/test_sector_entry_server_contract.py tests.v2/test_bigbang_sql_selection.py tests.v2/test_pg_bigbang_sql_selection.py` — 41 passed. These are source-contract/manifest selection checks, not database runtime.
- Migration selection tests include new PG migration 173; MySQL migration 125 was not executed.
- JSON syntax: `python3 -m json.tool tests.v2/suite_planet_slave_unrest_config_v3.json` — passed; configuration acceptance cases were not run against a server.
- Bank order JSON acceptance cases were added to `suite_economy_and_bank.json`; JSON syntax passed, but cases have not been run against a server.
- JSON syntax: `python3 -m json.tool tests.v2/suite_combat_advanced.json` and `python3 -m json.tool tests.v2/suite_planet_fighter_transfers.json` — passed.
- Bank standing-order source and migrations are committed in `0c444420`; runtime and migration application remain unverified.
- `5f7bb98c` commits the policy config fields, SysOp validation, PG migration 173, MySQL migration 125, JSON scenario and docs. No PostgreSQL/MySQL migration execution or JSON protocol runtime was performed.
- `434cd2c8` closes the slave-purchase alignment race by revalidating the evil band after transaction start and before port stock/cargo mutation. `make -C bin -j2` passed with ASan/UBSan; focused `test_planetary_server_contract.py` and `test_sector_entry_server_contract.py` passed (37 tests); no DB protocol runtime.
- Ferengi retaliation persistence now fails combat closed: `handle_ship_attack` rolls back when bounty/hunt-state recording fails; `ferengi_handle_hostile_attack` propagates captain lookup, intelligence upsert and action-completion failures. Orion blockader source assertions cover home-sector targeting, defensive deployment and Orion exclusion. `make -C bin -j2` passed with ASan/UBSan; `python3 -m pytest -q tests.v2/test_planetary_server_contract.py` passed (35 tests). No DB-backed NPC runtime test was run. This work is committed as `e4954ef7` and `ed08c3be`.
- Orion blockaders displaced from home now return without deploying assets or attacking from the wrong sector; those actions run only in the configured home sector (or current sector when no home is configured). `make -C bin -j2` passed with ASan/UBSan; focused source-contract tests remained 35/35; `git diff --check` passed. Runtime behavior remains unverified.
- Lethal NPC attacks against player ships now call the same destruction/insurance/escape-pod transaction instead of persisting a zero-hull active ship; destroyed cargo is cleared before the NPC loot step. `python3 -m pytest -q tests.v2/test_planetary_server_contract.py tests.v2/test_sector_entry_server_contract.py` passed (40 tests); `make -C bin -j2` passed with ASan/UBSan; `git diff --check` passed. No JSON server/DB runtime was performed. Committed as `aaf8a02e`.
- Lethal PvP hits now use shared destruction settlement when the target owner is a real player; the authoritative NPC-owner flag is queried first so NPC hulls are not routed through player podding. If ownership classification fails, combat rolls back. Combined focused tests passed (41); sanitized build and `git diff --check` passed. No JSON/DB runtime; committed as `6d2e36f0`.
- Other recent gameplay commits: ship sale `bcaf3e93`; evil-only slave acquisition `477bc1ed`; online listing `a07b3bf4`; Godot ship-sale/population display `b7ff3720`; universal destruction `63169074`; funded hull insurance `15821cd8`.
- `git diff --check` — passed.
- A combined pytest command once referenced nonexistent `test_v3_population_contract.py`; that command failed collection and is not counted as a test pass. The valid focused command above was run separately and passed.
- Prior checkpoint note (superseded by the disposable PostgreSQL integration
  evidence immediately below): no PostgreSQL integration, server protocol run,
  full regression or Godot test had been performed at that time.

### Disposable PostgreSQL integration evidence — 2026-11-10

- QA target was verified as PostgreSQL at `127.0.0.1:55438`, role
  `twclone_qa_runner`, with `CREATEDB`; no connection was made to production
  port 5432 or a remote host. An isolated runner-generated database was
  migrated from clean state through migration 177 and then removed after the
  tests. The system PostgreSQL client wrapper was unusable in the sandbox;
  using the direct PostgreSQL 16 client with inherited `PGPASSWORD` unset and
  the configured `.pgpass` succeeded. No password or role was changed.
- Full real-server JSON execution completed all supported suites: 169 JSON
  files / 161 suites discovered; 157 suites executed; 2,225 cases executed,
  2,133 passed and 92 failed; 1 TLS suite blocked; 3 nested-DSL suites
  unsupported; 0 not-executed cases. The runner retained this as an incomplete
  `/tmp` report and did not replace the prior completed baseline. Server log
  records clean engine shutdown and no crash/transport cascade.
- Focused JSON execution: `suite_combat_sector_hazards.json` 45/45,
  `suite_economy_insurance.json` 12/12,
  `suite_planet_slave_unrest_config_v3.json` 11/11, and
  `suite_planet_taxable_activity_e2e.json` 23/23 passed (91/91 total). The
  planet sale workflow verified seller stock decreases by 10, Exchange stock
  increases by 10, and exactly one sale transaction is recorded. This
  verifies immediate Exchange accounting for this sale path, not subsequent
  Exchange-to-port settlement.
- Focused follow-up: `suite_economy_and_bank.json` 29/30 and
  `suite_planet_market_buy_order_lifecycle.json` 25/27 (54/57 total). Standing
  order creation/list/cancel protocol paths passed; the remaining bank failure
  is an undocumented exact error-code expectation (implementation returns
  403 for another player's order, while protocol only requires ownership).
  Market matching did not settle the seeded port-to-planet pair: both orders
  remained open, no trade was recorded, and inventories/credits were unchanged.
  This is a reproduced runtime failure; its underlying matcher cause remains
  unisolated and must not be reported as fixed.
- Supplementary PostgreSQL checks: `test_pg_bank_debit_concurrency.py` passed;
  `test_pg_ship_claim_concurrency.py` failed in its fixture because the claim
  function resolves `federation_protected_assets` from the shared public
  schema but the isolated test schema does not define it. Insurance settlement
  and insurance ledger SQL scripts passed. The ship-lifecycle restart snapshot
  script failed because included migrations commit and discard its
  `SET LOCAL search_path`, leaving its later snapshot view unresolved. These
  are fixture/script execution failures, not evidence of gameplay pass/fail.
- Fresh schema inspection confirmed `taverns.enabled BOOLEAN NOT NULL DEFAULT
  TRUE`, matching the canonical PostgreSQL migration. The previous `bigint`
  report was not reproduced on a clean migration.
- Source correction in this batch: `h_get_system_account_id_unlocked` now
  recognizes the repository's `ERR_DB_NOT_FOUND` result and creates the
  Exchange clearing account. `test_planetary_server_contract.py` passes 47
  tests; ASan/UBSan build passed; JSON syntax validation and
  `git diff --check` are pending final local verification. Market order
  matching remains the next integration defect to isolate.
- Exchange matcher correction: `h_daily_market_settlement` now treats the
  positive inserted `commodity_trades` ID as success (the repository returns
  a row ID, not a boolean status), and records the actual buyer-debit and
  seller-credit `bank_transactions` IDs instead of zero in the trade receipt.
  This resolves both the prior rollback at trade-record interpretation and
  the settlement FK failure. Verification: `make -C bin -j2` passed with
  ASan/UBSan; `python3 -m pytest -q
  tests.v2/test_planetary_server_contract.py` passed (48); focused actual V3
  server/PostgreSQL `suite_planet_taxable_activity_e2e.json` passed 36/36,
  after the fixture funded the intended full port purchase. It verifies
  planet→Exchange→port stock/credit conservation, ledger linkage and repeat-tick
  idempotency. Concurrent matcher execution and server-restart recovery remain
  unverified. The latest full execution ran 2,242 cases: 2,168 passed, 74
  failed, 1 blocked and 3 unsupported; zero crashes. The report is incomplete
  because of its blocked TLS suite, not a server failure. Relative to the prior
  92-failure report, 21 failures resolved and 3 appeared (net -18); exactly two
  resolved cases directly evidenced the Exchange clearing-account failure.
  See `tests.v2/REGRESSION_FAILURE_CLASSIFICATION.md` for all 74 case records.
- `suite_combat_port_rob.json::case-0009` was a fixture defect: the fixture
  selected an arbitrary port, potentially in FedSpace, where protocol §23
  mandates refusal of aggression. It now selects a stable port outside sectors
  1–10. This correction has not yet been runtime-rerun.
- Re-running `make regression-full` in the sandbox stopped before migration or
  gameplay execution: BigBang could not connect to its admin DB. A read-only
  connection made with approved QA credentials confirmed `postgres`,
  `127.0.0.1:55438`, user `twclone_qa_runner`. The runner could not be retried
  outside the sandbox because the permission review rejected that command;
  this is not evidence of a bad password or a gameplay failure. No production
  or remote database was contacted.
- The remaining failed Ferengi roster case (`case-0036`) is a fixture issue,
  not a spawn regression: the test added a definition after the active NPC
  population had already reached its configured 2%-of-sectors cap. The fixture
  now temporarily raises the cap by exactly one slot, restores the original
  value, and reconciles population afterward. Classification is updated to
  FIXTURE_DEFECT; this fixture correction is JSON-validated but runtime
  verification remains blocked by the sandbox's QA connection failure.
- The six formerly UNCLASSIFIED failures have been classified from retained
  evidence: two MFA requests are protocol-name mismatches with no authoritative
  V3 MFA contract; three legacy SysOp requests have no V3 contract or approved
  removal; one post-kick session remained usable, violating
  `29_SysOp_Commands.md`. The latter is corrected by revoking durable session
  tokens before socket closure. Two independent citadel failures previously
  labeled missing implementations are now UNCLASSIFIED: the handler reads
  `planettypes` values that canonical seed SQL provides, but the failed report
  omitted the selected type/value needed to explain the nonpositive duration.
  The shipyard-upgrade schema's missing mutually exclusive alternatives is
  corrected; planet-deposit and bank-refusal fixtures/assertions are also
  corrected based on their captured evidence. Focused source test passed (1),
  ASan/UBSan build passed, JSON parsing and classification sum passed. No QA
  runtime/full regression ran. Totals remain 74: 2 implementation defects,
  3 protocol mismatches, 6 specification blockers, 37 fixture defects, 16
  obsolete V2 tests, 8 infrastructure failures, 0 missing implementations and
  2 unclassified. No new full run was available.
- QA regression retry policy: the restricted sandbox cannot reach the QA
  PostgreSQL service for BigBang; one approved-identity read-only check outside
  it had previously verified `127.0.0.1:55438` / `twclone_qa_runner`, but the
  subsequent test-run escalation was denied. Do not retry outside the permitted
  environment or modify credentials/security configuration. Focused Ferengi,
  FedSpace and concurrent Exchange runtime scenarios remain pending.
- Insurance scope audit: `repo_ships_insurance_buy_policy` previously accepted
  `cargo` and `planet` subjects even though the approved INS-01 contract and
  public schema permit hull-only policies. The repository now rejects every
  non-`ship` subject before opening a transaction, and its ownership lock is
  ship-only. Added a repository-boundary assertion to
  `test_insurance_server_contract.py`. Verification actually run:
  `python3 -m pytest -q tests.v2/test_insurance_server_contract.py
  tests.v2/test_planetary_server_contract.py` (56 passed),
  `make -C bin -j2` (ASan/UBSan build passed),
  `tests.v2/run_engine_regressions.sh` (all existing engine regression unit
  checks passed), and `git diff --check` (passed). PostgreSQL/server runtime
  was not run. This is a source-verified correctness fix, not runtime
  verification.
- Approved insurance valuation update: per-policy payout may be below hull
  replacement value and governs settlement; a separate SysOp valuation setting
  is unnecessary. `SYS-004` is now IMPLEMENTED_UNVERIFIED. Added a focused
  assertion that destruction settlement uses the configured payout and clamps
  only when it exceeds remaining hull value. Updated the insurance protocol
  contract and decision register. `python3 -m pytest -q
  tests.v2/test_insurance_server_contract.py
  tests.v2/test_planetary_server_contract.py` passed (57 tests), including the
  underinsured payout assertion. Database-backed settlement remains
  unverified.
- Approved parity scope update: corporate equity/stock-market expansion is
  excluded from V3 parity/release acceptance; existing `NOT_IMPLEMENTED`
  responses may remain. It is not one of the 97 approved master requirement
  rows. This decision does not defer unrelated corporate requirements.
- MOVE-001 policy supersession: transwarp is explicitly atomic and synchronous;
  eligibility/fuel/destination checks precede movement, success completes at
  the destination, and entry hazards resolve after arrival. Mid-flight
  interruption is excluded from V3 acceptance unless a canonical trigger is
  later established. Source inspection found no existing pre-arrival event.
  Added a focused source-order assertion; database/runtime verification remains
  outstanding.
