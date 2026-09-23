# Godot Client Audit and Parity Programme

## 1. Executive assessment

The Godot client is an early single-scene prototype, not a second mature client. It can open a TCP connection, harvest server schemas, submit a small subset of commands, and display basic player, ship and sector data. It does not yet provide a reliable playable game loop.

The main gaps are architectural rather than cosmetic:

- no request/reply correlation;
- no typed client state or authoritative refresh model;
- no safe refusal/error state handling;
- no usable port, trade, communications, planet, combat or ship workflows;
- raw protocol responses are displayed to normal players;
- server command discovery is treated as a complete UI/API contract;
- no Godot tests or runtime validation are available in this checkout.

An incremental rebuild around reusable transport, state and view-model components is justified. A wholesale rewrite is not yet necessary: the existing scene can remain the shell while networking and state boundaries are extracted behind it.

## 2. Repository and runtime baseline

Initial and final repository checks showed the same status. No files were edited, created, staged, deleted, restored or committed during the audit.

- HEAD: `7c69ff13 feat(python-client): add responsive terminal rendering`
- Python client tests: `112 passed in 0.69s`
- Godot project: `client/godot_client/godot`
- Declared Godot version: 4.5, Forward Plus renderer
- Installed Godot executable: none found as `godot` or `godot4`
- Godot tests: none found
- Final overall dirty status entries: 1330
- Focused status: `?? client/godot_client/`

The untracked Godot tree was already present and remained unchanged. The focused Python-client documentation files remained clean.

Relevant commits confirmed:

```text
7c69ff13 feat(python-client): add responsive terminal rendering
7a2691fd feat(python-client): improve communications workflows
8c5b90f3 feat(python-client): add quoted port trading workflow
c5b9892a refactor(python-client): streamline main navigation
```

The Python suite was used as the behavioural benchmark. The Godot-specific `verify_schema_harvest.py` helper is a live-server probe, not a test; running it with `--help` attempted a connection to the literal host `--help` and failed without changing the repository.

No graphical session or Godot runtime was available, so visual correctness and scene execution could not be verified.

## 3. Godot architecture map

### Project and scene structure

`client/godot_client/godot/project.godot` declares Godot 4.5 / Forward Plus and `Main.tscn` as the entry scene. There are no autoloads, input-map actions or test configuration. The project contains no separate model/view scripts or domain resources beyond icon/import artefacts.

`Main.tscn` contains one scripted root `Control` with:

- a static News panel;
- left player/ship panel;
- central scrolling output log;
- right sector panel;
- bottom command `LineEdit`;
- `HFlowContainer` action deck;
- autocomplete `PopupMenu`.

`Main.gd` is approximately 703 lines containing networking, authentication, schema discovery, command parsing, state, rendering and navigation. `Main.gd.old`, `Main.gd~` and `Main.tscn~` are backup artefacts, not active architecture.

### Networking and framing

`Main.gd:14-20, 121-260, 599-628` uses `StreamPeerTCP`, polls it every frame, reads newline-delimited JSON and writes newline-delimited JSON. It generates an ID and timestamp for most non-system commands and adds the session token as `auth.session`.

Missing are a request registry, timeout handling, `reply_to` correlation, stale-response protection, concurrent-operation control, mutation locking, event/reply separation and structured refusal propagation.

The server envelope implementation confirms that replies identify the originating request through `reply_to`. The Godot client never reads or uses this field.

### Authentication and configuration

`Main.gd:70-119, 445-535` persists server IP, port, username and session token in `user://client_config.cfg`, always prompts for a password, does not use the saved token to refresh a session, and harvests all advertised schemas before login.

The server schema and `src/server_auth.c:cmd_auth_login` require `username` and `passwd`. The Godot fallback in `_get_field_name` is `password`, not `passwd`; login can therefore fail when schema harvesting is unavailable or incomplete.

### Event and state handling

