"""V3-PLAN-01 client guard for malformed post-landing ship identity."""

import client


def test_landing_does_not_open_surface_for_boolean_onplanet_refresh(
    ctx_factory, fake_conn, capsys
):
    """A JSON boolean is not an authoritative landed-planet ID."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok",
        "type": "planet.land.success",
        "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "type": "ship.status",
        "data": {"ship": {"ship_id": 9, "onplanet": True, "hull": 100}},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]
    assert ctx.state.get("on_planet") is False
    assert ctx.state.get("current_planet_id") == 77
    assert ctx.menu_stack[-1] != "SURFACE"
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out
