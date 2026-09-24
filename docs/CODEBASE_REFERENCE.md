# twclone Codebase Module Reference

**Date**: 2026-03-03  
**Total Lines of Code**: ~71,891 (167 source files)  
**Languages**: C (primary), Python (test runner), JSON (protocol specs)

---

## I. MAIN SERVER MODULES (src/server_*.c)

### Connection & Dispatch
- **server_loop.c** (1,604 lines)  
  Main server event loop. TCP listener, client connection handler (per-thread), command dispatch table, TLS initialization. **Entry point for server binary.**

- **server_envelope.c** (1,265 lines)  
  JSON message framing. Request/response/error envelope construction, broadcast pump to clients, rate limit metadata injection.

- **server_auth.c** (not listed but present)  
  Login/logout, session token management, password verification, capability negotiation.

### Game Systems

#### Navigation & Movement
- **server_universe.c** (2,154 lines)  
  Sector topology, warp/transwarp logic, pathfinding, navigation aids (bookmarks, avoid lists, beacons).

- **server_autopilot.c** (8,555 lines in hold/)  
  Client-side autopilot path execution, route following, multi-hop navigation.

#### Ships & Cargo
- **server_ships.c** (943 lines)  
  Ship status, inspection, renaming, repair, self-destruct, towing. Cargo operations: transfer, deposit, withdraw, jettison.

#### Combat
- **server_combat.c** (1,126 lines)  
  Ship-to-ship, planet, and port attacks. Weapon system, damage events, FedSpace enforcement (Captain Z). Defense deployment (fighters, mines, limpets).

#### Trading & Economy
- **server_ports.c** (3,904 lines)  
  Port inventory, buy/sell orders, dynamic pricing, quantity restrictions, alignment-based legality. Port economy tick.

- **server_stardock.c** (3,015 lines)  
  Hardware/ship sales, shipyard restrictions, trade-in system, upgrade system.

#### Banking & Ledger
- **server_bank.c** (1,582 lines)  
  Personal accounts, deposits/withdrawals, transfers, standing orders, interest accrual. Corporate treasury.

#### Planets & Colonization
- **server_planets.c** (2,701 lines)  
  Land/launch, colonist transfer, resource growth, citadels (build/upgrade), Genesis torpedoes, outposts.

- **server_citadel.c** (8,507 lines)  
  Citadel construction, upgrade mechanics, defensive capabilities.

#### Corporations & Factions
- **server_corporation.c** (2,349 lines)  
  Corp creation, membership, CEO privileges, taxation, dividends, corporate banking. Faction-as-corporation model.

#### NPCs & Enforcement
- **server_police.c** (Not found in main; in hold/)  
  Bribery, surrender, law enforcement, wanted level mechanics.

#### Community & Social
- **server_communication.c** (2,002 lines)  
  Bounties, mail, news feed, tavern, noticeboard, player rankings, online lists. Notes and personal messaging.

#### Configuration & Sysop
- **server_config.c** (960 lines)  
  Load/parse config from database, live reload mechanism, validation.

- **server_sysop.c** (808 lines)  
  SysOp commands: config get/set/history, player search/kick, engine status, job management, logs, notices.

#### Utilities
- **server_clusters.c** (10,137 lines)  
  Cluster-based law/enforcement mechanics, zone configuration.

- **server_log.c** (6,385 lines)  
  Logging utilities (LOGD, LOGI, LOGW, LOGE macros).

- **server_bulk.c** (4,195 lines)  
  Bulk operations, batch command execution.

- **common.c** (various lines)  
  Shared utilities, JSON helpers, error codes.

---

## II. ENGINE MODULES (src/server_*.c engine-related)

- **server_engine.c** (1,484 lines)  
  Engine process startup, S2S connection, health checks, graceful shutdown. Orchestrates tick loop.

- **server_cron.c** (2,689 lines)  
  Durable cron scheduler. Task definitions: `daily_turn_reset`, `terra_replenish`, `port_reprice`, `planet_growth`, `fedspace_cleanup`, `npc_step`. Idempotent execution, watermark tracking.

---

## III. DATABASE & REPOSITORY LAYER (src/db/)

### Abstract Database API
- **db_api.c/h**  
  High-level database operations. Schema creation/migration. Connection pooling interface.

- **db_int.h**  
  Internal driver interface (plugin model for PostgreSQL, MySQL, etc.).

### SQL Utilities
- **sql_driver.c/h**  
  Parameterized query builders, placeholder conversion (`:1`, `:2` → dialect-specific).

### Repository Layer (Data Access Objects)

**Per-domain repositories** (`src/db/repo/repo_*.c`):

- **repo_players.c/h** — Player account queries
- **repo_ships.c/h** — Ship state and status
- **repo_cargo.c/h** — Cargo inventory
- **repo_corporation.c/h** — Corporation/faction data
- **repo_planets.c/h** — Planet data
- **repo_warp.c/h** — Sector topology
- **repo_items.c/h** — Generic item storage
- **repo_player_settings.c/h** — Player preferences
- **repo_commodities.c/h** — Commodity catalog
- **repo_cmd.c/h** — Commands table (engine→server)
- **repo_engine.c/h** — Engine process state
- **repo_engine_consumer.c/h** — Event consumption watermark
- **repo_s2s_peers.c/h** — S2S peer management
- **repo_cron.h** — Cron task scheduling
- **repo_market_dynamic.h** — Dynamic pricing engine
- **repo_sysop.c/h** — SysOp audit/config

