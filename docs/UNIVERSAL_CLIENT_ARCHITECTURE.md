# Universal Client Architecture

Status: client state and transport architecture, 2026-09-29. This document
records the cross-client state boundary and readiness findings for desktop,
terminal, browser, and future mobile clients. The game protocol and server
authority remain unchanged.

## Goal and design boundary

All clients should use the same authoritative game commands, responses, and
events. A client may adapt presentation and transport to its platform, but it
must not independently decide game rules or invent authoritative state. The
existing game server remains authoritative for identity, permissions, player
and ship state, navigation, sector contents, economy, and command outcomes.

The HTTPS gateway is a transport and web security boundary. It translates
browser HTTP/WebSocket traffic to the existing server interface; it does not
become another gameplay service. The shared `assets/` catalogue is the
client-neutral source for artwork IDs and paths. See
[`UNIVERSAL_GAME_ASSETS.md`](UNIVERSAL_GAME_ASSETS.md) and the gateway
architecture/API documents for their specific decisions.

## Current client landscape

| Client | Current role and capabilities | Readiness limits |
| --- | --- | --- |
| Python terminal | Active menu-driven TCP/NDJSON client. Supports login, commands, navigation, settings/HUD, and bounded unsolicited-event queue/log. | Uses individual RPCs and has no atomic snapshot or replay cursor. Disconnect ends its session loop; no automatic resume. |
| Godot desktop | Active TCP/NDJSON client with correlated requests, timeout handling, bounded event queue, normalized player/ship/sector state, per-domain freshness, and shared catalogue lookup. | The combined state is sequentially refreshed, not atomic. Reconnect requires user action and re-authentication; durable event replay is unavailable. Port detail workflows hold separate UI data rather than a server-backed client-state domain. |
| Angular web | Authenticated Angular entry point under `client/angular_client/web/`, using the canonical gateway for login, logout, CSRF, and `player/me`. A typed browser state model now tracks session, player, ship, sector, selected port, notices, and freshness. | The gateway command/event WebSocket is not implemented. Browser gameplay state refresh, replay, and offline persistence are therefore not available. Old menu/HUD services remain prototype code and are not the active login flow. |
| Mobile (future) | No mobile client implementation was found in the repository. It should be a thin native presentation and transport client over the same game capabilities, likely through the HTTPS gateway. | Needs a supported session-resume strategy, event catch-up, efficient snapshot refresh, and bandwidth-aware shared asset loading before it can provide reliable background/reconnect behavior. |

The game protocol is newline-delimited JSON over TCP. The planned gateway
WebSocket will carry existing JSON envelopes, but it is not yet implemented.
One envelope per WebSocket text frame is described in
[`PROTOCOL.v3/01_Transport_and_Framing.md`](PROTOCOL.v3/01_Transport_and_Framing.md).
The clients should share command semantics and response interpretation even
when the web gateway later provides a browser-appropriate WebSocket transport.

## Responsibilities

### Clients

- Render server-provided state and explicit command outcomes; keep local
  presentation state separate from authoritative game state.
- Use the same command names, payload meanings, validation rules, and error
  categories across platforms. Platform-specific menus must not change game
  semantics.
- Display freshness clearly. Cached values after disconnect are stale, not
  confirmed current state.
- Resolve stable `asset_id` values through the shared asset catalogue. Do not
  derive paths from server-provided strings or hard-code per-client art maps.
- Treat commands that change game state as unconfirmed until the server
  acknowledges them. Do not automatically replay an ambiguous timed-out
  command unless the existing idempotency contract makes that retry safe.
- Keep secrets in platform-appropriate storage: terminal credentials/session
  handling locally, desktop protected storage where available, browser gateway
  cookies rather than JavaScript-readable game tokens, and mobile OS secure
  storage for any native credentials/session material.

### Game server

- Authenticate users and authorize every command; remain the source of truth
  for gameplay state and results.
- Return enough typed information for clients to render entities without
  guessing their gameplay type or availability from incidental fields.
- Define the authoritative state snapshot boundary and event ordering/cursor
  semantics needed to recover after a lost connection.
- Keep browser and mobile reachability behind the gateway. The gateway must
  attach upstream authentication and must not expose arbitrary TCP targets.

### Gateway

- Own HTTPS, browser session cookies, CSRF/origin policy, request limits, and
  the future same-origin WebSocket connection.
