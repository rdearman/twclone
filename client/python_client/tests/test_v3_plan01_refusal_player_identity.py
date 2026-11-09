"""V3-PLAN-01 client acceptance for refusal refresh identity handling."""

import client


def test_landing_refusal_does_not_accept_other_player_refresh(
    ctx_factory, fake_conn, capsys
):
    """A refusal refresh must not replace the logged-in player's HUD record."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.player_info = {"player": {"id": 3, "turns": 40, "credits": 1200}}
    fake_conn.queue("planet.land", {
        "status": "refused",
        "error": {"message": "Planetary defenses stopped landing."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "data": {"player": {"id": 99, "turns": 1, "credits": 1}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "data": {"ship": {"ship_id": 9, "hull": 89, "onplanet": None}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert ctx.player_info == {"player": {"id": 3, "turns": 40, "credits": 1200}}
    assert ctx.state.get("on_planet") is not True
    assert "did not identify the logged-in player" in capsys.readouterr().out
