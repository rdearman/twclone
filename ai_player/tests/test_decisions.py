import sys
import time
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import MagicMock


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

from bandit_policy import make_context_key  # noqa: E402
from planner import Planner  # noqa: E402
from main import is_goal_complete  # noqa: E402
from state_manager import StateManager  # noqa: E402


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


def test_opportunist_prefers_a_fresh_public_recommended_market_route():
    state = navigation_state("opportunist")
    state.update({
        "market_recommendations_refreshed_at": time.time(),
        "market_recommendations": [{
            "sector_a_id": 20,
            "sector_b_id": 50,
            "a_to_b": True,
            "estimated_profit_a_to_b": 75,
        }],
    })

    assert make_planner("opportunist")._behavior_profile(state) == "opportunist"
    assert make_planner("opportunist")._get_next_warp_target(state) == 20


def test_planet_landing_uses_a_planet_id_from_visible_sector_state():
    planner = make_planner()
    state = {
        "player_location_sector": 12,
        "sector_data": {
            "12": {
                "has_planet": True,
                "celestial_objects": [{"planet_id": 321, "name": "QA World"}],
            }
        },
    }

    assert planner._achieve_goal(state, "planet: land") == {
        "command": "planet.land", "data": {"planet_id": 321}
    }


def test_planet_landing_defers_when_sector_state_has_no_planet_id():
    planner = make_planner()
    state = {
        "player_location_sector": 12,
        "sector_data": {"12": {"has_planet": True, "celestial_objects": []}},
    }

    assert planner._achieve_goal(state, "planet: land") is None


def test_survival_override_repairs_damaged_ship_at_known_port():
    planner = make_planner()
    planner.config["repair_hull_threshold"] = 70
    state = {
        "ship_info": {"hull": 40},
        "player_location_sector": 10,
        "sector_data": {"10": {"has_port": True}},
        "server_commands": ["ship.repair"],
        "command_retry_info": {},
    }
    assert planner._survival_command(state) == {"command": "ship.repair", "data": {}}


def test_survival_override_retreats_to_nearest_known_port():
    planner = make_planner()
    planner.state_manager.find_path.return_value = [10, 20]
    state = {
        "ship_info": {"hull": 20},
        "player_location_sector": 10,
        "sector_data": {"10": {"has_port": False}},
        "port_info_by_sector": {"20": {"port_id": 200}},
        "server_commands": ["move.warp"],
        "command_retry_info": {},
    }
    assert planner._survival_command(state) == {
        "command": "move.warp", "data": {"to_sector_id": 20}
    }


def test_planet_land_goal_completes_only_after_authoritative_landing_response():
    assert not is_goal_complete(
        "planet: land", {}, {"planet_id": 321}, "planet.info"
    )
    assert is_goal_complete(
        "planet: land", {}, {"planet_id": 321, "status": "landed"}, "planet.land"
    )
    assert not is_goal_complete(
        "planet: land", {}, {"planet_id": 321, "status": "refused"}, "planet.land"
    )


def test_planet_info_goal_completes_after_public_info_response():
    assert is_goal_complete("planet: info", {}, {"planet_id": 321}, "planet.info")
    assert not is_goal_complete("planet: info", {}, {"planet_id": 321}, "planet.land")


def test_planet_info_goal_waits_until_ship_is_landed():
    planner = make_planner()
    state = {
        "player_location_sector": 12,
        "sector_data": {
            "12": {"has_planet": True, "celestial_objects": [{"planet_id": 321}]}
        },
        "ship_info": {"onplanet": 0},
    }

    assert planner._achieve_goal(state, "planet: info") is None


def test_solo_policy_prioritizes_bank_security_over_optional_corporation_play():
    planner = make_planner()
    planner.config.update({
        "solo_empire_mode": True,
        "enable_bank_management": True,
        "operating_reserve": 5000,
        "minimum_bank_deposit": 1000,
        "allow_corporation_play": True,
        "corporation_win_condition": True,
    })
    state = {
        "player_info": {"player": {"credits": "8000"}},
        "player_location_sector": 12,
        "sector_data": {"12": {"has_planet": True}},
        "ship_info": {"onplanet": 0},
        "server_commands": ["bank.deposit"],
    }
    assert planner._solo_progression_goal(state) == "bank: secure"
    assert planner._achieve_goal(state, "corp: join") is None


