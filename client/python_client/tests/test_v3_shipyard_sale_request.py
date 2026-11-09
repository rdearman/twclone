"""V3-SHIP-02: shipyard sale uses the server-owned request identity."""

import client


def test_shipyard_sale_requires_owned_replacement_and_surfaces_server_refusal(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.player_info = {"ship_id": 7}
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [
            {"ship_id": 7, "name": "Current", "active": True},
            {"ship_id": 8, "name": "Replacement", "active": False},
        ]},
    })
    fake_conn.queue("shipyard.sell", {
        "status": "refused",
        "error": {"message": "Not at an authorized shipyard"},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "type": "player.info", "data": {"ship_id": 7}})
    fake_conn.queue("ship.status", {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 7}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "type": "sector.info", "data": {"id": 1}})
    answers = iter(["8", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.shipyard_sell_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "ship.list", "shipyard.sell", "player.my_info", "ship.status", "move.describe_sector"
    ]
    assert fake_conn.calls[1]["data"] == {"ship_id": 7, "replacement_ship_id": 8}
    assert "Not at an authorized shipyard" in capsys.readouterr().out


def test_shipyard_sale_confirms_receipt_and_reconciles_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.player_info = {"ship_id": 7}
    fake_conn.queue("ship.list", {
        "status": "ok", "type": "ship.list.response",
        "data": {"ships": [
            {"ship_id": 7, "name": "Current", "active": True},
            {"ship_id": 8, "name": "Replacement", "active": False},
        ]},
    })
    fake_conn.queue("shipyard.sell", {
        "status": "ok", "type": "ship.sold_v1",
        "data": {"ship_id": 7, "replacement_ship_id": 8, "sale_proceeds": 900,
                 "cargo_forfeited": True, "insurance_terminated": True},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "type": "player.info", "data": {"ship_id": 8}})
    fake_conn.queue("ship.status", {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 8}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "type": "sector.info", "data": {"id": 1}})
    answers = iter(["8", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.shipyard_sell_flow(ctx)

    assert "Ship sale confirmed" in capsys.readouterr().out
