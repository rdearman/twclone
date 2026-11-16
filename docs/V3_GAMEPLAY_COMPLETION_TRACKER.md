# V3 gameplay and acceptance status

Reviewed: 15 November 2026. This is a concise current summary; detailed test
logs and transactions belong in the linked acceptance reports. Do not infer
that an entire subsystem is verified from a focused scenario.

## Release-level regression results

The latest recorded gates are:

| Area | Result | Scope |
|---|---:|---|
| Server protocol regression | 2,341 passed, 0 failed, 4 skipped | 155 suites; disposable PostgreSQL acceptance environment |
| Python client | 807 passed | Current reported run |
| `tests.v2` | 262 passed, 7 skipped, 227 subtests | Regression harness |
| Godot 4.5.2 | 104 passed, 0 failed, 1 skipped | Godot client suite |
| Godot command menu | 206 checks passed | Menu regression |
| Planet response | 115 checks passed | Response/identity regression |

These are the latest reported counts, not a claim that every gameplay operation
or deployment has been verified.

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
