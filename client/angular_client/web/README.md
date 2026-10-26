# TWClone Angular browser client

This client connects to the canonical FastAPI service in repository-root
`gateway/`. It uses same-origin HTTP endpoints and an HttpOnly gateway cookie;
it does not connect directly to game-server TCP or the deprecated WebSocket
prototype in `../gateway/`.

## Local development

Start the gateway in one terminal (Python 3.11+):

```bash
cd gateway
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -e '.[test]'
GATEWAY_ENV=development GATEWAY_SESSION_COOKIE_SECURE=false \
  python -m uvicorn app.main:app --host 127.0.0.1 --port 8080
```

The gateway requires the existing game server to listen at the configured
`TWCLONE_SERVER_HOST` and `TWCLONE_SERVER_PORT` (default `127.0.0.1:1234`).
In a second terminal:

```bash
cd client/angular_client/web
npm ci
npm start
```

Angular serves at `http://localhost:4200` and proxies `/api` and `/healthz` to
the local gateway. The insecure cookie override is for local HTTP development
only; use the default secure cookie setting in production behind HTTPS.

## Playable client slice

- Sign in via the game server's existing `auth.login` command.
- Keep the game session token only in the gateway process.
- Read player information through `player.my_info` and refresh ship, sector,
  port, notices and combat status using existing game commands.
- Navigate to a sector with `move.warp`, inspect a listed port, quote/buy/sell
  commodities, and attack a ship listed in the current sector.
- Send those existing command envelopes over the authenticated
  `/api/v1/events` WebSocket. The gateway injects the server-held session token
  and returns correlated command results; the game server remains authoritative.
- Deduplicate and resume replayable events, reconnect after interruptions, and
  refresh authoritative snapshots when the cursor has expired.
- Revoke the game session and clear the gateway cookie on logout.
- Use the same Angular application in web and Capacitor builds. For Android,
  configure the HTTPS gateway address on the login screen; the game token is
  never stored in the app. Configure the gateway's exact WebView origin,
  Secure cookie and SameSite=None for cross-origin native requests.

The current screen is deliberately functional and data-focused. The web build
is the primary verification target; Android packaging does not change game
logic or server authority.

See [gateway architecture](../../../docs/V3_WEB_GATEWAY_ARCHITECTURE.md) and
[API contract](../../../docs/V3_WEB_GATEWAY_API.md).
