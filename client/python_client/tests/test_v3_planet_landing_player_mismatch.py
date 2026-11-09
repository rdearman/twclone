"""V3-PLAN-01 acceptance for mismatched post-landing player refreshes."""

import client


def test_landing_does_not_open_surface_for_mismatched_player_refresh(
    ctx_factory, fake_conn, capsys
):
    """A refresh for another player cannot authorize planetary actions."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.state["player_id"] = 3
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 4, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "onplanet": 77}},
    })

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is False
    assert ctx.menu_stack == ["MAIN"]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]
