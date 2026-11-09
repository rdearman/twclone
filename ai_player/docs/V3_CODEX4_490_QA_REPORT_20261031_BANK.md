# CODEX 4 issue #490 QA report — bank-interest scenario preparation

## Requirement

`V3-ECO-02` — authoritative bank-interest runtime path and daily idempotency.

This is the next safe supporting artifact after the already-prepared landing
scenario. The release ledger still marks runtime wiring `PARTIAL`; this report
does not claim live acceptance.

## Work completed

- Added `scenarios/v3_bank_interest_runtime.json` with eligible, same-day,
  below-minimum and ledger-failure journeys.
- Kept canonical requirements separate from deterministic ordinary-player AI
  policy.
- Reused the read-only adapter harness so every command captures a sanitized
  response, event batch, authoritative state and database snapshots before and
  after, including restart readback.
- Added deterministic tests for one bounded ledger delta and rollback evidence.

The fixture does not invent the interest rate or scheduler semantics; those must
be supplied by CODEX 2/3's selected runtime path and the V3 canonical decision.
The adapter performs no provisioning, migration, direct row writes, retries or
service restart.

## Blocker and dependency

Live execution remains unavailable while `QA-INT-01` rejects migration 125 and
while the authoritative engine/cron invocation is unresolved. CODEX 2/3 should
wire one path, then run the disposable PostgreSQL/bootstrap gate. After that,
CODEX 4 can execute this manifest against the disposable universe and attach
sanitized evidence; no production infrastructure is involved.
