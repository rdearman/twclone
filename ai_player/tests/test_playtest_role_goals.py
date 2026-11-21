from pathlib import Path
import sys
import time
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from planner import Planner
from main import (_canonicalize_strategy_goal, _role_action_inventory,
                  _role_goal_options, discard_protected_combat_goals,
                  discard_unavailable_role_goal_plan, is_goal_complete,
                  normalize_public_shipyard_search, normalize_shipyard_catalog,
                  reconcile_warp_readback, schema_commands_to_fetch)


def planner(config):
    state = SimpleNamespace(values={})
    state.get = lambda key, default=None: state.values.get(key, default)
    state.set = lambda key, value: state.values.__setitem__(key, value)
    return Planner(state, None, None, config)


def test_corporation_founder_creates_then_invites_configured_partner():
    p = planner({"allow_corporation_play": True, "corporation_win_condition": True,
                 "corporation_ai_players": 2, "corporation_name": "TWClone Playtest Founders",
                 "corporation_partner_username": "playbot09"})
    base = {"bank_balance": 1000, "server_commands": ["corp.create", "corp.invite", "player.list_online"]}
    assert p._achieve_goal({**base, "player_info": {"player": {"corp_id": 0}}}, "corp:found") == {
        "command": "corp.create", "data": {"name": "TWClone Playtest Founders"}}
    state = {**base, "player_info": {"player": {"corp_id": 7}},
             "online_players": [{"player_id": 12, "username": "playbot09", "corporation": None}],
             "online_players_refreshed_at": time.time()}
    assert p._achieve_goal(state, "corp:found") == {
        "command": "corp.invite", "data": {"target_player_id": 12}}


def test_corporation_creation_resolves_fee_from_player_bank_through_public_deposit():
    p = planner({"allow_corporation_play": True, "corporation_win_condition": True,
                 "corporation_ai_players": 2, "corporation_name": "TWClone Playtest Founders",
                 "corporation_creation_fee": 1000})
    state = {"server_commands": ["bank.deposit", "corp.create"],
             "player_info": {"player": {"corp_id": 0, "credits": "10000.00"}},
             "bank_balance": 0}
    assert p._achieve_goal(state, "corp:found") == {
        "command": "bank.deposit", "data": {"amount": 1000}}
    state["bank_balance"] = 1000
    assert p._achieve_goal(state, "corp:found") == {
        "command": "corp.create", "data": {"name": "TWClone Playtest Founders"}}


def test_corporate_operative_uses_public_corporation_list_and_join_contract():
    p = planner({"allow_corporation_play": True, "corporation_win_condition": True,
                 "corporation_ai_players": 2, "corporation_partner_name": "TWClone Playtest Founders"})
    state = {"server_commands": ["corp.list", "corp.join"],
             "player_info": {"player": {"corp_id": 0}}}
    assert p._achieve_goal(state, "corp:join") == {"command": "corp.list", "data": {}}
    state["corporations"] = [{"corp_id": 7, "name": "TWClone Playtest Founders"}]
    # A stale catalogue must be refreshed so newly created corporations and
    # valid invitations become visible to the cooperating player.
    assert p._achieve_goal(state, "corp:join") == {"command": "corp.list", "data": {}}
    state["corporations_refreshed_at"] = time.time()
    assert p._achieve_goal(state, "corp:join") == {
        "command": "corp.join", "data": {"corp_id": 7}}


def test_corporation_goals_remain_disabled_outside_opted_in_pair():
    p = planner({"allow_corporation_play": False})
    assert p._achieve_goal({"player_info": {"player": {"corp_id": 0}}}, "corp:found") is None


def test_coloniser_uses_catalogue_before_buying_genesis_hardware():
    p = planner({"enable_planet_creation": True})
    state = {
        "player_location_sector": 4,
        "player_info": {"player": {"corp_id": 0, "credits": "10000"}},
        "ship_info": {"genesis_torpedoes": 0, "onplanet": False},
        "sector_data": {"4": {"has_port": True, "has_planet": False}},
        "server_commands": ["hardware.list", "hardware.buy", "planet.genesis_create"],
    }
    assert p._achieve_goal(state, "hardware:genesis") == {
        "command": "hardware.list", "data": {}}
    state["hardware_catalog_checked_sectors"] = ["4"]
    state["hardware_catalog_by_sector"] = {
        "4": {"items": [{"code": "GENESIS", "price": 500,
                           "stock_quantity": 3, "max_purchase": 3}]}}
    assert p._achieve_goal(state, "hardware:genesis") == {
        "command": "hardware.buy", "data": {"code": "GENESIS", "quantity": 1}}


def test_coloniser_does_not_buy_genesis_without_affordability_or_available_stock():
    p = planner({"enable_planet_creation": True})
    state = {
        "player_location_sector": 4,
        "player_info": {"player": {"corp_id": 0, "credits": "100"}},
        "ship_info": {"genesis_torpedoes": 0, "onplanet": False},
        "sector_data": {"4": {"has_port": True, "has_planet": False}},
        "server_commands": ["hardware.list", "hardware.buy", "planet.genesis_create"],
        "hardware_catalog_checked_sectors": ["4"],
        "hardware_catalog_by_sector": {"4": {"items": [
            {"code": "GENESIS", "price": 500, "stock_quantity": 3,
             "max_purchase": 3},
        ]}},
    }
    assert p._achieve_goal(state, "hardware:genesis") is None
    state["player_info"]["player"]["credits"] = "10000"
    state["hardware_catalog_by_sector"]["4"]["items"][0]["stock_quantity"] = 0
    assert p._achieve_goal(state, "hardware:genesis") is None


