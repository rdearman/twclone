import sys
import time
from pathlib import Path


AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

from main import (advance_completed_goals, align_command_idempotency,
                  build_registration_command, is_goal_complete, _pause_for_turn_refresh,
                  _audit_state_snapshot, process_responses, quarantine_timed_out_trades)  # noqa: E402
from planner import Planner  # noqa: E402
from state_manager import StateManager  # noqa: E402
from bug_reporter import BugReporter  # noqa: E402


class FakeStateManager:
    def __init__(self, state, pending):
        self.state = state
        self.pending = pending
        self.updates = []

    def get_all(self):
        return dict(self.state)

    def get_pending_command(self, request_id):
        return self.pending.get(request_id)

    def remove_pending_command(self, request_id):
        self.pending.pop(request_id, None)

    def add_pending_command(self, request_id, command):
        self.pending[request_id] = command

    def set(self, key, value):
        self.state[key] = value
        self.updates.append((key, value))

    def get(self, key, default=None):
        return self.state.get(key, default)

    def set_last_action_result(self, value):
        self.state["last_action_result"] = value

    def record_command_failure(self, _command):
        pass

    def record_ai_metric(self, name, amount=1):
        metrics = self.state.setdefault("ai_metrics", {})
        metrics[name] = metrics.get(name, 0) + amount

    def add_to_warp_blacklist(self, sector_id):
        self.state.setdefault("warp_blacklist", []).append(sector_id)

    def validate_state(self):
        return True


class FakeConnection:
    def __init__(self):
        self.sent = []

    def send_command(self, command):
        self.sent.append(command)
        return True


class UnusedBugReporter:
    def report_state_inconsistency(self, *_args):
        raise AssertionError("unexpected state inconsistency")


class QuietBandit:
    def give_feedback(self, *_args):
        pass


def test_timed_out_trade_quarantine_survives_restart(tmp_path):
    path = tmp_path / "state.json"
    manager = StateManager(str(path), {})
    quarantine_timed_out_trades(manager, [("buy-1", {"command": "trade.buy", "meta": {"session_token": "secret"}})])
    restored = StateManager(str(path), {})
    assert restored.get("uncertain_trade") == {"request_id": "buy-1", "command": "trade.buy"}
    assert "secret" not in path.read_text()


