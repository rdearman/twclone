# Architecture

This document collects the repository architecture, ownership boundaries, database rules, engine design, client architecture, codebase map, and web gateway architecture. Protocol wire contracts remain in [`PROTOCOL.v3/`](PROTOCOL.v3/00_index.md). Approved game rules and current acceptance evidence live in [`V3_GAMEPLAY_SPEC.md`](V3_GAMEPLAY_SPEC.md) and [`V3_PARITY.md`](V3_PARITY.md).


---

# Ownership and dependency boundaries

# V3 Architecture Ownership and Dependency Boundaries

Status: control baseline, 2026-10-31

This document defines ownership for new V3 work. It is intentionally a
boundary map, not a request to move files or rewrite working subsystems.

## Authoritative subsystem owners

| Subsystem | Server/domain owner | Persistence owner | Allowed cross-subsystem use |
|---|---|---|---|
| Session, authentication and player identity | `server_auth.c`, `server_players.c` | `repo_auth.c`, `repo_players.c`, `repo_session.c` | Other domains may query identity through public repository APIs. |
| Ship identity, ownership, movement and towing | `server_ships.c`, `server_universe.c`, `server_autopilot.c` | `repo_ships.c`, `repo_universe.c`, `repo_cargo.c` | Combat, planets and ports call movement/ownership APIs; they do not update player/ship identity rows directly. |
| Ports and immediate trading | `server_ports.c` | `repo_ports.c`, `repo_market*.c`, `repo_commodities.c`, `repo_cargo.c` | Banking is called through the balance/ledger contract; trade owns port stock and trade receipts. |
| Banking and credit ledger | `server_bank.c` | `repo_bank.c` and the PostgreSQL bank procedures/invariants | All credit mutations use the bank/ledger public API. |
| Combat and sector defences | `server_combat.c` | `repo_combat.c`, `repo_ships.c` | Planet combat may call shared combat primitives but owns planet-specific defence rules. |
| Planets, production and citadels | `server_planets.c`, `server_citadel.c` | `repo_planets.c`, `repo_citadel.c` | Planet trade calls the canonical market/ledger APIs; no duplicate port settlement. |
| Engine, cron and events | `server_engine.c`, `server_cron.c` | `repo_engine*.c`, `repo_cron.c` | Background work uses idempotent domain operations, not handler-only mutation paths. |
| Protocol and command registration | `server_loop.c`, `schemas.c`, `server_envelope.c` | none | One command name and one schema/response contract; clients consume server facts. |

## Dependency direction

```text
client -> protocol/schema -> server handler/domain owner
                             -> repository owner -> DB driver -> PostgreSQL
engine/cron -----------------^
```

Handlers validate transport input and construct responses. Domain owners hold
gameplay rules and transaction orchestration. Repositories issue persistence
operations. PostgreSQL remains the durable integrity boundary. New code must not
make a compatibility helper (`repo_cmd.*` or `game_db.*`) a second authority
for a mutation already owned by a domain repository.

## Transitional legacy inventory

The following are migration targets, not new extension points:

- `repo_cmd.*` and `game_db.*`: compatibility/query surface with existing
  callers; no new gameplay mutation should be added here.
- `repo_database.c`: compiled deprecated compatibility code; callers must be
  inventoried before removal.
- `repo_trade.c` and `repo_s2s.c`: source files present outside the active
  production build; either register deliberately or classify as dead after
  symbol-reference review.
- Deprecated command aliases in `server_loop.c`: preserve wire compatibility,
  but they must delegate to one implementation and must not gain new rules.

## Required controls

`tools/check_architecture_boundaries.py` checks the mechanical invariants that
can be checked without a compiler or database: command-name uniqueness,
production C source registration, and PostgreSQL migration-manifest coverage.
The checker reports the known transitional inventory separately. Future changes
that add a duplicate command, an unregistered production C unit, or an SQL file
outside the manifest fail the check.

Gameplay consolidation remains incremental. A subsystem may only be marked
consolidated after its callers have moved, its old mutation path is retired or
explicitly classified, and the relevant gameplay journey has been executed.


---

# Codebase reference

# Codebase map

Use this map to find current code. It intentionally avoids file counts and
historical test totals, which become stale quickly.

