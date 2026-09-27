import sys
from pathlib import Path


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

from simulation import load_scenarios, run_scenario, run_suite  # noqa: E402


def test_scenario_episodes_are_repeatable_for_a_fixed_seed():
    scenario = next(item for item in load_scenarios() if item["name"] == "risky_frontier")

    first = run_scenario(scenario, "adaptive", seed=31)
    second = run_scenario(scenario, "adaptive", seed=31)

    assert first == second


def test_market_scenario_measures_positive_trade_performance():
    scenario = next(item for item in load_scenarios() if item["name"] == "market_loop")

    result = run_scenario(scenario, "trader", seed=5)

    assert result["purchases"] > 0
    assert result["sales"] > 0
    assert result["profit_credits"] > 0
    assert result["profitable_sales"] == result["sales"]
    assert result["failed_actions"] == 0
    assert result["combat_success_rate"] is None


def test_suite_reports_archetype_comparison_and_survival():
    report = run_suite(load_scenarios(), ["explorer", "cautious", "adaptive"], runs=2, seed=9)

    assert set(report["summary"]) == {"explorer", "cautious", "adaptive"}
    assert all(row["episodes"] == 4 for row in report["summary"].values())
    assert all(0 <= row["survival_rate"] <= 1 for row in report["summary"].values())
    assert all(row["failed_actions"] == 0 for row in report["summary"].values())


def test_adaptive_archetype_uses_shield_state_and_cargo_state():
    from planner import Planner

    planner = Planner.__new__(Planner)
    planner.config = {"behavior_profile": "adaptive"}

    state = {"ship_info": {"shields": 20, "max_shields": 100, "cargo": []}}
    assert planner._behavior_profile(state) == "cautious"

    state["ship_info"].update({"shields": 90, "cargo": [{"commodity": "ORE", "quantity": 1}]})
    assert planner._behavior_profile(state) == "trader"

    state["ship_info"].update({"cargo": [{"commodity": "ORE", "quantity": "bad"}]})
    assert planner._behavior_profile(state) == "explorer"

    planner.config["behavior_profile"] = "merchant"
    assert planner._behavior_profile(state) == "trader"
    planner.config.update({"behavior_profile": "adaptive", "adaptive_caution_threshold": "invalid"})
    state["ship_info"].update({"shields": 20, "max_shields": 100})
    assert planner._behavior_profile(state) == "cautious"
