# 05. Authentication & Security

## 1. Authentication Methods

*   **Session Token**: Opaque bearer string issued by the server at login and
    stored in the server session table.
*   **HMAC (S2S)**: Used for Server-to-Engine communication.

## 2. Auth Commands

### 2.1 `auth.register`
Creates a new player account.

**Request**
```json
{
  "command": "auth.register",
  "data": {
    "username": "new_player",
    "passwd": "strong_password",
    "ship_name": "My First Ship",
    "ui_locale": "en_US",
    "ui_timezone": "America/New_York"
  }
}
```
**Response**
```json
{
  "status": "ok",
  "type": "auth.session",
  "data": {
    "player_id": 12345,
    "session_token": "eyJhbGciOiJIUzI1Ni..."
  }
}
```

### 2.2 `auth.login`
Authenticates a user.

**Request**
```json
{
  "command": "auth.login",
  "data": { "username": "player", "passwd": "password" }
}
```
**Response**
```json
{
  "status": "ok",
  "type": "auth.session",
  "data": {
    "player_id": 12345,
    "current_sector": 1,
    "unread_news_count": 0,
    "session_token": "eyJhbGciOiJIUzI1Ni..."
  }
}
```

### 2.3 `auth.logout`
Invalidates the session.

**Request**
```json
{ "command": "auth.logout", "auth": { "session": "..." } }
```
**Response**
```json
{ "status": "ok", "type": "auth.logged_out" }
```

### 2.4 `auth.refresh`
Rotates the authenticated session token. Session tokens are opaque bearer
secrets stored by the server and are checked against the session table on each
command. A newly issued login or refresh token expires after 24 hours.

**Request**
```json
{ "command": "auth.refresh", "auth": { "session": "..." } }
```
The token in `auth.session` is canonical. `data.session_token` and
`meta.session_token` remain accepted for legacy callers when they identify the
same player as `auth.session`.

**Response**
```json
{ "status": "ok", "type": "auth.session", "data": { "session_token": "new_token..." } }
```
Refresh atomically replaces the submitted token with the returned token. The
old token stops authenticating commands as soon as the refresh succeeds. A
missing, revoked, or expired token is rejected; refresh does not create a
second independent session.

`auth.logout` revokes the authenticated session. The client must discard that
token; subsequent commands using it are unauthenticated.

## 3. Security Rules

*   **Sanitization**: The server sanitizes all user-provided strings (stripping ANSI codes, scripts, etc.) before storage and before broadcasting.
*   **Secrets**: Passwords must never be echoed in responses or logs.
*   **Tokens**: Session tokens are opaque bearer secrets. Send them in
    `auth.session`; never place them in URLs, logs, or user-visible data.
    Clients must replace their local token with the value returned by
    `auth.refresh` and discard the old value.

## 4. S2S Authentication (HMAC)

The Server and Engine share secrets (`s2s_keys` table).
*   **Scheme**: HMAC-SHA256.
*   **Signature**: Signed canonicalized JSON payload (excluding `auth.sig`).
*   **Key Rotation**: Supported via `key_id`.

See [07_Server_to_Server_Protocol.md](./07_Server_to_Server_Protocol.md) for details.
