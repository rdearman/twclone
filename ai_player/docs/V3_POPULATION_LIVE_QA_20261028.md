# V3 AI Population Live QA — 2026-10-28

## Environment and scope

- Disposable V3 game server: `127.0.0.1:12483`.
- Configured inference: local Ollama, `qwen3:8b`.
- The AI used public `auth.register` / `auth.login` / `auth.session` and ordinary player commands. It did not access PostgreSQL.
- Four existing ordinary QA players were joined by six ordinary QA players registered through the public protocol. No NPC flags or account records were changed directly.
- Test activity was limited to the isolated QA universe. No server or database service was restarted.

## Cohort evidence

The first 10-player launch was interrupted before it wrote its cohort summary. All ten player logs show successful `auth.session` responses. The same ten private runtime configurations and saved states were resumed for a bounded 180-second run; no accounts were registered again intentionally and no new accounts were created for the resume.

The resumed run exited normally (`exit_code: 0` for all ten players; `interrupted: false`). Because it resumed persisted states, the summary metrics are cumulative across the incomplete first launch and the 180-second resumed run:

| Metric | Cumulative result |
| --- | ---: |
| Public gameplay/controller commands sent | 512 |
| Successful commands | 140 |
| Refused commands | 5 |
| Command timeouts | 19 |
| Completed warps | 5 |
| Reconnects | 6 |
| Trade sales | 0 |
| Realized trade profit | 0 credits |
| Newly registered accounts | 6 |

At least eight successful Ollama strategy plans were recorded during the resumed window. The plans used distinct visible navigation targets, for example explorer `goto: 2, goto: 9`, opportunist `goto: 5, goto: 10`, and cautious/trader `goto: 5, goto: 10`. Ten-player planning throughput was not sufficient for reliable progression: two players did not record an Ollama plan during that window, and the cohort completed no sale. The 20-player scaling gate therefore failed and was not attempted.

Four `auth.register` retries returned the expected `Username already exists` refusal after process restart; authentication then succeeded. No duplicate accounts were created. A follow-up controller fix persists the registration-attempt flag so resumed players proceed to login instead of repeating registration.

The trade loop did not recur as repeated network quote requests. One expected market-direction refusal from the preceding four-player run (`Port is not buying this commodity right now`) exposed an AI issue: a positive quote was incorrectly treated as proof that a port would buy cargo purchased there. The planner now remembers cargo origin through authoritative ship refresh/reconnect and will not choose the purchase port as its sale destination. This fix has offline regression coverage; the ten-player run did not complete another trade cycle to verify it live.

## Controller changes and regressions

- Reuse cached command schemas across reconnects to reduce repeated public metadata requests.
- Persist one-time registration attempts so a resumed cohort does not repeat `auth.register` after an uncertain reply.
- Preserve cargo origin metadata across reconnect and skip same-origin sale destinations.
- Make observed planet IDs part of `planet.land` and `planet.info` requests; defer when sector data does not expose an ID.
- Complete `planet: land` only on a successful `planet.land` response, and `planet: info` only after `planet.info`.

The schema-cache and persistent registration retry changes were added after this live run and are covered by offline regressions; neither has yet been re-qualified against the server.

`pytest -q ai_player/tests`: **85 passed**. `py_compile` and `git diff --check` passed. The suite includes the 163-tick empty-port quote regression, trading direction/origin checks, independent cohort configuration, secret redaction, and the new planet-goal completion checks. A later regression verifies that timeout counters can be attributed to public command names without recording request data.

The 19 timeouts in the resumed cohort predate per-command timeout metrics, so the historical evidence does not identify which command types timed out. Future runs now retain aggregate and per-command timeout counts in the player's AI metrics. The live QA server and its PostgreSQL service were unavailable during the follow-up; these metrics have not yet been collected in a resumed live run.

## Remaining acceptance work

- Improve multi-player Ollama throughput and investigate the 19 command timeouts before attempting 20 players.
- Complete an autonomous sale and verify realized credits/cargo/turn reconciliation in live QA.
- Run live checks for schedule transitions and reconnect during an active trade objective; current evidence confirms reconnects occurred but does not establish active-objective recovery without duplicate mutations.
- Implement and qualify equipment purchase, ship maintenance, planetary trading/colonist transport, Genesis creation, fighter handling, and defensive behavior. These remain unimplemented or unqualified; no unsupported mechanics were added.
- The public `ship.status` contract documented for the target does not expose current hull, while `ship.repair` depends on damage state. The AI therefore cannot make a grounded repair decision from its current public ship readback; this server/API dependency is tracked in GitHub issue #492.
