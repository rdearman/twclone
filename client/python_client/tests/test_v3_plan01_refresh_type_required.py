"""V3-PLAN-01: refresh responses must carry the authoritative schema type."""

import client


def test_landing_success_does_not_authorize_untyped_player_refresh(
    ctx_factory, fake_conn, capsys
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
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 9, "onplanet": 77}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"id": 17, "planets": [{"id": 77}]},
    })

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert ctx.state.get("current_planet_id") == 77
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out


def test_landing_success_does_not_authorize_untyped_ship_refresh(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "type": "planet.land.success",
        "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3, "ship_id": 9, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "data": {"ship": {"id": 9, "onplanet": 77}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"id": 17, "planets": [{"id": 77}]},
    })

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert ctx.state.get("current_planet_id") == 77
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out
