"""V3-SHIP-02 client acceptance for ship-roster transport failures."""

import client


def test_ship_roster_malformed_reply_enters_typed_recovery(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", None)
    fake_conn.queue("ship.status", {
        "status": "ok",
        "type": "ship.status",
        "data": {"ship": {"id": 7, "name": "Confirmed"}},
    })

    client.ship_list_flow(ctx)

    output = capsys.readouterr().out
    assert "invalid response" in output
    assert fake_conn.calls == [
        {"command": "ship.list", "data": {}},
        {"command": "ship.status", "data": {}},
    ]


def test_ship_roster_disconnect_does_not_crash_or_claim_empty_roster(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()

    def disconnect(_command, _data):
        raise ConnectionError("socket closed")

    monkeypatch.setattr(fake_conn, "rpc", disconnect)

    client.ship_list_flow(ctx)

    output = capsys.readouterr().out
    assert "could not be confirmed after connection loss" in output
    assert "No authoritative ships were returned" not in output
    assert ctx.state["last_rpc"] is None
