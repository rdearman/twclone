"""
Tests for protocol.Conn's event-queue behaviour (persistent HUD slice):

1. Conn queues unsolicited events without printing.
2. RPC replies still reach the correct caller while events are interleaved.
3. Event order is preserved.
4. Queue overflow follows the documented bounded (drop-oldest) policy.
5. Connection loss updates state and queues one client event.
16. No background thread writes to stdout (Conn never spawns a reader thread).
"""
import json
import socket
import threading

import pytest

from protocol import Conn, EventQueue, classify_event_type


def _make_conn_pair(debug=False):
    """A real socketpair with one end wrapped in Conn, the other a raw peer
    the test drives directly (writes JSON lines, reads JSON lines)."""
    a, b = socket.socketpair()
    conn = Conn(a, debug=debug)
    peer = b.makefile("rw", encoding="utf-8", newline="\n")
    return conn, peer


def _send_line(peer, obj):
    peer.write(json.dumps(obj) + "\n")
    peer.flush()


def test_conn_queues_unsolicited_events_without_printing(capsys):
    conn, peer = _make_conn_pair()
    _send_line(peer, {"type": "chat.broadcast", "data": {"message": "hi"}})
    _send_line(peer, {"reply_to": "cli-0001", "status": "ok", "data": {}})

    resp = conn.rpc("noop", {})
    # Manually undo the id mismatch below by matching Conn's own id scheme:
    assert resp is not None

    out = capsys.readouterr().out
    assert out == ""  # Conn never prints server content
    assert len(conn.events) == 1
    ev = conn.events.drain()[0]
    assert ev["category"] == "chat"
    assert ev["type"] == "chat.broadcast"


def test_rpc_reply_reaches_caller_with_interleaved_events():
    conn, peer = _make_conn_pair()

    def server():
        line = peer.readline()
        req = json.loads(line)
        _send_line(peer, {"type": "system.notice", "data": {"id": 1, "title": "T", "body": "B"}})
        _send_line(peer, {"type": "combat.hit", "data": {"message": "you were hit"}})
        _send_line(peer, {"reply_to": req["id"], "status": "ok", "data": {"value": 42}})

    t = threading.Thread(target=server, daemon=True)
    t.start()
    resp = conn.rpc("player.my_info", {})
    t.join(timeout=2)

    assert resp["status"] == "ok"
    assert resp["data"]["value"] == 42


def test_event_order_is_preserved():
    conn, peer = _make_conn_pair()

    def server():
        line = peer.readline()
        req = json.loads(line)
        for i in range(5):
            _send_line(peer, {"type": "nav.beacon", "data": {"seq": i}})
        _send_line(peer, {"reply_to": req["id"], "status": "ok", "data": {}})

    t = threading.Thread(target=server, daemon=True)
    t.start()
    conn.rpc("move.describe_sector", {})
    t.join(timeout=2)

    drained = conn.events.drain()
    assert [ev["data"]["seq"] for ev in drained] == [0, 1, 2, 3, 4]


def test_queue_overflow_drops_oldest_and_counts_dropped():
    q = EventQueue(maxlen=3)
    for i in range(5):
        q.push("unknown", "t", {"i": i})
    assert len(q) == 3
    assert q.dropped == 2
    remaining = [ev["data"]["i"] for ev in q.drain()]
    assert remaining == [2, 3, 4]  # oldest (0, 1) dropped, order preserved


def test_unknown_event_types_are_retained_not_discarded():
    q = EventQueue(maxlen=10)
    q.push(classify_event_type("some.new.thing"), "some.new.thing", {"x": 1})
    drained = q.drain()
    assert len(drained) == 1
    assert drained[0]["category"] == "unknown"


def test_connection_loss_updates_state_and_queues_one_client_event():
    conn, peer = _make_conn_pair()
    peer.close()  # peer socket closed -> readline() returns "" -> ConnectionError

    with pytest.raises(ConnectionError):
        conn.rpc("player.my_info", {})

    assert conn.connected is False
    events = conn.events.drain()
    assert len(events) == 1
    assert events[0]["category"] == "connection"
    assert events[0]["type"] == "connection.lost"


def test_no_background_thread_created_by_conn():
    before = threading.active_count()
    conn, peer = _make_conn_pair()

    def server():
        line = peer.readline()
        req = json.loads(line)
        _send_line(peer, {"reply_to": req["id"], "status": "ok", "data": {}})

    t = threading.Thread(target=server, daemon=True)
    t.start()
    conn.rpc("system.hello", {})
    t.join(timeout=2)

    # Conn itself must not have spawned any thread of its own; only our
    # explicit test-driver thread (already joined/finished) may have existed.
    assert threading.active_count() <= before + 1
