# CODEX 4 issue #490 — V3-PLAN-01 restart readback contract

Requirement: `V3-PLAN-01`, the next unblocked CODEX 4 scenario in the release
ledger.

The landing harness now supports an opt-in `restart_required` acceptance
requirement. A disposable live adapter may provide `read_restart_state()` and
`read_restart_db()` after an operator-controlled restart/reconnect. The
harness records both snapshots and fails qualification when either readback is
missing or differs from the immediate post-command snapshot. It does not
restart services, migrate schemas, seed rows, retry mutations, or write to a
database.

Canonical requirements remain in the scenario manifest; deterministic retry
and stop-on-ambiguity behavior remain in the separate AI-policy field. The
landing cases cover malformed, valid-owned, remote, unauthorized, zero-turn,
defense, and persistence-failure outcomes. Defense damage is retained as an
explicit event/database delta while turn and landing state remain protected.

Verification:

```text
python3 -m pytest -q \
  ai_player/tests/test_deterministic_scenarios.py \
  ai_player/tests/test_v3_planet_landing_harness.py \
  ai_player/tests/test_v3_codex4_acceptance_catalog.py
16 passed
```

This is harness/fixture evidence only. The release ledger still reports the
QA integration blocker: BigBang rejects migration
`125_trade_settlement_invariants.sql`, so no PostgreSQL or live-server
landing run was attempted. The scenario is not accepted.
