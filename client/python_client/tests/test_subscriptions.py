"""
Tests for subscription list/add/remove using the implemented topic/topics
contract (src/server_communication.c), not the draft event_type/items shape.
"""
import client


def test_subscriptions_list_flow_reads_topics_field(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("subscribe.list", {
        "status": "ok",
        "data": {"topics": [
            {"topic": "system.notice", "locked": True, "enabled": True},
            {"topic": "chat.global", "locked": False, "enabled": True},
        ]},
    })

    client.subscriptions_list_flow(ctx)

    out = capsys.readouterr().out
    assert "system.notice" in out
    assert "(locked)" in out
    assert "chat.global" in out
    assert fake_conn.calls == [{"command": "subscribe.list", "data": {}}]


def test_subscriptions_add_flow_sends_topic_field(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("subscribe.catalog", {
        "status": "ok",
        "data": {"topics": [
            {"pattern": "system.notice", "kind": "exact", "desc": "Server notices"},
        ]},
    })
    fake_conn.queue("subscribe.add", {"status": "ok", "data": {"topic": "system.notice"}})

    monkeypatch.setattr("builtins.input", lambda *_: "1")

    client.subscriptions_add_flow(ctx)

    add_call = [c for c in fake_conn.calls if c["command"] == "subscribe.add"][0]
    assert add_call["data"] == {"topic": "system.notice"}
    assert "event_type" not in add_call["data"]
    out = capsys.readouterr().out
    assert "Subscribed to system.notice." in out


def test_subscriptions_add_flow_prompts_for_concrete_topic_on_wildcard(ctx_factory, fake_conn, monkeypatch):
    ctx = ctx_factory()
    fake_conn.queue("subscribe.catalog", {
        "status": "ok",
        "data": {"topics": [{"pattern": "sector.*", "kind": "wildcard", "desc": "Sector events"}]},
    })
    fake_conn.queue("subscribe.add", {"status": "ok", "data": {"topic": "sector.42"}})

    inputs = iter(["1", "sector.42"])
    monkeypatch.setattr("builtins.input", lambda *_: next(inputs))

    client.subscriptions_add_flow(ctx)

    add_call = [c for c in fake_conn.calls if c["command"] == "subscribe.add"][0]
    assert add_call["data"] == {"topic": "sector.42"}


def test_subscriptions_remove_flow_sends_topic_field(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("subscribe.remove", {"status": "ok", "data": {}})
    monkeypatch.setattr("builtins.input", lambda *_: "chat.global")

    client.subscriptions_remove_flow(ctx)

    remove_call = [c for c in fake_conn.calls if c["command"] == "subscribe.remove"][0]
    assert remove_call["data"] == {"topic": "chat.global"}
    out = capsys.readouterr().out
    assert "Unsubscribed from chat.global." in out


def test_subscriptions_remove_flow_reports_locked_topic_refusal(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("subscribe.remove", {
        "status": "refused",
        "error": {"code": 1407, "message": "Topic is locked."},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "system.notice")

    client.subscriptions_remove_flow(ctx)

    out = capsys.readouterr().out
    assert "locked" in out.lower()
