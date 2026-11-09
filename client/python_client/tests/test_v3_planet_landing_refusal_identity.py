"""Acceptance coverage for V3-PLAN-01 refusal identity handling."""

import client


def test_landing_refusal_data_cannot_replace_confirmed_surface(
    ctx_factory, fake_conn
):
    """A refusal diagnostic must not claim the requested planet locally."""
    ctx = ctx_factory(
        sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]}
    )
    ctx.state["current_planet_id"] = 66
    ctx.state["on_planet"] = True
    ctx.menu_stack.append("SURFACE")

    fake_conn.queue("planet.land", {
        "status": "error",
        "error": {
            "message": "Planetary defenses refused landing.",
            # This is diagnostic data from the attempted target, not proof of
            # a successful transition.  The client must not promote it.
            "data": {"planet_id": 77, "landing_allowed": True},
        },
    })
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "data": {"player": {"id": 3, "turns": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "data": {"ship": {"ship_id": 9, "hull": 85, "onplanet": 66}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert ctx.state["current_planet_id"] == 66
    assert ctx.state["on_planet"] is True
    assert ctx.menu_stack[-1] == "SURFACE"
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