- Translate transport only: forward allowed existing game commands and
  server outcomes; do not implement economy, movement, or other gameplay.
- Keep the upstream game token out of browser JSON, URLs, and browser-readable
  storage. The root gateway now implements login/logout and player information;
  command/event forwarding remains planned.

## Protocol audit: information clients currently infer

The documented envelopes already provide request IDs, correlated replies,
status/type/data, timestamps, and an event envelope. This is a useful common
base, but it does not yet define enough state identity and recovery metadata
for multiple long-lived graphical clients.

| Area | Current evidence and client inference | Recommended future contract (additive where possible) |
| --- | --- | --- |
| Entity artwork | The canonical catalogue has stable IDs and fallback mappings. Planet responses include class/type information and port entries include numeric type, but no general `asset_id` is specified in the current gameplay response. Clients map type codes/names to artwork. | Add optional stable `asset_id` to entity objects as responses are extended. Keep catalogue resolution client-side; the server returns an opaque catalogue ID, never a filesystem path. Preserve existing type/class fields for gameplay meaning and old clients. |
| Ship type | Current sector ship listings expose ship ID and name but not enough type information to consistently choose the right ship illustration. Godot falls back to generic art even though its asset catalogue maps known ship types. | Include stable ship type ID and/or canonical type name in sector listings and relevant entity views; optionally include `asset_id`. Do not force clients to infer a type from ship name, owner, or combat stats. |
| Planet and port type | Current payloads provide numeric type and, for planets, class/type name. Clients contain mappings from these fields to UI labels, actions, and art (including identifying Stardock from a numeric port type). | Keep the gameplay type fields, document their stable meanings, and optionally provide `asset_id` and explicit capability/action metadata where the server already knows eligibility. Client controls still request actions and display the server's final refusal/result. |
| Action availability | Clients often derive whether an action should appear from object type and locally maintained rules. This can drift from server permissions, game state, and future content. | Where practical, expose descriptive capabilities/eligibility in the existing object or detail response. These are UI hints only; server authorization remains authoritative. A refused command must return a stable error code and useful safe message. |
| Player state | Godot refreshes `player.my_info`, `ship.status`, and `sector.info` sequentially and combines them into a local snapshot with per-domain freshness. Python keeps player, sector, HUD, and event state in separate objects. Angular now has a normalized client view, but only `player/me` is currently served by the gateway. These views can span changing server state. | Define a versioned player snapshot covering requested domains, with a server revision/snapshot identifier and server timestamp. Until then retain per-domain receipt times and never claim an atomic cross-domain view. |
| Events and subscriptions | Protocol docs describe topic subscriptions and event envelopes; Python uses subscription commands. Godot currently avoids subscription requests because its code/docs expect differing topic fields (`event_type` versus `topic`). Event IDs are described, but ordering, replay, retention, and delivery guarantees are not established. | Publish one canonical subscription request/response contract, a topic catalogue, event IDs plus monotonic per-player or per-stream sequence/cursor, and explicit delivery semantics. Define which events are durable and which are ephemeral. |
| Reconnect and resume | Login issues a server session token. The gateway keeps it server-side and uses an HttpOnly cookie. The `auth.refresh` contract remains ambiguous; no gateway event bridge or replay cursor exists. | Clarify token expiry, refresh, revocation, and resume behavior. Re-authenticate through the gateway, refresh authoritative state, then replay events from a cursor only when the upstream contract supports it. Expired cursors must trigger a full resync. |
| IDs and measurements | Some existing response values are decoded as numeric JSON values even when they represent identifiers; clients have previously surfaced decimal formatting for IDs. | Define identifier fields as integer/opaque IDs and measurements as numeric quantities with units. UI formatters should use schema meaning, not generic floating-point formatting. |
| Timeouts and uncertain outcomes | The envelope supports request correlation, and clients use timeouts. A missing reply does not necessarily mean a mutating command failed. The optional idempotency metadata is documented, but command-by-command retry guarantees are not. | Specify which commands support idempotency and how duplicate requests are answered. For other commands, require state refresh or user-visible uncertainty after timeout; do not silently repeat. |

These are protocol readiness findings, not proposed changes in this task. The
current protocol, server, and database remain untouched.

## Notifications and event model

Use one event stream model across all clients. The server should identify each
event by a stable event type, schema version, event ID, server timestamp, and
stream sequence/cursor. Subscription topics should be authorized for the
authenticated player and scoped explicitly (player, sector, corporation, or
global). A topic catalogue should describe scope, durability, and payload
version.

