** A new attempt at using json for the protocol. Not everything will be implemented! **


> **Transport-agnostic**: works over TCP (line-delimited JSON), WebSocket, or stdio. UTF-8, one JSON object per frame/line.

---

# twclone — JSON Protocol (v1.0)

A compact, strongly-typed, forward-compatible JSON protocol for clients, servers, and server-to-server components.

* **Message envelope** with `id`, `ts`, `command|event`, `data`, `auth`, `meta`.
* **Deterministic responses** with `status`, `type`, `data`, `error`.
* **Correlation & idempotency**: `id`, `reply_to`, `idempotency_key`.
* **Subscriptions**: real-time events with `subscribe`/`unsubscribe`.
* **Schemas & capability discovery**: `hello`, `capabilities`, `describe_schema`.
* **Pagination, partials, bulk**, and **rate-limit hints**.

---

## 0. Transport, Framing, Limits

* **Encoding**: UTF-8.
* **Framing**: one JSON object per line (NDJSON) or one object per WebSocket message.
* **Max frame**: 64 KiB default (server MAY advertise higher via `hello.capabilities.limits`).
* **Compression**: per-transport (e.g., permessage-deflate on WebSocket).
* **Clocks**: timestamps are RFC 3339 UTC (`YYYY-MM-DDThh:mm:ss.sssZ`).
* **Numbers**: integers for IDs, fixed-precision decimals for currency/credits.

---

## 1. Message Envelope

Every inbound/outbound frame is a single JSON object.

```json
{
  "id": "a6f1b8a0-5c8d-4b89-9d82-bb2f0a7b8d57",   // UUIDv4 client-generated unless noted
  "ts": "2025-09-17T19:45:12.345Z",
  "command": "move.warp",                         // or "event": "combat.attacked"
  "auth": { "session": "eyJhbGciOi..." },         // see §2
  "data": { /* command-specific payload */ },
  "meta": {
    "idempotency_key": "c4e0e1a9-...-1",         // optional, client-provided
    "client_version": "ge-client/0.9.3",
    "locale": "en-GB"
  }
}
```

### 1.1 Server Response Envelope

```json
{
  "id": "3c1b...server",          // server message id
  "ts": "2025-09-17T19:45:12.410Z",
  "reply_to": "a6f1b8a0-5c8d-4b89-9d82-bb2f0a7b8d57",
  "status": "ok",                  // "ok" | "error" | "granted" | "refused" | "partial"
  "type": "sector.info",           // domain-qualified response type
  "data": { /* result payload */ },
  "error": null,                   // or see §8
  "meta": {
    "rate_limit": { "limit": 200, "remaining": 152, "reset": "2025-09-17T20:00:00Z" },
    "trace": "srv-az1/fe2:rx394",
    "warnings": []
  }
}
```

> **Improvements vs legacy**: consistent envelope, correlation (`reply_to`), idempotency, traceability, rate-limit hints, locale.

---

## 2. Authentication & Session

### 2.1 Login

**Cmd**: `auth.login`
**Req**

```json
{ "command": "auth.login", "data": { "user_name": "Rick", "password": "secret" } }
```

**Rsp**

```json
{
  "status": "ok",
  "type": "auth.session",
  "data": {
    "session": "eyJhbGciOi...",            // JWT or opaque token
    "expires": "2025-09-17T21:45:12Z",
    "player": { "id": 1, "name": "Rick", "alignment": "Neutral" }
  }
}
```

### 2.2 New Player

`auth.register` → same envelope; returns session + initial ship.

### 2.3 Refresh, Logout

* `auth.refresh` → rotates token.
* `auth.logout` → invalidates session (idempotent).

> **Security**: Servers SHOULD use HTTPS/WSS; tokens are bearer credentials; rotate on privilege elevation; support `mfa.totp.verify` if enabled.

---

## 3. Capability Discovery & Schema

### 3.1 Hello

Client sends `system.hello` immediately after connect (no auth required).

**Req**

```json
{ "command": "system.hello", "data": { "client": "ge-cli", "version": "0.9.3" } }
```

**Rsp**

```json
{
  "status": "ok",
  "type": "system.capabilities",
  "data": {
    "server": "ge-core/1.7.2",
    "protocol": { "version": "1.0", "min": "1.0", "max": "1.x" },
    "namespaces": ["auth","player","sector","move","trade","combat","planet","ship","chat","admin","s2s","subscribe"],
    "limits": { "max_frame_bytes": 65536, "max_bulk": 50 },
    "features": {
      "subscriptions": true,
      "bulk": true,
      "partial": true,
      "idempotency": true,
      "schemas": true
    }
  }
}
```

