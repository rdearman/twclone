"""Repeatable, offline scenarios that exercise the real AI planner selectors.

This is a policy harness, not a replacement game server: it models quoted
markets, graph movement, cargo, credits, and a small shield hazard surface.
"""

from __future__ import annotations

import argparse
import json
import logging
import random
import statistics
import time
from collections import deque
from pathlib import Path
from typing import Any

from planner import Planner


SCENARIO_DIR = Path(__file__).with_name("scenarios")


class ScenarioMap:
    def __init__(self, adjacency: dict[str, list[int]]):
        self.adjacency = {str(key): [int(item) for item in values] for key, values in adjacency.items()}

    def find_path(self, start: Any, target: Any) -> list[int] | None:
        try:
            source, destination = int(start), int(target)
        except (TypeError, ValueError):
            return None
        pending = deque([(source, [source])])
        visited = {source}
        while pending:
            sector, path = pending.popleft()
            if sector == destination:
                return path
            for adjacent in self.adjacency.get(str(sector), []):
                if adjacent not in visited:
                    visited.add(adjacent)
                    pending.append((adjacent, path + [adjacent]))
        return None

    @staticmethod
    def get(_key: str, default: Any = None) -> Any:
        return [] if default is None else default


def load_scenarios(directory: Path = SCENARIO_DIR) -> list[dict[str, Any]]:
    scenarios = []
    for path in sorted(directory.glob("*.json")):
        with path.open(encoding="utf-8") as scenario_file:
            scenario = json.load(scenario_file)
        scenario["_source"] = path.name
        scenarios.append(scenario)
    return scenarios


def _state_for(scenario: dict[str, Any], sector: int, credits: float, shields: float,
               cargo: list[dict[str, Any]], turn: int, recent: list[int]) -> dict[str, Any]:
    ports = {}
    prices = {}
    for sector_id, port in scenario.get("ports", {}).items():
        sector_key = str(sector_id)
        port_id = int(port["port_id"])
        ports[sector_key] = {
            "port_id": port_id,
            "commodities": [
                {"commodity": code, "quantity": 0, "max_quantity": 10_000}
                for code in port.get("commodities", {})
            ],
        }
        prices[str(port_id)] = {
            "buy": {code: data.get("buy") for code, data in port.get("commodities", {}).items()},
            "sell": {code: data.get("sell") for code, data in port.get("commodities", {}).items()},
            "quoted_at": {code: time.time() for code in port.get("commodities", {})},
        }
    adjacency = scenario["adjacency"]
    return {
        "player_location_sector": sector,
        "recent_sectors": recent[-5:],
        "sector_data": {str(sector): {"adjacent": adjacency.get(str(sector), [])}},
        "universe_map": {
            str(key): {"is_explored": int(key) in scenario.get("explored_sectors", [])}
            for key in adjacency
        },
        "port_info_by_sector": ports,
        "price_cache": prices,
        "ship_info": {
            "holds": int(scenario.get("holds", 10)),
            "cargo": cargo,
            "shields": shields,
            "max_shields": float(scenario.get("max_shields", 100)),
            "cargo_capacity": int(scenario.get("holds", 10)),
        },
        "ship_cargo_capacity": int(scenario.get("holds", 10)),
        "ship_current_cargo_volume": sum(int(item["quantity"]) for item in cargo),
        "player_info": {"player": {"credits": credits}},
        "port_trade_blacklist": [],
        "last_port_trade": None,
        "turn": turn,
    }


