"""V3-PLAN-01: landing refusal refreshes must remain response-correlated."""

import client


def test_landing_refusal_requires_shared_error_response_type():
    assert client._landing_response_type_matches({
        "status": "refused", "type": "error",
    })
    assert not client._landing_response_type_matches({
        "status": "refused", "type": "refused",
    })


def test_landing_refusal_does_not_apply_wrong_typed_player_refresh(
    ctx_factory, fake_conn, capsys
):
    """A success-shaped response for another read cannot update landing HUD."""
    ctx = ctx_factory(
        sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    )
    ctx.player_info = {"player": {"id": 3, "turns": 40, "credits": 1200}}
    fake_conn.queue("planet.land", {
        "status": "refused",
        "error": {"message": "Landing refused."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "type": "ship.status",
        "data": {"player": {"id": 3, "turns": 39, "credits": 1}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "type": "ship.status",
        "data": {"ship": {"ship_id": 9, "onplanet": 0}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "data": {"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    })

    client.land_flow(ctx)

    assert ctx.player_info == {
        "player": {"id": 3, "turns": 40, "credits": 1200}
    }
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert "did not identify the logged-in player" in capsys.readouterr().out
