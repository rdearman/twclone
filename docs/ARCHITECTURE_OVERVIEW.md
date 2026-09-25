# twclone Architecture & System Design Overview

**Date**: 2026-03-03  
**Purpose**: High-level architectural understanding for developers  
**Audience**: Architects, senior engineers planning feature additions

---

## I. SYSTEM ARCHITECTURE (50,000 ft view)

```
┌─────────────────────────────────────────────────────────────────┐
│                         PLAYERS (CLIENTS)                        │
│         (Terminal client, Web client, AI bots, etc.)            │
└────────────┬────────────────────────────────────────────────────┘
             │ JSON-RPC over TCP/WebSocket (port 1234)
             │ TLS optional (configurable)
             │
┌────────────▼────────────────────────────────────────────────────┐
│                    GAME SERVER (Single Process)                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ - Client connection handler (multi-threaded)             │   │
│  │ - Command dispatcher & validation                        │   │
│  │ - Event emitter (facts to engine)                        │   │
│  │ - Command executor (mutations from engine)               │   │
│  │ - Broadcast pump (real-time to clients)                 │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                   │
│  Languages: C (server_loop.c, server_*.c)                       │
│  Concurrency: Multi-threaded per-client                         │
└────┬──────────────────────────┬──────────────────────────────────┘
     │ S2S Protocol             │ PostgreSQL/MySQL
     │ (TCP, HMAC-auth,         │ (source of truth)
     │  config updates)         │
     │                          │
┌────▼──────────┐     ┌────────▼──────────────────────────┐
│ GAME ENGINE   │     │ DATABASE                          │
│ (Forked       │     │  ┌─────────────────────────────┐  │
│  process)     │     │  │ events (server→engine)      │  │
│               │     │  │ commands (engine→server)    │  │
│ - Tick loop   │     │  │ cron_tasks (scheduler)      │  │
│ - NPC logic   │     │  │ config (live reload)        │  │
│ - Economy     │     │  │ [sectors, ships, ports,     │  │
│ - Cron jobs   │     │  │  planets, players, corps]   │  │
│ - Enforcement │     │  └─────────────────────────────┘  │
└───────────────┘     └────────────────────────────────────┘
```

### Key Design Principles

1. **Separation of Concerns**: 
   - Server = Client I/O + Command validation
   - Engine = Background simulation + Enforcement
   - Database = Source of truth (all state)

2. **Durable Rails** (Event sourcing-lite):
   - Server emits `events` (facts that happened)
   - Engine consumes `events`, produces `commands` (mutations)
   - Both sides read back from DB; resilient to crashes

3. **Single Source of Truth**: Database
   - No in-memory game state (except connection/client context)
   - Server can restart; game continues
   - Engine can restart; game continues
   - Both fail gracefully if other is down (DB polling fallback)

4. **Data-Driven Configuration**:
   - Game parameters (turnsperday, interest rates, port behavior) in DB
   - Live reload via config_version bump (no restart needed)
   - All economic rules stored as data, not hardcoded

---

## II. COMMUNICATION PATTERNS

### A. Client ↔ Server (JSON-RPC)

**Protocol**: NDJSON (newline-delimited JSON)  
**Transport**: TCP/WebSocket (port 1234)  
**Security**: TLS optional; HMAC tokens for session auth

**Message Flow**:
```
Client sends request (command):
  {"cmd": "move.warp", "seq": 123, "args": {"sector_id": 42}}

Server validates & executes:
  - Check authorization
  - Validate args
  - Execute game logic
  - Emit event (if state-changing)
  - Build response

Server sends response:
  {"id": "<req_id>", "seq": 123, "type": "move.warp.resp", 
   "data": {"ship_location": 42}, "rate_limit": {...}}

Server may broadcast event to other clients:
  {"type": "player.moved.v1", "data": {
    "player_id": 5, "from_sector": 41, "to_sector": 42, ...}}
```

**Rate Limiting**:
- Per-command quotas (configurable)
- Rate limit metadata in response header
- Graceful rejection if exceeded

---

### B. Server ↔ Engine (S2S Protocol)

**Protocol**: HMAC-authenticated length-prefixed JSON  
**Transport**: TCP (port 4321, internal)  
**Auth**: HMAC-SHA256 (keys in `s2s_keys` table)

**Message Types**:
- `s2s.health.check`: Heartbeat (every 5s)
- `s2s.config.bump`: Live reload notification
- `s2s.shutdown`: Graceful engine termination
- `s2s.engine.nudge`: "Wake up and check for work"

