# Game engine overview

This page is a source map, not a duplicate protocol specification. The
normative server-to-engine message schemas and durable event contract are in
[`PROTOCOL.v3/07_Server_to_Server_Protocol.md`](PROTOCOL.v3/07_Server_to_Server_Protocol.md)
and [`PROTOCOL.v3/08_Engine_Event_Contract.md`](PROTOCOL.v3/08_Engine_Event_Contract.md).

## Responsibilities

The server accepts player connections, validates commands and applies
transactional gameplay changes. Engine and scheduled work handles recurring
simulation tasks, production, market/economy ticks and other durable work.
PostgreSQL is the authoritative state store. Repository code owns database
operations; gameplay handlers enforce command policy and response contracts.

## Source map

- Server startup and listeners: `src/server_main.c`, `src/server_loop.c`
- Engine and scheduler: `src/server_engine.c`, `src/server_cron.c`
- Durable engine repositories: `src/db/repo/repo_engine.c`,
  `src/db/repo/repo_cron.c`
- Gameplay command handlers: `src/server_*.c`
- Database repositories and migrations: `src/db/repo/`, `src/db/migrations/`
- Runtime and integration tests: `tests.v2/`

Names may shift as code changes; search the source tree for the handler or
repository symbol when tracing a specific operation.

## Design constraints

- Treat the database as authoritative; client state is a presentation cache.
- Keep multi-asset gameplay settlements inside a single database transaction.
- Revalidate ownership, permissions, balances and active entity state under
  transaction locks where the operation can race.
- Use persisted idempotency keys for operations whose clients may retry after
  a lost reply.
- Do not infer runtime guarantees from schema parity or a driver API. The
  supported full-game runtime is PostgreSQL.

See [`ARCHITECTURE_OWNERSHIP.md`](ARCHITECTURE_OWNERSHIP.md) for module
boundaries, [`DATABASE_RULES.md`](DATABASE_RULES.md) for persistence rules,
and the current
[`gameplay tracker`](V3_GAMEPLAY_COMPLETION_TRACKER.md) for evidence status.
