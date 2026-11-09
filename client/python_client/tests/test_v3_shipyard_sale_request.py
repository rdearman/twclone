"""V3-SHIP-02: shipyard sale uses the server-owned request identity."""

import client


def test_shipyard_sale_submits_authoritative_ship_id_and_surfaces_server_refusal(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.player_info = {"ship_id": 7}
    fake_conn.queue("shipyard.sell", {
        "status": "refused",
        "error": {"message": "Not implemented: shipyard.sell"},
    })
    fake_conn.queue("player.my_info", {"status": "ok", "type": "player.info", "data": {"ship_id": 7}})
    fake_conn.queue("ship.status", {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 7}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "type": "sector.info", "data": {"id": 1}})
    monkeypatch.setattr("builtins.input", lambda *_: "y")

    client.shipyard_sell_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "shipyard.sell", "player.my_info", "ship.status", "move.describe_sector"
    ]
    assert fake_conn.calls[0]["data"] == {"ship_id": 7}
    assert "Not implemented: shipyard.sell" in capsys.readouterr().out


def test_shipyard_sale_does_not_claim_success_without_published_receipt(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.player_info = {"ship_id": 7}
    fake_conn.queue("shipyard.sell", {"status": "ok", "data": {"ship_id": 7}})
    monkeypatch.setattr("builtins.input", lambda *_: "y")

    client.shipyard_sell_flow(ctx)

    assert "was not confirmed" in capsys.readouterr().out
