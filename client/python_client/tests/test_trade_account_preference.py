"""ECO-01 client contract for the server-owned default trade account."""

import client


def test_trade_account_preference_sends_authoritative_command(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9})
    fake_conn.queue("player.set_trade_account_preference", {"status": "ok", "data": {}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "b")

    client.trade_account_preference_flow(ctx)

    assert fake_conn.calls == [{"command": "player.set_trade_account_preference", "data": {"preference": 1}}]
    assert "saved as bank" in capsys.readouterr().out


def test_trade_account_preference_cancel_does_not_cross_protocol_boundary(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9})
    monkeypatch.setattr("builtins.input", lambda _prompt: "")

    client.trade_account_preference_flow(ctx)

    assert fake_conn.calls == []
    assert "unchanged" in capsys.readouterr().out


def test_trade_account_preference_refusal_is_not_reported_as_saved(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory({"id": 9})
    fake_conn.queue("player.set_trade_account_preference", {"status": "refused", "error": {"message": "Not available"}})
    monkeypatch.setattr("builtins.input", lambda _prompt: "p")

    client.trade_account_preference_flow(ctx)

    assert "not saved" in capsys.readouterr().out
