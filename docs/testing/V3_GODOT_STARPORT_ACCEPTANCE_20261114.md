# V3 Godot and StarPort integration checkpoint — 14 November 2026

This addendum records the evidence collected after `e37b8b94`. It does not
promote the whole Godot client or StarPort system to fully verified.

## Godot command menu

Godot 4.5.2 imported the project. The command menu regression passed with 196
checks after restoring reachability for existing V3 commands and correcting
navigation/category visibility. Additional focused tests passed for command
results (12), computer intelligence (15), density responses (5), dialog layout
(8), gameplay view (22), authoritative refresh (14), authentication dispatch,
protocol transport (9), and event presentation (24). The shared asset catalogue
reported 13 assets and no warnings.

The whole client regression runner passed its 802 Python tests, then stopped at
`planet_command_response_test.gd`. This test still contains scenarios whose
fixtures do not consistently settle an authoritative refresh between simulated
commands. In particular, a landed planet is represented by its actual ID in
`ship.onplanet`, and a launch keeps the last confirmed surface until the
authoritative departure refresh completes. Some fixture cases instead use
`onplanet: 1` for planet 77, expect the context to clear from the reply alone,
or issue new requests while a previous refresh is pending. These are test
contract issues to repair; the remaining test has not been marked passing.

## Godot live journey

`client/godot_client/godot/tests/live_disposable_journey_test.gd` drives the
actual `Main.tscn`, `LoginView`, menu buttons, inline warp form and Godot network
transport. With `TWCLONE_GODOT_LIVE=1`, it registered an ordinary player through
the UI, waited for authoritative player/ship/sector refresh, warped to an
adjacent sector, checked the exact one-turn debit, and ran the detailed current
sector scan. The journey printed:

```text
LIVE_DISPOSABLE_PASS journey=registration,navigation,turns,scan player_id=108 origin=1 destination=2 ship_id=41
```

Read-only PostgreSQL verification for player 108 showed sector 2, 119 turns,
active ship 41 and starting credits persisted. This is a partial Godot journey;
trading, shipyard, planet, corporation, logout and reconnect have not been
completed through Godot UI.

An attempted post-restart replay was blocked by the disposable image entrypoint:
it tried to overwrite a read-only `/tmp/bigbang.json` and exited before serving
requests. The database remained available. The StarPort persistence query after
that restart showed port 1 inactive in sector 11, durability 0, no positive
stock rows, and capacity level 4. This proves PostgreSQL persistence across the
container restart, but not game-server recovery with that broken entrypoint.

## StarPort concurrent requests and rollback

All actions below used two ordinary accounts authenticated to the disposable
V3 server. Direct SQL only staged deterministic test state, injected database
faults, and read authoritative outcomes. The two contenders were synchronized
to issue actual concurrent requests: player IDs 109 and 110, port ID 1 in
sector 11. The server-level request outcomes and before/after PostgreSQL values
were observed during execution. PostgreSQL transaction IDs and a sanitized
request-by-request transcript were not retained as standalone artifacts, so
the reproducibility/evidence packaging requirement remains incomplete.

| Case | Result and database evidence |
|---|---|
| Two concurrent upgrades | PASS. Requests upgraded the same commodity capacity serially from level 0 to level 2. Exactly 300 ORE was consumed, credits fell by the two approved costs (10,000 and 20,000), port petty cash rose by 30,000, and no balance went negative. |
| Concurrent upgrade and attack | PASS. The attack applied one 200-durability hit and 50-point retaliation; the upgrade reached level 3 and settled 300 ORE and 30,000 credits. Port remained active, defence and capacity were consistent, and the sector had one active port. |
| Same-key concurrent attack retry | PASS. Both callers received the same receipt. PostgreSQL showed one receipt, one turn debit, and one durability settlement. |
| Injected failure after upgrade resource settlement | PASS rollback. Server returned error 1403; credits, ORE, port cash, capacity and receipt count were unchanged. Retrying the same idempotency key after removing the fault completed one upgrade exactly once. |
| Injected failure during destruction stock settlement | PASS rollback. Server returned error 1012. Port remained active at durability 1, commodity stocks were unchanged, turns and action receipts were unchanged. Removing the fault and retrying destroyed the port atomically, zeroed stock without salvage, and retained the inactive historical row. |
| Server restart persistence | PARTIAL. PostgreSQL retained the destroyed port, capacity level 4 and zero stock after restart. The server container did not return to service because the image entrypoint could not overwrite its read-only `/tmp/bigbang.json`; no post-restart protocol request was accepted. |

One shield snapshot increased by five between observations during the injected
destruction test, consistent with a scheduled shield tick. The rollback check
therefore does not claim exact shield conservation across that scheduler
interval. It did show no combat receipt or turn settlement on the injected
failure, and the subsequent retry settled destruction once.

## Remaining state

### Follow-up server restart check

The read-only `/app/bin/bigbang.json` startup issue was fixed in
`docker/entrypoint-server.sh`: each server start copies the image config into a
private writable directory created with `mktemp`. A later isolated V3 server
container using that entrypoint restarted and accepted an ordinary Python
login plus `feedback.status` request against the same PostgreSQL database.
This verifies the bootstrap fix and serving-process restart path generally;
the full StarPort-specific construction-replay matrix was not repeated in
that later feedback-focused database.

