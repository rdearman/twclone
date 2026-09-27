import sys
from pathlib import Path
from unittest.mock import MagicMock


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

from main import process_responses  # noqa: E402
from planner import Planner  # noqa: E402


def test_goto_pathfind_uses_only_protocol_fields():
    planner = Planner.__new__(Planner)
    planner.state_manager = MagicMock()
    planner.state_manager.find_path.return_value = []
    planner._is_command_ready = lambda _command, _retry: True

    command = planner._achieve_goal(
        {
            "player_location_sector": 10,
            "sector_data": {"10": {"adjacent": []}},
            "command_retry_info": {},
        },
        "goto: 42",
    )

    assert command == {
        "command": "move.pathfind",
        "data": {"from_sector_id": 10, "to_sector_id": 42},
    }


class _Unused:
    def report_state_inconsistency(self, *_args):
        raise AssertionError("unexpected state inconsistency")


class _Connection:
    def send_command(self, _command):
        pass


def test_pathfind_response_is_cached_after_pending_request_is_removed():
    state = MagicMock()
    state.get_all.return_value = {"strategy_plan": []}
    state.get_pending_command.return_value = {"command": "move.pathfind"}
    state.validate_state.return_value = True

    process_responses(
        [
            {
                "reply_to": "path-request",
                "status": "ok",
                "type": "move.pathfind",
                "data": {"steps": [10, 24, 42]},
            }
        ],
        game_conn=_Connection(),
        state_manager=state,
        bug_reporter=_Unused(),
        bandit_policy=None,
        config={},
    )

    state.remove_pending_command.assert_called_once_with("path-request")
    state.set.assert_any_call("current_path", [10, 24, 42])


def test_confirmed_warp_advances_cached_server_path():
    state = MagicMock()
    state.get_all.return_value = {"strategy_plan": [], "current_path": [10, 24, 42]}
    state.get_pending_command.return_value = {
        "command": "move.warp",
        "data": {"to_sector_id": 24},
    }
    state.get.return_value = []
    state.validate_state.return_value = True

    process_responses(
        [
            {
                "reply_to": "warp-request",
                "status": "ok",
                "type": "move.result",
                "data": {"to_sector_id": 24},
            }
        ],
        game_conn=_Connection(),
        state_manager=state,
        bug_reporter=_Unused(),
        bandit_policy=None,
        config={},
    )

    state.set.assert_any_call("player_location_sector", 24)
    state.set.assert_any_call("current_path", [24, 42])
