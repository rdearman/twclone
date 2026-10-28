import json
import sys
from pathlib import Path


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

from bug_reporter import BugReporter  # noqa: E402


def test_qa_logs_redact_authentication_and_session_secrets(tmp_path):
    reporter = BugReporter(str(tmp_path))
    reporter.log_command({
        "command": "auth.login",
        "data": {"username": "qa", "passwd": "do-not-save"},
        "meta": {"session_token": "also-secret"},
    })

    logged = (tmp_path / "qa_log.jsonl").read_text()
    assert "do-not-save" not in logged
    assert "also-secret" not in logged
    assert "[REDACTED]" in logged


def test_reproducible_defect_writes_json_and_markdown_without_secrets(tmp_path):
    reporter = BugReporter(str(tmp_path))
    folder = Path(reporter.report_defect(
        defect_id="trade-cargo-mismatch",
        identity="qa-player-1",
        seed=17,
        starting_state={"credits": 100, "session_id": "private"},
        commands=[{"command": "trade.buy", "data": {"quantity": 1}}],
        responses=[{"status": "ok", "data": {"session_token": "private"}}],
        events=[],
        expected={"cargo_delta": 1},
        actual={"cargo_delta": 0},
        reproduction=["survey port", "quote ORE", "buy ORE x1"],
        severity="major",
        confidence="high",
    ))

    report = json.loads((folder / "defect.json").read_text())
    summary = (folder / "summary.md").read_text()
    assert report["test_seed"] == 17
    assert report["expected_outcome"] == {"cargo_delta": 1}
    assert report["starting_state"]["session_id"] == "[REDACTED]"
    assert "private" not in (folder / "defect.json").read_text()
    assert "trade-cargo-mismatch" in summary


def test_successful_warp_is_checked_against_authoritative_readback(tmp_path):
    reporter = BugReporter(str(tmp_path))
    before = {"player_location_sector": 1, "player_info": {"player": {"credits": 5000}}, "ship_info": {"id": 4, "cargo": []}}
    after = {"player_location_sector": 2, "player_info": {"player": {"credits": 5000}}, "ship_info": {"id": 4, "cargo": []}}
    result = reporter.log_gameplay_outcome(
        identity="ai_qa_bot", seed=9,
        command={"id": "warp-1", "command": "move.warp", "data": {"to_sector_id": 2}},
        response={"status": "ok", "type": "move.result", "data": {"to_sector_id": 2}},
        state_before=before, state_after=after,
    )
    assert result["succeeded"] is True
    assert result["consistent"] is True


def test_successful_trade_mismatch_creates_reproducible_defect(tmp_path):
    reporter = BugReporter(str(tmp_path))
    before = {"player_info": {"player": {"credits": "5000"}}, "ship_info": {"id": 4, "cargo": []}}
    after = {"player_info": {"player": {"credits": "4900"}}, "ship_info": {"id": 4, "cargo": []}}
    result = reporter.log_gameplay_outcome(
        identity="ai_qa_bot", seed=11,
        command={"id": "buy-1", "command": "trade.buy", "data": {"items": [{"commodity": "ORE", "quantity": 2}]}},
        response={"status": "ok", "type": "trade.buy_receipt_v1", "data": {"credits_remaining": "4900", "lines": [{"commodity": "ORE", "quantity": 2}]}},
        state_before=before, state_after=after,
    )
    assert result["consistent"] is False
    report = next(tmp_path.glob("live_trade_buy_*/defect.json"))
    assert json.loads(report.read_text())["severity"] == "major"


def test_expected_refusal_is_not_reported_as_a_defect(tmp_path):
    reporter = BugReporter(str(tmp_path))
    result = reporter.log_gameplay_outcome(
        identity="ai_qa_bot", seed=1,
        command={"command": "trade.buy", "data": {"items": [{"commodity": "ORE", "quantity": 1}]}},
        response={"status": "refused", "error": {"code": 1403}},
        state_before={}, state_after={},
    )
    assert result["succeeded"] is False
    assert result["consistent"] is None
    assert list(tmp_path.glob("live_*/defect.json")) == []


def test_incomplete_readback_is_not_reported_as_a_gameplay_defect(tmp_path):
    reporter = BugReporter(str(tmp_path))
    result = reporter.log_gameplay_outcome(
        identity="ai_qa_bot", seed=1,
        command={"command": "move.warp", "data": {"to_sector_id": 2}},
        response={"status": "ok", "type": "move.result", "data": {"to_sector_id": 2}},
        state_before={"player_location_sector": 1}, state_after={"player_location_sector": 1},
        verification_complete=False, state_after_source="readback unavailable",
    )
    assert result["consistent"] is None
    assert list(tmp_path.glob("live_*/defect.json")) == []


def test_planner_debug_context_redacts_session_and_password(caplog):
    import logging
    from planner import Planner

    caplog.set_level(logging.DEBUG, logger="planner")
    class StateStub:
        def get(self, key, default=None):
            return default

    planner = Planner(StateStub(), None, None, {})
    planner._build_payload("bank.balance", {
        "session_token": "session-canary",
        "password": "password-canary",
    })

    assert "session-canary" not in caplog.text
    assert "password-canary" not in caplog.text
    assert "[REDACTED]" in caplog.text
