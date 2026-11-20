import importlib.util
from pathlib import Path

import pytest


MODULE_PATH = Path(__file__).resolve().parents[1] / "live_playtest.py"
spec = importlib.util.spec_from_file_location("live_playtest", MODULE_PATH)
live = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)


def test_playtest_contract_uses_ten_bots_and_fixed_model_on_both_hosts():
    assert len(live.BOT_ROLES) == 10
    assert live.MODEL == "qwen3:8b"
    assert live.MODEL_ID == "500a1f067a9f"
    assert all("Primary" in objective or objective for _, objective, _ in live.BOT_ROLES)


def test_generated_configs_use_two_independent_ollama_endpoints(tmp_path, monkeypatch):
    monkeypatch.setattr(live, "RUNTIME", tmp_path)
    monkeypatch.setattr(live, "check_ollama", lambda: {"A": {}, "B": {}})
    runtime = {}
    live._write_bot_configs(runtime)
    assert runtime["bot_count"] == 10
    for index in range(1, 11):
        config = live.json.loads((tmp_path / "bots" / f"BOT{index:02d}" / "config.json").read_text())
        assert config["strategy_mode"] == "ollama"
        assert config["require_llm_decisions"] is True
        assert config["ollama_model"] == "qwen3:8b"
        assert config["ollama_url"] == (live.OLLAMA_A if index <= 5 else live.OLLAMA_B)
        assert config["ollama_max_concurrent"] == 2
        assert config["ollama_num_ctx"] == 4096
        assert config["ollama_num_predict"] == 256
        assert config["ollama_temperature"] == 0.3
        assert config["player_username"] == f"playbot{index:02d}"
        assert len(config["player_password"]) >= 32
        assert config["register_if_missing"] is True


def test_bot_startup_is_sequentially_staggered_and_individual_restartable(tmp_path, monkeypatch):
    monkeypatch.setattr(live, "RUNTIME", tmp_path)
    (tmp_path / "playtest.json").write_text('{"bot_launch_interval_seconds":5}')
    calls = []
    def fake_run(args, **kwargs):
        calls.append(args)
        active_check = args[:2] == ["systemctl", "--user"] and len(args) > 2 and args[2] == "is-active"
        return type("R", (), {"returncode": 3 if active_check else 0,
                              "stdout": "inactive" if active_check else ""})()
    monkeypatch.setattr(live, "run", fake_run)
    delays = []
    monkeypatch.setattr(live.time, "sleep", delays.append)
    live.start_bots()
    assert len([c for c in calls if c[0] == "systemctl" and c[2] in ("start", "restart")]) == 10
    assert delays == [5.0] * 9
    launched = [call[3] for call in calls if len(call) == 4 and call[0:3] == ["systemctl", "--user", "start"]]
    assert launched == [f"twclone-live-bot@BOT{i:02d}.service" for i in range(1, 11)]


def test_reset_requires_explicit_confirmation():
    class Args:
        confirm_reset = False
    with pytest.raises(SystemExit, match="--confirm-reset"):
        live.reset_universe(Args())


def test_dashboard_counts_successful_gameplay_actions_but_not_read_only_queries():
    log = "\n".join([
        "2026-11-20 10:00:00,000 - root - INFO - PLAYTEST_ACTION bot=BOT01 number=1 command=move.warp",
        "2026-11-20 10:00:01,000 - root - INFO - PLAYTEST_ACTION bot=BOT01 number=2 command=bank.balance",
        "2026-11-20 10:00:02,000 - root - INFO - PLAYTEST_ACTION bot=BOT01 number=3 command=trade.buy",
        "2026-11-20 10:00:03,000 - root - INFO - PLAYTEST_ACTION bot=BOT02 number=1 command=trade.sell",
    ])
    assert live.completed_gameplay_actions("BOT01", log) == [
        ("2026-11-20 10:00:00", "move.warp"),
        ("2026-11-20 10:00:02", "trade.buy"),
    ]
