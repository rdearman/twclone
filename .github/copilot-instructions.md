# Copilot Instructions for twclone

Essential context for working effectively in this codebase.

## Project Overview

**twclone** is a modern C-based space-trading game (BBS-era TradeWars 2002 remake) with:
- **Headless server**: Multi-player JSON protocol, player state management
- **Terminal client**: Built-in test client
- **Game Engine**: Separate forked process managing simulation, tick loops, economy, NPCs, enforcement
- **PostgreSQL backend**: Primary database engine (MySQL 8.0+ supported via abstraction layer)
- **Architecture**: Client ↔ Server (state/commands) ↔ Database ↔ Engine (tick loop/simulation)

## Build, Test, Lint

### Build
```bash
make clean && make -j
# Artifacts: ./bin/server, ./bin/bigbang, ./bin/client
```

Verbose:
```bash
make V=1
```

**Prerequisites**: GCC/Clang, GNU make, PostgreSQL dev libraries (`libpq-dev` on Ubuntu), POSIX (Linux/WSL/macOS)

### Universe Generation ("Big Bang")
```bash
./bin/bigbang
```
Creates sectors, ports, and universe graph. Reads configuration from `bin/bigbang.json` (copy and edit the sample).

### Run Server
```bash
./bin/server --host 0.0.0.0 --port 1234
```
Logs to `./twclone.log`.

### Run Client
```bash
./bin/client --host localhost --port 1234 --menus ./data/menus.json
```

### Run Tests
Full suite (auto-seeds database, runs all tests):
```bash
python3 tests.v2/run_suites_all.py
```

Single test file:
```bash
python3 tests.v2/json_runner.py tests.v2/suite_NAME.json
```

**Test data management**: Centralized in `tests.v2/test_rig.json` (idempotent setup via `rig_db.py`). Do NOT write SQL in test files; add test data to `test_rig.json` instead.

### Database Setup
- PostgreSQL required (or MySQL 8.0+ with adapter layer)
- Connection string in `bin/bigbang.json` or environment
- Schema auto-created at first run
- Reset: Drop DB, restart server or `./bin/bigbang` to re-seed

## High-Level Architecture

### Core Layers

**1. Client Protocol** (`src/server_loop.c`)
- TCP socket listener, JSON request/response envelopes
- Session management, authentication, player state
- Broadcasts to connected clients

**2. Database Abstraction** (`src/db/`)
- `db_api.h/c`: High-level API (queries, mutations)
- `db_int.h`: Internal driver interface
- `pg/db_pg.c`, `mysql/db_mysql.c`: Pluggable drivers
- `sql_driver.c/h`: Query utilities
- **All queries parameterized** (no string concatenation of values)

**3. Game Engine** (Separate Process)
- Short-tick loop (~250–1000 ms, config: `engine.tick_ms`)
- Consumes **events** table (server → engine facts)
- Runs **cron_tasks** (daily turns, market repricing, planet growth, NPC steps)
- Produces **commands** table (engine → server mutations)
- TCP S2S link (health checks, config bumps, shutdown)
- Database is **source of truth**; TCP reduces latency

**4. Configuration** (DB-backed, live-reload)
- Table `config`: typed key/values
- Table `config_version`: version counter for reload triggers
- Table `s2s_keys`: HMAC secrets (Server ↔ Engine auth)
- Seeded from `bin/bigbang.json` on first run

### IPC & Durability

**Events** (`events` table):
- Server writes facts (player actions, illegal activity, etc.)
- Engine reads in `id ASC` order (idempotency via `idem_key`)
- Watermark tracked in `engine_offset`

**Commands** (`commands` table):
- Engine writes mutations (ship damage, port restock, planet growth)
- Server reads and applies

**Real-time**:
- TCP S2S link carries health checks, nudges, config version bumps, shutdown signals
- Length-prefixed JSON; HMAC-SHA256 authenticated

## Database Architecture & Rules

### DB-Agnostic Design Principle

"Database-agnostic" means:
- **Gameplay logic is completely unaware of database dialects**
- **All dialect differences isolated to the DB driver layer** (`src/db/pg/*`, `src/db/mysql/*`)
- **Adding a new backend is additive, not invasive**
- **No `if (db == X)` branches outside DB code**

This is **PostgreSQL-first**, with **explicit MySQL 8.0+ support** via drivers.

### Architectural Layers (Strict Dependency Order)

| Layer | Location | May Depend On | Must NOT Depend On |
|-------|----------|---------------|-------------------|
| **Domain/Gameplay** | `src/*.c`, `src/server_*` | repo_* APIs | SQL, db_api, drivers |
| **Repository Layer** | `src/db/repo/*.c` | db_api, db_int, sql_driver | Backend drivers, dialect SQL |
| **DB API & Dialect** | `src/db/db_api.*`, `src/db/sql_driver.*` | Backend drivers | Gameplay logic |
| **Backend Drivers** | `src/db/pg/*`, `src/db/mysql/*` | DB SDK only | Anything above |

