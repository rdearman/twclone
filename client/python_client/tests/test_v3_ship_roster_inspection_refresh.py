"""V3-SHIP-02 refreshes stale ship selection after inspection ambiguity."""

import client


def test_ship_inspection_mismatch_refreshes_active_ship(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 7, "name": "Hauler", "active": True}]},
    })
    fake_conn.queue("ship.inspect", {
        "status": "ok", "type": "ship.inspect",
        "data": {"ship": {"ship_id": 99, "name": "Stale vessel"}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 7, "name": "Confirmed Hauler"}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "1")

    client.ship_roster_inspect_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "ship.list", "ship.inspect", "ship.status",
    ]
    assert "different ship ID" in capsys.readouterr().out

