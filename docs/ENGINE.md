# ENGINE.md — Game Engine (Forked Process)

## 1) Purpose & scope

Run background game logic outside the player server: clocks/timers, economy drift, planetary growth, FedSpace maintenance, NPC scaffolding, and sanctions—communicating via durable DB rails and a TCP control link.

**Out of scope:** inter-server federation; rich NPC AI beyond stubs; client UX.

## 2) Roles & boundaries

* **Server (players)**: sessions/auth; command validation; fast reads; emits **events** (facts); executes **commands** (mutations); broadcasts to clients.
* **Engine (simulation)**: short-tick loop; cron scheduler; economy & growth; NPC loop; sanctions; writes **system notices**; enqueues **commands**.

**DB is the source of truth**. TCP link reduces latency (health/nudges/config bump/shutdown).

---

## 3) Process & lifecycle

### Startup (engine)

1. Open DB (WAL, busy_timeout).
2. Seed/validate DB-backed config; load `config_version`.
3. Read watermark (`engine_offset`).
4. Connect to server’s TCP S2S endpoint; `s2s.health.check`.
5. Enter tick loop (poll socket with timeout until next due job).

### Graceful shutdown

On `s2s.engine.shutdown` or signal:

* Stop claiming new work → finish current batch → persist watermark → close DB → exit 0.

### Degraded mode (S2S down)

If TCP S2S fails beyond `s2s.breaker_open_ms`, continue without nudges (DB polling only) and log **DEGRADED** at the metrics interval until recovered.

---

## 4) Scheduling model

### 4.1 Short-tick loop

* Interval: 250–1000 ms (config `engine.tick_ms`).
* Per tick (bounded):

  * Consume **events** in `id ASC` batches.
  * Run **sweepers** (TTL cleanup, auto-uncloak).
  * Step **NPCs** (`npc_step_batch`).
  * Run due **cron_tasks** (4.2).

### 4.2 Cron tasks (durable)

`cron_tasks(id, name UNIQUE, schedule, last_run_at, next_due_at, enabled, payload JSON)`

**Schedules**: `every:Ns|Nm` (aligned; no drift) and `daily@HH:MMZ` (UTC wall-clock).
**Seeded**:

* `daily_turn_reset` → `daily@03:00Z`
* `terra_replenish` → `daily@04:00Z`
* `port_reprice` → `daily@05:00Z`
* `planet_growth` → `every:10m`
* `fedspace_cleanup` → `every:1m`
* `autouncloak_sweeper` → `every:1m`
* `traps_process` → `every:1s`
* `npc_step` → `every:2s`
* `broadcast_ttl_cleanup` → `every:5m`

**Idempotency**: after run, set `last_run_at=now` and recompute `next_due_at` deterministically. If engine was down past a due time, **catch up once** on next start.

---

## 5) Durable rails & tables

### 5.1 Events (server→engine)

```sql
CREATE TABLE IF NOT EXISTS events(
  id INTEGER PRIMARY KEY,
  ts INTEGER NOT NULL,                 -- UTC epoch
  type TEXT NOT NULL,                  -- e.g., player.illegal.v1
  actor_player_id INTEGER,
  sector_id INTEGER,
  payload JSON NOT NULL,
  idem_key TEXT
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_events_idem ON events(idem_key) WHERE idem_key IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts);
CREATE INDEX IF NOT EXISTS idx_events_actor_ts ON events(actor_player_id, ts);
CREATE INDEX IF NOT EXISTS idx_events_sector_ts ON events(sector_id, ts);

CREATE TABLE IF NOT EXISTS engine_offset(
  key TEXT PRIMARY KEY,                -- 'events'
  last_event_id INTEGER NOT NULL,
  last_event_ts INTEGER NOT NULL
);
```

**Consumption**: strictly `id ASC`.
**Watermark**: updated after each batch commit.
**Poison/quarantine**: on repeated handler failures, move to `events_deadletter`:

```sql
CREATE TABLE IF NOT EXISTS events_deadletter(
  id INTEGER PRIMARY KEY,
  ts INTEGER NOT NULL,
  type TEXT NOT NULL,
  payload JSON NOT NULL,
  error TEXT NOT NULL,
  moved_at INTEGER NOT NULL
);
```

### 5.2 Commands (engine→server)

