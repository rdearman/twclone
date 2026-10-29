# V3 Client Parity Matrix

**Scope:** Python terminal and Godot player-facing client workflows for the
V3 release contract in issue #491. This is a client evidence snapshot, not a
claim that V3 release acceptance has passed. A client unit test proves request
construction or presentation only; it does not prove the server transaction
or a complete live gameplay journey.

**Canonical reference:** [TradeWars 2002 Version 3 documentation](https://www.tw-attac.com/docs/TradeWars.html).
See also [issue #491](https://github.com/rdearman/twclone/issues/491) and
[`docs/CANONICAL_COMPARISON.md`](../CANONICAL_COMPARISON.md), whose March 2026
status estimates are historical and must not be treated as current verification.

## Client capability matrix

| Player capability | Python | Godot | Client evidence | Current qualification / dependency |
|---|---|---|---|---|
| Login, registration, password change | Available | Available | `tests/test_registration.py`, `tests/test_password_change.py`; `tests/login_view_test.gd`, `tests/command_result_test.gd` | Offline client regressions pass. Live end-to-end acceptance must use a named server/database candidate. |
| Player/ship state and refresh | Available | Available | `tests/test_hud_state.py`, `tests/test_ship_workflows.py`; `tests/state_model_test.gd` | State refresh is server-authoritative; fixture tests do not replace live verification. |
| Sector inspection, scans, fog of war | Available | Available | `tests/test_sector_visibility.py`; `tests/sector_visibility_test.gd`, `tests/computer_intel_test.gd` | Clients preserve unknown/missing fields and do not infer port class from a scan. Remote/proximity policy remains server-owned (#466/#467). |
| Warp, route plotting, autopilot controls | Available | Available | `tests/test_navigation.py`, `tests/test_autopilot_controls.py`; `tests/ship_navigation_test.gd`, `tests/route_confirmation_test.gd` | Godot cancels after the current server-confirmed hop. Named saved plans require a storage/API contract (#426). |
| Port inspection and quoted commodity buy/sell | Available | Available | `tests/test_port_trading.py`; `tests/port_workflow_test.gd` | Both use server quotes and receipts. Both now expose the documented per-trade petty-cash/bank selector (`account: 0|1`). Live persistence and idempotency still require a traceable server/database test. |
| Hardware purchase and ship repair | Available | Available | `tests/test_ship_workflows.py`; command menu and shipyard coverage in `tests/command_menu_test.gd` | Server validates dock, catalogue, stock and cost. Do not infer live integration from UI tests. |
| Hull listing and hull exchange | Available | Available | Python `shipyard_upgrade_flow`; Godot `shipyard.upgrade` command menu workflow | The shipyard sell handler remains `ERR_NOT_IMPLEMENTED`; issue #256 is open. |
| Inspect a specific other ship | Limited sector snapshot only | Explicitly unavailable | Python `enter_ship_menu`; Godot selected-ship menu | The published schema requires `ship_id`, but the handler returns a sector list without using it. Tracked for server/protocol correction in [#497](https://github.com/rdearman/twclone/issues/497). Python now labels and displays only the already-received visible sector record. |
| Cargo jettison | Available with explicit warning/confirmation | Available with explicit form warning | `tests/test_ship_workflows.py`; `tests/command_menu_test.gd` | Python validates against the last ship report, confirms one-turn cargo loss, and refreshes ship state after success. |
| Towing | Available | Available | `tests/test_towing.py`; Godot ship navigation/menu tests | Supported status and tow controls are exposed. Server remains authoritative for eligibility and movement effects. |
| Combat, deployed assets and damage events | Available | Available | `tests/test_planet_workflows.py`, `tests/test_protocol_events.py`; `tests/planet_combat_presenter_test.gd`, `tests/event_presenter_test.gd` | Clients present server outcomes and event fields without calculating damage. Release acceptance requires persisted live combat evidence. |
| Planet landing, colony, production, transfers and citadel | Available | Available | `tests/test_planet_workflows.py`; `tests/planet_workflow_test.gd`, `tests/planet_command_response_test.gd` | Workforce, commodity/treasury transfers, fighter transfers, citadel and launch flows are wired. Server/database production and ownership rules require integration verification. |
| Corporation membership and treasury | Available | Available | `tests/test_corporation_equity.py`, `tests/test_menu_gating.py`; `tests/command_menu_test.gd` | Server authorization and atomic settlement remain server/database acceptance items. Corporate elections/stock markets are excluded expansions under #491. |
| Chat, mail, subscriptions and notices | Available | Available | `tests/test_communications.py`, `tests/test_subscriptions.py`, `tests/test_event_log.py`; `tests/event_presenter_test.gd` | Client presentation/requests are covered. Notice persistence, recipient fan-out and live event delivery remain server-side integration evidence (#80). |
| Current-sector Ferengi offers | Available | Available | `tests/test_ferengi_workflows.py`; `tests/ferengi_presenter_test.gd` | There is no paginated directory of all known NPCs/last-seen locations; the requested directory contract remains open in #83. |
| Rankings / historical attack reports | Explicitly unavailable where server data is absent | Explicitly unavailable where server data is absent | `tests/test_menu_gating.py`; `tests/computer_intel_test.gd` | Rankings currently return an empty array (#415); historical reports need server history/API support (#425). Clients do not fabricate either. |

## Verified local regression baseline

At the snapshot that produced this report:

- Python client suite: **254 passed**.
- Godot headless suite: **20/20 scripts passed**; command-menu script: **249
  checks passed**.
- Shared asset catalogue validation: **13 assets, 0 warnings**.
- Bank selection: full Python and Godot suites passed after implementation
  (`249` Python tests and `20/20` Godot scripts).
- Jettison confirmation and authoritative refresh: full Python suite passed
  (`252` tests); Godot command-menu focused test passed (`125` checks); the
  full Godot suite passed (`20/20` scripts).

The full suite includes offline client tests only; it does not establish live
gameplay acceptance against a running server/PostgreSQL candidate.

## Open contract dependencies affecting client parity

| Dependency | Evidence | Owner boundary |
|---|---|---|
| Ship rename schema/handler mismatch | `schema_ship_rename` accepts `name`; handler requires `ship_id` and `new_name`; #88 reopened | Server/protocol |
| Cargo transfer stub | `cmd_ship_transfer_cargo` returns `ERR_NOT_IMPLEMENTED`; #90 reopened | Server/database |
| Shipyard sell stub | `cmd_shipyard_sell` returns `ERR_NOT_IMPLEMENTED`; #256 reopened | Server/database |
| Saved trade-account preference mismatch | Setter stores `trade.prefer_bank`, buy/sell read `trade.default_account`; #496 filed | Server/database |
| All-known NPC/trader directory | Current `ferengi.traders` is sector-scoped; #83 remains open | Server/protocol |
| Durable announcements and recipient delivery | Client presenters exist; server producer/fan-out acceptance remains open in #80 | Server/engine/database |
| Rankings and historical attack reports | Current handlers do not provide the requested populated history; #415 and #425 | Server/database |
| Named navigation plans | Existing route/autopilot state is not a named multi-plan store; #426 | Server/database/protocol |

These dependencies do not justify inventing client-only commands or local
state that appears authoritative. When the public server contract changes,
both clients need matching workflow tests and live integration coverage.
