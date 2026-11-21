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
                  schema_commands_to_fetch)


def planner(config):
    return Planner(SimpleNamespace(get=lambda _key, default=None: default), None, None, config)


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
        "player_info": {"player": {"corp_id": 0}},
        "ship_info": {"genesis_torpedoes": 0, "onplanet": False},
        "sector_data": {"4": {"has_port": True, "has_planet": False}},
        "server_commands": ["hardware.list", "hardware.buy", "planet.genesis_create"],
    }
    assert p._achieve_goal(state, "hardware:genesis") == {
        "command": "hardware.list", "data": {}}
    state["hardware_catalog_checked_sectors"] = ["4"]
    state["hardware_catalog_by_sector"] = {
        "4": {"items": [{"code": "GENESIS", "stock_quantity": 3}]}}
    assert p._achieve_goal(state, "hardware:genesis") == {
        "command": "hardware.buy", "data": {"code": "GENESIS", "quantity": 1}}


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
    assert is_goal_complete("shipyard: recover", {
        "ship_info": {"holds": 20},
    }, {}, "shipyard.upgraded_v1")


def test_shipyard_recovery_completes_from_success_receipt_even_before_ship_refresh():
    assert is_goal_complete("shipyard: recover", {
        "ship_info": {"id": 56, "holds": 0},
    }, {"ship_id": 56, "new_type_id": 2}, "shipyard.upgraded_v1")
    assert not is_goal_complete("shipyard: recover", {
        "ship_info": {"id": 56, "holds": 0},
    }, {"ship_id": 56}, "shipyard.upgraded_v1")


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
             "adjacent_ship_targets": [{"ship_id": 81, "sector_id": 23}]}
    assert p._achieve_goal(state, "combat:photon") == {
        "command": "combat.fire_photon",
        "data": {"sector_id": 23, "target_ship_id": 81},
    }
    state["ship_info"]["photons"] = 0
    assert p._achieve_goal(state, "combat:photon") is None


def test_unavailable_role_goal_is_reported_as_blocked_instead_of_falling_back_to_trade():
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
        "ship_info": {"id": 8, "onplanet": False, "genesis_torpedoes": 0},
        "sector_data": {"4": {"adjacent": [5], "has_port": False,
                                "has_planet": False, "_last_refreshed": 9999999999}},
        "server_commands": ["planet.genesis_create", "trade.buy", "move.warp"],
        "known_ports": {}, "port_info_by_sector": {},
    }
    assert p.get_next_command(game, "planet: develop") is None
    assert state_manager.values["blocked_objective"]["goal"] == "planet: develop"
    assert "Genesis torpedo" in state_manager.values["blocked_objective"]["reason"]


def test_llm_command_shaped_corporation_and_planet_actions_are_bounded_to_role_goals():
    founder = _role_goal_options("Corporation founder", {
        "player_info": {"player": {"corp_id": 0}}, "server_commands": ["corp.create"]
    }, {}, {})
    assert founder == ["corp: found"]
    assert _canonicalize_strategy_goal("corp.create", founder, {}) == "corp: found"
    assert _canonicalize_strategy_goal("combat.attack.ship", founder, {}) is None

    coloniser = _role_goal_options("Coloniser", {
        "player_location_sector": 62,
        "player_info": {"player": {"corp_id": 0}},
        "ship_info": {"genesis_torpedoes": 0},
        "sector_data": {"62": {"has_planet": True}},
        "server_commands": ["planet.land", "planet.harvest", "hardware.list", "hardware.buy"],
    }, {}, {})
    assert "planet: develop" in coloniser
    assert _canonicalize_strategy_goal("planet.land", coloniser, {}) == "planet: develop"

    actions = _role_action_inventory("Coloniser", [
        "planet.land", "planet.harvest", "hardware.buy", "corp.create", "combat.attack",
        "move.warp", "trade.buy", "sector.info",
    ])
    assert actions == ["planet.land", "planet.harvest", "hardware.buy", "move.warp", "sector.info"]


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
            "has_port": False, "adjacent": [{"to_sector": 14}],
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
