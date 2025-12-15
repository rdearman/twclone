# 05. Authentication & Security

## 1. Authentication Methods

*   **Session Token**: The primary method. JWT (JSON Web Token) or opaque string provided upon login.
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
    "password": "strong_password",
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
  "data": { "username": "player", "password": "password" }
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
Refreshes a session token.

**Request**
```json
{ "command": "auth.refresh", "auth": { "session": "..." } }
```
**Response**
```json
{ "status": "ok", "type": "auth.session", "data": { "session_token": "new_token..." } }
```

## 3. Security Rules

*   **Sanitization**: The server sanitizes all user-provided strings (stripping ANSI codes, scripts, etc.) before storage and before broadcasting.
*   **Secrets**: Passwords must never be echoed in responses or logs.
*   **Tokens**: Session tokens should be treated as secrets by the client.

## 4. S2S Authentication (HMAC)

The Server and Engine share secrets (`s2s_keys` table).
*   **Scheme**: HMAC-SHA256.
*   **Signature**: Signed canonicalized JSON payload (excluding `auth.sig`).
*   **Key Rotation**: Supported via `key_id`.

See [07_Server_to_Server_Protocol.md](./07_Server_to_Server_Protocol.md) for details.
