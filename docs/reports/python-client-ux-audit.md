# Python Client UX Audit — Durable Findings

This is a condensed record of the design decisions and prioritised findings
from the Python client audit and the two correctness slices that followed
("truthful and safe client" and "settings/money normalisation"). It exists
for future Python, C/ncurses, and Godot client work; it does not attempt to
reconstruct the original full audit verbatim.

> For the authoritative, continuously-updated record of completed slices,
> architecture, tests, and the continuation backlog, see
> [`docs/python-client-development-handover.md`](../python-client-development-handover.md).

## 1. Implemented server contract is authoritative

Documentation (`PROTOCOL.v2.md`, feature docs) is a proposed or historical
description; the client must match what `src/server_*.c` handlers actually
accept/return, corroborated by `tests.v2/*.json` fixtures where available.
Where documentation and implementation disagreed, implementation won. This
found and fixed several real defects (wrong subscription field names, wrong
bulk payload key, wrong auth field names, non-existent `stock.*` list
commands, and multiple money unit-conversion bugs) — see the git history of
`client/python_client/client.py` for the exact fixes.

## 2. Protocol/domain state must stay independent of rendering

The client is split so protocol/data concerns and presentation concerns
don't mix:

* `protocol.py` — socket/RPC transport (`Conn`) and pure envelope-shape
  helpers (`get_data`, `extract_current_sector`, `normalize_sector`). `Conn`
  never renders server content; unsolicited frames are classified and
  pushed onto a bounded `EventQueue` (`Conn.events`).
* `state.py` — `Context`: session/menu state, no menu-loading logic. Now
  also holds `hud: HudState` and `event_log: EventLog`.
* `hud.py` — pure extraction of HUD fields from confirmed server responses,
  the `HudState` model, connection-state derivation, and HUD line rendering.
  No RPC calls.
* `events.py` — the bounded, presentation-ready `EventLog` (read/unread
  tracking) and player-facing event-to-text rendering. No RPC calls.
* `settings_model.py` — pure normalisation of settings/bookmarks/avoid/
  notes/prefs/subscriptions into one stable internal shape, independent of
  which of the server's several field-name variants (`items` vs
  `bookmarks`, prefs-as-array vs prefs-as-object, etc.) supplied it.
* `money.py` — a single, non-lossy credits parser/formatter (`Decimal`-based,
  never binary float, never a blind ×100/÷100 assumption).
* `presenters.py` — turns normalised data into player-facing text; performs
  no RPCs and mutates no state.
* `client.py` — the orchestration layer: menus, handlers, RPC calls.

This split is what will let a future C/ncurses or Godot client reuse the
*behavioural* rules (which commands exist, what payloads they need, how to
interpret a response) without reusing any Python UI code.

## 3. Persistent HUD (implemented)

The HUD is a compact status header rendered by `render_menu()` before every
gameplay menu (excluded on `TESTING`/`BULK`, the debug-only screens). It is
driven entirely from `Context.hud` (a `HudState`), which is populated by a
single centralised extractor (`hud.apply_response`) — menu handlers never
parse response fields themselves for HUD purposes.

**HUD state model** (`hud.HudState`, all fields `Optional`; `None` means
"unknown/not supplied", never coerced to `0`):

