# V3 StarPort acceptance — 19 November 2026

This acceptance closes the remaining StarPort clause evidence using a fresh
disposable PostgreSQL database and a serving V3 server built from the current
working source. All service ports were loopback-only. No beta or production
service was contacted.

## Results

| Acceptance | Result | Evidence |
|---|---:|---|
| StarPort attack event | PASS | `manual_acceptance/starport_godot_event_acceptance.json`: 3/3; Godot `live_starport_attack_test.gd`: 8 checks. The ordinary player used the Godot UI, received `combat.port_strike.v1`, and the SQL assertions found one strike receipt, one `player.port_strike.v1` event, an inactive destroyed port and cleared stock. |
| Class-direction construction | PASS | `suite_starport_class_directions.json`: 7/7. The registered construction scheduler completed all eight classes; PostgreSQL checked the persisted fixed exchange directions and initial stock. |
| Attack numeric rules | PASS | `suite_starport_combat_numeric.json`: 12/12. Two ordinary-player attacks checked 1,000 defence, the damage formula, one turn per attack and shield→fighter→hull retaliation. Authenticated turn state moved 1,000→999→998; SQL confirmed durability and damage layers. |
| Godot construction workflow | PASS | `manual_acceptance/starport_godot_build_acceptance.json`: 3/3; Godot `live_starport_build_test.gd`: 8 checks. An ordinary player authenticated through the Godot login UI, selected and landed on planet 700, waited for authoritative planet/colonist reads, selected **Construct StarPort**, submitted class 8, and received `planet.port_construction_v1`. SQL found one construction record, a 100,000-credit debit (1,000,000,000→999,900,000), and 1,000 each of ORE/ORG/EQU debited from the planet. The player and landed-planet context remained confirmed after refresh. |

Focused client/regression results:

- Godot `command_menu_test.gd`: 244 checks passed.
- Godot `planet_landing_response_test.gd`: 91 checks passed.
- Python `tests.v2/test_deterministic_scheduler_contract.py`: 3 passed.
- `make -j4`: passed after the construction cron-handler correction.

## Defect and harness findings

`h_port_construction_tick` returned its number of processed rows, but the cron
dispatcher interprets nonzero handler returns as failure. Construction rows
were committed while `sys.cron.advance_to` reported failure. The handler now
returns zero on successful transaction completion; the construction class
acceptance then passed with the actual scheduler.

The first live Godot build attempt evaluated the menu while the expected
`planet.info` and `planet.colonists.get` refreshes were still in flight. The UI
correctly kept actions disabled during that period. The acceptance harness now
waits for both requests and the menu busy state to settle before asserting
that the action can be used. No landing or identity safety check was relaxed.

## Reproduction commands

Run the database-backed cases with the repository's isolated QA runner, using
unique loopback service ports:

```sh
python3 tests.v2/run_regression_instance.py \
  --suite suite_starport_class_directions.json \
  --port 18555 --s2s-port 53357

python3 tests.v2/run_regression_instance.py \
  --suite suite_starport_combat_numeric.json \
  --port 18556 --s2s-port 53358
```

For the live Godot build case, run the disposable runner with
`manual_acceptance/starport_godot_build_acceptance.json`, start
`gdot --headless --path client/godot_client/godot --script
res://tests/live_starport_build_test.gd` during its fixture window, and provide
the temporary server port and ordinary fixture account through the
`TWCLONE_GODOT_PORT_BUILD_LIVE`, `TWCLONE_GODOT_PORT`,
`TWCLONE_GODOT_USERNAME`, and `TWCLONE_GODOT_PASSWORD` environment variables.
The runner captures sanitized protocol replies and removes its disposable
database when complete.

Live Godot acceptance used Godot 4.5.1. This report does not establish a new
Godot 4.5.2 run. Live GitHub publication remains unrelated and unverified.
