import json
import sys
from pathlib import Path


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

import main  # noqa: E402
from llm_client import DEFAULT_MODEL, ollama_generate_url  # noqa: E402
from state_manager import StateManager  # noqa: E402


class PlannerStub:
    current_stage = "explore"


def _game_state():
    return {
        "player_location_sector": 20,
        "player_info": {"player": {"credits": 5000, "alignment": 0, "turns_remaining": 100}},
        "ship_info": {"holds": 20, "cargo": []},
        "sector_data": {"20": {"adjacent": [42]}},
    }


def test_configured_ollama_url_points_to_generate_api_and_model_default_is_realistic():
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
        call.update({"state": state, "model": model, "endpoint": endpoint})
        return json.dumps({"plan": ["goto: 42"]})

    monkeypatch.setattr(main, "get_ollama_response", fake_ollama)
    monkeypatch.setattr(main.time, "sleep", lambda _seconds: None)

    plan = main.select_strategy_plan(
        _game_state(),
        {"ollama_model": "qwen3:8b", "ollama_url": "http://192.168.25.20:11434"},
        manager,
        PlannerStub(),
    )

    assert call["model"] == "qwen3:8b"
    assert call["endpoint"] == "http://192.168.25.20:11434"
    assert "current_sector" in call["state"]
    assert plan == ["goto: 42"]
    assert manager.get("strategy_plan_source") == "ollama"
    assert manager.get("strategy_inference_status") == {"status": "succeeded", "reason": None}


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
