# Database rules

## Supported runtime

PostgreSQL is the supported full-game runtime and the database used for current
server acceptance. MySQL driver/API work and schema checks do not establish
server runtime support; see [`V3_MYSQL_BACKEND.md`](V3_MYSQL_BACKEND.md).

## Persistence boundaries

- Gameplay handlers call repository APIs; repositories own SQL and state
  settlement. Keep dialect-specific code inside the database layer.
- Bind all user or runtime values as parameters. Do not construct SQL by
  interpolating player input.
- A gameplay operation that changes related balances, inventory, ownership,
  progress or receipts must settle atomically.
- Revalidate mutable authority and resource constraints under transaction
  locks when concurrent requests could invalidate an earlier check.
- Use durable idempotency records for retryable operations. A response lost
  after commit must not repeat the economic effect.
- Use migrations for schema changes. Keep PostgreSQL and MySQL schema parity
  checks intact, while describing MySQL runtime support accurately.
- Database tests must use an isolated disposable database. Do not use host,
  beta or production databases as test fixtures.

See [`ENGINE.md`](ENGINE.md) for system boundaries and the
[`V3_GAMEPLAY_COMPLETION_TRACKER.md`](V3_GAMEPLAY_COMPLETION_TRACKER.md) for
acceptance evidence.
