# Player Navigation — Bookmarks & Avoid List

**Status:** Stable (v1)
**Scope:** Client-visible navigation helpers stored per-player.
**Related:** `system.capabilities` advertises features & limits.

---

## 1) Data model (SQLite)

### 1.1 `player_bookmarks`

```sql
CREATE TABLE player_bookmarks (
  id         INTEGER PRIMARY KEY AUTOINCREMENT,
  player_id  INTEGER NOT NULL,
  name       TEXT    NOT NULL,
  sector_id  INTEGER NOT NULL,
  updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
  UNIQUE(player_id, name),
  FOREIGN KEY (player_id) REFERENCES players(id)  ON DELETE CASCADE,
  FOREIGN KEY (sector_id) REFERENCES sectors(id) ON DELETE CASCADE
);

CREATE INDEX idx_bookmarks_player ON player_bookmarks(player_id);
```

### 1.2 `player_avoid`

```sql
CREATE TABLE player_avoid (
  player_id  INTEGER NOT NULL,
  sector_id  INTEGER NOT NULL,
  updated_at INTEGER NOT NULL DEFAULT (strftime('%s','now')),
  PRIMARY KEY (player_id, sector_id),
  FOREIGN KEY (player_id) REFERENCES players(id)  ON DELETE CASCADE,
  FOREIGN KEY (sector_id) REFERENCES sectors(id) ON DELETE CASCADE
);

CREATE INDEX idx_avoid_player ON player_avoid(player_id);
```

---

## 2) Limits & capability advertisement

`system.capabilities` includes:

```json
{
  "features": {
    "bookmark_add":  true,
    "bookmark_remove": true,
    "bookmark_list":  true,
    "avoid_add":     true,
    "avoid_remove":  true,
    "avoid_list":    true
  },
  "limits": {
    "max_bookmarks": 64,
    "max_avoids":    64
  }
}
```

> Values are server defaults; if you later wire them to config, the endpoint should reflect the configured caps.

---

## 3) Error model (codes used here)

* **1105** `ERR_DUPLICATE_REQUEST` — violates unique/PK (idempotent “already there”).
* **1301** `ERR_MISSING_FIELD` — required field absent (e.g., `sector_id`, `name`).
* **1302** `ERR_INVALID_ARG` — type/format invalid (bad `name`, out-of-range `sector_id`).
* **1304** `ERR_LIMIT_EXCEEDED` — exceeded `max_bookmarks`/`max_avoids`.
* **1401** `ERR_NOT_AUTHENTICATED` — auth required.

Server replies use the standard envelope with `status: "ok" | "error" | "refused"`.

---

## 4) Validation rules

**Common**

* Auth required.
* `sector_id` must be a positive integer; server MAY verify sector exists.

**Bookmarks**

* `name`: non-empty UTF-8, trimmed.
* Length cap: **≤ 64 chars** (or `config.max_name_length` if you wire it).
* Unique per player (`UNIQUE(player_id, name)`).
* Adding an existing `name` → `ERR_DUPLICATE_REQUEST`.

**Avoid list**

* Unique per player/sector (`PRIMARY KEY(player_id, sector_id)`).
* Re-adding same `sector_id` → `ERR_DUPLICATE_REQUEST`.

---

## 5) RPCs & payloads

### 5.1 Bookmarks

**Add**

```json
{ "command": "nav.bookmark.add", "data": { "name": "Home", "sector_id": 1 } }
```

**OK**

```json
{ "status":"ok", "type":"nav.bookmark.added",
  "data": { "name":"Home", "sector_id":1 } }
```

**Errors**

* Missing `name`/`sector_id` → `1301`
* Name too long/invalid → `1302`
* Cap exceeded → `1304`
* Duplicate (`name` already saved) → `1105`

**Remove**

```json
{ "command": "nav.bookmark.remove", "data": { "name": "Home" } }
```

```json
{ "status":"ok", "type":"nav.bookmark.removed",
  "data": { "name":"Home", "removed": true } }
```

> If you prefer a strict “not found” error on unknown `name`, return `ERR_NOT_FOUND` (1209) or treat as idempotent OK. Current recommendation: **idempotent OK**.

**List**

```json
{ "command": "nav.bookmark.list" }
```

```json
{ "status":"ok", "type":"nav.bookmark.list",
  "data": { "bookmarks": [ { "name":"Home", "sector_id":1 } ] } }
```

---

### 5.2 Avoid list

**Add**

```json
{ "command": "nav.avoid.add", "data": { "sector_id": 2 } }
```

```json
{ "status":"ok", "type":"nav.avoid.added",
  "data": { "sector_id":2 } }
```

**Errors**

* Missing `sector_id` → `1301`
* Invalid `sector_id` → `1302`
* Cap exceeded → `1304`
* Duplicate (already avoided) → `1105`

**Remove**

```json
{ "command": "nav.avoid.remove", "data": { "sector_id": 2 } }
```

```json
{ "status":"ok", "type":"nav.avoid.removed",
  "data": { "sector_id":2, "removed": true } }
```

> As with bookmarks, you can return idempotent OK when the tuple didn’t exist.

**List**

```json
{ "command": "nav.avoid.list" }
```

```json
{ "status":"ok", "type":"nav.avoid.list",
  "data": { "sectors": [2, 9, 501] } }
```

---

## 6) Implementation notes

* **Idempotency:** duplicates are treated as success or mapped to `ERR_DUPLICATE_REQUEST`. Clients may retry safely.
* **Ordering:** `list` responses SHOULD be stable (e.g., `ORDER BY updated_at DESC, name ASC`).
* **Sanitization:** server strips ANSI/control chars from `name` on output.
* **Hydration:** both surfaces are included in `player.get_settings` (see #210).

---

## 7) Acceptance checklist

* [ ] `system.capabilities` advertises `bookmark_*`, `avoid_*` and `limits`.
* [ ] Add/Remove/List handlers return the types shown above.
* [ ] Caps enforced; exceeding produces `1304`.
* [ ] Duplicate add produces `1105` (or idempotent OK, if that’s your chosen convention).
* [ ] JSON tests pass for: happy paths, missing fields, invalid args, duplicate add, and list results.

---

## 8) Quick test frames

```json
{ "command":"nav.bookmark.add", "data":{ "name":"HQ", "sector_id":278 } }
{ "command":"nav.bookmark.list" }
{ "command":"nav.avoid.add", "data":{ "sector_id":278 } }
{ "command":"nav.avoid.list" }
```

---

**Changelog**

* **v1 (2025-10-24):** Initial one-pager (tables, caps, RPCs, examples, tests).
