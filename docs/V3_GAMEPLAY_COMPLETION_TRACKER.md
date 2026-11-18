# V3 gameplay and acceptance status

Reviewed: 18 November 2026. This is a concise current summary; detailed test
logs and transactions belong in the linked acceptance reports. Do not infer
that an entire subsystem is verified from a focused scenario.

**Parity refresh — 18 November 2026:** 15 VERIFIED, 44
IMPLEMENTED_UNVERIFIED, 7 PARTIAL, 1 BLOCKED, 0 MISSING. `BANK-03` is now
VERIFIED: a day-31 monthly standing order settled once through the registered
database scheduler, retained day 31 for its next recurrence, and remained
visible with its original day after a serving-server restart. This acceptance
also fixed scheduler-time propagation and PostgreSQL `smallint` JSON typing.
The Orion targeting
decision remains PARTIAL: its 60-case PostgreSQL run passed, while safe-zone,
protected-player, NPC-removal and genuine concurrent-arrival cases remain open.
The approved ruleset 1.2 cycle unrest and Ferengi destruction reward passed
focused PostgreSQL acceptance. `NPC-FER-01` is now IMPLEMENTED_UNVERIFIED because
the existing penalty and pursuit clauses still lack clause-specific evidence.
POP-05's daily recurrence, stoppage-duration and population-loss numeric policy
values remain the sole rules blocker. Exact evidence and limits are in the
clause matrix and [`V3_RULESET_1_2.md`](V3_RULESET_1_2.md).

The current requirement-by-requirement reconciliation is
[`V3_FINAL_GAMEPLAY_PARITY_CLOSURE_20261116.md`](testing/V3_FINAL_GAMEPLAY_PARITY_CLOSURE_20261116.md).
It supersedes stale implementation-status remarks in older focused reports,
while preserving their historical test evidence.

## Release-level regression results

The latest recorded gates are:

| Area | Result | Scope |
|---|---:|---|
| Server protocol regression | 2,625 passed, 0 failed, 0 skipped | All 166 suites; disposable PostgreSQL; TLS suite separately configured and run |
| Python client | 809 passed | `python3 -m pytest -q tests` in `client/python_client` |
| `tests.v2` | 274 passed, 7 skipped, 227 subtests passed | `python3 -m pytest -q tests.v2` |
| Godot 4.5.1 | 106 passed, 0 failed, 6 skipped | 112 scripts discovered by `suite_client_regressions.py`; skipped scripts require live acceptance mode |
| Godot command menu | 206 checks passed | Menu regression |
| Planet response | 117 checks passed | Response, planet and sector identity regression |

The full server regression reported **2,621 passed, 0 failed** across 165
suites; the aggregate runner skipped its four TLS cases because TLS listener
configuration is enabled only for explicitly selected TLS suites. The separate
TLS-enabled run passed all 4 cases, accounting for **166/166 suites and 2,625
passed cases with zero failures**. A
focused live test verifies one suspicion point per successful lawful robbery,
wanted promotion at the contract threshold, persistence after reauthentication,
and persistence after restarting the serving game process. See
[`V3_POLICE_ROBBERY_ACCEPTANCE_20261116.md`](testing/V3_POLICE_ROBBERY_ACCEPTANCE_20261116.md).

The same run passed the slave production-cycle unrest suites (20/20), POP-05
daily stoppage/loss/recovery and rollback suites (30/30 across a serving-server
restart), Ferengi destruction reward and restart suites (41/41), Orion
arrival/restart suites (60/60), and deterministic black-market restock
acceptance (34/34). POP-05 concurrent scheduler invocation remains untested. The
30-day actual-scheduler simulation passed 64/64 invariant checks; it does not
establish a quantitative economy balance result.

## Player-facing acceptance

The 16 November live Godot journey against disposable PostgreSQL exercised
authentication, navigation and scanning, port trading, hardware purchase,
shipyard upgrade, planet landing/harvest/launch, a corporation operation, and
logout/reconnect. SQL confirmed persisted player, ship, planet and corporation
state. It fixed and regression-tested stale cargo in an open port workflow.
See [`testing/V3_GODOT_LIVE_JOURNEY_ACCEPTANCE_20261116.md`](testing/V3_GODOT_LIVE_JOURNEY_ACCEPTANCE_20261116.md).
Earlier Godot journeys exercised registration and feedback queueing. Python journeys exercised
ordinary-player workflows, including StarPort construction replay after a
server restart. Refer to
[`testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md`](testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md),
[`testing/V3_STARPORT_ACCEPTANCE_20261114.md`](testing/V3_STARPORT_ACCEPTANCE_20261114.md),
and [`testing/V3_PLAYER_FEEDBACK_ACCEPTANCE_20261114.md`](testing/V3_PLAYER_FEEDBACK_ACCEPTANCE_20261114.md)
for exact evidence and limits.

That journey is a verified subset, not a complete continuous V3 gameplay loop.
Combat/destruction/recovery, planetary capture and citadel progression, and the
full corporation transfer workflow still lack live UI evidence through both
clients. The latest parity-closure report lists the journey gaps separately.
The robbery fix is verified at the server protocol/database level but does not
by itself close any of the 67 decision-register requirements.

## Mechanics with focused acceptance evidence

