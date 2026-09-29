# V3 Server API and Runtime Contract Audit

Status: contract baseline and infrastructure preparation. This document
records current server behavior, the V3-facing contract, and unresolved
runtime work. It does not define federation handoff or gameplay behavior.

## Client authentication and sessions

### Contract for HTTPS, Angular, and mobile clients

- `auth.login` accepts `data.username` and `data.passwd`; it returns
  `auth.session` with `player_id` and an opaque `session_token`.
- Session tokens are bearer secrets, stored by the server in the `sessions`
  table and submitted on every authenticated command as `auth.session`. They
  are not JWTs and must not be exposed in URLs, browser storage, or logs.
- Login and refresh sessions live for 24 hours. The server checks expiry on
  each command. `auth.logout` revokes the current token.
- `auth.refresh` takes the current token in `auth.session` and atomically
  replaces it. On success, clients must immediately discard the old token and
  retain `data.session_token` from the `auth.session` response. A revoked,
  invalid, or expired token is rejected. Historical `data.session_token` and
  `meta.session_token` forms remain accepted for compatibility when they
  identify the authenticated player.
- Request authentication is per message. A prior authenticated command on a
  TCP connection does not authorize a later message that omits a valid token.

The HTTPS gateway stores the game token server-side and sends it only in
upstream `auth.session`. Its browser cookie is a separate opaque identifier;
the mapping must expire no later than the game token. `auth.login`'s game
password field is `passwd`; an HTTP API may call it `password` and translate
at the gateway boundary.

### Session implementation status

Sessions are persistent rows with a unique token and expiry. Before this
assignment, refresh ignored `auth.session`, created a second token in the
fallback path, and its repository function was a stub. Expired session lookup
also returned the row without enforcing expiry. This assignment fixes refresh
rotation and expiry enforcement without a migration. Regression coverage is
added to `tests.v2/suite_auth_session.json`.

## Player information API

`player.my_info` is an authenticated, no-argument command. Its established
response type is `player.info`; `data.player` currently contains:

- `id` and `username`
- `credits` as a decimal string
- `turns_remaining`, `sector`, `ship_id`, and `corp_id`
- `alignment` and `experience`

The response is a current summary; it does not presently include a `ships`
array or a `location` object. `id` and all other private fields derive from
the validated server session. The HTTPS `GET /api/v1/player/me` endpoint can
wrap this data, but should preserve its server values and not synthesize game
state. A future expanded profile needs a separately versioned contract and
compatibility review.

## Events, subscriptions, ordering, and reconnect

### Current delivery behavior

- Client TCP uses newline-delimited JSON. Command replies carry an opaque
  message ID and timestamp in `meta`; when a request has `meta.id`, the reply
  includes `in_reply_to`.
- Subscriptions are stored per player and survive disconnects. `subscribe.add`
  and `subscribe.remove` are idempotent. Scoped and wildcard matches are
  delivered once per player.
- Event delivery to a connected player is live fan-out. There is no client
  event cursor, sequence number, acknowledgement, or general event replay.
  Reconnect restores authentication and stored subscriptions, but clients
  must refresh authoritative state through commands after reconnect.
- Durable sector notices are queried through the notice API. They are separate
  from transient subscription events. The current login wrapper's notice-push
  condition is inconsistent with `cmd_auth_login`'s return value, so clients
  must use the notice query as the source of truth rather than depend on a
  login-time push.
- Events have no guaranteed total order across concurrent producers, and
  there is no per-player monotonic delivery sequence for deduplication. Some
  broadcast paths attach `meta.topic`; ordinary subscribed events do not
  consistently do so. The event `type` and payload are the usable current
  fields.

### V3 delivery contract for the first gateway/mobile release

Treat unsolicited events as best-effort live updates. On every reconnect:

1. Re-authenticate with a valid game session or log in again if it expired.
2. Keep the player's server-stored subscriptions; use `subscribe.list` to
   inspect them and do not assume missed events will be replayed.
3. Refresh authoritative player, navigation, market, notice, and other
   required views with their commands.
4. Use each event's type and payload for immediate UI updates. Do not infer
   order from `meta.id` or timestamps; neither is an event sequence.

Before promising replay, exactly-once handling, or ordered cross-device event
streams, coordinate a durable event-log/cursor design and schema changes with
the database owner. Before enabling the gateway WebSocket bridge, serialize
all writes per upstream socket: current concurrent send paths do not provide
a stream-level ordering lock and may interleave line fragments.

## S2S inventory and federation reuse

### Existing server-engine link

- The active path is a local server/engine pair. The server binds a loopback
  TCP listener, forks the engine, and the engine connects to that listener.
