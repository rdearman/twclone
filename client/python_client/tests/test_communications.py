"""Communications rendering and safe interaction tests."""

import client


def test_comms_menu_has_clear_landing_entries(menus):
    options = {item["key"]: item for item in menus["COMMS"]["options"]}
    assert options["c"]["action"]["submenu"] == "CHAT"
    assert options["m"]["action"]["submenu"] == "MAIL"
    assert options["e"]["action"]["pycall"] == "comms_events_view"
    assert options["n"]["action"]["pycall"] == "comms_notices_view"


def test_chat_empty_and_malformed_history_are_safe(ctx_factory, fake_conn, capsys):
    fake_conn.queue("chat.history", {"status": "ok", "data": {"messages": [None, "bad"]}})
    client.cli_chat_history(ctx_factory())
    out = capsys.readouterr().out
    assert "No chat history yet." in out
    assert "{" not in out


def test_mail_inbox_uses_display_index_and_unread_marker(ctx_factory, fake_conn, monkeypatch, capsys):
    fake_conn.queue("mail.inbox", {"status": "ok", "data": {"items": [
        {"id": 44, "sender_name": "Nova", "subject": "Hello", "sent_at": "now"},
        {"id": 45, "sender_name": "Sol", "subject": "Read", "read_at": "then"},
    ]}})
    ctx = ctx_factory()
    client.mail_inbox_flow(ctx)
    assert ctx.state["mail_items"][0]["id"] == 44
    out = capsys.readouterr().out
    assert "1. [NEW]" in out
    assert "2.       From: Sol" in out

    fake_conn.queue("mail.read", {"status": "ok", "data": {
        "id": 44, "sender_name": "Nova", "subject": "Hello", "body": "Welcome", "sent_at": "now"
    }})
    monkeypatch.setattr("builtins.input", lambda _prompt: "1")
    client.mail_read_flow(ctx)
    assert fake_conn.calls[-1] == {"command": "mail.read", "data": {"id": 44}}
    assert "Welcome" in capsys.readouterr().out


def test_mail_empty_and_refusal_are_clean(ctx_factory, fake_conn, capsys):
    fake_conn.queue("mail.inbox", {"status": "ok", "data": {"items": []}})
    ctx = ctx_factory()
    client.mail_inbox_flow(ctx)
    assert "Inbox is empty." in capsys.readouterr().out
    fake_conn.queue("mail.inbox", {"status": "error", "error": {"message": "not available"}})
    client.mail_inbox_flow(ctx)
    out = capsys.readouterr().out
    assert "not available" in out
    assert '"status"' not in out


def test_notices_malformed_row_is_skipped_and_unread_is_consistent(ctx_factory, fake_conn, capsys):
    fake_conn.queue("notice.list", {"status": "ok", "data": {"items": [
        {"title": "Alert", "body": "Look", "severity": "warn"}, None,
    ]}})
    client.comms_notices_view(ctx_factory())
    out = capsys.readouterr().out
    assert "Alert [new]" in out
    assert "Unreadable notice (skipped)" in out
    assert '"title"' not in out