```sql
CREATE TABLE IF NOT EXISTS commands(
  id INTEGER PRIMARY KEY,
  type TEXT NOT NULL,                      -- npc.dispatch.v1, ...
  payload JSON NOT NULL,
  status TEXT NOT NULL DEFAULT 'ready',    -- ready|running|done|failed
  priority INTEGER NOT NULL DEFAULT 100,
  attempts INTEGER NOT NULL DEFAULT 0,
  created_at INTEGER NOT NULL,
  due_at INTEGER NOT NULL,
  started_at INTEGER,
  finished_at INTEGER,
  worker TEXT,
  idem_key TEXT
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_cmds_idem ON commands(idem_key) WHERE idem_key IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_cmds_status_due ON commands(status, due_at);
CREATE INDEX IF NOT EXISTS idx_cmds_prio_due ON commands(priority, due_at);
```

**Lifecycle**: `ready→running→done|failed`.
**Crash**: on restart, `running` rows are retried with backoff (update `due_at`).
**Audit** (server-side effects, e.g. sanctions/destructions):

```sql
CREATE TABLE IF NOT EXISTS engine_audit(
  id INTEGER PRIMARY KEY,
  ts INTEGER NOT NULL,
  cmd_type TEXT NOT NULL,
  correlation_id TEXT,
  actor_player_id INTEGER,
  details JSON
);
```

### 5.3 Notices (engine→players via server)

```sql
CREATE TABLE IF NOT EXISTS system_notice(
  id INTEGER PRIMARY KEY,
  ts INTEGER NOT NULL,
  scope TEXT NOT NULL,                   -- global|sector|corp|player
  sector_id INTEGER,
  corp_id INTEGER,
  player_id INTEGER,
  message TEXT NOT NULL,
  meta JSON,
  ephemeral INTEGER NOT NULL DEFAULT 0,
  ttl_seconds INTEGER,
  idem_key TEXT,
  published INTEGER NOT NULL DEFAULT 0
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_notice_idem ON system_notice(idem_key) WHERE idem_key IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_notice_pub ON system_notice(published, ts);
CREATE INDEX IF NOT EXISTS idx_notice_scope_ts ON system_notice(scope, ts);
```

**Broadcast pump** (server): select unpublished (bounded), emit, then mark `published=1`.
**TTL sweeper**: delete ephemerals after expiry.

### 5.4 Zones (FedSpace / lanes)

```sql
CREATE TABLE IF NOT EXISTS zones(
  id INTEGER PRIMARY KEY,
  name TEXT NOT NULL,
  kind TEXT NOT NULL CHECK(kind IN ('fedspace','msl','custom'))
);
CREATE TABLE IF NOT EXISTS sector_zones(
  sector_id INTEGER NOT NULL,
  zone_id INTEGER NOT NULL,
  PRIMARY KEY(sector_id, zone_id)
);
```

Used by enforcement and cleanup.

### 5.5 NPC & traps (minimal models)

```sql
CREATE TABLE IF NOT EXISTS npc(
  id INTEGER PRIMARY KEY,
  kind TEXT NOT NULL,               -- 'imperial','ferrengi',...
  sector_id INTEGER NOT NULL,
  next_move_at INTEGER NOT NULL,
  state JSON
);
CREATE INDEX IF NOT EXISTS idx_npc_due ON npc(next_move_at);

CREATE TABLE IF NOT EXISTS traps(
  id INTEGER PRIMARY KEY,
  sector_id INTEGER NOT NULL,
  owner_player_id INTEGER,
  kind TEXT NOT NULL,               -- 'mine','torp'
  arming_at INTEGER NOT NULL,
  expires_at INTEGER,
  payload JSON
);
CREATE INDEX IF NOT EXISTS idx_traps_due ON traps(arming_at, expires_at);
```

---

## 6) Idempotency & ordering

### Idem key recipes

* **Event-derived decisions**: `idem_key = "ev:" || event.id`
* **Broadcasts** (re-emittable): hash of `{type, scope, targets, message, floor(ts/5s)}`
* **Commands**: hash of `{cmd_type, salient_payload_fields}` (e.g., `npc`, `to_sector`, `offender`)

### Ordering & quarantine

* Consume events in `id ASC`.
* If handler fails more than `config.events.max_attempts`, move that event to `events_deadletter` and **advance watermark**; emit an admin notice with correlation id.

---

## 7) Retention & compaction

* `events`: retain `config.events.retention_days` (archive or delete older).
* `commands`: keep `done/failed` for `config.commands.retention_days`.
* `system_notice`: ephemerals TTL; durable for `config.notice.retention_days`.
* Sweepers run as cron jobs; all bounded (`LIMIT`).

---

## 8) Subsystems

### 8.1 Economy: ports

* **Inputs**: current stock/price, volume since last roll, elasticity params.
* **Cron**: `port_reprice` daily baseline; optional intra-day drift on volume thresholds.
* **Baseline algorithm**:

  * Apply net delta `Δ` toward equilibrium; clamp price to `[min_price, max_price]`.
  * Regenerate stock toward capacity; clamp to `[0, capacity]`.