def test_coloniser_escape_pod_recovery_selects_genesis_capable_hull():
    p = planner({"role_name": "Coloniser"})
    state = {
        "player_location_sector": 440,
        "player_info": {"player": {"credits": "1000000"}},
        "ship_info": {"id": 81, "type_id": 23, "name": "Escape Pod",
                       "holds": 0, "ported": 190},
        "shipyard_catalog": {"sector_id": 440, "is_shipyard": True, "available": [
            {"type_id": 2, "type": "Scout Marauder", "eligible": True,
             "net_cost": 15950, "max_genesis": 0, "max_holds": 25},
            {"type_id": 6, "type": "Colonial Transport", "eligible": True,
             "net_cost": 45000, "max_genesis": 5, "max_holds": 250},
        ]},
        "server_commands": ["shipyard.upgrade"],
    }
    assert p._achieve_goal(state, "shipyard: recover") == {
        "command": "shipyard.upgrade",
        "data": {"new_type_id": 6, "new_ship_name": "Playtest Recovery"},
    }


def test_coloniser_with_incompatible_hull_is_offered_shipyard_recovery():
    state = {
        "player_location_sector": 228,
        "player_info": {"player": {"corp_id": 0}},
        "ship_info": {"id": 81, "type_id": 2, "holds": 10,
                       "max_holds": 25, "max_genesis": 0},
        "server_commands": ["shipyard.list", "shipyard.upgrade", "hardware.list"],
    }
    assert _role_goal_options("Coloniser", state, {}, {}) == ["shipyard: recover"]


def test_escape_pod_coloniser_launches_and_prioritizes_public_shipyard_recovery():
    p = planner({})
    game = {
        "player_location_sector": 62,
        "player_info": {"player": {"turns_remaining": 900, "credits": "10000000"}},
        "ship_info": {"id": 56, "type_id": 23, "name": "Escape Pod", "holds": 0,
                       "onplanet": 1, "genesis": 0},
        "sector_data": {"62": {"has_planet": True}},
        "server_commands": ["planet.launch", "move.warp", "shipyard.list", "shipyard.upgrade"],
    }
    assert _role_goal_options("Coloniser", game, {}, {}) == ["shipyard: recover"]
    assert p._achieve_goal(game, "shipyard: recover") == {"command": "planet.launch", "data": {}}


def test_escape_pod_recovery_uses_only_a_publicly_observed_stardock():
    class MapState:
        def get(self, _key, default=None):
            return default
        def find_path(self, _start, _target):
            return []

    p = Planner(MapState(), None, None, {})
    state = {
        "player_location_sector": 62,
        "ship_info": {"id": 56, "type_id": 23, "holds": 0, "onplanet": 0},
        "sector_data": {
            "62": {"sector_id": 62, "adjacent": [14]},
            "440": {"sector_id": 440, "ports": [{"name": "Stardock", "port_id": 190}]},
        },
        "server_commands": ["move.warp", "move.pathfind", "shipyard.list", "shipyard.upgrade"],
    }
    assert p._achieve_goal(state, "shipyard: recover") == {
        "command": "move.pathfind", "data": {"from_sector_id": 62, "to_sector_id": 440}}


def test_escape_pod_recovery_uses_documented_public_search_before_exploration():
    p = planner({})
    state = {
        "player_location_sector": 62,
        "ship_info": {"id": 56, "type_id": 23, "holds": 0, "onplanet": 0},
        "sector_data": {"62": {"sector_id": 62, "adjacent": [14]}},
        "shipyard_search_complete": False,
        "server_commands": ["sector.search", "move.warp", "shipyard.list", "shipyard.upgrade"],
    }
    assert p._achieve_goal(state, "shipyard: recover") == {
        "command": "sector.search", "data": {"q": "Stardock", "type": "port", "limit": 20}}
    state["shipyard_search_complete"] = True
    assert p._achieve_goal(state, "shipyard: recover")["command"] == "move.warp"


def test_public_shipyard_search_results_are_scoped_to_stardock_port_matches():
    assert normalize_public_shipyard_search("sector.search_results_v1", {
        "items": [
            {"kind": "port", "name": "Stardock", "sector_id": 440},
            {"kind": "sector", "name": "Stardock", "sector_id": 441},
            {"kind": "port", "name": "Trading Post", "sector_id": 442},
            {"kind": "port", "name": "stardock", "sector_id": 443},
            {"kind": "port", "name": "Stardock"},
        ]
    }) == [
        {"kind": "port", "name": "Stardock", "sector_id": 440},
        {"kind": "port", "name": "stardock", "sector_id": 443},
    ]
    assert normalize_public_shipyard_search("unexpected", {"items": [
        {"kind": "port", "name": "Stardock", "sector_id": 440}
    ]}) == []


def test_escape_pod_recovery_docks_then_reads_the_v3_shipyard_catalogue():
    p = planner({})
    state = {
        "player_location_sector": 440,
        "ship_info": {"id": 56, "type_id": 23, "holds": 0, "onplanet": 0, "ported": 0},
        "sector_data": {"440": {"sector_id": 440, "ports": [{"name": "Stardock"}]}},
        "server_commands": ["dock.status", "shipyard.list", "shipyard.upgrade"],
    }
    assert p._achieve_goal(state, "shipyard: recover") == {
        "command": "dock.status", "data": {"action": "dock"}}
    state["ship_info"]["ported"] = 190
    assert p._achieve_goal(state, "shipyard: recover") == {"command": "shipyard.list", "data": {}}


