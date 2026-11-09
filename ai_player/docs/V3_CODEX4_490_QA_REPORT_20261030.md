# CODEX 4 issue #490 QA checkpoint — 2026-10-30

## Selected requirement

`V3-PLAN-01` is the next unblocked CODEX 4 supporting scenario from section 19:
ordinary-player planet landing and refusal atomicity. The reusable fixture is
`scenarios/v3_planet_landing_atomicity.json`; the live adapter boundary is
`qa_harness.py`.

The harness is deliberately not a server or database client. An operator must
inject an adapter connected only to an explicitly disposable QA universe. It
does not provision players, seed rows, migrate schemas, retry ambiguous
mutations, or write database rows.

## Reproducible evidence contract

Each command records a redacted request, response, and event batch. Each
journey records authoritative player/ship state and read-only database
snapshots before and after. Ordinary refusals require unchanged landing state,
turns, and database evidence. A defense refusal is explicitly allowed to change
ship combat state and append defense events, but still must preserve landing
state and turns; the evidence records that database delta rather than hiding it.
Session and credential fields are redacted before the evidence JSON is written.

## Scenario catalog

The companion `scenarios/v3_codex4_acceptance_catalog.json` keeps AI policy
separate from canonical requirements and covers the requested ordinary-player
journeys:

| Requirement | Journey | Current gate |
|---|---|---|
| V3-PLAN-01 | landing/refusal atomicity | selected next scenario |
| V3-SHIP-01 | ownership and lifecycle settlement | server contract / ledger |
| V3-ECO-01 | trade settlement and retry | server contract / live settlement |
| V3-ECO-02 | bank interest | engine wiring |
| V3-VIS-01 | visibility and scan boundaries | server policy |
| V3-HAZ-01 | sector-entry hazards | atomic movement |
| V3-PLAN-02 | production and citadel lifecycle | canonical decision |
| V3-INS-01 | insurance purchase and loss settlement | canonical decision |

The catalog is a preparation artifact, not acceptance evidence. No live
scenario was run in this checkpoint.

## Verification

```text
python3 -m pytest -q ai_player/tests/test_v3_codex4_acceptance_catalog.py \
  ai_player/tests/test_v3_planet_landing_harness.py \
  ai_player/tests/test_offline_acceptance.py
24 passed
```

The release ledger records QA-INT-01 as blocked because BigBang rejects
`125_trade_settlement_invariants.sql` as an unclassified migration. Therefore
PostgreSQL/live-server evidence remains unavailable; no release item is marked
accepted here. Once the migration classification/build dependency is resolved,
run the selected landing cases in the disposable universe and attach sanitized
evidence JSON for valid, remote, unauthorized, zero-turn, defense, and
persistence-failure cases.

## Harness hardening follow-up

The `V3-PLAN-01` adapter harness now treats explicitly declared success
requirements as evidence gates: a required landing event, an authoritative
database change, and response/target planet identity must agree with the
before/after state. Refusal handling remains side-effect-free for landing state
and turns; defense damage is retained as an explicitly recorded exception.

Verification after this change:

```text
python3 -m pytest -q ai_player/tests/test_v3_planet_landing_harness.py \
  ai_player/tests/test_v3_codex4_acceptance_catalog.py \
  ai_player/tests/test_deterministic_scenarios.py: 14 passed
python3 -m pytest -q ai_player/tests/test_qa_reporting.py ai_player/tests/test_recovery.py: 38 passed
```

This remains fixture/harness evidence only. No response, event batch or
database snapshot from a live disposable universe is claimed; the release
ledger's QA-INT-01 migration-classification blocker still prevents that run.

