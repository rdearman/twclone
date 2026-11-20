from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from turn_budget import affordable_plan, command_turn_cost, goal_turn_cost, turn_refresh_due


def test_server_confirmed_turn_costs_cover_movement_trading_scans_and_combat():
    assert command_turn_cost("move.warp") == 1
    assert command_turn_cost("move.transwarp") == 1
    assert command_turn_cost("trade.buy") == 1
    assert command_turn_cost("trade.sell") == 1
    assert command_turn_cost("sector.scan") == 1
    assert command_turn_cost("move.scan") == 1
    assert command_turn_cost("sector.scan.density") == 0
    assert command_turn_cost("combat.flee") == 1
    assert command_turn_cost("trade.quote") == 0
    assert command_turn_cost("player.my_info") == 0


def test_route_estimate_uses_known_hops_and_unknown_route_is_one_step():
    state = {"player_location_sector": 1}
    assert goal_turn_cost("goto: 4", state, lambda _a, _b: [1, 2, 3, 4]) == 3
    assert goal_turn_cost("goto: 4", state, lambda _a, _b: None) == 1
    assert goal_turn_cost("goto: 1", state) == 0


def test_budget_trims_lower_priority_goals_and_preserves_configured_priority_turns():
    state = {"player_location_sector": 1}
    plan = ["goto: 2", "buy: ORE", "planet: land"]
    assert affordable_plan(plan, state, 14, 12) == ["goto: 2", "buy: ORE"]
    assert affordable_plan(["planet: land", "buy: ORE"], state, 2, 12,
                           ["planet:"]) == ["planet: land"]
    assert affordable_plan(plan, state, 0, 12, ["planet:"]) == []


def test_exhausted_turn_refresh_poll_is_throttled_without_state_invalidation():
    assert turn_refresh_due(0, 100, 30)
    assert not turn_refresh_due(90, 100, 30)
    assert turn_refresh_due(90, 120, 30)