### 3.2 Describe Schema

`system.describe_schema` → returns JSON Schema for a named `command`/`type` or an entire namespace (for codegen & validation).

---

## 4. Common Types

* **ID**: integer or UUID string; stable.
* **Credits**: string decimal `"12345.00"` to avoid float errors.
* **Vector**: `{ "x": 0, "y": 0, "z": 0 }` if needed.
* **SectorRef**: `{ "id": 42 }`.
* **Pagination**: `{ "cursor": "abc", "limit": 20 }` and response `{ "items": [...], "next_cursor": "def" }`.

---

## 5. Client Commands

### 5.1 Player / Session

* `player.my_info` → ship, stats, location, turns.
* `player.list_online` (paginated).
* `player.rankings` (paginated, sortable).
* `player.set_prefs` (partial update): theme, locale, notifications.

**Example: `player.my_info` Rsp**

```json
{
  "status": "ok",
  "type": "player.info",
  "data": {
    "player": {
      "id": 1, "name": "Rick", "experience": 500, "alignment": "Neutral",
      "turns_remaining": 125, "credits": "15000.00"
    },
    "ship": {
      "id": 123, "name": "Explorer", "type": "Standard",
      "stats": { "fighters": 15, "shields": 20, "holds": 100 },
      "cargo": { "ore": 50, "organics": 50, "equipment": 50, "colonists": 0 },
      "location": { "sector_id": 42, "turns_per_warp": 1 }
    }
  }
}
```

### 5.2 Movement

* `move.describe_sector` → detailed sector view (current or `{sector_id}`).
* `move.scan` → quick scan (adjacency, hostiles).
* `move.warp` → to adjacent sector; consumes turns.
* `move.pathfind` → server-suggested path (Dijkstra/A\*) with cost.
* `move.autopilot.start/stop/status` → executes path over time.

**`move.warp` Req**

```json
{ "command": "move.warp", "data": { "to_sector_id": 43 } }
```

**Rsp (ok)**

```json
{
  "status": "ok", "type": "move.result",
  "data": { "from": 42, "to": 43, "turns_spent": 1, "encounter": null }
}
```

**Rsp (refused)**

```json
{
  "status": "refused", "type": "move.result",
  "data": { "from": 42, "to": 9999, "reason": "no_warp_link" },
  "error": { "code": 1402, "message": "Warp not possible" }
}
```

### 5.3 Ports & Trade

* `trade.port_info` → port inventory/prices.
* `trade.buy` / `trade.sell` (idempotent via `idempotency_key`).
* `trade.offer` → create private trade offer.
* `trade.accept` / `trade.cancel`.
* `trade.history` (paginated).

**`trade.buy` Req**

```json
{
  "command": "trade.buy",
  "data": { "port_id": 7, "commodity": "ore", "quantity": 30, "max_price": "120.00" },
  "meta": { "idempotency_key": "b857c..." }
}
```

### 5.4 Combat & Defences

* `combat.attack` → target ship/planet/fighters.
* `combat.deploy_fighters`
* `combat.lay_mines` / `combat.sweep_mines`
* `combat.status` → recent combat log.

**`combat.attack` Req**

```json
{ "command": "combat.attack", "data": { "target_type": "ship", "target_id": 321, "stance": "aggressive" } }
```

**Rsp (granted)** – attack executed

```json
{
  "status": "granted",
  "type": "combat.summary",
  "data": {
    "rounds": 3,
    "attacker_loss": { "fighters": 5, "shields": 10 },
    "defender_loss": { "fighters": 20, "shields": 0 },
    "destroyed": false, "loot": { "credits": "250.00", "ore": 10 }
  }
}
```

**Rsp (refused)** – e.g., safe sector

```json
{
  "status": "refused",
  "type": "combat.summary",
  "data": { "reason": "safe_zone" },
  "error": { "code": 1812, "message": "Combat disallowed in safe sectors" }
}
```

### 5.5 Ship Management

* `ship.status` / `ship.upgrade` (hulls, shields, holds, engines).
* `ship.transfer_cargo` (to ship/planet/port).
* `ship.jettison` (with sector rules).
* `ship.rename`
* `ship.repair`

### 5.6 Planets & Citadel

* `planet.genesis` (create planet; admin or item-based).
* `planet.info` / `planet.rename`
* `planet.land` / `planet.launch`
* `planet.transfer_ownership`
* `citadel.build` / `citadel.upgrade`
* `planet.harvest` / `planet.deposit` / `planet.withdraw`

### 5.7 Communication

