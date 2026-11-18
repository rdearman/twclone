# V3 final gameplay parity closure

Reviewed: 17 November 2026
Starting review baseline: `4e60f9966b363529b20ab34bcb49b05b1d160a53`
Parity status refresh: 16 November 2026, after the robbery persistence fix.

This report reconciles the approved decision register with the current source,
the runnable client/server tests, and prior disposable-PostgreSQL acceptance
reports. The register contains 69 IDs: 67 V3 requirements and two explicit
exclusions (`STARPORT-V4-01`, `EQUITY-01`). V3 scope includes the approved
TW2002 player loop plus approved TWClone additions. The Galactic Exchange
(`EXC-*`) is a V3 extension, not an original TW2002 mechanic. Versioned rates
and costs under `V3-*` are configurable, noncanonical defaults; they do not
establish TWGS 3.13 parity.

## Requirement status

`VERIFIED` means the named behavior has executable runtime evidence. It does
not mean every UI path or every neighboring mechanic in that subsystem passed.
The Python and Godot coverage limits are listed separately below. All other
in-scope rows remain obligations; source presence is not treated as proof.

| Decision ID | Requirement area | Status |
|---|---|---|
| POP-01 | Population state and transitions | VERIFIED |
| POP-02 | Acquisition and capture disposition | PARTIAL |
| POP-03 | Slave productivity | IMPLEMENTED_UNVERIFIED |
| POP-04 | Slave morale | IMPLEMENTED_UNVERIFIED |
| POP-05 | Slave unrest configuration and scheduler | BLOCKED |
| POP-06 | Emancipation | IMPLEMENTED_UNVERIFIED |
| POP-07 | Capture conversion | IMPLEMENTED_UNVERIFIED |
| HARV-01 | Planet harvest capacity and accounting | VERIFIED |
| HARV-02 | Planet harvest permissions | IMPLEMENTED_UNVERIFIED |
| HARV-03 | Planet surplus to Exchange | IMPLEMENTED_UNVERIFIED |
| EXC-01 | Exchange participants | VERIFIED |
| EXC-02 | Exchange settlement | VERIFIED |
| EXC-03 | Exchange ring-fencing | VERIFIED |
| EXC-04 | No virtual vessel entities | IMPLEMENTED_UNVERIFIED |
| EXC-05 | Exchange order locking | IMPLEMENTED_UNVERIFIED |
| EXC-06 | Port demand and settlement | VERIFIED |
| EXC-07 | Exchange quantity conservation | VERIFIED |
| EXC-08 | Exchange finance and idempotency | VERIFIED |
| EXC-09 | Broader economy balancing | PARTIAL |
| SHIP-05 | Used-hull resale | IMPLEMENTED_UNVERIFIED |
| INS-02 | Guaranteed insurance funding | IMPLEMENTED_UNVERIFIED |
| TRADE-01 | No price-negotiation mechanic | IMPLEMENTED_UNVERIFIED |
| BANK-01 | Standing-order scheduling | IMPLEMENTED_UNVERIFIED |
| BANK-02 | Standing-order insufficient funds | IMPLEMENTED_UNVERIFIED |
| BANK-03 | Monthly date boundary | VERIFIED |
| BANK-04 | Invalid standing-order recipient | IMPLEMENTED_UNVERIFIED |
| BANK-05 | Standing-order settlement and races | IMPLEMENTED_UNVERIFIED |
| PLAYER-01 | Online-player visibility | IMPLEMENTED_UNVERIFIED |
| PLANET-01 | Planet production overview | IMPLEMENTED_UNVERIFIED |
| NPC-POP-01 | Ferengi population distribution | IMPLEMENTED_UNVERIFIED |
| NPC-POP-02 | Orion population distribution | IMPLEMENTED_UNVERIFIED |
| NPC-POP-03 | Ferengi fleet assignment | IMPLEMENTED_UNVERIFIED |
| FED-01 | Captain Z enforcement | VERIFIED |
| FED-02 | FedSpace MSL cleanup | VERIFIED |
| FED-03 | ISS jurisdiction boundaries | IMPLEMENTED_UNVERIFIED |
| NPC-FER-01 | Ferengi bounty and retaliation | IMPLEMENTED_UNVERIFIED |
| NPC-ORI-01 | Orion blockade targeting | PARTIAL |
| SHIP-01 | Ship-sale locations | VERIFIED |
| SHIP-02 | Ship acquisition and ownership | IMPLEMENTED_UNVERIFIED |
| SHIP-03 | Ship repair and upgrades | IMPLEMENTED_UNVERIFIED |
| SHIP-04 | Ship loss and recovery | IMPLEMENTED_UNVERIFIED |
| COMBAT-01 | Player ship combat | PARTIAL |
| COMBAT-02 | Combat destruction and recovery | PARTIAL |
| INS-01 | Insurance eligibility and claim | IMPLEMENTED_UNVERIFIED |
| INS-03 | Underinsured policy settlement | IMPLEMENTED_UNVERIFIED |
| MOVE-03 | Interruption policy | IMPLEMENTED_UNVERIFIED |
| MOVE-01 | Atomic transwarp | IMPLEMENTED_UNVERIFIED |
| MOVE-04 | No V3 mid-flight interruption | IMPLEMENTED_UNVERIFIED |
| MOVE-02 | Movement entry hazards | IMPLEMENTED_UNVERIFIED |
| PTA-V3-01 | Planetary Trade Agreement | VERIFIED |
| STARPORT-V3-01 | StarPort construction and lifecycle | VERIFIED |
| PARITY-PORT-ATTACK-01 | StarPort attack and destruction | IMPLEMENTED_UNVERIFIED |
| PARITY-PHOTON-01 | Photon targeting and effects | PARTIAL |
| PARITY-TOLL-01 | Toll-fighter payment and passage | PARTIAL |
| PARITY-TW-HOMING-01 | Transwarp homing fighter | IMPLEMENTED_UNVERIFIED |
| MOVE-05 | Transwarp fuel debit | VERIFIED |
| V3-PLANET-RATE-01 | Configured planet-class production ratios | VERIFIED |
| V3-TOLL-RATE-01 | Noncanonical toll rate | IMPLEMENTED_UNVERIFIED |
| V3-PHOTON-DURATION-01 | Noncanonical photon duration | IMPLEMENTED_UNVERIFIED |
| V3-PORT-BUILD-01 | Noncanonical port build/upgrade costs | IMPLEMENTED_UNVERIFIED |
| V3-PORT-COMBAT-01 | Noncanonical port durability/defence | IMPLEMENTED_UNVERIFIED |
| CORP-XFER-01 | Corporation member asset transfers | VERIFIED |
| SYS-07 | Session invalidation on player kick | VERIFIED |
| CFG-01 | Ship-sale factor configuration | IMPLEMENTED_UNVERIFIED |
| CFG-02 | Fixed slave output multiplier | IMPLEMENTED_UNVERIFIED |
| CFG-03 | Disabled-by-default slave unrest values | IMPLEMENTED_UNVERIFIED |
| CFG-04 | Insurance configuration | IMPLEMENTED_UNVERIFIED |

