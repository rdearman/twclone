# CODEX 4 issue #490 QA addendum — 2026-10-31

## Requirement

`V3-PLAN-01` — planet landing turn atomicity and refusal rollback.

This remains the next unblocked CODEX 4 supporting scenario. The ordinary-player
catalog also retains reproducible scenario definitions for ship ownership, trade
settlement, bank interest, visibility, hazards, planet lifecycle, and insurance;
those journeys remain deferred exactly as recorded in the release ledger and are
not treated as acceptance evidence.

## Progress

Added a deterministic persistence-failure landing fixture. It verifies that a
database error response is captured together with the request, event batch and
state/database before-and-after snapshots, while turn count and landing state
remain unchanged. Canonical requirements remain separate from AI policy, and the
adapter remains read-only: it does not provision, migrate, seed, reset, retry, or
write database rows.

## Verification

```text
python3 -m pytest -q \
  ai_player/tests/test_v3_planet_landing_harness.py \
  ai_player/tests/test_v3_codex4_ordinary_player_scenarios.py \
  ai_player/tests/test_v3_codex4_acceptance_catalog.py \
  ai_player/tests/test_deterministic_scenarios.py
27 passed
```

This is fixture/harness evidence only. No PostgreSQL or live-server gameplay run
was performed, and no acceptance state is claimed.

## Dependency note

The release ledger reports `QA-INT-01` blocked because BigBang rejects migration
125 as unclassified. CODEX 2/3 should classify and build the supported V3
migration set, then run the disposable identity/bootstrap gate. Once that gate
passes, CODEX 4 can execute the selected valid, remote, unauthorized, zero-turn,
defense, persistence-failure, and restart/readback landing journeys against the
disposable QA universe. CODEX 5's `V3-CAN-00` decision remains required for the
edition-specific turn/defense policy.

