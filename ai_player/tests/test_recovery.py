import sys
import time
from pathlib import Path


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

from main import advance_completed_goals, is_goal_complete, process_responses  # noqa: E402
from state_manager import StateManager  # noqa: E402


class FakeStateManager:
    def __init__(self, state, pending):
        self.state = state
        self.pending = pending
        self.updates = []

    def get_all(self):
        return dict(self.state)

    def get_pending_command(self, request_id):
        return self.pending.get(request_id)

    def remove_pending_command(self, request_id):
        self.pending.pop(request_id, None)

    def add_pending_command(self, request_id, command):
        self.pending[request_id] = command

    def set(self, key, value):
        self.state[key] = value
        self.updates.append((key, value))

    def get(self, key, default=None):
        return self.state.get(key, default)

    def set_last_action_result(self, value):
        self.state["last_action_result"] = value

    def record_command_failure(self, _command):
        pass

    def record_ai_metric(self, name, amount=1):
        metrics = self.state.setdefault("ai_metrics", {})
        metrics[name] = metrics.get(name, 0) + amount

    def add_to_warp_blacklist(self, sector_id):
        self.state.setdefault("warp_blacklist", []).append(sector_id)

    def validate_state(self):
        return True


class FakeConnection:
    def __init__(self):
        self.sent = []

    def send_command(self, command):
        self.sent.append(command)
        return True


class UnusedBugReporter:
    def report_state_inconsistency(self, *_args):
        raise AssertionError("unexpected state inconsistency")


def test_pending_command_timeout_removes_only_expired_request(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.add_pending_command("old", {"command": "trade.buy"})
    manager.add_pending_command("new", {"command": "sector.info"})
    manager._pending_command_times["old"] = time.monotonic() - 60

    assert manager.expire_pending_commands(30) == [
        ("old", {"command": "trade.buy"})
    ]
    assert manager.get_pending_command("new") == {"command": "sector.info"}


def test_atomic_state_save_keeps_previous_state_when_serialization_fails(tmp_path):
    path = tmp_path / "state.json"
    manager = StateManager(str(path), {})
    manager.set("strategy_plan", ["goto: 42", "survey: port"])
    original = path.read_bytes()

    manager.state["invalid_for_json"] = object()
    manager.save_state()

    assert path.read_bytes() == original
    assert list(tmp_path.glob(".state.json.*.tmp")) == []


def test_persisted_goto_goal_completes_with_string_or_numeric_sector_id():
    assert is_goal_complete("goto: 42", {"player_location_sector": "42"}, {}, "state.snapshot")
    assert is_goal_complete("goto: 42", {}, {"to_sector_id": "42"}, "move.result")


def test_persisted_survey_goal_completes_from_fresh_cached_quotes():
    state = {
        "player_location_sector": 42,
        "port_info_by_sector": {
            "42": {"port_id": 7, "commodities": [{"commodity": "ORE"}]}
        },
        "price_cache": {"7": {"buy": {"ORE": 9}, "sell": {"ORE": None}, "quoted_at": {"ORE": time.time()}}},
    }

    assert is_goal_complete("survey: port", state, {}, "state.snapshot")


def test_plan_advances_completed_prefix_and_preserves_later_goals():
    state = {
        "player_location_sector": "42",
        "port_info_by_sector": {
            "42": {"port_id": 7, "commodities": [{"commodity": "ORE"}]}
        },
        "price_cache": {"7": {"buy": {"ORE": 9}, "sell": {"ORE": None}, "quoted_at": {"ORE": time.time()}}},
    }

    remaining = advance_completed_goals(
        ["goto: 42", "survey: port", "buy: ORE", "sell: ORE"], state
    )
    assert remaining == ["buy: ORE", "sell: ORE"]


def test_refused_warp_blacklists_hop_and_discards_stale_goto_plan():
    state = FakeStateManager(
        {
            "player_location_sector": 10,
            "session_id": "session",
            "strategy_plan": ["goto: 42", "survey: port"],
            "current_path": [10, 24, 42],
            "recent_sectors": [],
        },
        {"warp-request": {"command": "move.warp", "data": {"to_sector_id": 24}}},
    )
    connection = FakeConnection()

    process_responses(
        [{
            "reply_to": "warp-request",
            "status": "refused",
            "type": "error",
            "error": {"code": 1453, "message": "No link"},
        }],
        game_conn=connection,
        state_manager=state,
        bug_reporter=UnusedBugReporter(),
        bandit_policy=None,
        config={},
    )

    assert 24 in state.state["warp_blacklist"]
    assert state.state["current_path"] == []
    assert state.state["strategy_plan"] == ["survey: port"]
    assert {cmd["command"] for cmd in connection.sent} == {"player.my_info", "ship.info"}


def test_refused_pathfind_discards_unreachable_navigation_goal():
    state = FakeStateManager(
        {
            "player_location_sector": 10,
            "strategy_plan": ["goto: 42", "sell: ORE"],
            "current_path": [10, 24, 42],
        },
        {"path-request": {"command": "move.pathfind", "data": {"to_sector_id": 42}}},
    )

    process_responses(
        [{
            "reply_to": "path-request",
            "status": "refused",
            "type": "error",
            "error": {"code": 1453, "message": "No path"},
        }],
        game_conn=FakeConnection(),
        state_manager=state,
        bug_reporter=UnusedBugReporter(),
        bandit_policy=None,
        config={},
    )

    assert state.state["current_path"] == []
    assert state.state["strategy_plan"] == ["sell: ORE"]
