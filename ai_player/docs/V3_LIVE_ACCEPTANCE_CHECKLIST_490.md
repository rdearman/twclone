# Issue #490 deterministic five-player live acceptance checklist

Status: prepared, not executed. This checklist is for an authorised isolated
QA universe only. The offline five-player fixture is regression evidence and
does not satisfy any live acceptance item below.

## Gate before launch

- Confirm the game endpoint, server build/commit, and PostgreSQL identity are
  the approved disposable QA environment. Do not use a production endpoint or
  production database, and do not reset a shared universe.
- Confirm five ordinary, pre-authorised player credentials. Store them in a
  protected file; never place passwords or session tokens in logs, reports,
  command output, or Git. Verify that each username and credential is unique.
- Run `fleet.py --config-dir ... --count 5 --dry-run` and require a
  credential-free manifest with five identities and deterministic strategy mode.
- Use separate mode-0700 runtime directories and mode-0600 configs, state
  files, logs, and reports. Set `strategy_mode` to `deterministic` and
  `register_if_missing` to `false`. Do not provision equipment or modify the
  database.
- Set a bounded duration and concurrency limit. The recommended qualification
  is five players, at most five concurrent processes, and no more than 30
  minutes. Stop the run cleanly on service loss or identity mismatch.

## Evidence required per player

Record a run ID and UTC timestamps, then retain sanitized protocol envelopes,
response types/statuses, and authoritative public reads. Capture:

- initial player ID, ship ID, sector, credits, cargo, and turns;
- source and destination ports, commodity, quantity, public quotes and route;
- player and ship reads immediately before and after `trade.buy`;
- the complete buy receipt, charged total including fees, cargo delta and turn
  delta;
- each confirmed movement result and current sector;
- player and ship reads immediately before and after `trade.sell`;
- the complete sell receipt, net proceeds including fees, cargo delta, credit
  delta and turn delta;
- final strategy plan, completed goals, refusals, timeouts, reconnects,
  unresolved mutation markers and process exit status.

Acceptance requires each successful receipt and readback to agree on balance,
cargo, location, ownership and turn consumption. A cohort aggregate cannot
replace per-player evidence. Reconnect once after the final read and compare a
fresh login's public state to the recorded final snapshot.

## Failure handling

Classify ordinary refusals separately from confirmed defects. If a trade or
warp times out, disconnects, or has an ambiguous response, stop that player's
mutations. Do not retry with a new idempotency key. Resolve the original
request using its idempotency key, receipt and authoritative readbacks; retain
the `uncertain_trade` or `uncertain_warp` marker until resolved. Stop the whole
cohort on unexpected account/universe identity, database mismatch, or evidence
of cross-player state contamination.

Write a sanitized machine-readable report for every player, including expected
versus observed values and replay steps for confirmed inconsistencies. Mark
missing evidence, unavailable services, incomplete goals and interrupted runs
as unqualified rather than successful acceptance.