| Concern | Location |
|---|---|
| Server startup, listeners and dispatch | `src/server_main.c`, `src/server_loop.c`, `src/server_*.c` |
| Gameplay handlers | `src/server_*.c` |
| Database abstraction and drivers | `src/db/db_api.*`, `src/db/pg/`, `src/db/mysql/` |
| Persistence repositories | `src/db/repo/` |
| Schema and migrations | `src/db/migrations/`, `src/db/schema/` |
| Universe initialization | `src/bigbang_pg_main.c`, `src/bigbang_config.c` |
| Python client | `client/python_client/` |
| Godot client | `client/godot_client/` |
| Protocol-level regression | `tests.v2/` |
| Python client tests | `client/python_client/tests/` |
| Godot test harness | `client/godot_client/` and its Godot project tests |
| GitHub feedback publisher | `tools/feedback_github_publisher.py` |

## Where to read contracts

- Client/server protocol: [`PROTOCOL.v3/00_index.md`](PROTOCOL.v3/00_index.md)
- Approved game rules: [`V3_GAMEPLAY_SPEC.md`](V3_GAMEPLAY_SPEC.md)
- Gameplay and acceptance status: [`V3_PARITY.md`](V3_PARITY.md)
- Database policy: [`ARCHITECTURE.md`](ARCHITECTURE.md)
- Engine responsibilities: [`ARCHITECTURE.md`](ARCHITECTURE.md)
- Deployment status: [`V3_PARITY.md`](V3_PARITY.md)

The protocol is authoritative for message shapes. Handler source is
authoritative for implementation details; acceptance reports establish only
the specific behavior actually exercised.


---

# Database rules

# Database rules

## Supported runtime

PostgreSQL is the supported full-game runtime and the database used for current
server acceptance. MySQL driver/API work and schema checks do not establish
server runtime support; see [`V3_MYSQL_BACKEND.md`](V3_MYSQL_BACKEND.md).

## Persistence boundaries

- Gameplay handlers call repository APIs; repositories own SQL and state
  settlement. Keep dialect-specific code inside the database layer.
- Bind all user or runtime values as parameters. Do not construct SQL by
  interpolating player input.
- A gameplay operation that changes related balances, inventory, ownership,
  progress or receipts must settle atomically.
- Revalidate mutable authority and resource constraints under transaction
  locks when concurrent requests could invalidate an earlier check.
- Use durable idempotency records for retryable operations. A response lost
  after commit must not repeat the economic effect.
- Use migrations for schema changes. Keep PostgreSQL and MySQL schema parity
  checks intact, while describing MySQL runtime support accurately.
- Database tests must use an isolated disposable database. Do not use host,
  beta or production databases as test fixtures.

See [`ARCHITECTURE.md`](ARCHITECTURE.md) for system boundaries and the
[`V3_PARITY.md`](V3_PARITY.md) for
acceptance evidence.


---

# Engine

# Game engine overview

This page is a source map, not a duplicate protocol specification. The
normative server-to-engine message schemas and durable event contract are in
[`PROTOCOL.v3/07_Server_to_Server_Protocol.md`](PROTOCOL.v3/07_Server_to_Server_Protocol.md)
and [`PROTOCOL.v3/08_Engine_Event_Contract.md`](PROTOCOL.v3/08_Engine_Event_Contract.md).

## Responsibilities

The server accepts player connections, validates commands and applies
transactional gameplay changes. Engine and scheduled work handles recurring
simulation tasks, production, market/economy ticks and other durable work.
PostgreSQL is the authoritative state store. Repository code owns database
operations; gameplay handlers enforce command policy and response contracts.

## Source map

- Server startup and listeners: `src/server_main.c`, `src/server_loop.c`
- Engine and scheduler: `src/server_engine.c`, `src/server_cron.c`
- Durable engine repositories: `src/db/repo/repo_engine.c`,
  `src/db/repo/repo_cron.c`
- Gameplay command handlers: `src/server_*.c`
- Database repositories and migrations: `src/db/repo/`, `src/db/migrations/`
- Runtime and integration tests: `tests.v2/`

Names may shift as code changes; search the source tree for the handler or
repository symbol when tracing a specific operation.

## Design constraints

- Treat the database as authoritative; client state is a presentation cache.
- Keep multi-asset gameplay settlements inside a single database transaction.
- Revalidate ownership, permissions, balances and active entity state under
  transaction locks where the operation can race.
- Use persisted idempotency keys for operations whose clients may retry after
  a lost reply.
- Do not infer runtime guarantees from schema parity or a driver API. The
  supported full-game runtime is PostgreSQL.

See [`ARCHITECTURE.md`](ARCHITECTURE.md) for module
boundaries, [`ARCHITECTURE.md`](ARCHITECTURE.md) for persistence rules,
and the current
[`gameplay tracker`](V3_PARITY.md) for evidence status.


