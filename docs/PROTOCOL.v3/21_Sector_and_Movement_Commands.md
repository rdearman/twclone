# 21. Sector & Movement Commands

## 1. Sector Information

### `sector.info` (Alias: `move.describe_sector`)
Get detailed info about the current or specified sector.
**Args**: `{ "sector_id": 1 }` (Optional)
**Response**: `sector.info`
```json
{
  "sector_id": 1,
  "name": "Sol",
  "adjacent_sectors": [...],
  "celestial_objects": [...],
  "ports": [...],
  "ships_present": [...],
  "players_present": [...]
}
```

### `sector.scan` / `move.scan`
`move.scan`: Summary scan (counts).
`sector.scan`: Detailed scan (objects, resources).
`sector.scan.density`: Density scan of surrounding sectors.

### `sector.search`
Search for objects/players.
**Args**: `{ "q": "name", "type": "player|ship|...", "limit": 10 }`

### `sector.set_beacon`
Set a beacon message.
**Args**: `{ "sector_id": 1, "text": "Message" }`

## 2. Movement

### `move.warp`
Move to an adjacent sector.
**Args**: `{ "to_sector_id": 2 }`
**Response**: `move.result` `{ "player_id": ..., "current_sector": 2 }`
**Events**: Triggers `sector.player_left` (origin) and `sector.player_entered` (dest).

### `move.transwarp`
Long-range jump (requires equipment).
**Args**: `{ "sector_id": 50 }`

### `move.pathfind`
Calculate route.
**Args**: `{ "from": 1, "to": 10, "avoid": [666] }`
**Response**: `{ "steps": [1, 2, 5, 10], "total_cost": 3 }`

## 3. Autopilot (Client-Side)

The server provides routes; the client executes hops.

### `move.autopilot.start`
Get a route for autopilot.
**Args**: `{ "to_sector_id": 542, "from_sector_id": 101 }`
**Response**: `{ "path": [101, 200, ...], "hops": 3 }`

### `move.autopilot.status`
Check readiness.
**Response**: `{ "state": "ready", "mode": "client" }`

### `move.autopilot.stop`
No-op/Ack command for UI consistency.

## 4. Navigation Data (Bookmarks & Avoid)

### `nav.bookmark.add` / `remove` / `list`
Manage personal bookmarks.
**Args**: `{ "name": "Home", "sector_id": 1 }`

### `nav.avoid.add` / `remove` / `list`
Manage sector avoid list for pathfinding.
**Args**: `{ "sector_id": 666 }`
