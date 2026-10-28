# V2 client readiness audit

Date: 2026-09-27

## Scope and result

This pass reviewed the Godot command menu and gameplay workflows, the Python
terminal client's menu/command handlers, help and developer handover text,
error presentation, and client regression coverage. It changed only Python
client code, Python client tests, and Python client documentation. No server,
protocol, database, migration, or engine implementation was changed.

The clients have broad coverage of the currently implemented command surface,
and both client test suites pass. The issues found in this pass were stale
Python menu entries and incomplete autopilot response handling. Remaining
readiness limitations that need server ownership or a wider follow-up are
listed below.

## Fixes completed

- Removed the Python menu entry for `game.get_clock`; it is not registered by
  the server and had been incorrectly presented as a debug command.
- Exposed the registered `move.autopilot.control` flow in normal play and
  improved Python feedback: successful controls show the confirmed state and
  next sector, while server refusals show the reason without changing local
  state.
- Replaced the exchange list and portfolio menu calls with the canonical
  `equity.exchange.list` and `equity.portfolio.list` commands.
- Made the implemented dividend declaration flow available to corporation
  CEOs in normal play; it remains hidden from non-CEOs.
- Added regression checks for menu reachability, canonical command names,
  command refusals, successful autopilot state updates, and dividend access.
- Corrected the current Python client menu policy in
  `python-client-development-handover.md`; earlier slice notes are retained
  as historical records and explicitly marked as superseded.

## Client findings

- **Python menu accuracy:** stale diagnostics previously advertised an
  unavailable game clock command and described registered equity/autopilot
  commands as unimplemented. The menus now use the registered canonical
  command names and only expose CEO dividend action to CEOs.
- **Autopilot result handling:** the control handler previously discarded the
  RPC reply, leaving the player unable to distinguish acceptance from
  refusal. It now renders the server result and records the returned status.
- **Disconnect recovery:** the Python client safely reports a lost connection
  and ends the session, but does not reconnect automatically. Reconnect is a
  remaining client feature; retrying a mutation blindly would risk duplicate
  actions, so any future implementation needs explicit reconnect and
  in-flight command semantics.
- **Godot command coverage:** the command menu includes several actions that
  are disabled or marked unavailable where current server handlers/contracts
  do not support them. Do not enable these solely through client changes.
- **Value formatting:** Godot views have identifier formatting coverage for
  sector density and gameplay state. Numeric database identifiers that
  arrive as floats should continue to be formatted as integer identifiers;
  quantities and measurements should retain their units/labels.
- **Planet/port workflows:** regression suites cover the existing surface,
  port, dialog, double-click and protocol flows. Missing planet capabilities
  must not be implied by client controls until a server command and response
  exist.

## V2 milestone perspective

### Completed or supported from the client side

- Godot sector navigation, object selection, port and planet workflows,
  command dialogs, pending/result presentation, and sector-level rendering.
- Python terminal menus and handlers for registered movement, trade,
  communications, settings, bookmarks/avoid lists/notes, autopilot and
  corporation equity workflows.
- Client-side regression coverage for protocol event interleaving, clean
  disconnect handling, refusal presentation, menu reachability, trade and
  port flows, planet/port workflows, and state rendering.

### Remaining client work or dependencies

- Python automatic reconnect remains unimplemented. The existing behavior is
  a clear session exit rather than a recovery attempt.
- Live-server smoke testing was not performed during this audit.
- GitHub issue state could not be checked because the environment could not
  connect to the GitHub API. Reconcile these findings against the live V2
  milestone before closing issues.
- Some Godot menu actions remain unavailable because their server-side
  handlers or response contracts are absent/incomplete. Server/API ownership
  should close those gaps; this audit did not alter them.
- Specific Godot-visible server dependencies already surfaced in the UI
  include mismatches for `ship.rename`, `chat.send`, `mail.send`,
  `mail.delete`, and `subscribe.add`/`subscribe.remove`; `ship.inspect`
  currently does not return details for the selected ship; the player-rank
  handler returns an empty list; and insurance handlers are stubs. The UI
  keeps these actions disabled or explains their limitations. These need
  server/protocol resolution before client enablement.
- The audit did not find a client-only basis to implement event encounter
  reactions. NPC special encounters remain dependent on engine/server
  encounter generation and a player-facing encounter action interface.

## Verification

Python client:

```text
python3 -m pytest -q client/python_client/tests
127 passed
```

The full Python suite requires permission for local socketpair tests; it was
run successfully with that permission.

Godot client: all 11 scripts in `client/godot_client/godot/tests` passed with
Godot 4.5.1 headless, for 176 assertions total. The test run used a temporary
`XDG_DATA_HOME` and per-test log files under `/tmp` so it did not alter
repository-local data.

## Files changed by this readiness pass

- `client/python_client/client.py`
- `client/python_client/menus.json`
- `client/python_client/tests/test_autopilot_controls.py`
- `client/python_client/tests/test_corporation_equity.py`
- `client/python_client/tests/test_menu_gating.py`
- `docs/python-client-development-handover.md`
- `docs/V2_CLIENT_READINESS.md`
