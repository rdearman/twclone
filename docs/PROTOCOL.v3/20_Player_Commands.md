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
Request recommended trade loops based on personally visited ports and their known buy/sell profiles. This does not use global market data (prices/stock).
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
      "sector_a_id": 10,
      "sector_b_id": 15,
      "hops_between": 2,
      "hops_from_player": 5,
      "is_two_way": true
    }
  ],
  "pathing_model": "full_graph",
  "truncated": false,
  "pairs_checked": 45
}
```

## 2. Settings & Preferences

### `player.get_settings`
Retrieve all settings (prefs, bookmarks, avoid, notes) in one bundle.
**Response**: `player.settings_v1`
```json
{
  "prefs": [...],
  "subscriptions": [...],
  "avoid": [...],
  "bookmarks": [...],
  "notes": [...]
}
```

### `player.set_prefs` / `player.get_prefs`
Manage typed Key-Value preferences (e.g., UI theme, locale).
**Args**: `{ "items": [{ "key": "ui.theme", "type": "string", "value": "dark" }] }`

### `notes.set` / `notes.delete` / `notes.list`
Manage personal notes attached to scopes (port, sector, player).
**Args**: `{ "scope": "port", "key": "501", "note": "Cheap Ore" }`

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
