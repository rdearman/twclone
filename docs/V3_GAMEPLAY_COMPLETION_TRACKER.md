# V3 Gameplay Completion Tracker

This cumulative tracker covers every requirement ID in the master V3 gameplay
completion assignment. Status is conservative: source presence is not test
verification. `VERIFIED` is reserved for tests actually executed against the
relevant behavior. The policy source is [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md);
Exchange requirements additionally use [`V3_GALACTIC_EXCHANGE_CONTRACT.md`](V3_GALACTIC_EXCHANGE_CONTRACT.md).

Status values: `NOT_STARTED`, `PARTIAL`, `IMPLEMENTED_UNVERIFIED`, `VERIFIED`,
`BLOCKED`. Verification listed here is evidence already executed, not a plan.
No live DB or full JSON regression run has been performed in this work session.

| ID | Description | Status | Implementation location | Test location / executed result | Commit | Remaining action |
|---|---|---|---|---|---|---|
| SHIP-001 | Eligible sale locations | IMPLEMENTED_UNVERIFIED | `src/server_ships.c`, `src/server_stardock.c`, `src/db/repo/repo_stardock.c` | Sale JSON scenario authored and parsed; no runtime execution | `bcaf3e93` | Verify database classifications/police restrictions |
| SHIP-002 | Integer 60% hull+equipment valuation | IMPLEMENTED_UNVERIFIED | `repo_stardock.c`, `server_config.c` | ASan/UBSan build passed; focused JSON/client scenario not runtime-run | `bcaf3e93` | Runtime valuation verification; confirm prices exist for every equipped item type |
| SHIP-003 | Cargo forfeiture | IMPLEMENTED_UNVERIFIED | used-sale transaction in `repo_stardock.c` | JSON scenario asserts legacy and normalized cargo cleared; not runtime-run | `bcaf3e93` | Verify loss in DB-backed flow |
| SHIP-004 | Equipment remains with used hull | IMPLEMENTED_UNVERIFIED | `repo_stardock.c`, migration 170/122 | JSON scenario checks persistent ship identity and used inventory reference; not runtime-run | `bcaf3e93` | Verify identifiable used-hull contents |
| SHIP-005 | Used hull is purchasable | PARTIAL | used inventory schema exists; no complete used-hull acquisition contract/path | None executed | `bcaf3e93` | Implement purchase settlement after resale price/purchase request policy is specified |
| SHIP-006 | Last/active ship restrictions and replacement | IMPLEMENTED_UNVERIFIED | `server_ships.c`, `server_stardock.c`, `repo_stardock.c` | Sale JSON scenario asserts active replacement; error cases authored, not runtime-run | `bcaf3e93` | Runtime ownership/active-state checks |
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
| HARV-008 | Exchange-sold goods unavailable to harvest | PARTIAL | current market sell decrements planet stock; Exchange ring-fencing gap audited | No runtime tests | — | Ensure Exchange inventory receipt, then harvest only remaining planetary stock |
| VIEW-001 | Read-only overview | IMPLEMENTED_UNVERIFIED | `cmd_planet_view` in `server_planets.c` | Planet suite authored; Python static suite 28 passed (not runtime) | — | Verify read-only response via JSON runtime |
| VIEW-002 | Keep detailed `planet.info` | IMPLEMENTED_UNVERIFIED | `cmd_planet_info` | Same | — | Verify management fields preserved |
| VIEW-003 | Population/type/status | IMPLEMENTED_UNVERIFIED | `cmd_planet_view`, `cmd_planet_info` | `suite_planet_fighter_transfers.json`; not runtime-run | — | Runtime verify |
| VIEW-004 | Current production disclosure | IMPLEMENTED_UNVERIFIED | `db_cron_planet_get_production_overview_json` in `repo_cron.c`; `cmd_planet_view` | Sanitised build; static contract suite 32 passed; no runtime test | — | Runtime compare overview values with production tick |
| VIEW-005 | Morale representation | IMPLEMENTED_UNVERIFIED | response reports slave `LOW` | Static tests only | — | Runtime verify; no dynamic recovery |
| VIEW-006 | Treasury privacy in view/info | IMPLEMENTED_UNVERIFIED | owner/corporation authorization in handlers | Static suite 28 passed; no DB runtime | — | Verify unauthorized response redaction |
| PLAYER-001 | `player.list_online` | IMPLEMENTED_UNVERIFIED | `src/server_players.c` | `suite_player_status.json`; not runtime-run | — | Run protocol case |
| PLAYER-002 | 1-based page/limit | IMPLEMENTED_UNVERIFIED | `server_players.c`, schema | Static/protocol coverage exists; no runtime here | — | Verify edge pages |
| PLAYER-003 | Public field allowlist | IMPLEMENTED_UNVERIFIED | `server_players.c`, `schemas.c` | Protocol suite not runtime-run | — | Verify no private fields |
| PLAYER-004 | Deduplicate authenticated users | IMPLEMENTED_UNVERIFIED | `server_players.c` | Protocol suite not runtime-run | — | Verify multi-session case |
| PLAYER-005 | Stable order | IMPLEMENTED_UNVERIFIED | `server_players.c` | Protocol suite not runtime-run | — | Runtime verify pagination stability |
| PLAYER-006 | Include corporation/alignment | IMPLEMENTED_UNVERIFIED | `server_players.c` | Protocol suite not runtime-run | — | Verify representations |
| BANK-001 | Create/list/cancel | IMPLEMENTED_UNVERIFIED | `cmd_bank_orders_create/list/cancel`, registry and request/response schemas | `suite_economy_and_bank.json` cases added; runtime not run | — | Run protocol flow and verify persistence/ownership |
| BANK-002 | Daily/weekly/monthly | IMPLEMENTED_UNVERIFIED | `bank_order_first_run`, recurrence advancement in `db_bank_process_orders` | Daily/weekly/monthly JSON cases added; scheduler runtime not run | — | Verify calendar edges and overdue occurrences |
| BANK-003 | Persisted specified UTC time | IMPLEMENTED_UNVERIFIED | migrations 172/124; UTC fields and `next_run_at`; one-minute durable cron task | Build passed; no database migration/runtime | — | Verify scheduler time behavior and restart |
| BANK-004 | Any existing-player recipient | IMPLEMENTED_UNVERIFIED | create command locks and validates player row; processor rechecks and locks recipient | JSON uses an existing non-corporate player; not runtime-run | — | Verify recipient eligibility and deletion race |
| BANK-005 | Skip insufficient occurrence | IMPLEMENTED_UNVERIFIED | processor records `skipped_insufficient_funds`, advances next run and uses no debt | No DB test | — | Verify account and next occurrence state |
| BANK-006 | Monthly last-day adjustment | IMPLEMENTED_UNVERIFIED | monthly recurrence clamps execution date and retains configured `month_day` | No leap/non-leap DB test | — | Verify 28/29/30-day month boundaries |
| BANK-007 | Deleted recipient cancellation | IMPLEMENTED_UNVERIFIED | processor disables order and records cancellation outcome when recipient absent | No DB test | — | Verify deleted-recipient scenario |
| BANK-008 | Immediate cancellation | IMPLEMENTED_UNVERIFIED | cancel and processor serialize on order row in transaction | JSON cancellation and wrong-owner cases added; not runtime-run | — | Verify concurrent cancel/execute order |
| BANK-009 | Exactly once per occurrence | IMPLEMENTED_UNVERIFIED | occurrence table unique `(bank_orders_id,scheduled_for)`; transfer and advancement in same transaction | Build passed; no database retry/restart test | — | Verify concurrent workers and restart |
| BANK-010 | Atomic debit/credit | IMPLEMENTED_UNVERIFIED | locked bank accounts, existing debit/credit ledger functions within order transaction | No DB test | — | Verify rollback and ledger pair |
| BANK-011 | No arbitrary order limit | IMPLEMENTED_UNVERIFIED | create has no active-order cap | Source inspection; no protocol execution | — | Keep limit absent; validate large-account list pagination |
| POP-001 | Exclusive population type | IMPLEMENTED_UNVERIFIED | migrations 171/123; planet handlers | `test_planetary_server_contract.py`: 28 passed | — | DB migration/runtime constraint verification |
| POP-002 | Existing population migration | IMPLEMENTED_UNVERIFIED | migrations 171/123 | No DB migration test in this session | — | Validate migration on both backends |
| POP-003 | Evil acquisition rules | IMPLEMENTED_UNVERIFIED | `server_ports.c::cmd_trade_buy` explicitly requires the authoritative E-VIL alignment band for SLV; `cmd_planet_deposit`, `repo_planets_deposit_slaves` | `test_planetary_server_contract.py`: evil-only SLV purchase guard included; focused planetary/hazard tests: 37 passed; no DB runtime | `477bc1ed` | Runtime verify port stock/credit/capacity and neutral/good refusal |
| POP-004 | Explicit evil conquest choice | IMPLEMENTED_UNVERIFIED | `cmd_combat_attack_planet`, `db_planets_capture` | Sanitised build passed; combat JSON request updated; static tests passed, no DB runtime | — | Test alignment-specific capture outcomes and rollback |
| POP-005 | Neutral inheritance/no acquisition | IMPLEMENTED_UNVERIFIED | capture retains existing SLAVE type; delivery requires existing alignment-band evil flag | Static contract suite 32 passed; no runtime | — | Test neutral capture and attempted purchase |
| POP-006 | Good auto-liberation | IMPLEMENTED_UNVERIFIED | capture transaction conversion | Build passed; no dedicated executed test | — | JSON scenario and runtime validation |
| POP-007 | Evil may retain slave population | IMPLEMENTED_UNVERIFIED | capture path leaves SLAVE unchanged | No runtime test | — | JSON scenario |
| POP-008 | Slave production at 50% | IMPLEMENTED_UNVERIFIED | `repo_cron.c` integer production multiplier | Static contract tests not specific; no DB runtime | — | Test exact floor for all three outputs |
| POP-009 | Permanently low morale, no second penalty | IMPLEMENTED_UNVERIFIED | `server_planets.c`, `repo_cron.c` | Source/static only | — | Verify production and API |
| POP-010 | Deterministic daily unrest | BLOCKED | state columns/event table migrations only | No execution | — | Trigger, stoppage and loss values await owner policy |
| POP-011 | No unapproved defaults | PARTIAL | no activated unrest policy | No execution | — | Add validated configurable keys after policy values approved |
| POP-012 | Restart-safe unrest idempotency | PARTIAL | persisted state/event table only | No execution | — | Scheduler unavailable until policy values approved |
| POP-013 | Whole-population emancipation | IMPLEMENTED_UNVERIFIED | `cmd_planet_emancipate`, `repo_planets_emancipate` | Sanitised build; static contract suite 32 passed; no DB runtime | — | Test corp roles and transaction |
| POP-014 | Atomic acquisition/conversion/conquest | PARTIAL | emancipation/capture transactions; purchase missing | No runtime tests | — | Purchase transaction and concurrent cases |
| EXC-001 | Persistent exchange market | PARTIAL | bilateral `commodity_orders` / trades; no exchange inventory | Source audit in committed contract | — | Resolve/fund exchange inventory model |
| EXC-002 | Sale into ring-fenced inventory and funded payout | PARTIAL | planet sale decrements stock and credits seller without Exchange receipt | Source audit | — | Implement only with authoritative funded buyer/account |
| EXC-003 | Exchange purchases to planet/port | NOT_STARTED | No exchange inventory buyer path found | Source audit | — | Depends on exchange inventory and finance model |
| EXC-004 | Ring-fenced stock | NOT_STARTED | No Exchange-owned stock ledger found | Source audit | — | Add authoritative inventory lifecycle after account source decision |
| EXC-005 | Virtual transport | IMPLEMENTED_UNVERIFIED | no physical exchange vessels; transaction matching is direct | Source inspection only | — | Ensure no ship dependency in Exchange path |
| EXC-006 | Open order lifecycle | IMPLEMENTED_UNVERIFIED | `repo_market.c`, `server_cron.c` | Existing tests not executed here | — | Verify partial fills, cancel and expiry |
| EXC-007 | Port demand purchases | PARTIAL | `h_port_economy_tick` places port buy orders matched bilaterally | Source audit | — | Route port restock through Exchange inventory |
| EXC-008 | Commodity conservation | PARTIAL | bilateral matching transfers stock; immediate planetary sell loses Exchange ownership record | Source audit | — | Add conservation tests/ledger |
| EXC-009 | Financial conservation | PARTIAL | bilateral bank transfers; Exchange no funded clearing source | Source audit | — | Specify/implement market liquidity accounting |
| EXC-010 | Audit named market paths | IMPLEMENTED_UNVERIFIED | `server_planets.c`, `server_cron.c`, `repo_market.c` | Source audit documented; no runtime tests | — | Resolve Exchange gaps |
| EXC-011 | No loss/duplication/oversell | PARTIAL | conditional stock decrement and bilateral settlement | No DB runtime tests | — | Verify race/retry/restart cases |
| EXC-012 | Authoritative contract | VERIFIED | `docs/V3_GALACTIC_EXCHANGE_CONTRACT.md` | Documentation commit `434d81a6` | `434d81a6` | Keep source status synchronized; contract documentation only |
| COMBAT-001 | Universal escape-pod respawn | IMPLEMENTED_UNVERIFIED | `repo_ships_handle_destruction`; command, combat/Captain Z, engine self-destruct and legacy repository entry points converge via `handle_ship_destruction` | `test_sector_entry_server_contract.py`; 36 focused tests passed on latest run | `63169074` | Execute JSON destruction paths and verify runtime schema |
| COMBAT-002 | No starter-ship respawn | IMPLEMENTED_UNVERIFIED | shared repository creates canonical Escape Pod at sector 1 and assigns it active | Source contract and ASan/UBSan build; no server/DB runtime | `63169074` | JSON state assertion / runtime test |
| COMBAT-003 | No duplicate hull/pod/loss | IMPLEMENTED_UNVERIFIED | player/hull locking, conditional destroyed update, ownership removal, one pod and per-hull event key | Source contract and ASan/UBSan build; no DB concurrency/runtime test | `63169074` | Verify retries and simultaneous lethal paths |
| COMBAT-004 | Cargo/equipment loss | IMPLEMENTED_UNVERIFIED | shared destruction update zeroes cargo and ship equipment/defence quantities | Source inspection/build only | `63169074` | Protocol/database assertion |
| COMBAT-005 | Preserve bank/planet/corporate assets | IMPLEMENTED_UNVERIFIED | shared transaction mutates ship, active/sector/XP/blown-up state, ownership, pod status and event; no other asset tables | Source inspection/build only | `63169074` | Runtime state-preservation assertion |
| INS-001 | Hull-only coverage | IMPLEMENTED_UNVERIFIED | `settle_destruction_insurance` settles funded player-held policy, capped at shiptype basecost; excludes cargo/equipment | Build passed; insured Captain Z JSON scenario authored, not runtime-run | `15821cd8` | Verify valuation cap and exclusion of cargo/equipment |
| INS-002 | Qualifying destruction | IMPLEMENTED_UNVERIFIED | player-held active policy selected only when start/expiry epoch bounds include destruction time | Source inspection/build; no runtime | `15821cd8` | Verify time bounds and voluntary/nonqualifying loss cases |
| INS-003 | Credit bank account | IMPLEMENTED_UNVERIFIED | locked CRD account credited with ADJUSTMENT/CREDIT ledger entry in loss transaction | JSON case authored; not runtime-run | `15821cd8` | Verify ledger balance and transaction atomicity |
| INS-004 | Pod independent of policy | IMPLEMENTED_UNVERIFIED | unconditional pod creation is independent of insurance result | Source contract/build only | `63169074`, `15821cd8` | Verify insured and uninsured runtime cases |
| INS-005 | Exactly-once payout | IMPLEMENTED_UNVERIFIED | destruction locks player/hull, conditional destroyed update, per-hull event key and same-transaction insurance claims/ledger | Static checks/build; JSON scenario authored, not run | `15821cd8` | Verify retry/concurrent payout and uniqueness on both backends |
| INS-006 | Sale terminates without refund | IMPLEMENTED_UNVERIFIED | `repo_stardock.c` sale transaction | Ship-sale JSON suite authored, not run | — | DB-backed verify |
| CAP-001 | Defeat defenses | IMPLEMENTED_UNVERIFIED | `cmd_combat_attack_planet` | Combat JSON suite authored, not run | — | Verify citadel/fighter defenses |
| CAP-002 | Successful invasion required | IMPLEMENTED_UNVERIFIED | existing fighter comparison determines success | Combat JSON suite authored, not run | — | Validate combat outcome tests |
| CAP-003 | Ownership transfer | IMPLEMENTED_UNVERIFIED | `db_planets_capture` | Build passed; no DB runtime | — | Concurrency test and event assertion |
| CAP-004 | Resources/entire treasury remain with planet | IMPLEMENTED_UNVERIFIED | capture updates ownership/population only | Source inspection; no DB runtime | — | Verify associated stock/accounts stay linked |
| CAP-005 | Unrelated assets stay put | IMPLEMENTED_UNVERIFIED | capture does not mutate external accounts/assets | Source inspection only | — | Verify via JSON/DB journey |
| CAP-006 | Population transition atomic | IMPLEMENTED_UNVERIFIED | `db_planets_capture` conditional owner/population update in combat tx | Build passed; no runtime test | — | Add full capture population matrix |
| MOVE-001 | Interrupted transwarp consumes fuel, stays origin | PARTIAL | `cmd_move_transwarp` in `src/server_universe.c` | No interruption source/path found; no runtime test | — | Existing handler commits initiation, relocation and entry hazards atomically; identify an approved interrupting event/mechanism before adding interruption behavior |
| MOVE-002 | Rejected validation does not consume fuel | IMPLEMENTED_UNVERIFIED | preflight validation precedes transaction and turn consumption in `cmd_move_transwarp` | Existing transwarp JSON cases; not executed | — | Execute refusal turn/fuel assertions |
| MOVE-003 | Immediate entry hazards | IMPLEMENTED_UNVERIFIED | `cmd_move_warp`, `cmd_move_transwarp`, shared `server_combat_apply_entry_hazards_report` | Lethal transwarp JSON case added; JSON validation/build pending, runtime not run | — | Execute normal/transwarp hazard scenarios |
| MOVE-004 | Hazard destruction uses common lifecycle | IMPLEMENTED_UNVERIFIED | shared hazard resolver delegates lethal outcomes to `handle_ship_destruction`; session context reset to sector 1 | New JSON case verifies attempted/current sectors and Escape Pod state; runtime not run | — | Execute lethal movement case against disposable server |
| TRADE-001 | No haggling | IMPLEMENTED_UNVERIFIED | fixed-price ports, dynamic pricing | Port tests not run here | — | Confirm no haggling command path |
| TRADE-002 | Dynamic pricing retained | IMPLEMENTED_UNVERIFIED | port price repositories | No test executed | — | Verify supply/demand prices |
| TRADE-003 | Direction, stock, cargo, policing | IMPLEMENTED_UNVERIFIED | `server_ports.c`, repositories | No test executed | — | Runtime contract verification |
| SYS-001 | 60% ship sale setting | IMPLEMENTED_UNVERIFIED | `server_config.c`, `server_sysop.c` | Build passed, no runtime | — | Confirm default and bounds via protocol |
| SYS-002 | Fixed 50% slave productivity | IMPLEMENTED_UNVERIFIED | `repo_cron.c` | No DB test | — | Verify exact floor |
| SYS-003 | Configurable deterministic unrest | BLOCKED | population state/migration only | No test executed | — | Await approved values; never activate defaults |
| SYS-004 | Existing insurance valuation config | PARTIAL | insurance config/repositories need trace | No test executed | — | Confirm authoritative value or request policy |
| SYS-005 | Economy config | PARTIAL | current market config | Source audit only | — | Document Exchange settlement parameters after funding decision |
| SYS-006 | Config documentation | IMPLEMENTED_UNVERIFIED | `docs/SYSOP_MENU.md`, protocol 29 | Docs commit `434d81a6` | `434d81a6` | Ensure new config entries match implementation |