### Backend Drivers (Dialect-Specific)

**PostgreSQL** (`src/db/pg/`):
- **db_pg.c** — PostgreSQL initialization, schema creation, dialect queries

**MySQL** (`src/db/mysql/`):
- **db_mysql.c** — MySQL initialization, schema creation, dialect queries

---

## IV. GAME LOGIC & RULES (src/)

- **game_db.c/h**  
  High-level game queries (business logic layer above repository).

- **schemas.c** (4,659 lines)  
  Schema definitions for protocol messages, request/response validation (JSON schema generation).

- **errors.h**  
  Error code definitions (ERR_HOLD_FULL, ERR_NOT_AUTHORIZED, etc.).

---

## V. INTER-PROCESS COMMUNICATION (src/)

- **s2s_transport.c** (located in hold/ and src/)  
  S2S protocol: length-prefixed JSON, HMAC authentication, health checks.

- **s2s_keyring.c/h** (located in hold/ and src/)  
  HMAC key management for S2S authentication.

---

## VI. UNIVERSE GENERATION (src/)

- **bigbang_pg_main.c** (1,286 lines)  
  "Big Bang" universe generator. Reads `bin/bigbang.json`, creates sectors, ports, planets, navigation graph. **Entry point for bigbang binary.**

- **test_bang.c**  
  Universe validation/testing tool.

---

## VII. PROTOCOL & SERIALIZATION (src/)

- **server_envelope.c** (listed above)  
  JSON envelope, error codes, rate limit headers.

- **schemas.c** (listed above)  
  JSON schema generation for all commands/responses.

- **ansi.h** (in hold/)  
  ANSI color codes (for terminal output).

---

## VIII. UTILITIES & INFRASTRUCTURE (src/)

- **common.c/h**  
  - Logging macros (LOGD, LOGI, LOGW, LOGE)
  - JSON helpers
  - String utilities
  - Error handling

- **globals.c**  
  Global variables (g_cfg, g_ssl_ctx, etc.)

---

## IX. TEST INFRASTRUCTURE (tests.v2/)

### Test Framework
- **json_runner.py**  
  Main test harness. Parses JSON test suites, connects to server, executes test steps, validates results.

- **twclient.py**  
  Python client library. JSON-RPC over TCP/TLS. Session management, command sending.

- **rig_db.py**  
  Test data seeding. Loads `test_rig.json` into database (idempotent upserts).

- **run_suites_all.py**  
  Master test runner. Executes all registered test suites.

### Test Data & Suites
- **test_rig.json**  
  Canonical test data (users, ships, sectors, planets, corporations, commodities).

- **macros.json**  
  Reusable test step sequences (login, warp, trade, etc.).

- **suite_*.json**  
  Individual test suites organized by feature (login, ships, trade, combat, bounties, etc.).

- **suite_regression_full.json**  
  Master regression suite including all sub-suites.

---

## X. DOCUMENTATION (docs/)

### Architecture & Design
- **ENGINE.md** — Game engine tick loop, cron scheduling, durable rails
- **DATABASE_RULES.md** — Architectural layers, SQL portability, schema rules
- **ARCHITECTURE_OVERVIEW.md** — High-level system design, communication patterns
- **GALACTIC_ECONOMY.md** — Market mechanics, dynamic pricing, Ferengi arbitrage

### Protocol Specification (PROTOCOL.v3/)
- **00_index.md** — Overview and directory
- **01-10_*.md** — Transport, framing, envelope, error handling, auth, handshake, S2S, events, limits
- **19_Ship_Commands.md** — ship.status, ship.inspect, cargo operations
- **20_Player_Commands.md** — Auth, profile, settings, session
- **21_Sector_and_Movement_Commands.md** — Warp, scan, pathfinding, navigation
- **22_Trade_and_Port_Commands.md** — Buy/sell, port queries
- **23_Combat_and_Weapons.md** — Attack, weapons, deployment
- **24_Planets_Outposts_Stations.md** — Land/launch, colonization, citadels
- **25_Corporations_and_Stock_Market.md** — Corp management, equity (partial)
- **26_Banking_and_Ledger.md** — Banking operations
- **27_NPC_and_Ferengi_AI.md** — NPC spawning, police, bribery
- **28_Tavern_Noticeboard_and_Community_Systems.md** — Bounties, notes, rankings
- **29_SysOp_Commands.md** — Admin operations

### Operational Guides
- **SYSOP_FIRST_TIME_SETUP.md** — First-time server setup (databases, build, Big Bang)
- **PRODUCTION_DEPLOYMENT_GUIDE.md** — Deployment, PgBouncer pooling, scaling
- **TLS_CONFIGURATION_GUIDE.txt** — TLS/SSL setup for encrypted connections
- **tls_implementation_summary.md** — TLS implementation details

