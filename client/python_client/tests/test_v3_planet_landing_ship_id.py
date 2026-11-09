"""V3-PLAN-01 acceptance for authoritative post-landing ship identity."""

import client


def test_landing_does_not_open_surface_without_authoritative_ship_id(
    ctx_factory, fake_conn, capsys
):
    """A partial ship refresh must not make a newly landed surface actionable."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"hull": 100}},
    })
    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]
    assert ctx.menu_stack[-1] != "SURFACE"
    assert ctx.state["current_planet_id"] == 77
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out
