"""Communications rendering and safe interaction tests."""

import client


def test_comms_menu_has_clear_landing_entries(menus):
    options = {item["key"]: item for item in menus["COMMS"]["options"]}
    assert options["c"]["action"]["submenu"] == "CHAT"
    assert options["m"]["action"]["submenu"] == "MAIL"
    assert options["e"]["action"]["pycall"] == "comms_events_view"
    assert options["n"]["action"]["pycall"] == "comms_notices_view"
    assert options["a"]["action"]["pycall"] == "comms_notice_ack_flow"
    assert options["b"]["action"]["pycall"] == "comms_bounty_board"
    assert options["t"]["action"]["submenu"] == "PLAYER_TRADES"


def test_player_trade_menu_exposes_documented_operations(menus):
    options = {item["key"]: item for item in menus["PLAYER_TRADES"]["options"]}
    assert options["c"]["action"]["pycall"] == "trade_offer_create_flow"
    assert options["a"]["action"]["pycall"] == "trade_offer_accept_flow"
    assert options["x"]["action"]["pycall"] == "trade_offer_cancel_flow"


def test_chat_empty_and_malformed_history_are_safe(ctx_factory, fake_conn, capsys):
    fake_conn.queue("chat.history", {"status": "ok", "data": {"messages": [None, "bad"]}})
    client.cli_chat_history(ctx_factory())
    out = capsys.readouterr().out
    assert "No chat history yet." in out
    assert "{" not in out


def test_private_chat_validates_recipient_and_refreshes_history(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    fake_conn.queue("chat.send", {"status": "ok", "data": {"chat_id": 91}})
    fake_conn.queue("chat.history", {"status": "ok", "data": {
        "messages": [{"sender_name": "Me", "recipient_name": "Nova", "message": "Hello"}],
    }})
    answers = iter(["Hello", "player", "Nova"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))
    ctx = ctx_factory()

    client.cli_chat_send(ctx)

    assert fake_conn.calls == [
        {"command": "chat.send", "data": {"to_player": "Nova", "message": "Hello"}},
        {"command": "chat.history", "data": {"limit": 20}},
    ]
    assert ctx.state["chat_messages"][0]["recipient_name"] == "Nova"
    assert "Message sent." in capsys.readouterr().out


def test_private_chat_refusal_does_not_refresh_history(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    fake_conn.queue("chat.send", {"status": "error", "error": {"message": "Recipient not found"}})
    answers = iter(["Hello", "player", "Missing"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.cli_chat_send(ctx_factory())

    assert fake_conn.calls == [{
        "command": "chat.send", "data": {"to_player": "Missing", "message": "Hello"},
    }]
    assert "Recipient not found" in capsys.readouterr().out


def test_chat_rejects_unknown_scope_before_rpc(ctx_factory, fake_conn, monkeypatch, capsys):
    answers = iter(["Hello", "orbit"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.cli_chat_send(ctx_factory())

    assert fake_conn.calls == []
    assert "Unknown scope" in capsys.readouterr().out


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


def test_notice_ack_only_submits_a_listed_unread_id_and_confirms_server_result(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    fake_conn.queue("notice.list", {"status": "ok", "data": {"items": [
        {"id": 71, "title": "New", "seen_at": None},
        {"id": 72, "title": "Already read", "seen_at": "2026-10-29T10:00:00Z"},
    ]}})
    fake_conn.queue("notice.ack", {"status": "ok", "data": {"id": 71, "seen_at": 1761732000}})
    answers = iter(["71", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.comms_notice_ack_flow(ctx_factory())

    assert fake_conn.calls == [
        {"command": "notice.list", "data": {}},
        {"command": "notice.ack", "data": {"notice_id": 71}},
    ]
    output = capsys.readouterr().out
    assert "ID 71: New" in output
    assert "ID 72" not in output
    assert "Notice 71 acknowledged (confirmed by server)." in output


def test_notice_ack_rejects_unlisted_id_without_mutation(ctx_factory, fake_conn, monkeypatch, capsys):
    fake_conn.queue("notice.list", {"status": "ok", "data": {"items": [
        {"id": 71, "title": "New", "seen_at": None},
    ]}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "999")

    client.comms_notice_ack_flow(ctx_factory())

    assert fake_conn.calls == [{"command": "notice.list", "data": {}}]
    assert "not an unread notice" in capsys.readouterr().out


def test_notice_ack_displays_server_refusal(ctx_factory, fake_conn, monkeypatch, capsys):
    fake_conn.queue("notice.list", {"status": "ok", "data": {"items": [
        {"id": 71, "title": "New", "seen_at": None},
    ]}})
    fake_conn.queue("notice.ack", {"status": "error", "error": {"message": "not found"}})
    answers = iter(["71", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.comms_notice_ack_flow(ctx_factory())

    assert fake_conn.calls[-1] == {"command": "notice.ack", "data": {"notice_id": 71}}
    assert "Notice acknowledgement refused: not found" in capsys.readouterr().out


def test_bounty_board_displays_authoritative_rows_and_empty_state(ctx_factory, fake_conn, capsys):
    fake_conn.queue("bounty.list", {"status": "ok", "data": {"bounties": [
        {"bounty_id": 19, "target_id": 7, "target_name": "Ranger", "reward": 2500},
        None,
        {"bounty_id": "bad", "target_id": 8, "reward": 4},
    ]}})
    client.comms_bounty_board(ctx_factory())
    output = capsys.readouterr().out
    assert "#19: Ranger — 2,500 CR" in output
    assert "Player 8" not in output

    fake_conn.queue("bounty.list", {"status": "ok", "data": {"bounties": []}})
    client.comms_bounty_board(ctx_factory())
    assert "There are no active bounties." in capsys.readouterr().out
