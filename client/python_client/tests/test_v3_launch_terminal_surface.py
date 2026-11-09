"""V3-HAZ-01: terminal launch outcomes invalidate the planet surface."""

import client


def test_terminal_launch_closes_surface_and_refreshes_all_affected_domains(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Current"})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "refused",
        "error": {
            "message": "The ship was destroyed during launch.",
            "data": {
                "ship_destroyed": True,
                "ship_remains_in_sector": False,
                "hazards": [{"hazard_type": "minefield", "destroyed": True}],
            },
        },
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 37}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "hull": 0}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77}]},
    })

    client.launch_flow(ctx)

    output = capsys.readouterr().out
    assert "minefield" in output
    assert "Launch outcome is terminal" in output
    assert ctx.state["on_planet"] is False
    assert "current_planet_id" not in ctx.state
    assert ctx.menu_stack == ["MAIN"]
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.launch", "player.my_info", "ship.status", "move.describe_sector",
    ]
