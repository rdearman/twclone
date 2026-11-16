# Lawful port robbery persistence acceptance

Date: 16 November 2026  
Source baseline: `2afe35dc`; implementation `068f2550`; acceptance tests
`927dafdf`.  
Environment: fresh disposable PostgreSQL and V3 server containers; ordinary
player registration and authentication; no production or beta service used.

## Defect and correction

The successful `port.rob` response was emitted even though PostgreSQL had
aborted the transaction. The repository inserted integer `0`/`1` into the
PostgreSQL boolean `player_last_rob.was_success` column. PostgreSQL then
rejected subsequent statements in that transaction. A second independent SQL
failure came from unqualified `suspicion`, `bust_count`, and `last_bust_at`
references in an `ON CONFLICT` update. Wanted promotion and incident resolution
also supplied integer values to boolean columns. Finally, the robbery handler
ignored failure from the success-marker write and could return no response on
an aborted transaction.

The correction uses explicit PostgreSQL boolean literals and a qualified
conflict update; records one lawful crime through the common incident path
inside the robbery transaction; checks the success-marker write; and returns a
database error when the transaction cannot commit. The common incident path
keeps the existing lawless-sector and FedSpace exclusions and performs wanted
promotion at the established suspicion threshold.

## Executed acceptance

`python3 tests.v2/run_suites_all.py --rig --suite tests.v2/suite_police_phase_b_incidents.json`

Passed against the disposable server. The sequence used an ordinary player to
rob a lawful port three times, with deterministic fixture time advancement so
the port cooldown did not collapse separate offences. It verified:

- each successful robbery adds exactly one suspicion point;
- the third offence promotes wanted level and resets suspicion per contract;
- `police.status` returns the promoted status after logout and reauthentication;
- the database row has the same values as the protocol response;
- surrender clears wanted/suspicion using the published successful response;
- the Orion lawless-sector action does not create a police incident.

Restart recovery was separately run with:

`python3 tests.v2/run_police_robbery_restart_acceptance.py --seed tests.v2/acceptance/suite_police_phase_b_restart_seed.json --readback tests.v2/acceptance/suite_police_phase_b_restart_readback.json --server-container twclone-qa-robbery-parity-game-2afe35dc --host 127.0.0.1 --port 55492 --evidence-dir /tmp/twclone-robbery-parity-2afe35dc/restart-evidence`

The seed phase created three real successful offences, asserted suspicion `0`
and wanted level `1` through both protocol and SQL, then logged out. The runner
restarted the serving game container (not only PostgreSQL). The readback phase
reauthenticated the same ordinary account and confirmed the same protocol and
database state. The server accepted further requests after restart.

## Regression results

- Complete live server run: 155 suites, **2,361 passed, 0 failed, 4 skipped**.
  The four skipped cases require TLS; the disposable listener was plain TCP.
- Python client: **807 passed**.
- Python `tests.v2` suite: **266 passed, 7 skipped, 227 subtests passed**.
- Godot 4.5.1 client regressions: **104 passed, 0 failed, 1 skipped**.
- `git diff --check`: passed.

The full suite report, sanitized protocol transcript, and server log are
retained at `/tmp/twclone-robbery-parity-2afe35dc-full.json`,
`/tmp/twclone-robbery-parity-2afe35dc-full.jsonl`, and
`/tmp/twclone-robbery-parity-2afe35dc-full.log`. Restart transcripts and SQL
assertion outputs are in the restart evidence directory above.

## Scope and remaining evidence

This closes the lawful port robbery persistence defect. It is server protocol
and PostgreSQL acceptance, not proof of complete police UX in both graphical
clients. The 67-requirement parity matrix therefore remains at 13 VERIFIED,
44 IMPLEMENTED_UNVERIFIED, 7 PARTIAL, 3 BLOCKED, and 0 MISSING; robbery is a
cross-cutting defect fix and not a separate decision-register requirement.
Combat/recovery, planetary capture/citadel progression, and continuous
ordinary-player client journeys remain priority acceptance work.
