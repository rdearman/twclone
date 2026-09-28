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
**Events**: 
- Triggers `sector.player_left` (origin) and `sector.player_entered` (dest).
- **Hazard Trigger**: If the destination sector contains hostile fighters, mines (Armid), or limpets, they trigger immediately upon entry.

When environmental hazards damage the entering ship, the successful movement
response includes a `hazards` array. Each reported hazard uses `hazard_type`
(for example, `nebula`, `radiation`, or `volcanic`), `severity`, `damage`,
`shields_lost`, `fighters_lost`, `hull_lost`, and a `message`. `hazard_type` is
the protocol field name for the hazard kind; it is distinct from the event
envelope's `type`.

### `move.transwarp`
Long-range jump (requires equipment).
**Args**: `{ "to_sector_id": 50 }`; legacy clients may use `{ "sector_id": 50 }`.
**Hazard Trigger**: Triggers all entry hazards (fighters, mines, limpets) at the destination sector.

### `move.pathfind`
Calculate route.
**Args**: `{ "from": 1, "to": 10, "avoid": [666] }`
**Response**: `{ "steps": [1, 2, 5, 10], "total_cost": 3 }`

## 3. Autopilot Route Controls

The server stores one current route per player in the existing player
preferences store. Starting another route replaces it. The stored path and
cursor survive disconnects; status reconciles the cursor with the player's
authoritative current sector. If the player is outside the stored path, the
route becomes `reconcile_required` and cannot be resumed.

### `move.autopilot.start`
Get a route for autopilot.
**Args**: `{ "to_sector_id": 542, "from_sector_id": 101 }`
**Response**: `{ "path": [101, 200, ...], "hops": 3 }`
The response keeps the existing `move.autopilot.route_v1` type and includes
`from_sector_id` and `to_sector_id`. A successful request persists this route
with `state: "running"` and `mode: "continue"`.

### `move.autopilot.status`
Returns the persisted route state, `mode`, full `path`, current sector,
`target_sector_id`, and `next_sector_id`, alongside the existing
`current_sector_id` and `last_error` fields. The response remains
`move.autopilot.status_v1`. State is `idle`, `running`, `stopped`, `complete`,
or `reconcile_required`. `next` is retained as a compatibility alias for
`next_sector_id`.

### `move.autopilot.control`
Control the persisted route with `{ "action": "stop_at_next" }`,
`{ "action": "continue" }`, or `{ "action": "express" }`. The response type is
`move.autopilot.controlled_v1` and acknowledges the action, state, current
sector, and next sector. `stop_at_next` changes the route to stopped after the
next successful `move.warp`; `continue` resumes from the reconciled cursor;
`express` persists the requested client execution mode. Express does not bypass
per-hop server validation: each hop still uses `move.warp` and its normal turn,
interdiction, jurisdiction, and entry-hazard checks.

### `move.autopilot.stop`
Stops the persisted route immediately. The existing `move.autopilot.stopped_v1`
response remains, with an additive `state` field. The saved path remains
available for a later `move.autopilot.control` resume if the player is still on
that path.

## 4. Navigation Data (Bookmarks & Avoid)

### `nav.bookmark.add` / `remove` / `list`
Manage personal bookmarks.
`nav.bookmark.add` args: `{ "name": "Home", "sector_id": 1 }`;
`nav.bookmark.remove` args: `{ "name": "Home" }`; `nav.bookmark.list` takes
no arguments. Responses carry the bookmark fields or an `items` array.
`nav.bookmark.set` and `player.set_bookmarks` retain bulk updates using
`{ "bookmarks": [{ "name": "Home", "sector_id": 1 }] }`.

### `nav.avoid.add` / `remove` / `list`
Manage sector avoid list for pathfinding.
`nav.avoid.add` and `nav.avoid.remove` args: `{ "sector_id": 666 }`;
`nav.avoid.list` takes no arguments and returns an `items` array.
`nav.avoid.set` and `player.set_avoids` retain bulk updates using
`{ "avoid": [666] }`.
