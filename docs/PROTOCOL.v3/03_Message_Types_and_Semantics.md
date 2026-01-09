# 03. Message Types & Semantics

## 1. Naming Conventions

*   **Format**: `domain.action[.subaction]` (e.g., `engine.tick`, `nav.sector.enter`, `combat.hit`).
*   **Field Case**: `lower_snake_case`.
*   **IDs**: Numeric integers suffixed with `_id` (e.g., `player_id`, `sector_id`), except for UUIDs used in envelopes.

## 2. Event Subscriptions (PubSub)

Events are published to topic namespaces. Clients must subscribe to receive most events.

### 2.1 Topics
*   **`system.notice`**: System-wide notices (Locked, always subscribed).
*   **`sector.*`**: Events in the current sector.
*   **`combat.*`**: Combat events involving the player.
*   **`trade.*`**: Market updates.

### 2.2 Subscription Commands

**Subscribe (Idempotent)**
```json
{ "command": "subscribe.add", "data": { "topic": "sector.*" } }
```
Response: `subscribe.ack_v1`

**Unsubscribe (Idempotent)**
```json
{ "command": "subscribe.remove", "data": { "topic": "sector.42" } }
```
Response: `subscribe.ack_v1`

**List Subscriptions**
```json
{ "command": "subscribe.list" }
```
Response: `subscribe.list_v1` containing `{"topics": [...]}`.

**Subscription Catalog**
```json
{ "command": "subscribe.catalog" }
```
Response: `subscribe.catalog_v1` containing `{"topics": [...]}` where each topic has `pattern`, `kind`, and optional `desc`.

### 2.3 Locked Topics
Some topics (like `system.notice`) are **locked**. Attempts to unsubscribe will fail with `1405 Topic is locked`.

## 3. Common Event Types

### 3.1 `engine.tick` (v1)
Emitted periodically by the engine.
```json
{
  "type": "engine.tick",
  "data": { "v":1, "tick":123456, "dt":1, "universe_time":"2025-10-20T18:50:03Z" }
}
```

### 3.2 `system.notice` (v1)
Important server messages (maintenance, global events).
```json
{
  "type": "system.notice",
  "data": {
    "v":1,
    "notice_id": 1001,
    "title": "Scheduled Maintenance",
    "body": "Server going down in 10 minutes.",
    "severity": "warn",
    "expires_at": "2025-10-20T19:10:00Z"
  }
}
```

## 4. Data Semantics
*   **No Prose**: Inside `data` payloads, favor codes, IDs, and enums over raw strings.
*   **Localization**: Text should be rendered client-side using localization keys where possible (see [10_Serialization_Rules.md](./10_Serialization_Rules.md)).
*   **Sanitization**: All outbound strings are ANSI-stripped by the server.
