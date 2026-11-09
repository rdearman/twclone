"""V3-PLAN-01 acceptance for malformed landing envelope metadata."""

import client


def test_landing_non_string_response_type_is_not_claimed(
    ctx_factory, fake_conn, capsys
):
    """Schema-mismatched envelope metadata cannot grant planet access."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok", "type": 17, "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 40}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "onplanet": 77}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77}]},
    })

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert ctx.state.get("current_planet_id") is None
    assert ctx.menu_stack == ["MAIN"]
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert "unexpected type 17" in capsys.readouterr().out