* `chat.send` (private), `chat.broadcast` (sector/global with rules).
* `chat.history` (paginated).
* `mail.send` / `mail.inbox` / `mail.read` / `mail.delete`.

### 5.8 Discovery & Search

* `sector.search` (filters: has\_port, has\_planet, owner, danger).
* `planet.search`, `player.search`.

### 5.9 Subscriptions (Real-time)

* `subscribe.add` → e.g., `["sector.events","chat.sector","combat.events"]`
* `subscribe.remove`
* `subscribe.list`

**Rsp**

```json
{ "status": "ok", "type": "subscribe.state", "data": { "active": ["sector.events","chat.sector"] } }
```

---

## 6. Server-to-Client Events (Asynchronous)

All events share the envelope with `event` key.

### 6.1 Sector Events

```json
{
  "id": "evt-1",
  "ts": "2025-09-17T19:47:01Z",
  "event": "sector.player_entered",
  "data": { "sector_id": 43, "player": { "id": 2, "name": "Jane" } }
}
```

* `sector.player_left`
* `sector.beacon_changed`
* `sector.mine_detonation`
* `sector.port_stock_update`

### 6.2 Combat Events

* `combat.attacked`
* `combat.defence_triggered`
* `combat.ship_destroyed`

**Example**

```json
{
  "event": "combat.attacked",
  "data": {
    "attacker": { "id": 99, "name": "Corsair" },
    "defender": { "id": 1, "name": "Rick" },
    "sector_id": 43,
    "snapshot": { "attacker_fighters": 50, "defender_fighters": 30 }
  }
}
```

### 6.3 Chat & Mail

* `chat.message`
* `mail.new`

### 6.4 Admin / System

* `system.notice`
* `system.shutdown_warning`

> **Improvements vs legacy**: explicit subscription model; well-typed event names; small focused payloads.

---

## 7. Bulk, Partial, and Pagination

* **Bulk**: `bulk.execute` with up to `limits.max_bulk` commands.

```json
{ "command": "bulk.execute", "data": { "items": [
  { "command": "trade.buy", "data": { "port_id": 7, "commodity": "ore", "quantity": 10 } },
  { "command": "trade.buy", "data": { "port_id": 7, "commodity": "equipment", "quantity": 5 } }
]}}
```

* **Partial**: PATCH-style for settings:
  `player.set_prefs`: `{ "patch": { "notifications.chat": false } }`.

* **Pagination**: All list endpoints accept `{ "cursor", "limit" }` and return `"next_cursor"`.

---

## 8. Error Model (Catalogue)

Standard response on failure:

```json
{
  "status": "error",
  "type": "error",
  "error": { "code": 1220, "category": "auth", "message": "Invalid password", "details": { "attempts_left": 2 } }
}
```

### 8.1 Code Ranges

* **1100–1199**: System/General
* **1200–1299**: Auth/Player
* **1300–1399**: Validation/Quota/Rate limit
* **1400–1499**: Movement
* **1500–1599**: Planet/Citadel
* **1600–1699**: Port/Stardock
* **1700–1799**: Trade
* **1800–1899**: Ship/Combat
* **1900–1999**: Chat/Mail/Comms
* **2000–2099**: S2S/Replication/Admin

### 8.2 Full Catalogue

> The following provides a complete, canonical set. Servers MAY extend with vendor-specific `>= 9000`.

**System (1100)**

* 1100 Unknown error
* 1101 Not implemented
* 1102 Service unavailable
* 1103 Maintenance mode
* 1104 Timeout
* 1105 Duplicate request (idempotency replay)
* 1106 Serialization error
* 1107 Version not supported
* 1108 Message too large
* 1109 Rate limited (generic)
* 1110 Insufficient turns
* 1111 Permission denied

**Auth/Player (1200)**

* 1200 Auth required
* 1201 Invalid token
* 1202 Token expired
* 1203 Session revoked
* 1204 User not found
* 1205 Name already taken
* 1206 Weak password
* 1207 MFA required
* 1208 MFA invalid
* 1210 Player banned
* 1211 Alignment restricted action
* 1220 Invalid credentials
* 1221 Registration disabled

**Validation/Quota (1300)**

* 1300 Invalid request schema
* 1301 Missing required field
* 1302 Invalid field value
* 1303 Out of range
* 1304 Quota exceeded
* 1305 Too many bulk items
* 1306 Cursor invalid

**Movement (1400)**

* 1400 Not in sector
* 1401 Sector not found
* 1402 Warp not possible (no link)
* 1403 Turn cost exceeds remaining
* 1404 Autopilot already running
* 1405 Autopilot path invalid
* 1406 Safe zone transit only
* 1407 Blocked by mines/fighters
* 1408 Transwarp unavailable