---

# Universal client architecture

# Client architecture

All clients consume the same authoritative V3 command and event contracts.
Client-specific menus and transports may differ, but a client must render
server outcomes rather than invent gameplay state.

## Current clients

| Client | Role | Current boundary |
|---|---|---|
| Python terminal | Menu-driven TCP/NDJSON client and reference gameplay workflow | Uses direct game-server protocol; see `client/python_client/README.md`. |
| Godot desktop | Scene/menu-based TCP/NDJSON client | Uses the same game commands and state refresh rules; see `client/godot_client/godot/project.godot`. |
| Angular web | Browser client using the HTTPS gateway | Gateway authentication/session and event-stream status are documented in `V3_WEB_GATEWAY_API.md`; do not infer full gameplay command support. |
| Mobile | No current client implementation | Future work only. |

## Shared rules

- The game server owns identity, permissions, gameplay state and command
  outcomes. Local state is a cache and must be refreshed from authoritative
  responses.
- Treat a timeout as an uncertain outcome. Retry only with the same
  idempotency key where the command contract permits it.
- Keep transport correlation, request IDs and event handling consistent with
  [`PROTOCOL.v3`](PROTOCOL.v3/00_index.md).
- Disable or reject commands unavailable in the current context. Failed
  commands must not move the client into a fabricated success state.
- Sanitize diagnostics before buffering or display; private messages and
  credentials are never player-report diagnostics.
- Resolve shared artwork through [`UNIVERSAL_GAME_ASSETS.md`](UNIVERSAL_GAME_ASSETS.md)
  and `assets/catalog.json`; clients should not maintain duplicate asset maps.

The gateway is a transport/security boundary, not a gameplay authority. Its
API and implementation status are recorded separately in
the gateway section below and [`V3_WEB_GATEWAY_API.md`](V3_WEB_GATEWAY_API.md).


---

# V3 web gateway architecture

# V3 HTTPS Gateway Architecture

Status: canonical location selected; FastAPI health, login, logout, and
player-information endpoints implemented. The browser game command/event
stream remains future work.

## Architecture decision

Use repository-root `gateway/` for the HTTPS backend. Keep browser UI and
Angular assets in `client/angular_client/web/`. The old
`client/angular_client/gateway/` is a deprecated prototype; do not revive or
extend it. See the separate [browser API contract](V3_WEB_GATEWAY_API.md).

```text
Browser / Angular UI
  client/angular_client/web/
             |
      same-origin HTTPS
             v
Gateway backend (FastAPI)
  gateway/
             |
       private TCP/NDJSON
             v
Existing TWClone game server
```

The game server remains authoritative for passwords, game sessions, player
state, sectors, ships, economy, and gameplay commands. The gateway owns the
browser-facing transport, cookie boundary, CSRF/CORS policy, HTTP response
mapping, and eventual WebSocket bridge. It must not become another gameplay
service or database.

## Existing code inventory

| Path | Classification | Decision |
| --- | --- | --- |
| `gateway/` | Canonical FastAPI backend | Keep all new gateway code here. It provides HTTP auth/player routes and a fixed-target TCP/NDJSON adapter. |
| `client/angular_client/web/` | Angular frontend | Keep UI, assets, and same-origin HTTP client here. |
| `client/angular_client/gateway/gateway.py` | WebSocket-to-TCP prototype | Deprecated. Do not deploy or add features. It accepts arbitrary browser-selected TCP targets and forwards plaintext game-protocol traffic. |
| `client/angular_client/gateway/requirements.txt` and `test_echo.py` | Prototype dependencies and manual echo script | Historical reference only; not the canonical backend/test setup. |
| `client/angular_client/gateway/README.md` | Prototype instructions | Marks that directory deprecated and points to this architecture. |
| `client/angular_client/gateway/.venv/` | Local Python environment | Not source code. Left untouched; repository cleanup is outside this foundation task. |

## Current foundation

`gateway/app/config.py` parses and validates environment settings for the ASGI
listener, trusted hostnames, optional CORS origins, fixed game-server target,
cookie policy, and upstream timeout. `gateway/app/game_server.py` opens a
fixed-target TCP/NDJSON connection for one existing game command at a time.
`gateway/app/main.py` provides health, auth, and player routes. Health is
process liveness only and does not claim the game server is healthy. Production
mode disables interactive API docs.

## Authentication audit

The authentication boundary already exists in the game server and should be
reused:

