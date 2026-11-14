# V3 Python test failure triage — 2026-11-14

## Result

The assessment's reported Python suite result was reproduced at the starting
HEAD (`bb31270f6d8b819af959a81289b27f9db0fbecd7`): **609 passed, 190 failed
(799 total)**. A fresh run after correcting test fixtures to use the published
V3 response types and updating a superseded harvest availability assertion
produced **709 passed, 90 failed**. After resolving a port-direction
normalization defect and preserving non-dictionary malformed responses in the
fake connection, the final complete run produced **716 passed, 83 failed
(799 total)** in 7.00 seconds.

Exact command:

```sh
python3 -m pytest -q client/python_client/tests \
  --junitxml=/tmp/twclone-v3-python-failures-triaged-20261114-final.xml
```

The final JUnit artifact is at
`/tmp/twclone-v3-python-failures-triaged-20261114-final.xml`. The suite remains
red. The increase in passing tests is not 106 confirmed gameplay defects
fixed: most of that change comes from test fixtures now supplying the schema
types published by the V3 server, and from replacing an obsolete test that
asserted harvesting was unavailable despite the workflow being implemented.

## Failure groups

| Root-cause area | Failing tests | Current evidence and classification |
|---|---:|---|
| Planet workflows and landing | 54 | Mixed. Most failures exercise authoritative landing/refresh contracts with incomplete snapshots, stale call-order/diagnostic expectations, or contexts lacking a confirmed landed planet. These are client contract-test mismatches until checked against the current protocol. Planet menu workflows in this group also need separate review before concluding the ordinary-player flow works. |
| Trading | 13 | One real bug was fixed: omitted fixed-direction metadata was normalized to explicit `None`, causing `_trade_rows` to reject commodities that should be left to the server quote. The remaining failures include out-of-date presentation/error expectations and typed receipt/identity refresh fixtures; the trade journey still needs runtime acceptance. |
| Ship roster | 6 | Disconnect, typed response and receipt reconciliation tests have incomplete or stale fake-server responses. No live server evidence in this run establishes whether these are client defects or test-contract drift. |
| Navigation | 4 | Cached-sector and movement refresh tests disagree with the current authoritative normalization behavior. Not yet adjudicated against live server responses. |
| Combat and hazards | 4 | One attack test expects a request field not sent by the current handler; other failures concern hazard refresh contracts. Need protocol-backed client acceptance to classify gameplay impact. |
| Other client workflows/contracts | 2 | Remaining response-shape and player-experience contract tests. Ship rename's untyped-success fixture was corrected and now passes. Not yet adjudicated against a live server. |
| **Total** | **83** | Full suite failures; none counted as passing or as proven harmless. |

The largest concentration is the planet and landing area. The failure count
is not a count of distinct gameplay defects: related tests share fixtures and
contract assumptions. Some assertions are clearly stale (for example, a test
expects `sell/buy` while the renderer emits `buy/sell`); others assert
important fail-closed behavior and must not be relaxed merely to turn the suite
green.

## Confirmed changes in this triage

- Fake successful RPC replies now receive the published response `type` for
  commands with a known V3 contract. Tests specifically exercising untyped or
  malformed replies use `queue_untyped`; scalar/`None` malformed replies remain
  untouched. This makes the fixture exercise the same schema boundary as the
  server without weakening client validation.
- Updated the harvest menu test to execute the implemented Python harvest
  workflow instead of asserting that the menu reported it unavailable.
- Port normalization now preserves omitted `sells`/`buys` fields as omitted.
  Explicit malformed `null` values remain rejected. Focused port tests cover
  omitted versus explicit-null metadata, and the full Python suite was rerun.
- Sector refresh now checks that the response data is an object before
  normalization and reports malformed data while preserving the last
  confirmed sector. Visibility refresh tests assert the security-relevant
  fields rather than requiring normalized snapshots to equal a minimal fixture
  dictionary exactly.

## Next repair order

1. Reconcile the landing contract tests with actual server response envelopes,
   then repair any client workflow that blocks a legitimate landing or surface
   operation. Keep identity, ship and `onplanet` checks fail-closed.
2. Run and repair the Python trade journey against a disposable PostgreSQL V3
   server; settle each remaining trade fixture mismatch against the server's
   emitted schema before changing client behavior.
3. Exercise ship roster, transfer and recovery workflows against the server.
4. Use actual client/server transcripts to adjudicate navigation and combat
   refresh failures; rerun the full suite after each confirmed repair.

No database-backed acceptance journey was run as part of this failure-count
triage. Therefore this report does not certify player journeys, gameplay
parity, or beta readiness. Godot runtime status is unchanged and remains
unverified.
