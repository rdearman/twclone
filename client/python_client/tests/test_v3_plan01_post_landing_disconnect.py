"""V3-PLAN-01 acceptance for an interrupted post-landing refresh."""

import client


def test_landing_success_does_not_open_surface_when_player_refresh_disconnects(
    ctx_factory, fake_conn, capsys
):
    """A confirmed landing reply is not enough to authorize a stale surface."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.player_info = {"player": {"id": 3, "turns": 40}}
    ctx.state["on_planet"] = False
    ctx.state["current_planet_id"] = 12

    fake_conn.queue("planet.land", {
        "status": "ok",
        "type": "planet.land.success",
        "data": {"planet_id": 77},
    })

    def disconnect_on_player_refresh(command, _data):
        if command == "planet.land":
            return fake_conn._queued[command].pop(0)
        raise ConnectionError("socket closed during player refresh")

    fake_conn.rpc = disconnect_on_player_refresh

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is False
    assert ctx.state["current_planet_id"] == 77
    assert ctx.player_info == {"player": {"id": 3, "turns": 40}}
    assert ctx.menu_stack[-1] != "SURFACE"
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out