**Failure Modes**:
- If S2S TCP fails > breaker_open_ms: Server logs DEGRADED
- Engine continues via DB polling (slower but safe)
- Commands still reach engine via `commands` table
- No loss of functionality; just higher latency

---

### C. Engine ↔ Database (Durable Rails)

**Events** (Server → Engine fact journal):
```sql
CREATE TABLE events (
  id INTEGER PRIMARY KEY,
  ts INTEGER,              -- UTC epoch
  type TEXT,               -- e.g., "player.moved.v1"
  payload JSON,            -- Full event data
  idem_key TEXT UNIQUE,    -- Deduplication key
  actor_player_id INTEGER
);
```

**Consumption**:
- Engine reads `id ASC` starting from `engine_offset.last_event_id`
- Processes in batches (configurable)
- On success: updates `engine_offset.last_event_id`
- On failure: moves to `events_deadletter` (quarantine)

**Idempotency**:
- Each event has unique `idem_key`
- Re-running same event with same key = no-op
- Safe for retries on network failure

**Commands** (Engine → Server mutation journal):
```sql
CREATE TABLE commands (
  id INTEGER PRIMARY KEY,
  type TEXT,               -- e.g., "npc.spawn.v1"
  payload JSON,
  status TEXT,             -- ready|running|done|failed
  priority INTEGER,
  due_at INTEGER,          -- When to execute
  idem_key TEXT UNIQUE
);
```

**Execution**:
- Server reads `status='ready'` rows
- Sets `status='running'`, marks `started_at`
- Executes mutation (NPC spawn, port restock, etc.)
- Sets `status='done'`, marks `finished_at`
- On failure: increments `attempts`, resets `due_at` (backoff)

---

## III. DATABASE SCHEMA (Key Tables)

### Core Game State

| Table | Purpose | Key Fields |
|-------|---------|-----------|
| `players` | Player accounts | id, username, alignment, net_worth, score |
| `ships` | Player/NPC ships | id, owner_id, sector_id, cargo_holds, location |
| `ship_cargo` | Cargo inventory | ship_id, commodity, quantity |
| `sectors` | Universe topology | id, x, y, warps (JSON) |
| `ports` | Trading locations | id, sector_id, inventory, bank_balance |
| `port_inventory` | Per-commodity stock | port_id, commodity, quantity |
| `planets` | Colonies | id, sector_id, owner_id, class, colonists |
| `corporations` | Factions & corps | id, name, tag, treasury, members |

### Transactional History

| Table | Purpose |
|-------|---------|
| `bank_accounts` | Player/corp bank balances |
| `bank_transactions` | Transfer audit trail |
| `orders` | Port buy/sell orders |
| `trade_transactions` | Buy/sell history |

### Engine & Control

| Table | Purpose |
|-------|---------|
| `events` | Server→Engine event journal |
| `commands` | Engine→Server mutation queue |
| `engine_offset` | Event consumption watermark |
| `cron_tasks` | Background job schedule |
| `config` | Live-tunable parameters |
| `config_version` | Reload trigger counter |
| `config_audit` | Change history |

### Durable Execution

| Table | Purpose |
|-------|---------|
| `events_deadletter` | Failed/poison events |
| `engine_audit` | Mutation effects log |
| `sessions` | Active client connections |

---

## IV. REQUEST LIFECYCLE (Example: Move Warp)

```
1. CLIENT sends:
   {"cmd": "move.warp", "sector_id": 42}

2. SERVER receives (server_loop.c):
   - Connection thread calls process_message()
   - Dispatches to cmd_move_warp() handler

3. VALIDATION (server_cmds.c):
   - Is player authenticated? ✓
   - Is sector_id valid? ✓
   - Is target sector adjacent? ✓
   - Does player have fuel? ✓
   - Any hazards? (mines, enemies, etc.)

4. EXECUTION (server_universe.c):
   - Move ship to new sector
   - Update ships table: sector_id = 42
   - Check for hazards
   - Emit event: "player.moved.v1"

5. EVENT JOURNAL (database):
   INSERT INTO events (type, payload, idem_key) 
   VALUES ('player.moved.v1', {...}, 'move_<ship>_<ts>')

6. RESPONSE to client:
   {"type": "move.warp.resp", "data": {"sector_id": 42, ...}}

7. BROADCAST to other clients:
   {"type": "player.moved.v1", "data": {
     "ship_id": 5, "from_sector": 41, "to_sector": 42, ...}}

8. ENGINE (background):
   - Reads event from events table
   - Checks for hazards (mines, limpets, enemies)
   - Emits events for hazard encounters
   - May execute cron jobs (planet growth, etc.)
```