There is no event queue, event reducer or event presenter. All frames pass through `_process_message`; unknown successful responses are printed as JSON. Unknown asynchronous events are indistinguishable from replies.

State consists mainly of `player_cache`, `ship_cache`, `session_token` and `player_id`. There is no structured sector, port, trade, communications, navigation or operation state. Views interpret protocol dictionaries directly.

### Navigation and input

The action deck dynamically builds buttons from server command names, drills into dot-separated categories, supports a Back button and pre-fills slash commands for schemas with required fields. Otherwise it submits an empty payload.

There is no domain-aware navigation model, global Escape/Back policy, focus policy, shortcut system or operation-state feedback. Keyboard support is limited to text entry, autocomplete and Tab.

### Presentation

The interface is mostly protocol output. The News panel is static text and is not connected to a news command. Errors are partly human-readable, but successful unknown responses and parse failures can expose raw JSON. The persistent network log writes complete request JSON, including authentication data and session tokens.

## 4. Functional parity matrix

| Domain | Python behaviour | Godot behaviour | Server evidence | Parity status | User impact | Recommended action |
|---|---|---|---|---|---|---|
| Launch/configuration | Structured startup and safe terminal fallback | Persisted IP/port/user/token, with little validation | `project.godot`; `Main.gd:70-119` | Partially implemented | Startup and invalid input states are unclear | Add explicit connection state view and validation |
| Connection | Transport owns connection and safe failure | `StreamPeerTCP` polling in `Main.gd` | `Main.gd:123-161`; `protocol.py` | Partially implemented | Errors do not reliably transition the UI | Extract transport service |
| Login/authentication | Correlated RPC and readable failures | Schema harvest and password prompt, but type-based response handling | `src/schemas.c`; `src/server_auth.c` | Present but broken | Wrong fallback field and stale replies can break login | Typed auth request and correlation |
| Character/player selection | Username-based player state | Username only; no selection | `auth.login` handler | Missing | Selection cannot be performed if required | Confirm handler support before adding |
| Disconnect handling | Safe mid-session disconnect | Only toggles a flag and logs | `Main.gd:154-161` | Present but broken | UI may remain apparently usable | Centralize disconnect state |
| HUD/player state | Credits, turns, cargo, fighters, shields, unread indicators | Basic player/ship text | Python `hud.py`; `Main.gd:264-306` | Partially implemented | Important information is missing or defaults to zero | Add normalized HUD model |
| Current ship | Uses `ship.status` and current fields | Requests `ship.info`; expects `hp/max_hp` | `server_loop.c:562`; Python `hud.py` | Present but broken | Ship state can display false zeroes | Normalize `ship.status` |
| Sector/current location | Normalized sector and redisplay flow | Raw-ish sector display, defaults ID to 0 | `server_loop.c:496`; `Main.gd:308-348` | Partially implemented | Location and actions are unreliable | Add sector normalizer and contextual actions |
| Movement/navigation | Menu-driven actions and authoritative refresh | Manual `/move/warp/...`; generic command list | Movement handlers; `Main.gd:202-205` | Partially implemented | Refusals can appear successful | Correlate and refresh only on success |
| Avoids/bookmarks | Grouped workflows where supported | No dedicated UI/state | Confirm concrete handlers first | Missing | Navigation preferences unavailable | Add only after handler confirmation |
| Ports/docking | `port.info` on entering DOCK and normalized inventory | `port.dock` appears statically but is not registered; no dock flow | `server_loop.c:474-479` | Present but broken | Impossible command and no port context | Add context-aware port screen |
| Port summary | Stock, capacity, availability and indicative prices | No port presentation | `cmd_trade_port_info` | Missing | Player cannot understand a port | Add normalized port view |
| Quote-first trading | `trade.quote` before mutation | Direct buy/sell only | `cmd_trade_quote`; Python `dock_trade_flow` | Missing | No authoritative cost/proceeds preview | Add quote model and screen |
| Trade confirmation | Explicit Y/N, default No | None | Python trade tests | Missing | Unsafe mutation workflow | Add modal confirmation |
| Trade payload/idempotency | Port/items/account/sector/idempotency | Commodity/quantity and generic request UUID | `src/server_ports.c` | Present but broken | Server refuses or malformed requests | Typed trade request |
| Trade receipts | Direction-aware buy/sell wording | Generic JSON | Server receipt construction; Python presenter | Missing | Player cannot tell what changed | Add receipt presenter and refresh |
| Planets/citadels | Grouped workflows where supported | Planet names only | Planet handlers/registry | Missing | Planet gameplay inaccessible | Add after core navigation |
| Ships/boarding | Ship workflows with refreshes | Passive ship fetch only | Ship handlers; Python flows | Missing | No ship management | Add read-only ship screen first |
| Fighters/mines/beacons/towing | Command groupings where implemented | Beacon is text only | Server registries/handlers | Missing | Deployment gameplay inaccessible | Add validated operations |
| Combat | Combat groupings where supported | No combat UI/state | Combat handlers/tests | Missing | Combat cannot be played | Add after mutation framework |
| Communications landing | COMMS landing with Chat/Mail/Events/Notices | None | Communication handlers | Missing | Communications inaccessible | Add landing screen |
| Chat | Readable history and confirmed send | None | `cmd_chat_history` and send handlers | Missing | Chat unavailable | Add history first |
| Mail | Indexed inbox/detail/read flow | None | `mail.inbox`, `mail.read` | Missing | Mail unavailable | Add indexed inbox |
| Notices | Readable notices and seen state | None | `notice.list`, `notice.ack` | Missing | Notices hidden | Add notice list without fake totals |
| Async events | Bounded queue at safe prompts | No queue; generic log output | Envelope `reply_to`; Python `events.py` | Present but broken | Events can be misclassified | Add event decoder and queue |
| Corporations | Grouped workflows where supported | None | Server registry/handlers | Missing | Corporation gameplay unavailable | Add after common patterns |
| News | News workflow where handler exists | Static “Game News” text | News handlers require confirmation | Stubbed | Misleading availability | Implement or show unavailable |
| Notes/logs | Player notes/logs where supported | Only developer network log | Notes/log handlers | Missing | Player notes unavailable | Separate player notes from diagnostics |
| Settings | Uses `player.get_settings` | Advertises nonexistent `player.settings` | `server_loop.c:444` | Present but broken | Settings action is invalid | Use confirmed command |
| Help | Menu guidance | None | No Godot help evidence | Missing | New players lack guidance | Add contextual help |
| Debug-only functions | Debug paths separated | All advertised commands become buttons | `Main.gd:223-235` | Present but broken | Developer operations leak into UI | Filter and gate diagnostics |
| Errors/refusals | Human-readable, no raw JSON | Mixed formatting and false movement success | `server_envelope.c`; `Main.gd:181-205` | Present but broken | Success/refusal is ambiguous | Map status/error codes |
| Money | Non-lossy `money.py` handling | Strings/default zero; no arithmetic | Trade schemas/receipts | Unverifiable risk | Future trade arithmetic may be wrong | Create integer-money model |
| Responsive layout | Width-aware terminal layout | Fixed panels and flow buttons | `Main.tscn` | Partially implemented | Narrow windows become crowded | Add responsive containers |
| Accessibility/input | Keyboard-safe prompts and predictable menus | Mouse buttons and text entry only | `Main.tscn`; `Main.gd:698-703` | Partially implemented | Keyboard play is awkward | Establish focus and Back rules |

