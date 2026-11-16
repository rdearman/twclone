# V3 final gameplay parity closure

Reviewed: 16 November 2026  
Starting commit: `4e60f9966b363529b20ab34bcb49b05b1d160a53`  
Final documentation commit: this report and tracker reconciliation.

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
| POP-01 | Population state and transitions | PARTIAL |
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
| BANK-03 | Monthly date boundary | IMPLEMENTED_UNVERIFIED |
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
| NPC-FER-01 | Ferengi bounty and retaliation | BLOCKED |
| NPC-ORI-01 | Orion blockade targeting | BLOCKED |
| SHIP-01 | Ship-sale locations | IMPLEMENTED_UNVERIFIED |
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
| V3-PLANET-RATE-01 | Configured planet-class production ratios | IMPLEMENTED_UNVERIFIED |
| V3-TOLL-RATE-01 | Noncanonical toll rate | IMPLEMENTED_UNVERIFIED |
| V3-PHOTON-DURATION-01 | Noncanonical photon duration | IMPLEMENTED_UNVERIFIED |
| V3-PORT-BUILD-01 | Noncanonical port build/upgrade costs | IMPLEMENTED_UNVERIFIED |
| V3-PORT-COMBAT-01 | Noncanonical port durability/defence | IMPLEMENTED_UNVERIFIED |
| CORP-XFER-01 | Corporation member asset transfers | VERIFIED |
| SYS-07 | Session invalidation on player kick | IMPLEMENTED_UNVERIFIED |
| CFG-01 | Ship-sale factor configuration | IMPLEMENTED_UNVERIFIED |
| CFG-02 | Fixed slave output multiplier | IMPLEMENTED_UNVERIFIED |
| CFG-03 | Disabled-by-default slave unrest values | IMPLEMENTED_UNVERIFIED |
| CFG-04 | Insurance configuration | IMPLEMENTED_UNVERIFIED |

**Count:** 13 VERIFIED, 44 IMPLEMENTED_UNVERIFIED, 7 PARTIAL, 3 BLOCKED,
0 MISSING; 2 additional IDs are explicitly out of V3 scope. The two V4
exclusions are described above; they are not parity failures.

The verified Exchange rows refer to the exercised PostgreSQL planet-sale,
port-purchase, settlement, accounting, idempotency, and ring-fencing paths.
`MOVE-05`, `CORP-XFER-01`, FedSpace enforcement/MSL, PTA, harvest, and StarPort
status refer to their linked focused acceptance evidence. Verification remains
operation-specific; the remaining client coverage and multi-system journeys
below are not implied by these rows.

## Executed regressions

The final server run used an actual V3 server built from current source, a
freshly rigged disposable PostgreSQL database, and the `tests.v2` JSON-RPC
runner. It completed 155 suites: **154 passed, 1 failed**. It executed 2,355
cases: **2,352 passed, 3 failed**. The four declared TLS cases were skipped
because this isolated listener was plain TCP.

The only failing suite was `suite_police_phase_b_incidents.json`. Its three
failures are the same ordinary-player journey: three successful robberies in a
lawful non-FedSpace sector leave suspicion at zero and never promote the player
to wanted status. A focused instrumented disposable-server run showed the write
helper returning success and reading suspicion `2` inside the transaction, but
the post-commit read returned `0`; the protocol acceptance then observed `0`.
The source also increments by two in this path while the published V3 police
contract specifies one. This is a confirmed P0 server defect with the exact
reproduction in that suite; its persistence/transaction root cause remains
unresolved. Diagnostic logging used to isolate it was removed from source.

Other regression results:

- Python client: **807 passed**.
- Python tests under `tests.v2`: **266 passed, 7 skipped, 227 subtests passed**.
- Godot client suite via `/home/rick/bin/gdot` (Godot **4.5.1**): **104 passed,
  0 failed, 1 skipped**. The skipped script is the live-disposable-journey
  script; live journey evidence is separately documented below.
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

The latest recorded Godot live UI journey against disposable PostgreSQL
covered registration/login, resources and ship, navigation/scan, port buy/sell,
hardware purchase, shipyard upgrade, planet landing/harvest/launch, corporation
creation/deposit, logout/reconnect, and feedback queueing. The detailed report
is [`V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`](V3_GODOT_STARPORT_ACCEPTANCE_20261114.md).
That journey did not cover player-vs-player combat, destruction/recovery,
planetary capture/citadel progression, or the entire corporation transfer
workflow through Godot. This turn reran the Godot regression suite, but did
not rerun that UI journey.

Python has 807 passing client tests and prior ordinary-player workflow evidence
recorded in the completion tracker and focused StarPort/feedback reports. This
turn did not execute one continuous Python-menu journey spanning the complete
combat, planet, corporation, economy, NPC, and recovery loop. The full server
JSON suite is not a substitute for that client journey.

## Confirmed remaining work, ranked

### P0 — prevents a defensible playable-parity claim

1. Repair lawful port-robbery incident persistence and the one-point suspicion
   contract; rerun the ordinary-player police journey and rollback checks.
2. Complete two-player ship combat through retreat, destruction, escape pod,
   replacement/recovery, and persisted assets through Python and Godot.
3. Complete planet colonization, production ticks, harvesting, citadel upgrades,
   capture, and planetary combat through both clients with conservation and
   restart assertions.
4. Execute one continuous ordinary-player gameplay journey through each client;
   current journeys cover meaningful but separate subsets.

### P1 — approved rules with remaining verification or specification

1. Decide and configure POP-05's recurrence interval, stoppage duration, and
   population-loss amount. Effects remain disabled while values are unset.
2. Approve the Ferengi bounty amount and Orion hostile/alliance targeting rules;
   current code exists but those decisions are not canonicalized.
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

- Disposable live-suite report: `/tmp/twclone-parity-report-20261116-closure.json`
- Sanitized protocol transcript: `/tmp/twclone-parity-responses-20261116-closure.jsonl`
- Runner log: `/tmp/twclone-parity-run-20261116-closure.log`
- Current suite summary: [`V3_GAMEPLAY_COMPLETION_TRACKER.md`](../V3_GAMEPLAY_COMPLETION_TRACKER.md)
- Approved mechanics: [`V3_GAMEPLAY_DECISIONS.md`](../V3_GAMEPLAY_DECISIONS.md)

No gameplay source regression is claimed fixed for the lawful robbery incident.
The tested work improves regression trustworthiness, fixes a Ferengi cache
reinitialization defect after database re-rig/restore, and makes the live suite
fixtures deterministic. This project is not at complete V3 parity yet.
