from pathlib import Path
import sys
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from planner import Planner


def planner(config):
    return Planner(SimpleNamespace(get=lambda _key, default=None: default), None, None, config)


def test_corporation_founder_creates_then_invites_configured_partner():
    p = planner({"allow_corporation_play": True, "corporation_win_condition": True,
                 "corporation_ai_players": 2, "corporation_name": "TWClone Playtest Founders",
                 "corporation_partner_username": "playbot09"})
    base = {"server_commands": ["corp.create", "corp.invite", "player.list_online"]}
    assert p._achieve_goal({**base, "player_info": {"player": {"corp_id": 0}}}, "corp:found") == {
        "command": "corp.create", "data": {"name": "TWClone Playtest Founders"}}
    state = {**base, "player_info": {"player": {"corp_id": 7}},
             "online_players": [{"player_id": 12, "username": "playbot09", "corporation": None}]}
    assert p._achieve_goal(state, "corp:found") == {
        "command": "corp.invite", "data": {"target_player_id": 12}}


def test_corporate_operative_uses_public_corporation_list_and_join_contract():
    p = planner({"allow_corporation_play": True, "corporation_win_condition": True,
                 "corporation_ai_players": 2, "corporation_partner_name": "TWClone Playtest Founders"})
    state = {"server_commands": ["corp.list", "corp.join"],
             "player_info": {"player": {"corp_id": 0}}}
    assert p._achieve_goal(state, "corp:join") == {"command": "corp.list", "data": {}}
    state["corporations"] = [{"corp_id": 7, "name": "TWClone Playtest Founders"}]
    assert p._achieve_goal(state, "corp:join") == {
        "command": "corp.join", "data": {"corp_id": 7}}


def test_corporation_goals_remain_disabled_outside_opted_in_pair():
    p = planner({"allow_corporation_play": False})
    assert p._achieve_goal({"player_info": {"player": {"corp_id": 0}}}, "corp:found") is None
