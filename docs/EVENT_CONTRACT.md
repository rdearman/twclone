# TWClone Event Contract (Server ⇄ Client)

**Contract version:** 1
**Updated:** 2025-10-20

This document defines the **envelope**, **events**, **commands (RPCs)**, **types/units**, and **localisation** rules for messages exchanged between client and server. It is the single source-of-truth for the client UI and for server compatibility guarantees.

---

## 1) Envelope (always present)

Every server→client message and RPC reply is a JSON object with this shape:

```json
{
  "id": "srv-ok",                 // server message id
  "reply_to": "req-uuid",         // request id being answered (if any)
  "ts": "2025-10-20T18:23:40Z",   // ISO-8601 UTC
  "status": "ok",                 // ok | refused | error
  "type": "session.hello",        // event or reply type
  "data": { /* payload */ },      // object or null
  "error": null,                  // {code:int, message:string} when not ok
  "meta": {                       // optional transport metadata
    "rate_limit": { "limit":60, "remaining":60, "reset":60 }
  }
}
```

**Server guarantees**

* `ts` is **ISO-8601 UTC**.
* All **strings are sanitized**: ANSI escape codes are stripped before send.
* **No prose** inside `data`. All renderable content is represented as **codes/ids/enums**.
* Backwards compatibility: within a given `type`/**version** (`v`), fields are only **added** (never removed/renamed). Breaking changes require a version bump (see below).

---

## 2) Versioning policy (per event)

Each **event** (and some replies) carries a `v` field inside `data`:

```json
"type": "combat.hit",
"data": { "v": 1, ... }
```

* **Additive-only** within a version: you may add optional fields.
* **Breaking change** → bump `v` and keep prior versions available during a migration window.

---

## 3) Types, units & naming

* **Event type** format: `domain.action[.subaction]` (e.g., `engine.tick`, `nav.sector.enter`, `combat.hit`).
* **Field case:** `lower_snake_case`.
* **IDs:** numeric and suffixed with `_id` (e.g., `player_id`, `sector_id`).
* **Numbers & units:**

  * Distances: **sectors** (int)
  * Currency: **credits** (int)
  * Durations: **seconds** (int) unless otherwise noted
  * Percentages: **0–100** (int)
  * Hit points: **hp** (int)

---

## 4) Subscriptions & topics

* Events are published to topic namespaces (e.g., `system.notice`, `sector.*`, `combat.*`).
* Clients control delivery with **subscriptions**:

  * **Locked topics** (e.g., `system.notice`) are **always on**; unsubscribe is refused with `1405 Topic is locked`.
  * Optional topics can be added/removed idempotently.
* Broadcast respects saved subscriptions (**#195**).

---

## 5) Authentication & sessions

* Some commands require an authenticated session (`1401 Not authenticated` if missing).
* On login/register success, server returns `auth.session` with a `session_token`.

---

## 6) Error codes (subset in use)

| Code | Meaning                 |
| ---- | ----------------------- |
| 1101 | Not implemented         |
| 1105 | Validation failed       |
| 1210 | Username already exists |
| 1401 | Not authenticated       |
| 1402 | Bad arguments           |
| 1403 | Invalid topic           |
| 1405 | Topic is locked         |
| 1407 | Rate limited            |
| 1503 | Database error          |

(Extend as needed; keep meanings stable.)

---

## 7) Commands (RPCs) — Requests & Replies

Below are the **client→server** commands with concrete **request** and **response** examples.

### 7.1 `system.hello` (handshake)

**Request**

```json
{ "id":"<uuid>", "command":"system.hello",
  "data": { "client":"tw-client", "version":"1.0" } }
```

**Reply**

```json
{
  "id":"srv-ok","reply_to":"<uuid>","ts":"2025-10-20T18:22:00Z",
  "status":"ok","type":"session.hello",
  "data":{
    "protocol_version":"1.0",
    "server_time_unix":1760984520,
    "authenticated":false,
    "player_id":null,
    "current_sector":null,
    "server_time":"2025-10-20T18:22:00Z"
  },
  "error":null,"meta":{"rate_limit":{"limit":60,"remaining":60,"reset":60}}
}
```

> When authenticated, `authenticated=true`, `player_id`, `current_sector` are set.

---

### 7.2 `auth.register`

**Request**

```json
{
  "id":"<uuid>", "command":"auth.register",
  "data": { "user_name":"NewPilot", "password":"secret" }
}
```

**Reply (success)**

```json
{
  "status":"ok","type":"auth.session",
  "data":{ "player_id": 123, "session_token":"<token>" }
}
```

**Reply (username exists)**

```json
{ "status":"error","type":"error","error":{"code":1210,"message":"Username already exists"} }
```

> On success, login hydration runs (#194): required defaults and **locked subscriptions** are upserted.

---

### 7.3 Subscriptions: `subscribe.add`, `subscribe.remove`, `subscribe.list`

**Add (idempotent)**

```json
{ "id":"<uuid>","command":"subscribe.add","data":{ "topic":"sector.*" } }
```

```json
{ "status":"ok", "type":"subscribe.ack_v1", "data":{ "topic":"sector.*","added":true } }
```

**Remove (idempotent)**

```json
{ "command":"subscribe.remove","data":{ "topic":"sector.42" } }
```

```json
{ "status":"ok","type":"subscribe.ack_v1","data":{ "topic":"sector.42","removed":true } }
```

**Refuse locked**

```json
{ "status":"refused","type":"error","error":{"code":1405,"message":"Topic is locked"} }
```

**List**

```json
{ "command":"subscribe.list" }
```

```json
{ "status":"ok","type":"subscribe.list_v1","data":{ "topics":["system.notice","sector.*"] } }
```

---

### 7.4 Player prefs (typed KV): `player.set_prefs`, `player.get_prefs`  ✅ (shipped)

**Set**

```json
{
  "command":"player.set_prefs",
  "data":{ "items":[
    { "key":"ui.locale", "type":"string", "value":"en-GB" },
    { "key":"ui.clock_24h","type":"bool",   "value":"true" }
  ] }
}
```

```json
{ "status":"ok","type":"player.pref.ack_v1","data":{"updated":2} }
```

**Get**

```json
{ "command":"player.get_prefs" }
```

```json
{
  "status":"ok","type":"player.prefs_v1",
  "data":{"prefs":[
    {"key":"ui.ansi","type":"bool","value":"true"},
    {"key":"ui.clock_24h","type":"bool","value":"true"},
    {"key":"ui.locale","type":"string","value":"en-GB"},
    {"key":"ui.page_length","type":"int","value":"20"},
    {"key":"privacy.dm_allowed","type":"bool","value":"true"}
  ]}
}
```

> Server validates types; strings are ANSI-scrubbed on output.

---

### 7.5 Navigation Avoid List (required for #210): `nav.avoid.add/remove/list`

**Add (idempotent)**

```json
{ "command":"nav.avoid.add","data":{"sector_id":278} }
```

```json
{ "status":"ok","type":"nav.avoid.ack_v1","data":{"sector_id":278,"added":true} }
```

**Remove (idempotent)**

```json
{ "command":"nav.avoid.remove","data":{"sector_id":278} }
```

```json
{ "status":"ok","type":"nav.avoid.ack_v1","data":{"sector_id":278,"removed":true} }
```

**List**

```json
{ "command":"nav.avoid.list" }
```

```json
{ "status":"ok","type":"nav.avoid.list_v1","data":{"sectors":[278, 999]} }
```

Errors:

* `1401` unauthenticated
* `1402` bad arguments (missing/invalid `sector_id`)
* Optional: `1404` sector not found (if enforced)

---

### 7.6 Bookmarks (optional for #210): `nav.bookmark.add/remove/list`

**Add**

```json
{ "command":"nav.bookmark.add","data":{"name":"Dock","sector_id":1} }
```

```json
{ "status":"ok","type":"nav.bookmark.ack_v1","data":{"name":"Dock","added":true} }
```

**Remove**

```json
{ "command":"nav.bookmark.remove","data":{"name":"Dock"} }
```

```json
{ "status":"ok","type":"nav.bookmark.ack_v1","data":{"name":"Dock","removed":true} }
```

**List**

```json
{ "command":"nav.bookmark.list" }
```

```json
{
  "status":"ok","type":"nav.bookmark.list_v1",
  "data":{"bookmarks":[{"name":"Dock","sector_id":1},{"name":"HQ","sector_id":278}]}
}
```

---

### 7.7 Notes (scoped): `notes.set/delete/list`

**Set / upsert**

```json
{ "command":"notes.set","data":{"scope":"port","key":"501","note":"Great ore prices"} }
```

```json
{ "status":"ok","type":"notes.ack_v1","data":{"scope":"port","key":"501","set":true} }
```

**Delete**

```json
{ "command":"notes.delete","data":{"scope":"port","key":"501"} }
```

```json
{ "status":"ok","type":"notes.ack_v1","data":{"scope":"port","key":"501","deleted":true} }
```

**List (by optional scope)**

```json
{ "command":"notes.list","data":{"scope":"port"} }
```

```json
{
  "status":"ok","type":"notes.list_v1",
  "data":{"items":[{"scope":"port","key":"501","note":"Great ore prices"}]}
}
```

---

### 7.8 Settings aggregator: `player.get_settings` (#210)

**Request**

```json
{ "command":"player.get_settings" }
```

**Reply**

```json
{
  "status":"ok","type":"player.settings_v1",
  "data":{
    "prefs":[ /* as in player.prefs_v1 */ ],
    "subscriptions":["system.notice","sector.*"],
    "avoid":[278, 999],
    "bookmarks":[{"name":"Dock","sector_id":1}],
    "notes":[{"scope":"port","key":"501","note":"Great ore prices"}]
  }
}
```

> This is a convenience wrapper over the granular RPCs. Clients should prefer this on login.

---

### 7.9 Localisation negotiation (Epic 19)

#### 7.9.1 Client → Server: `client.hello` (locale and catalog info)

**Request**

```json
{
  "id":"<uuid>",
  "command":"client.hello",
  "data":{
    "ui_locale":"en-GB",
    "catalogs":[
      { "name":"core", "hash":"sha256:abcd..." },
      { "name":"combat", "hash":"sha256:ef01..." }
    ],
    "supports":{
      "format":"icu-messageformat@1",
      "plural":"cldr@v42"
    }
  }
}
```

**Reply**

```json
{
  "status":"ok","type":"client.hello.ack_v1",
  "data":{
    "schema_version":1,
    "missing_catalogs":[{"name":"core","hash":"sha256:newhash"}],     // fetch these
    "server_locale":"en-GB"
  }
}
```

#### 7.9.2 Manifest fetch: `i18n.manifest.get` (#203)

**Request**

```json
{ "command":"i18n.manifest.get" }
```

**Reply**

```json
{
  "status":"ok","type":"i18n.manifest_v1",
  "data":{
    "contract_version":1,
    "updated_at":"2025-10-20T00:00:00Z",
    "events":[
      { "type":"engine.tick", "v":1, "keys":["engine.tick.title","engine.tick.body"] },
      { "type":"combat.hit",  "v":1, "keys":["combat.hit.title","combat.hit.body"] }
    ]
  }
}
```

> This is the machine-readable index (keys + versions) the client uses to validate its catalogs.

---

## 8) Events (server→client)

These are **broadcasts** the client might receive, gated by subscriptions.

### 8.1 `engine.tick` (v1)

```json
{
  "status":"ok","type":"engine.tick",
  "data":{ "v":1, "tick":123456, "dt":1, "universe_time":"2025-10-20T18:50:03Z" }
}
```

### 8.2 `nav.sector.enter` (v1)

```json
{
  "status":"ok","type":"nav.sector.enter",
  "data":{ "v":1, "player_id":14, "sector_id":42, "from_sector_id":41 }
}
```

### 8.3 `combat.hit` (v1)

```json
{
  "status":"ok","type":"combat.hit",
  "data":{
    "v":1, "attacker_id":12, "defender_id":99,
    "weapon":"laser_mk2", "damage":42, "defender_hp_after":58,
    "sector_id":278
  }
}
```

### 8.4 `trade.deal.matched` (v1)

```json
{
  "status":"ok","type":"trade.deal.matched",
  "data":{
    "v":1,"player_id":14,"port_id":501,"commodity":"ore",
    "quantity":100,"price_per_unit":45,"total_price":4500,"sector_id":42
  }
}
```

### 8.5 `system.notice` (v1) — **locked subscription**

```json
{
  "status":"ok","type":"system.notice",
  "data":{
    "v":1,
    "notice_id":1001,
    "title":"Scheduled Maintenance",
    "body":"Server going down in 10 minutes.",
    "severity":"warn",
    "expires_at":"2025-10-20T19:10:00Z"
  }
}
```

---

## 9) Localisation keys (Epic 19)

Server never sends prose for events; clients render text using **catalogs** keyed by stable identifiers. Below is a **sample key set** (illustrative — grow as needed):

```text
engine.tick.title
engine.tick.body

