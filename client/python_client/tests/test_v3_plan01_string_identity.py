"""V3-PLAN-01: landing identity must remain an authoritative JSON integer."""

import client


def test_landing_string_id_success_is_reconciled_but_not_claimed(
    ctx_factory, fake_conn, capsys
):
    """A schema-mismatched success cannot make stale planet actions usable."""
    ctx = ctx_factory(
        sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]}
    )
    fake_conn.queue(
        "planet.land",
        {
            "status": "ok",
            "type": "planet.land.success",
            "data": {"planet_id": "77"},
        },
    )
    fake_conn.queue(
        "player.my_info",
        {"status": "ok", "data": {"player": {"id": 3, "turns": 40}}},
    )
    fake_conn.queue(
        "ship.status",
        {"status": "ok", "data": {"ship": {"ship_id": 9, "onplanet": 77}}},
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
    assert ctx.state.get("on_planet") is not True
    assert "did not return the requested planet ID" in capsys.readouterr().out