## 5. Ranked UI/UX findings

### Critical

#### C1 — Mutating trades are not quote-first, validated or idempotent

`Main.gd:578-585` sends only `commodity` and `quantity`. It does not call `trade.quote`, confirm, or send `port_id`, `items`, `account`, `sector_id` or `idempotency_key`. The handlers in `src/server_ports.c` validate those fields. The Python reference is `client/python_client/client.py:dock_trade_flow` and `tests/test_port_trading.py`.

The player can submit malformed or unsafe trades and receives no authoritative preview. Implement a typed port/trade model, quote screen, explicit confirmation, UUID idempotency key, direction-aware receipt and post-success refresh.

#### C2 — Reply correlation is absent

`Main.gd:173-260` dispatches by `type` and command name only. `reply_to` is ignored even though the server includes it. Delayed or concurrent replies can update the wrong workflow. Add tracked request objects, timeouts and typed completion signals.

#### C3 — Refusals can be presented as successful movement

`Main.gd:202-205` treats every `move.result` as “Move successful.” Server refusals use `status: refused`, `type: error` and an error object. Inspect status before success messaging, state mutation or refresh.

#### C4 — Normal play exposes raw protocol data

`Main.gd:181-189` prints unknown response data with `JSON.stringify`; `Main.gd:260` prints the complete malformed line. `Main.gd:618-637` also persists complete request JSON, including secrets. Separate diagnostics from player messages and redact credentials.

