# V3 gameplay and acceptance status

Reviewed: 16 November 2026. This is a concise current summary; detailed test
logs and transactions belong in the linked acceptance reports. Do not infer
that an entire subsystem is verified from a focused scenario.

The current requirement-by-requirement reconciliation is
[`V3_FINAL_GAMEPLAY_PARITY_CLOSURE_20261116.md`](testing/V3_FINAL_GAMEPLAY_PARITY_CLOSURE_20261116.md).
It supersedes stale implementation-status remarks in older focused reports,
while preserving their historical test evidence.

## Release-level regression results

The latest recorded gates are:

| Area | Result | Scope |
|---|---:|---|
| Server protocol regression | 2,361 passed, 0 failed, 4 skipped | 155 suites; disposable PostgreSQL; TLS cases skipped on plain TCP |
| Python client | 807 passed | `python3 -m pytest -q client/python_client/tests` |
| `tests.v2` | 266 passed, 7 skipped, 227 subtests | `python3 -m pytest -q tests.v2/test_*.py` |
| Godot 4.5.1 | 104 passed, 0 failed, 1 skipped | `/home/rick/bin/gdot` client regression run |
| Godot command menu | 206 checks passed | Menu regression |
| Planet response | 115 checks passed | Response/identity regression |

These are the latest executed counts, not a claim that every gameplay operation
or deployment has been verified.

The server run skipped the four TLS smoke cases because the disposable game
listener was plain TCP. The earlier three robbery assertions now pass. A
focused live test verifies one suspicion point per successful lawful robbery,
wanted promotion at the contract threshold, persistence after reauthentication,
and persistence after restarting the serving game process. See
[`V3_POLICE_ROBBERY_ACCEPTANCE_20261116.md`](testing/V3_POLICE_ROBBERY_ACCEPTANCE_20261116.md).

## Player-facing acceptance

The live Godot journey against disposable PostgreSQL exercised registration
and authentication, navigation and scanning, port trading, hardware purchase,
shipyard upgrade, planet landing/harvest/launch, corporation deposit,
logout/reconnect and queue-only feedback submission. Python journeys exercised
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
