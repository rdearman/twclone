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
