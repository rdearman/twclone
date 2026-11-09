# V3 Architecture Ownership and Dependency Boundaries

Status: control baseline, 2026-10-31

This document defines ownership for new V3 work. It is intentionally a
boundary map, not a request to move files or rewrite working subsystems.

## Authoritative subsystem owners

| Subsystem | Server/domain owner | Persistence owner | Allowed cross-subsystem use |
|---|---|---|---|
| Session, authentication and player identity | `server_auth.c`, `server_players.c` | `repo_auth.c`, `repo_players.c`, `repo_session.c` | Other domains may query identity through public repository APIs. |
| Ship identity, ownership, movement and towing | `server_ships.c`, `server_universe.c`, `server_autopilot.c` | `repo_ships.c`, `repo_universe.c`, `repo_cargo.c` | Combat, planets and ports call movement/ownership APIs; they do not update player/ship identity rows directly. |
| Ports and immediate trading | `server_ports.c` | `repo_ports.c`, `repo_market*.c`, `repo_commodities.c`, `repo_cargo.c` | Banking is called through the balance/ledger contract; trade owns port stock and trade receipts. |
| Banking and credit ledger | `server_bank.c` | `repo_bank.c` and the PostgreSQL bank procedures/invariants | All credit mutations use the bank/ledger public API. |
| Combat and sector defences | `server_combat.c` | `repo_combat.c`, `repo_ships.c` | Planet combat may call shared combat primitives but owns planet-specific defence rules. |
| Planets, production and citadels | `server_planets.c`, `server_citadel.c` | `repo_planets.c`, `repo_citadel.c` | Planet trade calls the canonical market/ledger APIs; no duplicate port settlement. |
| Engine, cron and events | `server_engine.c`, `server_cron.c` | `repo_engine*.c`, `repo_cron.c` | Background work uses idempotent domain operations, not handler-only mutation paths. |
| Protocol and command registration | `server_loop.c`, `schemas.c`, `server_envelope.c` | none | One command name and one schema/response contract; clients consume server facts. |

## Dependency direction

```text
client -> protocol/schema -> server handler/domain owner
                             -> repository owner -> DB driver -> PostgreSQL
engine/cron -----------------^       
```

Handlers validate transport input and construct responses. Domain owners hold
gameplay rules and transaction orchestration. Repositories issue persistence
operations. PostgreSQL remains the durable integrity boundary. New code must not
make a compatibility helper (`repo_cmd.*` or `game_db.*`) a second authority
for a mutation already owned by a domain repository.

## Transitional legacy inventory

The following are migration targets, not new extension points:

- `repo_cmd.*` and `game_db.*`: compatibility/query surface with existing
  callers; no new gameplay mutation should be added here.
- `repo_database.c`: compiled deprecated compatibility code; callers must be
  inventoried before removal.
- `repo_trade.c` and `repo_s2s.c`: source files present outside the active
  production build; either register deliberately or classify as dead after
  symbol-reference review.
- Deprecated command aliases in `server_loop.c`: preserve wire compatibility,
  but they must delegate to one implementation and must not gain new rules.

## Required controls

`tools/check_architecture_boundaries.py` checks the mechanical invariants that
can be checked without a compiler or database: command-name uniqueness,
production C source registration, and PostgreSQL migration-manifest coverage.
The checker reports the known transitional inventory separately. Future changes
that add a duplicate command, an unregistered production C unit, or an SQL file
outside the manifest fail the check.

Gameplay consolidation remains incremental. A subsystem may only be marked
consolidated after its callers have moved, its old mutation path is retired or
explicitly classified, and the relevant gameplay journey has been executed.
