# 06. Connection States & Handshake

## 1. Connection Lifecycle

1.  **Connect**: Client establishes TCP or WebSocket connection.
2.  **System Handshake (`system.hello`)**: Client announces itself. Server replies with capabilities.
3.  **Localization (Optional)**: Client negotiates locale and catalogs.
4.  **Session (`auth.login`/`auth.register`)**: Client authenticates.
5.  **Interaction**: Normal command flow.
6.  **Disconnect**: Connection closed or `auth.logout`.

## 2. System Handshake

### `system.hello` (Client -> Server)
Announce client version and capabilities.

**Request**
```json
{
  "command": "system.hello",
  "data": {
    "client_version": "tw-client/2.1.0",
    "capabilities": ["websockets", "compression.deflate"]
  }
}
```

### `system.welcome` (Server -> Client)
Server response with version and limits.

**Response**
```json
{
  "type": "system.welcome",
  "data": {
    "server_version": "tw-server/2.0.0",
    "capabilities": {
      "namespaces": ["auth", "bank", "market", "combat"],
      "limits": { "max_frame_size": 131072, "max_req_per_min": 200 },
      "auth_methods": ["session", "token"]
    }
  }
}
```

## 3. Capability Discovery

### `system.capabilities`
Query supported features at any time.

**Request**
```json
{ "command": "system.capabilities" }
```
**Response**
```json
{
  "type": "system.capabilities",
  "data": {
    "namespaces": ["auth", "bank", "market", "combat"],
    "features": ["websockets", "compression.deflate", "bookmark_add", "avoid_add"],
    "limits": { "max_bookmarks": 64 }
  }
}
```

## 4. Session Ping/Pong
Keep-alive mechanism.

**Request**
```json
{ "command": "session.ping" }
```
**Response**
```json
{ "type": "session.pong" }
```

## 5. Localization Negotiation

### `client.hello` (Locale Info)
Extended handshake for i18n.

**Request**
```json
{
  "command": "client.hello",
  "data": {
    "ui_locale": "en-GB",
    "catalogs": [ { "name": "core", "hash": "sha256:..." } ]
  }
}
```
**Response (`client.hello.ack_v1`)**
```json
{
  "type": "client.hello.ack_v1",
  "data": {
    "schema_version": 1,
    "missing_catalogs": [{ "name": "core", "hash": "sha256:new..." }],
    "server_locale": "en-GB"
  }
}
```

### `i18n.manifest.get`
Fetch localization manifest.

**Response**
```json
{
  "type": "i18n.manifest_v1",
  "data": {
    "contract_version": 1,
    "events": [
      { "type": "engine.tick", "v": 1, "keys": ["engine.tick.title", "engine.tick.body"] }
    ]
  }
}
```
