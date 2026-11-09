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