## Execution evidence log

- Sanitised server build: `make -C bin -j2` — passed after current capture changes.
- Focused Python source-contract tests: `python3 -m pytest -q tests.v2/test_planetary_server_contract.py` — 32 passed.
- Migration selection tests: `python3 -m pytest -q tests.v2/test_bigbang_sql_selection.py tests.v2/test_pg_bigbang_sql_selection.py tests.v2/test_v3_faction_rosters.py` — 8 passed; these validate manifest/selection only, not applying migration 172.
- Bank order JSON acceptance cases were added to `suite_economy_and_bank.json`; JSON syntax passed, but cases have not been run against a server.
- JSON syntax: `python3 -m json.tool tests.v2/suite_combat_advanced.json` and `python3 -m json.tool tests.v2/suite_planet_fighter_transfers.json` — passed.
- Banking source audit found the previous state had no `bank.orders.*` registrations and `db_bank_process_orders` was a no-op. Those gaps are now addressed in uncommitted source and migrations 172/124. Final build/test is pending; no runtime or migration test has been performed. Do not count this as VERIFIED.
- `git diff --check` — passed.
- A combined pytest command once referenced nonexistent `test_v3_population_contract.py`; that command failed collection and is not counted as a test pass. The valid focused command above was run separately and passed.
- No PostgreSQL integration, server protocol run, full regression or Godot test was performed.
