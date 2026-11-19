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
