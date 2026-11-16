# Godot live gameplay journey acceptance — 16 November 2026

## Environment and method

- Godot executable: `/home/rick/bin/gdot`, Godot 4.5.1.
- Server: current V3 source in the disposable acceptance container, loopback
  port 55492.
- Database: disposable PostgreSQL container `twclone-robbery-parity-db-2afe35dc`,
  database `twclone_v3qa_parity_2afe35dc`.
- Account: ordinary player ID 576. Authentication and gameplay were performed
  by the Godot client. SQL was used only to prepare deterministic disposable
  fixtures and then read back persisted state.
- The run exercised `Main.tscn`, actual Godot menu/workflow handlers, and the
  real protocol transport. No JSON-only substitute was used.

## Journey results

| Operation | UI/client result | Server/database evidence | Result |
|---|---|---|---|
| Authenticate | Existing ordinary account authenticated in the login view | Player 576's session restored its current ship and state | PASS |
| Navigate and scan | Warp to connected sector and scan completed | Authoritative sector refreshed; navigation consumed turns | PASS |
| Dock and trade | Port workflow opened; bought and sold commodities | Server-confirmed balances/cargo; post-run SQL confirms ship 9141 cargo state | PASS |
| Refresh sell availability | The port workflow remained visible after authoritative snapshot refresh; selling became enabled when cargo was available | Regression test reproduces no-ORG → ORG=1 snapshot transition | PASS |
| Buy equipment | Equipment purchase completed in the port workflow | SQL readback: ship 9141 equipment count 10 | PASS |
| Shipyard upgrade | Shipyard menu accepted and confirmed the upgrade | Updated active ship persisted as ship 9141 | PASS |
| Planet landing and harvest | Landed on planet 700, harvested 3 ORE, and received server-confirmed result | SQL: planet 700 retained ORE quantity 3; active ship cargo ORE was 5 after the operation | PASS |
| Planet departure | Launched from the surface and returned to the sector workflow | SQL: active ship `ported=0`, `onplanet=false`, sector 1 | PASS |
| Corporation operation | Corporation workflow completed an authorized operation | SQL: player 576 is Leader in corporation 112 | PASS |
| Disconnect and reconnect | Logged out and authenticated again through the client | SQL: player, active ship, location and corporation membership persisted | PASS |

The run log is `/tmp/twclone-godot-live-20261116l.log`. The read-only SQL
readback is `/tmp/twclone-godot-live-acceptance-sql-20261116.txt`. No
credentials are included in either evidence summary.

## Defect repaired

When a port workflow was already open, `GameplayView.set_snapshot()` updated
the main snapshot but did not refresh the workflow's authoritative ship hold
and credit values. The menu could continue to disable a valid sale after cargo
changed. The view now refreshes the open port workflow from each authoritative
snapshot. `gameplay_view_test.gd` covers the disabled-to-enabled transition.
The live journey verified buy, sale, and subsequent server-confirmed state.

## Regression results

- `gdot --headless --path client/godot_client/godot --script
  res://tests/gameplay_view_test.gd`: **22 passed**.
- `GODOT_BIN=/home/rick/bin/gdot python3
  tests.v2/suite_client_regressions.py`: Python **807 passed**; Godot **104
  passed, 0 failed, 1 skipped**. The single skip is the live-only journey
  script, which is executed separately with disposable server credentials.
- Live journey exit code: **0**; final marker:
  `LIVE_DISPOSABLE_PASS journey=existing-login,navigation,turns,scan,docking,port-workflow,commodity-buy,commodity-sale,hardware-purchase,shipyard-upgrade,planet-landing,harvest,launch,corporation,logout,reconnect`.

## Limits

This is a successful Godot journey for the listed operations, not full client
parity. It does not cover two-player combat, retreat/destruction/escape-pod
recovery, planetary capture/citadel progression, or corporation asset
transfers. Godot 4.5.2 was not available; this run used 4.5.1.