**Count (updated 18 November 2026):** 18 VERIFIED, 41 IMPLEMENTED_UNVERIFIED, 7 PARTIAL, 1 BLOCKED,
0 MISSING; 2 additional IDs are explicitly out of V3 scope. The two V4
exclusions are described above; they are not parity failures.

`POP-01` is now VERIFIED following disposable-PostgreSQL capture of an
unpopulated planet, preservation/rollback checks, player/corporation ownership
transitions, and readback after an actual serving-server restart. Executed
suite evidence and client response checks are recorded in the clause-level
matrix. This promotes one parent requirement. On 18 November, the approved
Orion targeting boundary resolved its specification blocker; focused live
acceptance verified warp/transwarp hostility, allied protection, restart and
retry behavior, so `NPC-ORI-01` moved from BLOCKED to PARTIAL. The ruleset 1.2
Ferengi reward amount and funding source have since been approved and the
reward passed authenticated PostgreSQL acceptance; `NPC-FER-01` moved from
BLOCKED to IMPLEMENTED_UNVERIFIED because penalty/pursuit clauses remain
untested. POP-05's separate per-cycle rule is compatible with its daily
stoppage/loss contract, but the latter's numeric settings remain unset. See
[`V3_RULESET_1_1.md`](../V3_RULESET_1_1.md) and the clause matrix. The remaining
parent classifications otherwise remain unchanged.

