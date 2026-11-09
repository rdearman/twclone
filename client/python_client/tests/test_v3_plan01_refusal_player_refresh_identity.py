"""V3-PLAN-01 refusal refresh must remain bound to the logged-in player."""

import client


def test_landing_refusal_does_not_apply_unidentified_player_refresh(
    ctx_factory, fake_conn
):
    ctx = ctx_factory(
        sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]},
    )
    ctx.player_info = {"player": {"id": 3, "turns": 40}}
    fake_conn.queue("planet.land", {
        "status": "refused",
        "error": {"message": "Landing refused."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "type": "player.info",
        "data": {"player": {"turns": 39}},
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

    assert ctx.player_info == {"player": {"id": 3, "turns": 40}}
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
