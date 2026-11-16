# StarPort acceptance

Latest recorded evidence: 15 November 2026. All live cases used ordinary
player workflows against isolated disposable PostgreSQL and a serving V3
server. Direct SQL was limited to deterministic fixture setup, fault injection
and read-only assertions. Numeric rates and costs are versioned V3 settings;
where the decision register labels them noncanonical, no TWGS 3.13 parity is
claimed.

## Construction and scheduled delivery

The Python player's surface workflow began construction after a lost successful
reply. The V3 server was restarted, the player reauthenticated, and the same
request key was retried. The retry returned construction ID **2** with
`idempotent_retry=true`; PostgreSQL retained one construction row. Credits
changed once, **500,000→400,000**, and planetary ORE/ORG/EQU each changed once,
**2,000→1,000**.

Three scheduled deliveries were then run across serving-process restarts on a
fresh PostgreSQL 16 database. Construction began with 10,000,000 credits and
2,000 of each required material. Initial settlement left 9,900,000 credits
and 1,000 each ORE/ORG/EQU. Each scheduled delivery consumed 100 of each
material. After delivery three, construction was `completed`, port 190 was
active in sector 151, and the planet retained 700 of each. A later restart
and authenticated `player.my_info`/`ship.status` requests did not replay the
completed delivery or change balances.

| Scenario | Result |
|---|---|
| Delivery after restart | PASS; progress and material deduction persisted once |
| Injected failure during delivery 2 | PASS rollback; progress remained 1 and all materials remained 900 |
| Restart and retry after removing fault | PASS; progress advanced once to 2 and materials became 800 each |
| Third delivery and completion | PASS; port 190 created once, materials became 700 each |
| Restart after completion | PASS; receipt, stock and active port remained unchanged |
| Construction persistence trigger failure | PASS rollback; credits/materials unchanged and no construction row persisted |

The five-second interval and advanced due timestamps were confined to the
disposable database. No PostgreSQL transaction IDs were captured, and failures
were not injected at every SQL statement boundary.

## Capacity upgrades and port combat

See [`V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`](V3_GODOT_STARPORT_ACCEPTANCE_20261114.md)
for synchronized concurrent upgrade/attack requests, same-key retry,
transaction rollback, destruction and post-restart `port.status` evidence.
The following Python player scenarios are also recorded from the focused
acceptance run:

- Upgrade charged 10,000 credits and 100 ORE once, increased capacity by one
  level/1,000 units, and increased port petty cash by 10,000. Same-key replay
  made no second debit.
- Insufficient upgrade material was refused without changing credits, cargo,
  port cash or capacity.
- Captain Z refused the protected-sector port attack without changing the
  port or turns; the player's destruction/recovery occurred once, and replay
  did not destroy another ship.
- In an unprotected sector, attack reduced durability from 10,000 to 1,000,
  then a second strike destroyed the port. Retaliation consumed fighters,
  then hull; destruction left an inactive historical record, zero stock and
  no salvage. The attacker recovered in an escape pod.
- Concurrent same-key attack retries returned one receipt and applied one
  turn, retaliation and durability settlement.

## Limits

The tested operations passed, but this evidence is not exhaustive across every
port class, possible concurrency ordering, database statement failure or
combat edge case. External beta remains blocked by the operational items in
[`V3_RELEASE_CANDIDATE_AUDIT_20261115.md`](V3_RELEASE_CANDIDATE_AUDIT_20261115.md).
