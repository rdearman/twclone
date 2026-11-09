# CODEX 4 issue #490 QA checkpoint — bank-interest scenario contract

## Requirement selected

`V3-ECO-02` is the next unblocked CODEX 4 supporting slice. The release
ledger marks it `PARTIAL`: the isolated SQL routine exists, but authoritative
runtime wiring and live scheduled/restart evidence are unresolved. No accepted
status is claimed.

## Progress

- Added structured canonical evidence boundaries to all eight requested
  ordinary-player journeys: response, events, state/database before/after, and
  restart readback.
- The bank-interest manifest now explicitly requires one daily application,
  same-day idempotency, and ledger reconciliation. It does not invent a rate,
  scheduler, or eligibility policy.
- Ship ownership, trade settlement, visibility, hazards, planet lifecycle and
  insurance retain their own invariant contracts. Insurance remains disposition
  gated and must never turn an unresolved policy into fabricated success.
- AI strategy remains separate in the catalog-level `ai_policy`; no scenario
  requirement is inferred from AI behavior.

The adapter boundary remains read-only. It captures sanitized requests,
responses, event batches, authoritative state and database snapshots, but does
not provision, migrate, seed, reset, retry, or restart services.

## Verification

```text
python3 -m pytest -q \
  ai_player/tests/test_v3_codex4_scenario_contract.py \
  ai_player/tests/test_v3_codex4_acceptance_catalog.py \
  ai_player/tests/test_v3_codex4_ordinary_player_scenarios.py \
  ai_player/tests/test_v3_bank_interest_scenario.py \
  ai_player/tests/test_v3_planet_landing_harness.py
```

No PostgreSQL or live-server gameplay run was performed. The release ledger's
`QA-INT-01` migration-classification failure still prevents disposable-universe
execution, and bank runtime wiring remains a CODEX 2/3 dependency.

## Dependency note

After `QA-INT-01` is repaired and one authoritative interest path is wired,
run eligible, same-day replay, below-minimum, ledger-failure, and restart cases
against the disposable QA universe. Attach the sanitized evidence bundle and
compare balance, transaction count, event count, and scheduler date before and
after each command. Do not execute insurance or harvest as canonical behavior
until `V3-CAN-00` supplies the edition/configuration disposition.
