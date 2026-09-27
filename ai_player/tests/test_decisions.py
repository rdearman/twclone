import sys
from pathlib import Path
from unittest.mock import MagicMock


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

from bandit_policy import make_context_key  # noqa: E402
from planner import Planner  # noqa: E402


def make_planner(profile="balanced"):
    planner = Planner.__new__(Planner)
    planner.config = {"behavior_profile": profile}
    planner.state_manager = MagicMock()
    planner.state_manager.get.return_value = []
    planner.bandit_policy = MagicMock()
    planner.bandit_policy.epsilon = 0.1
    return planner


def navigation_state(profile):
    return {
        "player_location_sector": 10,
        "recent_sectors": [20],
        "sector_data": {"10": {"adjacent": [20, 30, 40]}},
        "universe_map": {
            "20": {"is_explored": True},
            "30": {"is_explored": False},
            "40": {"is_explored": True},
        },
        "port_info_by_sector": {"40": {"port_id": 400}},
    }


def test_profiles_choose_targets_according_to_their_priorities():
    assert make_planner("explorer")._get_next_warp_target(navigation_state("explorer")) == 30
    assert make_planner("balanced")._get_next_warp_target(navigation_state("balanced")) == 30
    assert make_planner("trader")._get_next_warp_target(navigation_state("trader")) == 40
    assert make_planner("cautious")._get_next_warp_target(navigation_state("cautious")) == 40


def test_navigation_ignores_current_sector_and_blacklisted_warps():
    planner = make_planner()
    planner.state_manager.get.return_value = [20]
    state = navigation_state("balanced")
    state["sector_data"]["10"]["adjacent"] = [10, 20, 30]

    assert planner._get_next_warp_target(state) == 30


def test_automatic_exploit_actions_never_start_combat():
    planner = make_planner()
    state = {
        "player_location_sector": 50,
        "sector_data": {"50": {"ships_present": [{"ship_id": 900}]}},
    }

    actions = planner._build_action_catalogue_by_stage(state, "exploit")
    assert "combat.attack" not in actions
    assert "combat.deploy_fighters" not in actions
    assert "combat.deploy_mines" not in actions


def test_explicit_combat_goal_is_refused_in_fedspace():
    planner = make_planner()
    state = {
        "player_location_sector": 3,
        "ship_info": {"id": 100},
        "sector_data": {"3": {"ships_present": [{"ship_id": 900}]}},
    }

    assert planner._achieve_goal(state, "combat: attack") is None


def test_combat_target_uses_visible_ship_id_and_excludes_own_ship():
    planner = make_planner()
    state = {
        "player_location_sector": 50,
        "ship_info": {"id": 100},
        "sector_data": {
            "50": {"ships_present": [{"ship_id": 100}, {"ship_id": 900}]}
        },
    }

    assert planner._generate_required_field("target_ship_id", "combat.attack", state) == 900


def test_payload_builder_blocks_direct_combat_commands_in_fedspace():
    planner = make_planner()
    planner.state_manager.get_all.return_value = {"player_location_sector": 7}

    assert planner._build_payload("combat.attack", {}) is None
    assert planner._build_payload("combat.deploy_fighters", {}) is None


def test_stage_epsilon_uses_profile_stage_setting_and_clamps_values():
    planner = make_planner()
    planner.current_stage = "explore"
    planner.config["epsilon_explore"] = 0.25
    assert planner._stage_epsilon() == 0.25

    planner.config["epsilon_explore"] = 2
    assert planner._stage_epsilon() == 1.0

    planner.config["epsilon_explore"] = "bad"
    assert planner._stage_epsilon() == 0.1


def test_bandit_context_generalizes_across_ports_and_uses_current_credits():
    base = {
        "stage": "exploit",
        "player_location_sector": 10,
        "sector_data": {"10": {"class": "A", "ports": [{"id": 101}]}},
        "ship_info": {"holds": "20", "cargo": [{"quantity": "8"}]},
        "player_info": {"player": {"credits": "15000"}},
    }
    another_port = {
        **base,
        "player_location_sector": 20,
        "sector_data": {"20": {"class": "A", "ports": [{"id": 202}]}},
    }

    first_key = make_context_key(base, {})
    second_key = make_context_key(another_port, {})
    assert first_key == second_key
    assert "credits:medium" in first_key
    assert "holds_full:not_full" in first_key


def test_long_goto_uses_advertised_persisted_autopilot_route():
    planner = make_planner()
    planner.state_manager.find_path.return_value = None
    planner.state_manager.get.return_value = []
    state = {
        "player_location_sector": 10,
        "sector_data": {"10": {"adjacent": [20]}},
        "current_path": [],
        "server_commands": ["move.autopilot.start"],
        "command_retry_info": {},
    }

    command = planner._achieve_goal(state, "goto: 90")
    assert command == {
        "command": "move.autopilot.start",
        "data": {"from_sector_id": 10, "to_sector_id": 90},
    }


def test_stopped_matching_autopilot_route_is_resumed():
    planner = make_planner()
    state = {
        "player_location_sector": 10,
        "autopilot_status": {"state": "stopped", "target_sector_id": 90},
        "server_commands": ["move.autopilot.control"],
    }

    assert planner._achieve_goal(state, "goto: 90") == {
        "command": "move.autopilot.control", "data": {"action": "continue"}
    }
