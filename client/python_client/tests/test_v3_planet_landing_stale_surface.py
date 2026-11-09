"""V3-PLAN-01 prevents ambiguous landing replies from exposing stale actions."""

import client


def test_ambiguous_landing_reply_invalidates_stale_surface_before_refresh(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 12
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success", "data": {"message": "Landed"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "onplanet": 12}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77}]},
    })

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is False
    assert "current_planet_id" not in ctx.state
    assert ctx.menu_stack == ["MAIN"]
    assert "did not return the requested planet ID" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
