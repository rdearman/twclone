import sys
from datetime import datetime, timezone
from pathlib import Path


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

import main  # noqa: E402
from state_manager import StateManager  # noqa: E402


class MemoryState:
    def __init__(self):
        self.state = {}
        self.saves = 0

    def save_state(self):
        self.saves += 1

    def set(self, key, value):
        self.state[key] = value


def test_population_turn_budget_causes_configured_inactivity_and_resumes():
    manager = MemoryState()
    config = {
        "population_mode": True,
        "population_active_minutes": 10,
        "population_inactive_minutes": 5,
        "population_max_turns_per_period": 2,
    }

    assert main.population_pause_seconds(config, manager, now=100, turns_remaining=20) == 0
    assert main.population_pause_seconds(config, manager, now=110, turns_remaining=19) == 0
    pause = main.population_pause_seconds(config, manager, now=120, turns_remaining=18)

    assert pause == 300
    assert manager.state["population_turns_used"] == 2
    assert main.population_pause_seconds(config, manager, now=200, turns_remaining=18) == 220
    assert main.population_pause_seconds(config, manager, now=420, turns_remaining=18) == 0
    assert manager.state["population_turns_used"] == 0


def test_population_active_period_is_time_bounded():
    manager = MemoryState()
    config = {
        "population_mode": True,
        "population_active_minutes": 1,
        "population_inactive_minutes": 2,
        "population_max_turns_per_period": 10,
    }

    assert main.population_pause_seconds(config, manager, now=100, turns_remaining=20) == 0
    assert main.population_pause_seconds(config, manager, now=160, turns_remaining=20) == 120


def test_population_daily_utc_window_suspends_outside_configured_hours():
    manager = MemoryState()
    now = datetime(2026, 10, 28, 3, 0, tzinfo=timezone.utc).timestamp()
    config = {
        "population_mode": True,
        "population_daily_start_utc": "08:00",
        "population_daily_end_utc": "22:00",
    }

    assert main.population_pause_seconds(config, manager, now=now) == 5 * 60 * 60
    assert manager.state == {}


def test_population_pacing_is_disabled_by_default():
    manager = MemoryState()

    assert main.population_pause_seconds({}, manager, now=100, turns_remaining=10) == 0
    assert manager.state == {}


def test_population_turn_budget_refreshes_after_possible_mutations_only():
    assert main.population_turn_readback_required("move.warp", {"population_mode": True})
    assert main.population_turn_readback_required("trade.buy", {"population_mode": True})
    assert not main.population_turn_readback_required("player.my_info", {"population_mode": True})
    assert not main.population_turn_readback_required("system.schema", {"population_mode": True})
    assert main.population_turn_readback_required(
        "trade.sell", {"population_mode": True, "live_gameplay_audit": True}
    )


def test_population_fails_closed_until_authoritative_turn_readback():
    manager = MemoryState()
    manager.state["population_turns_unverified"] = True

    assert main.population_pause_seconds(
        {"population_mode": True}, manager, now=100, turns_remaining=10
    ) == 60


def test_live_command_catalogue_is_cached_without_schema_fanout():
    manager = MemoryState()
    manager.state["pending_schema_requests"] = {"old.command": "old-request"}

    commands = main.cache_server_command_catalogue({
        "commands": [
            {"cmd": "player.my_info", "summary": "Player state"},
            {"cmd": "trade.quote", "summary": "Quote"},
            {"summary": "Malformed entry"},
        ]
    }, manager)

    assert commands == ["player.my_info", "trade.quote"]
    assert manager.state["server_commands"] == commands
    assert manager.state["pending_schema_requests"] == {}
    assert main.schema_commands_to_fetch(commands) == ["player.my_info", "trade.quote"]
    assert main.schema_commands_to_fetch(
        commands, {"player.my_info": {"type": "object"}}
    ) == ["trade.quote"]
    assert "admin.shutdown_warning" not in main.schema_commands_to_fetch(
        ["admin.shutdown_warning", "trade.buy"]
    )


def test_public_registration_is_opt_in_and_attempted_once_across_restart(tmp_path):
    state_path = tmp_path / "registration-state.json"
    manager = StateManager(str(state_path), config={})
    assert not main.should_attempt_registration({}, manager)
    assert main.should_attempt_registration({"register_if_missing": True}, manager)

    manager.set("registration_attempted", True)
    resumed = StateManager(str(state_path), config={})
    assert not main.should_attempt_registration({"register_if_missing": True}, resumed)


def test_command_timeout_metrics_include_command_name_but_not_request_data(tmp_path):
    manager = StateManager(str(tmp_path / "timeout-state.json"), config={})
    manager.record_command_timeout("trade.buy")
    manager.record_command_timeout("trade.buy")
    manager.record_command_timeout("move.warp")

    metrics = manager.get("ai_metrics")
    assert metrics["commands_timed_out"] == 3
    assert metrics["command_timeout_trade_buy"] == 2
    assert metrics["command_timeout_move_warp"] == 1


def test_persistent_state_never_contains_session_credentials(tmp_path):
    state_path = tmp_path / "state.json"
    manager = StateManager(str(state_path), config={})
    manager.set("session_id", "runtime-session-secret")
    manager.set("player_info", {
        "player": {
            "id": 42,
            "session_token": "nested-session-secret",
            "session": "nested-legacy-secret",
            "credits": "5000.00",
        }
    })

    saved = state_path.read_text()
    assert "runtime-session-secret" not in saved
    assert "nested-session-secret" not in saved
    assert "nested-legacy-secret" not in saved
    assert manager.get("session_id") == "runtime-session-secret"
    assert manager.get("player_info")["player"]["session_token"] == "nested-session-secret"


def test_authoritative_ship_refresh_preserves_cargo_origin_across_reauthentication(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), config={})
    manager.set("ship_info", {"id": 39, "cargo": []})
    manager.update_cargo_after_buy(
        [{"commodity": "ORE", "quantity": 4, "unit_price": 120}],
        port_id=9,
    )

    manager.clear_connection_state(record_reconnect=False)
    assert manager.get("ship_info") is None

    manager.update_player_info({"ship": {
        "id": 39,
        "cargo": [{"commodity": "ore", "quantity": 4}],
    }})

    cargo = manager.get("ship_info")["cargo"]
    assert cargo == [{
        "commodity": "ORE", "quantity": 4,
        "purchase_price": 120, "origin_port_id": 9,
    }]
