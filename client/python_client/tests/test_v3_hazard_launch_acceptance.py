"""V3-HAZ-01 client acceptance for terminal planet-launch outcomes."""

import client


def test_terminal_launch_hazard_clears_surface_and_refreshes_all_affected_state(
    ctx_factory, fake_conn, capsys
):
    """An explicit server terminal outcome must close stale surface controls."""
    ctx = ctx_factory(sector={"id": 17, "name": "Planet sector"})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "error",
        "error": {
            "message": "Entry hazard destroyed the ship.",
            "data": {
                "ship_destroyed": True,
                "ship_remains_in_sector": False,
                "hazards": [{"hazard_type": "minefield", "destroyed": True}],
            },
        },
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 38}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 0}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"sector_id": 17, "name": "Planet sector"},
    })

    client.launch_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.launch", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert ctx.state["on_planet"] is False
    assert "current_planet_id" not in ctx.state
    assert ctx.menu_stack[-1] != "SURFACE"
    output = capsys.readouterr().out
    assert "minefield" in output
    assert "ship destroyed" in output