| Field | Source command (response type) | Notes |
|---|---|---|
| `sector_id` / `sector_name` | `move.describe_sector` (via `normalize_sector`) | Updated after every successful `move.warp`. |
| `ship_id` / `ship_name` | `ship.status` (`"ship.status"`, `data.ship`) | `ship.info` is a deprecated compat alias of the same handler. |
| `turns_remaining` | `player.my_info` (`"player.info"`, `data.player.turns_remaining`) | Plain int. |
| `credits` | `player.my_info` (`data.player.credits`, decimal string `"N.00"`) | Parsed via `money.parse_credits`; a third, independent encoding of the same "always-integral decimal string" shape already handled by that parser. |
| `cargo_used` / `cargo_total` | `ship.status` (`data.ship.holds`, `data.ship.cargo[]`) | `holds` is total capacity (confirmed via the `ships` table CHECK constraint in `sql/pg/000_tables.sql`); `cargo_used` is derived client-side as `sum(qty)`, which is safe only because the server enforces that invariant, not a heuristic. |
| `fighters` / `shields` | `ship.status` (`data.ship.fighters` / `.shields`) | |
| `unread_mail` | `mail.inbox` (count of `data.items` lacking `read_at`) | Page-bounded approximation, not a verified total; `mail.inbox` itself never marks mail read, so it is safe to poll on demand. |
| `unread_notices` | `notice.list` (count of `data.items` lacking `seen_at`) | Safe to poll; only `notice.ack` mutates seen-state. |
| `unread_news` | `auth.login` **only** (`"auth.session"`, `data.unread_news_count`) | **Confirmed gap**: no other implemented command exposes an unread-news count without a side effect — `news.get_feed` marks all news read as part of its own execution (`repo_news_update_last_read`). This field is a login-time snapshot and cannot be refreshed mid-session without consuming it. |
| `last_refresh_ts` | set by `hud.merge_hud` on every successful extraction | Drives the ONLINE→STALE derivation (30s threshold, `hud.STALE_AFTER_SECONDS`). |