**Planet/Citadel (1500)**

* 1500 Planet not found
* 1501 Not planet owner
* 1502 Landing refused (defences)
* 1503 Citadel required
* 1504 Citadel max level reached
* 1505 Insufficient resources
* 1506 Transfer not permitted
* 1507 Genesis disabled

**Port/Stardock (1600)**

* 1600 Port not found
* 1601 Port out of stock
* 1602 Price slippage beyond limit
* 1603 Docking refused
* 1604 License required
* 1605 Blacklisted at port

**Trade (1700)**

* 1700 Commodity unknown
* 1701 Insufficient holds
* 1702 Insufficient credits
* 1703 Offer not found
* 1704 Offer expired
* 1705 Offer not yours
* 1706 Trade window closed

**Ship/Combat (1800)**

* 1800 Ship not found
* 1801 Target invalid
* 1802 Friendly fire blocked
* 1803 Combat disallowed in sector
* 1804 Ammo/fighters depleted
* 1805 Hull critical, action refused
* 1806 Mine limit exceeded
* 1810 Destroyed (terminal)

**Chat/Mail (1900)**

* 1900 Recipient not found
* 1901 Muted or blocked
* 1902 Broadcast forbidden
* 1903 Inbox full
* 1904 Message too long

**S2S/Admin (2000)**

* 2000 Replication lag
* 2001 Conflict – authoritative copy newer
* 2002 Admin only
* 2003 Shard unavailable
* 2004 Capability not enabled

---

## 9. Domain Payloads (Selected)

### 9.1 Sector Info (`move.describe_sector`)

```json
{
  "status": "ok",
  "type": "sector.info",
  "data": {
    "sector_id": 42,
    "beacon_text": "Full of promise.",
    "adjacent_sectors": [43,44,45],
    "port": { "id": 7, "name": "Trade Hub", "class": "A", "buying": ["organics"], "selling": ["ore","equipment"] },
    "planets": [ { "id": 789, "name": "Prometheus", "type": "Earth-like", "owner_id": 1 } ],
    "entities": { "fighters": 100, "mines": { "armid": 5, "limpet": 2 }, "derelicts": 3, "aliens": 1 },
    "players_present": [ { "id": 1, "name": "Rick" }, { "id": 2, "name": "Jane" } ],
    "security": { "safe_zone": false, "federation_patrol": 0 }
  }
}
```

### 9.2 Port Info (`trade.port_info`)

```json
{
  "status": "ok",
  "type": "port.info",
  "data": {
    "id": 7, "name": "Trade Hub", "class": "A",
    "stock": { "ore": 1000, "organics": 800, "equipment": 650 },
    "prices": { "ore": "3.50", "organics": "4.10", "equipment": "9.75" },
    "refresh_in": 3600
  }
}
```

---

## 10. Server-to-Server (S2S)

Same envelope; mutually-authenticated channel (mTLS or signed tokens; include `meta.origin_shard`).

* `s2s.planet.genesis`
* `s2s.planet.transfer`
* `s2s.player.migrate` (shard rebalancing)
* `s2s.port.restock`
* `s2s.event.relay`
* `s2s.replication.heartbeat`

**Example**

```json
{
  "command": "s2s.planet.genesis",
  "auth": { "service_token": "srv:core:az1:..." },
  "data": { "planet_name": "Prometheus", "sector_id": 42, "owner_id": 1, "type": "Earth-like" },
  "meta": { "origin_shard": "az1-core" }
}
```

---

## 11. Versioning & Compatibility

* **Semantic**: `protocol.version` returned in `system.hello`.
* **Non-breaking additions**: new fields are optional; clients MUST ignore unknown fields.
* **Breaking changes**: bump **major**; server MAY advertise multiple supported majors.
* **Deprecation**: `meta.warnings += ["deprecated: move.transwarp will be removed in 1.3"]`.

---

## 12. Idempotency, Retries, Ordering

* Mutating commands SHOULD carry `meta.idempotency_key`.
* Retries of the same key MUST return the same result (or a distinct `duplicate=true` flag).
* Server MAY use exactly-once semantics per key for 24h.

---

## 13. Rate Limits & Backoff

* Include `meta.rate_limit` in responses.
* On `1109` (rate limited), clients SHOULD use **exponential backoff with jitter**.

---

## 14. Security Notes

* Prefer **WSS/HTTPS**; tokens are bearer; scope-limited.
* Consider **HMAC signing** per frame for high-value ops (`meta.signature`).
* Enforce **server-side validation** (also publish schemas for client-side validation).

---

