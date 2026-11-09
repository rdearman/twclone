# CODEX 4 issue #490 QA checkpoint — command-boundary evidence

## Requirement

`V3-PLAN-01` remains the next unblocked CODEX 4 supporting slice. The
section-19 landing journey is still not release-accepted because PostgreSQL
and live-server evidence are unavailable.

## Progress

- Extended `ai_player/qa_harness.py` so a scenario can declare expected
  response statuses/types and state/database invariants for each command.
- A mixed journey can now fail when a refused or duplicate retry mutates its
  own command boundary, even when the aggregate journey classification is
  `mixed`.
- The adapter remains read-only and injected: it does not provision, migrate,
  seed, reset, retry, or write database rows. Evidence continues to capture
  sanitized requests, responses, events, and state/database snapshots before
  and after every ordinary-player command.

## Verification

```text
python3 -m pytest -q \
  ai_player/tests/test_v3_codex4_acceptance_catalog.py \
  ai_player/tests/test_v3_codex4_ordinary_player_scenarios.py \
  ai_player/tests/test_v3_planet_landing_harness.py \
  ai_player/tests/test_deterministic_scenarios.py \
  ai_player/tests/test_v3_bank_interest_scenario.py
35 passed
```

No PostgreSQL or live-server gameplay run was performed. No acceptance state is
claimed. Ship ownership, trade, bank-interest, visibility, hazard, planet
lifecycle and insurance remain represented as deferred/decision-gated scenario
definitions and were not executed ahead of their ledger dependencies.

## Dependency note

`QA-INT-01` remains blocked because the disposable BigBang path rejects
`125_trade_settlement_invariants.sql` as unclassified. CODEX 2/3 must classify
and build the supported migration set, then complete the disposable identity
and bootstrap gate. After that, CODEX 4 can run the landing cases against the
loopback QA server and attach real response/event/database evidence.
