# V3 Five-Hour Gameplay Sprint

Date: 2026-11-14  
Branch: `v3-development`  
Starting commit: `c43d6bf8`  
Ending implementation checkpoint: `5fb3877e`  
Report and tracker are committed separately after that implementation checkpoint.

## Outcome

The Python client now supports additional ordinary-player trading, shipyard,
planet, corporation, mine, and recovery workflows, and several database-backed
journeys were executed against the V3 server. This is meaningful playability
progress, but **Python beta playability is still PARTIAL**. The complete
eight-stage acceptance journey was not finished, some fundamental rules remain
unverified or blocked, and the broad Python test suite is not green. No beta or
production deployment was changed.

All player journeys used ordinary accounts through Python menu/workflow code.
Direct SQL was limited to deterministic disposable fixtures and authoritative
state checks; no SysOp command substituted for a player action. Server and
database ran in disposable Docker containers. Transcripts and run outputs are
retained under `/tmp/twclone-v3-playability-sprint-20261114/` and are not
committed because client transcripts contain test credentials.

## Commits created

Starting from `c43d6bf8`, the sprint created:

- `dbf7802f` — Fix Python trading, shipyard and scan playability
- `1d3ce44f` — Fix planet access, corporation roles and hull recovery
- `cf3020ed` — Refresh Python HUD after shipyard sale
- `57b0fa99` — Read canonical cargo quantities in Python HUD
- `665a6c8f` — Deliver PvP combat hit events to defenders
- `0c87fae8` — Expose corporation leave to ordinary members
- `f2adddff` — Expose deployable mines in Python gameplay
- `0ede73ed` — Refresh Python acceptance evidence for ship recovery
- `5fb3877e` — Fix market decay and stabilize PostgreSQL acceptance fixtures

## Implemented or repaired gameplay

- Repaired Python port buy/sell request direction and cargo display, shipyard
  sale/replacement HUD refresh, ship rename/repair workflows, and Fast Scan
  response parsing.
- Added Python workflows for equipment purchases, owned-ship cargo and
  ownership transfer, planetary harvest and PTA, corporation member transfers
  and leaving, plus mine deployment/recall.
- Repaired server-side ship sale/cargo accounting and planet landing/harvest
  authorization defects encountered in the real-client journeys.
- Ensured PvP hit events are persisted and pushed to the defending player.
- Fixed market decay repository success handling so cluster state is reached
  after port-state decay. Corrected the denominator upper bound from `1` to
  `100`.
- Stabilized repeatable PostgreSQL acceptance fixtures for PTA, corporation
  transfers, ship sale, combat hazards, FedSpace MSL cleanup, and market decay.

## Player journeys actually executed

These were Python-client journeys against the running server, with PostgreSQL
queries used for state assertions:

1. Registered and logged in as two ordinary players; confirmed starting ship
   and resources.
2. Moved between connected sectors; consumed turns; plotted/followed a route;
   persisted an avoid and confirmed route refusal; reconnected after server
   restart.
3. Sold ORE, bought ORG, and resold it at another port. Credits, cargo,
   inventory, port stock, and turns reconciled. Bought Genesis torpedoes,
   shields, mines, and other equipment through client workflows.
4. Purchased, repaired, renamed, and sold an active ship; confirmed the
   replacement hull and refreshed HUD. Transferred three ORE between owned
   ships with aggregate cargo conserved; an injected database failure rolled
   back both sides.
5. Deployed and recalled fighters and Armid mines with inventory conserved;
   capacity and insufficient-resource refusals left balances unchanged.
   Beta's deployed mine destroyed Alpha's ship. Alpha became an Escape Pod in
   FedSpace, plotted a route to Stardock, bought a Merchant Cruiser, and
   continued. Database state confirmed mine consumption, pod replacement,
   hull condition, and the 41,300-credit debit.
6. Created and landed on a planet, transported colonists, allocated workers,
   harvested/transferred ORE, ORG, and EQU, and verified cargo capacity,
   stock conservation, and insufficient-stock refusal. Built a level-one
   citadel and verified completion after the scheduled construction tick.
7. Created a corporation, invited and joined a second player, deposited and
   withdrew treasury credits, transferred credits/fighters, and exercised an
   empty-fighter refusal. The second player left through the Python menu and
   PostgreSQL showed membership removed.
8. Executed a planetary trade agreement from the Python surface workflow;
   stocks, credits, turns, and receipt reconciled. A trigger-injected failure
   rolled back the settlement.
9. Two players attacked in one sector. Three accepted laser attacks each
   applied ten hull damage and persisted hit events for the defender. Display
   of the pushed event in the defender's interactive screen was not confirmed.
10. Transferred an inactive ship between players through the Python Computer
    workflow; owner changed from player 106 to 107 while its three ORE cargo
    remained. The recipient roster showed it after reconnect and server restart.

These journeys demonstrate substantial operations but do not constitute the
full requested acceptance journey. In particular, commodity reversal-mode
refusals, full ship-vs-ship retreat/destruction/recovery, planet production,
planetary combat, and all equipment combinations were not completed.

## Test results

