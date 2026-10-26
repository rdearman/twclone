# TWClone HTTPS Gateway

The repository-root `gateway/` directory is the canonical HTTPS backend. The
Angular application lives in `client/angular_client/web/`. The old
`client/angular_client/gateway/` WebSocket-to-TCP prototype is deprecated and
must not be deployed or extended.

This is the browser authentication boundary and player event transport, not a
web game. It provides health, login, logout, current-player and authenticated
event/command stream endpoints. The existing game server remains authoritative
and its TCP NDJSON protocol is unchanged.
The game session token stays in an in-memory gateway session map; the browser
only receives a gateway cookie marked `HttpOnly`; production uses `Secure`.
SameSite defaults to `Lax` for same-origin browser hosting.

## Local development

Requires Python 3.11+.

```bash
cd gateway
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -e '.[test]'
GATEWAY_ENV=development GATEWAY_SESSION_COOKIE_SECURE=false \
  python -m uvicorn app.main:app \
    --host "${GATEWAY_HOST:-127.0.0.1}" \
    --port "${GATEWAY_PORT:-8080}" \
    --log-level "${GATEWAY_LOG_LEVEL:-info}"
```

The gateway requires the existing game server at the configured
`TWCLONE_SERVER_HOST` and `TWCLONE_SERVER_PORT` (default `127.0.0.1:1234`).
The routes are `GET /healthz`, `POST /api/v1/auth/login`,
`GET /api/v1/auth/csrf`, `POST /api/v1/auth/logout`, and
`GET /api/v1/player/me`. The authenticated event stream is
`wss://<gateway>/api/v1/events`; reconnect with the opaque
`after_cursor` query parameter. Existing game command envelopes are sent over
that same authenticated socket and replies are correlated with `reply_to`.
Only the gateway injects the held game token; browser-supplied auth, target and
S2S fields are discarded. FastAPI docs are enabled outside production mode.

For a Capacitor Android build pointed at a different HTTPS host, add the
WebView's exact origin to `GATEWAY_CORS_ORIGINS`, use
`GATEWAY_SESSION_COOKIE_SECURE=true`, and set
`GATEWAY_SESSION_COOKIE_SAMESITE=none` so the cross-origin WebView requests and
WebSocket can carry the HttpOnly session cookie. Serve the gateway over HTTPS;
never embed game credentials or tokens in the app. Browser deployments should
prefer same-origin reverse proxying and the default `lax` cookie.

Run gateway tests with `pytest -q tests`. They use a fake adapter and a
disposable local TCP server; no game database is required.

## Configuration

All settings are environment variables. `.env.example` documents local
development values; the application does not load dotenv files automatically.

| Variable | Default | Purpose |
| --- | --- | --- |
| `GATEWAY_ENV` | `development` | `development`, `test`, or `production`; production disables `/docs`. |
| `GATEWAY_HOST` | `127.0.0.1` | ASGI bind address. |
| `GATEWAY_PORT` | `8080` | ASGI listen port. |
| `GATEWAY_LOG_LEVEL` | `info` | Uvicorn log level. |
| `GATEWAY_TRUSTED_HOSTS` | `localhost,127.0.0.1,testserver` | Comma-separated Host allowlist; configure public hostnames in production. |
| `GATEWAY_CORS_ORIGINS` | empty | Exact browser origins; keep empty for same-origin deployment. |
| `GATEWAY_PUBLIC_ORIGIN` | unset | Exact public origin for Origin checks when TLS terminates at a reverse proxy. |
| `GATEWAY_SESSION_COOKIE_NAME` | `twclone_session` | Gateway cookie name. |
| `GATEWAY_SESSION_COOKIE_SECURE` | environment-dependent | Defaults to true in production; local HTTP explicitly sets false. |
| `GATEWAY_SESSION_COOKIE_SAMESITE` | `lax` | `lax`, `strict`, or `none`; `none` requires a secure cookie for cross-origin Capacitor access. |
| `TWCLONE_SERVER_HOST` | `127.0.0.1` | Fixed private game-server TCP host. Never browser-controlled. |
| `TWCLONE_SERVER_PORT` | `1234` | Fixed private game-server TCP port. |
| `TWCLONE_SERVER_TIMEOUT_SECONDS` | `5` | Upstream connect and reply timeout. |

## Authentication boundary

- Login maps browser `password` to the server's existing `auth.login` data
  field `passwd`.
- Logout calls `auth.logout` with the gateway-held token in `auth.session`.
- Player lookup calls `player.my_info` with the gateway-held token.
- The cookie contains a random gateway session ID, never the game token.
- Logout requires the session's CSRF token in `X-CSRF-Token`; the token can be
  reloaded from `GET /api/v1/auth/csrf` after a browser refresh.
- Sessions are process-local and expire after the game's 24-hour session
  lifetime. Run one worker for this foundation; a shared ephemeral store is
  needed before multiple workers or failover.

Production exposure still requires TLS/reverse-proxy configuration, shared
session storage for multiple workers, login rate limiting, monitoring, and an
operational security review. The command bridge forwards only existing server
commands; it does not add gameplay semantics. The Angular app has no direct
TCP or fallback game-server connection.

See [gateway architecture](../docs/V3_WEB_GATEWAY_ARCHITECTURE.md) and
[HTTP API contract](../docs/V3_WEB_GATEWAY_API.md).
