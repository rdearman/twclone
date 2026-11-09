# V3 Gameplay Workplan

Persistent execution plan for the cumulative master assignment. Requirement
statuses and per-item evidence are tracked in
[`V3_GAMEPLAY_COMPLETION_TRACKER.md`](V3_GAMEPLAY_COMPLETION_TRACKER.md).
Approved policy remains in [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md).

## Active package — universal destruction settlement and insurance

- **Requirement IDs:** COMBAT-001–005, INS-001–006.
- **Dependencies:** canonical Escape Pod ship type and sector-1 spawn convention;
  existing insurance policies, claims, funded insurance account and bank ledger.
- **Source:** `src/db/repo/repo_ships.c`, `src/server_ships.c`,
  `src/server_combat.c`, `src/engine_consumer.c`, `src/server_loop.c`,
  `sql/pg/124_insurance_settlement.sql`.
- **Migrations:** none in the current destruction change.
- **Protocol:** `ship.self_destruct`, combat/hazard responses and automatic
  insurance in protocol 23/26.
- **Tests:** `tests.v2/suite_combat_and_crime.json`,
  `suite_fedspace_aggression_captainz.json`, `suite_combat_sector_hazards.json`;
  `test_sector_entry_server_contract.py`.
- **Completion criteria:** all destruction paths atomically lose hull/cargo/
  equipment, assign exactly one Escape Pod, preserve unrelated assets, persist
  an idempotent loss event, and settle eligible hull insurance to bank.
- **Current position:** common destruction transaction and call-site
  consolidation are implemented; ASan/UBSan build and 35 focused source-contract
  tests pass. Captain Z JSON state assertion now checks active owned Escape Pod.
  No JSON server/database runtime run. Automatic insurance remains missing;
  policy payout valuation, overlapping policies and insufficient-fund behavior
  need reconciliation before settlement is safe.
- **Next action:** complete insurance source/policy reconciliation and implement
  the unambiguous funded player-policy payout path; then verify destruction
  paths and proceed to movement/hazards.

## Subsequent packages

| Order | Package | Requirement IDs | Dependencies / exit criteria |
|---|---|---|---|
| 1 | Finish planetary overview and view data | VIEW-001–006 | Overview is allow-listed and now includes production values; validate runtime treasury privacy/read-only behavior. |
| 2 | Population purchase/transport and exclusivity | POP-001–003, POP-005, POP-014 | SLV cargo delivery path added; validate port purchase, alignment/stock/capacity and mixed-type rejection. |
| 3 | Production/unrest policy groundwork | POP-008–013, SYS-002–003 | Slave 50% path exists; implement daily persisted evaluation infrastructure, but keep effects dormant until all three numeric rules have approved values. |
| 4 | Galactic Exchange settlement | EXC-001–011, HARV-008, SYS-005 | Do not pay from an unfunded account. First identify approved, bounded clearing-fund/price/accounting mechanism; no commodity sink or money minting. |
| 5 | Used ship resale completion | SHIP-001–009 | Sale path exists; sale policy lacks subsequent used-hull purchase price/request details. Keep SHIP-005 partial until a purchase rule is approved. |
| 6 | Bank standing orders | BANK-001–011 | Source implementation and JSON coverage are present; verify runtime calendar, exactly-once, cancellation, failure and ledger behavior when an isolated DB runtime is available. |
| 7 | Universal destruction and insurance | COMBAT-001–005, INS-001–006 | Next source package; audit every destruction caller and unify pod/loss settlement, then hull-only bank payout. |
| 8 | Movement hazards/transwarp | MOVE-001–004 | Trace initiation/interruption and normal/transwarp entry processing; use common destruction lifecycle. |
| 9 | Online listing/trading/SysOp closeout | PLAYER-001–006, TRADE-001–003, SYS-001–006 | Online listing and sale config source need protocol verification; preserve dynamic prices and validate configuration bounds. |
| 10 | End-to-end requirements reconciliation | All IDs | Use available focused tests; full suite/runtime verification only in an already authorized disposable environment. Every ID needs implementation and executed evidence or a precise blocker. |

## Permanent execution constraints

- Never access production or remote DBs; this workplan does not authorize QA
  service or credential changes.
- Do not run historical regression recovery as a substitute for gameplay work.
- Do not modify the parked AI Player.
- Do not infer policy from implementation. Do not activate unrest without
  approved values or fund Exchange trades from an invented source.
- Do not mark tracker rows `VERIFIED` without appropriate executed tests.
- Update the tracker after each coherent package and record commits here.