def test_solo_policy_creates_planet_then_maintains_landed_citadel():
    planner = make_planner()
    planner.config.update({"solo_empire_mode": True, "enable_planet_creation": True})
    create_state = {
        "player_location_sector": 12,
        "sector_data": {"12": {"has_planet": False, "adjacent": [13, 14]}},
        "ship_info": {"genesis_torpedoes": 1, "onplanet": 0},
        "server_commands": ["planet.genesis_create"],
    }
    assert planner._solo_progression_goal(create_state) == "planet: create"
    assert planner._achieve_goal(create_state, "planet: create") == {
        "command": "planet.genesis_create",
        "data": {"sector_id": 12, "name": "AI World 12", "owner_entity_type": "player"},
    }

    landed_state = {
        "ship_info": {"onplanet": 321, "planet_id": 321},
        "planet_info": {"planet_id": 321, "citadel": {"level": 0, "construction_status": "idle"}},
        "server_commands": ["citadel.build"],
    }
    assert planner._achieve_goal(landed_state, "citadel: maintain") == {
        "command": "citadel.build", "data": {"planet_id": 321}
    }


def test_solo_policy_does_not_create_planet_in_three_exit_sector():
    planner = make_planner()
    planner.config.update({"solo_empire_mode": True, "enable_planet_creation": True})
    state = {
        "player_location_sector": 12,
        "sector_data": {"12": {"has_planet": False, "adjacent": [13, 14, 15]}},
        "ship_info": {"genesis_torpedoes": 1, "onplanet": 0},
        "server_commands": ["planet.genesis_create"],
    }
    assert planner._solo_progression_goal(state) is None


def test_solo_policy_assigns_workforce_and_reinforces_planet():
    planner = make_planner()
    planner.config.update({"solo_empire_mode": True, "enable_citadel_management": True})
    state = {
        "ship_info": {"onplanet": 321, "planet_id": 321, "fighters": 20},
        "planet_info": {"planet_id": 321, "colonists_unassigned": 10, "fighters": 0},
        "server_commands": ["planet.colonists.allocate", "planet.fighters.transfer"],
    }
    assert planner._solo_progression_goal(state) == "planet: workforce"
    state["planet_info"]["colonists_unassigned"] = 0
    assert planner._solo_progression_goal(state) == "planet: defend"
    assert planner._achieve_goal(state, "planet: defend") == {
        "command": "planet.fighters.transfer",
        "data": {"planet_id": 321, "direction": "ship_to_planet", "quantity": 20},
    }


def test_endgame_claim_preempts_growth_when_server_authorizes_victory():
    planner = make_planner()
    planner.config.update({"solo_empire_mode": True, "victory_command": "game.claim_victory"})
    state = {
        "victory_status": {"can_claim": True},
        "server_commands": ["game.claim_victory"],
        "ship_info": {},
    }
    assert planner._endgame_goal(state) == "victory: claim"
    assert planner._achieve_goal(state, "victory: claim") == {
        "command": "game.claim_victory", "data": {}
    }


def test_combat_readiness_repairs_at_port_and_avoids_threatened_exit():
    planner = make_planner()
    planner.config.update({"combat_min_hull_ratio": 0.65})
    state = {
        "ship_info": {"hull": 40, "max_hull": 100},
        "player_location_sector": 10,
        "sector_data": {"10": {"has_port": True, "adjacent": [20, 30]}},
        "server_commands": ["ship.repair"],
        "command_retry_info": {},
    }
    assert planner._combat_readiness_command(state) == {"command": "ship.repair", "data": {}}
    state["ship_info"]["hull"] = 100
    state["opponent_model"] = {"p1": {"sector_id": 20, "threat_score": 100}}
    assert planner._get_next_warp_target(state) == 30