## 15. Logging & Traceability

* Correlate via `id` and `reply_to`.
* `meta.trace` is an opaque server path, useful for support.

---

## 16. Minimal JSON Schemas (illustrative)

> Use `system.describe_schema` to fetch full schemas at runtime for codegen/validation.

**Envelope (partial)**

```json
{
  "$id": "ge://schema/envelope.json",
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "required": ["ts"],
  "properties": {
    "id": { "type": "string" },
    "ts": { "type": "string", "format": "date-time" },
    "command": { "type": "string" },
    "event": { "type": "string" },
    "auth": { "type": "object" },
    "data": { "type": "object" },
    "meta": { "type": "object" }
  },
  "oneOf": [
    { "required": ["command"] },
    { "required": ["event"] }
  ]
}
```

**Error object**

```json
{
  "$id": "ge://schema/error.json",
  "type": "object",
  "required": ["code", "message"],
  "properties": {
    "code": { "type": "integer" },
    "category": { "type": "string" },
    "message": { "type": "string" },
    "details": { "type": "object" }
  }
}
```

---

## 17. Worked Flows

### 17.1 Login → Sector → Buy → Warp

1. `auth.login` → session
2. `move.describe_sector` (current)
3. `trade.port_info` → price check
4. `trade.buy` with `idempotency_key`
5. `move.warp` to next sector
6. Receive `sector.player_entered` events if subscribed

### 17.2 Autopilot

* `move.pathfind` → choose path
* `move.autopilot.start` → server ticks through, emitting `sector.player_entered`/`move.progress` events
* `move.autopilot.stop` to abort

---

## 18. Command Inventory (Index)

**auth**: `login`, `refresh`, `logout`, `register`, `mfa.totp.verify`
**system**: `hello`, `describe_schema`
**player**: `my_info`, `list_online`, `rankings`, `set_prefs`
**move**: `describe_sector`, `scan`, `warp`, `pathfind`, `autopilot.start`, `autopilot.stop`, `autopilot.status`
**trade**: `port_info`, `buy`, `sell`, `offer`, `accept`, `cancel`, `history`
**combat**: `attack`, `deploy_fighters`, `lay_mines`, `sweep_mines`, `status`
**ship**: `status`, `upgrade`, `transfer_cargo`, `jettison`, `rename`, `repair`
**planet**: `genesis`, `info`, `rename`, `land`, `launch`, `transfer_ownership`, `harvest`, `deposit`, `withdraw`
**citadel**: `build`, `upgrade`
**chat**: `send`, `broadcast`, `history`
**mail**: `send`, `inbox`, `read`, `delete`
**sector**: `search`, `set_beacon`
**subscribe**: `add`, `remove`, `list`
**bulk**: `execute`
**admin**: `notice`, `shutdown_warning`
**s2s**: `planet.genesis`, `planet.transfer`, `player.migrate`, `port.restock`, `event.relay`, `replication.heartbeat`

---

## 19. Migration Notes (Legacy → JSON)

* Map legacy verbs to new namespaced commands (see **Index**).
* Convert multi-status textual responses into `status` + `type` + `data` with concrete fields.
* Replace positional fields with named keys.
* Consolidate all error text to canonical `error.code` + `message`.
* Introduce subscriptions to replace ad-hoc unsolicited notices.
* Use idempotency for all credits/cargo mutating ops.

---

## 20. Examples: Refusals vs Errors

* **Refused**: rule-based rejection, expected (e.g., safe zone, insufficient turns):

```json
{ "status": "refused", "type": "combat.summary", "data": { "reason": "safe_zone" }, "error": { "code": 1812, "message": "Combat disallowed in safe sectors" } }
```

* **Error**: malformed/exceptional (schema invalid, server failure):

```json
{ "status": "error", "type": "error", "error": { "code": 1300, "message": "Invalid request schema" } }
```

---

## 21. Testing & Validation

* Clients SHOULD validate against schemas returned by `system.describe_schema`.
* Servers MUST validate inbound payloads server-side.
* Provide a conformance test suite with canned frames (golden files).

---

### Appendix A: Minimal Client Loop (pseudocode)

```text
open connection
send system.hello
recv capabilities
send auth.login
recv session
send subscribe.add ["sector.events","chat.sector","combat.events"]
loop:
  read frame
  if "reply_to": match pending future
  else if "event": route to handlers
  else: log unexpected frame
```

---

**This document is intentionally exhaustive and forward-compatible**. It retains all legacy semantics while leveraging JSON for clarity, validation, and evolvability (namespaces, schemas, idempotency, subscriptions, pagination, bulk, and robust error handling).
