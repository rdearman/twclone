# TW2002 → twclone V3 Functional Parity Audit (GitHub committed baseline)

**Audit date:** 2026-10-30  
**Repository:** [rdearman/twclone](https://github.com/rdearman/twclone), default branch `master`  
**Baseline tree:** `d1d3781d795b58c3cbb8d40c0d80f4179cfadc83` (resolve the corresponding commit before final sign-off).  
**Status:** Source/documentation desk audit — **Codex review required; no live integration tests executed**.  
**Release authority:** [#491](https://github.com/rdearman/twclone/issues/491): full *original* TW2002 player-facing parity, intentional differences documented; federation and noncanonical expansions excluded.

## 1. Method and evidence hierarchy

1. Original TW2002 manuals and contemporaneous help, version-qualified (references below).
2. Committed runtime handlers and dispatcher (`src/server_loop.c`, `src/server_*.c`), repository and migrations (`src/db/`, `sql/`).
3. Both committed client implementations (`client/python_client/`, `client/godot_client/godot/`).
4. Executed end-to-end tests, where independently available. **None were executed in this audit.**
5. Committed test definitions and historical reports (evidence of coverage intent, *not* proof of passing behaviour).
6. Open/closed GitHub issues and prior agent reports (triage signals; may be stale).

**Status vocabulary:** `I/U` = implementation evidence, not runtime-verified; `P` = partial, conflicting, stubbed, or missing essential behaviour; `M` = no demonstrated implementation; `?` = not adequately audited; `D` = deliberate noncanonical difference; `N/A` = not applicable. No entry is marked `V` (verified) without executed gameplay evidence. The matrix describes **committed master**, not unpushed agent work. Client status is **usable workflow**, not merely ability to send arbitrary RPCs.

### Historical references

- [TradeWars 2002 Bible v1.1](https://wiki.classictw.com/index.php/TradeWars_2002_Bible_v1.1)
- [Gypsy's Big Dummy's Guide to TradeWars](https://wiki.classictw.com/index.php/Gypsy%27s_Big_Dummy%27s_Guide_to_TradeWars_Text)
- [TradeWars 2002 Assistant Help](https://wiki.classictw.com/index.php/TradeWars_2002_Assistant_Help_Text)
- [Sherrick/Morris TradeWars 2 v11.2 Documentation](https://wiki.classictw.com/index.php/Sherrick/Morris_TradeWars_2_v11.2_Documentation_Text)
- [TradeWars Helper Help v8.8](https://wiki.classictw.com/index.php/TradeWars_Helper_Help_Doc_v8.8)

**Canon caveat:** These references cover different versions, tools and historical contexts. Do not treat every documented mechanic as mandatory TW2002 parity without a version-specific citation. In particular, issue titles calling a feature “canonical” are not sufficient evidence.

## 2. Executive findings

- The existing March 2026 [canonical comparison](../CANONICAL_COMPARISON.md) claims **85–90%** parity and “only three critical gaps”. **Do not use that figure for V3 release decisions.** It claims port fixed modes, sector-entry mine triggers and planetary destruction are complete, whereas newer issue audits report gaps in exactly those areas. It also classifies citadels as an expansion, despite citadels being part of classic TradeWars gameplay. Its simple percentage has no defensible capability denominator.
- [Feature parity analysis](../FEATURE_PARITY_ANALYSIS.md) is a useful historical inventory but likewise contains claims not reconciled with later defects.
- `tests.v2/COVERAGE.md` records positive/negative fixture presence, not successful runs, semantic assertions or complete client journeys. It marks `trade.buy` **N/Y**, `ship.transfer_cargo` **N/Y**, `ship.self_destruct` **N/Y**, `ship.sell` **N/Y**, `player.rankings` **Y/Y**, and `sector.scan` **N/N**. Treat these as coverage risks, not automatic implementation failures.
- The committed Godot `CommandMenu.gd` explicitly disables selected operations as server schema/handler mismatches or stubs (rename, private messaging, rankings, insurance). These are stronger evidence of incomplete player-facing parity than a generic command registry entry.
- The committed [V3 runtime audit](reports/V3_SERVER_RUNTIME_CONTRACT_AUDIT.md) states that live events lack durable client cursor/ACK/replay; this is a resilience gap, **not** automatically a TW2002 canon gap.
- Existing `src/s2s_*` infrastructure is local engine transport, not V4 federation; the V3 runtime audit explicitly identifies remote federation handoff placeholders.

## 3. Functional parity matrix

**Notation:** Server / Python / Godot. `I/U` is deliberately conservative. Each line identifies a required follow-up and code/test anchor, not an assertion that the complete system was executed.

| ID | Original gameplay capability / expected behaviour | Server | Python | Godot | Committed evidence and next verification |
|---|---|---|---|---|---|
| NAV-01 | Connected-sector travel, turns and movement refusal | I/U | I/U | I/U | `server_universe.c`; `suite_movement_move_warp.json`; Python `test_movement.py`; confirm failed moves leave position/turns unchanged |
| NAV-02 | Warp graph, plotting and Computer routes | I/U | I/U | P | `server_autopilot.c`, `suite_navigation_pathfind.json`; verify no unknown-universe omniscience |
| NAV-03 | Auto-navigation, abort/interrupt and avoids | I/U | I/U | P | `suite_movement_autopilot_controls.json`, Python `test_autopilot_controls.py`; verify interruptions and hazards |
| NAV-04 | Transwarp equipment, fuel, destination and interdiction | I/U | ? | ? | `suite_movement_move_transwarp.json`; validate sector ownership/lock rules |
| NAV-05 | Density, holo/long-range scans with correct disclosure | P | P | P | `suite_information_fog.json`, `suite_sector_scan.json`; open issue “Scans must not reveal port class” |
| NAV-06 | Fog of war, sector/port/planet proximity | P | P | P | Open issue “Enforce information proximity & fog-of-war boundaries”; audit `sector.info`, `trade.port_info`, `planet.info`, `move.pathfind` |
| NAV-07 | Beacons, navigation notes and saved avoids | I/U | I/U | P | `suite_server_nav_notes_contracts.json`; `LocalSectorNotes.gd` |
| NAV-08 | Probes, hostile detection and probe intelligence | P | ? | ? | [Intel epic #423](https://github.com/rdearman/twclone/issues/423); establish canon variant and handler coverage |
| ECO-01 | Fixed commodity buy/sell direction per port class | P | I/U | I/U | Open issue “Enforce fixed port buy/sell modes”; `suite_port_fixed_modes.json`; **test may not imply runtime fix** |
| ECO-02 | Three canonical commodities, cargo limits and capacity | I/U | I/U | I/U | `server_ports.c`, `suite_commodity_quantity_restrictions.json`; extended commodities are D |
| ECO-03 | Port dock, quantity selection, quotes, buy/sell, credit/turn updates | I/U | I/U | I/U | `suite_trade_market.json`, Python `test_port_trading.py`, Godot `PortWorkflow.gd`; verify atomic ledger/inventory changes |
| ECO-04 | Price variation and player haggling | P | ? | ? | `docs/CANONICAL_COMPARISON.md` says haggling missing; modern dynamic pricing is D; verify canonical negotiation scope |
| ECO-05 | Port regeneration, production, depletion and world ticks | I/U | N/A | N/A | `server_cron.c`, `suite_phase10_market_decay.json`; deterministic clock tests |
| ECO-06 | Port robbery/illegal trade and enforcement | I/U | ? | ? | `suite_combat_port_rob.json`, `suite_trade_illegal.json` |
| ECO-07 | Galactic bank, deposits/withdrawals and credit protection | I/U | I/U | I/U | `server_bank.c`, `suite_economy_and_bank.json`; cross-transaction reconciliation |
| ECO-08 | Player trade account preference affects default transactions | P | P | P | Open issue “Fix player trade account preference key mismatch” (`trade.prefer_bank` vs `trade.default_account`) |
| SHIP-01 | Ship catalogue, purchase, sale, holds, fighters and shields | I/U | P | P | `server_ships.c`, `server_stardock.c`; `suite_shipyard_and_ships.json`; sale positive coverage absent in `COVERAGE.md` |
| SHIP-02 | Current ship state, equipment, damage and repair | P | I/U | P | Open issue “Expose ship hull in public ship.status”; Godot menu repair; confirm real hull and cost |
| SHIP-03 | Inspect a **selected** ship with correct schema | P | P | P | Open issue “Fix ship.inspect request and selected-ship response contract” |
| SHIP-04 | Ship rename and registration | P | ? | P | Godot `CommandMenu.gd` marks rename unavailable due to schema/handler mismatch |
| SHIP-05 | Multiple ships, towing, transfers, cargo moves | I/U | P | P | `suite_ship_tow_status.json`, Python `test_towing.py`; positive `ship.transfer_cargo` absent in coverage; Python tow issue may be stale |
| SHIP-06 | Ship loss, escape pod, respawn and cargo/bank outcomes | P | ? | ? | `CANONICAL_COMPARISON.md` lists respawn unverified; full death/recovery test required |
| COM-01 | Ship-to-ship attack, odds, fighter/shield/hull resolution | P | P | P | `server_combat.c`, `suite_combat_complete.json`; odds and casualty semantics require canon-grounded test |
| COM-02 | Sector fighters/mine/limpet triggers on arrival | P | P | P | Open issue “Sector assets must engage ships on entry”; trigger helpers reportedly not invoked by warp/transwarp |
| COM-03 | Mine scanning, removal, limpet effects/tracking | P | P | P | `suite_combat_mines.json`; open limpet decontamination/tracking issues |
| COM-04 | Photon, retreat, pursuit, boarding/capture and combat aftermath | P | ? | ? | [Combat epic #396](https://github.com/rdearman/twclone/issues/396); `weapon.photon.fire` open; differentiate canon from expansions |
| COM-05 | Safe FedSpace/StarDock and law consequences | I/U | ? | ? | `server_police.c`, `suite_fedspace_aggression_captainz.json`; compare canon response thresholds |
| PLAN-01 | Genesis torpedoes, planet creation, landing, ownership | I/U | P | I/U | `server_planets.c`, `suite_planet_creation.json`, Godot `PlanetWorkflow.gd` |
| PLAN-02 | Colonist transport, allocation, growth and production | P | P | P | [Planet epic #416](https://github.com/rdearman/twclone/issues/416); `suite_planet_fighter_production_e2e.py`; verify colonist economics |
| PLAN-03 | Planet inventories, transfers, harvesting and persistence | I/U | P | P | `suite_planet_ops.json`; verify complete multi-command client flow |
| PLAN-04 | Citadel building/upgrades, shields, cannons, defence | P | P | P | `server_citadel.c`, `suite_citadel_upgrade_lifecycle.json`; shields/defence issue still open |
| PLAN-05 | Planetary destruction/capture, ownership and loss | P | P | P | Open issue “Planet destruction mechanics (bombard / annihilation)” despite old comparison claiming complete |
| CORP-01 | Create/join/invite/leave, officers and membership | I/U | P | I/U | `server_corporation.c`, `suite_corporations.json`, Godot corporation menu |
| CORP-02 | Corporate assets, permissions, treasury and sharing | I/U | P | P | `suite_corp_perms.json`; validate corp ownership and hostile action prevention |
| FED-01 | Alignment, experience, rank and federal penalties | P | P | P | `server_police.c`; Godot menu states `player.rankings` empty; rank thresholds and display need proof |
| FED-02 | Bounties, fines, law enforcement, Federal protection | I/U | P | P | `suite_bounty.json`, `suite_police_phase_*.json`; confirm canonical vs extended law policy |
| FED-03 | Insurance and loss settlement, if in selected canon | P | ? | M | Godot `CommandMenu.gd` explicitly says policy/buy/claim handlers are stubs; canon applicability must be confirmed |
| NPC-01 | Ferrengi movement, combat, trading and interactions | P | N/A | N/A | `suite_ferengi_travelling_traders.json`, `suite_npc_ferengi_init.json`; autonomous behaviour not live-verified |
| NPC-02 | Other canonical aliens, special encounters and events | P | P | P | [NPC epic #437](https://github.com/rdearman/twclone/issues/437); verify historical version and event scheduler |
| COMMS-01 | Private mail, inbox/read/delete and broadcasts | I/U | I/U | P | `server_communication.c`, `suite_communication.json`; Godot `chat.send` schema/handler mismatch |
| COMMS-02 | Tavern, rumours, gambling, messages and news | I/U | P | P | `suite_tavern_extras.json`, `suite_chat_mail_news.json`; player-facing workflows incomplete |
| COMP-01 | Computer reports, port intelligence, trader/asset rankings | P | P | P | [Intel epic #423](https://github.com/rdearman/twclone/issues/423); Godot says rankings handler returns empty |
| COMP-02 | Holodisk/last-seen, attack reports, saved navigation plans | P | ? | ? | [Intel epic #423](https://github.com/rdearman/twclone/issues/423); canon/version classification pending |
| OPS-01 | SysOp universe setup, policy, maintenance, player moderation | I/U | N/A | N/A | `server_sysop.c`, `suite_admin_sysop.json`; operator acceptance separately required |
| OPS-02 | Session security, reconnect, event delivery and state refresh | P | P | P | [V3 runtime contract audit](reports/V3_SERVER_RUNTIME_CONTRACT_AUDIT.md); operational release quality, not canon scoring |

**Important:** Some rows combine several sub-capabilities. Codex should split them into atomic rows before producing any numerical completion percentage. `P` often means “unresolved evidence or known defect”, not “nothing exists”.

## 4. Highest-priority source-level discrepancies to review

| Priority | Finding | Required proof/fix |
|---|---|---|
| P0 | Fixed port classes vs dynamic buy/sell logic | Trace actual `trade.buy/sell` eligibility and port mode schema; negative test for prohibited commodity direction |
| P0 | Remote information leakage / fog-of-war | For every sector/port/planet/computer query, define allowed observer/location and assert refusal/redaction |
| P0 | Sector defences not invoked by movement | Trace both warp and transwarp through fighter/mine/limpet processing, transaction and event outcome |
| P0 | Credit/cargo/turn consistency and retries | Concurrent duplicate request and rollback tests; never double-charge/double-award |
| P1 | `ship.inspect` selected-ship contract mismatch | Align schema, handler, both clients and positive/negative tests |
| P1 | `trade.prefer_bank` vs `trade.default_account` | One authoritative setting name; subsequent trade honours preference |
| P1 | Ship status lacks hull and Godot disables rename | Repair authoritative response and rename schema/handler; both client flows |
| P1 | Insurance handlers apparently stubbed | Confirm scope in canon; if V3-required, implement policy/claim lifecycle or explicitly defer with reason |
| P1 | `player.rankings` reportedly empty | Populate authoritative rank data and verify nonempty seeded universe |
| P1 | Planet destruction and citadel defence | Trace handlers and game-state transitions; validate against chosen canonical edition |
| P1 | Private chat `chat.send` schema mismatch | Align schema and handler; verify Python/Godot direct messaging |
| P2 | Python towing and cargo transfer | Reconcile stale towing issue against committed `test_towing.py` and server command |
| P2 | AI QA population | #490 is V3 test/playability capability, **not** original TW2002 canon; track separately |
| V4 | SQL Server backend #493 | Independent branch, explicitly outside V3 parity |
| V4 | Federation #472–#483 | Defer implementation; preserve V3 event/identity/transaction contracts |

## 5. Client acceptance rules

A server RPC existing is **not** a completed client feature. For each required capability Codex must inspect:
- Python `client.py` registration, `menus.json` reachability, payload, confirmation, refusal handling, state refresh and tests.
- Godot `CommandMenu.gd`, `GameplayView.gd`, `PlanetWorkflow.gd`, `PortWorkflow.gd`, `ProtocolTransport.gd`, `RequestManager.gd`, explicit `unavailable_reason` labels, and tests.
- Whether the workflow works for an ordinary non-SysOp account, with no raw JSON or debug command entry.
- Both clients' handling of denied commands, stale responses, partial transactions, reconnect and unknown fields.

The older [Godot client audit](reports/godot-client-audit.md) described an early prototype; **do not use its conclusions as current state without checking the newer committed `CommandMenu.gd` and workflow modules**. Likewise the Python [development handover](python-client-development-handover.md) contains historical slices; use current code/tests for disposition.

## 6. Codex review and audit completion checklist

1. Pin the exact commit SHA corresponding to the audited tree; record `git rev-parse HEAD` and working-tree status.
2. Read all five historical sources, cite chapter/section for each TW2002 requirement, and distinguish later TradeWars versions and optional features. Challenge unsupported “canonical” GitHub claims.
3. Enumerate **all** registered player commands from `src/server_loop.c`, classify handler as real, stub, mismatched, inaccessible or alias; follow through repository/schema.
4. Enumerate all normal-player reachable Python menu flows and Godot command/workflow entries; compare against each server command and canonical capability.
5. Reconcile **open and closed** GitHub issues; label stale issues and regressions, but do not close automatically.
6. Examine tests for assertions rather than filenames; identify positive, negative, adversarial and live persistence coverage separately.
7. Split grouped rows into atomic capabilities; replace `?` with grounded statuses and file/line references; identify deliberate differences with an explicit design source.
8. Prioritise server-rule defects before client polish, and client workflow gaps before noncanonical expansion.
9. On restored infrastructure, execute disposable PostgreSQL live-server scenarios, then Python and Godot gameplay journeys. Record command transcripts, version/seed and database invariants.
10. Produce a revised matrix with evidence confidence and a justified denominator. **Do not reuse the old 85–90% score.**

## 7. Release decision

**V3 parity is not demonstrated by the committed evidence reviewed here.** The project has broad implemented foundations, but multiple high-impact canonical-rule and client-contract gaps remain open or contradictory. This is a **reviewable first-pass gap register**, not a signed-off line-by-line canonical certification. Codex should use it as a falsifiable checklist, correct false positives/negatives, and produce the final source-and-canon-cited release matrix.

No GitHub issues, runtime code or tests were modified by this document.
