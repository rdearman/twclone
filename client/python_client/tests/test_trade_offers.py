"""Client-facing player trade offer safety and contract tests."""

import re

import client


def test_trade_offer_create_sends_documented_fields_and_idempotency_key(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    fake_conn.queue("trade.offer", {"status": "ok", "data": {
        "offer_id": 93, "status": "pending",
    }})
    answers = iter(["8", "ore", "3", "0", "sell", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.trade_offer_create_flow(ctx_factory())

    call = fake_conn.calls[-1]
    assert call["command"] == "trade.offer"
    sent_data = dict(call["data"])
    key = sent_data.pop("idempotency_key")
    assert sent_data == {
        "to_player_id": 8, "commodity": "ORE", "quantity": 3,
        "price": 0, "mode": "sell",
    }
    assert re.fullmatch(r"[0-9a-f-]{36}", key)
    output = capsys.readouterr().out
    assert "do not reserve cargo or credits" in output
    assert "Trade offer 93 created" in output


def test_trade_offer_invalid_fields_or_cancel_do_not_send(ctx_factory, fake_conn, monkeypatch, capsys):
    answers = iter(["8", "ORE", "2", "-1", "sell"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))
    client.trade_offer_create_flow(ctx_factory())
    assert fake_conn.calls == []
    assert "no request was sent" in capsys.readouterr().out

    answers = iter(["8", "ORG", "2", "4", "buy", "n"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))
    client.trade_offer_create_flow(ctx_factory())
    assert fake_conn.calls == []
    assert "cancelled" in capsys.readouterr().out


def test_trade_offer_accept_and_cancel_use_server_ids_and_confirmation(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    fake_conn.queue("trade.accept", {"status": "ok", "data": {
        "offer_id": 93, "status": "accepted",
    }})
    fake_conn.queue("trade.cancel", {"status": "ok", "data": {
        "offer_id": 94, "status": "cancelled",
    }})
    answers = iter(["93", "y", "94", "y"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))

    client.trade_offer_accept_flow(ctx_factory())
    client.trade_offer_cancel_flow(ctx_factory())

    assert fake_conn.calls == [
        {"command": "trade.accept", "data": {"offer_id": 93}},
        {"command": "trade.cancel", "data": {"offer_id": 94}},
    ]
    output = capsys.readouterr().out
    assert "offer 93: accepted" in output
    assert "offer 94: cancelled" in output


def test_trade_offer_actions_do_not_send_without_confirmation(ctx_factory, fake_conn, monkeypatch, capsys):
    answers = iter(["93", "n"])
    monkeypatch.setattr("builtins.input", lambda _prompt: next(answers))
    client.trade_offer_accept_flow(ctx_factory())
    assert fake_conn.calls == []
    assert "cancelled" in capsys.readouterr().out