The clause-level worklist is in
[`V3_CLAUSE_ACCEPTANCE_MATRIX_20261116.md`](V3_CLAUSE_ACCEPTANCE_MATRIX_20261116.md).
It separates 227 candidate clauses from these parent counts, records ten
small executable clauses verified in the latest focused run, and lists the
three exact specification decisions still blocking acceptance. Clause
evidence is not used to promote a parent requirement until all its clauses
are covered.

The verified Exchange rows refer to the exercised PostgreSQL planet-sale,
port-purchase, settlement, accounting, idempotency, and ring-fencing paths.
`MOVE-05`, `CORP-XFER-01`, FedSpace enforcement/MSL, PTA, harvest, and StarPort
status refer to their linked focused acceptance evidence. Verification remains
operation-specific; the remaining client coverage and multi-system journeys
below are not implied by these rows.

## Executed regressions

The first run at this review baseline used an actual V3 server and disposable
PostgreSQL. It completed 155 suites with 154 passed and 1 failed (2,352 passed,
3 failed cases); the three failures exposed a lawful robbery persistence defect.
That defect has now been fixed and the full server run was repeated. The current
run completed all 155 suites: **2,361 passed, 0 failed, 4 skipped**. The four
TLS cases were skipped because the isolated listener was plain TCP. Details and
restart evidence are in
[`V3_POLICE_ROBBERY_ACCEPTANCE_20261116.md`](V3_POLICE_ROBBERY_ACCEPTANCE_20261116.md).

Other regression results:

- Python client: **807 passed** (`python3 -m pytest -q client/python_client/tests`).
- Python tests under `tests.v2`: **271 passed, 2 skipped, 227 subtests passed**.
- Godot client suite via `/home/rick/bin/gdot` (Godot **4.5.1**): **106 passed,
  0 failed, 5 skipped** (111 scripts); the five live-only scripts are run
  separately against a disposable server and PostgreSQL database.
- Latest combined Python/Godot regression rerun: Python **807 passed**; Godot
  **106 passed, 0 failed, 5 live-only skipped**.
- Godot catalog: 13 assets, 0 warnings.
- `make -C bin -j2`: passed with the repository's ASan/UBSan build flags.
- `git diff --check`: passed.

The stronger JSON assertion runner now fails closed on missing response paths,
resolves saved-variable and array/filter paths, supports the operators used by
the suite, and rejects unknown operators. Four focused tests cover those
behaviors. This exposed stale fixtures and assertions that had previously been
silently ignored. Corrections were made only where the protocol or actual
server response established the mismatch: port-route selection, bank balances
and standing-order cleanup, shield-aware quasar damage setup, omitted privacy
fields, trade direction selection, and the Orion persistence fixture after the
canonical rig reset.

## Client journeys and coverage

The source-level and prior runtime evidence supports ordinary-player access to
common Python and Godot workflows, but not complete parity through both clients.