### High

#### H1 — Authentication fallback uses the wrong field

The server requires `passwd`, while `_get_field_name` falls back to `password` (`Main.gd:516-524`). Use the confirmed handler field and make schema discovery optional for basic login.

#### H2 — Server command discovery is being used as application design

`Main.gd:223-235` turns every advertised command into a menu entry. Static commands include nonexistent `port.dock` and `player.settings`. Maintain a player-safe application catalogue and use capabilities only to gate entries.

#### H3 — There is no meaningful primary gameplay loop

`Main.tscn` contains panels, a log and a command prompt, but no sector object model, contextual action area or navigation screen. Add a primary gameplay shell with persistent HUD and contextual actions.

#### H4 — Current ship and money information is misleading

`_update_left_panel` defaults missing credits, hull and cargo to zero and expects `hp/max_hp`, unlike the current `ship.status` model. Normalize responses and distinguish missing from zero.

#### H5 — Communications are entirely missing

No Godot communications scene or handler exists despite implemented `chat.history`, `mail.inbox`, `mail.read` and `notice.list` operations. Add communications landing, chat history, mail and notices.

#### H6 — Disconnects and in-flight operations are unsafe

`Main.gd:154-161` only toggles a flag. Pending operations are not failed, controls are not disabled and no disconnected state is presented. Add explicit connection/session states and mutation disabling.

## 6. Protocol correctness findings

| Finding | Classification | Evidence |
|---|---|---|
| `reply_to` ignored | Demonstrated defect | Server envelope and `Main.gd:173-260` |
| `port.dock` not registered | Demonstrated mismatch | `src/server_loop.c` registry |
| `player.settings` not registered; `player.get_settings` is | Demonstrated mismatch | `src/server_loop.c:444`; `Main.gd:45` |
| `auth.login` fallback uses `password` instead of `passwd` | Demonstrated defect | `src/schemas.c`, `src/server_auth.c`, `Main.gd:516-524` |
| Trade payload is incomplete | Demonstrated defect | `src/server_ports.c`; Python tests |
| UUID generator lacks RFC 4122 version/variant semantics | Strong source-based risk | `Main.gd:622-628` |
| Trade UUID is not an idempotency key | Demonstrated omission | `Main.gd:599-620`; trade handlers |
| Movement success inferred from type | Demonstrated defect | `Main.gd:202-205` |
| Schema harvesting blocks login | Strong design risk | `Main.gd:145-146, 237-257` |
| Async events can be mistaken for replies | Demonstrated architectural gap | No event queue/correlation path |
| Raw response JSON reaches players | Demonstrated defect | `Main.gd:181-189` |
| Password/session data is written to debug log | Demonstrated security risk | `Main.gd:618-619, 633-637` |
| Invalid port input is not validated | Demonstrated source defect; runtime-unverified | `Main.gd:461-465` |
| Malformed envelopes are not type-checked | Runtime-unverified risk | `Main.gd:173-180` |
| No post-trade authoritative refresh | Demonstrated omission | No trade response path |