- `auth.login` checks the player's credentials and returns `player_id`,
  `current_sector`, `unread_news_count`, and a generated opaque
  `session_token`.
- `src/server_auth.c` reads the password from `data.passwd`; the strict schema
  requires `username` and `passwd`. The protocol example currently says
  `password`. The gateway adapter must translate its browser-facing
  `password` field to upstream `passwd` and must not change either shared
  contract here.
- Current successful login creates a server session expiring after 24 hours.
  The game token is secret bearer material and must never be sent in browser
  JSON, URL parameters, JavaScript-readable storage, or logs.
- `src/server_loop.c` authenticates each command from `auth.session` (or the
  legacy metadata token) and reloads player context from the server session.
  The gateway should attach `auth.session` itself to authenticated commands;
  it must discard browser-supplied upstream auth fields.
- `auth.logout` accepts the game token in `auth.session` and revokes it. Its
  `data` object is empty under the current strict schema.
- `auth.refresh` has a contract mismatch: the handler looks in `data` or
  `meta.session_token`, while the documentation example puts the token in
  `auth.session`. With only authenticated context, its fallback creates a new
  session rather than clearly rotating/revoking the existing token. Do not
  build gateway refresh on this ambiguous path. The initial cookie lifetime
  should not exceed the existing server session; server/API owners must settle
  refresh semantics before a refresh endpoint is added.
- `player.my_info` is the existing authenticated source for the first player
  endpoint. The gateway maps its server envelope but does not synthesize game
  state.

No changes were made to `server_auth.c`, schemas, or the game protocol.

## Gateway session architecture

The browser receives a cryptographically random gateway session identifier in
a `Secure`, `HttpOnly`, `SameSite=Lax` (or stricter) cookie. The gateway maps
that identifier to the game-issued session token, player ID, and expiry. The
raw game token stays inside the gateway process/session store. The mapping
expires no later than the server's session expiry. Logout attempts upstream
revocation and always invalidates the gateway mapping and clears the cookie;
if upstream revocation fails, the server token may remain valid until its
normal expiry, but the browser cannot use it through the gateway.

The implementation uses a process-local mapping; a gateway restart forces
users to log in again. Run one gateway worker. Before multiple workers or
production failover, use a dedicated ephemeral session store such as Redis for
the mapping only. It must not store player/gameplay records or replace the
game's database. Do not implement session persistence by changing game schema.

## HTTP and WebSocket boundary

Implemented HTTP operations are `POST /api/v1/auth/login`,
`GET /api/v1/auth/csrf`, `POST /api/v1/auth/logout`, and
`GET /api/v1/player/me`. Login sets a cookie and omits the game token. Logout
requires a CSRF header and clears local state if upstream revocation fails.
`player/me` calls `player.my_info` using the gateway-held session, so an
expired/revoked server token is detected by the authoritative command.

Later, a same-origin `wss://.../api/v1/game` bridge will accept one existing
game-protocol envelope per WebSocket message. It injects authentication
server-side, relays correlated replies and unsolicited events, and never
accepts arbitrary upstream host, port, or auth settings from the browser.
Before enabling it, define command allowlists, limits, event/subscription
delivery, backpressure, disconnect cleanup, and reconnect/token expiry
behavior. Gameplay remains in the existing game server.

## Security considerations

- Terminate public TLS at a trusted reverse proxy and bind the ASGI service to
  a private interface. Forwarded headers may be trusted only from configured
  proxy addresses.
- Set explicit `GATEWAY_TRUSTED_HOSTS` in production. Keep CORS disabled for
  same-origin deployment; if needed, configure exact origins, never `*` with
  credentials.
- Set `GATEWAY_SESSION_COOKIE_SECURE=true` in production. Logout uses a
  session-bound CSRF token and same-host origin check.
- Never log passwords, cookies, game tokens, or full authentication envelopes.
  Use generic login failures, bounded request/frame sizes, rate limits, and
  connection/read timeouts. Login rate limiting is still required before
  public deployment.
- The browser must never choose a TCP destination. The game-server host and
  port are gateway-only configuration to prevent SSRF/network pivoting.
- Keep `/healthz` minimal. It does not expose dependency details or credentials.
- Do not enable the deprecated prototype as a fallback path.

## Next implementation steps

1. Add login rate limiting and a shared ephemeral session store before public
   multi-worker deployment.
2. Resolve the server `auth.refresh` contract with its owner before adding
   refresh or long-lived browser session behavior.
3. Add the same-origin WebSocket bridge only after command/event policy,
   queue/backpressure, reconnect, and load limits are specified.
