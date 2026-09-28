# V2 Client Release Status

Date: 2026-09-27
Scope: Python client, Godot client, and shared assets needed by those clients.

## Release assessment

Core Python and Godot automated client suites pass in the current worktree,
and the shared asset catalogue validates. This is not yet a clean release
candidate checkout: client and asset changes are uncommitted alongside
concurrent server/protocol work, and no live-server smoke session was run.
The disabled Godot flows below remain blocked by server behavior/contracts;
they must remain visibly unavailable until those are resolved.

This review used `docs/V2_CLIENT_READINESS.md`,
`docs/V2_CLIENT_GAMEPLAY_BACKLOG.md`, and
`docs/reports/CLIENT_PROTOCOL_STATUS_AUDIT.md`. The latter is under
`docs/reports/`, rather than the repository-root `docs/` path named in the
assignment.

## Completed

- Python menus use the registered autopilot control command and canonical
  equity exchange/portfolio command names. Autopilot results and refusals are
  presented to the player. The stale `game.get_clock` entry was removed.
- Godot supports the existing sector, navigation/autonav, trade, port,
  planet, shipyard, repair, and player-state flows documented in the readiness
  review. It marks cached data stale after disconnect and does not present
  refused commands as confirmed results.
- Godot uses the shared asset catalogue in the current worktree. Artwork
  variants resolve locally for the supported port, planet, ship, and sector
  visuals.
- No new client changes were needed for the supported V2 flows during this
  stabilisation pass. The known unavailable flows are recorded under
  **Blocked by server**.

## Verified

- Python client: `python3 -m pytest -q client/python_client/tests` — **130
  passed**. The initial sandbox run could not use socket pairs and showed four
  permission failures; the same suite passed when rerun outside that
  restriction.
- Godot 4.5.1 headless: all **11** scripts under
  `client/godot_client/godot/tests/` passed, **186 assertions** total.
- Asset catalogue: `python3 assets/tools/validate_catalog.py` — **PASS**, 13
  assets, zero warnings.
- Verification is automated/local only. There was no live-server test or
  visual GUI/export verification in this pass.

## Blocked by server

These affect specific features, not the basic client startup or the tested
supported gameplay flows:

- **Ship inspection:** `ship.inspect` schema requires `ship_id`, while the
  current handler reads `sector_id` and returns a sector ship list instead of
  details for the selected ship. Godot correctly keeps this action
  unavailable.
- **Ship rename:** current schema and handler require different field names;
  Godot keeps rename unavailable.
- **Private chat and mail send/delete:** the current server schemas and
  handlers disagree on recipient/deletion fields. Godot keeps these actions
  unavailable.
- **Event delivery:** `subscribe.add` accepts the documented `topic` field in
  the current schema and handler, but current server fanout has no active
  subscription-map registration path. Godot's event display can render
  received events, but reliable subscribed delivery is not currently
  available. This requires server work; changing the client request alone
  would not make events arrive.
- **Insurance and rankings:** current handlers return stub/empty data rather
  than authoritative policies or rankings. The client must not present those
  as functioning services.
- **Live-server confirmation:** none of the above or the supported flows were
  exercised against a running release server in this pass.

## Deferred to V3

- Angular/Web player login and gameplay, the HTTPS gateway login/session/API
  bridge, and a mobile client are outside the V2 client release scope.
- Durable event replay, reconnect cursors, atomic cross-domain snapshots, and
  background/mobile session resume are not prerequisites for the tested V2
  local client flows; they remain broader cross-client work.
- Commodity, hardware, and common UI artwork categories are empty in the
  shared catalogue and are not required by the current Python/Godot V2 flows.

## Release gate

Before calling this a V2 client release candidate, preserve the passing test
results and run a live-server smoke of login, sector refresh, warp/autonav,
trade, port entry, planet landing/launch, repair, and command refusal handling
against the exact server build intended for release. Keep server-blocked
features disabled. Package the shared asset changes in the release commit and
verify the Godot project can load them from a clean checkout. This report did
not commit or stage any client changes.

## Files changed in this pass

- `docs/reports/V2_CLIENT_RELEASE_STATUS.md` only.