The local `PROTOCOL.v3` material was not treated as authoritative where it conflicted with implemented handlers. It contains draft and non-player material, including S2S/engine concepts.

## 7. Testing assessment

There are no Godot unit, integration, parser or scene smoke tests. The Python benchmark has 112 tests covering protocol correlation, events, HUD/state, money, communications, trading, navigation, session handling and responsive rendering.

Recommended Godot extraction seams:

1. `ProtocolTransport`: framing, IDs, `reply_to`, timeout, disconnect and events.
2. `AuthSessionModel`: login construction and refusal mapping.
3. `PlayerStateReducer`: player, ship and sector authoritative state.
4. `SectorViewModel`: normalized objects and contextual actions.
5. `PortModel`: commodity, stock, capacity and availability.
6. `TradeViewModel`: quantity limits, quotes, confirmation and receipts.
7. `CommunicationsModel`: chat, mail, notices and empty/malformed states.
8. `EventModel`: bounded queue and compact unknown-event rendering.
9. `NavigationState`: screen stack, modal stack, focus and Back behaviour.

Use headless GDScript tests for these pure components once a supported Godot test runner is available, followed by a minimal scene smoke test. Do not introduce a framework during implementation planning without confirming project tooling.

## 8. Target Godot experience

The target should retain keyboard friendliness while using native controls, focus management and contextual screens.

### Main gameplay/sector screen

```text
+---------------------------------------------------------------+
| Player: Alice | Ship: Voyager | Credits: 12,400 | Turns: 83   |
| Cargo: 14/50 | Fighters: 20 | Shields: 80 | Sector: Sol      |
+---------------------------+-----------------------------------+
| Current sector             | Available actions                 |
| Sol (1)                    | [Move] [Dock] [Inspect]           |
| Warps: 2, 7, 9             | [Planets] [Operations]            |
| Ports: Orion Trade         |                                   |
| Planets: Earth             | Recent events                     |
| Ships: Voyager, Atlas      | Trade completed                   |
|                            | Mail: 2 unread                    |
+---------------------------+-----------------------------------+
| Status: Ready                                  [Comms] [Help] |
+---------------------------------------------------------------+
```

### Port summary

```text
+---------------------------------------------------------------+
| Docked at Orion Trade                                         |
| Port buys and sells the following commodities                  |
+----------------+----------+----------+--------+---------------+
| Commodity      | Stock    | Capacity | Sells  | Buys          |
| Food           | 120      | 500      | ~42    | yes           |
| Ore            | 0        | 300      | no     | yes           |
| Fuel           | 80       | 200      | ~17    | no            |
+----------------+----------+----------+--------+---------------+
| [Buy] [Sell] [Undock]                              [Back]     |
+---------------------------------------------------------------+
```

### Quote and confirmation

```text
+---------------------------------------------------------------+
| Buy Food                                                       |
| Available cargo: 36 free       Port stock: 120                |
| Credits: 12,400                                               |
| Quantity: [ 10 ]                                               |
+---------------------------------------------------------------+
| AUTHORITATIVE QUOTE                                            |
| Unit price: ~42                                                |
| Fees: 12                                                       |
| Total cost: 432                                                |
| Projected credits: 11,968                                     |
| Projected cargo: 24/50                                        |
+---------------------------------------------------------------+
| Confirm purchase? [Y/N]  (default: No)                         |
+---------------------------------------------------------------+
```

### Communications inbox

```text
+---------------------------------------------------------------+
| Communications                                                 |
| [Chat] [Mail] [Notices] [Events]                              |
+---------------------------------------------------------------+
| Mail                                                           |
| 1. * Trade offer from Morgan       2026-09-23  unread          |
| 2.   Welcome to the corporation     2026-09-22  read            |
|                                                               |
| Select an item by number.                                      |
| [Open] [Back]                                                  |
+---------------------------------------------------------------+
```

