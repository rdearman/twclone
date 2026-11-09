"""V3-PLAN-01 acceptance for the authoritative landing result."""

import client


def test_typed_landing_response_uses_nested_authoritative_id_and_refreshes(
    ctx_factory, fake_conn, monkeypatch
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success",
        "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "onplanet": 77}},
    })
    fake_conn.queue("planet.info", {
        "status": "ok", "data": {"planet": {"planet_id": 77, "sector_id": 17}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "planet.info",
    ]
