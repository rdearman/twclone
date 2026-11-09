"""V3-PLAN-01: refusal diagnostics must not authorize a landing locally."""

import client


def test_landing_refusal_diagnostic_cannot_open_reported_planet_surface(
    ctx_factory, fake_conn, capsys
):
    """Error data is a server report, not a successful landing transition."""
    ctx = ctx_factory(
        sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]}
    )
    ctx.state["on_planet"] = False
    ctx.state["current_planet_id"] = 12
    fake_conn.queue(
        "planet.land",
        {
            "status": "refused",
            "type": "planet.land.success",
            "error": {
                "message": "Planetary defenses refused landing.",
                "data": {"planet_id": 77, "landing_allowed": True},
            },
        },
    )
    fake_conn.queue(
        "player.my_info",
        {"status": "ok", "data": {"player": {"id": 3, "turns": 40}}},
    )
    fake_conn.queue(
        "ship.status",
        {"status": "ok", "data": {"ship": {"ship_id": 9, "onplanet": 12}}},
    )
    fake_conn.queue(
        "move.describe_sector",
        {"status": "ok", "data": {"id": 17, "planets": [{"id": 77}]}},
    )

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land",
        "player.my_info",
        "ship.status",
        "move.describe_sector",
    ]
    assert ctx.state["current_planet_id"] == 12
    assert ctx.state["on_planet"] is False
    assert ctx.menu_stack == ["MAIN"]
    output = capsys.readouterr().out
    assert "Landing failed: Planetary defenses refused landing." in output
    assert "Landed on planet 77." not in output

