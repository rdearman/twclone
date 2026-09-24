# Godot Client Phase 2 Progress

## Scope delivered in the current uncommitted working tree

The authenticated gameplay view now renders the normalized sector snapshot rather than reading transport envelopes. The first interaction layer provides synchronized selection from illustrated objects, warp markers, and structured contents rows; adjacent warp destinations can be moved to through a confirmation and correlated `move.warp` request. Successful, refused, timed-out, and disconnected operations have distinct feedback, and movement is followed by authoritative refresh.

The HUD preserves missing values separately from zero, shows credits/turns/cargo/shields/fighters and discreet link/freshness status, and retains the last confirmed view behind a disconnect notice. Sector composition uses a decorative arrangement only; no route geometry, radar, piloting, or coordinate mechanics are implied. All visible ships currently use the same generic vessel asset because current sector records expose only `ship_id` and `name`.

A command drawer is available from the right-side sector pane and from selected-object actions. Its compact category selector and bounded action scroll expose player/ship information, scans and route recommendations; planet information/landing/launch; selected-ship attack/claim/tow (with server-side eligibility checks and no target actions for the active ship); public broadcast; fighter/mine deployment and recall; beacon setting; hardware purchasing; banking/credit transfer; Genesis Torpedo planet creation; news; subscriptions; bookmarks/avoid lists; settings/preferences and notes; corporations; equities; tavern games, graffiti and the paid rumour hint; standalone port quotes; and mail inbox/read. A client-owned field guide describes controls, discrete server-authoritative actions, decorative object placement and stale-state behavior. Hardware purchase uses the implemented server schema (`code`, `quantity`, optional idempotency key); this intentionally does not copy the Python helper's conflicting `item_code` field. Beacon setting uses the confirmed current sector ID and enforces the server's FedSpace restriction and 80-character text limit in the UI. Planet launch is offered only when normalized `ship.onplanet` state confirms the player is landed. A successful `planet.land` response opens an illustrated Planet Surface mode; it requests planet and colonist details, bounds transfer quantities to returned colony/hold counts, requires confirmation, and submits `planet.colonists.set`. Successful transfers refresh player/ship and planet data without requesting invalid sector state while landed. Launch is a discrete command; the surface remains visible on refusal and returns to sector mode only after success. Shipyard upgrades use the authoritative eligible-hull list, disable ineligible hulls, ask for a replacement name, and submit `new_type_id`/`new_ship_name`; the server rechecks eligibility and cost. Deployed fighter/mine lists are rendered as selectable asset rows and recall sends the confirmed asset and current-sector IDs. Dock opens a dedicated port mode only after `port.info` succeeds. Port inventory rows expose buy/sell quantities; both directions request `trade.quote`, show the authoritative quote, require explicit confirmation, submit the documented item/account/sector/idempotency payload, then refresh player/ship state and the port inventory. Port and planet modes leave the top and bottom HUD visible. Commands use correlated transport requests, forms validate numeric inputs, responses are rendered as readable fields rather than raw envelopes, and mutations refresh authoritative state. Hover previews are explicitly marked `PREVIEW · HOVER`, distinct from selected-object state. After authentication, the client requests `sector.*`, `combat.*`, and `trade.*` subscriptions; always-on `system.notice` and recognized event summaries are presented as concise notifications. The last 50 recognized summaries are available in Recent Server Events; unknown event payloads and internal IDs are omitted. Debug, bulk-execution, raw-JSON and SysOp operations are not exposed.

## Confirmed server/client data mismatch

Live `sector.info` data received on 2026-09-23 contains `ships_present` entries with `ship_id` and `name`, but no type identifier. The database query behind sector composition (`db_ships_at_sector_json` in `src/db/repo/repo_cmd.c`) selects only `ship_id` and `name`. Consequently:

- the client uses the same generic silhouette for all visible ships and never guesses class/faction from names;
- numeric ship type IDs are not assumed stable across server configurations;
- the desired future `sector.info` shape should include a stable visual key (preferably a canonical `ship_type.code`) in addition to the mechanical type ID/name;
- no image filenames or visual dimensions belong in gameplay/database records.

`ship.inspect` also has a handler/schema mismatch. Its schema requires `ship_id`, but `cmd_ship_inspect` currently reads optional `sector_id`, ignores the requested ship ID, and returns a sector-wide `ships` list. The client therefore does not present that operation as single-ship inspection. Correcting this requires a separate server contract change and is outside this client-only work.

Other inspected protocol gaps that prevent safe Python-menu parity:

