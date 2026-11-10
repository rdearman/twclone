# Regression failure classification

Latest full execution: **2,242 executed; 2,168 passed; 74 failed**. The runner marked the report `incomplete` because one TLS suite was blocked; 157 suites executed, 3 were unsupported, and there were no server crashes. The run started at `2026-11-10T22:15:16Z`; the runner did not write a completion timestamp.

| Primary classification | Cases |
| --- | ---: |
| IMPLEMENTATION_DEFECT | 0 |
| MISSING_IMPLEMENTATION | 2 |
| OBSOLETE_V2_TEST | 16 |
| PROTOCOL_SCHEMA_MISMATCH | 3 |
| SPECIFICATION_BLOCKER | 3 |
| INFRASTRUCTURE | 8 |
| UNCLASSIFIED | 6 |
| FIXTURE_DEFECT | 36 |
| **Failed total** | **74** |

Blocked (1) and unsupported (3) are outside the failed total.

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
- 2: No citadel requirement rows seeded for supported planet type
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
