"""V3-HAZ-01 keeps player state authoritative after movement hazards."""

import client
from hud import HudState


def test_successful_hazard_move_replaces_cached_player_record(
    ctx_factory, fake_conn
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    ctx.player_info = {"player": {"id": 3, "turns_remaining": 40, "credits": 100}}
    ctx.hud = HudState(sector_id=17, ship_id=9, turns_remaining=40, credits=100)
    fake_conn.queue("move.warp", {
        "status": "ok",
        "data": {"sector_id": 18, "hazards": [{"hazard_type": "minefield", "hull_lost": 4}]},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"sector_id": 18, "name": "After"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok",
        "data": {"player": {"id": 3, "turns_remaining": 39, "credits": 92}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "hull": 96}},
    })

    assert client._perform_warp(ctx, 18)

    assert ctx.player_info == {"player": {"id": 3, "turns_remaining": 39, "credits": 92}}
    assert ctx.hud.turns_remaining == 39
    assert ctx.hud.credits == 92
    assert ctx.hud.hull == 96


def test_hazard_move_does_not_merge_anonymous_player_refresh(
    ctx_factory, fake_conn, capsys
):
    """A hazard refresh without the session player ID is not authoritative."""
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    ctx.player_info = {"player": {"id": 3, "turns_remaining": 40, "credits": 100}}
    ctx.hud = HudState(sector_id=17, ship_id=9, turns_remaining=40, credits=100)
    fake_conn.queue("move.warp", {
        "status": "ok", "data": {"sector_id": 18, "hazards": [
            {"hazard_type": "minefield", "hull_lost": 4}
        ]},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"sector_id": 18, "name": "After"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"turns_remaining": 1, "credits": 999}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 9, "hull": 96}},
    })

    assert client._perform_warp(ctx, 18)

    assert ctx.player_info == {
        "player": {"id": 3, "turns_remaining": 40, "credits": 100}
    }
    assert ctx.hud.turns_remaining == 40
    assert ctx.hud.credits == 100
    assert ctx.hud.hull == 96
    assert "did not identify the logged-in player" in capsys.readouterr().out

