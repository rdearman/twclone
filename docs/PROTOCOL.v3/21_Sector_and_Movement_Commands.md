# 21. Sector & Movement Commands

Canonical product decisions: [`../V3_GAMEPLAY_DECISIONS.md`](../V3_GAMEPLAY_DECISIONS.md).

## 1. Sector Information

### `sector.info` (Alias: `move.describe_sector`)
Get detailed info about the current sector or a directly adjacent sector.
**Args**: `{ "sector_id": 1 }` (Optional; remote sectors are refused)
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

`sector.scan` accepts an optional mode:

```json
{ "mode": "holographic" }
```

Holographic scans reveal detailed contents for each directly adjacent sector
without moving the ship. They require the active ship to have a long-range
(holographic) scanner and consume one turn. The server verifies the hardware;
clients cannot claim scanner ownership.

The response type is `sector.holographic.scan`:

```json
{
  "sector_id": 1,
  "scanner": "holographic",
  "sectors": [ { "sector_id": 2, "ports": [], "ships_present": [], "celestial_objects": [] } ]
}
```

Without the mode, `sector.scan` retains its existing detailed current-sector
behavior. A missing scanner returns an error and does not consume a turn.

The Godot client exposes density scanning normally. When `ship.status` reports
`has_long_range_scanner: true`, Sector and Navigation also expose holographic
scanning; the shortcuts are Alt+S for density and Alt+H for holographic.

Detailed sector information and scans omit port class and trade-mode details.

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

The entry resolver checks the existing hostile sector assets after a successful
warp. Fighter and Armid damage consumes the corresponding sector asset units
and applies damage through the existing ship shield/fighter/hull resolution;
destruction is persisted before the movement response. The server records
`combat.hit.fighters`, `combat.hit`, and `combat.limpet.attached` events for
those interactions. Limpet attachment persists on the ship. Ongoing limpet
fuel or turn penalties are not implemented in this entry-trigger slice and
remain deferred to a separately specified follow-up.

When environmental hazards damage the entering ship, the successful movement
response includes a `hazards` array. Each reported hazard uses `hazard_type`
(for example, `nebula`, `radiation`, or `volcanic`), `severity`, `damage`,
`shields_lost`, `fighters_lost`, `hull_lost`, and a `message`. `hazard_type` is
the protocol field name for the hazard kind; it is distinct from the event
envelope's `type`.
If an entry hazard destroys the ship, the error data preserves `sector_id` as
the hazardous destination and includes `current_sector_id` for the Escape Pod's
authoritative respawn sector (sector 1).

### `move.transwarp`
Long-range jump (requires equipment).
**Args**: `{ "to_sector_id": 50 }`; legacy clients may use `{ "sector_id": 50 }`.
**Hazard Trigger**: Triggers all entry hazards (fighters, mines, limpets) at the destination sector.
The same persistence and event behavior applies to transwarp entry. A refused
movement does not invoke the entry resolver or consume sector assets.
If destination-entry damage destroys the transwarping ship, the response
includes both the attempted destination (`sector_id`) and the pod's actual
`current_sector_id`.

Transwarp is an atomic, synchronous movement operation. Eligibility, fuel and
destination validation occur before movement. Once accepted, movement
completes at the destination and existing destination-entry hazards resolve
after arrival in the same transaction. There is no mid-flight interruption
mechanism in V3; interrupted-transwarp behavior is excluded from V3 acceptance
unless a canonical trigger is subsequently established. Do not introduce
arbitrary random interruption.

The original TradeWars manual describes a deployed fighter in the destination
sector as the transwarp homing reference. V3 validates and locks a destination
fighter asset before turn or location mutation. The approved TWGS 3.13 baseline
charges three Fuel Ore per sector of distance. Distance is the shortest
directed warp-hop count in the authoritative universe graph; one directed warp
edge is one sector of distance. Fuel Ore is read from the active ship's
authoritative cargo, and the exact amount is debited atomically with the turn,
movement and arrival-hazard settlement. An unreachable destination has no
defined graph distance and is refused without fuel or turn consumption.
Insufficient Fuel Ore is refused before movement. A same-sector transwarp has
zero distance and therefore costs no Fuel Ore; existing equipment, homing,
turn and hazard checks still apply. Legacy request field `sector_id` remains
accepted alongside `to_sector_id`.

Successful `move.transwarp` responses include `distance_sectors` and
`fuel_ore_consumed`; if entry hazards destroy the ship, the same settled fuel
amount is included in the destruction response data.

### `move.pathfind`
Calculate a route through the current sector and sectors the player has visited.
Unvisited sectors are excluded so the response does not reveal hidden topology.
**Args**: `{ "from_sector_id": 1, "to_sector_id": 10 }`
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
