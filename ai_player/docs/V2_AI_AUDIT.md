# v2 AI Player Audit

## Client-side improvements

The AI player uses the existing server command interface. Its navigation and
trading decisions were tightened without changing server APIs or protocol
contracts:

- `move.pathfind` requests include only the documented sector ID fields.
- Pathfinding replies remain usable after the request is removed from pending
  state, and confirmed warps advance the cached route.
- Automatic trading buys only commodities with a known positive gross margin
  at a surveyed buyer. The bot no longer falls back to an arbitrary commodity
  or an unquoted purchase.
- Cached quotes carry their receipt time. A quote older than the configurable
  `quote_max_age_seconds` window (15 minutes by default) is refreshed before a
  trade and excluded from purchase, sale, and cargo-destination choices.
- When cargo needs a destination, the bot selects a profitable surveyed buyer
  and prefers the shortest known route among equally profitable options.
- Navigation supports `balanced`, `explorer`, `trader`, and `cautious`
  behavior profiles. Targets are selected from known adjacent sectors with
  deterministic tie-breaking; trader favors known ports, explorer favors new
  sectors, and cautious favors previously explored sectors.
- Stage-specific epsilon settings now control bandit exploration rates.
- Routine action selection no longer starts attacks or deploys defenses.
  Explicit attack goals reject FedSpace (sectors 1–10), and visible ship IDs
  are read from the server's `ship_id` field while excluding the bot's own ship.

Regression coverage is in `ai_player/tests/` for navigation, trading,
strategy profiles, combat safeguards, and exploration-rate settings.

## Integration and long-running behavior

The runner now permits only one tracked command at a time, so a new decision
does not use state from before an unacknowledged purchase, warp, or other
action. `command_response_timeout` defaults to 30 seconds. On timeout, the
runner clears the pending action and plan, then fetches player and ship state
again before choosing another action. It does not replay a timed-out mutation,
because the server may have processed it even if the reply was lost.

Refused warps blacklist the rejected hop, discard cached path steps, and drop
the stale `goto` goal. Refused pathfind requests discard the unreachable
`goto` goal while leaving later plan steps available.

Saved plans now advance goals already satisfied by refreshed state, including
sector arrival and completed port surveys. A temporary lack of a ready command
no longer erases the full plan. State writes use a temporary file and atomic
replacement, so an interrupted write cannot leave a truncated state file.
Bandit contexts group equivalent ports instead of creating a separate learning
bucket for every port ID, and credit buckets use the authoritative player
credit value when available.

The configured local server endpoint was not listening during this run, so
live gameplay integration could not be exercised. The checked-in AI tests use
protocol-shaped responses to cover these recovery paths.

## Remaining dependencies

- Issue #439 special encounters: engine/server must generate and deliver
  encounters, with a player-facing response interface before the AI can react.
- Event subscriptions need a server-owned compatibility fix before the AI can
  reliably subscribe to `sector.*` and `combat.*`: the checked-in
  `subscribe.add` schema requires `event_type`, while its handler and other
  clients send/read `topic`. The AI processes delivered events, but does not
  send a request that the current schema/handler pair will reject.

## Issue #439: random NPC special events

Issue [#439](https://github.com/rdearman/twclone/issues/439) calls for random
encounters such as the Space Hag, Gold Transport, derelict ship, mysterious
trader, and pirate trap.

The existing engine event contract describes NPC spawn, movement, attacks, and
defense deployment, but it does not define generation or delivery of those
random encounters. The engine documentation describes NPC patrols and an
`encounter.resolve` stub. The missing probability/timing triggers and the
encounter outcomes and rewards therefore require engine/server work.

The AI client also has no documented player-facing encounter payload or
existing decision command for reacting to those encounters. Once the server
work defines how an encounter is presented and acted on, the AI can consume
that interface. Until then, simulating the encounter in the bot would create a
separate, non-authoritative gameplay system.

## Audit limitations

GitHub was unreachable during this audit, so the open/closed state and full
milestone issue list could not be refreshed. The repository documents #439 as
the relevant special-events gap; confirm the issue list when GitHub access is
available.

## v3 client capability cross-check

The latest local Godot client change (`f9b68ce4`, “Improve Godot route and
player controls”) adds trade-route recommendation controls and player-facing
controls for persisted autopilot routes. The documented interfaces include
`player.computer.recommend_routes` fields for two-way trading and route limits,
plus `move.autopilot.status` and `move.autopilot.control` for route recovery
and execution mode. These are existing client/server capabilities the AI can
consume; they do not require protocol or engine changes.

The AI now polls advertised route recommendations on a configurable interval,
uses their path costs alongside locally surveyed destination quotes, and
prefers expected cargo profit per warp. Long `goto` plans use advertised
`move.autopilot.start` routes when available, reconcile persisted status after
login, resume a matching stopped route, and stop a persisted route after a
refused hop. Older servers keep the `move.pathfind` fallback. Server quotes
and trades remain authoritative.

The stream loop now queues partial writes, drains available frames, bounds
frame sizes, distinguishes events from command replies, ignores late replies,
and clears session-scoped state on reconnect. Delivered events are
deduplicated and retained; relevant sector/combat events schedule authoritative
refreshes. Saved metrics track command outcomes/timeouts, realized trade profit,
planned route hops versus completed warps, reconnects, and received/duplicate
events. Offline regression tests exercise these paths; no live server was
available for an end-to-end simulation.
