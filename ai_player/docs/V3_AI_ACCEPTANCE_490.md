# Issue #490: isolated AI acceptance procedure

Status: prepared; not executed. The previously reported one-player journey
earned 13 credits, but its transcript has not been recovered. The 13-credit
fixture in `test_trading.py` checks local decisions and is not that live run.

## Before a run

Confirm the isolated server and database identity and authorization for each
ordinary AI account. Do not provision equipment, alter a database, or target a
production universe. Record the server commit/build, endpoint, UTC start time,
run identifier, account identifiers, configuration and observed initial public
state. Keep credentials and session tokens in private files, and redact them
from transcripts. Use separate configs, state files, logs and reports per
account. Set `strategy_mode` to `deterministic` to avoid inference dependencies.

First qualify one player using fresh public port information and quotes. Record
the selected commodity, quantity, source and destination, route, expected
receipt amounts including fees, credits, cargo, location and turns. Let the
controller issue its own buy, adjacent navigation and sell commands. Capture
sanitized requests, responses and authoritative reads before and after each
trade and after movement. Compare receipt totals with credit and cargo changes;
record actual profit and turn consumption. Reconnect and compare the persisted
state. Ordinary market or route refusals are not defects by themselves.

Only after that qualification, and with approval for the cohort, use the
bounded runner described in `FLEET_QA.md` with five distinct authorized players.
Retain each player's evidence and the runner summary. Verify independent
identities and state, per-player goal progress, profit, refusals, timeouts,
reconnects and stalls; an aggregate alone does not establish acceptance.
Document missing capabilities and incomplete or failed goals explicitly.

## Stop conditions

Stop on unexpected identity or universe, service loss, or an ambiguous mutation.
A timeout does not establish that a trade failed: a refresh may race with a late
commit. Do not issue a replacement mutation until its outcome has been resolved
using authoritative transaction evidence. The controller persists an
`uncertain_trade` marker before sending a trade and pauses gameplay if no
recognized receipt or refusal resolves it, including after restart. Once the operator has established the outcome, preserve the
evidence and remove that marker from the private player state before resuming.
Keep transcripts and expected/observed values for reproducible defects, and
classify incomplete evidence separately from a confirmed failure.
