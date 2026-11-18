from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_normal_engine_and_qa_clock_share_the_same_cron_dispatcher():
    source = (ROOT / "src/server_engine.c").read_text()
    assert "server_engine_run_due_cron_tasks" in source
    assert "handler (db, now_s)" in source
    assert "cron_next_due_from (now_s, tasks[i].schedule)" in source
    assert '"test.deterministic_scheduler_enabled"' in source


def test_qa_clock_is_sysop_gated_config_gated_and_day_bounded():
    cron = (ROOT / "src/server_cron.c").read_text()
    command_table = (ROOT / "src/server_loop.c").read_text()
    assert "cmd_sys_cron_advance_to" in cron
    assert '"test.deterministic_scheduler_enabled"' in cron
    assert "PLAYER_TYPE_SYSOP" in cron[cron.index("cmd_sys_cron_advance_to"):]
    assert "days > 30" in cron[cron.index("cmd_sys_cron_advance_to"):]
    assert "server_engine_run_due_cron_tasks (db, now_s, 256)" in cron
    assert '"sys.cron.advance_to"' in command_table


def test_black_market_hardware_uses_simulated_scheduler_not_wall_wait():
    suite = (ROOT / "tests.v2/suite_black_market_hardware.json").read_text()
    assert '"command": "sys.cron.advance_to"' in suite
    assert '"seconds": 125' not in suite
    assert '"value": 11' in suite
    assert '"value": 9' in suite