- Frames are 4-byte big-endian length-prefixed JSON. The transport applies
  HMAC-SHA256 using the local keyring. The initial health hello/ack starts the
  control thread.
- `server_s2s_dispatch` routes `s2s.health.check`,
  `s2s.broadcast.sweep`, and `s2s.command.push`. Command pushes enter the
  durable command acceptance path, which provides idempotency and an ACK.
- Engine event consumption is database-backed and tracks a last event ID and
  lag. That consumer and the command queue are useful local patterns for a
  future federation transport.
- The config parser accepts `s2s.transport` values `tcp` and `uds` and an
  `s2s.uds_path`, but the active listener and engine connector still use TCP.
  UDS is not implemented by the runtime.
- `s2s_peers`/key registry configuration is not used by the local engine
  handshake. There is no authenticated remote peer lifecycle, federation
  identity, ownership transfer, replayed peer cursor, or inter-server event
  relay. `s2s.planet.transfer`, `s2s.player.migrate`, and
  `s2s.event.relay` handlers are placeholders.

Federation could reuse framing, HMAC signing, health reporting, bounded
queues, idempotent command acceptance, and event-consumer checkpoint patterns.
It still needs peer identity, key and replay lifecycle, protocol version
negotiation, peer-specific authorization, durable relay IDs, backpressure, and
auditable ownership semantics. None of those federation contracts exist in
the local server-engine channel.

## Infrastructure issue preparation

### #179 Imperial policy foundations

FedSpace enforcement exists in several server handlers, including hard-coded
Captain Z intervention. There is no shared Imperial policy document or
reloadable thresholds, so the current behavior is not driven by the proposed
policy.

Define a versioned, validated policy document with `offence_window_secs`,
`warn_threshold`, `dispatch_threshold`, `destroy_threshold`,
`cooldown_secs`, per-action rate limits, and message templates. Parse and
validate the whole policy before activation; reject negative values, invalid
threshold ordering, zero windows, and unreasonable upper bounds with a clear
startup error. A reload should validate a complete new document before
atomically replacing the active policy and should log its version with player,
event, and correlation IDs. Tests should cover parser boundaries, deterministic
escalation tables, failed reload retaining the prior policy, successful
reload, and rate limiting. Enforcement behavior remains outside this audit.

### #182 Metrics and structured logging

Existing engine consumer metrics expose processed/quarantined counts, last
event ID, and lag, and are logged on engine ticks. S2S counters are logged
hourly. These are plain text logs; the requested configurable 30–60 second
stable metric set, server command queue depth, jobs per tick, reconnect
counters, cross-component correlation IDs, and structured JSON logs are not a
complete shared contract yet.

Define stable names and units before implementation: seconds for lag/uptime,
counts for queue depth and work, and monotonic counters for reconnects and
processed work. Use the request/envelope ID as the initial correlation value
where it exists; generate one at ingress when absent and carry it across event,
command, and broadcast logs. Metrics disabled means no timer, formatting, or
log work beyond a configuration check. Tests should parse output, verify
cadence and disabled behavior, and induce queue lag. Keep any endpoint or CLI
dump optional and local-only.

### #183 UDS/TCP smoke-test design

The acceptance harness should use one disposable database and isolated config
per run. The TCP case starts server and engine on loopback with a test HMAC
key; the UDS case uses a fresh socket path under a private temporary
directory, requires mode `0600`, and rejects an unsafe mode. Both cases run
health check and broadcast sweep with empty and populated work. Negative TCP
cases cover missing and wrong HMAC. The script must clean up child processes,
socket paths, logs, and temporary ports without dropping or mutating the
database. Switching transport must be a config change only after runtime UDS
support exists; the present runtime cannot pass the UDS leg.

### #184 Load-test preparation

Use an opt-in harness against an isolated PostgreSQL database and a running
server/engine pair. Parameters should include event rate, duration, mix, and
priority distribution. The engine consumer already processes bounded batches
(currently 200 events) and selects configured priority event types first
after a backlog threshold (currently 5,000). Record p50/p95/p99
event-to-apply latency, throughput, queue depth, and drain time, along with
host/build details. Run a moderate
baseline, a burst above baseline, and a drain/recovery phase; assert bounded
batch behavior, priority service, no starvation, and queue return to zero.
Treat the issue's proposed p95 target (under 500 ms at 100 events/s) as a
candidate measurement target, not a release SLO, until representative hardware
and workload are agreed. Keep load tests out of default CI.

## Verification and scope

The API/runtime audit uses source and protocol inspection. The session
contract changes are covered by `tests.v2/suite_auth_session.json`; their
build and integration result should be recorded with the implementation
change. No schema migration, federation handoff, gameplay policy engine, UDS
transport, metrics endpoint, or load generator is implemented here.
