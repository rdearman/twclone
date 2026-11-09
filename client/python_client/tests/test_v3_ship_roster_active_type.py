"""V3-SHIP-02 rejects malformed active-ship flags in the owned roster."""

import client


def test_ship_roster_rejects_non_boolean_active_flag(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory()
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [
            {"ship_id": 7, "name": "Hauler", "active": True},
            {"ship_id": 9, "name": "Scout", "active": "false"},
        ]},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 7, "name": "Confirmed"}},
    })

    client.ship_list_flow(ctx)

    output = capsys.readouterr().out
    assert "Scout" not in output
    assert "ambiguous ownership or active-ship state" in output
    assert fake_conn.calls[-1] == {"command": "ship.status", "data": {}}