def test_state_manager_persists_market_forecast_opponents_and_restart_plan():
    with TemporaryDirectory() as directory:
        path = str(Path(directory) / "state.json")
        config = {"default_cooldown": 1}
        manager = StateManager(path, config)
        manager.set("strategy_plan", ["goto: 9"])
        manager.update_price_cache({"port_id": 7, "commodity": "ORE", "buy_price": 10, "sell_price": 20})
        manager.update_price_cache({"port_id": 7, "commodity": "ORE", "buy_price": 10, "sell_price": 30})
        manager.record_opponent_observation([{"player_id": 8, "sector_id": 12, "hostile": True}])
        manager2 = StateManager(path, config)
        assert manager2.get("strategy_plan") == ["goto: 9"]
        assert manager2.forecast_market(7, "ORE")["sell"]["delta"] > 0
        assert manager2.get("opponent_model")["8"]["threat_score"] == 25


def test_mission_planner_honors_deadline_budget_and_fallback():
    planner = make_planner()
    state = {
        "mission": {"goals": ["goto: 44"], "deadline": time.time() - 1, "fallback": "goto: 2"},
        "player_info": {"player": {"credits": 5000}},
    }
    assert planner._mission_goal(state) == "goto: 2"


def test_fleet_roles_and_player_to_player_offer_are_explicit():
    planner = make_planner()
    state = {
        "fleet_state": {"ships": {"1": {"ship_id": 1}, "2": {"ship_id": 2, "role": "scout"}}},
        "server_commands": ["ship.info", "trade.offer"],
        "diplomacy_queue": [{"target_player_id": 9, "commodity": "ORE", "quantity": 3, "price": 40}],
    }
    assert planner._fleet_goal(state) == "fleet: assign"
    assert planner._achieve_goal(state, "fleet: assign")["command"] == "ship.info"
    assert planner._diplomacy_goal(state) == "diplomacy: offer"
    assert planner._achieve_goal(state, "diplomacy: offer") == {
        "command": "trade.offer",
        "data": {"target_player_id": 9, "commodity": "ORE", "quantity": 3, "price": 40},
    }


def test_decision_trace_and_learning_history_survive_restart():
    with TemporaryDirectory() as directory:
        path = str(Path(directory) / "state.json")
        manager = StateManager(path, {})
        manager.record_decision({"goal": "goto: 4", "reason": "mission", "source": "deterministic"})
        manager.record_learning_outcome("move.warp", "ctx", 2.5, True)
        restored = StateManager(path, {})
        assert restored.get("decision_trace")[0]["goal"] == "goto: 4"
        assert restored.get("learning_history")[0]["reward"] == 2.5


def test_navigation_prefers_safe_exit_after_entry_hazard():
    planner = make_planner()
    planner.state_manager.get.return_value = []
    state = navigation_state("explorer")
    state["sector_danger"] = {"20": {"severity": 4, "hazards": ["mine"]}}
    assert planner._get_next_warp_target(state) == 30


def test_autonomous_exploit_does_not_randomly_attempt_planet_landings():
    planner = make_planner()
    state = {
        "player_location_sector": 12,
        "sector_data": {"12": {"has_planet": True}},
        "ship_info": {"onplanet": 0, "cargo": [], "holds": 10},
        "player_info": {"player": {"credits": "5000"}},
    }

    for _ in range(25):
        actions = planner._build_action_catalogue_by_stage(state, "exploit")
        assert "planet.land" not in actions
        assert "planet.info" not in actions


def test_autonomous_planet_info_is_available_after_landing():
    planner = make_planner()
    state = {
        "player_location_sector": 12,
        "sector_data": {"12": {"has_planet": True}},
        "ship_info": {"onplanet": 321, "cargo": [], "holds": 10},
        "player_info": {"player": {"credits": "5000"}},
    }

    actions = planner._build_action_catalogue_by_stage(state, "exploit")
    assert "planet.info" in actions


def test_navigation_ignores_current_sector_and_blacklisted_warps():
    planner = make_planner()
    planner.state_manager.get.return_value = [20]
    state = navigation_state("balanced")
    state["sector_data"]["10"]["adjacent"] = [10, 20, 30]

    assert planner._get_next_warp_target(state) == 30


def test_nearest_known_sector_uses_shortest_known_route_not_cache_order():
    planner = make_planner()
    planner.state_manager.find_path.side_effect = lambda _origin, target: {
        20: [10, 11, 12, 20],
        30: [10, 30],
    }[int(target)]
    state = {"player_location_sector": 10}

    assert planner._nearest_known_sector(state, ["20", "30"]) == 30


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