The 16 November Godot live UI journey against disposable PostgreSQL passed
navigation/scan, docking, port buy/sell, equipment purchase, shipyard upgrade,
planet landing/harvest/launch, corporation operation, logout and reconnect.
It used the real Godot scene, menu handlers and network transport. The
acceptance harness also verifies server-confirmed messages and authoritative
state refreshes. A stale cargo snapshot in the visible port workflow was
repaired and covered by a regression test. SQL readback showed player 576 at
sector 1 with ship 9141 active and not docked/on a planet, ship cargo and
equipment updated, planet 700's ORE quantity changed, and corporation 112
membership persisted. Full evidence is in
[`V3_GODOT_LIVE_JOURNEY_ACCEPTANCE_20261116.md`](V3_GODOT_LIVE_JOURNEY_ACCEPTANCE_20261116.md).
Additional live Godot journeys now cover two-player ship destruction and
escape-pod recovery after a game-server restart (6 + 6 assertions), a persisted
planet surface allocation and launch (6 assertions), and StarPort attack and
destruction with defence retaliation, turn debit, stock destruction and a
persisted attack receipt (7 assertions). The StarPort contextual menu now
exposes the confirmed port attack command; an isolated retry test verifies
that an ambiguous retry against the same port retains its idempotency key (5
assertions). Python's actual menu workflow also destroyed a seeded StarPort
and verified its inactive record and stock settlement. These are operation
evidence, not full closure of the broader composite combat, planet, or port
requirements; no requirement status is promoted in this update.

The journeys still do not cover the full player-vs-player combat rules
(retreat, damage/resource combinations and recovery alternatives), planetary
capture/citadel progression and complete defences, or corporation-owned planet
permissions through both clients.

Python has 807 passing client tests and prior ordinary-player workflow evidence
recorded in the completion tracker and focused StarPort/feedback reports. This
turn did not execute one continuous Python-menu journey spanning the complete
combat, planet, corporation, economy, NPC, and recovery loop. The full server
JSON suite is not a substitute for that client journey.

## Confirmed remaining work, ranked

### P0 — prevents a defensible playable-parity claim

1. Complete two-player ship combat through retreat, destruction, escape pod,
   replacement/recovery, and persisted assets through Python and Godot.
2. Complete planet colonization, production ticks, harvesting, citadel upgrades,
   capture, and planetary combat through both clients with conservation and
   restart assertions.
3. Execute one continuous ordinary-player gameplay journey through each client;
   current journeys cover meaningful but separate subsets.

### P1 — approved rules with remaining verification or specification

1. Approve POP-05's daily recurrence interval, stoppage duration, and
   population-loss amount; the cycle-level unrest values are already approved.
   Daily effects remain disabled while values are unset.
2. Verify existing Ferengi wanted-list penalty and captain pursuit behavior;
   the separate 5,000-credit destruction reward is approved and runtime-tested.
   Close Orion safe-zone/protected-player, NPC-removal and concurrent-arrival
   acceptance cases.
3. Verify bank standing orders, ship sale/recovery, sector defence combat,
   Federation/ISS boundaries, production persistence, photon full effect/expiry,
   toll transwarp/mixed ownership, and all required StarPort attack client paths.
4. Verify all relevant operations are reachable and authoritative in both
   clients, beyond the passing menu regressions.

### P2 — release and optional scope

1. Full-game MySQL runtime support remains unestablished; PostgreSQL is the
   tested runtime target.
2. Live GitHub issue publication and deployment/security acceptance remain
   operational questions outside this gameplay task.
3. `STARPORT-V4-01` and `EQUITY-01` remain explicitly deferred V4 work.

## Evidence files

- Current full-suite report: `/tmp/twclone-robbery-parity-2afe35dc-full.json`
- Current sanitized protocol transcript: `/tmp/twclone-robbery-parity-2afe35dc-full.jsonl`
- Current full-suite runner log: `/tmp/twclone-robbery-parity-2afe35dc-full.log`
- Current suite summary: [`V3_GAMEPLAY_COMPLETION_TRACKER.md`](../V3_GAMEPLAY_COMPLETION_TRACKER.md)
- Approved mechanics: [`V3_GAMEPLAY_DECISIONS.md`](../V3_GAMEPLAY_DECISIONS.md)

The lawful robbery persistence defect is fixed and accepted at server and
PostgreSQL level. This does not close the broader combat, capture, client journey
or outstanding decision-register gaps. The project is not at complete V3 parity
yet; the 67-row classification above is 14 VERIFIED, 45
IMPLEMENTED_UNVERIFIED, 7 PARTIAL, 1 BLOCKED, and 0 MISSING.
