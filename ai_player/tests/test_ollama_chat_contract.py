import json
import sys
from pathlib import Path


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))
import llm_client  # noqa: E402
import main  # noqa: E402
from state_manager import StateManager  # noqa: E402


def test_chat_uses_schema_constrained_payload_and_records_model_latency(monkeypatch, tmp_path):
    seen = {}

    class Response:
        def raise_for_status(self):
            pass
        def json(self):
            return {"model": "qwen3:8b", "message": {"content": '{"plan":["scan: density"]}'}}

    class Client:
        def __init__(self, timeout):
            seen["timeout"] = timeout
        def __enter__(self):
            return self
        def __exit__(self, *_args):
            pass
        def post(self, url, json):
            seen["url"] = url
            seen["payload"] = json
            return Response()

    monkeypatch.setattr(llm_client.httpx, "Client", Client)
    report = {}
    response = llm_client.get_ollama_response(
        {"sector": 20}, model="qwen3:8b", override_prompt="Observe {game_state}",
        endpoint="http://ollama.local:11434", error_report=report,
        lock_dir=str(tmp_path), request_timeout=110,
    )
    payload = seen["payload"]
    assert seen["url"] == "http://ollama.local:11434/api/chat"
    assert payload["think"] is False
    assert payload["format"]["required"] == ["plan"]
    assert payload["options"] == {"temperature": 0.3, "num_ctx": 4096, "num_predict": 256}
    assert payload["keep_alive"] == "30m"
    assert json.loads(response) == {"plan": ["scan: density"]}
    assert report["status"] == "succeeded"
    assert report["model"] == "qwen3:8b"
    assert report["latency_seconds"] >= 0


def test_slow_inference_uses_authenticated_session_keepalive(monkeypatch):
    class FakeEvent:
        def __init__(self):
            self.waits = 0
            self.stopped = False
        def wait(self, _seconds):
            self.waits += 1
            return self.waits > 1 or self.stopped
        def set(self):
            self.stopped = True

    class FakeThread:
        def __init__(self, target, **_kwargs):
            self.target = target
        def start(self):
            self.target()
        def join(self, timeout=None):
            pass

    class Connection:
        sock = object()
        def __init__(self):
            self.commands = []
        def send_command(self, command):
            self.commands.append(command)
            return True

    class State:
        def get(self, key):
            return "private-session" if key == "session_id" else None
        def record_ai_metric(self, _key):
            pass

    event = FakeEvent()
    monkeypatch.setattr(main.threading, "Event", lambda: event)
    monkeypatch.setattr(main.threading, "Thread", FakeThread)
    connection = Connection()
    stop = main._start_llm_keepalive(connection, State(), {"client_version": "test"})
    stop()
    assert len(connection.commands) == 1
    request = connection.commands[0]
    assert request["command"] == "session.ping"
    assert request["meta"]["session_token"] == "private-session"


def test_uncertain_warp_retains_original_idempotency_for_recovery(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    request = {
        "command": "move.warp",
        "data": {"to_sector_id": 27},
        "meta": {"idempotency_key": "stable-warp-receipt"},
    }
    main.quarantine_timed_out_trades(manager, [("request-1", request)])
    assert manager.get("uncertain_warp") == {
        "request_id": "request-1", "command": "move.warp",
        "to_sector_id": 27, "idempotency_key": "stable-warp-receipt",
    }