def test_escape_pod_recovery_selects_an_affordable_eligible_hull_from_v3_available_field():
    p = planner({})
    state = {
        "player_location_sector": 440,
        "player_info": {"player": {"credits": "1000000"}},
        "ship_info": {"id": 56, "type_id": 23, "name": "Escape Pod", "holds": 0, "ported": 190},
        "sector_data": {"440": {"sector_id": 440, "ports": [{"name": "Stardock"}]}},
        "shipyard_catalog": {
            "sector_id": 440,
            "available": [
                {"type_id": 23, "type": "Escape Pod", "eligible": True, "net_cost": 0},
                {"type_id": 2, "type": "Scout", "eligible": False, "net_cost": 500},
                {"type_id": 7, "type": "Merchant", "eligible": True, "net_cost": 25000},
            ],
        },
        "server_commands": ["dock.status", "shipyard.list", "shipyard.upgrade"],
    }
    assert p._achieve_goal(state, "shipyard: recover") == {
        "command": "shipyard.upgrade",
        "data": {"new_type_id": 7, "new_ship_name": "Playtest Recovery"},
    }
    assert not is_goal_complete("shipyard: recover", {
        "ship_info": {"holds": 20, "max_genesis": 0},
        "ship_recovery_requirements": {"max_genesis": 1, "max_holds": 25},
    }, {"ship_id": 56, "new_type_id": 7}, "shipyard.upgraded_v1")


def test_shipyard_recovery_requires_authoritative_role_capacities_after_purchase():
    requirements = {"max_genesis": 1, "max_holds": 25}
    assert not is_goal_complete("shipyard: recover", {
        "ship_info": {"id": 56, "holds": 0, "max_genesis": 0, "max_holds": 0},
        "ship_recovery_requirements": requirements,
    }, {"ship_id": 56, "new_type_id": 6}, "shipyard.upgraded_v1")
    assert not is_goal_complete("shipyard: recover", {
        "ship_info": {"id": 56, "holds": 10, "max_genesis": 0, "max_holds": 25},
        "ship_recovery_requirements": requirements,
    }, {"ship_id": 56}, "shipyard.upgraded_v1")
    assert is_goal_complete("shipyard: recover", {
        "ship_info": {"id": 56, "holds": 25, "max_genesis": 5, "max_holds": 250},
        "ship_recovery_requirements": requirements,
    }, {}, "ship.status")


def test_coloniser_incapable_hull_remains_in_recovery_goal():
    state = {
        "ship_info": {"id": 81, "type_id": 2, "name": "Scout Marauder", "holds": 10,
                      "max_holds": 25, "max_genesis": 0},
        "ship_recovery_requirements": {"max_genesis": 1, "max_holds": 25},
        "server_commands": ["shipyard.list", "shipyard.upgrade"],
    }
    assert Planner._needs_ship_recovery(state)
    assert not is_goal_complete("shipyard: recover", state, {}, "state.snapshot")


def test_coloniser_refreshes_stale_shipyard_catalogue_without_capacities():
    p = planner({"role_name": "Coloniser"})
    state = {
        "player_location_sector": 440,
        "ship_info": {"id": 81, "type_id": 2, "holds": 10, "ported": 190,
                      "max_holds": 25, "max_genesis": 0},
        "ship_recovery_requirements": {"max_genesis": 1, "max_holds": 25},
        "sector_data": {"440": {"sector_id": 440, "ports": [{"name": "Stardock"}]}},
        "shipyard_catalog": {"sector_id": 440, "_refreshed_at": 0, "available": [
            {"type_id": 2, "eligible": True, "net_cost": 15950},
            {"type_id": 6, "eligible": True, "net_cost": 63600},
        ]},
        "server_commands": ["shipyard.list", "shipyard.upgrade"],
    }
    assert p._needs_ship_recovery(state)
    assert p._achieve_goal(state, "shipyard: recover") == {"command": "shipyard.list", "data": {}}


def test_coloniser_blocks_when_fresh_shipyard_catalogue_omits_required_capacities():
    p = planner({"role_name": "Coloniser"})
    state = {
        "player_location_sector": 440,
        "ship_info": {"id": 81, "type_id": 2, "holds": 10, "ported": 190,
                      "max_holds": 25, "max_genesis": 0},
        "ship_recovery_requirements": {"max_genesis": 1, "max_holds": 25},
        "sector_data": {"440": {"sector_id": 440, "ports": [{"name": "Stardock"}]}},
        "shipyard_catalog": {"sector_id": 440, "_refreshed_at": time.time(), "available": [
            {"type_id": 6, "eligible": True, "net_cost": 63600},
        ]},
        "server_commands": ["shipyard.list", "shipyard.upgrade"],
    }
    assert p._needs_ship_recovery(state)
    assert p._achieve_goal(state, "shipyard: recover") is None


def test_genesis_search_skips_checked_ports_and_selects_untried_stardock():
    class RouteState:
        def get(self, _key, default=None):
            return default
        def find_path(self, start, end):
            return [int(start), int(end)]

    p = Planner(RouteState(), None, None, {"role_name": "Coloniser"})
    state = {
        "player_location_sector": 409,
        "ship_info": {"holds": 10, "genesis": 0, "max_genesis": 2},
        "sector_data": {
            "408": {"sector_id": 408, "has_port": True},
            "409": {"sector_id": 409, "adjacent": [{"to_sector": 408}, {"to_sector": 440}]},
            "440": {"sector_id": 440, "has_port": True},
        },
        "port_info_by_sector": {
            "408": {"name": "Aseagan", "sector_id": 408},
            "440": {"name": "Stardock", "sector_id": 440},
        },
        "hardware_catalog_checked_sectors": ["408", "409"],
        "hardware_catalog_by_sector": {"408": {"items": [{"code": "FIGHTERS"}]}},
        "server_commands": ["move.warp", "hardware.list", "hardware.buy"],
    }
    assert p._achieve_goal(state, "hardware: genesis") == {
        "command": "move.warp", "data": {"to_sector_id": 440},
    }


