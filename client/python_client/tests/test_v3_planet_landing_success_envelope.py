"""V3-PLAN-01 acceptance for the authoritative landing success envelope."""

import client


def test_explicit_landing_success_type_refreshes_hud_before_surface(
    ctx_factory, fake_conn, monkeypatch
):
    """A typed success must refresh authoritative HUD state before opening UI."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {
        "status": "ok",
        "type": "planet.land.success",
        "data": {"planet_id": 77},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "data": {"player": {"id": 3, "turns_remaining": 39, "credits": "1200.00"}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok",
        "data": {"ship": {"ship_id": 9, "hull": 100, "onplanet": 77}},
    })
    fake_conn.queue("planet.info", {
        "status": "ok",
        "data": {"planet": {"planet_id": 77, "sector_id": 17}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.land_flow(ctx)

    assert ctx.state["on_planet"] is True
    assert ctx.state["current_planet_id"] == 77
    assert ctx.player_info["player"]["id"] == 3
    assert ctx.menu_stack[-1] == "SURFACE"
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status", "planet.info",
    ]


def test_omitted_success_type_is_not_a_landing_success(
    ctx_factory, fake_conn, monkeypatch
):
    """An omitted response type is not the authoritative landing schema."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "onplanet": 77}},
    })
    fake_conn.queue("planet.info", {
        "status": "ok", "data": {"planet": {"planet_id": 77, "sector_id": 17}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert "SURFACE" not in ctx.menu_stack


def test_root_level_landed_envelope_is_not_a_landing_success(
    ctx_factory, fake_conn, monkeypatch
):
    """A root-level compact envelope is not the server response schema."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    fake_conn.queue("planet.land", {"status": "landed", "planet_id": 77})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "onplanet": 77}},
    })
    fake_conn.queue("planet.info", {
        "status": "ok",
        "data": {"planet": {"planet_id": 77, "sector_id": 17}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "")

    client.land_flow(ctx)

    assert ctx.state.get("on_planet") is not True
    assert "SURFACE" not in ctx.menu_stack