---

## V. ARCHITECTURAL LAYERS & BOUNDARIES

### Hard Boundaries (Non-Negotiable)

1. **Gameplay Logic ↔ Database Layer**
   - Gameplay code (server_*.c) never contains SQL
   - All DB access goes through `repo_*.c` (repository layer)
   - `src/db/` handles dialect differences (PostgreSQL vs MySQL)

2. **Transport ↔ Protocol**
   - `server_envelope.c` handles JSON serialization
   - `server_loop.c` handles TCP/WebSocket sockets
   - Protocol-agnostic business logic in `server_*.c`

3. **Server ↔ Engine**
   - Durable DB rails: events table (server writes), commands table (engine reads)
   - S2S control link for health/config (graceful degradation if fails)
   - No direct function calls between processes

4. **Engine ↔ Database**
   - Event consumption: read-only + watermark update
   - Command execution: read, execute, write status
   - Cron scheduling: read due tasks, execute, update next_due

### Soft Boundaries (Flexible)

- **Per-command validation**: Can move between client and server
- **Rate limiting**: Can move between transport and protocol layers
- **Caching**: Can add in-memory caches if needed (but flushes on reload)

---

## VI. CONCURRENCY & THREADING MODEL

### Server Concurrency

**Model**: Multithreaded per-client (NOT thread pool)

```c
main_loop():
  while (true):
    fd = accept(listener_socket)          // Block until client connects
    ctx = new client_ctx_t(fd)            // Create context
    pthread_create(connection_thread, ctx)  // Spawn per-client thread

connection_thread(ctx):
  while (true):
    line = readline(ctx->fd)              // Block until command
    msg = json_parse(line)
    dispatch(msg)                         // Handle command
    send_response(ctx->fd)
```

**Safety**:
- Each client has its own context + file descriptor
- DB connection: per-thread via thread-local storage (if needed)
- Shared state (config, engine connection): protected by mutexes
- No race conditions on game state (all in DB)

**Scaling**:
- 100+ concurrent clients: 100+ threads (lightweight; OS handles)
- Each thread blocks on socket I/O when idle
- No busy loops or polling

---

## VII. KEY DESIGN PATTERNS

### 1. Repository Pattern (Abstraction)

```c
// src/db/repo/repo_players.c
int h_player_get_by_id(int player_id, player_t *out) {
  // Hides SQL dialect differences
  // PostgreSQL: SELECT * FROM players WHERE id = $1
  // MySQL: SELECT * FROM players WHERE id = ?
  // Returns standard C struct
}
```

**Benefit**: Game logic never sees SQL; portable across databases

---

### 2. Event Sourcing (Durability)

```c
// server_universe.c (gameplay logic)
h_ship_warp(ship_id, sector_id);        // Updates DB

// server_loop.c (event emitter)
json_t *event = json_object();
json_object_set(event, "type", "player.moved.v1");
h_events_insert(event);  // Durable audit trail
```

**Benefit**: Replay, recovery, audit trail, engine consumption

---

### 3. Idempotency Keys (Safety)

```c
// Every state-changing operation includes idem_key
char idem_key[64];
snprintf(idem_key, sizeof(idem_key), "buy_%d_%d_%ld", 
         ship_id, port_id, time(NULL));

// DB UNIQUE constraint prevents duplicate execution
INSERT INTO events (type, payload, idem_key) 
VALUES ('trade.buy.v1', {...}, idem_key)
ON CONFLICT (idem_key) DO NOTHING;
```

**Benefit**: Retries are safe; no duplication on network failure

---

### 4. Live Configuration (Data-Driven)

```c
// Load at startup + reload on version bump
server_config_t g_cfg = load_config_from_db();

// Update config in database (no restart):
UPDATE config SET value = '8' WHERE key = 'turnsperday';
UPDATE config_version SET version = version + 1;

// Server detects version change, reloads:
if (g_cfg.config_version != db_config_version):
  g_cfg = reload_config_from_db();
```

**Benefit**: Game tuning without downtime

---

### 5. Atomic Transactions (Consistency)

