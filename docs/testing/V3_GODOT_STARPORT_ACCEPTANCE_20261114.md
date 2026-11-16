# Godot and StarPort acceptance

Latest evidence: 15 November 2026. All runtime cases used disposable
PostgreSQL and an isolated V3 server. This report records only the tested
journeys and transaction cases; the overall deployment decision is in the
[release-candidate audit](V3_RELEASE_CANDIDATE_AUDIT_20261115.md).

## Godot regression and player journey

Godot 4.5.2 project import and client regression completed with **104 passed,
0 failed, 1 skipped**. The command-menu regression passed **206 checks** and
`planet_command_response_test.gd` passed **115 checks**. The separately run
live Godot journey used the actual `Main.tscn`, login UI, menu handlers and
network transport. It exercised login, navigation, turn consumption, scan,
docking, commodity buy/sell, hardware purchase, shipyard upgrade, planet
landing, harvest, launch, corporation creation/deposit, logout, reconnect and
queue-only feedback submission. The run emitted no Godot script, ObjectDB or
resource-leak errors. Feedback remained queued locally; no public issue was
created.

## StarPort concurrency and recovery

Two ordinary authenticated players made synchronized requests against port 9.
The tested outcomes were:

| Scenario | Result |
|---|---|
| Two concurrent ORE capacity upgrades | PASS. Operations serialized at levels 1 and 2; 100 and 200 ORE and 10,000 and 20,000 credits were each charged once; receipts were unique. Concurrent same-key retries changed no state. |
| Concurrent upgrade and attack | PASS. Capacity advanced from level 2 to 3; durability changed 10,000→9,350; the documented 100-point retaliation and resource/turn effects matched receipts. The port remained active with one active port in the sector. |
| Concurrent same-key attack retry | PASS. Both requests returned the same receipt; one turn and durability settlement occurred. |
| Injected failure during upgrade settlement | PASS rollback. Credits, ORE, port cash, capacity and receipt count were unchanged. Retrying after removing the failpoint committed once. |
| Injected failure during destruction stock settlement | PASS rollback. Port activity, stock, turns and receipts were unchanged. Retry destroyed the port, zeroed stock without salvage and retained its inactive history row. |
| Upgrade persistence after game-server restart | PASS. Reauthenticated player read `port.status`; level, balances, materials and single receipt matched pre-restart state. |

One shield snapshot changed by five during a scheduled regeneration interval
in the earlier destruction rollback run. That observation is not treated as a
combat debit; the failure path produced no combat receipt or turn settlement.

The latest [StarPort lifecycle report](V3_STARPORT_ACCEPTANCE_20261114.md)
also verifies construction replay after restart, scheduled material deliveries
across serving-process restarts, and selected delivery/construction rollback
boundaries. PostgreSQL transaction IDs were not collected; request IDs and
durable idempotency receipts provide the recorded correlation evidence.

## Godot test harness fixture correction

The full server regression initially had one failure because a shared seller
port retained unrelated open market orders. The fixture now cancels those
orders before its isolated assertion. The affected suite passed 29/29, then
the full run passed **2,341 cases, 0 failed, 4 skipped across 155 suites**.
This is a test-fixture correction, not a gameplay behavior change.

## Evidence limits

- The acceptance is for Godot 4.5.2 and the operations listed above.
- No live GitHub issue was created; publication is mock-tested and queue-only
  mode was exercised.
- StarPort fault injection covers selected transaction boundaries, not every
  SQL statement failure or possible transaction ordering.
- Deployment to external players remains blocked by the operational findings
  in the release-candidate audit.