- **IMPLEMENTED and focused-tested:** Godot menu reachability, response handling,
  authoritative refresh safeguards.
- **VERIFIED against disposable PostgreSQL:** StarPort concurrent upgrades,
  concurrent upgrade/attack, concurrent same-key attack retry, upgrade rollback,
  destruction rollback and database persistence.
- **VERIFIED against live disposable server through Godot UI:** registration,
  initial authoritative refresh, navigation, turn debit and sector scan.
- **BLOCKED:** Godot test runner after the planet response fixture; post-restart
  server protocol acceptance due the disposable entrypoint issue.
- **NOT IMPLEMENTED in this checkpoint:** player feedback and suggestion
  collection, diagnostic ring buffers/redaction, feedback protocol/outbox,
  GitHub publisher/mock integration, and the broader Godot gameplay journey.

No public GitHub issues were created. The existing beta and production systems
were not used or modified.

## Acceptance closure — 15 November 2026

All new evidence below used disposable PostgreSQL on loopback and the isolated
V3 server container. The server was restarted after fixture reset so its
in-memory NPC state matched the database.

### Godot 4.5.2

`tests.v2/suite_client_regressions.py` completed with Python **807 passed** and
Godot **104 passed, 0 failed, 1 skipped** (the live-server journey is run
separately). `command_menu_test.gd` passed **206 checks** and
`planet_command_response_test.gd` passed **115 checks**. The Godot 4.5.2 live
journey executed the actual `Main.tscn`, `LoginView`, menu handlers and network
transport against the disposable server. It passed login, navigation, turns,
scan, docking, commodity buy/sell, hardware purchase, shipyard upgrade, planet
landing, harvest, launch, corporation creation/deposit, logout, reconnect and
queue-only feedback. Output included
`LIVE_DISPOSABLE_PASS journey=existing-login,navigation,turns,scan,docking,port-workflow,commodity-buy,commodity-sale,hardware-purchase,shipyard-upgrade,planet-landing,harvest,launch,corporation,logout,reconnect,feedback`.
No Godot error, script error, ObjectDB or resource-leak diagnostic was emitted.
The feedback row remained pending locally; no public issue was created.

### StarPort upgrade and server restart

Player 5567 (`planet_gov`) used the disposable server to upgrade ORE capacity
at active port 9 in sector 1. Deterministic fixture setup supplied 100 ORE and
adequate holds. The authenticated response reported level 1, 10,000 credits
spent and 100 material spent. Repeating the same idempotency key returned the
same successful result. After restarting the serving game-server container,
the same player authenticated and `port.status` succeeded. SQL assertions
showed credits reduced by exactly 10,000, ship ORE reduced from 100 to 0, port
petty cash increased by 10,000, ORE capacity level persisted at 1, and exactly
one action receipt existed for the idempotency key. This closes the post-restart
serving request and upgrade persistence case.

### Full server-suite fixture correction

The 155-suite run executed 2,340 cases: 2,339 passed, 1 failed, 4 skipped. The
only failure was `suite_planet_market_buy_order_lifecycle.json`: its shared
seller port also had unrelated open commodity orders, so scheduled settlement
changed the port balance during the isolated assertion. The fixture now cancels
open orders at its selected port before creating its scenario. The affected
suite was rerun after database reset/server restart and passed **29/29** cases.
After the fixture correction, the complete run executed **155 suites, 2,341
cases passed, 0 failed, 4 skipped** (one suite declared those four skipped
cases). The report and transcript are under
`/tmp/twclone-acceptance-d5b11270/tests-v2-full-final-report.json` and
`/tmp/twclone-acceptance-d5b11270/tests-v2-full-final.log`.

### Concurrent StarPort operations and retry accounting

Two ordinary sessions were synchronized with a thread barrier against port 9.
The first pair issued two concurrent ORE capacity upgrades from level 0. Both
returned success at serialized levels 1 and 2. Final PostgreSQL state was
capacity level 2, petty cash +30,000, each player's credits debited by the
response amount, and ship ORE debited by 100 and 200 respectively. Both
idempotency receipts were present exactly once. Reissuing both keys
concurrently returned the same results and left capacity, balances, materials,
and the two receipt rows unchanged.

A second barrier-synchronized pair issued an ORE upgrade and a port attack
against port 9 in unprotected sector 11. Both succeeded. The upgrade advanced
level 2 to 3, spending 30,000 credits and 300 ORE; the attack applied 650
damage and 100 retaliation. PostgreSQL showed port durability 10,000 to 9,350,
capacity 2 to 3, petty cash 40,000 to 70,000, the upgrading player's credits
100,000 to 70,000 and ORE 300 to 0. The attacking player's credits were
unchanged, turns went 1,000 to 999, shields 100 to 0, and hull/fighters stayed
100/20. The port stayed active and both action receipts recorded one result.
After restarting the serving game container, the attacker authenticated and a
fresh `port.status` request succeeded; all listed state remained unchanged.

Sanitised concurrent request/response and SQL snapshots are retained under
`/tmp/twclone-acceptance-d5b11270/starport-concurrent-upgrades.log`,
`starport-concurrent-upgrade-retries.log`, and
`starport-concurrent-upgrade-attack.log`. The receipt schema does not store a
PostgreSQL transaction ID, so request IDs and durable receipt keys are the
available transaction correlation evidence.
