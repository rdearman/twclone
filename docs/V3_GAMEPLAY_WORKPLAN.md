# V3 Gameplay Workplan

Persistent execution plan for the cumulative master assignment. Requirement
statuses and per-item evidence are tracked in
[`V3_GAMEPLAY_COMPLETION_TRACKER.md`](V3_GAMEPLAY_COMPLETION_TRACKER.md).
Approved policy remains in [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md).

## Active package — bank standing-order workflow audit and completion

- **Requirement IDs:** BANK-001–011.
- **Dependencies:** existing bank-order schema, transaction/ledger path and
  durable cron scheduling.
- **Source:** `src/server_bank.c`, `src/db/repo/repo_bank.c`, `src/server_cron.c`,
  bank migrations and protocol schemas.
- **Migrations:** only if current durable schema cannot represent normalized
  frequency, UTC execution time, original monthly day and per-occurrence state.
- **Protocol:** `bank.orders.create`, `bank.orders.list`,
  `bank.orders.cancel`.
- **Tests:** existing banking JSON/contract suites; no QA/database work is
  authorized as part of this package.
- **Completion criteria:** durable daily/weekly/monthly recurrence, month-end
  adjustment, idempotent occurrence settlement, atomic ledger-backed transfer,
  skip-on-insufficient-funds, invalid-recipient cancellation and serialized
  cancellation/execution.
- **Current position:** harvest, production overview, emancipation, slave cargo
  delivery and capture population transitions are implemented in source; build
  and 32 focused source-contract tests passed. Database-backed behavior remains
  unverified. No bank audit has yet been completed in this checkpoint.
- **Next action:** inspect handler, repository, migrations and scheduler to map
  BANK-001–011 to real code and identify the first incomplete behavior.

## Subsequent packages

| Order | Package | Requirement IDs | Dependencies / exit criteria |
|---|---|---|---|
| 1 | Finish planetary overview and view data | VIEW-001–006 | Overview is allow-listed and now includes production values; validate runtime treasury privacy/read-only behavior. |
| 2 | Population purchase/transport and exclusivity | POP-001–003, POP-005, POP-014 | SLV cargo delivery path added; validate port purchase, alignment/stock/capacity and mixed-type rejection. |
| 3 | Production/unrest policy groundwork | POP-008–013, SYS-002–003 | Slave 50% path exists; implement daily persisted evaluation infrastructure, but keep effects dormant until all three numeric rules have approved values. |
| 4 | Galactic Exchange settlement | EXC-001–011, HARV-008, SYS-005 | Do not pay from an unfunded account. First identify approved, bounded clearing-fund/price/accounting mechanism; no commodity sink or money minting. |
| 5 | Used ship resale completion | SHIP-001–009 | Sale path exists; sale policy lacks subsequent used-hull purchase price/request details. Keep SHIP-005 partial until a purchase rule is approved. |
| 6 | Bank standing orders | BANK-001–011 | Current active package; audit and complete UTC scheduling, month-end adjustment, cancellation serialization, recipient deletion and exactly-once settlement. |
| 7 | Universal destruction and insurance | COMBAT-001–005, INS-001–006 | Audit every destruction caller; use one idempotent pod/loss settlement and bank-only hull payout without inventing valuation. |
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