Recommended delivery behavior is at-least-once for durable player events,
with stable IDs so clients can de-duplicate; ephemeral notices may be
best-effort and should be marked as such. On reconnect, a client supplies its
last acknowledged cursor. If retained events are available, the server
replays them in sequence; otherwise the server signals a cursor gap and the
client refreshes its snapshot before continuing. Events must not be treated
as a replacement for a fresh authoritative snapshot.

The gateway should preserve event IDs, ordering, and cursor semantics while
bridging to WebSocket. It should apply bounded queues and backpressure and
close or resynchronize slow consumers rather than silently presenting an
apparently complete feed after drops. A web reconnect should re-authenticate
through the gateway session and perform the same cursor/snapshot recovery.

## Shared client state model

Each client maps the same concepts into its native language. This is a local
projection of existing server responses, not a new wire protocol or source of
game rules:

| State domain | Contents and authority | Freshness rule |
| --- | --- | --- |
| Session | Authentication confidence and transport connectivity. Gateway session cookies remain browser-owned; upstream game tokens remain gateway-only. | A lost transport changes authentication to unknown, not invalid. Only gateway/server auth responses can confirm authenticated or unauthenticated. |
| Player | Last successful player information response. | Timestamp each successful response locally; preserve values but mark stale/offline on disconnect. |
| Ship | Last successful ship status response. | Same rule as player; do not infer a successful mutation from a submitted command. |
| Sector | Last successful sector description, including its ports, planets, ships, and warps. | This is a sequentially fetched view, not an atomic snapshot. |
| Port | A selected port row from the current sector response. Current protocol has no independent port-state snapshot in this path. | Inherit the sector response freshness and timestamp; clear the selection when the sector changes or becomes unavailable. |
| Notices/events | Bounded event history with stable IDs when supplied by the server. | Event receipt is not proof that a related player/ship/sector domain is current. Deduplicate by server ID; never invent a replay cursor where none is supplied. |

Freshness vocabulary is `unavailable`, `refreshing`, `fresh`, `partial`,
`stale`, and `disconnected`. Every domain retains its last confirmed value and
local receipt time when stale, so the UI can show it with an explicit stale
indicator. A successful response refreshes only the domain that response
actually describes. A refusal/error never changes authoritative state. A
command is pending until a correlated successful server response arrives.

Godot already has a normalized `TwClientState` with player/ship/sector
freshness and generation checks for late responses. Python has equivalent
building blocks split across `Context`, `HudState`, normalized sector data,
and `EventLog`; its UI must preserve the distinction between unknown and
zero. Angular's `StateService` implements the typed browser projection,
selected-port derivation, bounded notice history, and stale/reconnect status.
These are language-specific adapters to the table above; do not duplicate
validation or gameplay rules in them.

## Client transport and cache boundaries

Use three replaceable boundaries while retaining existing game command names
and payloads:

1. **Request/response transport:** authenticated HTTPS through the canonical
   gateway for browser clients; existing NDJSON transport for current Python
   and Godot builds. Response correlation, timeout, and refusal handling belong
   here, while domain state updates belong to the state adapter.
2. **Event transport:** an optional stream that emits existing server event
   envelopes plus connection status. Angular defines the interface but has no
   implementation until the gateway stream is delivered. Python and Godot
   already queue unsolicited TCP events. Do not simulate events by polling or
   create client-only gameplay events.
3. **Snapshot cache:** a platform storage adapter may load/save only the last
   confirmed display snapshot and freshness metadata. Cached data always starts
   stale. Never persist credentials/game bearer tokens in this abstraction,
   and never queue offline mutations for later execution.

The browser's gateway cookie stays HttpOnly/Secure and is not copied into
client state. Mobile should place any native credential/session material in
the OS secure store and use the same state semantics. All clients must mark
state stale after reconnect and fetch confirmed domains before enabling actions
that depend on current state.

## Shared assets across platforms

`assets/catalog.json` owns stable asset IDs, display names, categories,
object mappings, and variant paths. Clients choose thumbnail, standard, or
large variants based on display size and bandwidth. The catalogue is data,
not a second set of mappings embedded in each client.

- Godot resolves asset IDs through its asset catalogue adapter and packages
  the shared files/catalogue for desktop exports.
