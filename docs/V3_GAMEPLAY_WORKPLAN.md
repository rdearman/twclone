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
  recorded in the tracker; sanitised build recorded below. The Exchange port
  purchase path has since been exercised against an isolated PostgreSQL DB and
  the actual V3 server; concurrent settlement and restart verification remain.
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
- The insurance purchase schema/server now reject cargo and planet coverage;
  the existing JSON insurance suite includes a refusal case. Focused tests and
  sanitised build pass; committed as `dfdcd791`.
- Bank standing-order first-run and recurrence calculations now share a UTC
  month-end helper. Existing C engine tests cover leap days and Gregorian
  century rules; sanitizer build and focused bank/planet contract tests pass.
- **Next action:** add or run a deterministic concurrent matcher scenario
  against the disposable database to verify stable inventory lock ordering,
  no oversell and no duplicate ledger/trade records. The sandbox could not
  reach QA PostgreSQL and a permitted escalation was denied, so do not repeat
  that connection attempt. Continue source-supported work while runtime is
  blocked. The 36/36 sequential Exchange run remains valid evidence;
  concurrency is not verified.
- **Known blockers:** Exchange, unrest, used-hull and insurance migrations and
  transaction behavior require disposable PostgreSQL/MySQL runtime verification.
  Unrest trigger/stoppage/loss values remain deliberately unset. Some installed
  ship equipment lacks a canonical valuation in `hardware_items` (SHIP-002).

## Remaining package order

| Order | Package | Requirement IDs | Exit criteria |
|---|---|---|---|
| 1 | Complete current Exchange/unrest/resale/insurance batch | EXC-001–011, HARV-008, POP-05/10/12, SHIP-005, INS-01/02 | Exchange sequential planet→port settlement and conservation pass against PostgreSQL; verify concurrent matcher/restart, then continue the remaining unrest/resale/insurance checks. |
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

## Current resume point — 2026-11-11

- The six cases previously marked `UNCLASSIFIED` have been categorized, but
  two separate citadel failures remain `UNCLASSIFIED`. Two MFA-name failures
  are protocol mismatches pending an
  approved MFA contract; three undocumented SysOp world commands are
  specification blockers; the post-kick session case is a confirmed defect.
  The two citadel failures report a nonpositive/unavailable planet-type
  duration, but source shows values in `planettypes` seed SQL and the failed
  run did not retain the selected type/value. Capture that row at the next
  allowed runtime instead of inventing seed data.
- Corrected the `shipyard.upgrade` discovery schema to express the documented
  new-hull versus used-hull request alternatives and updated its existing JSON
  schema assertion. Updated the planet-deposit fixture to capture its generated
  ID and establish owner/landing state; removed an unsupported ship ID field.
  The bank ownership-refusal assertion now checks error status without
  prescribing an undocumented code.
- Fixed `sysop.player.kick` to revoke durable target session rows before
  closing sockets, matching the documented invalidation requirement. The
  source/schema/fixture/evidence batch is committed as `2f03808b`.
- Local verification on the current worktree: `python3 -m pytest -q
  tests.v2/test_sysop_session_kick_contract.py` passed (1); `make -C bin -j2`
  passed with ASan/UBSan after the schema follow-up; four edited JSON files
  parsed; the category sum equals 74; and `git diff --check` passed. No
  PostgreSQL runtime or full regression ran against the current tree.
- Classification now totals 74: 2 implementation defects, 3 protocol
  mismatches, 6 specification blockers, 37 fixture defects, 16 obsolete V2
  tests, 8 infrastructure failures and 2 unclassified; the missing-
  implementation category is 0. The two citadel cases were removed from that
  category because their previously alleged missing `citadel_requirements`
  rows are not used by the handler.
- The current local checks were performed on the worktree based at
  `332f0695322e267f9cac765e67fd6408d546c395` and committed as `2f03808b`.
  The historical 74-failure report did not store its exact tested Git SHA and
  was later overwritten by a blocked attempt. Do not attribute the 2,242-case
  result to `769e16dc` or `332f0695` without an artifact proving the binary
  revision.
- Next: run local tests/build and static reconciliation, then inspect citadel
  requirement data for recoverable historical values; do not invent costs.
  Resume focused Ferengi/FedSpace and Exchange-concurrency PostgreSQL checks
  only if an allowed in-sandbox QA connection is available.