def run_scenario(scenario: dict[str, Any], profile: str, seed: int = 1) -> dict[str, Any]:
    """Run one deterministic bot episode and return comparable policy metrics."""
    rng = random.Random(seed)
    planner = Planner.__new__(Planner)
    planner.config = {"behavior_profile": profile, "quote_max_age_seconds": 3600}
    planner.state_manager = ScenarioMap(scenario["adjacency"])
    planner.current_stage = "exploit"
    planner._get_fuzzed_buy_quantity = lambda maximum: max(0, maximum)

    sector = int(scenario["start_sector"])
    credits = float(scenario.get("starting_credits", 1000))
    shields = float(scenario.get("max_shields", 100))
    cargo: list[dict[str, Any]] = []
    recent = [sector]
    visited = {sector}
    turns = int(scenario.get("turns", 40))
    total_profit = 0.0
    sales = purchases = hops = planned_hops = route_warps = failed_actions = 0
    profitable_sales = loss_sales = 0
    deaths = 0
    shield_damage = 0.0
    active_route_target = None

    for turn in range(turns):
        state = _state_for(scenario, sector, credits, shields, cargo, turn, recent)
        port = scenario.get("ports", {}).get(str(sector))
        port_id = int(port["port_id"]) if port else None
        bought_or_sold = False

        if cargo and port:
            commodity = planner._get_best_commodity_to_sell(state)
            if commodity:
                market = port["commodities"].get(commodity, {})
                sale_price = market.get("sell")
                item = next((entry for entry in cargo if entry["commodity"] == commodity), None)
                if item and sale_price is not None:
                    quantity = int(item["quantity"])
                    revenue = quantity * float(sale_price)
                    cost = quantity * float(item["purchase_price"])
                    credits += revenue
                    profit = revenue - cost
                    total_profit += profit
                    sales += 1
                    profitable_sales += profit > 0
                    loss_sales += profit < 0
                    cargo.remove(item)
                    active_route_target = None
                    state["last_port_trade"] = {"port_id": port_id, "commodity": commodity, "type": "sell"}
                    bought_or_sold = True

        if not cargo and port and not bought_or_sold:
            commodity = planner._get_cheapest_commodity_to_buy(state)
            market = port["commodities"].get(commodity, {}) if commodity else {}
            price = market.get("buy")
            available = int(scenario.get("holds", 10))
            quantity = min(available, int(credits // float(price))) if price and price > 0 else 0
            if commodity and quantity > 0:
                credits -= quantity * float(price)
                cargo.append({"commodity": commodity, "quantity": quantity, "purchase_price": float(price), "origin_port_id": port_id})
                purchases += 1
                bought_or_sold = True

        if bought_or_sold:
            continue

        # Carry cargo toward its best known profitable buyer. Otherwise use
        # the selected personality's normal exploration/port preference.
        target_sector = None
        route = planner._find_port_for_cargo(state) if cargo else None
        if route:
            target_sector = int(route[0])
            path = planner.state_manager.find_path(sector, target_sector)
            if path and active_route_target != target_sector:
                planned_hops += len(path) - 1
                active_route_target = target_sector
        else:
            active_route_target = None
        if target_sector is None:
            target_sector = planner._get_next_warp_target(state)
        if target_sector is None:
            failed_actions += 1
            break

        path = planner.state_manager.find_path(sector, target_sector)
        next_sector = path[1] if path and len(path) > 1 else int(target_sector)
        if next_sector not in scenario["adjacency"].get(str(sector), []):
            failed_actions += 1
            break

        hops += 1
        if route:
            route_warps += 1
        sector = next_sector
        if sector not in visited:
            visited.add(sector)
        recent.append(sector)
        recent = recent[-5:]
        hazard = scenario.get("hazards", {}).get(str(sector), 0)
        if isinstance(hazard, list):
            hazard = rng.randint(int(hazard[0]), int(hazard[1]))
        shield_damage += float(hazard)
        shields = max(0.0, shields - float(hazard))
        if shields <= 0:
            deaths += 1
            break

    completed = deaths == 0
    return {
        "scenario": scenario.get("name", "unnamed"),
        "profile": profile,
        "seed": seed,
        "turns_used": min(turns, turn + 1),
        "credits_final": round(credits, 2),
        "profit_credits": round(total_profit, 2),
        "purchases": purchases,
        "sales": sales,
        "profitable_sales": profitable_sales,
        "loss_sales": loss_sales,
        "survived": completed,
        "deaths": deaths,
        "shield_damage": round(shield_damage, 2),
        "warps": hops,
        "planned_route_hops": planned_hops,
        "route_hops_traveled": route_warps,
        "route_efficiency": round(route_warps / planned_hops, 3) if planned_hops else None,
        "failed_actions": failed_actions,
        "decision_quality": round(profitable_sales / sales, 3) if sales else None,
        "combat_attempts": 0,
        "combat_success_rate": None,
    }


def run_suite(scenarios: list[dict[str, Any]], profiles: list[str], runs: int = 1,
              seed: int = 1) -> dict[str, Any]:
    episodes = [
        run_scenario(scenario, profile, seed + run)
        for scenario in scenarios
        for profile in profiles
        for run in range(runs)
    ]
    summary = {}
    for profile in profiles:
        rows = [row for row in episodes if row["profile"] == profile]
        profits = [row["profit_credits"] for row in rows]
        survival = [row["survived"] for row in rows]
        qualities = [row["decision_quality"] for row in rows if row["decision_quality"] is not None]
        efficiencies = [row["route_efficiency"] for row in rows if row["route_efficiency"] is not None]
        summary[profile] = {
            "episodes": len(rows),
            "mean_profit_credits": round(statistics.mean(profits), 2) if profits else 0,
            "median_profit_credits": round(statistics.median(profits), 2) if profits else 0,
            "survival_rate": round(sum(survival) / len(survival), 3) if survival else 0,
            "mean_decision_quality": round(statistics.mean(qualities), 3) if qualities else None,
            "mean_route_efficiency": round(statistics.mean(efficiencies), 3) if efficiencies else None,
            "failed_actions": sum(row["failed_actions"] for row in rows),
            "combat_success_rate": None,
        }
    return {"episodes": episodes, "summary": summary}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runs", type=int, default=3, help="episodes per scenario/profile")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--profiles", nargs="+", default=["balanced", "trader", "explorer", "cautious", "adaptive"])
    parser.add_argument("--output", type=Path, help="write full report JSON to this path")
    args = parser.parse_args()
    logging.basicConfig(level=logging.WARNING)
    report = run_suite(load_scenarios(), args.profiles, max(1, args.runs), args.seed)
    rendered = json.dumps(report, indent=2, sort_keys=True)
    if args.output:
        args.output.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)


if __name__ == "__main__":
    main()