* **Idempotency**: compute deltas based on last_run markers; use single transaction per port batch.

### 8.2 Terra population

* **Cron**: `terra_replenish` daily; add X colonists (config) up to cap.
* **Notice** on high-water mark.

### 8.3 Planet growth & production

* **Cron**: `planet_growth` every 10m.
* Convert colonist labour to FO/OR/EQ by class ratio; optional fighter production with citadel.
* Clamp outputs; single transaction per batch.

### 8.4 FedSpace cleanup

* **Cron**: `fedspace_cleanup` every 1m; remove illegal objects in `fedspace`/`msl` zones.
* Admin/system notice if large cleanup.

### 8.5 Traps & TTL

* **Short-tick**: detonate/expire due traps (bounded).
* Effects → `broadcast.v1` sector message; damage through server commands or a clear ownership boundary.

### 8.6 Auto-uncloak

* **Cron**: `autouncloak_sweeper` each minute; if `cloak_since < now - max_duration`, clear cloak; notice to player.

### 8.7 Daily turn reset

* **Cron**: `daily_turn_reset` at UTC alignment; restore turns per config; summary notice.

### 8.8 NPC scaffold

* **Tick**: `npc_step` every 2s, max `npc_step_batch`.
* Minimal behaviors: patrols/waypoints; on entering occupied sector, enqueue `encounter.resolve` (stub → broadcast).

### 8.9 Enforcement (Imperial policy)

* **Config** (`policy.imperial` JSON):
  `window, warn, dispatch, destroy, cooldown, rate_per_min, templates`.
* **Flow** on `player.illegal.v1`:

  * First offence → `broadcast.v1` warn + `npc.dispatch.v1`.
  * Repeat within window → `player.destroy_ship.v1`.
* **Idempotency**:

  * warn/dispatch `idem_key="ev:"||event.id`
  * destroy `idem_key="destroy:"||offender||":"||event.id"`
* **Audit** each sanction/destruction.

---

## 9) Concurrency & safety

* SQLite **WAL**, `busy_timeout` (e.g., 3000 ms), short transactions.
* **Advisory locks** in `locks(name, owner, acquired_at)` for exclusivity and future engine leader election.
* **Priority** policy (when backlog exists):

  * Tiers: **P0** enforcement/critical, **P1** NPC/TTL, **P2** economy/cosmetic.
  * Engine claims per tick by tier in order (configurable share).

---

## 10) DB-backed configuration

### Tables

```sql
CREATE TABLE IF NOT EXISTS config(
  key TEXT PRIMARY KEY,
  value TEXT NOT NULL,
  type TEXT NOT NULL,                 -- int|bool|float|string|json|duration
  scope TEXT NOT NULL DEFAULT 'shared', -- server|engine|shared
  description TEXT,
  updated_at INTEGER NOT NULL,
  updated_by TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_config_scope ON config(scope);

CREATE TABLE IF NOT EXISTS config_version(
  id INTEGER PRIMARY KEY CHECK (id=1),
  version INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS config_audit(
  id INTEGER PRIMARY KEY,
  ts INTEGER NOT NULL,
  key TEXT NOT NULL,
  old_value TEXT,
  new_value TEXT,
  actor TEXT NOT NULL,
  note TEXT
);
CREATE INDEX IF NOT EXISTS idx_config_audit_ts ON config_audit(ts);

CREATE TABLE IF NOT EXISTS s2s_keys(
  key_id TEXT PRIMARY KEY,
  key_b64 TEXT NOT NULL,
  active_from INTEGER NOT NULL,
  active_to INTEGER,
  is_default_tx INTEGER NOT NULL DEFAULT 0,
  updated_at INTEGER NOT NULL,
  updated_by TEXT NOT NULL
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_s2s_keys_default
  ON s2s_keys(is_default_tx) WHERE is_default_tx=1;
```

### Usage

* On writes to `config`/`s2s_keys`: bump `config_version.version += 1` and append to `config_audit`; server sends `s2s.config.bump {version}` to engine.
* **Validation** examples:

  * `engine.tick_ms` ∈ [50, 5000]
  * `s2s.frame_size_limit` ∈ [4096, 262144]
  * `policy.imperial` JSON contains required fields and sane ordering
  * `s2s_keys.key_b64` decodes to ≥ 32 bytes; only one `is_default_tx=1` and it’s active

---

## 11) TCP S2S protocol (engine↔server)

### Transport & framing

* **TCP** (server listens, engine connects).
* 4-byte big-endian length prefix, then UTF-8 JSON envelope.
* `frame_size_limit` default 64 KiB; hard reject larger.

