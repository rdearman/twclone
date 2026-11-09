"""V3-PLAN-01: explicit terminal landing outcomes invalidate stale surfaces."""

import client


def test_terminal_landing_refusal_closes_stale_surface_before_refresh(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.land", {
        "status": "refused",
        "error": {
            "message": "Planetary defenses destroyed the ship.",
            "data": {"ship_destroyed": True, "ship_remains_in_sector": False},
        },
    })
    fake_conn.queue("player.my_info", {"status": "ok", "data": {"player": {"id": 3}}})
    fake_conn.queue("ship.status", {"status": "ok", "data": {"ship": {"id": 9}}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"id": 17, "planets": []}})

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is False
    assert "current_planet_id" not in ctx.state
    assert ctx.menu_stack == ["MAIN"]
    assert "terminal" in capsys.readouterr().out
