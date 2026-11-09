import sys
from pathlib import Path
import time
from unittest.mock import MagicMock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from planner import Planner  # noqa: E402


def _planner():
    planner = Planner.__new__(Planner)
    planner.state_manager = MagicMock()
    planner.config = {}
    planner._get_fuzzed_buy_quantity = lambda maximum: min(1, maximum)
    return planner


def test_multiple_cargo_types_choose_a_valid_non_origin_buyer():
    planner = _planner()
    planner.state_manager.find_path.side_effect = lambda _start, target: {
        2: [1, 2], 3: [1, 4, 3], 4: [1, 4],
    }[target]
    state = {
        "player_location_sector": 1,
        "ship_info": {"cargo": [
            {"commodity": "ORE", "quantity": 1, "purchase_price": 100, "origin_port_id": 101},
            {"commodity": "ORG", "quantity": 1, "purchase_price": 80, "origin_port_id": 103},
        ]},
        "port_info_by_sector": {
            "2": {"port_id": 102}, "3": {"port_id": 103}, "4": {"port_id": 104},
        },
        "price_cache": {
            "102": {"sell": {"ORE": 140, "ORG": None}, "quoted_at": {"ORE": time.time(), "ORG": time.time()}},
            "103": {"sell": {"ORE": 220, "ORG": 90}, "quoted_at": {"ORE": time.time(), "ORG": time.time()}},
            "104": {"sell": {"ORE": 120, "ORG": 130}, "quoted_at": {"ORE": time.time(), "ORG": time.time()}},
        },
    }

    assert planner._find_port_for_cargo(state) == (3, "ORE")


def test_rerouting_uses_next_known_hop_after_refused_first_hop():
    planner = _planner()
    planner.state_manager.get.return_value = [24]
    planner._is_command_ready = lambda *_args: True
    state = {
        "player_location_sector": 10,
        "sector_data": {"10": {"adjacent": [24, 30]}},
        # The first route was refused at 24; the authoritative reroute now
        # contains 30 as the next hop and must be used without replaying 24.
        "current_path": [10, 30, 42],
        "warp_blacklist": [24],
        "command_retry_info": {},
    }

    command = planner._achieve_goal(state, "goto: 42")

    assert command == {"command": "move.warp", "data": {"to_sector_id": 30}}
