import json
import os
import signal
from pathlib import Path

import pytest

import fleet


def _write_config(directory: Path, name: str, username: str, password: str) -> Path:
    path = directory / f"{name}.json"
    path.write_text(json.dumps({
        "player_username": username,
        "player_password": password,
        "state_file": "state.json",
        "log_file": "ai_player.log",
        "bug_report_path": "bug_reports",
        "ollama_model": "qwen3:8b",
    }))
    return path


def test_player_configs_require_independent_identity_and_credentials(tmp_path):
    _write_config(tmp_path, "one", "qa_one", "secret-one")
    _write_config(tmp_path, "two", "qa_two", "secret-two")

    players = fleet.load_player_configs(tmp_path, 2)

    assert [config["player_username"] for _, config in players] == ["qa_one", "qa_two"]
    with pytest.raises(fleet.FleetConfigError, match="distinct credentials"):
        _write_config(tmp_path, "three", "qa_three", "secret-one")
        fleet.load_player_configs(tmp_path, 3)


def test_runtime_configs_isolate_state_logs_and_qa_reports(tmp_path):
    source = tmp_path / "source"
    source.mkdir()
    _write_config(source, "one", "qa_one", "secret-one")
    _write_config(source, "two", "qa_two", "secret-two")
    configs = fleet.load_player_configs(source, 2)
    run_root = tmp_path / "run"
    run_root.mkdir()

    prepared = fleet.prepare_runtime_configs(configs, run_root)
    runtime = [json.loads(Path(item["config_path"]).read_text()) for item in prepared]

    for key in ("state_file", "log_file", "bug_report_path"):
        assert runtime[0][key] != runtime[1][key]
    assert runtime[0]["player_username"] != runtime[1]["player_username"]
    assert all(os.stat(item["config_path"]).st_mode & 0o777 == 0o600 for item in prepared)

    resumed = fleet.load_resumable_players(run_root)
    assert [item["identity"] for item in resumed] == ["qa_one", "qa_two"]
    assert [item["player_dir"] for item in resumed] == [item["player_dir"] for item in prepared]


def test_count_is_bounded_to_population_target(tmp_path):
    with pytest.raises(fleet.FleetConfigError, match="between 1 and 50"):
        fleet.load_player_configs(tmp_path, 51)


def test_orchestration_respects_concurrency_and_writes_aggregate(tmp_path, monkeypatch):
    players = []
    for index in range(4):
        player_dir = tmp_path / f"p{index}"
        player_dir.mkdir()
        players.append({
            "identity": f"qa_{index}", "config_path": str(player_dir / "config.json"),
            "player_dir": str(player_dir),
        })
    observed = {"active": 0, "maximum": 0, "started": []}

    class Child:
        _next_pid = 10000

        def __init__(self, args, **kwargs):
            self.pid = Child._next_pid
            Child._next_pid += 1
            self.returncode = None
            observed["started"].append(args[-1])
            observed["active"] += 1
            observed["maximum"] = max(observed["maximum"], observed["active"])

        def poll(self):
            if self.returncode is None:
                self.returncode = 0
                observed["active"] -= 1
            return self.returncode

    monkeypatch.setattr(fleet.subprocess, "Popen", Child)
    report = fleet.run_cohort(
        players, run_root=tmp_path, duration_seconds=5, max_concurrent=2,
        main_path=tmp_path / "main.py",
    )

    assert observed["maximum"] <= 2
    assert len(observed["started"]) == 4
    assert all(item["exit_code"] == 0 for item in report["players"])
    assert json.loads((tmp_path / "summary.json").read_text())["requested_players"] == 4


def test_shutdown_signals_only_owned_children_for_graceful_exit(monkeypatch):
    signals = []

    class Child:
        pid = 124567
        returncode = None

        def poll(self):
            return self.returncode

        def wait(self, timeout=None):
            self.returncode = 0
            return self.returncode

    monkeypatch.setattr(fleet.os, "killpg", lambda pid, sig: signals.append((pid, sig)))
    child = Child()
    fleet.stop_processes([child], grace_seconds=1)

    assert signals == [(child.pid, signal.SIGINT)]
    assert child.returncode == 0
