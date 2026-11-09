"""Client acceptance checks for authoritative planet IDs in surface flows."""

import client


def test_colonist_transfer_requires_confirmed_integer_planet_id(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17})
    ctx.state["last_rpc"] = {"status": "ok", "data": {"planet_id": "77"}}

    client.colonist_transfer_flow(ctx)

    assert fake_conn.calls == []
    assert "confirmed planet ID" in capsys.readouterr().out


def test_colonist_transfer_uses_confirmed_landing_id(
    ctx_factory, fake_conn, monkeypatch
):
    ctx = ctx_factory(sector={"id": 17})
    ctx.state["current_planet_id"] = 77
    fake_conn.queue("planet.colonists.get", {
        "status": "ok",
        "data": {"planet_colonists": 10, "ship_colonists": 2, "ship_holds_available": 4},
    })
    fake_conn.queue("planet.colonists.set", {"status": "ok", "data": {}})
    monkeypatch.setattr("builtins.input", lambda prompt: {
        "Action? ": "P",
        "How many? (1-4): ": "3",
    }[prompt])

    client.colonist_transfer_flow(ctx)

    assert fake_conn.calls[0] == {
        "command": "planet.colonists.get", "data": {"planet_id": 77}
    }
