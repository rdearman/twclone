"""V3-PLAN-01 requires an authoritative player identity before surface access."""

import client


def test_landing_refresh_without_player_id_stays_non_actionable(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"turns_remaining": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "onplanet": 77}},
    })

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is False
    assert ctx.menu_stack[-1] != "SURFACE"
    assert ctx.hud.turns_remaining is None
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out


def test_landing_refresh_without_player_id_does_not_merge_partial_hud(
    ctx_factory, fake_conn, capsys
):
    """A partial player refresh cannot replace confirmed landing HUD state."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.hud = ctx.hud.__class__(turns_remaining=40, credits=1200)
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success", "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"turns_remaining": 39, "credits": "999.00"}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "onplanet": 77}},
    })

    client.land_flow(ctx)

    assert ctx.hud.turns_remaining == 40
    assert ctx.hud.credits == 1200
    assert ctx.state["on_planet"] is False
    assert "player refresh did not identify" in capsys.readouterr().out