**Refresh points** (deliberate, never per-render): login (`hydrate_login`,
after `auth.login`/`player.my_info`/`ship.status`/`move.describe_sector`/
`mail.inbox`/`notice.list`); after a successful `move.warp` (sector fields
updated directly, then one deliberate `player.my_info` refresh since
`move.warp`'s response does not include turns/credits); whenever any
handler happens to call `player.my_info`, `ship.status`/`ship.info`,
`mail.inbox`, or `notice.list` (routed through the `_hud_rpc` wrapper in
`client.py`, which is the single choke point that calls
`hud.apply_response` — this is additive to those handlers' existing
behaviour, not a new call site). Refused/error responses never update HUD
state (`hud.apply_response` checks `status == "ok"` first).

**Connection state** (`ONLINE`/`STALE`/`OFFLINE`/`RECONNECTING`) is derived,
not duplicated: `OFFLINE` comes directly from `Conn.connected`; `STALE` is
derived by comparing `last_refresh_ts` against a 30s threshold; `RECONNECTING`
is part of the presentation vocabulary but is unreachable in this slice — no
automatic-reconnect logic exists yet (a disconnect currently propagates as
`ConnectionError` up to `main()`); it is reserved for a future slice.

**Rendering** (`hud.render_hud_lines`): two logical lines (sector/ship/turns/
credits, then holds/fighters/shields/link/activity), each independently
word-wrapped to the caller-supplied terminal width so a value is never
truncated mid-string — narrower terminals get more, shorter lines instead.
Unavailable fields render as `—`, never `0` or blank.

**Activity indicator**: `Context.activity_count` sums `unread_mail` +
`unread_notices` (when known) + the event log's live unread counter
(`event_log.unread_count`). `unread_news` is intentionally excluded from
this live sum since it cannot be refreshed without side effects (see table
above); it remains visible as its own HUD-independent one-time signal from
login only. Returns `None` (rendered as `—`) only when nothing is known yet.

## 4. Communications is a normal gameplay area

Chat and mail were implemented server-side but unreachable from the main
menu (orphaned submenus) and used broken payloads (`scope`/`to` instead of
the confirmed `to_player`/`message`, raw JSON dumps instead of formatted
history). Fixed: a top-level `Comms` entry now routes to working Chat/Mail
screens using the confirmed `chat.send`/`chat.broadcast`/`chat.history`
contract, plus two new entries added in the HUD slice: **Events** (the
event inbox/log, see §6) and **Notices** (`notice.list`). A deeper
communications redesign (threading, richer scrollback) is future work.

## 5. Debug tools excluded from normal play

Testing/Bulk/raw-JSON-Trade menus, and menu items for commands confirmed
absent from the server registry (`game.get_clock`, `move.autopilot.control`,
`ship.set_primary`) or confirmed unimplemented despite existing
(`equity.exchange.list`/`portfolio.list`, since the generic `cmd_equity`
dispatcher only implements `ipo.register`/`buy`/`sell`/`dividend.set`), are
now gated behind the existing `--debug` flag via `menus.json`
`show_if_ctx`. No new CLI flag was introduced. The HUD is also excluded from
these debug/protocol screens.

## 6. Asynchronous events: queued, non-disruptive presentation (implemented)

`protocol.Conn` is synchronous (no reader thread ever existed; the earlier
note in this document describing one was inaccurate) — it only reads from
the socket while a `rpc()` call is waiting for its reply. Unsolicited frames
encountered during that wait (notices, broadcasts, legacy `event` envelopes,
async errors, and any unrecognised frame shape) are classified
(`protocol.classify_event_type`: `system`/`chat`/`nav`/`combat`/`trade`/
`connection`/`unknown`) and pushed onto `Conn.events`, a thread-safe,
bounded (`maxlen=200`) FIFO (`protocol.EventQueue`) — never printed.
Overflow policy is drop-oldest: when full, the oldest queued event is
discarded to admit the newest, and a `dropped` counter is incremented;
arrival order of the events that remain is always preserved. Unknown event
types are always retained (tagged `"unknown"`), never discarded by type. A
detected disconnect (failed `send`/`recv`) sets `Conn.connected = False`
exactly once and queues a single `connection.lost` event before the
`ConnectionError` propagates to the caller.

`Context.drain_events()` moves everything queued on `Conn.events` into the
long-lived `EventLog` (`state.py`/`events.py`); this is the only safe
boundary at which draining happens — immediately after the top-level
`input()` in the main menu loop returns, never during text entry. A
one-line notification (`"N new events. See Comms > Events."`) is printed at
that point; the **Comms → Events** screen (`comms_events_view`) shows a
bounded recent history (last 25 of up to 500 retained) rendered as
player-facing text (`events.render_event_text`) and calls
`event_log.mark_read()`, which clears the *unread counter* only — the
retained history is never cleared by reading it. Unknown events get a safe,
compact, JSON-free representation in normal mode and their raw type/data
in `--debug` mode only.

## 7. Capability-driven menus

Menu visibility is presently driven by static `show_if_ctx` flags set from
local heuristics (e.g. `is_ceo`, `debug`), not by a server-advertised
capability/command list. This remains recorded technical debt — the HUD
slice deliberately did not broaden into a full menu-gating rewrite. A
future slice should have the client request its actual permitted command
set (if/when the server exposes one) rather than hard-coding assumptions
menu-by-menu.

## 8. Planned interaction conventions shared across clients

For future C/ncurses and Godot clients, the following should become shared
*specifications* (not shared code): the confirmed command catalogue and
payload shapes per command; the settings/bookmarks/avoid/notes/prefs
normalised model (`settings_model.py`'s shapes); the money
parsing/formatting rules (`money.py`'s rules: integer credits by default,
decimal-string receipts only for the four confirmed trade-receipt fields);
the move-result contract (never describe/cache a destination on refusal or
error); the debug/diagnostic-gating convention (hide non-implemented or
internal-only commands from the normal player interface); the HUD field
model and its per-field authoritative source table (§3); and the event
classification vocabulary and queued/non-disruptive presentation rules
(§6) — every client should classify events the same way and never let
transport-layer input block or corrupt an active prompt.

## Outstanding known correctness items (not yet fixed)

* Menu-driven capability gating is heuristic, not server-confirmed (§7).
* `unread_news` cannot be refreshed mid-session without consuming it (§3) —
  a genuine server-side API gap, not a client defect.
* `unread_mail`/`unread_notices` are page-bounded approximations (limited by
  the `limit`/pagination parameters of `mail.inbox`/`notice.list`), not
  verified server-side totals.
* No automatic reconnect/`RECONNECTING` behaviour exists yet; a dropped
  connection currently ends the session (§3).
