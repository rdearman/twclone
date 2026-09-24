# 01. Transport, Framing, & Limits

## 1. Transport

*   **Encoding**: UTF-8.
*   **Transport Protocols**:
    *   **TCP/NDJSON**: One JSON object per line (newline-delimited).
    *   **WebSocket**: One JSON object per WebSocket message (text frames).
*   **Compression**: Per-transport negotiation (e.g., `permessage-deflate` on WebSocket).

## 2. Framing

### NDJSON (TCP)
*   Each message must be terminated by a newline character (`\n`).
*   The JSON object must be complete and valid.

### WebSocket
*   Each WebSocket message frame contains exactly one JSON envelope.

## 3. Limits

*   **Max Frame Size**: 64 KiB (65,536 bytes) default.
    *   Server MAY advertise a higher limit via `system.capabilities` -> `limits.max_frame_size`.
    *   **Engine S2S Limit**: Default 64 KiB. Hard reject larger.
*   **Rate Limits**: See [09_Command_and_Rate_Limits.md](./09_Command_and_Rate_Limits.md).

## 4. Clocks & Time

*   **Timestamps**: RFC 3339 / ISO-8601 UTC (`YYYY-MM-DDTHH:mm:ss.sssZ`).
*   **Server Time**: The server is the source of truth for time.
*   **Drift**: Clients should sync their local display time with `server_time` provided in handshake messages.

## 5. Versioning

*   **Protocol Version**: The protocol version (e.g., "v2.0", "v3.0") applies to the overall structure.
*   **Event Versioning**: Individual events carry a `v` field (e.g., `"v": 1`) inside their `data` payload.
    *   **Additive Changes**: New optional fields may be added without bumping the version.
    *   **Breaking Changes**: Removing or renaming fields requires a version bump.
