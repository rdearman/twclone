# 02. Envelope & Metadata

This document defines the standard JSON envelopes used for all communication.

## 1. Client Request Envelope

Every client-to-server message is a single JSON object.

```json
{
  "id": "a6f1b8a0-5c8d-4b89-9d82-bb2f0a7b8d57",   // UUIDv4 client-generated unless noted
  "ts": "2025-09-17T19:45:12.345Z",
  "command": "move.warp",                         // The command to execute
  "auth": { "session": "eyJhbGciOi..." },         // Session token
  "data": { /* command-specific payload */ },
  "meta": {
    "idempotency_key": "c4e0e1a9-...",         // optional, client-provided for safe retries
    "client_version": "tw-client/2.0.0",
    "locale": "en-GB"
  }
}
```

### Fields
*   **id**: Unique Request ID (UUIDv4 recommended).
*   **ts**: Timestamp (ISO-8601 UTC).
*   **command**: The RPC method name (e.g., `move.warp`).
*   **auth**: Authentication object (usually containing `session`).
*   **data**: Command arguments (payload).
*   **meta**: Request metadata (idempotency, client info).

## 2. Server Response Envelope

The server replies with a corresponding envelope.

```json
{
  "id": "srv-ok-...",             // server message id
  "ts": "2025-09-17T19:45:12.410Z",
  "reply_to": "a6f1b8a0-...",     // Correlates to request `id`
  "status": "ok",                 // "ok" | "error" | "refused" | "partial"
  "type": "sector.info",          // domain-qualified response type
  "data": { /* result payload */ },
  "error": null,                  // or { code, message } see Error Handling
  "meta": {
    "rate_limit": { "limit": 200, "remaining": 152, "reset": "2025-09-17T20:00:00Z" },
    "trace": "srv-az1/fe2:rx394"
  }
}
```

### Fields
*   **id**: Unique Response ID.
*   **reply_to**: The `id` of the request being answered.
*   **status**: Outcome indicator.
*   **type**: The type of data returned (often matches or relates to the command).
*   **data**: The result payload.
*   **error**: Error details if `status` is not `ok`.

## 3. Server-Sent Event Envelope (Broadcasts)

For asynchronous events pushed to the client (not in response to a specific command).

```json
{
  "id": "evt-12345",
  "ts": "2025-09-17T19:47:01Z",
  "type": "sector.player_entered", // The event name (formerly "event" in v2 drafts, "type" in v3)
  "data": { 
      "v": 1,                      // Event version
      /* event-specific payload */ 
  }
}
```

**Key distinction**: Broadcasts have no `reply_to` field.

## 4. Client Broadcast Envelope (S2S/Internal Definition)

When the engine or server broadcasts to clients, the internal envelope used to generate the above is:

```json
{
  "type": "broadcast.v1",
  "data": {
    "id": 123,
    "ts": 1737990000,
    "scope": "global|sector|corp|player",
    "sector_id": 321,
    "corp_id": null,
    "player_id": null,
    "message": "Imperial Warship en route to sector 321",
    "meta": {"severity": "info"},
    "ephemeral": true
  }
}
```

This structure drives the content delivered in the `data` field of the public broadcast.
