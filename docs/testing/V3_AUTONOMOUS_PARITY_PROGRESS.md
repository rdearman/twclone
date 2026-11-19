# V3 autonomous parity progress

Updated: 19 November 2026

## Starting checkpoint

- Branch: `v3-development`
- Starting commit: `a0cf9339a5dcfcf8d0105362ac0ea522b852dfc9`
- Starting parent classification: 27 VERIFIED, 32 IMPLEMENTED_UNVERIFIED, 7
  PARTIAL, 1 BLOCKED.
- Existing worktree changes were preserved. The pre-existing changes to
  `Makefile`, `config.status`, the deletion of
  `tests.v2/regression_report.json.tmp`, and untracked
  `docs/V3_PLAYABILITY_AUDIT_20261113.md` were not staged or edited.
- No Godot process for `persisted_planet_restore_test.gd` or
  `planet_command_response_test.gd` was running when checked; no process was
  terminated. The previous test backtrace was a real GDScript parser failure:
  Godot could not infer the type of `sale_receipt_valid` in `Main.gd`. An
  explicit `bool` annotation fixed it.

## Batch 1 — complete

Requirements attempted and promoted:

- CFG-01 — ship-sale configuration.
- CFG-02 — fixed slave production multiplier.
- CFG-04 — hull-only insurance configuration and payout.
- EXC-04 — immediate Exchange delivery without physical virtual vessels.
- V3-PHOTON-DURATION-01 — configurable photon suppression duration.

Executable evidence:

- `suite_ship_sale_v3.json`: 19/19 passed.
- `slave_unrest_production_prepare.json`: 15/15 passed.
- `slave_unrest_production_verify.json`: 8/8 passed.
- `suite_economy_insurance.json`: 22/22 passed, including injected transaction
  rollback and duplicate-claim retry.
- `suite_planet_taxable_activity_e2e.json`: 44/44 passed, including a database
  snapshot proving no physical ship state changed during Exchange delivery.
- `suite_photon_duration_acceptance.json`: 15/15 passed.
- Latest focused disposable-PostgreSQL runner artifact:
  `/tmp/twclone_autonomous_batch1_final_norestart.json` reports 118 passed,
  zero failed. Later isolated reruns of insurance and Exchange passed 22/22 and
  44/44 respectively after adding rollback and physical-ship snapshots.
- Python: 810 passed.
- Godot 4.5.1: 106 passed, 0 failed, 6 skipped (live-server acceptance only).
- `persisted_planet_restore_test.gd`: 8 passed; `planet_command_response_test.gd`:
  117 checks passed.

The `--restart-between-suites` attempt is not counted as passing: after the
harness restarted the serving server, AddressSanitizer reported a startup
failure in `load_bootstrap_config` at `getenv`. Photon expiry restart evidence
from the earlier dedicated acceptance remains valid; this batch did not
complete restart-between-suites acceptance. A subsequent broad client command
was accidentally invoked without the required runner environment and piped to
`head`; its partial output is not a valid test result and is not counted.

Classification after batch 1: **32 VERIFIED, 27 IMPLEMENTED_UNVERIFIED, 7
PARTIAL, 1 BLOCKED**. Clause counts: 89 VERIFIED and 138 still
IMPLEMENTED_UNTESTED across 227 base rows.

No gameplay server defect was found in these five requirements. The Godot
parser failure was corrected. The JSON assertion runner gained a
`not_contains` operator for the fixed-setting negative assertion.

### Evidence policy item

POP-07's corporation-capture sentence is a documentary scope constraint; it
does not define a forbidden runtime action. No behavior-inventing negative
test was added. The matrix requires runtime evidence for VERIFIED, so POP-07
remains IMPLEMENTED_UNVERIFIED. Proposed minimal policy: allow explicitly
scope-exclusion clauses to cite approved contract evidence labeled `CONTRACT`,
separate from `RUNTIME`; do not silently promote until the policy is accepted.
SHIP-01 remains VERIFIED; its previous Godot menu and Python sale workflow
evidence covers the contract's player-facing access requirement.

## Batch 2 — completed 19 November 2026

Requirements selected from the smallest remaining gaps:

- CFG-03 — VERIFIED.
- SHIP-02 — VERIFIED.
- SHIP-04 — VERIFIED.
- POP-04 — VERIFIED.
- EXC-05 — sequential clauses VERIFIED, parent remains
  IMPLEMENTED_UNVERIFIED pending concurrent PostgreSQL settlement/order-lock
  acceptance required in `V3_GAMEPLAY_DECISIONS.md`.

Executed evidence:

- `suite_planet_market_buy_order_lifecycle.json`: 36/36.
- `suite_ship_sale_v3.json`: 27/27.
- `manual_acceptance/ship_sale_atomic_rollback.json`: 19/19.
- `suite_slave_unrest_config_defaults.json`: 12/12.
- `slave_unrest_production_prepare.json`: 19/19.
- `slave_unrest_production_verify.json`: 10/10.
- Total: 123/123 disposable-PostgreSQL cases across six suites.
- `test_ship_sale_equipment_catalog.py` plus JSON assertion runner tests:
  9 passed, 17 subtests passed.

The first EXC-05 run failed because other open ORE orders could consume the
seeded port stock before the intended player buyer. This was a fixture
isolation failure. The test now cancels unrelated ORE orders, disables
unrelated cron tasks in the disposable database, and advances the actual
settlement handler through the deterministic test scheduler. A second
acceptance run proved the exact two-unit fill, remainder cancellation, balance
and stock conservation, and no duplicate trade after scheduler retry. No
server behavior or expected result was relaxed.

