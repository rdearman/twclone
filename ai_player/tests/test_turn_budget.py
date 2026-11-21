from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from turn_budget import (affordable_plan, command_fits_turn_budget,
                         command_turn_cost, goal_turn_cost,
                         session_heartbeat_interval, turn_refresh_due)


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


def test_session_heartbeat_stays_inside_server_request_deadline():
    assert session_heartbeat_interval({"server_request_timeout_ms": 10_000}) == 4
    assert session_heartbeat_interval({"server_request_timeout_ms": 5_000}) == 2
    assert session_heartbeat_interval({"session_heartbeat_interval_seconds": 3,
                                       "server_request_timeout_ms": 10_000}) == 3


def test_turn_budget_never_allows_a_costful_priority_action_at_zero_turns():
    assert not command_fits_turn_budget("move.warp", 0, reserve=12, priority=True)
    assert not command_fits_turn_budget("planet.land", 0, reserve=12, priority=True)
    assert command_fits_turn_budget("corp.create", 0, reserve=12, priority=True)
    assert command_fits_turn_budget("move.warp", 1, reserve=12, priority=True)
    assert not command_fits_turn_budget("move.warp", 1, reserve=12, priority=False)
