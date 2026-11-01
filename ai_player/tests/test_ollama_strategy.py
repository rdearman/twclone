import json
import sys
from pathlib import Path
import pytest


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

import main  # noqa: E402
from llm_client import DEFAULT_MODEL, ollama_generate_url  # noqa: E402
from state_manager import StateManager  # noqa: E402


class PlannerStub:
    current_stage = "explore"


def test_deterministic_strategy_bypasses_inference_and_uses_local_plan(monkeypatch, tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    planner = PlannerStub()
    monkeypatch.setattr(main, "get_strategy_from_llm", lambda *args, **kwargs: pytest.fail("inference called"))
    monkeypatch.setattr(main, "_get_fallback_plan", lambda state, state_manager, selected_planner: ["goto: 42"])

    plan = main.select_strategy_plan(_game_state(), {"strategy_mode": "deterministic"}, manager, planner)

    assert plan == ["goto: 42"]
    assert manager.get("strategy_plan_source") == "deterministic"
    assert manager.get("strategy_inference_status") == {"status": "disabled", "reason": None}


def test_invalid_strategy_mode_does_not_call_inference(monkeypatch, tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    monkeypatch.setattr(main, "get_strategy_from_llm", lambda *args, **kwargs: pytest.fail("inference called"))
    with pytest.raises(ValueError, match="Unknown strategy_mode"):
        main.select_strategy_plan(_game_state(), {"strategy_mode": "determministic"}, manager, PlannerStub())


def _game_state():
    return {
        "player_location_sector": 20,
        "player_info": {"player": {"credits": 5000, "alignment": 0, "turns_remaining": 100}},
        "ship_info": {"holds": 20, "cargo": []},
        "sector_data": {"20": {"adjacent": [42]}},
    }


def test_configured_ollama_url_points_to_generate_api_and_model_default_is_realistic():
    assert ollama_generate_url() == "http://192.168.25.5:11434/api/generate"
    assert json.loads((AI_PLAYER_DIR / "config.json").read_text())["ollama_url"] == "http://192.168.25.5:11434"
    assert ollama_generate_url("http://192.168.25.20:11434") == (
        "http://192.168.25.20:11434/api/generate"
    )
    assert ollama_generate_url("http://ollama:11434/api/generate") == (
        "http://ollama:11434/api/generate"
    )
    assert DEFAULT_MODEL == "qwen3:8b"


def test_ollama_plan_is_selected_and_retained_as_controller_strategy(monkeypatch, tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.set("valid_commodity_names", ["ORE", "ORG", "EQU"])
    call = {}

    def fake_ollama(state, model, prompt_key, override_prompt=None, endpoint=None,
                    error_report=None):
        call.update({"state": state, "model": model, "endpoint": endpoint,
                     "prompt": override_prompt})
        return json.dumps({"plan": ["goto: 42"]})

    monkeypatch.setattr(main, "get_ollama_response", fake_ollama)
    monkeypatch.setattr(main.time, "sleep", lambda _seconds: None)

    plan = main.select_strategy_plan(
        _game_state(),
        {"ollama_model": "qwen3:8b", "ollama_url": "http://192.168.25.20:11434",
         "behavior_profile": "trader"},
        manager,
        PlannerStub(),
    )

    assert call["model"] == "qwen3:8b"
    assert call["endpoint"] == "http://192.168.25.20:11434"
    assert "Prioritize viable market trades" in call["prompt"]
    assert "current_sector" in call["state"]
    assert plan == ["goto: 42"]
    assert manager.get("strategy_plan_source") == "ollama"
    assert manager.get("strategy_inference_status") == {"status": "succeeded", "reason": None}


def test_ollama_prompt_uses_the_selected_population_profile(monkeypatch, tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.set("valid_commodity_names", ["ORE", "ORG", "EQU"])
    prompts = []

    def fake_ollama(_state, _model, _key, override_prompt=None, **_kwargs):
        prompts.append(override_prompt)
        return json.dumps({"plan": ["goto: 42"]})

    monkeypatch.setattr(main, "get_ollama_response", fake_ollama)
    monkeypatch.setattr(main.time, "sleep", lambda _seconds: None)
    for profile in ("explorer", "cautious", "opportunist"):
        main.select_strategy_plan(
            _game_state(),
            {"ollama_model": "qwen3:8b", "behavior_profile": profile},
            manager,
            PlannerStub(),
        )

    assert "Prioritize unexplored reachable sectors" in prompts[0]
    assert "Prefer known, surveyed reachable locations" in prompts[1]
    assert "fresh public route recommendations" in prompts[2]


def test_exploration_prompt_shows_known_map_and_rejects_revisiting_known_sectors(
    monkeypatch, tmp_path
):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.set("valid_commodity_names", ["ORE", "ORG", "EQU"])
    state = _game_state()
    state.update({
        "player_location_sector": 10,
        "sector_data": {
            "5": {"adjacent": []},
            "10": {"adjacent": [{"to_sector": 5}, {"to_sector": 11}]},
            "20": {"adjacent": []},
        },
        "port_info_by_sector": {},
    })
    call = {}

    def fake_ollama(game_state, _model, _key, override_prompt=None, **_kwargs):
        call["state"] = game_state
        call["prompt"] = override_prompt
        return json.dumps({"plan": ["goto: 5", "goto: 11"]})

    monkeypatch.setattr(main, "get_ollama_response", fake_ollama)
    monkeypatch.setattr(main.time, "sleep", lambda _seconds: None)

    plan = main.select_strategy_plan(
        state, {"ollama_model": "qwen3:8b", "behavior_profile": "explorer"},
        manager, PlannerStub(),
    )

    assert call["state"]["known_sector_ids"] == [5, 10, 20]
    assert call["state"]["valid_goto_sectors"] == [5, 11]
    assert call["state"]["unexplored_adjacent_sector_ids"] == [11]
    assert "unexplored_adjacent_sector_ids" in call["prompt"]
    assert plan == ["goto: 11"]
    assert manager.get("strategy_plan_source") == "ollama"


def test_recovery_discards_stale_non_port_navigation_but_keeps_trade_route():
    state = {
        "sector_data": {"5": {}, "10": {}, "20": {}},
        "port_info_by_sector": {"20": {"port_id": 4, "commodities": [{"commodity": "ORE"}]}},
        "ship_info": {"cargo": []},
    }
    assert main.discard_stale_exploration_goals(
        ["goto: 5", "goto: 11", "goto: 20"], state
    ) == ["goto: 11", "goto: 20"]

    state["ship_info"]["cargo"] = [{"commodity": "ORE", "quantity": 1}]
    assert main.discard_stale_exploration_goals(["goto: 5", "sell: ORE"], state) == [
        "goto: 5", "sell: ORE"
    ]


def test_live_audit_before_snapshot_is_not_mutated_by_authoritative_readback():
    state = {
        "player_location_sector": 7,
        "player_info": {"player": {"turns_remaining": 100, "credits": "5000.00"}},
        "ship_info": {"cargo": []},
    }

    before = main._audit_state_snapshot(state)
    state["player_info"]["player"]["turns_remaining"] = 99
    state["ship_info"]["cargo"].append({"commodity": "ORE", "quantity": 1})

    assert before["player_info"]["player"]["turns_remaining"] == 100
    assert before["ship_info"]["cargo"] == []


def test_ollama_failure_uses_existing_fallback_and_records_failure(monkeypatch, tmp_path, caplog):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.set("valid_commodity_names", ["ORE", "ORG", "EQU"])
    def failed_ollama(*_args, error_report=None, **_kwargs):
        error_report.update({"status": "http_error", "reason": "Ollama returned HTTP 404"})
        return None

    monkeypatch.setattr(main, "get_ollama_response", failed_ollama)
    monkeypatch.setattr(main.time, "sleep", lambda _seconds: None)
    fallback = ["goto: 42"]
    monkeypatch.setattr(main, "_get_fallback_plan", lambda *_args: fallback)

    plan = main.select_strategy_plan(
        _game_state(),
        {"ollama_model": "qwen3:8b", "ollama_url": "http://192.168.25.20:11434"},
        manager,
        PlannerStub(),
    )

    assert plan == fallback
    assert manager.get("strategy_plan_source") == "fallback"
    status = manager.get("strategy_inference_status")
    assert status == {"status": "http_error", "reason": "Ollama returned HTTP 404"}