nav.sector.enter.title
nav.sector.enter.body

combat.hit.title
combat.hit.body

trade.deal.matched.title
trade.deal.matched.body

system.notice.title
system.notice.body
```

**Example (ICU-style)**

```json
{
  "combat.hit.title": "Hit!",
  "combat.hit.body": "{attacker_id} hit {defender_id} with {weapon} for {damage} HP (remaining: {defender_hp_after})."
}
```

Client renders by mapping **event data fields** to template variables. If a field is missing (older server), templates must degrade gracefully.

---

## 10) Security & sanitization

* **All outbound strings** are run through ANSI scrubbing (server-side).
* User-provided strings stored in server DB (e.g., notes) are scrubbed on **output**.
* Do not include unvetted user content in keys or enums.

---

## 11) Capability advertisement

Server may publish capabilities (e.g., via `session.hello` or a dedicated endpoint):

```json
{
  "limits": { "max_bulk":100, "max_page_size":50, "max_beacon_len":256 },
  "features": {
    "auth": true,
    "warp": true,
    "sector.describe": true,
    "trade.buy": true,
    "nav.avoid": true,
    "nav.bookmark": true,
    "notes": true
  },
  "version":"1.0.0-alpha"
}
```

---

## 12) Testing guidance (client & integration)

* Always start with `system.hello`, then `auth.register`/`auth.login`.
* Assert that **locked topics** (e.g., `system.notice`) are present after login and cannot be removed.
* Use `player.get_settings` on login to hydrate UI (prefs, subs, avoid, bookmarks, notes) in one call.
* When adding new events, update:

  1. This contract (new section under **Events**),
  2. The **manifest** (keys + `v`),
  3. JSON test fixtures (subscribe to topic, assert shape).

---

## 13) Appendix — Common enums (initial set)

```json
{
  "weapon_code": ["laser_mk1", "laser_mk2", "plasma_cannon", "railgun"],
  "notice_severity": ["info", "warn", "error"],
  "commodity": ["ore","organics","equipment"]
}
```

---

### Changelog

* **v1 (2025-10-20):** Initial contract including envelopes, prefs RPCs, subscriptions, avoid/bookmark/notes RPCs, settings aggregator, localisation negotiation stubs, and core events (`engine.tick`, `nav.sector.enter`, `combat.hit`, `trade.deal.matched`, `system.notice`).