**Violating layer boundaries is automatic rejection.**

### Repository SQL Rules (Portable Core Only)

**ALLOWED:**
- ANSI SELECT / INSERT / UPDATE / DELETE
- WHERE, JOIN, GROUP BY, ORDER BY
- LIMIT, basic aggregates
- Parameterized queries (`:1`, `:2` → driver converts)

**EXPLICITLY FORBIDDEN in `src/db/repo/**`:**
- Dialect-specific clauses: `RETURNING`, `ON CONFLICT`, `ON DUPLICATE KEY`
- Backend operators: `ILIKE`, `::type`
- Backend functions: `NOW()`, `to_timestamp()`, `FROM_UNIXTIME()`, `EXTRACT()`
- Locking: `FOR UPDATE`, `SKIP LOCKED`
- JSON operators/functions

**Time handling rule:** Time values **come from C** (via `time()`, `strftime()`, etc.), never from SQL `NOW()`. Columns may use native time types (PostgreSQL `timestamptz`, MySQL `TIMESTAMP`), but the **value** is produced in C and bound via driver conversions.

### Boolean Columns

**Schema:** Native boolean types
- PostgreSQL: `BOOLEAN`
- MySQL: `BOOLEAN` (or `TINYINT(1)`)

**Queries:** All comparisons must be **explicit**
- **Allowed:** `WHERE active = TRUE`, `WHERE is_admin = FALSE`
- **Forbidden:** `WHERE active`, `WHERE active = 1`, `WHERE active != 0`

Intent must be explicit; no implicit truthiness.

### Idempotency & Keys

- All commands/events include `idem_key` (unique across retries)
- Events/commands with same `idem_key` execute **once only**
- Engine watermark (`engine_offset`) persists last processed event ID

### Dynamic SQL

- **No manual SQL concatenation** (no `sprintf` SQL assembly)
- **No SQL injection risk** via string building
- All dynamic predicates use `sql_build()` utilities

### Adding a New Database Backend

**Required components** (all mandatory; missing any = rejection):
1. Driver implementation (`src/db/<backend>/db_<backend>.c`)
2. Dialect support (placeholder conversion in `sql_build`, all primitives)
3. Schema DDL producing equivalent tables, indexes, constraints
4. Migration scripts (deterministic, repeatable)
5. Full regression suite passes unchanged
6. Compatibility declaration (documented deviations)

**Must not:** change gameplay logic, add `if (db == X)` branches outside DB code, or modify repository SQL.

## Key Conventions

### C Code
- **Headers**: `#ifndef SRC_MODULE_H / #define SRC_MODULE_H / ... / #endif`
- **Error handling**: Check return codes, log with context (WARN/ERROR severity)
- **Memory**: No leaks on error paths; use `malloc`/`free` or context-aware allocators
- **Logging**: `server_log(LEVEL, "format %s", var)` → `twclone.log`

### JSON Protocol
All message envelopes include: `id`, `type`, `seq`, `error`, `data`

Client request:
```json
{"type": "command.name", "seq": 123, "payload": {...}}
```

Server response:
```json
{"id": "<request_id>", "type": "command.name.resp", "seq": 123, "data": {...}}
```

Error:
```json
{"id": "<request_id>", "error": {"code": "ERR_CODE", "message": "..."}}
```

### Test Framework (`tests.v2/`)
- **Test data**: Centralized in `test_rig.json` (users, ships, sectors, planets, corporations)
- **Test suites**: JSON files (`suite_*.json`) or Python scripts (`suite_*.py`)
- **Macros**: Reusable step sequences in `macros.json`
- **Runner**: `json_runner.py` parses JSON, connects to server, executes steps, validates results
- **Rig setup**: `rig_db.py` seeds database from `test_rig.json` (idempotent upserts)
- **No raw SQL in tests**: All test data defined in `test_rig.json`

### Directory Layout
```
src/
  ├─ server_loop.c       # Main server listener, command dispatch
  ├─ db/                 # Database abstraction & drivers
  │  ├─ db_api.c/h       # High-level database operations
  │  ├─ repo/            # Repository layer (portable SQL)
  │  ├─ pg/db_pg.c       # PostgreSQL driver
  │  ├─ mysql/db_mysql.c # MySQL driver
  │  └─ sql_driver.c/h   # Query utilities
  ├─ common.c/h          # Shared utilities (logging, JSON helpers)
  ├─ errors.h            # Error codes & definitions
  └─ game_db.c/h         # Game logic database queries
bin/
  ├─ Makefile.am         # Build definitions
  ├─ server              # Compiled server binary
  ├─ bigbang             # Compiled universe generator
  └─ client              # Compiled test client
data/                    # menus.json (UI templates)
docs/
  ├─ ENGINE.md           # Engine design, tick loop, cron tasks
  ├─ DATABASE_RULES.md   # Architectural layering, SQL rules
  ├─ PROTOCOL.v3/        # Complete protocol spec (requests, responses, all commands)
  └─ *.md                # Design docs (deployment, security, compatibility)
tests.v2/
  ├─ test_rig.json       # Canonical test data (users, ships, sectors, etc.)
  ├─ rig_db.py           # Loads test_rig.json into database
  ├─ json_runner.py      # Test suite executor
  ├─ suite_*.json        # JSON test definitions
  └─ macros.json         # Reusable test steps
```

