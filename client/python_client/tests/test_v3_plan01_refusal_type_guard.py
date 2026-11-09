"""V3-PLAN-01: unrelated response types cannot be shown as landing refusals."""

import client


def test_landing_refusal_with_unrelated_type_only_reconciles_state(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(
        sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    )
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 66
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.land", {
        "status": "error", "type": "ship.status",
        "error": {"message": "not a landing response"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3, "turns": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 9, "onplanet": 0}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is False
    assert "current_planet_id" not in ctx.state
    assert ctx.menu_stack == ["MAIN"]
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert "unexpected type ship.status" in capsys.readouterr().out
