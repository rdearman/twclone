# SysOp Menu — UX & API Specification

**Status:** Draft v0.2
**Last updated:** 2025-10-20

## 0) Summary

The SysOp Menu is the operator console for running, inspecting, and administering a live TW server. It consumes the same **data-only protocol** as player clients (see #205) and adds privileged reads and actions (mutations). This document covers UX, capabilities, **message contracts with examples**, error semantics, and acceptance criteria.

---

## 1) Goals & Non-Goals

### Goals

* Live read-only status (health, queues, logs, metrics).
* Safe administrative actions with explicit confirmations.
* Keyboard-first navigation and command palette.
* Proper **audit trails** and refusal/error codes.

### Non-Goals

* Content editing beyond notices/broadcasts.
* Gameplay automation/intervention.
* Localisation/theming (SysOp is English-first; server stores locale code only).

---

## 2) Roles & Permissions

* **SysOp**: full access.
* **GM**: limited admin (notices, broadcasts, moderate chat, kick).
* **Observer**: read-only dashboards/logs.

Server gates permissions; unauthorized calls return `refused 1407` (forbidden) or `refused 1401` (unauthenticated).

---

## 3) Transport & Envelope

* **Transport:** TCP JSON Lines (one JSON object per line).
* **Envelope (server replies):**

  ```json
  {
    "id": "srv-ok",
    "reply_to": "<client-msg-id>",
    "ts": "2025-10-20T18:40:00Z",    // ISO-8601 UTC
    "status": "ok|error|refused",
    "type": "payload.type",
    "data": { ... },                 // data-only (no ANSI)
    "error": null,                   // or { "code": 1407, "message": "Forbidden" }
    "meta": { "rate_limit": { "limit": 60, "remaining": 60, "reset": 60 } }
  }
  ```
* All outbound JSON is **ANSI-scrubbed** (#205), timestamps are ISO-8601 UTC.

---

## 4) Information Architecture (UX)

Top-level sections & hotkeys:

* **Dashboard** (`g d`): server/engine status, rates, notices, audit tail.
* **Players** (`g p`): search/list/detail, kick/logout, DM block toggle.
* **Corporations** (`g c`): list/detail, corp mail volume, freeze.
* **Universe** (`g u`): sector/ports/planets summaries, stardock.
* **Messaging** (`g m`): system notices CRUD, broadcasts, subspace moderation.
* **Engine & Jobs** (`g e`): S2S health, job queues, deadletter retry.
* **Logs & Metrics** (`g l`): system events, engine audit, counters.
* **Admin** (`g a`): S2S key mgmt, cron toggles, backfill tools.
* **Help** (`?`): shortcuts, error codes, versions.

Command palette (`:`) examples:
`player <name|id>`, `notice send`, `engine retry <deadletter_id>`, `cron disable <name>`, `corp <tag>`.

---

## 5) Read APIs (Queries) — Messages & Examples

> Unless noted, **all** reads require at least `observer` role. Timestamps ISO; data only.

### 5.1 Dashboard Snapshot

**Request**

```json
{ "command": "sysop.dashboard.get" }
```

**Response**

```json
{
  "status": "ok",
  "type": "sysop.dashboard_v1",
  "data": {
    "server": { "version": "1.0.0-alpha", "time": "2025-10-20T18:40:00Z", "uptime_s": 123456 },
    "links": {
      "engine": { "status": "up", "last_hello": "2025-10-20T18:39:55Z",
                  "counters": {"sent": 102, "recv": 101, "auth_fail": 0, "too_big": 0} }
    },
    "rates": {"rpc_per_min": 42, "refusals_per_min": 2, "errors_per_min": 1},
    "notices": [
      { "id": 101, "title": "Maintenance", "severity": "info",
        "created_at": "2025-10-20T17:30:00Z", "expires_at": "2025-10-20T19:00:00Z" }
    ],
    "audit_tail": [
      { "id": 9991, "ts": "2025-10-20T18:39:50Z", "cmd_type": "player.kick", "actor_player_id": 2 }
    ]
  }
}
```

### 5.2 Players Search

**Request**

```json
{ "command": "sysop.players.search", "data": { "q": "newguy", "page": 1, "page_size": 20 } }
```

**Response**

```json
{
  "status": "ok",
  "type": "sysop.players_v1",
  "data": {
    "items": [
      { "player_id": 1, "name": "newguy", "sector_id": 1, "ship_id": 1,
        "corp_id": null, "last_login": "2025-10-20T16:00:00Z", "approx_worth": 10012 }
    ],
    "page": 1, "page_size": 20, "total": 1
  }
}
```

### 5.3 Player Detail

**Request**

```json
{ "command": "sysop.player.get", "data": { "player_id": 1 } }
```

**Response**

```json
{
  "status": "ok",
  "type": "sysop.player_v1",
  "data": {
    "core": {
      "player_id": 1, "name": "newguy",
      "sector_id": 1, "sector_name": "Alpha",
      "ship_id": 1, "ship_name": "Scout"
    },
    "stats": { "credits": 1200, "alignment": 0, "experience": 5 },
    "corp": { "corp_id": null, "role": null },
    "subs": { "count": 2, "topics": ["system.notice","sector.*"] },
    "prefs": [
      {"key":"ui.ansi","type":"bool","value":"true"},
      {"key":"ui.clock_24h","type":"bool","value":"true"},
      {"key":"ui.page_length","type":"int","value":"20"},
      {"key":"ui.locale","type":"string","value":"en-GB"}
    ],
    "last_login": "2025-10-20T16:00:00Z"
  }
}
```

### 5.4 Universe Summary

**Request**

```json
{ "command": "sysop.universe.summary" }
```

**Response**

```json
{
  "status": "ok",
  "type": "sysop.universe.summary_v1",
  "data": {
    "world": { "sectors": 2000, "warps": 4500, "ports": 300, "planets": 150, "players": 20, "ships": 25 },
    "stardock": { "sector_id": 1, "name": "Stardock" },
    "hotspots": { "most_ports": [100, 101], "most_planets": [250, 251] }
  }
}
```

### 5.5 Engine Status

**Request**

```json
{ "command": "sysop.engine_status.get" }
```

**Response**

```json
{
  "status": "ok",
  "type": "sysop.engine_status_v1",
  "data": {
    "link": { "status": "up", "last_hello": "2025-10-20T18:39:55Z" },
    "counters": { "sent": 102, "recv": 101, "auth_fail": 0, "too_big": 0 }
  }
}
```

### 5.6 Jobs (Queues)

**Request**

```json
{ "command": "sysop.jobs.list", "data": { "status": "deadletter", "page": 1, "page_size": 25 } }
```

**Response**

```json
{
  "status": "ok",
  "type": "sysop.jobs_v1",
  "data": {
    "status": "deadletter",
    "items": [
      { "id": 555, "type": "trade", "due_at": "2025-10-20T18:10:00Z",
        "attempts": 3, "error": "timeout to engine" }
    ],
    "page": 1, "page_size": 25, "total": 1
  }
}
```

### 5.7 Logs

**Request**

```json
{ "command": "sysop.logs.tail", "data": { "since_id": 9000, "limit": 50 } }
```

**Response**

```json
{
  "status":"ok",
  "type":"sysop.audit_tail_v1",
  "data": {
    "items": [
      { "id": 9991, "ts": "2025-10-20T18:39:50Z", "cmd_type":"player.kick", "actor_player_id": 2 }
    ],
    "last_id": 9991
  }
}
```

---

## 6) Mutation APIs (Actions) — Messages & Examples

> Mutations include **audit** entries to `engine_audit` and follow **refused vs error** semantics. Unless noted, require `gm` or `sysop`.

### 6.1 System Notice — Create

**Request**

```json
{
  "command": "sysop.notice.create",
  "data": {
    "title": "Maintenance",
    "body": "Server reboot in 10 minutes.",
    "severity": "info",
    "expires_at": "2025-10-20T19:00:00Z"
  }
}
```

**Response**

```json
{
  "status": "ok",
  "type": "sysop.notice.ack_v1",
  "data": { "id": 123, "created_at": "2025-10-20T18:50:00Z" }
}
```

### 6.2 System Notice — Delete

**Request**

```json
{ "command": "sysop.notice.delete", "data": { "id": 123 } }
```

**Response**

```json
{ "status": "ok", "type": "sysop.notice.ack_v1", "data": { "deleted": true } }
```

**Refusals**

* `refused 1407` if not permitted
* `refused 1404` if notice not found

### 6.3 Broadcast (match subs, respect locked semantics)

**Request**

```json
{
  "command": "sysop.broadcast.send",
  "data": {
    "topic": "sector.*",
    "payload": { "type": "ops.message", "text": "Server reboot in 5m" }
  }
}
```

**Response**

```json
{
  "status": "ok",
  "type": "sysop.broadcast.ack_v1",
  "data": { "matched": 42, "delivered": 42 }
}
```

### 6.4 Player Kick / Force Logout

**Request**

```json
{ "command": "sysop.player.kick", "data": { "player_id": 10, "reason": "AFK farm" } }
```

**Response**

```json
{ "status": "ok", "type": "sysop.player.ack_v1", "data": { "kicked": true } }
```

**Refusals**: `1404` if player not found, `1407` if forbidden.

### 6.5 Player DM Block Toggle

**Request**

```json
{ "command": "sysop.player.block", "data": { "player_id": 10, "block_id": 11, "op": "add" } }
```

**Response**

```json
{ "status": "ok", "type": "sysop.player.ack_v1", "data": { "block": "added" } }
```

### 6.6 Jobs — Retry Deadletter

**Request**

```json
{ "command": "sysop.jobs.retry", "data": { "id": 555 } }
```

**Response**

```json
{ "status": "ok", "type": "sysop.jobs.ack_v1", "data": { "retried": true } }
```

### 6.7 Jobs — Cancel

**Request**

```json
{ "command": "sysop.jobs.cancel", "data": { "id": 777 } }
```

**Response**

```json
{ "status": "ok", "type": "sysop.jobs.ack_v1", "data": { "cancelled": true } }
```

### 6.8 S2S Key Rotate

**Request**

```json
{ "command": "sysop.s2s.rotate", "data": { "activate_old_for": 3600 } }
```

**Response**

```json
{
  "status":"ok",
  "type":"sysop.s2s.ack_v1",
  "data": { "new_key_id":"k_20251020_185000", "old_key_active_until":"2025-10-20T19:50:00Z" }
}
```

### 6.9 Cron Toggle

**Request**

```json
{ "command": "sysop.cron.disable", "data": { "name": "stats_rollup" } }
```

**Response**

```json
{ "status":"ok", "type":"sysop.cron.ack_v1", "data": { "name":"stats_rollup", "enabled": false } }
```

### 6.10 Backfill Defaults (#198)

**Request (dry-run)**

```json
{ "command": "sysop.backfill.player_defaults", "data": { "dry_run": true, "batch": 500 } }
```

**Response**

```json
{
  "status":"ok",
  "type":"sysop.backfill.ack_v1",
  "data": { "dry_run": true, "players_examined": 2500, "would_seed": 120 }
}
```

**Request (execute)**

```json
{ "command": "sysop.backfill.player_defaults", "data": { "dry_run": false, "batch": 500 } }
```

**Response**

```json
{
  "status":"ok",
  "type":"sysop.backfill.ack_v1",
  "data": { "dry_run": false, "players_updated": 118 }
}
```

---

## 7) Error & Refusal Codes

* `1401` Not authenticated
* `1403` Invalid topic / bad arguments
* `1404` Not found
* `1405` Topic locked (subscriptions)
* `1407` Forbidden (insufficient role)
* `1503` Database error
* `1101` Not implemented

**Conventions**

* **refused**: caller/policy issue (locked, forbidden, invalid input).
* **error**: server/DB failure.
* Include `"context"` in error’s `data` when helpful.

---

## 8) Audit & Metrics

* Mutations insert into `engine_audit` with:

  * `cmd_type`, `actor_player_id`, `correlation_id` (envelope `id`), and details JSON.
* System health/metrics:

  * S2S counters, RPC/refusal/error rates (exposed via dashboard and logs).
* Optional client-telemetry for SysOp UI as `system_events(scope="sysop_ui")`.

---

## 9) Keyboard Shortcuts

* Global: `?` help, `/` search, `:` palette.
* Nav: `g d`, `g p`, `g u`, `g e`, `g l`, `g a`, `g m`.
* Lists: `j/k` next/prev, `Enter` open, `Esc` close.
* Actions: `x` destructive action prompt, `r` retry job, `D` delete notice.

---

## 10) Wireframes (textual)

* **Dashboard**: server/engine cards; rates; notices list; audit tail.
* **Players**: search input; table (name/id/sector/ship/corp/last_login/worth); detail tabs.
* **Messaging**: notice compose modal; active notices table; broadcast form.
* **Jobs**: status tabs; deadletter details (payload/error); retry button.

---

## 11) Implementation Notes

* Keep mutations **idempotent** via optional `idempotency_key`.
* Use **pagination** (`page`, `page_size`) for large lists.
* Prefer **read-after-write refresh** with optimistic toasts.
* Reuse existing SQL views (`world_summary`, `sector_ops`, `player_info_v1`, etc.).
* Enforce **role** on every mutation; return `refused 1407` when forbidden.
* All responses pass through **ANSI scrub** and carry ISO `ts`.

---

## 12) Acceptance Criteria

* Dashboard loads with live server/engine status and counters.
* Can search players, open detail, and kick a player (audited).
* Can create/delete a system notice; subscribers receive it; deletion acknowledged.
* Broadcast reaches matched subscriptions (locked semantics respected).
* Jobs page lists deadletters; retry moves item to ready.
* All mutating commands produce audit entries; refusals use the specified codes.

---

## 13) Open Questions

* Require re-auth (password) for sensitive actions?
* Segment notices/broadcasts by corp/sector initially or later?
* Which server config knobs (if any) should become editable here?

---

### Appendix A — Subscriptions & Locked Semantics

* Required locked subs (seeded on login; see #194):
  `system.notice` (cannot be unsubscribed; `subscribe.remove` → `refused 1405`).
* Optional subs are user-managed as usual.

### Appendix B — Example Envelope from SysOp Mutation (Audit Correlation)

Server includes `id` in reply; store it as `correlation_id` in `engine_audit` for end-to-end trace.