- `chat.send` schema requires `channel` and `message`, but its handler requires `to_player`/`to_id` and `message`; the schema rejects the recipient fields. Private messaging is therefore shown as unavailable. Broadcast uses the compatible `chat.broadcast` operation.
- `mail.send` schema requires `to_player_name`, while its handler reads `to`/`to_id`; the schema rejects those handler fields. `mail.delete` schema requires `mail_id`, while its handler requires an `ids` array and disallows additional fields. Both are withheld.
- `mail.read` schema requires `mail_id`, while its handler reads `id`. The client sends both fields to bridge the current contract, whose schema does not prohibit additional fields.
- `ship.rename` schema accepts only `name`, but its handler requires `ship_id` and `new_name`; the UI withholds it. `ship.jettison` has the inverse mismatch: the schema expects numeric `commodity_id`, while the handler and Python flow consume a commodity string.
- `sector.scan.density`'s implemented handler uses session sector context and accepts no data, while the Python fallback helper may send `sector_id`; the Godot action safely sends an empty object for the current sector.
- Insurance list/buy/claim handlers currently return hard-coded stub responses. They are not presented as functional insurance workflows.
- Python `tow_flow` is only a placeholder, but the Godot client now offers a selected-ship `ship.tow` action backed by the implemented server handler; it limits the affordance to another vessel and leaves ownership/piloting checks to the server. Autopilot control is explicitly marked not implemented in the Python menu and remains unavailable.

## Visual assets and provenance

The runtime references these raster assets:

- `client/godot_client/godot/assets/sector_starfield.png` — 1672×941 RGB sector backdrop;
- `client/godot_client/godot/assets/sector_objects_atlas.png` — 1254×1254 RGBA atlas containing transparent port, ship, and planet artwork;
- `client/godot_client/godot/assets/sector_backdrop.png` — 1672×941 RGB older/alternate backdrop, not referenced by the current `SectorView`.

These files were present in the pre-existing untracked Godot tree. No embedded provenance, source attribution, or generation record was found in the inspected workspace, so this report does not claim an external source or a particular image-generation process. They should be treated as inherited working assets pending provenance/licensing confirmation.

## Current limitations / not yet at Python feature parity

This remains incomplete against `client/python_client/menus.json`. Mail send/delete, insurance purchase/claim, ship renaming, private chat, jettison, single-ship inspection, a full help/settings presentation, and some debug/testing-only or legacy flows remain incomplete or unavailable. The client supports preferences, bookmarks, avoid lists, subscription management, and recent events, but does not reproduce each Python submenu verbatim. No action is silently represented as implemented when its current server contract is unusable; notably, selected-ship inspection is unavailable because the implemented handler ignores the selected ship ID and returns a sector list. The old stock aliases are deprecated in favor of equity commands, while the Python testing/debug/raw-JSON/bulk operations remain intentionally outside normal gameplay.

Other limitations:

- Port trading has local quantity bounds based on returned stock/capacity, but affordability/cargo guidance remains server-authoritative and is not precomputed locally.
- Mouse, keyboard focus, warp marker/list synchronization, command drawer creation, desktop and narrow layouts have been smoke-tested, but a full interactive end-to-end session against the live server is still needed for each mutating domain.
- Screenshots are temporary review artefacts at `/tmp/twclone-sector-showcase-final.png` and `/tmp/twclone-sector-showcase-narrow-final3.png`; they are not project assets.

## Verification performed

- Godot state harness: 22 passed.
- Godot protocol/transport harness: 8 passed.
- Earlier Godot command-menu harness after mail contract handling, preferences, shipyard, and deployment recall workflows: 22 passed.
- Current Godot command-menu harness after selected-port quote, ship claim/tow contextual binding, tavern workflows, recent-event view, field guide, and bounded command drawer: 37 passed.
- Godot event presenter harness: 4 passed (system notices, concise combat summaries, unknown-event suppression, malformed-data suppression).
- Godot planet workflow harness: 6 passed.
- Godot state harness including scoped player/ship refresh while on a planet: 22 passed.
- Godot main scene headless smoke after planet mode integration: successful.
- Interactive planet-mode captures at 1600×900 and 900×780: captured and inspected; persistent HUD remains visible, planet artwork and authoritative values compose correctly, and colonist transfer/launch controls fit the available surface panel. Temporary files: `/tmp/twclone-planet-desktop.png` and `/tmp/twclone-planet-narrow.png`.
- Godot port-workflow harness: 4 passed.
- Godot main scene and showcase scene headless smoke: successful after the final script changes.
- Interactive Godot showcase captures at 1600×900 and 900×780 were captured and inspected after selection/action/warp-marker corrections. The selected warp is consistently Sector 18 in the list, marker, selection card, notification, and `MOVE TO SECTOR 18` action; all visible ships share the same non-type-specific silhouette. Current captures: `/tmp/twclone-sector-final-desktop.png`, `/tmp/twclone-sector-final-narrow.png`, `/tmp/twclone-commands-popup-styled.png`, and `/tmp/twclone-tavern-narrow-bounded.png`. The bounded command modal preserves HUD visibility; the Tavern menu scrollbar exposes its full list, and changing categories left the selected sector object unchanged.
- Python client regression: `112 passed` in the current final verification run.

The current final verification run passed the Python regression suite (`112 passed`), all six Godot harnesses (22 state, 8 protocol/transport, 37 command-menu, 4 port, 6 planet, 4 event presenter), and a Godot main-scene headless startup smoke test. The command drawer was additionally exercised interactively at 900×780: its category selector opened, switched to Tavern, exposed a scrollbar for the long list, and did not alter the background port selection.

All implementation and documentation changes remain uncommitted and unstaged.