### Connection/refusal state

```text
+---------------------------------------------------------------+
| Connection                                                     |
| Status: Disconnected                                           |
| Reason: The server refused the requested trade.                |
|                                                               |
| No local cargo or credit state was changed.                    |
|                                                               |
| [Retry connection] [Return to title] [Quit]                    |
+---------------------------------------------------------------+
```

Interaction rules:

- Every screen has predictable Back/Escape behaviour.
- Mutations use modal confirmation with a safe No default.
- Buttons are disabled during in-flight mutations.
- Duplicate submissions are prevented.
- Focus returns to the originating control after modal closure.
- Keyboard and mouse activate the same actions.
- Important status is never conveyed by colour alone.
- Lists use scroll containers and stable displayed indices.
- Empty states explain the next useful action.
- Unknown events are compactly labelled, never dumped as JSON.
- Connection loss disables mutations and shows the last known state as stale.

## 9. Ordered implementation programme

### Slice 1 — Transport and protocol boundary

Objective: make communication correct before expanding gameplay.

Likely files: `Main.gd`, new transport/protocol/session scripts, and connection-state UI in `Main.tscn` if needed.

Tests: NDJSON fragmentation/coalescing, request IDs, `reply_to`, refusals, timeouts, disconnects, event classification and redacted diagnostics.

Acceptance criteria: tracked request IDs, safe pending-request failure, event/reply separation, no raw JSON in normal output and no secrets in diagnostics.

Exclusions: no new gameplay domain.

Suggested commit: `feat(godot-client): establish correlated protocol transport`

Status: completed in `14b53b93`.

### Slice 2 — State models and authoritative refresh

Objective: replace direct dictionary-to-label coupling.

Likely files: new player/session/HUD/sector model scripts, `Main.gd`, and possibly `Main.tscn`.

Tests: normalization, missing-versus-zero fields, refresh sequencing and refusal non-mutation.

Acceptance criteria: confirmed current fields, explicit stale/disconnected state and refresh only after successful correlated operations.

Dependencies: Slice 1. Excludes major visual redesign.

Suggested commit: `feat(godot-client): add normalized player and sector state`

Status: implemented in the current Slice 2 work; verification and commit are in progress.

### Slice 3 — Primary gameplay shell and HUD

Objective: make the game understandable at a glance.

Likely files: `Main.tscn`, `Main.gd`, new HUD and sector view scripts/scenes.

Tests: view-model rendering, empty sector, missing ship, long names, stale state and focus order.

Acceptance criteria: the player can identify location, ship, resources, sector contents and available actions; static news is not presented as live data.

Dependencies: Slice 2. Excludes trade and communications.

Suggested commit: `feat(godot-client): add primary gameplay shell and HUD`

### Slice 4 — Sector navigation and movement

Objective: make movement discoverable and safe.

Likely files: sector view, navigation state, movement presenter and `Main.tscn`.

Tests: movement payload, adjacent selection, refusal, successful refresh, Back/Escape and duplicate prevention.

Acceptance criteria: selectable destinations, refusal never shown as success, and successful movement refreshes player, ship and sector.

Dependencies: Slices 1–3. Excludes avoids/bookmarks until concrete handlers are confirmed.

Suggested commit: `feat(godot-client): add contextual sector navigation`

### Slice 5 — Port and quoted trading

Objective: reproduce Python Slice 7 with native Godot modal screens.

Likely files: port/trade model and views, `Main.tscn`, refresh integration.

Tests: normalization, availability, indicative prices, quote payloads, confirmation safety, idempotency, receipts, credit warnings, zero balance, refusals and refreshes.

Acceptance criteria: `port.info` on entry, no quote flood, authoritative quote before mutation, default No, correct buy/sell receipt interpretation and refresh after success.