def test_genesis_search_prioritizes_untried_stardock_over_nearby_trade_ports():
    class RouteState:
        def get(self, _key, default=None):
            return default
        def find_path(self, start, end):
            return [int(start), int(end)]

    p = Planner(RouteState(), None, None, {"role_name": "Coloniser"})
    state = {
        "player_location_sector": 409,
        "ship_info": {"holds": 10, "genesis": 0, "max_genesis": 2, "ported": 0},
        "sector_data": {"409": {"sector_id": 409, "has_port": True,
                                   "adjacent": [{"to_sector": 440}],
                                   "ports": [{"name": "Fertoretue", "port_id": 155}]},
                         "440": {"sector_id": 440, "has_port": True,
                                   "ports": [{"name": "Stardock", "port_id": 190}]}},
        "port_info_by_sector": {"409": {"name": "Fertoretue", "sector_id": 409}},
        "hardware_catalog_checked_sectors": [],
        "server_commands": ["dock.status", "hardware.list", "hardware.buy", "move.warp"],
    }
    assert p._achieve_goal(state, "hardware: genesis") == {
        "command": "move.warp", "data": {"to_sector_id": 440},
    }


def test_role_hardware_goal_is_not_preempted_by_opportunistic_cargo_sale():
    class State:
        def __init__(self, values):
            self.values = values
        def get(self, key, default=None):
            return self.values.get(key, default)
        def get_all(self):
            return self.values
        def set(self, key, value):
            self.values[key] = value
        def find_path(self, start, end):
            return [int(start), int(end)]

    values = {
        "player_location_sector": 408,
        "player_info": {"player": {"credits": "100000", "turns_remaining": 100}},
        "ship_info": {"id": 81, "holds": 10, "max_holds": 65,
                      "genesis": 0, "max_genesis": 2,
                      "ported": 120, "cargo": [{"commodity": "EQU", "quantity": 1}]},
        "sector_data": {"408": {"sector_id": 408, "has_port": True,
                                  "ports": [{"port_id": 120, "name": "Aseagan"}]},
                         "440": {"sector_id": 440, "has_port": True,
                                  "ports": [{"port_id": 190, "name": "Stardock"}]}},
        "port_info_by_sector": {"408": {"port_id": 120, "name": "Aseagan",
                                           "commodities": [{"commodity": "EQU", "buys": True,
                                                            "sell_price": 225}]}},
        "hardware_catalog_checked_sectors": ["408"],
        "hardware_catalog_by_sector": {"408": {"items": [{"code": "FIGHTERS"}]}},
        "server_commands": ["move.warp", "trade.sell", "hardware.list", "hardware.buy"],
        "price_cache": {"120": {"buy": {}, "sell": {"EQU": 225}}},
        "last_command_result": {"command": "trade.quote", "status": "ok"},
    }
    manager = State(values)
    p = Planner(manager, None, None, {
        "playtest_bot": "BOT03", "role_name": "Coloniser",
        "turn_reserve_priority_prefixes": ["shipyard:", "planet:"],
    })
    action = p.get_next_command(values, "goto: 408")
    assert action is not None
    assert action["command"] != "trade.sell"
    assert manager.get("strategy_plan") == ["hardware: genesis"]


def test_recovery_waits_for_authoritative_ship_refresh_after_upgrade():
    p = planner({})
    state = {
        "player_location_sector": 440,
        "ship_info": {"id": 56, "type_id": 2, "holds": 0, "ported": 190},
        "shipyard_recovery_pending_refresh": True,
        "server_commands": ["ship.info", "shipyard.upgrade"],
    }
    assert _role_goal_options("Coloniser", state, {}, {}) == ["shipyard: recover"]
    assert p._achieve_goal(state, "shipyard: recover") == {"command": "ship.info", "data": {}}


def test_ship_recovery_preempts_stale_objective_for_every_playtest_role():
    state = {
        "ship_info": {"id": 82, "name": "Escape Pod", "holds": 0},
        "server_commands": ["shipyard.list", "shipyard.upgrade"],
    }
    for role in ("Trader", "Merchant", "Coloniser", "Industrialist", "Explorer",
                 "Pirate", "Bounty hunter", "Corporation founder", "Corporate operative",
                 "Opportunist"):
        assert _role_goal_options(role, state, {}, {}) == ["shipyard: recover"]


def test_ship_recovery_preempts_a_persisted_combat_goal_and_uses_affordable_public_hull():
    p = planner({})
    state = {
        "player_location_sector": 440,
        "player_info": {"player": {"credits": "2000.00"}},
        "ship_info": {"id": 82, "name": "Escape Pod", "type_id": 23,
                      "holds": 0, "onplanet": False, "ported": 12},
        "sector_data": {"440": {"sector_id": 440, "ports": [{"name": "Stardock"}]}},
        "shipyard_catalog": {"sector_id": 440, "available": [
            {"type_id": 23, "type": "Escape Pod", "eligible": True, "net_cost": 0},
            {"type_id": 2, "type": "Starter Hull", "eligible": True,
             "net_cost": 1500, "initialholds": 10},
            {"type_id": 7, "type": "Large Hull", "eligible": True,
             "net_cost": 3000, "initialholds": 30},
        ]},
        "server_commands": ["shipyard.list", "shipyard.upgrade", "combat.attack"],
    }
    assert p.get_next_command(state, "combat: attack") == {
        "command": "shipyard.upgrade",
        "data": {"new_type_id": 2, "new_ship_name": "Playtest Recovery"},
    }


def test_planet_development_uses_terra_only_for_colonist_pickup_and_checks_capacity():
    p = planner({})
    state = {
        "player_location_sector": 1,
        "player_info": {"player": {"player_id": 44}},
        "ship_info": {"id": 82, "onplanet": True, "planet_id": 1,
                      "colonists": 0, "holds": 10},
        "planet_info": {"planet_id": 1, "owner_type": "federation", "owner_id": 0},
        "owned_planets": [{"planet_id": 90, "sector_id": 62,
                            "owner_type": "player", "owner_id": 44}],
        "server_commands": ["planet.colonists.get", "planet.colonists.set", "planet.launch"],
    }
    assert p._achieve_goal(state, "planet: develop") == {
        "command": "planet.colonists.get", "data": {"planet_id": 1}}
    state["planet_colonist_state"] = {
        "planet_id": 1, "planet_colonists": 25, "ship_holds_available": 7,
    }
    assert p._achieve_goal(state, "planet: develop") == {
        "command": "planet.colonists.set",
        "data": {"planet_id": 1, "action": "pickup", "quantity": 7},
    }


