"""Reconnect restores a landed surface only from correlated V3 snapshots."""

import client


def _landed_context(ctx_factory, fake_conn):
    ctx = ctx_factory(sector={"id": 17, "planets": []})
    ctx.state["player_id"] = 3
    return ctx


def test_reconnect_restores_confirmed_landed_surface(ctx_factory, fake_conn):
    ctx = _landed_context(ctx_factory, fake_conn)
    fake_conn.queue("planet.info", {
        "status": "ok", "data": {"planet_id": 77, "sector_id": 17, "name": "Terra"},
    })
    ship = {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 9, "onplanet": 1}}}

    assert client._restore_landed_surface_after_login(
        ctx, {"id": 3, "ship_id": 9, "last_planet_id": 77, "sector": 17}, ship
    )
    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.menu_stack[-1] == "SURFACE"
    assert fake_conn.calls == [{"command": "planet.info", "data": {"planet_id": 77}}]


def test_reconnect_rejects_landed_flag_for_another_active_ship(ctx_factory, fake_conn):
    ctx = _landed_context(ctx_factory, fake_conn)
    ship = {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 10, "onplanet": 1}}}

    assert not client._restore_landed_surface_after_login(
        ctx, {"id": 3, "ship_id": 9, "last_planet_id": 77, "sector": 17}, ship
    )
    assert ctx.state.get("on_planet") is not True
    assert "SURFACE" not in ctx.menu_stack
    assert fake_conn.calls == []


def test_reconnect_rejects_planet_outside_current_sector(ctx_factory, fake_conn):
    ctx = _landed_context(ctx_factory, fake_conn)
    fake_conn.queue("planet.info", {
        "status": "ok", "data": {"planet_id": 77, "sector_id": 18, "name": "Terra"},
    })
    ship = {"status": "ok", "type": "ship.status", "data": {"ship": {"id": 9, "onplanet": 1}}}

    assert not client._restore_landed_surface_after_login(
        ctx, {"id": 3, "ship_id": 9, "last_planet_id": 77, "sector": 17}, ship
    )
    assert ctx.state.get("on_planet") is not True
    assert "SURFACE" not in ctx.menu_stack
