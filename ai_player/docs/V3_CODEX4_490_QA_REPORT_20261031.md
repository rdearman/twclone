# CODEX 4 issue #490 QA checkpoint — 2026-10-31

## Requirement selected

`V3-PLAN-01` remains the next unblocked CODEX 4 supporting scenario from
section 19. The release ledger still marks it `PARTIAL`, not accepted. The
scenario is ordinary-player planet landing with refusal/turn atomicity.

## QA work completed

- Added `valid-owned-readback`: `planet.land` followed by `player.my_info`.
- Added replayable `malformed-refusal`, `unauthorized-refusal`, and
  `zero-turn-refusal` cases so the selected catalog covers every section-19
  landing negative journey.
- Kept the adapter contract read-only for database snapshots and restart
  readback; no provisioning, migration, direct row writes, or automatic retry
  was added.
- Tightened the generic journey validator so an `expected: success` case fails
  if any command returns `refused` or `error`.
- Preserved the separation between `canonical_requirements` and `ai_policy`.

Every command in the selected journey records a sanitized request, response,
event batch, authoritative state before/after, and database snapshot
before/after. The fixture remains a scenario definition, not live evidence.

## Verification

```text
python3 -m pytest -q \
  ai_player/tests/test_v3_codex4_ordinary_player_scenarios.py \
  ai_player/tests/test_v3_codex4_acceptance_catalog.py \
  ai_player/tests/test_v3_planet_landing_harness.py \
  ai_player/tests/test_deterministic_scenarios.py
47 passed

The generic ordinary-player runner now enforces declared restart readback for
multi-command journeys, compares post-restart state and database snapshots to
the immediate result, and supports explicit response-type/final-state/database
change assertions. New tests cover the selected `valid-owned-readback` landing
journey, missing restart evidence, and restart-state mismatch. Every command
still records its own sanitized request, response, event batch, and
before/after state/database boundaries.
```

No PostgreSQL or live-server gameplay run was performed. The release ledger's
`QA-INT-01` migration-classification blocker still prevents the disposable
universe from reaching the selected landing scenario. No acceptance status is
claimed.

The same catalog also contains ordinary-player contracts for `V3-SHIP-01`
(ship ownership), `V3-ECO-01` (trade settlement), `V3-ECO-02` (bank interest),
`V3-VIS-01` (visibility), `V3-HAZ-01` (entry hazards), `V3-PLAN-02`
(planet production/citadel), and `V3-INS-01` (insurance). They remain
deferred or decision-gated exactly as recorded in the release ledger; their
presence is scenario preparation, not gameplay acceptance.

Dependency note for CODEX 2/3: once `QA-INT-01` is repaired, provide the
disposable server adapter and migration/schema identity to run `V3-PLAN-01`.
The adapter must return sanitized responses, event batches, authoritative
before/after state, database snapshots and post-restart readbacks. It must not
seed or mutate rows, restart services, or retry an ambiguous mutation.

## Next

After `QA-INT-01` is repaired by its owning agents, run the selected valid,
remote, unauthorized, zero-turn, defense, persistence-failure, and
landing/readback cases against the disposable QA universe and attach sanitized
evidence bundles. Do not execute deferred ship, trade, interest, visibility,
hazard, planet-production, or insurance cases until their ledger dependencies
and canonical policy gates are cleared.
