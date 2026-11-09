# CODEX 4 issue #490 QA checkpoint — evidence contract — 2026-10-31

## Requirement ID

`V3-PLAN-01` remains the next executable CODEX 4 supporting slice in the
scenario catalog. This change strengthens the shared issue #490 preparation
for the requested landing, ship ownership, trade, bank interest, visibility,
hazard, planet, and insurance journeys; it does not claim any of them passed.

## Progress

- Added a fail-closed contract test covering all eight section-19 scenario IDs.
- Each scenario must declare response, event, authoritative state, and
  database before/after evidence, plus restart-readback and dependency notes.
- Ordinary-player command boundaries reject admin/sysop, migration, reset,
  fixture-seeding, and SQL execution commands.
- Insurance remains explicitly decision-gated and must not fabricate success
  while `V3-CAN-00` is unresolved.
- Canonical requirements remain in scenario data separate from deterministic
  AI policy; this test does not infer canon from AI behavior.

## Verification

```text
python3 -m pytest -q \
  ai_player/tests/test_v3_codex4_evidence_contract.py \
  ai_player/tests/test_v3_codex4_ordinary_player_scenarios.py \
  ai_player/tests/test_v3_codex4_scenario_contract.py
22 passed
```

This is QA contract evidence only. No PostgreSQL, live-server, migration,
production, or database-mutating operation was performed. Live execution
remains blocked by `QA-INT-01` in the release ledger: BigBang rejects migration
125 as unclassified. No acceptance state is claimed.

## Dependency note

CODEX 2/3 must repair the disposable migration/bootstrap gate and provide the
release schema identity before CODEX 4 can attach real responses, events,
read-only database snapshots, and restart readbacks. The adapter must not seed,
mutate, migrate, restart, or replay an ambiguous mutation.
