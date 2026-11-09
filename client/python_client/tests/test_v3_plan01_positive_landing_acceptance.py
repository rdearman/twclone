"""V3-PLAN-01 positive client journey against the authoritative landing contract."""

import client


def test_typed_landing_refreshes_player_and_active_ship_before_surface(
    ctx_factory, fake_conn, monkeypatch
):
    """A confirmed landing is playable only after both owned refreshes agree."""
    ctx = ctx_factory(
        sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]}
    )
    fake_conn.queue("planet.land", {
        "status": "ok",
        "type": "planet.land.success",
        "data": {"planet_id": 77, "message": "Landed successfully."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "type": "player.info",
        "data": {"player": {"id": 3, "ship_id": 9, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "type": "ship.status",
        "data": {"ship": {"ship_id": 9, "onplanet": 77, "hull": 100}},
    })
    fake_conn.queue("planet.info", {
        "status": "ok",
        "type": "planet.info",
        "data": {"planet": {"planet_id": 77, "sector_id": 17}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack[-1] == "SURFACE"
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "planet.info",
    ]
