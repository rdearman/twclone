"""V3-PLAN-01: retained landing identity must not authorize stale mutations."""

import client


def test_surface_mutation_requires_authoritative_on_planet_state(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """An incomplete refresh keeps identity for recovery but blocks mutations."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.state["current_planet_id"] = 77
    ctx.state["on_planet"] = False
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.planet_workforce_flow(ctx)

    assert fake_conn.calls == []
    assert "Land on a planet before assigning its workforce." in capsys.readouterr().out
