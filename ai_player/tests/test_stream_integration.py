import json
import sys
from pathlib import Path

AI_PLAYER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(AI_PLAYER_DIR))

from main import GameConnection, process_responses  # noqa: E402
from state_manager import StateManager  # noqa: E402


class FakeStreamSocket:
    def __init__(self, initial=b"", recv_chunk=4096, send_chunk=4096):
        self.incoming = bytearray(initial)
        self.outgoing = bytearray()
        self.recv_chunk = recv_chunk
        self.send_chunk = send_chunk
        self.closed = False

    def recv(self, _maximum):
        if self.incoming:
            size = min(self.recv_chunk, len(self.incoming))
            value = bytes(self.incoming[:size])
            del self.incoming[:size]
            return value
        if self.closed:
            return b""
        raise BlockingIOError()

    def send(self, value):
        size = min(self.send_chunk, len(value))
        self.outgoing.extend(value[:size])
        return size

    def close(self):
        self.closed = True


class QuietReporter:
    def report_state_inconsistency(self, *_args):
        raise AssertionError("unexpected state inconsistency")


class QuietBandit:
    def give_feedback(self, *_args):
        pass


def test_stream_handles_fragmented_and_coalesced_json_frames(monkeypatch):
    connection = GameConnection("unused", 0)
    fake_socket = FakeStreamSocket(b'{"type":"system.notice","data":', recv_chunk=128)
    connection.sock = fake_socket
    monkeypatch.setattr("main.select.select", lambda _r, w, _e, _t: ([], w, []))
    assert connection.receive_responses() == []
    fake_socket.incoming.extend(b'{"body":"hi"}}\n{"reply_to":"r1","status":"ok"}\n')

    frames = connection.receive_responses()
    assert [frame.get("type") for frame in frames] == ["system.notice", None]
    assert frames[1]["reply_to"] == "r1"
    connection.disconnect()


def test_send_command_queues_partial_writes_until_frame_is_complete(monkeypatch):
    connection = GameConnection("unused", 0)
    fake_socket = FakeStreamSocket(send_chunk=4)
    connection.sock = fake_socket
    monkeypatch.setattr("main.select.select", lambda _r, w, _e, _t: ([], w, []))
    assert connection.send_command({"id": "r1", "command": "player.ping", "data": {}})
    while connection._send_buffer:
        connection.receive_responses()
    received = bytes(fake_socket.outgoing)
    assert json.loads(received.decode("utf-8")) == {
        "id": "r1", "command": "player.ping", "data": {}
    }
    assert received.endswith(b"\n")
    connection.disconnect()


def test_oversized_unterminated_server_frame_disconnects():
    connection = GameConnection("unused", 0)
    connection.max_frame_bytes = 8
    connection.sock = FakeStreamSocket(b"x" * 9)
    assert connection.receive_responses() == []
    assert connection.sock is None
    assert connection.consume_disconnect()


def test_unsolicited_event_is_deduplicated_and_requests_refresh(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.state["player_location_sector"] = 42
    manager.state["sector_data"]["42"] = {"_last_refreshed": 123}
    notice = {
        "id": "evt-1",
        "type": "sector.player_entered",
        "data": {"sector_id": 42, "player_id": 900},
    }

    assert manager.ingest_server_event(notice) is True
    assert manager.ingest_server_event(notice) is False
    assert manager.take_event_refresh_request() == "sector.info"
    assert manager.state["sector_data"]["42"]["_last_refreshed"] == 0
    assert manager.state["ai_metrics"]["events_seen"] == 1
    assert manager.state["ai_metrics"]["duplicate_events_ignored"] == 1


def test_combat_event_requests_authoritative_ship_and_combat_refresh(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    assert manager.ingest_server_event({
        "id": "evt-damage", "type": "combat.ship_damage",
        "data": {"target_ship_id": 5, "is_player": True, "damage": 20},
    })
    assert manager.take_event_refresh_request() == "combat.status"
    assert manager.take_event_refresh_request() == "ship.info"


def test_late_unmatched_reply_is_ignored_after_timeout(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.state["last_action_result"] = {"status": "ok", "command": "old"}

    process_responses(
        [{"reply_to": "expired-request", "status": "ok", "type": "player.info",
          "data": {"credits": 999999}}],
        None, manager, QuietReporter(), QuietBandit(), {}, None,
    )

    assert manager.state["last_action_result"] == {"status": "ok", "command": "old"}
    assert manager.state["player_info"] is None


def test_autopilot_route_reply_restores_server_path(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    command = {
        "id": "route-request", "command": "move.autopilot.start",
        "data": {"from_sector_id": 1, "to_sector_id": 8},
    }
    manager.add_pending_command("route-request", command)

    process_responses(
        [{"reply_to": "route-request", "status": "ok", "type": "move.autopilot.route_v1",
          "data": {"path": [1, 4, 8], "state": "running", "target_sector_id": 8}}],
        None, manager, QuietReporter(), QuietBandit(), {}, None,
    )

    assert manager.get("current_path") == [1, 4, 8]
    assert manager.get("autopilot_status")["state"] == "running"


def test_offline_warp_cycle_tracks_planned_vs_completed_hops(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    connection = type("FakeConnection", (), {"send_command": lambda self, cmd: True})()
    manager.add_pending_command("warp-1", {"command": "move.warp", "data": {"to_sector_id": 4}})
    manager.state["current_path"] = [1, 4, 8]
    manager.record_ai_metric("planned_route_hops", 2)

    process_responses(
        [{"reply_to": "warp-1", "status": "ok", "type": "move.result",
          "data": {"to_sector_id": 4}}],
        connection, manager, QuietReporter(), QuietBandit(), {}, None,
    )
    manager.add_pending_command("warp-2", {"command": "move.warp", "data": {"to_sector_id": 8}})
    process_responses(
        [{"reply_to": "warp-2", "status": "ok", "type": "move.result",
          "data": {"to_sector_id": 8}}],
        connection, manager, QuietReporter(), QuietBandit(), {}, None,
    )

    metrics = manager.get("ai_metrics")
    assert metrics["planned_route_hops"] == 2
    assert metrics["completed_warps"] == 2
    assert manager.get("current_path") == [8]


def test_trade_receipt_updates_realized_performance_metrics(tmp_path):
    manager = StateManager(str(tmp_path / "state.json"), {})
    manager.state["price_cache"] = {"202": {"sell": {"ORE": 150}}}
    manager.state["ship_info"] = {
        "cargo": [{"commodity": "ORE", "quantity": 2, "purchase_price": 100}]
    }
    manager.add_pending_command("sell-1", {
        "command": "trade.sell", "data": {"port_id": 202}
    })

    process_responses(
        [{"reply_to": "sell-1", "status": "ok", "type": "trade.sell_receipt_v1",
          "data": {"lines": [{"commodity": "ORE", "quantity": 2}],
                   "credits_remaining": 300}}],
        None, manager, QuietReporter(), QuietBandit(), {}, None,
    )

    metrics = manager.get("ai_metrics")
    assert metrics["trade_profit_credits"] == 100
    assert metrics["trade_sales"] == 1
    assert manager.get("ship_info")["cargo"] == []
