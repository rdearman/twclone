# Regression failure classification

Latest full execution: **2,242 executed; 2,168 passed; 74 failed**. The runner marked the report `incomplete` because one TLS suite was blocked; 157 suites executed, 3 were unsupported, and there were no server crashes. The run started at `2026-11-10T22:15:16Z`; the runner did not write a completion timestamp.

| Primary classification | Cases |
| --- | ---: |
| IMPLEMENTATION_DEFECT | 2 |
| MISSING_IMPLEMENTATION | 0 |
| OBSOLETE_V2_TEST | 16 |
| PROTOCOL_SCHEMA_MISMATCH | 3 |
| SPECIFICATION_BLOCKER | 6 |
| INFRASTRUCTURE | 8 |
| UNCLASSIFIED | 2 |
| FIXTURE_DEFECT | 37 |
| **Failed total** | **74** |

Blocked (1) and unsupported (3) are outside the failed total.

The six previously unclassified cases are now reconciled from preserved
request/response evidence and the current V3 contracts. The two
`auth.mfa_verify` cases use a command absent from the registry; the registry
does expose `auth.mfa.totp.verify`, but the V3 authentication contract does not
approve a rename or define MFA behavior, so the historical tests must not yet
be rewritten. Three legacy SysOp requests (`sysop.motd.set`,
`sysop.port.restock`, `sysop.audit.tail`) are specification blockers: they are
neither registered nor documented, and no approved removal is recorded. The
kicked-player case is a confirmed implementation defect: the V3 contract
requires session invalidation, while the captured post-kick `player.my_info`
succeeded because the handler only closed sockets. The handler now revokes
durable target sessions first; runtime verification remains pending.

Reconciliation of the other previously recorded protocol mismatches found
two additional actionable defects: the shipyard-upgrade discoverable schema
did not express the documented new-versus-used acquisition alternatives, so
the schema is corrected with `oneOf` and its existing JSON case now checks both
branches; the planet-deposit failure used an unresolved planet-ID substitution
and an unsupported `ship_id` field, so its fixture now captures the inserted
planet ID and establishes ownership/landing state. The standing-order
ownership case asserted an undocumented exact 404; its JSON expectation now
requires an error without prescribing the code. These edits are not runtime
verified. The historical 74-case classification now has 2 implementation
defects, 3 protocol mismatches, 6 specification blockers, 37 fixture defects,
16 obsolete V2 tests, 8 infrastructure failures, 0 missing implementations
and 2 unclassified cases.

The two citadel failures are no longer called confirmed missing
implementations. The handler reads per-level costs and duration from
`planettypes`, and the canonical lookup seed contains those values. The error
shows that the selected row had no usable positive duration, but the lost run
report did not preserve that planet type or its `citadelUpgradeTime` value.
The exact cause remains UNCLASSIFIED pending a runtime capture; no costs or
seed values are invented.

## Exchange attribution

Previous full run had 92 failures; current has 74 (net change -18): 21 prior failures resolved and 3 new failures appeared. Exactly two resolved cases directly evidenced the Exchange clearing-account failure (`suite_planet_taxable_activity_e2e.json::case-0018` and `case-0019`); the other 19 resolved cases are unrelated. The current port-robbery failure is a fixture selecting a port in FedSpace, where V3 explicitly refuses aggression. Two new Exchange assertions were caused by the test port's insufficient balance, not failed matching; an explicit QA fixture funding step corrected this and the focused scenario now passes. The remaining Ferengi case was also a fixture: a data-only definition sorted after the active population cap; the test now uses an early deterministic test code, preserving the real population limit.

The focused real-server PostgreSQL Exchange flow passed **36/36 executed cases** after funding the port buyer for the intended full fill. It verified planetary sale, ring-fenced goods, port purchase, stock conservation, linked ledger settlement and retry idempotency. These focused results do not replace the full-run figures. The later `suite_combat_port_rob` fixture was corrected to select a port outside sectors 1–10; this correction has not yet been rerun.

## Largest root-cause groups

- 13: Tavern fixture violates availability, economy limit, input or alignment precondition
- 10: Legacy port command naming or unsupported ALT commodity assumption
- 6: Unsupported sys.run_cron_handler trigger and dependent cleanup assertions
- 6: Fixture calls C repository function as SQL / combines statements in prepared SQL
- 4: Legacy expected success despite approved immediate lethal entry hazard and pod destruction response
- 3: Fixture selects a planet ID not present in generated universe
- 2: Citadel build/upgrade receive no usable duration; selected planet type and database value were not retained, so root cause is unclassified
- 1: Ferengi data-only roster fixture sorted beyond the configured active population cap
- 2: Unsupported sys.run_cron_handler test hook
- 1: Fixture leaves Exchange liquidity at configured zero default
- 1: suite_bucket1_remainder_ship_stock_system.json: ship.jettison - Positive
- 1: suite_bucket1_remainder_ship_stock_system.json: ship.sell - Positive
- 1: suite_bucket1_remainder_ship_stock_system.json: ship.transfer - Positive
- 1: suite_bucket1_remainder_ship_stock_system.json: ship.transfer_cargo - Positive
- 1: suite_bucket1_remainder_ship_stock_system.json: shipyard.sell - Positive
- 1: suite_combat_toll_fighters.json: Victim Warps to Toll Sector (Should Auto-Pay)
- 1: suite_combat_toll_final_working.json: Test: Toll Collector Verified
- 1: suite_combat_toll_final_working.json: Test: Victim Verified
- 1: Ownership refusal test asserts undocumented exact code 404; actual refusal is 403
- 1: Legacy status assertion expects active rather than podded after destruction
- 1: suite_movement_move_autopilot_start.json: Positive: Start Autopilot
- 1: suite_navigation.json: Get Route
- 1: Legacy test expects no active ship where V3 mandates active escape pod
- 1: Schema assertion assumes new_type_id required despite used_inventory_id alternative
- 1: Bootstrap System player is authenticated and included in online unique-player count
- 1: suite_police_phase_d1_enforcement.json: Test D1-D1: Dock intercept should block docking when wanted=2
- 1: suite_police_phase_d1_enforcement.json: Test D1-L3: Dock intercept should NOT trigger in lawless cluster
- 1: suite_ship_sale_v3.json: Sell inactive hull through shipyard.sell alias
- 1: suite_sys_admin_boundaries.json: Admin: admin.notice (Allow)
- 1: test_sysop_players.json: Verify Player Disconnected
- 1: test_sysop_world.json: Set MOTD
- 1: test_sysop_world.json: Queue Port Restock
- 1: test_sysop_world.json: View Audit Tail

## Case-level evidence

The JSON inventory contains each stable ID, suite, request, expected/asserted values, actual response, classification, root cause, source reference, action and confidence. Truly uncertain cases remain `UNCLASSIFIED`.
