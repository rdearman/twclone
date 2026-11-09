"""V3-PLAN-01 acceptance for player identity during landing refreshes."""

from dataclasses import replace

import client


def test_landing_does_not_open_surface_for_another_player_refresh(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.player_info = {"player": {"id": 3, "turns": 40}}
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 4, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100}},
    })

    client.land_flow(ctx)

    assert ctx.menu_stack == ["MAIN"]
    assert ctx.player_info == {"player": {"id": 3, "turns": 40}}
    assert ctx.state["current_planet_id"] == 77
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out


def test_landing_uses_confirmed_session_player_id_before_player_cache_exists(
    ctx_factory, fake_conn, capsys
):
    """Startup identity must protect landing refreshes before player_info is hydrated."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.state["player_id"] = 3
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 4, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100, "onplanet": 77}},
    })

    client.land_flow(ctx)

    assert ctx.player_info == {}
    assert ctx.menu_stack == ["MAIN"]
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out


def test_landing_does_not_open_surface_when_ship_matches_but_player_does_not(
    ctx_factory, fake_conn, capsys
):
    """A matching ship/planet pair cannot authorize a mismatched player refresh."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.player_info = {"player": {"id": 3, "turns": 40}}
    fake_conn.queue("planet.land", {"status": "ok", "data": {"planet_id": 77}})
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 4, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100, "onplanet": 77}},
    })

    client.land_flow(ctx)

    assert ctx.menu_stack == ["MAIN"]
    assert ctx.player_info == {"player": {"id": 3, "turns": 40}}
    assert ctx.state["current_planet_id"] == 77
    assert "post-landing HUD refresh was incomplete" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "planet.land", "player.my_info", "ship.status",
    ]


def test_refused_landing_does_not_replace_player_with_mismatched_refresh(
    ctx_factory, fake_conn
):
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.player_info = {"player": {"id": 3, "turns": 40}}
    fake_conn.queue("planet.land", {
        "status": "refused", "error": {"message": "Landing denied."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 4, "turns": 39}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 100}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77}]},
    })

    client.land_flow(ctx)

    assert ctx.player_info == {"player": {"id": 3, "turns": 40}}


def test_refused_landing_does_not_apply_mismatched_player_to_hud(
    ctx_factory, fake_conn, capsys
):
    """A mismatched refresh cannot overwrite the logged-in player's HUD."""
    ctx = ctx_factory(sector={"id": 17, "planets": [{"id": 77, "name": "Terra"}]})
    ctx.player_info = {"player": {"id": 3, "turns": 40}}
    ctx.hud = replace(ctx.hud, turns_remaining=40)
    fake_conn.queue("planet.land", {
        "status": "refused", "error": {"message": "Landing refused."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 99, "turns": 1}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"ship_id": 9, "hull": 85}},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 17, "planets": [{"id": 77}]},
    })

    client.land_flow(ctx)

    assert ctx.hud.turns_remaining == 40
    assert ctx.player_info == {"player": {"id": 3, "turns": 40}}
    assert "logged-in player" in capsys.readouterr().out
