"""V3-SHIP-02 client handling for server-provided ship roster state."""

import client


def test_roster_without_active_ship_preserves_server_rows(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [{"ship_id": 7, "name": "Stored Hauler", "active": False}]},
    })
    client.ship_list_flow(ctx)

    output = capsys.readouterr().out
    assert "Stored Hauler" in output
    assert "inactive" in output
    assert fake_conn.calls == [{"command": "ship.list", "data": {}}]
