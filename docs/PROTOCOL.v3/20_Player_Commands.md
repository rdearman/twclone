# 20. Player Commands

## 1. Profile & Info

### `player.my_info`
Get current player, ship, and location data.
**Response**: `{ "player": {...}, "ships": [...], "location": {...} }`

### `player.list_online`
List online players.
**Args**: `{ "page": 1, "limit": 50 }`
**Response**: `{ "players": [...], "pagination": {...} }`

### `player.rankings`
Get player rankings.
**Args**: `{ "by": "net_worth", "limit": 10 }`
**Response**: `{ "rankings": [...] }`

### `player.computer.recommend_routes`
Request recommended trade loops based on personally visited ports and their known buy/sell profiles. Each result identifies the matching commodity and direction, names both ports, and includes a current gross per-unit price spread estimate. The estimate excludes transaction fees and can change before arrival; the server remains authoritative for quotes and trades.
**Args**:
```json
{
  "max_hops_between": 10,
  "max_hops_from_player": 20,
  "require_two_way": false,
  "limit": 10
}
```
**Response**: `player.computer.trade_routes`
```json
{
  "routes": [
    {
      "port_a_id": 123,
      "port_b_id": 456,
      "port_a_name": "Port Alpha",
      "port_b_name": "Port Beta",
      "sector_a_id": 10,
      "sector_b_id": 15,
      "approach_sector_id": 10,
      "commodity": "ORE",
      "hops_between": 2,
      "hops_from_player": 5,
      "is_two_way": true,
      "a_to_b": true,
      "b_to_a": true,
      "estimated_profit_a_to_b": 18,
      "estimated_profit_b_to_a": -4
    }
  ],
  "pathing_model": "full_graph",
  "truncated": false,
  "pairs_checked": 45
}
```

## 2. Settings & Preferences

### `player.get_settings`
Retrieve preferences, bookmarks, avoid sectors, and subscriptions in one bundle.
Notes are managed by the separate `notes.*` commands and are not included.
**Response**: `player.settings_v1`
```json
{
  "prefs": { "ui.theme": "dark" },
  "subscriptions": [...],
  "avoid": [...],
  "bookmarks": [...]
}
```

### `player.set_prefs` / `player.get_prefs`
Manage key-value preferences (e.g., UI theme, locale). `player.set_prefs`
accepts the legacy arbitrary-key object form (`{ "ui.theme": "dark" }`) and
the v3 item form (`{ "items": [{ "key": "ui.theme", "type": "string",
"value": "dark" }] }`). The current handler stores primitive values as strings;
`type` is accepted for compatibility but does not affect storage. `get_prefs`
returns `{ "prefs": { ... } }` in the `player.prefs` response.

### `player.get_subscriptions` / `player.get_topics`
Return the player's subscriptions as `player.subscriptions` with a `topics`
array. Both command names are supported for compatibility.

### `player.set_subscriptions` / `player.set_topics`
Set subscriptions using `{ "subscriptions": ["player.attacked"] }` or
`{ "topics": ["player.attacked"] }`. Entries may be topic strings or objects
with a `topic` string. Both command names accept either payload key and return
the updated `player.subscriptions` response.

### `notes.set` / `notes.delete` / `notes.list`
Manage personal notes attached to scopes (port, sector, player).
`notes.set` args: `{ "scope": "port", "key": "501", "note": "Cheap Ore" }`.
`notes.delete` args: `{ "scope": "port", "key": "501" }`.
`notes.list` accepts an optional scope filter: `{ "scope": "port" }`;
without it, all of the player's notes are returned. Responses are
`player.note.set_v1`, `player.note.deleted_v1`, and `player.notes_v1` with a
`notes` array of `{ "scope", "key", "note" }` objects.

## 3. Fines (Legal)

### `fine.list`
List outstanding fines.
**Response**: `{ "fines": [{ "fine_id": "...", "amount": "100.00", "reason": "..." }] }`

### `fine.pay`
Pay a fine.
**Args**: `{ "fine_id": "..." }`
**Response**: `fine.paid`

## 4. Session Management

*   **`auth.logout`**: End session.
*   **`session.ping`**: Keep-alive.
