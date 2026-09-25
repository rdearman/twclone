# 90. Appendices

## A. Architecture Overview

The system consists of two main processes:
1.  **Server**: Handles player connections, RPCs, state reads, and high-frequency events.
2.  **Engine**: Handles simulation, ticks, cron jobs, and NPC logic.

They communicate via:
*   **DB Tables**: `engine_events` (Server->Engine) and `engine_commands` (Engine->Server).
*   **TCP Link**: S2S control channel for health and config.

## B. Database Schema Summaries

### Events
`events(id, ts, type, actor_player_id, sector_id, payload, idem_key)`

### Commands
`commands(id, type, payload, status, priority, due_at, idem_key)`

### Notices
`system_notice(id, ts, scope, sector_id, corp_id, player_id, message, meta, ephemeral, ttl_seconds)`

## C. Legacy Notes

*   The V2 protocol was monolithic. V3 splits it by domain.
*   `Intra-Stack_Protocol.md` and `ENGINE.md` logic has been merged into `07_S2S` and `08_Engine_Contract`.