### Supplementary
- **EVENT_CONTRACT.md** — Event/command durability guarantees
- **Intra-Stack_Protocol.md** — Internal (possibly legacy) protocol docs
- **MYSQL_COMPATIBILITY_DESIGN.md** — MySQL 8.0+ support design
- **SHIPTYPE_RESTRICTIONS.md** — Ship type limitations (data-driven)
- **SYSOP_MENU.md** — SysOp command menu structure
- **FEATURE_PARITY_ANALYSIS.md** — Feature checklist vs. TradeWars 2002

### Reports & Analysis
- **docs/reports/** — Phase-specific implementation reports (commodity system, dynamic pricing, ship cargo, etc.)
- **docs/reviews/** — Code review summaries, scalability analysis

---

## XI. BUILD & CONFIGURATION

### Autotools
- **configure.ac** — Autoconf configuration (PostgreSQL detection, library checks)
- **Makefile.am** / **bin/Makefile.am** — Automake build definitions
- Generated: **Makefile**, **config.h**, **configure**

### Runtime Configuration
- **bin/bigbang.json** — Universe generation config (sample provided)
- **data/menus.json** — Terminal client UI templates

---

## XII. AUXILIARY TOOLS

### Utilities
- **tools/** — Helper scripts, build utilities
- **ai_player/** — AI bots for testing/benchmarking

### Documentation Tools
- **hold/** — Archive of previous design docs and implementation notes

---

## III. CODEBASE STATISTICS

| Category | Count | Lines |
|----------|-------|-------|
| **Server modules** | ~15 | ~25,000 |
| **Database/Repository** | ~20 | ~10,000 |
| **Engine/Cron** | 2 | ~4,000 |
| **Protocol/Utilities** | ~10 | ~8,000 |
| **Universe generation** | 2 | ~1,500 |
| **Test framework** | ~6 (Python) | ~2,000 |
| **Schemas** | 1 | ~4,600 |
| **Documentation** | ~40 files | ~50,000 words |
| **Total** | **167 files** | **~71,891** |

---

## IV. KEY FILE LOCATIONS BY USE CASE

### "I want to add a new command"
1. Protocol spec: `docs/PROTOCOL.v3/XX_*.md`
2. Handler: `src/server_SYSTEM.c`
3. Registration: `src/server_loop.c` (command_entry_t table)
4. Tests: `tests.v2/suite_FEATURE.json`

### "I want to understand the economy"
1. Architecture: `docs/GALACTIC_ECONOMY.md`
2. Port mechanics: `src/server_ports.c`
3. Dynamic pricing: `src/db/repo/repo_market_dynamic.h`
4. Ferengi: See NPC step in `src/server_cron.c`

### "I want to trace a database operation"
1. Gameplay: `src/server_SYSTEM.c`
2. Repository: `src/db/repo/repo_*.c`
3. Driver: `src/db/pg/db_pg.c` or `src/db/mysql/db_mysql.c`
4. Schema: `src/db/db_api.c` (schema version bump)

### "I want to understand the engine"
1. Overview: `docs/ENGINE.md`
2. Main loop: `src/server_engine.c`
3. Cron jobs: `src/server_cron.c`
4. Event consumption: `src/db/repo/repo_engine_consumer.c`
5. Commands execution: `src/db/repo/repo_cmd.c`

### "I want to run tests"
1. Setup: `tests.v2/rig_db.py` (seed database)
2. Framework: `tests.v2/json_runner.py`
3. Data: `tests.v2/test_rig.json`
4. Suites: `tests.v2/suite_*.json`
5. Master: `tests.v2/run_suites_all.py`

### "I want to deploy"
1. Build: `make clean && make -j`
2. Database: `docs/SYSOP_FIRST_TIME_SETUP.md`
3. Universe: Run `./bin/bigbang`
4. Server: `./bin/server --host 0.0.0.0 --port 1234`
5. Scaling: `docs/PRODUCTION_DEPLOYMENT_GUIDE.md` (PgBouncer)
6. TLS: `docs/TLS_CONFIGURATION_GUIDE.txt`

---

## CONCLUSION

**twclone is a well-organized, production-grade codebase** with clear separation of concerns:
- **Gameplay logic** (server_*.c) separate from database (repo_*.c) separate from dialect SQL (db_pg.c, db_mysql.c)
- **Per-domain modules** (ships, combat, trading, planets, corporations)
- **Comprehensive test framework** (JSON suites, Python harness)
- **Extensive documentation** (architecture, protocol, operations)

For developers **adding features**:
1. Start with protocol spec in `docs/PROTOCOL.v3/`
2. Add handler in `src/server_SYSTEM.c`
3. Use repository layer for data access
4. Add tests in `tests.v2/suite_FEATURE.json`
5. Update documentation

The architecture supports this workflow seamlessly with **no refactoring required** for canonical game parity.

---

**Generated**: 2026-03-03  
**For questions**: Refer to `docs/ARCHITECTURE_OVERVIEW.md` or code comments