def test_planet_development_drops_population_on_owned_world_and_never_claims_landed_unowned_world():
    p = planner({})
    owned = {
        "player_location_sector": 62,
        "player_info": {"player": {"player_id": 44}},
        "ship_info": {"id": 82, "onplanet": True, "planet_id": 90,
                      "colonists": 6, "holds": 10},
        "planet_info": {"planet_id": 90, "owner_type": "player", "owner_id": 44,
                        "sector_id": 62, "colonists_unassigned": 0},
        "owned_planets": [{"planet_id": 90, "sector_id": 62}],
        "server_commands": ["planet.colonists.set", "planet.launch"],
    }
    assert p._achieve_goal(owned, "planet: develop") == {
        "command": "planet.colonists.set",
        "data": {"planet_id": 90, "action": "dropoff", "quantity": 6},
    }
    unowned = {**owned, "planet_info": {"planet_id": 90, "owner_type": "", "owner_id": 0},
               "owned_planets": []}
    assert p._achieve_goal(unowned, "planet: develop") == {
        "command": "planet.launch", "data": {}}


def test_genesis_creation_goal_requires_torpedo_and_non_fedspace_empty_sector():
    p = planner({})
    state = {
        "player_location_sector": 62,
        "ship_info": {"id": 82, "genesis_torpedoes": 0},
        "sector_data": {"62": {"has_planet": False}},
        "server_commands": ["planet.genesis_create", "hardware.list", "hardware.buy"],
    }
    assert p._achieve_goal(state, "planet: create") is None
    state["ship_info"]["genesis_torpedoes"] = 1
    state["player_location_sector"] = 5
    assert p._achieve_goal(state, "planet: create") is None


def test_genesis_creation_routes_from_occupied_sector_to_known_empty_sector():
    p = planner({})
    p.state_manager.find_path = lambda start, end: [int(start), int(end)]
    state = {
        "player_location_sector": 156,
        "ship_info": {"id": 63, "genesis_torpedoes": 1},
        "sector_data": {
            "156": {"is_msl": False, "has_planet": True,
                    "adjacent": [{"to_sector": 288}]},
            "288": {"is_msl": False, "has_planet": False},
        },
        "server_commands": ["planet.genesis_create", "move.warp", "move.pathfind"],
    }

    assert p._achieve_goal(state, "planet: create") == {
        "command": "move.warp", "data": {"to_sector_id": 288},
    }


def test_genesis_creation_refreshes_unknown_adjacent_planet_presence():
    p = planner({})
    state = {
        "player_location_sector": 156,
        "ship_info": {"id": 63, "genesis_torpedoes": 1},
        "sector_data": {
            "156": {"is_msl": False, "has_planet": True,
                    "adjacent": [{"to_sector": 288}]},
            "288": {"is_msl": False},
        },
        "server_commands": ["planet.genesis_create", "sector.info"],
    }

    assert p._achieve_goal(state, "planet: create") == {
        "command": "sector.info", "data": {"sector_id": 288},
    }


def test_v3_shipyard_list_response_normalizes_available_catalog_without_fleet_side_effects():
    catalog = normalize_shipyard_catalog("shipyard.list_v1", {
        "sector_id": 440, "is_shipyard": True,
        "available": [{"type_id": 7, "eligible": True}],
        "used_available": [{"used_inventory_id": 4}],
    }, refreshed_at=123.0)
    assert catalog == {
        "sector_id": 440, "is_shipyard": True,
        "available": [{"type_id": 7, "eligible": True}],
        "used_available": [{"used_inventory_id": 4}],
        "_refreshed_at": 123.0,
    }
    assert normalize_shipyard_catalog("shipyard.list_v1", {"ships": "invalid"})["available"] == []
    assert normalize_shipyard_catalog("shipyard.list", {"ships": [{"type_id": 2}]})["available"] == [{"type_id": 2}]
    assert normalize_shipyard_catalog("shipyard.upgraded_v1", {}) is None


def test_persisted_planet_goal_is_replaced_after_authoritative_ship_state_changes():
    assert discard_unavailable_role_goal_plan(
        ["planet: develop", "goto: 62"], ["shipyard: recover"]
    ) == []
    assert discard_unavailable_role_goal_plan(
        ["shipyard: recover", "goto: 440"], ["shipyard: recover"]
    ) == ["shipyard: recover", "goto: 440"]
    assert discard_unavailable_role_goal_plan(
        ["goto: 440", "planet: develop"], ["shipyard: recover"]
    ) == ["goto: 440", "planet: develop"]


def test_ship_recovery_actions_fetch_public_schemas_before_execution():
    missing = schema_commands_to_fetch(
        ["planet.launch", "dock.status", "shipyard.list", "shipyard.upgrade"],
        cached_schemas={},
    )
    assert "planet.launch" in missing
    assert "dock.status" in missing
    assert "shipyard.list" in missing
    assert "shipyard.upgrade" in missing


def test_bounty_role_maps_adjacent_observed_target_to_explicit_photon_protocol():
    p = planner({})
    state = {"ship_info": {"photons": 1},
             "server_commands": ["combat.fire_photon"],
             "adjacent_ship_targets": [{"ship_id": 81, "sector_id": 23, "hostile": True}]}
    assert p._achieve_goal(state, "combat:photon") == {
        "command": "combat.fire_photon",
        "data": {"sector_id": 23, "target_ship_id": 81},
    }
    state["ship_info"]["photons"] = 0
    assert p._achieve_goal(state, "combat:photon") is None


