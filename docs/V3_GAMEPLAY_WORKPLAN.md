# V3 Gameplay Workplan

Persistent execution plan for the cumulative master assignment. Requirement
statuses and per-item evidence are tracked in
[`V3_GAMEPLAY_COMPLETION_TRACKER.md`](V3_GAMEPLAY_COMPLETION_TRACKER.md).
Approved policy remains in [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md).

## Active package — finish independent NPC/planet implementation; preserve policy blockers

- **Requirement IDs:** Continue EXC-001–011 only if an existing funded Exchange
  counterparty/account is found; otherwise advance independent POP, NPC, ship,
  combat and banking requirements without inventing missing policy.
- **Dependencies:** Exchange settlement requires an approved, bounded funding
  source and price/counterparty rule. Unrest effect scheduling requires the
  already-approved configurable parameters, but population-loss allocation
  among unassigned and working population is not specified.
- **Source:** `src/server_planets.c`, `src/server_cron.c`,
  `src/db/repo/repo_market.c`, `src/db/repo/repo_market_dynamic.c`,
  `src/db/repo/repo_cron.c`, `src/server_engine.c`, and existing population
  state migrations 171/123.
- **Migrations:** do not add Exchange inventory/account tables until its
  accounting source is decided. Unrest may need only config/migration additions;
  keep destructive effects disabled while values or allocation policy are
  missing.
- **Protocol:** `22_Trade_and_Port_Commands.md`,
  `24_Planets_Outposts_Stations.md`, `29_SysOp_Commands.md`.
- **Tests:** existing market/planet JSON suites and focused contract tests;
  runtime DB verification is unavailable for this execution.
- **Current position:** persisted unrest configuration is implemented (`5f7bb98c`)
  but there is no scheduler consumer; effects remain inactive because timing,
  loss allocation and numeric values are not approved. Ferengi retaliation
  source previously ignored bounty/intel persistence errors; combat now rolls
  back on those failures and a focused source-contract test passes. Its bounty
  amount remains hardcoded without a policy decision. Orion blockaders now
  return to the assigned post without blockade actions in an off-post sector;
  target eligibility beyond excluding Orion-owned ships remains unresolved.
  A further combat-path trace found that NPC attacks could leave a player ship
  at zero hull without destruction settlement; lethal NPC attacks now use the
  shared pod/insurance/destruction path. Focused tests and sanitized build pass,
  but no database runtime has verified the nested settlement. The same bypass
  in PvP damage is now repaired; lethal PvP checks authoritative NPC ownership
  before podding and fails closed if ownership classification cannot be read.
  Source audit confirms immediate planet sales debit
  planet stock and credit the seller, with neither Exchange inventory receipt
  nor an identified funded Exchange buyer. The current book is bilateral.
  Creating a credit source or allowing goods to disappear would violate the
  approved contract. Separately, POP-003 now has an explicit evil-only SLV
  purchase guard (`477bc1ed`); build and 37 focused source-contract tests pass.
  Ship-sale workflow has been committed (`bcaf3e93`) but used-hull acquisition
  remains incomplete pending a resale price contract. Universal destruction
  and funded-policy hull insurance are committed (`63169074`, `15821cd8`),
  build/source-tested but not database-runtime verified.
- **Current batch:** remove Terra's legacy asset-seizure side effect, make
  enforcement fail closed when common destruction settlement fails, and verify
  that escape-pod respawn does not erase unrelated assets.
- **Completed follow-up:** planet combat now fails closed when citadel defense
  state cannot be read; it no longer treats a repository error as no defenses.
  Focused source-contract tests and the ASan/UBSan build pass.
- **Completed follow-up:** a standing-order worker whose due snapshot became
  stale while waiting for the order lock now skips that already-advanced
  occurrence rather than failing the scheduler batch. Focused contract tests
  and the ASan/UBSan build pass; database concurrency remains unverified.
- **Next action:** inspect insurance payout funding/valuation and planet capture
  asset retention for source-demonstrable policy conflicts, then continue with
  the next independently actionable gap. Preserve Exchange, unrest effects,
  used resale pricing, bounty amount and alliance-target rules as unresolved
  where their policy is incomplete. Do not infer liquidity, numeric defaults
  or eligibility policy.

## Subsequent packages

| Order | Package | Requirement IDs | Dependencies / exit criteria |
|---|---|---|---|
| 1 | Galactic Exchange settlement | EXC-001–011, HARV-008, SYS-005 | Audit confirms no Exchange-owned inventory or funded buyer source. Implement only after a valid existing clearing/funding mechanism is located; no minting or commodity sink. |
| 2 | Planet population and production completion | POP-001–014, SYS-002–003 | Evil-only slave purchase restriction is now source implemented; verify port trade/deposit and mixed-population transactions. Implement dormant daily unrest/config without selecting unapproved destructive values or allocation policy. |
| 3 | Used ship resale completion | SHIP-001–009 | Sale commands/schema/migrations committed at `bcaf3e93`; SHIP-005 remains partial until used-hull purchase contract and resale price are approved. |
| 4 | Insurance/destruction verification | COMBAT-001–005, INS-001–006 | Common destruction and funded hull-policy payout committed at `63169074`, `15821cd8`; runtime and concurrency verification remain outstanding; insolvency handling needs policy. |
| 5 | Movement hazards/transwarp | MOVE-001–004 | Lethal entry uses common pod lifecycle; MOVE-001 awaits defined interruption event/path. Runtime hazard and validation/no-charge tests remain outstanding. |
| 6 | Banking standing orders | BANK-001–011 | Durable scheduler/source and JSON scenarios committed; verify calendar, exactly-once, cancellation, deletion and ledger behavior with available isolated runtime. |
| 7 | Online listing and trading | PLAYER-001–006, TRADE-001–003 | Source for online pagination/public fields exists; verify client protocol and dynamic fixed-price trade behavior. |
| 8 | End-to-end requirements reconciliation | All IDs | Use available focused tests; do not count unavailable runtime as pass. Every ID needs implementation/test evidence or an exact policy blocker. |

## Permanent execution constraints

- Never access production or remote DBs; this workplan does not authorize QA
  service or credential changes.
- Do not run historical regression recovery as a substitute for gameplay work.
- Do not modify the parked AI Player.
- Do not infer policy from implementation. Do not activate unrest without
  approved values or fund Exchange trades from an invented source.
- Do not mark tracker rows `VERIFIED` without appropriate executed tests.
- Update the tracker after each coherent package and record commits here.
