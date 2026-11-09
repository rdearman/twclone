"""Client acceptance checks for authoritative planet IDs in V3-PLAN-01."""

import pytest

import client


@pytest.mark.parametrize(
    "planet_record",
    [
        {"name": "No ID"},
        {"name": "String ID", "planet_id": "77"},
        {"name": "Boolean ID", "planet_id": True},
        {"name": "Float ID", "planet_id": 77.0},
    ],
)
def test_landing_does_not_request_without_authoritative_integer_id(
    ctx_factory, fake_conn, planet_record, capsys
):
    """A malformed sector record must fail closed before mutating RPC dispatch."""
    ctx = ctx_factory(sector={"id": 17, "planets": [planet_record]})

    client.land_flow(ctx)

    output = capsys.readouterr().out
    assert (
        "No listed planet has a usable server ID." in output
        or "refresh the sector before landing" in output
    )
    assert [call["command"] for call in fake_conn.calls] == []
    assert "current_planet_id" not in ctx.state
    assert ctx.state.get("on_planet") is not True


def test_multi_planet_landing_does_not_display_malformed_ids_as_actionable(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [
        {"id": True, "name": "Boolean ID"},
        {"planet_id": 77.0, "name": "Float ID"},
        {"planet_id": 88, "name": "Valid ID"},
    ]})
    monkeypatch.setattr("builtins.input", lambda *_: "1")

    client.land_flow(ctx)

    output = capsys.readouterr().out
    assert "Boolean ID [ID: unavailable]" in output
    assert "Float ID [ID: unavailable]" in output
    assert "Valid ID [ID: 88]" in output
    assert "Could not determine a listed planet ID" in output
    assert [call["command"] for call in fake_conn.calls] == []
