# 30. HTTPS Gateway Command and Event Bridge

Status: V3 server contract. The PostgreSQL server implementation provides the
durable player journal, live event cursors, retention cleanup, and
`events.resume`. Gateway integration remains a separate workstream.

## 1. Boundary and authority

The gateway is a transport bridge between the browser and the existing game
server. It connects to the configured private TCP/NDJSON listener. It does not
connect to the Engine S2S listener and does not forward S2S envelopes.

The game server remains authoritative for session validity, command schemas,
permissions, rate limits, state changes, and command results. The gateway
holds the game session token and adds it as `auth.session` to each upstream
player command. It discards browser-supplied `auth`, upstream target, and S2S
fields. The server resolves the session on every command, so an expired or
revoked session fails through the ordinary server authentication path.

The bridge forwards the existing command name and `data` unchanged. Server
schema validation and handler dispatch remain the same as for a native client.
The gateway may enforce frame, connection, and command exposure limits, but
must not reproduce command permission rules or execute game logic.

## 2. Command submission and response

The browser sends one existing request envelope per WebSocket text message.
The gateway validates the envelope shape, generates a unique upstream `id`,
preserves the browser `id` for downstream correlation, and forwards the
command, `data`, and supported `meta` fields with the gateway-held session:

```json
{
  "id": "gateway-4e15c00b-...",
  "command": "move.warp",
  "auth": { "session": "<gateway-held game token>" },
  "data": { "destination_sector": 42 },
  "meta": { "idempotency_key": "client-operation-uuid" }
}
```

The gateway maps the upstream request ID to the browser request ID for the
life of the request. It relays the server response envelope and maps
`reply_to` back to the browser ID. `reply_to`, not the response `id`, is the
correlation field. A response may be `ok`, `error`, `refused`, or `partial`;
the gateway preserves the server's `status`, `type`, `data`, and `error`.
Unknown commands and invalid payloads are rejected by the server's existing
dispatcher and schema validator.

The bridge does not retry a command automatically after an upstream
disconnect. The outcome of a mutation can be unknown if the disconnect occurs
after execution but before its response arrives. The browser may retry only
with the same idempotency key, and only commands that already implement
idempotency guarantee deduplication. Each upstream connection processes
NDJSON requests in receive order. Responses and asynchronous events can
interleave on the stream; response order is not a substitute for correlation.

The server command registry is the source for client-facing command
availability. Transport-internal, administrative, debug, and S2S operations
are outside the browser command surface. Server-side permission checks remain
mandatory even when the gateway restricts commands.

## 3. Event envelope and identifiers

Command responses continue to use the existing response envelope and
`reply_to`. Events have no `reply_to`. Every replayable event carries a unique
event `id` and an opaque per-player stream cursor in `meta.cursor`:

```json
{
  "id": "7f12c743-6fb9-4a6b-b9d9-7a9dc52d0157",
  "ts": "2026-09-29T12:34:56Z",
  "type": "sector.player_entered",
  "data": { "v": 1, "player_id": 42, "sector_id": 18 },
  "meta": {
    "topic": "sector.18",
    "cursor": "opaque-player-stream-cursor",
    "replayable": true
  }
}
```

`id` uniquely identifies one player delivery and remains unchanged on replay.
The cursor is an opaque string scoped to the authenticated player's event
stream. Clients must not parse or compare cursors. The server returns the
ordered cursor as `next_cursor` from `events.resume`. Event timestamps are for
display and diagnostics, not ordering. Non-replayable live events set
`meta.replayable` to `false` and omit `meta.cursor`; they may be lost during a
disconnect and do not advance the resume cursor.

The current general subscription publisher persists each player delivery.
System notices retain their existing durable notice store and unseen-notice
delivery path; they are not duplicated into the general event journal.
Transient telemetry, heartbeats, and presence updates that bypass the
subscription publisher remain non-replayable. A replayed event keeps
the same `id`, `type`, `data`, event timestamp, and `meta.cursor` as its
original delivery.

## 4. Subscription and live delivery

The existing `subscribe.add`, `subscribe.remove`, and `subscribe.list`
commands remain authoritative. Subscriptions are player-scoped and persist
across connections. The server applies the existing exact, scoped, and
wildcard matching rules before delivering a player event. The gateway does
not implement topic matching.

After an authenticated player command, the server associates that connection
with the player and live subscription delivery can reach it. A gateway bridge
keeps one private upstream TCP/NDJSON connection for the lifetime of its
browser WebSocket. Closing either transport closes the other. Reconnecting
creates a new upstream connection and authenticates its commands with the
gateway-held token.

The same player's other active connections may also receive live events, as
they do today. A gateway must deduplicate replay/live overlap by event cursor.
Server writes to one connection must be serialized so a response and an
event cannot interleave bytes within an NDJSON frame.

## 5. Resume API and missed events

The authenticated `events.resume` command reads only the current
player's retained replayable events and does not mutate gameplay state.

Request:

```json
{
  "id": "browser-request-uuid",
  "command": "events.resume",
  "data": {
    "after_cursor": "opaque-player-stream-cursor",
    "through_cursor": "opaque-high-watermark-from-prior-page",
    "limit": 100
  }
}
```

`after_cursor` is optional. If omitted, the server starts at the current
high-water cursor and does not replay earlier history. `through_cursor` is
optional on the first page; the server captures a high-water cursor when it is
omitted. For subsequent pages, the gateway supplies the same
`high_watermark` returned by the first page. `limit` is optional, defaults to
100, and is capped at 500.

Response data:

