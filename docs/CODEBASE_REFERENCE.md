# Codebase map

Use this map to find current code. It intentionally avoids file counts and
historical test totals, which become stale quickly.

| Concern | Location |
|---|---|
| Server startup, listeners and dispatch | `src/server_main.c`, `src/server_loop.c`, `src/server_*.c` |
| Gameplay handlers | `src/server_*.c` |
| Database abstraction and drivers | `src/db/db_api.*`, `src/db/pg/`, `src/db/mysql/` |
| Persistence repositories | `src/db/repo/` |
| Schema and migrations | `src/db/migrations/`, `src/db/schema/` |
| Universe initialization | `src/bigbang_pg_main.c`, `src/bigbang_config.c` |
| Python client | `client/python_client/` |
| Godot client | `client/godot_client/` |
| Protocol-level regression | `tests.v2/` |
| Python client tests | `client/python_client/tests/` |
| Godot test harness | `client/godot_client/` and its Godot project tests |
| GitHub feedback publisher | `tools/feedback_github_publisher.py` |

## Where to read contracts

- Client/server protocol: [`PROTOCOL.v3/00_index.md`](PROTOCOL.v3/00_index.md)
- Approved game rules: [`V3_GAMEPLAY_DECISIONS.md`](V3_GAMEPLAY_DECISIONS.md)
- Gameplay and acceptance status: [`V3_GAMEPLAY_COMPLETION_TRACKER.md`](V3_GAMEPLAY_COMPLETION_TRACKER.md)
- Database policy: [`DATABASE_RULES.md`](DATABASE_RULES.md)
- Engine responsibilities: [`ENGINE.md`](ENGINE.md)
- Deployment status: [`V3_STATUS.md`](V3_STATUS.md)

The protocol is authoritative for message shapes. Handler source is
authoritative for implementation details; acceptance reports establish only
the specific behavior actually exercised.
