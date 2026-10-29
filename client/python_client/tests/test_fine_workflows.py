"""Computer fine actions use the authenticated player's published server data."""
import client


def test_fine_list_renders_authoritative_ids_amounts_and_reasons(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("fine.list", {"status": "ok", "type": "fine.list", "data": {
        "fines": [{"id": 12, "amount": 500, "reason": "Illegal trade", "status": "unpaid"}],
    }})

    client.fine_list_flow(ctx)

    assert fake_conn.calls == [{"command": "fine.list", "data": {}}]
    out = capsys.readouterr().out
    assert "ID 12" in out and "500 CR" in out and "Illegal trade" in out and "unpaid" in out


def test_pay_fine_requires_listed_id_and_confirms_then_refreshes_credits(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("fine.list", {"status": "ok", "data": {
        "fines": [{"id": 12, "amount": 500, "reason": "Illegal trade", "status": "unpaid"}],
    }})
    fake_conn.queue("fine.pay", {"status": "ok", "type": "fine.pay.success", "data": {
        "fine_id": 12, "amount_paid": 500, "message": "Fine paid successfully.",
    }})
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"credits": "9500"}}})
    answers = iter(["12", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.fine_pay_flow(ctx)

    assert fake_conn.calls == [
        {"command": "fine.list", "data": {}},
        {"command": "fine.pay", "data": {"fine_id": 12}},
        {"command": "player.my_info", "data": {}},
    ]
    assert ctx.player_info == {"player": {"credits": "9500"}}
    assert "Fine 12 paid: 500 CR (confirmed by server)." in capsys.readouterr().out


def test_pay_fine_does_not_send_unlisted_id(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("fine.list", {"status": "ok", "data": {"fines": [{"id": 12, "status": "unpaid"}]}})
    answers = iter(["13"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.fine_pay_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["fine.list"]
    assert "not in the server's current outstanding fine list" in capsys.readouterr().out


def test_pay_fine_displays_server_refusal_without_refresh(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory()
    fake_conn.queue("fine.list", {"status": "ok", "data": {"fines": [{"id": 12, "status": "unpaid"}]}})
    fake_conn.queue("fine.pay", {"status": "refused", "error": {"message": "Insufficient credits."}})
    answers = iter(["12", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.fine_pay_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["fine.list", "fine.pay"]
    assert "Insufficient credits" in capsys.readouterr().out