```json
{
  "events": [
    { "id": "7f12c743-6fb9-4a6b-b9d9-7a9dc52d0157", "type": "sector.player_entered",
      "ts": "2026-09-29T12:34:56Z", "data": { "v": 1 },
      "meta": { "topic": "sector.18", "cursor": "opaque-cursor-1",
                "replayable": true } }
  ],
  "next_cursor": "opaque-cursor-1",
  "high_watermark": "opaque-cursor-9",
  "has_more": true
}
```

The server returns events strictly after `after_cursor` and no later than the
response's `high_watermark`, in ascending per-player event order. When
`through_cursor` is supplied, it bounds the page and is echoed as
`high_watermark`. The gateway repeats `events.resume` with the returned
`next_cursor` and the same `through_cursor` until `has_more` is false. The
cursor for an empty page remains the input cursor (or the captured high-water
cursor when no input cursor was supplied).

The server captures the high-water cursor after registering the authenticated
connection for live delivery. This closes the replay/live gap: events at or
below the watermark are recovered by replay, while later events arrive live.
The gateway buffers replayable live events while catching up, merges them by
cursor, removes duplicates, then begins forwarding the ordered stream. It
forwards non-replayable events live without waiting for replay.

The server retains replayable events for seven days. If the supplied cursor
predates retained history or is not valid for the player's stream,
`events.resume` returns the existing
`ERR_CURSOR_INVALID` (1306) error with `error.data.resync_required: true`.
The gateway then reports a resynchronization requirement to the browser. The
browser refreshes authoritative state using normal read commands and starts a
new stream without a cursor. It must not pretend that the missing events were
replayed.

## 6. Ordering and delivery guarantees

Replayable events receive a per-player monotonically ordered internal
sequence when appended to the durable event journal. The public cursor
encodes the player's stream identity and sequence opaquely; the server rejects
cursors from another player's stream. Events are persisted before live
delivery. Journal insertion for the same player is serialized so cursor order
is commit order. The event's originating state change and journal record
should commit in one database transaction where the existing handler
transaction permits it; otherwise an outbox step must ensure a committed
mutation is not silently omitted from the journal.

The server guarantees ordered replay by cursor. Live transport may deliver a
replayable event more than once because a live event can overlap a replay
page; event cursor deduplication is required. The server does not guarantee
exactly-once command execution after a connection failure. There is no total
ordering guarantee between command responses and asynchronous events; each
response is correlated by `reply_to`, and replayable events are ordered by
their cursor.

## 7. Server implementation and limits

The server has synchronous command dispatch, per-request session lookup,
persistent subscriptions, and player event fanout. Existing `engine_events`
are Server-to-Engine input for the Engine consumer and are not a client event
log. Durable system notices have a special unseen-notice delivery path; that
path does not provide general event replay.

Implemented server support:

1. Migration 116 adds a PostgreSQL per-player stream identity, monotonic
   sequence, UUID event ID, JSONB payload, topic, creation time, and seven-day
   expiry. A server-loop maintenance sweep removes expired prefixes and
   records the resynchronization watermark.
2. The existing subscription fanout persists each selected player delivery
   before sending it live. All connected sessions for that player receive the
   same event ID, timestamp, and cursor. Scoped sector fanout records the
   concrete `sector.{id}` topic.
3. `events.resume` is registered as an authenticated command and advertises
   its request/response schemas through the existing schema registry. Cursors
   are encoded internally as `v1.<stream UUID>.<sequence>`; clients must treat
   them as opaque. Invalid, foreign, or expired cursors return error 1306 and
   `error.data.resync_required: true`.
4. Socket frame writes are serialized, and live event frames preserve the
   existing envelope while adding stable `id`, `meta.topic`, `meta.cursor`,
   and `meta.replayable` fields.

The generic event journal is PostgreSQL-backed. It does not replace system
notice persistence or make transient event paths replayable. Event records
are committed before fanout, but the publisher is invoked after the existing
gameplay mutation path; atomically committing every gameplay mutation and
event is outside this journal foundation and requires per-command outbox
integration where strict mutation/event atomicity is required.

## 8. Regression and integration tests required

Server regression coverage currently includes:

- a persistent TCP connection accepts multiple commands and correlates every
  response with its request `id` through `reply_to`;
- missing, invalid, expired, and revoked session tokens cannot submit an
  authenticated command or resume another player's stream;
- invalid command payloads retain existing schema error behavior;
- replay pages are ordered, exclusive of `after_cursor`, bounded by the
  high-water cursor, and continue correctly via `next_cursor`;
- omitted cursor starts at the current high-water mark;
- invalid/foreign cursors return `ERR_CURSOR_INVALID` with
  `resync_required: true`;
- migration fixture validates stream identity, ordered storage, constraints,
  and pruning watermark bounds.

Additional integration coverage still required:

- exact, scoped, wildcard, and duplicate subscription matches journal and
  deliver an event only once per player;
- live/replay overlap, persistence through a server restart, and retention
  cleanup against a live PostgreSQL instance;
- concurrent command responses and pushed events are serialized as complete
  NDJSON frames;
- event publication atomicity with originating gameplay transactions where
  required.

Existing TCP and notice delivery regression tests remain applicable.

Gateway integration tests should then verify cookie-authenticated WebSocket
commands, server-side auth injection, response correlation, concurrent event
delivery, replay across reconnect, expired-cursor resynchronization, and
closure of both transports when either side disconnects.

## 9. Compatibility and rollout

This adds an authenticated command and optional event metadata. Existing
clients continue to receive live events and existing command response
envelopes. Clients that do not send `events.resume` do not request replay.
The gateway must not enable automatic reconnect recovery until the journal,
cursor-bearing event envelopes, and serialized writes are deployed together.
No Engine S2S message, authentication model, gameplay command semantics, or
database authority moves to the gateway.
