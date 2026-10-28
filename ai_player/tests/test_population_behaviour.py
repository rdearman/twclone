import sys
from datetime import datetime, timezone
from pathlib import Path


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

import main  # noqa: E402


class MemoryState:
    def __init__(self):
        self.state = {}
        self.saves = 0

    def save_state(self):
        self.saves += 1


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