## Common Tasks

### Adding a New Player Command
1. Document request/response in `docs/PROTOCOL.v3/` (see `20_Player_Commands.md` as template)
2. Add handler in `server_loop.c`: check auth, validate args, emit **event**
3. If engine processing needed: add **cron_task** or **npc_step** handler in engine
4. Add test to `tests.v2/suite_commands.json` (use existing `test_rig.json` data via macros)

### Adding Database Table or Column
1. Add schema definition to `src/db/db_api.c` (schema version bump function)
2. Update both `pg/db_pg.c` and `mysql/db_mysql.c` if driver-specific SQL differs
3. Server auto-creates missing tables on startup
4. Test with `python3 tests.v2/run_suites_all.py`

### Debugging
- **Server logs**: `tail -f twclone.log`
- **Database state**: `psql -U twclone_user -d twclone_db -c "SELECT * FROM <table> LIMIT 10;"`
- **Single test**: `python3 tests.v2/json_runner.py tests.v2/suite_commands.json` with verbose output
- **Inspect test setup**: Check `test_rig.json` for data assumptions

## Important Constraints

- **Single-threaded server**: Event loop; no race conditions, but blocking calls block all players
- **Deterministic BigBang**: Universe seeded deterministically (reproducible for tests)
- **Idempotency via key**: Events/commands with same `idem_key` execute once (safe retries)
- **Database is source of truth**: Engine watermarks read from DB; no in-memory state survives restart
- **Live config reload**: Bump `config_version` to reload config in running server & engine (no restart)
- **Password security**: Currently plaintext; target: Argon2id hashes + rate limiting (see roadmap)
- **S2S auth**: HMAC-SHA256; keys in `s2s_keys` table; never logged
- **Test isolation**: Tests use unique IDs/usernames (e.g., `_<timestamp>` suffix) to avoid collisions

## Documentation Reference

- **README.md**: Quick start, project layout
- **docs/ENGINE.md**: Engine design, tick loop, durable rails (events/commands), S2S protocol
- **docs/DATABASE_RULES.md**: Architectural layering, SQL portability rules, backend support requirements
- **docs/PROTOCOL.v3/**: Complete message specs, auth, error codes, all command types
- **docs/PRODUCTION_DEPLOYMENT_GUIDE.md**: TLS, PgBouncer pooling (unlimited concurrency)
- **tests.v2/README_RIG.md**: Test rigging system, adding test data

## Global Guardrails (Non-Negotiable)

### HARD CONSTRAINTS
1. No unrelated refactors
2. No file renames
3. No new .md files unless explicitly requested
4. No new directories unless explicitly requested
5. Do not modify existing tests unless explicitly requested
6. All new tests must be JSON in `tests.v2/`
7. Do not add helper scripts (.sh, .py)
8. No random numbers—all logic must be deterministic
9. No floating point arithmetic—integer math only
10. Do not add abstractions or wrappers unless explicitly required
11. Do not introduce new dependencies
12. Do not move code between files
13. Default config must preserve current behavior
14. If unsure, ask instead of inventing architecture

### FILE CREATION POLICY
- Create only files explicitly named in the task
- Documentation: update existing docs if possible; if a new report is required, create **exactly ONE** file under `docs/reports/`
- No changelog, design docs (unless requested), or session summaries

### CHANGE SCOPE RULE
Changes limited to:
- Exact files named in the task
- Corresponding Makefile entries if necessary
- **Stop if touching >5 files; explain why first**

### ARCHITECTURE STABILITY
Project properties (preserved):
- Deterministic (no randomness)
- Data-driven
- Config-gated
- Backward compatible by default

**Do NOT introduce:**
- New service layers
- Caching systems
- Event buses
- Plugin systems
- New transaction models

Unless explicitly instructed.

### ECONOMIC ENGINE RULES
- All multipliers integer-scaled (100 = 1.0x)
- All price calculations remain symmetric (buy/sell)
- All new behavior gated by existing config flags
- Lawless cluster invariants preserved
- No randomness in pricing or decay

### TEST REQUIREMENTS
Every gameplay change requires:
- New JSON test suite (in `tests.v2/`)
- At least one negative test
- Regression manifest update
- Deterministic setup (no hardcoded sector IDs)
- **Stop if tests not possible; explain why**

### NO CLEANUP POLICY
Do NOT:
- Reformat code
- Reorder includes
- Remove unused variables
- Update unrelated comments
- Replace logic with "cleaner" alternatives

Only implement the requested change.

### OUTPUT FORMAT
1. Summary of changes (5 lines max)
2. List of files modified
3. Unified diff blocks only

**Do not assume or invent behavior. Ask clarifying questions if intent is ambiguous.**
