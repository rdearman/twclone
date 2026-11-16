# Documentation index

Use these documents as the active sources of truth. Dated acceptance reports
are retained only when they contain unique execution evidence; older triage
and sprint reports have been removed when superseded.

## Project and release

- [Current status](V3_STATUS.md)
- [Release-candidate audit](testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md)
- [Deployment hardening plan](V3_DEPLOYMENT_HARDENING_PLAN.md) — plan only;
  it does not authorize infrastructure changes
- [Public beta security baseline](SECURITY_PUBLIC_BETA.md)

## Gameplay

- [Approved gameplay decisions](V3_GAMEPLAY_DECISIONS.md)
- [Parity scope](V3_GAMEPLAY_PARITY.md)
- [Completion and acceptance summary](V3_GAMEPLAY_COMPLETION_TRACKER.md)
- [Galactic Exchange contract](V3_GALACTIC_EXCHANGE_CONTRACT.md)
- [Equipment catalogue](V3_EQUIPMENT_CATALOGUE.md)
- [Planet fighter production](PLANET_FIGHTER_PRODUCTION.md)
- [Password storage and migration](PASSWORD_HASHING.md)
- [Market fill settlement contract](design/V3_MARKET_FILL_ORDER_CONTRACT.md)

## Protocol and architecture

- [Canonical V3 protocol](PROTOCOL.v3/00_index.md)
- [Architecture ownership](ARCHITECTURE_OWNERSHIP.md)
- [Engine overview](ENGINE.md)
- [Codebase map](CODEBASE_REFERENCE.md)
- [Client architecture](UNIVERSAL_CLIENT_ARCHITECTURE.md)
- [Shared game assets](UNIVERSAL_GAME_ASSETS.md)
- [Database rules](DATABASE_RULES.md)
- [MySQL backend status](V3_MYSQL_BACKEND.md) — full game-server runtime
  support is not established
- [Player feedback system](V3_PLAYER_FEEDBACK_SYSTEM.md)
- [HTTPS gateway API](V3_WEB_GATEWAY_API.md)
- [SysOp command policy](SYSOP_MENU.md)

## Clients and tools

- [Python client](../client/python_client/README.md)
- [Python client developer guide](python-client-development-handover.md)
- [Godot project](../client/godot_client/godot/project.godot)
- [Integration QA](testing/V3_INTEGRATION_QA.md)

## Acceptance evidence

- [Release-candidate audit](testing/V3_RELEASE_CANDIDATE_AUDIT_20261115.md)
- [StarPort lifecycle and restart](testing/V3_STARPORT_ACCEPTANCE_20261114.md)
- [Godot and StarPort concurrency](testing/V3_GODOT_STARPORT_ACCEPTANCE_20261114.md)
- [Player feedback](testing/V3_PLAYER_FEEDBACK_ACCEPTANCE_20261114.md)
- [Photon missiles](testing/V3_PHOTON_ACCEPTANCE_20261114.md)
- [Toll fighters](testing/V3_TOLL_FIGHTER_ACCEPTANCE_20261114.md)
- [Planet production rates](testing/V3_PLANET_PRODUCTION_RATE_ACCEPTANCE_20261114.md)

The protocol and decision register are normative. The tracker summarizes
status; acceptance reports establish only the specific operations and
conditions they document. Do not treat an old report or source implementation
as current verification evidence.
