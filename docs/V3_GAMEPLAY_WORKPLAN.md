# V3 Gameplay Workplan

Persistent execution plan for the cumulative master assignment. Per-requirement
status and evidence are maintained in
[`V3_GAMEPLAY_COMPLETION_TRACKER.md`](V3_GAMEPLAY_COMPLETION_TRACKER.md), and
approved policy is in [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md).

## Active package — Exchange, unrest, used resale and insurance

- **Requirements:** EXC-001–011, HARV-008, POP-05/POP-10/POP-12, SHIP-005,
  INS-01/INS-02.
- **Current source:** Exchange stock receipt, configured liquidity and market
  matching are implemented in `src/server_planets.c` and
  `src/server_cron.c`; daily unrest scheduling and proportional integer loss
  are in `src/db/repo/repo_cron.c` and `src/server_engine.c`; existing used
  hulls can be listed and purchased through `shipyard.upgrade`; destruction
  insurance guarantees hull payouts to the bank.
- **Migrations:** PostgreSQL 174–177 and MySQL 126, 128–129 are included in
  the manifest/schema path. Runtime application has not been verified.
- **Approved policy:** Exchange liquidity may be explicitly injected by the
  game engine; slave losses use deterministic proportional largest-remainder
  distribution; used resale defaults to 80%; qualifying hull insurance is
  guaranteed. Unrest numerical policy remains unset and effects remain
  inactive until SysOp supplies all required positive values.
- **Verification performed:** Focused source-contract/migration test command
  recorded in the tracker; sanitised build recorded below. No database-backed
  runtime test was performed in this session.
- **Completed checkpoint:** commit `6bf7dcd2` contains the Exchange, unrest,
  insurance and used-hull source/migration/documentation batch, including the
  correction that last-ship eligibility counts every role-1 owned hull rather
  than only the primary hull. A subsequent focused package extracted the
  proportional loss allocator and shared output policy in
  `src/slave_population.h`, with C assertions in the existing engine test
  runner. Build and focused tests pass; the shared-output follow-up is
  committed as `6c790d40`.
  Portable insurance command operations are committed as `a6290ad6`.
  Tracker cleanup is committed as `06c8284f`. Exchange market matching was
  then changed to acquire bilateral inventory locks in deterministic order;
  sanitizer build and 47 focused tests pass, with runtime race verification
  outstanding. Source and test change committed as `c12668c2`; decision-register
  update committed as `ea74d79d`.
- **Next action:** continue with remaining actionable tracker requirements,
  beginning with the next actionable population/capture workflow after
  verifying the existing source contracts; preserve database runtime as
  unverified until executed.
- **Known blockers:** Exchange, unrest, used-hull and insurance migrations and
  transaction behavior require disposable PostgreSQL/MySQL runtime verification.
  Unrest trigger/stoppage/loss values remain deliberately unset. Some installed
  ship equipment lacks a canonical valuation in `hardware_items` (SHIP-002).

## Remaining package order

| Order | Package | Requirement IDs | Exit criteria |
|---|---|---|---|
| 1 | Complete current Exchange/unrest/resale/insurance batch | EXC-001–011, HARV-008, POP-05/10/12, SHIP-005, INS-01/02 | Source/migration tests and sanitised build pass; DB limitations remain accurately marked unverified. |
| 2 | Remaining planet population and capture workflows | POP-001–014, CAP-001–006, SYS-002/003 | Verify migrations, exclusive population, capture conversion, emancipation, exact 50% production, scheduler idempotency. Do not invent unset unrest values. |
| 3 | Ship sale/destruction completeness | SHIP-001–009, COMBAT-001–005, INS-001–006 | Resolve missing equipment valuation source; test sale, used-hull purchase, pod, destruction asset retention and guaranteed insurance. |
| 4 | Galactic Exchange end-to-end | EXC-001–011, HARV-008, SYS-005 | Verify immediate sale, port/planet purchase, conservation, liquidity and idempotency using an available isolated DB. |
| 5 | Banking standing orders | BANK-001–011 | Verify schedule boundaries, skips, cancellations, deleted recipient, ledger and exactly-once restart behavior. |
| 6 | Movement, hazards, listing and trade | MOVE-001–004, PLAYER-001–006, TRADE-001–003 | Verify approved wire behavior and immediate hazard/destruction settlement. |
| 7 | Remaining tracker reconciliation | All outstanding IDs | Every ID has implementation/test evidence or an exact policy/external blocker; do not infer completion from handler presence. |

## Execution constraints

- Do not access production or remote databases. Do not repair QA infrastructure.
- Do not run historical regression recovery or modify the parked AI Player.
- Preserve user changes, existing architecture and documented policy.
- Do not mark requirements `VERIFIED` without executing appropriate tests.
- Update tracker and decision register after each bounded package; record
  actual verification and commit identifiers.
