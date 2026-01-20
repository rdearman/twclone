# Protocol v3 Index

## Overview

This directory contains the authoritative **Version 3** protocol specification for the TradeWars Clone (twclone) project. It modularizes the previous monolithic documentation into specific domains to improve maintainability and readability.

The protocol defines the JSON-based message exchange between:
1.  **Clients and the Server** (Player commands, events, queries).
2.  **The Server and the Game Engine** (S2S control link, durable event contracts).

## Directory Structure

*   **[01_Transport_and_Framing.md](./01_Transport_and_Framing.md)**: Transport layers (TCP/WebSocket), framing (NDJSON), and basic connection limits.
*   **[02_Envelope_and_Metadata.md](./02_Envelope_and_Metadata.md)**: Standard JSON message envelopes for requests, responses, and broadcasts.
*   **[03_Message_Types_and_Semantics.md](./03_Message_Types_and_Semantics.md)**: Event types, subscription models (PubSub), and topic semantics.
*   **[04_Error_Handling.md](./04_Error_Handling.md)**: Standard error codes, error envelope structures, and handling strategies.
*   **[05_Authentication_and_Security.md](./05_Authentication_and_Security.md)**: Auth flows, session tokens, HMAC, and sanitization rules.
*   **[06_Connection_States_and_Handshake.md](./06_Connection_States_and_Handshake.md)**: `hello` messages, capability negotiation, and localization setup.
*   **[07_Server_to_Server_Protocol.md](./07_Server_to_Server_Protocol.md)**: The TCP S2S link between Server and Engine, including health checks and control messages.
*   **[08_Engine_Event_Contract.md](./08_Engine_Event_Contract.md)**: The durable DB-based contract for events (Server->Engine) and commands (Engine->Server).
*   **[09_Command_and_Rate_Limits.md](./09_Command_and_Rate_Limits.md)**: Rate limiting headers, strategies, and command-specific limits.
*   **[10_Serialization_Rules.md](./10_Serialization_Rules.md)**: Data types, units, formatting (numbers, dates), and localization keys.
*   **[20_Player_Commands.md](./20_Player_Commands.md)**: Core player RPCs: Profile, Settings, Fines, and Session management.
*   **[21_Sector_and_Movement_Commands.md](./21_Sector_and_Movement_Commands.md)**: Movement, Warp, Scanning, Navigation, Bookmarks, and Autopilot.
*   **[22_Trade_and_Port_Commands.md](./22_Trade_and_Port_Commands.md)**: Port docking, buying/selling cargo, and trading logic.
*   **[23_Combat_and_Weapons.md](./23_Combat_and_Weapons.md)**: Ship-to-ship combat, damage events, and weapon definitions.
*   **[24_Planets_Outposts_Stations.md](./24_Planets_Outposts_Stations.md)**: Planet management, colonization, and outpost interactions.
*   **[25_Corporations_and_Stock_Market.md](./25_Corporations_and_Stock_Market.md)**: Corporation creation, banking, and management.
*   **[26_Banking_and_Ledger.md](./26_Banking_and_Ledger.md)**: Personal banking, transfers, and transaction history.
*   **[27_NPC_and_Ferengi_AI.md](./27_NPC_and_Ferengi_AI.md)**: NPC interactions, AI behavior, and enforcement logic.
*   **[28_Tavern_Noticeboard_and_Community_Systems.md](./28_Tavern_Noticeboard_and_Community_Systems.md)**: Bounties, Chat, News, and Community features.
*   **[29_SysOp_Commands.md](./29_SysOp_Commands.md)**: SysOp v1 commands for live operations (Config, Players, Engine).
*   **[90_Appendices.md](./90_Appendices.md)**: Additional reference material, architecture diagrams, and legacy notes.
*   **[99_Reserved_Future_Extensions.md](./99_Reserved_Future_Extensions.md)**: Placeholders for future protocol expansions.

## Legacy Documentation

The following files are considered **legacy** and superseded by this directory:
*   `PROTOCOL.v2.md`
*   `ENGINE.md`
*   `EVENT_CONTRACT.md`
*   `Intra-Stack_Protocol.md`

Refer to `NAV_BOOKMARKS_AVOID.md` for meta-guidance on navigation logic (not normative protocol).