def test_bounty_role_hides_combat_until_fighters_and_shields_are_available():
    state = {
        "player_location_sector": 399,
        "ship_info": {"fighters": 0, "shields": 0,
                      "max_fighters": 250, "max_shields": 100},
        "server_commands": ["combat.attack", "sector.scan.density"],
    }
    filtered = {"visible_ship_targets": [{"ship_id": 115, "hostile": True}]}
    assert _role_goal_options("Bounty hunter", state, filtered, {}) == []
    state["ship_info"].update({"fighters": 10, "shields": 5})
    assert _role_goal_options("Bounty hunter", state, filtered, {}) == ["combat: attack"]


def test_playtest_navigation_goal_is_not_replaced_by_local_trade_or_exploration():
    assert planner({"playtest_bot": "BOT07"})._preserves_playtest_goal("goto: 177")
    assert planner({"playtest_bot": "BOT07"})._preserves_playtest_goal("combat: attack")
    assert not planner({})._preserves_playtest_goal("goto: 177")


def test_combat_role_purchases_missing_equipment_before_resuming_combat():
    p = planner({"role_name": "Bounty hunter", "combat_min_fighters": 10,
                 "combat_min_shields": 5})
    state = {
        "player_location_sector": 409,
        "sector_data": {"409": {"has_port": True}},
        "ship_info": {"hull": 100, "fighters": 0, "shields": 0,
                      "max_fighters": 250, "max_shields": 100},
        "player_info": {"player": {"credits": "10000"}},
        "server_commands": ["hardware.list", "hardware.buy"],
        "hardware_catalog_checked_sectors": [],
    }
    assert p._combat_readiness_command(state) == {"command": "hardware.list", "data": {}}
    state["hardware_catalog_checked_sectors"] = ["409"]
    state["hardware_catalog_by_sector"] = {"409": {"items": [
        {"code": "FIGHTERS", "price": 100, "stock_quantity": 10},
        {"code": "SHIELDS", "price": 100, "stock_quantity": 5},
    ]}}
    assert p._combat_readiness_command(state) == {
        "command": "hardware.buy", "data": {"code": "FIGHTERS", "quantity": 10}}
    state["ship_info"]["fighters"] = 10
    assert p._combat_readiness_command(state) == {
        "command": "hardware.buy", "data": {"code": "SHIELDS", "quantity": 5}}


def test_class0_catalog_routes_combat_role_to_known_stardock_instead_of_retrying_buy():
    p = planner({"role_name": "Bounty hunter", "combat_min_fighters": 10,
                 "combat_min_shields": 5})
    p.state_manager.find_path = lambda start, end: [int(start), int(end)]
    state = {
        "player_location_sector": 104,
        "sector_data": {"104": {"has_port": True, "adjacent": [440]}},
        "ship_info": {"hull": 100, "fighters": 0, "shields": 0,
                      "max_fighters": 250, "max_shields": 100},
        "server_commands": ["hardware.list", "hardware.buy", "move.warp"],
        "hardware_catalog_checked_sectors": ["104"],
        "hardware_catalog_by_sector": {"104": {
            "location_type": "CLASS0",
            "items": [{"code": "FIGHTERS", "price": 100}],
        }},
        "shipyard_search_results": [{"name": "Stardock", "sector_id": 440}],
    }
    assert p._combat_readiness_command(state) == {
        "command": "move.warp", "data": {"to_sector_id": 440}}


def test_unavailable_colonisation_prerequisite_is_reported_blocked_without_trading():
    class State:
        def __init__(self):
            self.values = {}
        def get(self, key, default=None):
            return self.values.get(key, default)
        def set(self, key, value):
            self.values[key] = value

    state_manager = State()
    p = Planner(state_manager, None, None, {
        "playtest_bot": "BOT03", "role_name": "Coloniser",
        "turn_reserve_priority_prefixes": ["planet:"],
    })
    game = {
        "player_location_sector": 4,
        "player_info": {"player": {"ship_id": 8, "turns_remaining": 0}},
        "ship_info": {"id": 8, "onplanet": False, "holds": 25, "max_holds": 25,
                       "max_genesis": 1, "genesis_torpedoes": 0},
        "sector_data": {"4": {"adjacent": [5], "has_port": False,
                                "has_planet": False, "_last_refreshed": 9999999999}},
        "server_commands": ["planet.genesis_create", "trade.buy", "move.warp"],
        "known_ports": {}, "port_info_by_sector": {},
    }
    assert p.get_next_command(game, "planet: develop") is None
    assert state_manager.values["blocked_objective"]["goal"] == "hardware: genesis"
    assert state_manager.values["blocked_objective"]["reason"] == (
        "turn budget exhausted before reaching a port that offers Genesis hardware"
    )


def test_genesis_creation_fails_closed_until_authoritative_msl_status_is_known():
    p = planner({"playtest_bot": "BOT03", "role_name": "Coloniser"})
    state = {
        "player_location_sector": 177,
        "player_info": {"player": {"player_id": 108, "ship_id": 81,
                                     "sector": 177, "turns_remaining": 100}},
        "ship_info": {"id": 81, "holds": 65, "max_holds": 65,
                      "max_genesis": 2, "genesis_torpedoes": 2},
        "sector_data": {"177": {"has_port": False, "has_planet": False,
                                  "adjacent": [178], "_last_refreshed": time.time()}},
        "server_commands": ["planet.genesis_create", "sector.info"],
    }
    assert p._achieve_goal(state, "planet: create") == {
        "command": "sector.info", "data": {"sector_id": 177}
    }


def test_genesis_creation_routes_away_from_authoritatively_confirmed_msl():
    p = planner({"playtest_bot": "BOT03", "role_name": "Coloniser"})
    p.state_manager.find_path = lambda start, end: [int(start), int(end)]
    state = {
        "player_location_sector": 177,
        "player_info": {"player": {"player_id": 108, "ship_id": 81,
                                     "sector": 177, "turns_remaining": 100}},
        "ship_info": {"id": 81, "holds": 65, "max_holds": 65,
                      "max_genesis": 2, "genesis_torpedoes": 2},
        "sector_data": {
            "177": {"is_msl": True, "has_port": False, "has_planet": False,
                    "adjacent": [178], "_last_refreshed": time.time()},
            "178": {"is_msl": False, "has_planet": False},
        },
        "server_commands": ["planet.genesis_create", "move.pathfind", "move.warp"],
    }
    action = p._achieve_goal(state, "planet: create")
    assert action is not None
    assert action["command"] in {"move.pathfind", "move.warp"}
    assert action["data"].get("to_sector_id", action["data"].get("to_sector")) == 178