def test_read_timeout_does_not_quarantine_gameplay(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    quarantine_timed_out_trades(manager, [("read-1", {"command": "ship.info"})])
    assert manager.get("uncertain_trade") is None


def test_pending_command_timeout_removes_only_expired_request(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.add_pending_command("old", {"command": "trade.buy"})
    manager.add_pending_command("new", {"command": "sector.info"})
    manager._pending_command_times["old"] = time.monotonic() - 60

    assert manager.expire_pending_commands(30) == [
        ("old", {"command": "trade.buy"})
    ]
    assert manager.get_pending_command("new") == {"command": "sector.info"}


def test_reconnect_preserves_known_lot_and_does_not_invent_cost_for_extra_cargo(tmp_path):
    path = tmp_path / "state.json"
    manager = StateManager(str(path), {})
    manager.set("cargo_metadata", [{
        "commodity": "ORE", "quantity": 3, "purchase_price": 100, "origin_port_id": 7,
    }])

    manager.update_player_info({"ship": {"id": 1, "cargo": [{"commodity": "ORE", "quantity": 5}]}})

    expected = [
        {"commodity": "ORE", "quantity": 3, "purchase_price": 100, "origin_port_id": 7},
        {"commodity": "ORE", "quantity": 2},
    ]
    assert manager.get("ship_info")["cargo"] == expected
    assert StateManager(str(path), {}).get("cargo_metadata") == expected


def test_reconnect_does_not_assign_a_lot_after_unexplained_cargo_loss(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.set("cargo_metadata", [
        {"commodity": "ORE", "quantity": 3, "purchase_price": 100, "origin_port_id": 7},
        {"commodity": "ORE", "quantity": 2, "purchase_price": 120, "origin_port_id": 8},
    ])

    manager.update_player_info({"ship": {"id": 1, "cargo": [{"commodity": "ORE", "quantity": 2}]}})

    assert manager.get("ship_info")["cargo"] == [{"commodity": "ORE", "quantity": 2}]


def test_exhausted_turn_pause_forces_authoritative_refresh_even_after_restart(tmp_path, monkeypatch):
    path = tmp_path / "state.json"
    manager = StateManager(str(path), {})
    manager.set("player_info", {"player": {"ship_id": 1, "turns_remaining": 0}})
    sleeps = []
    monkeypatch.setattr("main.time.sleep", sleeps.append)

    _pause_for_turn_refresh(manager, seconds=60)

    assert sleeps == [60]
    restarted = StateManager(str(path), {})
    planner = Planner.__new__(Planner)
    planner.state_manager = restarted
    assert planner.ensure_minimum_world_state(restarted.get_all()) == {
        "command": "player.my_info", "data": {},
    }


def test_authentication_retries_respect_failure_backoff():
    class Manager:
        def get(self, key, default=None):
            return default

    class Policy:
        epsilon = 0.1

    planner = Planner(Manager(), None, Policy(), {})
    assert not planner._is_command_ready(
        "auth.login", {"auth.login": {"next_retry_time": time.time() + 60}}
    )
    assert planner._is_command_ready("auth.login", {})


def test_explicit_registration_uses_public_auth_register_envelope():
    command = build_registration_command({
        "player_username": "ai_qa_bot_2",
        "player_password": "private-test-credential",
        "client_version": "ai-test",
    }, "0123456789abcdef")

    assert command["command"] == "auth.register"
    assert command["id"] == "c-01234567"
    assert command["data"]["username"] == "ai_qa_bot_2"
    assert command["data"]["passwd"] == "private-test-credential"
    assert command["meta"]["idempotency_key"] == "0123456789abcdef"


def test_trade_command_reuses_payload_idempotency_key_in_envelope():
    payload_key = "planner-generated-key"
    data = {"port_id": 10, "idempotency_key": payload_key}

    envelope_key = align_command_idempotency("trade.buy", data, "new-envelope-key")

    assert envelope_key == payload_key
    assert data["idempotency_key"] == envelope_key


def test_trade_command_adds_matching_idempotency_key_when_missing():
    data = {"port_id": 10}

    envelope_key = align_command_idempotency("trade.sell", data, "generated-key")

    assert envelope_key == "generated-key"
    assert data["idempotency_key"] == envelope_key


def test_successful_strategy_goal_increments_completion_metric(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.set("strategy_plan", ["planet: info"])
    manager.add_pending_command("planet-info", {
        "command": "planet.info", "data": {"planet_id": 321}
    })

    process_responses(
        [{"reply_to": "planet-info", "status": "ok", "type": "planet.info",
          "data": {"planet_id": 321}}],
        None, manager, UnusedBugReporter(), QuietBandit(), {}, None,
    )

    assert manager.get("strategy_plan") == []
    assert manager.get("ai_metrics")["goals_completed"] == 1


def test_atomic_state_save_keeps_previous_state_when_serialization_fails(tmp_path):
    path = tmp_path / "state.json"
    manager = StateManager(str(path), {})
    manager.set("strategy_plan", ["goto: 42", "survey: port"])
    original = path.read_bytes()

    manager.state["invalid_for_json"] = object()
    manager.save_state()

    assert path.read_bytes() == original
    assert list(tmp_path.glob(".state.json.*.tmp")) == []


def test_persisted_goto_goal_completes_with_string_or_numeric_sector_id():
    assert is_goal_complete("goto: 42", {"player_location_sector": "42"}, {}, "state.snapshot")
    assert is_goal_complete("goto: 42", {}, {"to_sector_id": "42"}, "move.result")


def test_login_after_restart_discards_cached_player_and_ship_state(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.set("player_info", {"player": {"player_id": 34, "turns_remaining": 999}})
    manager.set("ship_info", {"id": 60, "cargo": []})
    manager.set("player_location_sector", 1945)
    manager.add_pending_command("login", {"command": "auth.login"})

    process_responses(
        [{"reply_to": "login", "status": "ok", "type": "auth.session",
          "data": {"session_token": "private-session"}}],
        FakeConnection(), manager, UnusedBugReporter(), QuietBandit(), {}, None,
    )

    assert manager.get("session_id") == "private-session"
    assert manager.get("player_info") is None
    assert manager.get("ship_info") is None
    assert manager.get("player_location_sector") is None


def test_live_trade_audit_uses_dispatch_time_snapshot_for_outcome(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    before = {
        "player_location_sector": 812,
        "player_info": {"player": {"credits": "5000.00", "turns_remaining": 20}},
        "ship_info": {"id": 60, "holds": 10, "cargo": []},
    }
    manager.set("player_location_sector", 812)
    manager.set("player_info", before["player_info"])
    manager.set("ship_info", before["ship_info"])
    manager.add_pending_command("buy", {
        "command": "trade.buy",
        "data": {"port_id": 321, "items": [{"commodity": "ORG", "quantity": 1}]},
    })
    manager._live_audit_command_before = {"buy": _audit_state_snapshot(before)}
    connection = FakeConnection()

    process_responses(
        [{"reply_to": "buy", "status": "ok", "type": "trade.buy_receipt_v1",
          "data": {"port_id": 321, "lines": [{"commodity": "ORG", "quantity": 1,
                                                  "unit_price": 151, "value": "151.00"}],
                   "fees": "0.00", "credits_remaining": "4849.00"}}],
        connection, manager, BugReporter(str(tmp_path / "reports")), QuietBandit(),
        {"live_gameplay_audit": True}, None,
    )

    context = manager._live_audit_contexts["buy"]
    assert context["state_before"]["player_info"]["player"]["credits"] == "5000.00"
    assert context["state_before"]["ship_info"]["cargo"] == []
    assert context["before_verified"] is True
    assert manager._live_audit_command_before == {}


def test_persisted_survey_goal_completes_from_fresh_cached_quotes():
    state = {
        "player_location_sector": 42,
        "port_info_by_sector": {
            "42": {"port_id": 7, "commodities": [{"commodity": "ORE"}]}
        },
        "price_cache": {"7": {"buy": {"ORE": 9}, "sell": {"ORE": None}, "quoted_at": {"ORE": time.time()}}},
    }

    assert is_goal_complete("survey: port", state, {}, "state.snapshot")


def test_plan_advances_completed_prefix_and_preserves_later_goals():
    state = {
        "player_location_sector": "42",
        "port_info_by_sector": {
            "42": {"port_id": 7, "commodities": [{"commodity": "ORE"}]}
        },
        "price_cache": {"7": {"buy": {"ORE": 9}, "sell": {"ORE": None}, "quoted_at": {"ORE": time.time()}}},
    }

    remaining = advance_completed_goals(
        ["goto: 42", "survey: port", "buy: ORE", "sell: ORE"], state
    )
    assert remaining == ["buy: ORE", "sell: ORE"]


def test_refused_warp_blacklists_hop_and_discards_stale_goto_plan():
    state = FakeStateManager(
        {
            "player_location_sector": 10,
            "session_id": "session",
            "strategy_plan": ["goto: 42", "survey: port"],
            "current_path": [10, 24, 42],
            "recent_sectors": [],
        },
        {"warp-request": {"command": "move.warp", "data": {"to_sector_id": 24}}},
    )
    connection = FakeConnection()

    process_responses(
        [{
            "reply_to": "warp-request",
            "status": "refused",
            "type": "error",
            "error": {"code": 1453, "message": "No link"},
        }],
        game_conn=connection,
        state_manager=state,
        bug_reporter=UnusedBugReporter(),
        bandit_policy=None,
        config={},
    )

    assert 24 in state.state["warp_blacklist"]
    assert state.state["current_path"] == []
    assert state.state["strategy_plan"] == ["survey: port"]
    assert {cmd["command"] for cmd in connection.sent} == {"player.my_info", "ship.info"}


def test_refused_pathfind_discards_unreachable_navigation_goal():
    state = FakeStateManager(
        {
            "player_location_sector": 10,
            "strategy_plan": ["goto: 42", "sell: ORE"],
            "current_path": [10, 24, 42],
        },
        {"path-request": {"command": "move.pathfind", "data": {"to_sector_id": 42}}},
    )

    process_responses(
        [{
            "reply_to": "path-request",
            "status": "refused",
            "type": "error",
            "error": {"code": 1453, "message": "No path"},
        }],
        game_conn=FakeConnection(),
        state_manager=state,
        bug_reporter=UnusedBugReporter(),
        bandit_policy=None,
        config={},
    )

    assert state.state["current_path"] == []
    assert state.state["strategy_plan"] == ["sell: ORE"]


def test_uncertain_trade_survives_disconnect_and_mismatched_late_reply(tmp_path):
    path = tmp_path / "state.json"
    manager = StateManager(str(path), {})
    marker = {"request_id": "buy-1", "command": "trade.buy", "idempotency_key": "key-1"}
    manager.set("uncertain_trade", marker)
    manager.add_pending_command("buy-1", {"command": "trade.buy", "data": {"port_id": 1}})
    manager.clear_connection_state()
    restored = StateManager(str(path), {})
    process_responses([{"reply_to": "buy-1", "status": "ok", "type": "player.my_info",
                        "data": {"player": {"credits": "900"}}}],
                      FakeConnection(), restored, UnusedBugReporter(), QuietBandit(), {}, None)
    assert restored.get("uncertain_trade") == marker
    assert restored.get("pending_commands") == {}


def test_stale_player_read_cannot_overwrite_state_during_uncertain_warp(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.set("player_info", {"player": {"credits": "900.00"}})
    manager.set("player_location_sector", 1)
    marker = {"request_id": "warp-1", "command": "move.warp", "to_sector_id": 2}
    manager.set("uncertain_warp", marker)

    process_responses([{"reply_to": "warp-1", "status": "ok", "type": "player.my_info",
                        "data": {"player": {"credits": "9999.00"}}}],
                      FakeConnection(), manager, UnusedBugReporter(), QuietBandit(), {}, None)

    assert manager.get("uncertain_warp") == marker
    assert manager.get("player_info")["player"]["credits"] == "900.00"


def test_valid_late_trade_receipt_reconciles_without_replay(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    marker = {"request_id": "buy-1", "command": "trade.buy", "idempotency_key": "key-1"}
    manager.set("uncertain_trade", marker)
    manager.set("ship_info", {"cargo": []})
    manager.set("player_info", {"player": {"credits": "1000"}})
    process_responses([{"reply_to": "buy-1", "status": "ok", "type": "trade.buy_receipt_v1",
                        "data": {"port_id": 1,
                                 "lines": [{"commodity": "ORE", "quantity": 1, "unit_price": 100, "value": "100"}],
                                 "credits_remaining": "900"}}],
                      FakeConnection(), manager, UnusedBugReporter(), QuietBandit(), {}, None)
    assert manager.get("uncertain_trade") is None
    assert manager.get("ship_info")["cargo"][0]["commodity"] == "ORE"


def test_malformed_late_trade_receipt_keeps_uncertainty_marker(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    marker = {"request_id": "sell-1", "command": "trade.sell"}
    manager.set("uncertain_trade", marker)
    process_responses([{"reply_to": "sell-1", "status": "ok", "type": "trade.sell_receipt_v1",
                        "data": {"lines": [{"commodity": "ORE", "quantity": 1}]}}],
                      FakeConnection(), manager, UnusedBugReporter(), QuietBandit(), {}, None)
    assert manager.get("uncertain_trade") == marker
    assert manager.get("trade_successful") is False


def test_correlated_trade_receipt_resolves_uncertain_marker(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.set("uncertain_trade", {"request_id": "buy-1", "command": "trade.buy"})
    manager.set("ship_info", {"cargo": []})
    manager.set("player_info", {"player": {"credits": "1000"}})
    manager.add_pending_command("buy-1", {"command": "trade.buy", "data": {"port_id": 1}})
    process_responses([{"reply_to": "buy-1", "status": "ok", "type": "trade.buy_receipt_v1",
                        "data": {"lines": [{"commodity": "ORE", "quantity": 1, "value": "100"}],
                                 "credits_remaining": "900"}}],
                      FakeConnection(), manager, UnusedBugReporter(), QuietBandit(), {}, None)
    assert manager.get("uncertain_trade") is None
    assert manager.get("player_info")["player"]["credits"] == "900"


def test_interrupted_warp_reconciles_after_restart_and_late_reply(tmp_path):
    path = tmp_path / "state.json"
    manager = StateManager(str(path), {})
    marker = {"request_id": "warp-1", "command": "move.warp", "to_sector_id": 2}
    manager.set("uncertain_warp", marker)
    manager.add_pending_command("warp-1", {"command": "move.warp", "data": {"to_sector_id": 2}})
    manager.clear_connection_state()
    manager = StateManager(str(path), {})
    process_responses([{"reply_to": "warp-1", "status": "ok", "type": "move.result",
                        "data": {"to_sector_id": 2}}],
                      FakeConnection(), manager, UnusedBugReporter(), QuietBandit(), {}, None)
    assert manager.get("uncertain_warp") is None
    assert manager.get("player_location_sector") == 2


def test_warp_quarantine_requires_matching_correlated_destination(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    marker = {"request_id": "warp-1", "command": "move.warp", "to_sector_id": 2}
    manager.set("uncertain_warp", marker)
    manager.add_pending_command("warp-1", {"command": "move.warp", "data": {"to_sector_id": 2}})
    process_responses([{"reply_to": "warp-1", "status": "ok", "type": "move.result",
                        "data": {"to_sector_id": 3}}],
                      FakeConnection(), manager, UnusedBugReporter(), QuietBandit(), {}, None)
    assert manager.get("uncertain_warp") == marker

    manager.add_pending_command("warp-1", {"command": "move.warp", "data": {"to_sector_id": 2}})
    process_responses([{"reply_to": "warp-1", "status": "ok", "type": "move.result",
                        "data": {"to_sector_id": 2}}],
                      FakeConnection(), manager, UnusedBugReporter(), QuietBandit(), {}, None)
    assert manager.get("uncertain_warp") is None
    assert manager.get("player_location_sector") == 2
