"""V3-HAZ-01: launch success must correlate to the launch response schema."""

import client


def test_mismatched_launch_success_does_not_close_planet_surface(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Planet sector"})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "ok",
        "type": "trade.buy.success",
        "data": {"in_space": True},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 9}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"sector_id": 17, "name": "Planet sector"},
    })

    client.launch_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.menu_stack[-1] == "SURFACE"
    assert "Launched from planet." not in capsys.readouterr().out


def test_missing_launch_response_type_does_not_close_planet_surface(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Planet sector"})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {"status": "ok", "data": {"in_space": True}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 3},
        },
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 17},
    })

    client.launch_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.menu_stack[-1] == "SURFACE"
    assert "Launched from planet." not in capsys.readouterr().out


def test_mismatched_launch_success_does_not_render_untrusted_hazards(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Planet sector"})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "ok",
        "type": "trade.buy.success",
        "data": {
            "in_space": True,
            "hazards": [{"hazard_type": "mines", "damage": 99}],
        },
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 9}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"sector_id": 17, "name": "Planet sector"},
    })

    client.launch_flow(ctx)

    output = capsys.readouterr().out
    assert "mines" not in output
    assert "damage: 99" not in output


def test_mismatched_launch_terminal_fields_do_not_invalidate_surface(
    ctx_factory, fake_conn, capsys
):
    """Terminal state must be correlated to planet.launch before it is used."""
    ctx = ctx_factory(sector={"id": 17, "name": "Planet sector"})
    ctx.state["on_planet"] = True
    ctx.state["current_planet_id"] = 77
    ctx.menu_stack.append("SURFACE")
    fake_conn.queue("planet.launch", {
        "status": "refused",
        "type": "trade.buy.error",
        "data": {
            "ship_destroyed": True,
            "hazards": [{"hazard_type": "mines", "destroyed": True}],
        },
        "error": {"message": "not a launch response"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 9}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"sector_id": 17, "name": "Planet sector"},
    })

    client.launch_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack[-1] == "SURFACE"
    assert "terminal" not in capsys.readouterr().out.lower()
