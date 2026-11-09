"""V3-SHIP-02: ship rename responses stay correlated to the active ship."""

from dataclasses import replace

import client


def test_ship_rename_rejects_a_different_authoritative_ship_id(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.hud = replace(ctx.hud, ship_id=10)
    fake_conn.queue("ship.rename", {
        "status": "ok", "type": "ship.renamed", "data": {"ship_id": 11, "name": "Aurora"},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 10, "name": "Wayfarer"}},
    })
    monkeypatch.setattr("builtins.input", lambda _prompt: "Aurora")

    client.rename_ship_flow(ctx)

    assert fake_conn.calls == [
        {"command": "ship.rename", "data": {"name": "Aurora"}},
        {"command": "ship.status", "data": {}},
    ]
    output = capsys.readouterr().out
    assert "no authoritative active-ship identity" in output
    assert "confirmed by server" not in output


def test_ship_rename_requires_id_even_when_name_is_present(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.hud = replace(ctx.hud, ship_id=10)
    fake_conn.queue("ship.rename", {
        "status": "ok", "type": "ship.renamed", "data": {"name": "Aurora"},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 10, "name": "Aurora"}},
    })
    monkeypatch.setattr("builtins.input", lambda _prompt: "Aurora")

    client.rename_ship_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "ship.rename", "ship.status",
    ]
    assert "no authoritative active-ship identity" in capsys.readouterr().out


def test_ship_rename_rejects_untyped_success_and_refreshes_active_ship(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory()
    ctx.hud = replace(ctx.hud, ship_id=10)
    fake_conn.queue("ship.rename", {
        "status": "ok", "data": {"ship_id": 10, "name": "Aurora"},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"ship_id": 10, "name": "Wayfarer"}},
    })
    monkeypatch.setattr("builtins.input", lambda _prompt: "Aurora")

    client.rename_ship_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == ["ship.rename", "ship.status"]
    output = capsys.readouterr().out
    assert "unexpected response type" in output
    assert "confirmed by server" not in output

