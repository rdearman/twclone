# V2 Client Regression Status

## Coverage added

- `tests.v2/suite_client_regressions.py` runs the Python client tests, imports the Godot project from source assets, runs every Godot headless test, and validates the shared asset catalogue without connecting to the game server or database. `tests.v2/run_suites_all.py` discovers it as a `suite_*.py` test. The import step prevents tests from passing only because a developer already has a populated `.godot/imported` cache.
- `client/godot_client/godot/tests/asset_catalog_test.gd` protects object-to-asset resolution, legacy planet type fallback, explicit server `asset_id` precedence, image variant loading, and class K versus class L image distinction. It also composes entities containing newer or extra metadata to ensure the renderer selects the intended art without failing.
- `client/godot_client/godot/tests/command_result_test.gd` checks that an authoritative server refusal is shown to the player with the command name and server reason.

Existing Python and Godot regressions in the run cover menu reachability, unsupported command gating, autopilot and other server refusal messages, and the current player-facing command flows. These tests prevent a command from silently disappearing from menus, an unsupported command from being offered as usable, a refusal from looking like success, or different planet classes from accidentally sharing artwork.

## Verification

- Python client tests: **130 passed**.
- Godot headless tests: **211 checks passed** across all `*_test.gd` scripts.
- Shared asset catalogue validation: **passed**, 13 catalogue assets and 0 warnings.

Run the offline client regression suite with:

```bash
python3 tests.v2/suite_client_regressions.py
```

Set `GODOT_BIN` if Godot is not available as `godot` or at `~/bin/Godot_v4.5.1-stable_linux.x86_64`. The suite reports a skip for Godot when no executable is found; Python tests and asset validation still run.

## Scope

This pass changed tests and this report only. It did not change client behavior, server code, protocol definitions, or database files. The full database-backed `tests.v2` integration suites were not run; this regression suite is deliberately offline.