```c
// All state-changing operations atomic:
BEGIN TRANSACTION;
  UPDATE ships SET sector_id = 42 WHERE id = 5;
  UPDATE port_inventory SET quantity = quantity - 100 
    WHERE port_id = 1 AND commodity = 'ORE';
  INSERT INTO events (type, payload) VALUES (...);
COMMIT;  // All-or-nothing
```

**Benefit**: No partial updates; no data corruption

---

## VIII. PERFORMANCE CHARACTERISTICS

### Request Latency
- **Simple query** (ship.status): ~10-50ms (DB round-trip)
- **Complex operation** (trade.buy): ~50-200ms (validation + inventory update)
- **Bottleneck**: Database I/O (not CPU or networking)

### Database Load
- **100 concurrent players**: ~25-50 active DB connections (via PgBouncer)
- **Event consumption**: ~1000 events/sec per engine
- **Port repricing** (daily cron): ~15 seconds for 500 ports

### Scaling Limits
- **Native PostgreSQL**: 100+ connections, unlimited transactions
- **PgBouncer pooling**: Unlimited concurrent players (connection pool multiplexing)
- **Multi-server**: Not implemented (would need load balancer + session affinity)

---

## IX. EXTENSIBILITY POINTS

### Adding a New Command

1. **Define protocol** (docs/PROTOCOL.v3/XX_*.md)
2. **Add handler** (src/server_SYSTEM.c):
   ```c
   int cmd_feature_action(client_ctx_t *ctx, json_t *root) {
     // Validate args
     // Check permissions
     // Execute logic (via repo layer)
     // Emit event if state-changing
     // Return response
   }
   ```
3. **Register** (src/server_loop.c command table)
4. **Add tests** (tests.v2/suite_FEATURE.json)

### Adding Database State

1. **Define schema** (src/db/db_api.c)
2. **Add migration** (auto-created at startup)
3. **Wrap in repository** (src/db/repo/repo_NEW.c)
4. **Use in gameplay** (src/server_*.c via repo)

### Adding Cron Job

1. **Implement handler** (src/server_cron.c)
2. **Register** (src/server_engine.c, cron_tasks table)
3. **Set schedule** (e.g., `daily@12:00Z` or `every:5m`)

---

## X. OPERATIONAL CONSIDERATIONS

### Monitoring Points

| What | How | Threshold |
|------|-----|-----------|
| **Engine lag** | `events` table size | > 10K unprocessed |
| **Command queue** | `commands` table with status='ready' | > 1K |
| **DB connections** | `pg_stat_activity` count | > 80 (of 100 max) |
| **Deadletter size** | `events_deadletter` | > 100 (indicates bugs) |
| **Config reloads** | `config_audit` changes | Should be rare |

### Backup Strategy

- **Database**: pg_dump every 4 hours
- **Config**: Audit trail in `config_audit` table
- **Engine state**: Persistent via `cron_tasks` watermarks

### Disaster Recovery

1. Restore database from backup
2. Restart server (loads config from DB)
3. Restart engine (reads events from watermark)
4. Game resumes from point of backup (no loss of command state)

---

## XI. FUTURE ARCHITECTURAL IMPROVEMENTS

### Potential Enhancements

1. **Sharding**: Split universe into independent clusters; each with own server+engine
2. **Multi-engine**: Multiple engine processes (one per cluster) consuming same event journal
3. **Event streaming**: Kafka instead of DB table (for high-volume analytics)
4. **Caching layer**: Redis for frequently-queried data (config, player stats)
5. **Observability**: Prometheus metrics + distributed tracing

### Backward Compatibility

All improvements must:
- Keep durable rail semantics (events, commands, idempotency)
- Preserve repository layer abstraction
- Not require client protocol changes

---

## CONCLUSION

**twclone Architecture** is built for:
- **Resilience**: Durable rails survive restarts
- **Scalability**: Separation of server/engine; connection pooling
- **Maintainability**: Clean layers; data-driven rules
- **Extensibility**: Easy to add commands, jobs, database state
- **Operational Simplicity**: Live config, audit trails, graceful degradation

**Key Success Factor**: Database as source of truth (not in-memory state) enables robust, scalable distributed simulation.

For canonical game parity, the architecture is solid; focus is on **feature implementation**, not refactoring.

---

**Questions?** Refer to `docs/ENGINE.md`, `docs/DATABASE_RULES.md`, or code comments in `server_loop.c` and `server_cron.c`.
