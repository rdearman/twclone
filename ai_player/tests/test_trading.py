import sys
import time
from pathlib import Path
from unittest.mock import MagicMock


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

from planner import Planner  # noqa: E402


def make_planner():
    planner = Planner.__new__(Planner)
    planner.state_manager = MagicMock()
    planner.config = {}
    planner._get_fuzzed_buy_quantity = lambda maximum: min(1, maximum)
    return planner


def market_state(remote_sell_price=None, local_buy_price=100):
    ports = {
        "1": {"port_id": 101, "commodities": [{"commodity": "ORE"}]},
    }
    prices = {"101": {"buy": {"ORE": local_buy_price}, "sell": {"ORE": None}}}
    if remote_sell_price is not None:
        ports["2"] = {"port_id": 202, "commodities": [{"commodity": "ORE"}]}
        prices["202"] = {"buy": {"ORE": None}, "sell": {"ORE": remote_sell_price}}
    for quote in prices.values():
        quote["quoted_at"] = {"ORE": time.time()}

    return {
        "player_location_sector": 1,
        "sector_data": {"1": {"has_port": True}},
        "port_info_by_sector": ports,
        "price_cache": prices,
        "ship_info": {"holds": 20, "cargo": []},
        "player_info": {"player": {"credits": 1000}},
        "valid_commodity_names": ["ORE"],
    }


def test_buy_decision_selects_commodity_with_known_positive_margin():
    planner = make_planner()
    state = market_state(remote_sell_price=150)

    assert planner._get_cheapest_commodity_to_buy(state) == "ORE"
    assert planner._can_buy(state) is True


def test_buy_decision_skips_commodity_without_known_buyer():
    planner = make_planner()
    state = market_state()

    assert planner._get_cheapest_commodity_to_buy(state) is None
    assert planner._can_buy(state) is False
    assert "trade.buy" not in planner._build_action_catalogue_by_stage(state, "exploit")
    assert planner._generate_required_field("items", "trade.buy", state) is None
    assert planner._achieve_goal(state, "buy: ORE") is None


def test_buy_decision_skips_known_unprofitable_route():
    planner = make_planner()
    state = market_state(remote_sell_price=90)

    assert planner._get_cheapest_commodity_to_buy(state) is None
    assert planner._can_buy(state) is False


def test_cargo_route_chooses_nearest_profitable_buyer():
    planner = make_planner()
    planner.state_manager.find_path.side_effect = lambda _start, target: {
        2: [1, 4, 2],
        3: [1, 3],
        4: [1, 4],
    }[target]
    state = {
        "player_location_sector": 1,
        "ship_info": {
            "cargo": [{"commodity": "ORE", "quantity": 5, "purchase_price": 100}],
        },
        "port_info_by_sector": {
            "2": {"port_id": 202},
            "3": {"port_id": 303},
            "4": {"port_id": 404},
        },
        "price_cache": {
            "202": {"sell": {"ORE": 150}, "quoted_at": {"ORE": time.time()}},
            "303": {"sell": {"ORE": 150}, "quoted_at": {"ORE": time.time()}},
            "404": {"sell": {"ORE": 90}, "quoted_at": {"ORE": time.time()}},
        },
    }

    assert planner._find_port_for_cargo(state) == (3, "ORE")


def test_cargo_route_prioritizes_profit_per_warp_over_largest_total_margin():
    planner = make_planner()
    planner.state_manager.find_path.side_effect = lambda _start, target: {
        2: [1, 2],
        3: [1, 4, 5, 3],
    }[target]
    state = {
        "player_location_sector": 1,
        "ship_info": {
            "cargo": [{"commodity": "ORE", "quantity": 10, "purchase_price": 100}],
        },
        "port_info_by_sector": {
            "2": {"port_id": 202},
            "3": {"port_id": 303},
        },
        "price_cache": {
            "202": {"sell": {"ORE": 130}, "quoted_at": {"ORE": time.time()}},
            "303": {"sell": {"ORE": 180}, "quoted_at": {"ORE": time.time()}},
        },
    }

    # Sector 3 has the larger total margin, but needs three times as many
    # warps; the nearby sale yields more profit per warp.
    assert planner._find_port_for_cargo(state) == (2, "ORE")


def test_server_route_recommendation_supplies_destination_and_path_cost():
    planner = make_planner()
    planner.state_manager.find_path.return_value = None
    state = {
        "player_location_sector": 1,
        "ship_info": {
            "cargo": [{"commodity": "ORE", "quantity": 10, "purchase_price": 100}],
        },
        "port_info_by_sector": {"2": {"port_id": 202}},
        "price_cache": {"202": {"sell": {"ORE": 150}, "quoted_at": {"ORE": time.time()}}},
        "market_recommendations": [{
            "commodity": "ORE", "a_to_b": True, "b_to_a": False,
            "sector_a_id": 1, "sector_b_id": 2,
            "port_a_id": 101, "port_b_id": 202,
            "hops_from_player": 1, "hops_between": 2,
            "estimated_profit_a_to_b": 50,
        }],
    }

    assert planner._find_port_for_cargo(state) == (2, "ORE")


def test_stale_quotes_are_not_used_for_purchases_or_routes():
    planner = make_planner()
    state = market_state(remote_sell_price=150)
    old = time.time() - 3600
    for quote in state["price_cache"].values():
        quote["quoted_at"]["ORE"] = old

    assert planner._get_cheapest_commodity_to_buy(state) is None
    assert planner._find_port_for_cargo({
        "player_location_sector": 1,
        "ship_info": {"cargo": [{"commodity": "ORE", "quantity": 5, "purchase_price": 100}]},
        "port_info_by_sector": {"2": {"port_id": 202}},
        "price_cache": {"202": {"sell": {"ORE": 150}, "quoted_at": {"ORE": old}}},
    }) is None