def test_coloniser_does_not_offer_genesis_create_for_unknown_or_msl_sector():
    for sector in ({"has_planet": False}, {"is_msl": True, "has_planet": False}):
        goals = _role_goal_options("Coloniser", {
            "player_location_sector": 177,
            "ship_info": {"holds": 65, "genesis_torpedoes": 1, "max_genesis": 2},
            "sector_data": {"177": sector},
            "server_commands": ["planet.genesis_create"],
        }, {}, {})
        assert "planet: create" not in goals


def test_empty_owned_planet_routes_to_terra_before_landing_again():
    p = planner({"playtest_bot": "BOT03", "role_name": "Coloniser"})
    p.state_manager.find_path = lambda start, end: [int(start), int(end)]
    state = {
        "player_location_sector": 156,
        "player_info": {"player": {"player_id": 108, "ship_id": 81,
                                     "sector": 156, "turns_remaining": 50}},
        "ship_info": {"id": 81, "onplanet": False, "holds": 65,
                      "colonists": 0, "genesis_torpedoes": 1, "max_genesis": 2},
        "owned_planets": [{"planet_id": 151, "sector_id": 156,
                            "owner_type": "player", "owner_id": 108}],
        "sector_data": {"156": {"is_msl": False, "has_port": True,
                                  "adjacent": [177], "_last_refreshed": time.time()}},
        "server_commands": ["planet.land", "move.pathfind", "move.warp"],
    }
    assert p._achieve_goal(state, "planet: develop") == {
        "command": "planet.info", "data": {"planet_id": 151}
    }
    state["planet_info"] = {"planet_id": 151, "population": 0, "colonists_unassigned": 0}
    action = p._achieve_goal(state, "planet: develop")
    assert action["command"] == "move.pathfind"
    assert action["data"]["to_sector_id"] == 1


def test_populated_owned_planet_is_developed_instead_of_returning_to_terra():
    p = planner({"playtest_bot": "BOT03", "role_name": "Coloniser"})
    state = {
        "player_location_sector": 156,
        "ship_info": {"id": 81, "onplanet": False, "holds": 65, "colonists": 0},
        "owned_planets": [{"planet_id": 151, "sector_id": 156,
                            "owner_type": "player", "owner_id": 108}],
        "planet_info": {"planet_id": 151, "population": 29, "colonists_unassigned": 29},
        "sector_data": {"156": {"has_port": False, "has_planet": True}},
        "server_commands": ["planet.land"],
    }
    assert p._achieve_goal(state, "planet: develop") == {
        "command": "planet.land", "data": {"planet_id": 151}
    }


def test_server_route_string_sector_ids_are_normalized_before_adjacency_check():
    p = planner({})
    p.state_manager.find_path = lambda start, end: [str(start), "409", str(end)]
    state = {
        "player_location_sector": "75",
        "sector_data": {"75": {"adjacent": [{"to_sector": "409"}]}},
        "server_commands": ["move.warp", "move.pathfind"],
        "command_retry_info": {},
    }
    assert p._achieve_goal(state, "goto: 156") == {
        "command": "move.warp", "data": {"to_sector_id": 409}
    }


def test_uncertain_warp_readback_distinguishes_committed_and_uncommitted_moves():
    uncertain = {"from_sector_id": 75, "to_sector_id": 102}
    assert reconcile_warp_readback(uncertain, 102) == "arrived"
    assert reconcile_warp_readback(uncertain, 75) == "not_moved"
    assert reconcile_warp_readback(uncertain, 409) == "moved_elsewhere"
    assert reconcile_warp_readback(uncertain, None) is None


def test_ship_colonists_are_read_from_authoritative_cargo_snapshot():
    assert Planner._ship_colonist_count({
        "colonists": 0, "cargo": [{"commodity": "colonists", "quantity": 29}]
    }) == 29


def test_coloniser_goal_options_route_carried_cargo_to_owned_world():
    state = {
        "player_location_sector": 75,
        "ship_info": {"cargo": [{"commodity": "colonists", "quantity": 29}]},
        "owned_planets": [{"planet_id": 151, "sector_id": 156}],
        "sector_data": {"75": {"has_planet": False}},
        "server_commands": ["planet.colonists.set", "move.pathfind"],
    }
    assert _role_goal_options("Coloniser", state, {}, {}) == ["goto: 156"]


def test_owned_planet_colonisation_prerequisite_preempts_stale_goal_until_colonists_are_loaded():
    p = planner({"playtest_bot": "BOT03", "role_name": "Coloniser"})
    state = {
        "player_location_sector": 177,
        "ship_info": {"holds": 65, "colonists": 0, "genesis_torpedoes": 1,
                      "max_genesis": 2, "max_holds": 65},
        "owned_planets": [{"planet_id": 151, "sector_id": 156,
                            "owner_type": "player", "owner_id": 108}],
    }
    assert p._role_prerequisite_goal(state) == "planet: develop"
    state["planet_colonist_state"] = {"planet_id": 1, "planet_colonists": 0}
    assert p._role_prerequisite_goal(state) is None