### Auth

* **HMAC-SHA256** with `key_id` selected from `s2s_keys`; sign canonicalised JSON sans `auth.sig`.
* On auth failure: return generic error; never echo payload; log only `key_id` and peer.

### Envelope

```json
{
  "v": 1,
  "type": "s2s.health.check | s2s.broadcast.sweep | s2s.command.push | s2s.config.bump | s2s.engine.shutdown | s2s.ack | s2s.error",
  "id": "uuid",
  "ts": 1737990000,
  "src": "engine|server",
  "dst": "server|engine",
  "auth": {"scheme": "hmac-sha256", "key_id": "key1", "sig": "..."},
  "payload": { ... }
}
```

### Messages

* **`s2s.health.check`** → ack: `{role, version, uptime_s, caps}`
* **`s2s.broadcast.sweep {max_rows?}`** → ack: `{processed}`
* **`s2s.command.push {type, payload, idem_key}`** → ack: `{status:"queued", cmd_id}` (optional fast path)
* **`s2s.config.bump {version}`** → engine reloads config if version increased
* **`s2s.engine.shutdown {grace_ms}`** → graceful stop
* **`s2s.ack` / `s2s.error {code,message}`**

### Rate limits & safety

* Per-minute request cap (config) per connection; debounce sweeps; parse limits (JSON depth/array counts) if applicable.

---

## 12) Client broadcast envelope

Server → client:

```json
{
  "type": "broadcast.v1",
  "data": {
    "id": 123,
    "ts": 1737990000,
    "scope": "global|sector|corp|player",
    "sector_id": 321,
    "corp_id": null,
    "player_id": null,
    "message": "Imperial Warship en route to sector 321",
    "meta": {"severity": "info"},
    "ephemeral": true
  }
}
```

**Cap**: enforce `broadcast.message_max_bytes`; truncate with suffix.

---

## 13) Observability

* **Metrics logs** every `metrics.interval_s`: `last_event_id`, `event_lag_s`, `events_ready`, `commands_ready`, `jobs_per_tick`, `reconnects`.
* **Startup health line**: `{role, version, git_sha, db_schema_version, config_version}`.
* **Correlation IDs** thread event → command → broadcast.
* Secrets redacted.

---

## 14) Testing strategy

* **Golden path**: illegal → warn/dispatch → repeat → destruction (idempotent).
* **Crash-resume**: kill engine mid-batch; restart resumes without duplicates.
* **TCP smoke**: health + sweep; invalid HMAC rejected.
* **TTL expiry**: ephemerals removed after TTL.
* **Load & priority**: bursts keep p95 bounded; P0 drains first.
* **Idempotency harness**: replay same events/commands → identical final state.
* **Poison handling**: forced handler failures land in deadletter; stream continues.

---

## 15) Admin controls

* **Manual cron trigger**: set `next_due_at = now` for a named task.
* **Dry-run mode**: `config.engine.dry_run=1` (log decisions, skip mutations) for staging.
* **Config reload**: bump `config_version`, then `s2s.config.bump`.
* **Leader election (future HA)**: engine acquires `locks('engine.leader')` before doing cron or events.

---

## 16) Time & alignment

* **Clock sources**: monotonic for tick/poll; UTC epoch for DB fields.
* **Daily alignment**: UTC wall-clock; no drift; catch-up once after downtime.

---

## 17) Security notes

* DB file `0600`, owned by service user.
* HMAC keys in `s2s_keys`; never logged; rotation via insert → switch default → retire old.
* On auth failure: generic error; minimal logging.

---

### Appendices

#### A) Config keys (examples)

* `engine.tick_ms` (int), `engine.event_batch` (int), `engine.command_batch` (int), `engine.npc_step_batch` (int)
* `s2s.tcp_host` (string), `s2s.tcp_port` (int), `s2s.frame_size_limit` (int), `s2s.connect_timeout_ms` (int), `s2s.read_timeout_ms` (int), `s2s.backoff_ms` (json)
* `policy.imperial` (json)
* `broadcast.message_max_bytes` (int), `metrics.interval_s` (int)
* Retention knobs: `events.retention_days`, `commands.retention_days`, `notice.retention_days`
* Priority knobs: `priority.weights` (json), `priority.backlog_threshold`

#### B) Example idem_key constructions

* `broadcast warn`: `idem_key = hash("broadcast.warn", scope, sector_id, player_id, floor(ts/5s), message)`
* `npc dispatch`: `idem_key = hash("npc.dispatch", offender_id, to_sector, reason, window_start)`
* `destroy ship`: `idem_key = "destroy:"||offender_id||":"||event_id`