Classification moved from 32 VERIFIED / 27 IMPLEMENTED_UNVERIFIED / 7
PARTIAL / 1 BLOCKED to **36 VERIFIED / 23 IMPLEMENTED_UNVERIFIED / 7 PARTIAL /
1 BLOCKED**. All 18 base clauses for the five selected requirements have
executed evidence; EXC-05 parent promotion awaits concurrency acceptance.

## Next batch candidates

Next candidate set, pending exact-clause and evidence review:

1. EXC-05 — concurrent order settlement and order-lock acceptance.
2. NPC-POP-01 — Ferengi population target, stable roster and reconciliation.
3. NPC-POP-02 — Orion population target, stable roster and personality/class
   preservation.
4. NPC-POP-03 — NPC-only Ferengi ship classes and deterministic allocation.
5. BANK-05 — standing-order execution/cancel races and uncapped order count.

These are candidate selections, not verified results. Existing evidence will
be reused only if it exercises each mandatory clause. No deployment,
production or release work is in scope.

## Batch 3 — NPC population and Ferengi fleet acceptance

The live repository already recorded completion of batches 1 and 2. The
current batch closed the five next candidates from that queue, including the
three population/fleet requirements. Starting from the recorded 36 VERIFIED /
23 IMPLEMENTED_UNVERIFIED / 7 PARTIAL / 1 BLOCKED, the ending classification is
**39 VERIFIED / 20 IMPLEMENTED_UNVERIFIED / 7 PARTIAL / 1 BLOCKED**.

### Requirements verified

- `NPC-POP-01`: target formula and minimum, 250 distinct stable Ferengi names,
  repeated reconciliation without duplicate identities, sentinel combat-state
  preservation, and state after serving-server restart.
- `NPC-POP-02`: Orion target and minimum, 100 distinct stable names in the full
  BigBang world after a server restart, five configured Orion ship classes,
  effective personality precedence, and captain/ship sector consistency.
- `NPC-POP-03`: five NPC-only Ferengi ship classes, exact deterministic
  100/75/40/25/10 allocation over 250 traders, and preserved Warship catalogue
  values.

### Executed evidence

- Ordinary isolated PostgreSQL fixture: `suite_npc_population_targets_v3.json`
  **32/32** and `suite_npc_population_restart_verify_v3.json` **4/4**. The
  serving game process restarted between suites.
- Full migrated BigBang isolated PostgreSQL world:
  `suite_npc_population_targets_v3.json` **32/32**,
  `suite_npc_population_restart_verify_v3.json` **4/4**, and
  `suite_npc_population_bigbang_roster_v3.json` **2/2**, with a server restart
  between suites.
- Python focused regressions: `test_v3_faction_rosters.py` and
  `test_mysql_migration_portability.py`: **18 passed, 206 subtests passed**.
- `make -j4`: passed.

### Defects fixed

- Orion initialization passed the owner player's ID to a repository routine
  that requires the Orion corporation ID, causing member-link failure. The
  initializer now resolves and passes the corporation ID separately.
- Orion ship deployment updated the captain's active ship but not the
  captain's sector. The active-sector consistency trigger exposed the mismatch;
  deployment now updates both fields in the same transaction.
- A diagnostic acceptance query treated SQL `NULL` text as integer `0` through
  the SysOp raw-SQL serializer. The acceptance now checks effective personality
  using the approved ship override, corporation default, ship-type default,
  balanced fallback precedence. No migration was added for a defect that the
  database evidence did not show.

Changes remain uncommitted at this progress update. The three newly verified
clauses are recorded in the matrix and decision register. The next candidates
are `EXC-05` concurrent settlement, then the smallest outstanding bank/economy
and NPC behavior clauses; the separate Orion safe-zone/protected-player,
removal-race, and concurrent-arrival clauses remain open.

## Batch 4 — EXC-05 concurrent market settlement

`EXC-05` is now VERIFIED. Its existing sequential acceptance covered distinct
placement/reservation/execution, open orders, partial fills, conservation and
retry idempotency. The new acceptance runs two real V3 server processes on
separate loopback ports against one fresh disposable PostgreSQL database. A
controlled trigger holds the first settlement transaction after candidate
selection; the second server enters the same scheduler path while that
transaction is open. Both logs selected the same due buy/sell orders. Final
authoritative SQL checks confirmed exactly one trade, one fill, the expected
order completion, port/planet quantities and credit balances.

Evidence: `suite_planet_market_buy_order_lifecycle.json` passed **53/53**
cases, including the two-server race and final SQL assertions. Focused Python
runner tests passed **31/31**; `py_compile`, `jq empty`, and `git diff --check`
passed. The ordinary sandbox attempt to start disposable loopback PostgreSQL
was denied before cluster identity verification; the same isolated acceptance
was then run with the authorized local-test access and passed. No persistent
server changes were required.

The batch adds opt-in `parallel_requests` support to the JSON acceptance runner
and optional second isolated server startup to `run_regression_instance.py`.
Default single-server test behavior is unchanged. Parent classification after
the batch is **40 VERIFIED, 19 IMPLEMENTED_UNVERIFIED, 7 PARTIAL, 1 BLOCKED**.

The next candidates are the smallest remaining runtime evidence gaps visible
in the matrix: `POP-06` slave-to-colonist conversion/access/accounting, then
`PLAYER-01` pagination and privacy, followed by other small behavior slices.
`BANK-05` order-execution/cancel concurrency and Orion safe-zone/protected,
removal-race, and concurrent-arrival clauses remain larger targeted items.