Dependencies: Slices 1–4. Excludes banking and shipyard.

Suggested commit: `feat(godot-client): add quoted port trading workflow`

### Slice 6 — Communications

Objective: make chat, mail, notices and events usable.

Likely files: communications landing scene/view, normalizers, event queue and HUD indicators.

Tests: readable history, empty/malformed data, indexed mail, read handling, refusals, unread indicators, safe unknown events and menu reachability.

Acceptance criteria: clear COMMS landing, indexed mail selection, no fake total counts, safe event drain points and only confirmed compose/reply operations.

Dependencies: Slices 1–3, preferably Slice 5 for modal patterns.

Suggested commit: `feat(godot-client): add communications workflows`

### Slice 7 — Planets, ships and operations

Objective: add remaining high-value gameplay through reusable list/detail/action patterns.

Tests: concrete payloads, refusals, refreshes, disabled unavailable actions and boarding/deployment safety.

Acceptance criteria: every exposed action has a confirmed handler and safe mutation lifecycle.

Dependencies: Slices 1–6. Excludes unsupported or SysOp-only functionality.

Suggested commit: `feat(godot-client): add core ship planet and operations workflows`

### Slice 8 — Corporation, news, notes, settings and help

Objective: complete secondary player-facing domains.

Tests: empty/unavailable states, settings normalization, help reachability and no false implemented labels.

Acceptance criteria: settings uses `player.get_settings`; news and notes are implemented or explicitly unavailable; help explains controls and contextual actions.

Suggested commit: `feat(godot-client): complete secondary player workflows`

### Slice 9 — Responsive layout and accessibility

Objective: make completed screens reliable at different window sizes.

Tests: narrow/standard/wide viewports, long labels, scrolling, focus order, Escape/Back and non-colour status indicators.

Acceptance criteria: no unusable fixed side panels, readable lists, visible focus and equivalent keyboard/mouse workflows.

Dependencies: primary screens should exist first.

Suggested commit: `feat(godot-client): add responsive layout and navigation polish`

### Slice 10 — Integration and live-server validation

Objective: validate against implemented handlers.

Tests: login, refresh, movement, trade, communications and disconnect during requests.

Acceptance criteria: headless smoke test, current handler-compatible fixtures, no raw JSON/stack traces and tested UI reachability for all player-facing commands.

Suggested commit: `test(godot-client): add live protocol smoke coverage`

## 10. Risks, unknowns and decisions requiring approval

1. Godot tooling is unavailable in this checkout; approval is needed before selecting a test runner or CI integration.
2. The player-facing command surface must come from the current server registry, not `PROTOCOL.v3`.
3. The server player-selection model should be confirmed before implementing character selection.
4. Avoids, bookmarks, combat, corporations and deployment commands require handler-by-handler validation.
5. The untracked Godot tree may contain work from another contributor; implementation must avoid overwriting it.
6. The network debug log should be redacted or gated before broader testing.
7. End-to-end verification requires a live server or protocol fixture service.
8. Changing the Forward Plus renderer or project metadata is unnecessary risk for the initial slices.

## 11. Recommended first implementation slice

Start with Slice 1: correlated protocol transport. It is prerequisite to trade, communications and movement correctness. The first implementation should preserve the current scene as a consumer while introducing a testable transport/session boundary with:

- request/reply matching by `reply_to`;
- explicit success/refusal/error result objects;
- an event queue separate from replies;
- disconnect propagation;
- redacted diagnostics;
- no raw JSON in normal output.

Implementation status: Slice 1 (Transport and Protocol Boundary) was completed in commit `14b53b93`. Slice 2 (State models and authoritative refresh) is implemented in the current Godot Phase 1 work and is pending its dedicated commit. Slice 3 and all later slices remain unstarted. The audit's ordered programme remains the roadmap; this status note supersedes the earlier investigation-only wording without changing the findings or design.
