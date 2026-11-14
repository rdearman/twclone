# V3 Photon Firing Acceptance — 2026-11-14

## Scope and environment

Migration 183 and the current V3 server ran against disposable PostgreSQL 16
container `twclone-qa-prod182-20261114`, database `twclone_rates_game`, using
loopback port 55489. The existing beta containers were not used. A newly
registered ordinary player (107) used the Python client menu dispatcher for
the firing workflow. A deterministic database fixture changed that player's
ship to a Missile Frigate and set its photon stock to two; gameplay firing
itself was performed through `DEPLOYMENT_MAIN` option `x`.

## Executed results

- Migration `183_v3_photon_effects.sql` applied successfully.
- Menu firing at adjacent sector 2 succeeded with `combat.photon_fired_v1`,
  configured duration 600 seconds and one photon remaining.
- Replaying the same request ID returned success with
  `idempotent_retry=true`; the ship still had one photon and one request row
  existed.
- Firing at nonadjacent sector 2500 was refused. The ship retained one photon
  and the request table was unchanged.
- Two concurrent copies of a new request ID returned one fresh success and
  one idempotent replay. The ship had zero photons and exactly two successful
  request rows, proving one debit for each distinct successful request.
- After stopping and restarting the actual server, PostgreSQL still showed
  zero photons, two request records and an unexpired sector 2 effect.
- An initial live run exposed a server query/schema mismatch (`is_destroyed`
  versus the V3 `destroyed` column). The handler was corrected and all listed
  live cases were rerun successfully.

## Automated verification

- ASan/UBSan build: `make -C bin -j2` passed.
- Focused regression: `python3 -m pytest -q tests.v2/test_v3_photon_contract.py tests.v2/test_v3_planet_production_rates.py tests.v2/test_planetary_server_contract.py` — **60 passed**.
- Menu JSON parse and `git diff --check` passed.

## Status

Firing, eligibility, adjacency rejection, ammunition accounting, request
replay/concurrency and persisted expiry are runtime accepted. The mechanic
remains **IMPLEMENTED_UNVERIFIED** overall pending live verification of
shield-protected targets, sector-defense suppression during arrival,
citadel-computer suppression, effect expiry behavior and rollback on an
injected transaction failure. The duration is the configurable, noncanonical
600-second `V3-PHOTON-DURATION-01` default; no TWGS 3.13 parity claim is made.
