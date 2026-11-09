"""V3-PLAN-01: landing refusal must reconcile the authoritative sector identity."""

import client


def test_landing_refusal_rejects_contradictory_sector_without_claiming_planet(
    ctx_factory, fake_conn, capsys
):
    """A contradictory refresh cannot replace confirmed location or grant access."""
    ctx = ctx_factory(
        sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]}
    )
    ctx.state["current_planet_id"] = 12
    ctx.state["on_planet"] = True
    fake_conn.queue("planet.land", {
        "status": "refused",
        "error": {
            "message": "Landing is not permitted.",
            "data": {"planet_id": 77},
        },
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 85}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "data": {"id": 18, "planets": [{"id": 88, "name": "Authoritative"}]},
    })

    client.land_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert ctx.last_sector_desc["id"] == 17
    assert ctx.state["current_planet_id"] == 12
    assert ctx.state["on_planet"] is True
    output = capsys.readouterr().out
    assert "Landing failed: Landing is not permitted." in output
    assert "Landed on planet 77." not in output