def test_coloniser_stops_when_terra_has_no_transferable_colonists():
    p = planner({"playtest_bot": "BOT03", "role_name": "Coloniser"})
    state = {
        "ship_info": {"id": 81, "onplanet": 1, "planet_id": 1,
                      "colonists": 0, "holds": 65},
        "planet_info": {"planet_id": 1, "population": 0},
        "owned_planets": [{"planet_id": 151, "sector_id": 156,
                            "owner_type": "player", "owner_id": 108}],
        "planet_colonist_state": {"planet_id": 1, "planet_colonists": 0,
                                  "ship_holds_available": 65},
        "server_commands": ["planet.colonists.get", "planet.colonists.set", "planet.launch"],
    }
    assert p._achieve_goal(state, "planet: develop") is None
    assert p.state_manager.get("blocked_objective")["reason"] == "Terra has no transferable colonists"


def test_llm_command_shaped_corporation_and_planet_actions_are_bounded_to_role_goals():
    founder = _role_goal_options("Corporation founder", {
        "player_info": {"player": {"corp_id": 0}}, "server_commands": ["corp.create"]
    }, {}, {})
    assert founder == ["corp: found"]
    assert _canonicalize_strategy_goal("corp.create", founder, {}) == "corp: found"
    assert _canonicalize_strategy_goal("combat.attack.ship", founder, {}) is None

    coloniser = _role_goal_options("Coloniser", {
        "player_location_sector": 62,
        "player_info": {"player": {"player_id": 44, "corp_id": 0}},
        "ship_info": {"id": 54, "holds": 10, "genesis_torpedoes": 0},
        "owned_planets": [{"planet_id": 90, "sector_id": 62, "owner_type": "player", "owner_id": 44}],
        "sector_data": {"62": {"has_planet": True}},
        "server_commands": ["planet.land", "planet.colonists.get", "planet.colonists.set",
                            "planet.harvest", "hardware.list", "hardware.buy"],
    }, {}, {})
    assert "planet: develop" in coloniser
    assert _canonicalize_strategy_goal("planet.land", coloniser, {}) == "planet: develop"

    actions = _role_action_inventory("Coloniser", [
        "planet.land", "planet.colonists.get", "planet.colonists.set",
        "planet.harvest", "hardware.buy", "corp.create", "combat.attack",
        "move.warp", "trade.buy", "sector.info",
    ])
    assert actions == ["planet.land", "planet.colonists.get", "planet.colonists.set",
                       "planet.harvest", "hardware.buy", "move.warp", "sector.info"]


def test_founder_transitions_from_invitation_to_coordinated_operation():
    state = {"player_info": {"player": {"corp_id": 17}}, "server_commands": ["corp.member.transfer"]}
    assert _role_goal_options("Corporation founder", state, {}, {}) == ["corp: found"]
    state["corporation_invitation_sent"] = True
    assert _role_goal_options("Corporation founder", state, {}, {}) == ["corp: operate"]
    state["corporation_transfer_completed"] = True
    assert _role_goal_options("Corporation founder", state, {}, {}) == []
    assert _role_goal_options("Corporate operative", state, {}, {}) == []
    assert is_goal_complete("corp: operate", state, {}, "state.snapshot")


def test_combat_roles_leave_protected_zone_without_selecting_a_protected_target():
    goals = _role_goal_options("Bounty hunter", {
        "player_location_sector": 3,
        "ship_info": {"fighters": 10, "shields": 5,
                      "max_fighters": 250, "max_shields": 100},
        "sector_data": {"3": {"ships_present": [{"ship_id": 99, "is_npc": True}]}},
        "server_commands": ["combat.attack", "sector.scan.density", "move.warp"],
    }, {"visible_ship_targets": [{"ship_id": 99}],
        "nearest_known_unprotected_sector": 42}, {})
    assert goals == ["goto: 42"]
    assert "combat: attack" not in goals


def test_persisted_combat_goal_is_dropped_when_bot_is_in_protected_sector():
    plan = ["combat: attack", "goto: 42", "combat: photon"]
    assert discard_protected_combat_goals(plan, {"player_location_sector": 3}) == ["goto: 42"]
    assert discard_protected_combat_goals(plan, {"player_location_sector": 42}) == plan


def test_landed_ship_bootstrap_refreshes_planet_context_after_restart():
    p = planner({})
    state = {
        "player_info": {"player": {"ship_id": 56}},
        "ship_info": {"id": 56, "onplanet": True},
        "player_location_sector": 62,
        "sector_data": {"62": {
            "has_port": False, "is_msl": False, "adjacent": [{"to_sector": 14}],
            "_last_refreshed": time.time(),
            "celestial_objects": [{"planet_id": 62, "name": "Planet 62"}],
        }},
        "server_commands": ["planet.info"],
    }
    assert p.ensure_minimum_world_state(state) == {
        "command": "planet.info", "data": {"planet_id": 62}}


def test_corporate_transfer_waits_for_rendezvous_and_shared_membership():
    class RoutingState:
        @staticmethod
        def get(_key, default=None):
            return default
        @staticmethod
        def find_path(_from, _to):
            return None

    p = Planner(RoutingState(), None, None,
                {"allow_corporation_play": True, "corporation_win_condition": True,
                 "corporation_ai_players": 2, "corporation_partner_username": "playbot09",
                 "corporation_name": "TWClone Playtest Founders", "corporation_meeting_sector": 1})
    state = {"player_location_sector": 322, "sector_data": {"322": {"adjacent": []}},
             "server_commands": ["corp.member.transfer", "move.pathfind"],
             "player_info": {"player": {"corp_id": 17}},
             "online_players": [{"player_id": 114, "username": "playbot09",
                                 "corporation": "TWClone Playtest Founders"}],
             "online_players_refreshed_at": time.time()}
    command = p._achieve_goal(state, "corp:operate")
    assert command == {"command": "move.pathfind", "data": {"from_sector_id": 322, "to_sector_id": 1}}
    state["player_location_sector"] = 1
    assert p._achieve_goal(state, "corp:operate") == {
        "command": "corp.member.transfer",
        "data": {"target_player_id": 114, "asset": "credits", "quantity": 1},
    }
    state["online_players"][0]["corporation"] = None
    assert p._achieve_goal(state, "corp:operate") is None
