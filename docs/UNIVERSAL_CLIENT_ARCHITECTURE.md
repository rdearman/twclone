# Client architecture

All clients consume the same authoritative V3 command and event contracts.
Client-specific menus and transports may differ, but a client must render
server outcomes rather than invent gameplay state.

## Current clients

| Client | Role | Current boundary |
|---|---|---|
| Python terminal | Menu-driven TCP/NDJSON client and reference gameplay workflow | Uses direct game-server protocol; see `client/python_client/README.md`. |
| Godot desktop | Scene/menu-based TCP/NDJSON client | Uses the same game commands and state refresh rules; see `client/godot_client/godot/project.godot`. |
| Angular web | Browser client using the HTTPS gateway | Gateway authentication/session and event-stream status are documented in `V3_WEB_GATEWAY_API.md`; do not infer full gameplay command support. |
| Mobile | No current client implementation | Future work only. |

## Shared rules

- The game server owns identity, permissions, gameplay state and command
  outcomes. Local state is a cache and must be refreshed from authoritative
  responses.
- Treat a timeout as an uncertain outcome. Retry only with the same
  idempotency key where the command contract permits it.
- Keep transport correlation, request IDs and event handling consistent with
  [`PROTOCOL.v3`](PROTOCOL.v3/00_index.md).
- Disable or reject commands unavailable in the current context. Failed
  commands must not move the client into a fabricated success state.
- Sanitize diagnostics before buffering or display; private messages and
  credentials are never player-report diagnostics.
- Resolve shared artwork through [`UNIVERSAL_GAME_ASSETS.md`](UNIVERSAL_GAME_ASSETS.md)
  and `assets/catalog.json`; clients should not maintain duplicate asset maps.

The gateway is a transport/security boundary, not a gameplay authority. Its
API and implementation status are recorded separately in
[`V3_WEB_GATEWAY_ARCHITECTURE.md`](V3_WEB_GATEWAY_ARCHITECTURE.md) and
[`V3_WEB_GATEWAY_API.md`](V3_WEB_GATEWAY_API.md).
