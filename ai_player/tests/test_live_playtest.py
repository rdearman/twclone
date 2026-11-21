import importlib.util
from pathlib import Path
import time

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


def test_generated_configs_use_only_the_approved_z820_ollama_endpoint(tmp_path, monkeypatch):
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
        assert config["ollama_url"] == live.OLLAMA_B
        assert config["ollama_max_concurrent"] == 2
        assert config["ollama_num_ctx"] == 4096
        assert config["ollama_num_predict"] == 256
        assert config["ollama_temperature"] == 0.3
        assert config["player_username"] == f"playbot{index:02d}"
        assert len(config["player_password"]) >= 32
        assert config["register_if_missing"] is True


def test_runtime_reconciliation_fixes_bot10_endpoint_without_replacing_identity(tmp_path, monkeypatch):
    monkeypatch.setattr(live, "RUNTIME", tmp_path)
    (tmp_path / "playtest.json").write_text('{"bot_launch_interval_seconds":5}')
    for index in range(1, 11):
        path = tmp_path / "bots" / f"BOT{index:02d}" / "config.json"
        path.parent.mkdir(parents=True)
        path.write_text(live.json.dumps({
            "ollama_url": "http://old-endpoint.invalid:11434", "ollama_model": live.MODEL,
            "player_username": f"playbot{index:02d}", "player_password": "preserve-this",
            "role_name": live.BOT_ROLES[index - 1][0],
        }))
    live.configure_bots()
    for index in range(1, 11):
        config = live.json.loads((tmp_path / "bots" / f"BOT{index:02d}" / "config.json").read_text())
        assert config["ollama_url"] == live.ollama_endpoint_for_index(index)
        assert config["ollama_model"] == live.MODEL
        assert config["player_password"] == "preserve-this"
        assert config["session_heartbeat_interval_seconds"] == 4
    assert live.ollama_endpoint_for_index(10) == live.OLLAMA_B


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


def test_effectiveness_action_window_excludes_older_append_only_log_entries():
    now = time.time()
    old = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now - 3600))
    recent = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now))
    assert live.actions_since(now - 10, [(old, "trade.buy"), (recent, "trade.sell")]) == [
        (recent, "trade.sell")]


def test_role_effectiveness_reports_completed_trade_economics_not_activity_only():
    result = live.role_effectiveness_summary("Trader", {
        "trade_cycle_history": [
            {"profit": 500, "navigation_turns": 2, "cargo_utilization_percent": 80},
            {"profit": -50, "navigation_turns": 1, "cargo_utilization_percent": 40},
        ]
    }, [("10:00", "move.warp"), ("10:01", "trade.buy"),
        ("10:02", "trade.sell")])
    assert result["completed_profitable_cycles"] == 1
    assert result["completed_unprofitable_cycles"] == 1
    assert result["realized_trade_profit_credits"] == 450
    assert result["navigation_turns_per_cycle"] == 1.5
    assert result["profit_per_navigation_turn"] == 150
    assert result["average_cargo_utilization_percent"] == 60


def test_role_effectiveness_normalizes_meaningful_outcomes_by_observed_turns():
    result = live.role_effectiveness_summary("Trader", {
        "ai_metrics": {"turns_observed_consumed": 50},
        "trade_cycle_history": [{"profit": 500, "navigation_turns": 2,
                                  "cargo_utilization_percent": 80}],
    }, [("10:00", "trade.buy"), ("10:01", "trade.sell")])
    assert result["observed_turns_consumed"] == 50
    assert result["non_navigation_actions_per_100_turns"] == 4
    assert result["realized_trade_profit_per_100_turns"] == 1000
    assert result["profitable_cycles_per_100_turns"] == 2


def test_role_effectiveness_preserves_net_trade_losses():
    result = live.role_effectiveness_summary("Trader", {
        "ai_metrics": {"turns_observed_consumed": 50,
                       "trade_cycles_completed": 1,
                       "trade_cycle_profit_credits": -100,
                       "trade_unprofitable_cycles": 1},
        "playtest_effectiveness_baseline": {
            "turns_observed_consumed": 0, "trade_cycles_completed": 0,
            "trade_cycle_profit_credits": 0, "trade_unprofitable_cycles": 0,
        },
    }, [])
    assert result["realized_trade_profit_credits"] == -100
    assert result["realized_trade_profit_per_100_turns"] == -200


def test_bot_endpoint_assignment_never_selects_local_ollama():
    assert {live.ollama_endpoint_for_index(index) for index in range(1, 11)} == {
        "http://192.168.25.20:11434"}


def test_turn_grant_is_database_scoped_idempotent_and_audited(tmp_path, monkeypatch):
    monkeypatch.setattr(live, "RUNTIME", tmp_path)
    (tmp_path / "playtest.json").write_text(live.json.dumps({
        "db_name": "twclone_live_playtest", "db_container": live.PG_CONTAINER,
    }))
    (tmp_path / "bigbang.json").write_text(live.json.dumps({
        "db": "twclone_live_playtest",
        "app": "host=127.0.0.1 port=55639 dbname=%DB% user=postgres password=testpass connect_timeout=5",
    }))
    bot_rows = "\n".join(f"{name}|{100+i}|false|0" for i, name in enumerate(live.PLAYTEST_BOT_USERS))
    ledger_rows = "\n".join(f"{name}|0|1000|1000" for name in live.PLAYTEST_BOT_USERS)
    reads = iter([
        "twclone_live_playtest|5432", bot_rows, "f", ledger_rows,
    ])
    monkeypatch.setattr(live, "_read_playtest_sql", lambda _sql: next(reads))
    calls = []
    def fake_run(args, **kwargs):
        calls.append(args)
        if args[:2] == ["docker", "inspect"]:
            return type("R", (), {"returncode": 0, "stdout": "true\n"})()
        return type("R", (), {"returncode": 0, "stdout": ""})()
    monkeypatch.setattr(live, "run", fake_run)
    live.grant_playtest_turns(1000, "3ef172bd-7f77-4fe0-a7ac-18d7f496ddec", "debug test")
    sql = calls[-1][-1]
    assert "ON CONFLICT (grant_id,player_id) DO NOTHING" in sql
    assert "UPDATE turns t SET turns_remaining=i.resulting_turns" in sql
    assert "playbot10" in sql
    assert "ai_playtest_turn_grants" in sql


def test_turn_grant_rejects_wrong_database_or_invalid_amount_before_mutation(monkeypatch, tmp_path):
    monkeypatch.setattr(live, "RUNTIME", tmp_path)
    (tmp_path / "playtest.json").write_text(live.json.dumps({
        "db_name": "another_database", "db_container": live.PG_CONTAINER,
    }))
    monkeypatch.setattr(live, "require_runtime", lambda: live.json.loads((tmp_path / "playtest.json").read_text()))
    monkeypatch.setattr(live, "run", lambda *_a, **_k: pytest.fail("must not invoke Docker or SQL"))
    with pytest.raises(SystemExit, match="between 1 and 10000"):
        live.grant_playtest_turns(10001, "3ef172bd-7f77-4fe0-a7ac-18d7f496ddec", "debug")
    with pytest.raises(SystemExit, match="identity mismatch"):
        live.grant_playtest_turns(1000, "3ef172bd-7f77-4fe0-a7ac-18d7f496ddec", "debug")