### Additional clause-level acceptance from the `b3fcb7d9` work batch

- **Transwarp toll arrival:** on the disposable PostgreSQL V3 server, normal
  toll quotes were reused for transwarp arrival. Acceptance covered offer-before-
  movement, one-time payment/fuel/turn settlement, accepted-request replay,
  insufficient-credit rollback and funded retry, self/same-corporation
  exemptions, and refusal combat with one-time damage. Test IDs are listed in
  [`V3_CLAUSE_ACCEPTANCE_MATRIX_20261116.md`](testing/V3_CLAUSE_ACCEPTANCE_MATRIX_20261116.md).
- **Planet population transport:** `planet.colonists.set` now requires the
  active ship landed at the target and applies ship cargo and planet population
  changes atomically through the authoritative cargo layer. A PostgreSQL suite
  confirms remote refusal, pickup/drop-off conservation, and full rollback when
  a drop-off would mix colonists with slave population. This does not verify
  unrelated acquisition policy in `POP-02`. A separate same-database
  test stopped and restarted the serving V3 server between suites, then
  authenticated the ordinary player, read the persisted population and
  completed another conserving transfer.
- **Unpopulated planet acquisition:** ordinary-player capture, database-failure
  rollback, conservation of empty population/allocation state, commodity stock
  and citadel treasury, player↔corporation ownership transfer, former-owner
  denial, and authenticated planet readback after a serving-server restart
  passed in disposable PostgreSQL. The prepare and restart verification suites
  executed **57 cases, 0 failed**, including a stale competing-claim conflict
  injected at the capture compare-and-set boundary. The failed capture rolled
  back the conflicting ownership update and fighter debit. Focused client
  response checks passed for
  Python (**9 tests**) and Godot (**117 checks**). This closes the `POP-01`
  acquisition transition clause; the separate `POP-02` purchase and capture
  disposition clauses remain independently classified.
- **Production numeric rules:** the disposable database confirmed all 21
  built-in class/commodity rate rows for the seven approved noncanonical V3
  defaults. This verifies migrated configuration values; it does not verify
  every scheduled production tick, persisted remainder, or class-rate override.
- Focused result: four PostgreSQL suites **136 passed, 0 failed**; production
  rate migration suite **1 passed, 21 assertions**; the expanded colonist
  transfer and rate retest **27 passed**, including quantity/hold boundaries;
  server restart population suites **20 passed**; Python client plus two
  focused server contracts **866 passed**; Godot 4.5.1 toll workflow **13
  checks passed**; existing slave-delivery/harvest/fighter/emancipation suite
  **49 passed**; engine regression script passed. These are focused gates, not
  the full server regression suite.
- Defects fixed in this batch: transwarp did not request/settle toll offers;
  colonist transfer lacked a non-Terra landed-ship check and split population
  mutation from cargo mutation; the old path wrote only legacy ship cargo and
  left authoritative `ship_cargo` stale.

- **StarPorts:** construction retry after lost reply and server restart,
  scheduled deliveries across serving-process restarts, selected rollback
  boundaries, concurrent upgrades, upgrade/attack serialization, and
  post-restart requests passed for the documented cases. This is not exhaustive
  fault-injection coverage. See the two StarPort reports above and the
  [release-candidate audit](testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md).
- **Photon missiles:** focused disposable PostgreSQL firing, eligibility,
  ammunition, retry and expiry cases are recorded in
  [`testing/V3_PHOTON_ACCEPTANCE_20261114.md`](testing/V3_PHOTON_ACCEPTANCE_20261114.md).
- **Toll fighters:** focused payment, refusal, insufficient funds and retry
  cases are recorded in
  [`testing/V3_TOLL_FIGHTER_ACCEPTANCE_20261114.md`](testing/V3_TOLL_FIGHTER_ACCEPTANCE_20261114.md).
- **Planet production:** ratio/default and production acceptance is recorded
  in [`testing/V3_PLANET_PRODUCTION_RATE_ACCEPTANCE_20261114.md`](testing/V3_PLANET_PRODUCTION_RATE_ACCEPTANCE_20261114.md).
- **Player feedback:** PostgreSQL queueing, ownership, redaction and mock
  publisher paths are recorded in
  [`testing/V3_PLAYER_FEEDBACK_ACCEPTANCE_20261114.md`](testing/V3_PLAYER_FEEDBACK_ACCEPTANCE_20261114.md).
  Live public GitHub publication has not been tested.

## Remaining limits

- The release-candidate audit does not clear external deployment. Credential
  rotation, host/database exposure ownership, designated QA authentication and
  deployment-specific checks remain blockers.
- Live GitHub issue publication is unverified; local queue-only reporting is
  available and tested.
- Focused subsystem evidence is not proof of every original TW2002 edge case.
  Consult [`V3_GAMEPLAY_PARITY.md`](V3_GAMEPLAY_PARITY.md) and the approved
  [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md) for scope and open
  parity gaps.
- Full V3 server runtime on MySQL is not supported or established; PostgreSQL
  is the verified runtime target.

## Evidence policy

Use `VERIFIED` only for the operation and conditions recorded in an executed
acceptance report. Keep implementation, test-fixture, focused runtime and
deployment claims separate. The report date and commit identify the source
baseline; newer source changes require relevant tests to be rerun.
