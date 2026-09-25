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