| Test run | Result | Notes |
|---|---:|---|
| Python client full suite: `python3 -m pytest -q client/python_client/tests` | **609 passed, 190 failed** (799 total) | Existing failures span stale fixtures/schema assumptions and client workflow cases; not treated as passing. |
| Focused Python menu and ship workflow tests | **67 passed, 0 failed** | `test_menu_gating.py`, `test_ship_workflows.py` |
| Focused V3 server contract suites | **68 passed, 0 failed; 17 subtests passed** | Planetary contract, ship sale equipment catalogue, mine sweep, ship lifecycle. |
| PTA PostgreSQL acceptance suite | **32 passed, 0 failed** | Local sale, idempotency, conservation, unauthorized/no-buy/overstock refusals. |
| Corporation member-transfer PostgreSQL suite | **33 passed, 0 failed** | Persisted balances/assets and refusal cases. |
| Combat sector-hazard PostgreSQL suite | **47 passed, 0 failed** | Includes repeatable ship recovery fixture. |
| Ship-sale PostgreSQL suite | **18 passed, 0 failed** | Includes valuation and docked-hull acceptance cases. |
| FedSpace MSL real-scheduler PostgreSQL suite | **44 passed, 0 failed** | Sector-specific asset removal, non-MSL retention, and player-authored text beacon preservation. |
| Market-decay real-scheduler PostgreSQL suite | **14 passed, 0 failed** | Cluster pressure/volume decay and bounds, disabled pricing, repeated port decay. |
| Actual V3 Docker image build | **Passed** | Built from the sprint source for the isolated PostgreSQL server. |
| Python syntax, JSON parsing, `git diff --check` | **Passed** | Final checks run on the committed source/test changes. |

The JSON acceptance suites above are PostgreSQL-backed server regressions with
database fixtures; they are not counted as Python-client gameplay journeys.
The full Python suite's 190 failures remain unresolved and prevent a green
client regression claim.

## Remaining blockers ranked by ordinary-player impact

### P0 — beta playability

1. **No complete end-to-end beta journey has passed in both clients.** The
   Python run covered many operations but not every required step, and the
   complete journey was not repeated after all repairs.
2. **Combat survival lifecycle remains incomplete.** PvP damage and a mine
   destruction/recovery path were exercised, but ship-vs-ship shield/fighter
   damage rules, turn costs, retreat, destruction, and escape/recovery under
   PvP remain unverified.
3. **Planetary lifecycle remains partial.** Production is unusable on a fresh
   database because no `planet_production` rows are seeded and production
   ratios are zero. The intended production formula conflicts between current
   V3 assumptions and historical references; an approved rule is required.
   Planetary combat and full citadel/defense lifecycle are also unverified.
4. **Trading/equipment/ship lifecycle is partial.** Buy/sell, stock, capacity,
   equipment, sale, repair, cargo transfer, and replacement were exercised,
   but direction-enforcement refusal, broad rollback, all upgrade paths, and
   ship destruction outside the mine-hazard path remain unverified.
5. **Corporation workflows are partial.** Membership, treasury, credit/fighter
   transfers, leave, and persistence were exercised; permissions for officer
   removal and mine/shield transfers are not fully verified.

### P1 — original TW2002 parity gaps

- **Toll fighters:** not implemented as payment/passage/refusal; formula and
  player interaction contract are not established.
- **Photon missiles:** firing/effects are absent; duration and wire contract
  are not established.
- **StarPort construction/upgrades:** player lifecycle is absent; costs,
  timing, and eligibility are not pinned.
- **StarPort combat:** attack/durability/retaliation/destruction contract is
  absent.
- **Planetary Trade Agreement:** Python workflow and same-sector server sale
  passed acceptance; Godot access and broader multi-commodity/player cases
  remain unverified.

### P2 — verification and polish

- The broad Python suite has **190 failing tests** that need triage and repair;
  do not discard or weaken these tests.
- Fast Scan was verified for one exercised sector only. Fog-of-war disclosure
  and other visibility states still need runtime verification.
- Player-facing PvP event display is unconfirmed despite persisted/pushed
  events.
- Rankings, inspection, rename, private messaging, and other workflows that
  source audit says exist still need normal-client runtime coverage.

## Client and deployment status

- **Python:** playable for the exercised operations, but overall status is
  **PARTIAL / not beta-ready**. Several full game loops and the complete
  acceptance journey remain unproven.
- **Godot:** runtime acceptance **BLOCKED / NOT RUN** because the Godot runtime
  was unavailable in the execution environment. Source-level accessibility
  is not treated as runtime evidence.
- **Deployment:** do not expose a beta yet. Required blockers are the incomplete
  ordinary-player acceptance journey, unresolved high-impact gameplay gaps,
  and the 190 failing Python tests. Existing beta/production infrastructure
  was not contacted or modified.

## Environment and preservation

The server and PostgreSQL database were disposable Docker resources isolated
from host PostgreSQL, beta, and production. They were removed after evidence
capture. The unrelated working-tree modification
`tests.v2/regression_report.json.tmp` and untracked audit document
`docs/V3_PLAYABILITY_AUDIT_20261113.md` were preserved and not committed.