- The gateway/web host should serve the shared assets at a stable same-origin
  `/assets/` path, with immutable caching for versioned image paths and
  shorter revalidation for the catalogue.
- Mobile should load the catalogue once per catalogue version and cache
  selected variants by immutable path. Keep thumbnails and previously loaded
  art available for low-bandwidth or offline rendering.
- No client should use artwork placement as navigation coordinates or treat
  an asset ID as an authorization or gameplay value.

Artwork can be mapped locally while the server response fields are being
extended. Optional server `asset_id` support would remove hard-coded mappings
for newly added content, while stable type/class fields remain necessary for
gameplay and accessible text.

### Current catalogue coverage (2026-09-29)

`assets/catalog.json` has shared entries for generic fighter/hauler ships,
station ports, Stardock, sector backdrops/starfields, and seven planet classes
(M, K, O, L, H, U, C). The `assets/commodities/`, `assets/hardware/`, and
`assets/UI/` directories currently contain guidance placeholders but no
catalogue entries. Ports are currently one generic station plus Stardock;
individual port classes/types do not have dedicated art. Existing mappings are
usable with fallbacks, so these gaps do not block client state or gateway work.
Create the missing categories only when an actual UI screen needs the artwork;
do not start a bulk art pass as part of state architecture.

## Platform constraints

### Godot desktop

Use the shared catalogue and existing TCP/NDJSON transport. Keep asynchronous
requests correlated, present stale state distinctly after disconnection, and
refresh authoritative data after reconnect. Do not treat the current
client-composed player/ship/sector structure as an atomic server snapshot.
Enable event subscriptions only against the reconciled server contract.

### Web and Angular

Keep Angular under `client/angular_client/web/` and the canonical backend in
repository-root `gateway/`. The browser should use same-origin HTTPS and,
when implemented, a same-origin `wss://` bridge. Store only the gateway
session cookie in the browser; keep the game token server-side. Cookie-based
state-changing calls require CSRF and strict origin checks. Apply frame/body
limits, request timeouts, per-user limits, and backpressure. The current
gateway has authenticated player-information support but not gameplay command
or event forwarding.

### Mobile

No mobile code exists yet, so these are design constraints rather than a
mobile implementation audit. Use the same command and state semantics, with
either a native authenticated transport or the HTTPS gateway; prefer the
gateway when it provides the full required command/event surface. Account for
radio sleep, app backgrounding, intermittent connectivity, data caps, battery
use, and platform secure storage. Reconnect should fetch/reconcile state and
events rather than assuming the in-memory view stayed current. Do not queue
mutating gameplay commands offline unless the server explicitly guarantees
idempotent replay.

## Recommended next steps

1. **Reconcile the live contract.** Compare protocol documentation, strict
   request schemas, and handler behavior for subscriptions, events, session
   refresh, entity types, and `system.capabilities`. Publish one tested
   contract without breaking existing clients.
2. **Close rendering data gaps.** Add ship type information to sector entity
   listings. Add optional `asset_id` fields to port, planet, and ship objects
   where the server can resolve canonical catalogue mappings. Retain current
   type/class identifiers and make additions optional for compatibility.
3. **Define snapshot consistency.** Provide a coherent player/ship/sector
   snapshot or revision metadata and a server timestamp. Specify which
   responses are partial and how a client detects mixed revisions.
4. **Define event recovery.** Standardize subscription field names, topic
   authorization, event sequence/cursor, replay window, duplicate behavior,
   and explicit gap/full-resync behavior. Add reconnect conformance tests for
   Python and Godot before enabling Godot subscriptions.
5. **Make session resume safe.** Settle refresh/rotation/expiry/revocation
   semantics and clarify which commands can be retried with idempotency keys.
   Keep gateway cookie expiry no longer than the upstream session.
6. **Complete gateway gameplay transport.** Add bounded command forwarding
   and the event bridge only after event/reconnect semantics, allowlists, and
   backpressure policy are agreed with the server owner.
7. **Add web and mobile clients incrementally.** Extend Angular from login and
   player information once gateway command and snapshot endpoints are
   available. Start mobile after authentication, state refresh, and event
   recovery contracts are validated. Reuse shared fixtures across clients.

## Scope and verification

The current slice updates the client architecture, implements Angular's local
state projection and transport/cache interfaces, and adds state regression
tests. It does not change the game protocol, server, database, or artwork.
