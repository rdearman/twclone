"""Client acceptance checks for ambiguous V3-PLAN-01 landing replies."""

import pytest

import client


@pytest.mark.parametrize(
    "landing_response",
    [
        {"status": "ok", "data": {}},
        {"status": "ok", "data": {"planet_id": "77"}},
        {"status": "ok", "data": {"planet_id": True}},
        {"status": "ok", "data": {"planet_id": 77.0}},
    ],
)
def test_ambiguous_success_reconciles_without_claiming_planet(
    ctx_factory, fake_conn, landing_response, capsys
):
    """An incomplete success cannot establish a local landing or surface."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", landing_response)
    fake_conn.queue(
        "player.my_info",
        {"status": "ok", "data": {"player": {"id": 3, "turns": 40}}},
    )
    fake_conn.queue(
        "ship.status",
        {"status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100}}},
    )
    fake_conn.queue(
        "move.describe_sector",
        {"status": "ok", "data": {"id": 17, "planets": [{"id": 77}]}},
    )

    client.land_flow(ctx)

    assert "current_planet_id" not in ctx.state
    assert ctx.state.get("on_planet") is not True
    assert "did not return the requested planet ID" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land",
        "player.my_info",
        "ship.status",
        "move.describe_sector",
    ]
