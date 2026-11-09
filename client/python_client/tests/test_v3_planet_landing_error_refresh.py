"""Additional V3-PLAN-01 coverage for error-envelope landing refusals."""

import client


def test_landing_error_envelope_refreshes_without_claiming_planet(
    ctx_factory, fake_conn, capsys
):
    """A non-OK landing envelope cannot be treated as a successful landing."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "error",
        "error": {
            "message": "Planetary defenses stopped landing.",
            "data": {"planet_id": 77, "combat": [{
                "defense": "atmosphere_quasar", "damage": 11,
                "shields_lost": 0, "hull_lost": 11,
            }]},
        },
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 89}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert "current_planet_id" not in ctx.state
    assert ctx.state.get("on_planet") is not True
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "Landing failed: Planetary defenses stopped landing." in output
    assert "Atmospheric Quasar: 11 damage" in output
