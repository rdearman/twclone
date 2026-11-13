# V3 Python Player Journey Acceptance — 2026-11-13

## Scope and environment

This is a partial runtime acceptance against an isolated PostgreSQL database and the V3 server image built from the worktree at HEAD `994fdec1` plus the changes in this report's commit. It did not use the beta deployment or host PostgreSQL. The disposable DB container was `twclone-playability-db-20261113`, on private Docker network `twclone-playability-net-20261113`; the V3 server was `twclone-playability-game-20261113`, exposed only on loopback port 12485. It was initialized from scratch using the repository BigBang and migrations. The two accounts, `PlaytestOne` and `PlaytestTwo`, registered through the ordinary Python client. Both started at 10,000,000 credits with a starter ship and 120 turns.

The Docker server was restarted after the first session using the same database. The first restart attempt exposed a disposable-container `/tmp` permission issue; the test container was recreated with a writable tmpfs and restarted successfully. That was fixture configuration, not a game defect. Evidence transcripts and container/database details are retained in `/tmp/twclone-v3-playability-994fdec1-20261113/`.

## Operation results

| TW2002 operation | Result | Evidence / finding |
| --- | --- | --- |
| Register and login two ordinary players | PASS | Both registered through `client.py`; login worked before and after server restart. `python-register-one.txt`, `python-register-two.txt`, `python-restart-login.txt` |
| Starting resources and persistence | PASS | Read-only SQL showed both players at 10,000,000 credits; PlaytestOne remained in sector 3 after restart. |
| Adjacent navigation and turn charge | PASS | Python “Go to Adjacent Sector” moved player 106 from sector 1 to 2 and charged one turn; server response/client view agreed. |
| Route plotting/following | PASS | Python pathfind from 2 to 3 returned route `2 → 3`; following moved player 106 to 3 and charged one turn. `python-pathfind-retest.txt` |
| Avoid add/list persistence and route refusal | PASS | Added sector 2 via Settings → Avoid List; list displayed it. SQL confirmed `(player_id=106, sector_id=2)`. Pathfinding from 3 to avoided sector 2 failed with 403 and did not move or charge turns. `python-avoid-nav.txt`, `python-avoid-pathfind.txt` |
| Scan workflows | BLOCKED | Fast scan displayed no adjacent data at sector 2 despite server-reported exits. Detailed scan was not completed in this run; scan behavior needs a focused follow-up. |
| Commodity trading, profit, equipment purchase | BLOCKED | No trade journey was completed. Generated player accounts start with no cargo/equipment beyond a single fighter; a safe port workflow, purchase and stock/credit verification remain to be run. |
| Fighter/mine deployment, two-player combat, retreat, loss/recovery | BLOCKED | Players were not staged into a combat encounter. The starter ships have one fighter and no mines; combat lifecycle is unverified. |
| Ship purchase/repair/transfer/sale/replacement | BLOCKED | These workflows were not run through the client. |
| Planet creation/colonists/production/harvest/citadel | BLOCKED | 150 generated planets exist, but no ordinary player planet-acquisition and development loop was completed. Harvest client workflow is now exposed in the landed-player surface menu but was not accepted end to end. |
| Corporation recruitment/permissions/treasury/member transfer | BLOCKED | Member-transfer UI is now exposed in the corporation menu, but recruitment and transfer were not run. |
| Logout, reconnect, server restart and persistence | PASS | Python client quit cleanly; the V3 container restarted against the same DB; PlaytestOne authenticated again and SQL retained player location, credits, and avoid state. |
| Godot journeys | BLOCKED | Godot runtime is unavailable in this execution environment; no Godot journey was attempted. |

## Defects fixed during acceptance

1. `move.describe_sector` rejected omission of `sector_id` although the protocol and handler allow defaulting to the player's current sector. The server schema now agrees with that contract, so the Python client can confirm a move and refresh the destination.
2. Python adjacency normalization discarded the server's object-shaped `{to_sector: ...}` rows. It now extracts valid destination IDs, allowing the ordinary adjacent-move menu to show and use exits.
3. Server pathfinding ignored persisted avoids and treated only the current sector/direct exits as known. It now loads player avoids and known warp destinations before routing; the ordinary client test confirmed successful routing and avoid refusal.
4. Python planet surface lacked harvest and Planetary Trade Agreement workflows, and the corporation menu lacked member asset transfers. Harvest, PTA and member transfer workflows/menu entries have been added. Their real-world acceptance remains outstanding.

## Remaining blockers ranked by player impact

1. **P0 — Combat and recovery:** no proven two-player fight, retreat, destruction, or recovery loop.
2. **P0 — Planetary gameplay:** no proven ordinary-player colonisation, colonist/resource transport, production, harvest, or citadel lifecycle.
3. **P0 — Ship lifecycle:** purchase, repair, transfer, sale, and replacement remain unverified through Python.
4. **P0 — Corporation loop:** recruitment, permissions, treasury, and the newly exposed member transfer remain unverified end to end.
5. **P0 — Complete player journey:** trading/equipment and the above acquisition/combat/planet loops have not yet been joined into a complete session.
6. **P1 — Scans:** Fast Scan did not present adjacent data during runtime; determine whether response shape or server data is responsible, then verify detailed scan and fog-of-war boundaries.
7. **P1 — Planetary Trade Agreement:** new Python flow needs a real landed-planet transaction, receipt, resource conservation, and rollback acceptance.
8. **P1 — Godot parity:** repeat the same journeys when a Godot runtime is available.

## Verification boundaries

This run does not establish full V3 playability or original TW2002 parity. Passing observations above come from real Python menu flows plus read-only SQL evidence. Unrun behaviors remain blocked, not passed based on RPC registration or source presence. Focused client/server regression checks were run during implementation; a broader client test selection also exposed existing fake-response fixtures that omit protocol response types, so those failures were not treated as server-runtime findings.
