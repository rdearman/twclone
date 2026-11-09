# V3 HTTPS Gateway API Contract

This contract defines the browser-facing API boundary. It does not alter the
existing game protocol or make the gateway authoritative for gameplay.

## Current implementation

| Method and path | State | Behavior |
| --- | --- | --- |
| `GET /healthz` | Implemented | Process liveness only. Returns `{"status":"ok","service":"twclone-https-gateway"}` and `Cache-Control: no-store`; it does not probe the game server. |
| `POST /api/v1/auth/login` | Implemented | Calls existing `auth.login`; accepts `{ "username", "password" }`, maps `password` to upstream `passwd`, sets the gateway session cookie, and returns non-secret player summary plus CSRF token. |
| `GET /api/v1/auth/csrf` | Implemented | Requires a valid gateway cookie; returns the session-bound CSRF token for logout after a page refresh. |
| `POST /api/v1/auth/logout` | Implemented | Requires gateway cookie and matching `X-CSRF-Token`; calls `auth.logout`, clears gateway session/cookie, and returns `204`. |
| `GET /api/v1/player/me` | Implemented | Calls existing `player.my_info` with the gateway-held game token and returns `{ "player": <server data> }`. |
| `GET /api/v1/events` (WebSocket) | Implemented | Requires the gateway session cookie; opens a private persistent game-server connection, resumes from an optional opaque cursor, then forwards live player events. |

The general game-command WebSocket bridge remains planned. The event stream is
read-only and does not accept browser command envelopes.

## HTTP conventions

- Base prefix: `/api/v1`.
- JSON request/response bodies use UTF-8 `application/json`.
- Browser authentication uses a gateway-owned `Secure`, `HttpOnly`,
  `SameSite=Lax` (or stricter) cookie. The server's `session_token` is never
  returned in an HTTP response body, query string, or browser-readable storage.
- Cookie-authenticated state-changing endpoints require a session-bound CSRF
  token in `X-CSRF-Token`; requests with an `Origin` header must use the
  gateway's host. CORS is disabled unless explicit origins are configured.
- Errors use `{"error":{"code":"...","message":"..."}}`; login failures
  do not reveal whether the username exists or the password was wrong.
- Gateway errors distinguish invalid credentials/session (`401`), invalid
  request (FastAPI validation `422`), upstream timeout (`504`), upstream
  refusal/unavailable (`502`), and unreachable game server (`503`). The gateway
  must not expose internal stack traces or raw upstream transport details.

## Authentication and player endpoint details

### `POST /api/v1/auth/login`

Request:

```json
{"username":"player","password":"secret"}
```

The gateway validates the request, then calls the existing game command
`auth.login` with `{ "username": "player", "passwd": "secret" }`. The
`password` to `passwd` mapping is intentional: `src/server_auth.c` and the
strict request schema accept `passwd`, although the protocol example currently
uses `password`.

On success, the gateway stores the returned game `session_token` behind a
random gateway session ID and sets the gateway cookie. The response contains
only non-secret login state:

```json
{
  "player": {
    "id": 123,
    "current_sector": 1,
    "unread_news_count": 0
  }
}
```

The mapping expiry must not exceed the game session's 24-hour lifetime. The
game server owns credentials, session validity, and expiry.

### `GET /api/v1/auth/csrf`

Requires the gateway cookie and returns `{ "csrf_token": "..." }`. Angular
keeps the value in memory after login and reloads it from this endpoint after
a page refresh. The token is bound to the gateway session and is not an
authentication credential.

### `POST /api/v1/auth/logout`

Requires the gateway cookie and CSRF protection. The gateway sends
`auth.logout` upstream with the mapped token in `auth.session`, removes its
session mapping, and expires the cookie even if upstream revocation fails.
Return `204 No Content` after local cleanup so the browser is logged out of
the gateway; record upstream revocation failure without logging the token.
Because the game server owns token expiry, a failed revocation can leave that
server-side token valid until its normal expiry, but the gateway must not
retain or accept the cleared browser session.

### `GET /api/v1/player/me`

Requires the gateway cookie. The gateway calls `player.my_info` upstream with
the mapped game token and returns the authoritative player data in a stable
wrapper:

```json
{"player": {"...":"authoritative player.my_info data"}}
```

The endpoint is also the authoritative session check for the browser. Do not
add a separate local-only `session.valid` response that could appear valid
after the server revoked or expired the game token.

Account registration is deliberately outside the first slice. It should be
added only after rate limiting, abuse controls, and the existing registration
contract are reviewed.

## Player event stream WebSocket

Endpoint: same-origin `wss://<gateway>/api/v1/events`.

The browser authenticates with its existing gateway session cookie. The
gateway checks the `Origin`, uses its server-held game token for an upstream
`events.resume` command, and holds that private TCP/NDJSON connection for the
WebSocket lifetime. The optional `after_cursor` query parameter carries the
last opaque cursor accepted by the browser:

```text
wss://gateway.example/api/v1/events?after_cursor=<opaque-cursor>
```

The gateway completes all pages through the first response's fixed
`high_watermark` before forwarding buffered live events. It deduplicates
replay/live overlap by event ID without interpreting cursors. Events are
forwarded using their existing server envelope. A new connection without a
cursor starts at the server's current high-water mark and receives subsequent
live events.

If the server reports `ERR_CURSOR_INVALID` with
`error.data.resync_required: true`, the gateway sends
`{"type":"gateway.events.resync_required","data":{"resync_required":true}}`
and closes the WebSocket with code `4409`. The browser must refresh
authoritative state and reconnect without a cursor. Invalid or expired server
sessions close with `4401`; an invalid origin closes with `4403`. The endpoint
is read-only, bounds frames and replay/live buffering, and closes its upstream
connection when the browser disconnects, logs out, or its gateway session
expires.

The gateway does not create subscriptions or select topics. Existing server
subscriptions determine which player events are delivered. Browser commands
remain outside this endpoint and will use the separate game-command bridge.

## Game command WebSocket (planned)

Endpoint: same-origin `wss://<gateway>/api/v1/game`.

The browser sends one existing game command per WebSocket text message, for
example:

```json
{"id":"browser-42","command":"player.my_info","data":{}}
```

The gateway rejects browser-provided `auth`, upstream host/port, and
server-to-server fields. It injects the game session token server-side and
forwards the message over the configured private TCP/NDJSON connection:

```json
{"id":"browser-42","command":"player.my_info","data":{},"auth":{"session":"<gateway-held token>"}}
```

Replies retain their upstream status/type/data and are associated with the
browser request ID. Unsolicited server events are forwarded as separate
WebSocket messages. Initial bridge behavior should permit one outstanding
request per browser socket, enforce a command allowlist, bound frame sizes and
queues, apply upstream and idle timeouts, and close both transports together.
The gateway must not accept arbitrary TCP destinations from the browser.

Before browser gameplay ships, specify the allowlist/capability policy,
subscription lifecycle, event ordering, slow-consumer behavior, reconnect and
re-authentication semantics, and server-token refresh behavior. Never forward
S2S commands or let the browser choose upstream authentication fields.
