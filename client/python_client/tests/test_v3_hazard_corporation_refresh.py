"""V3-HAZ-01 keeps Python client gating current after movement errors."""

import client


def test_hazard_movement_error_refreshes_corporation_context(
    ctx_factory, fake_conn, monkeypatch
):
    """A hazard-bearing error must reconcile all player-owned client state."""
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    ctx.player_info = {"player": {"id": 3, "corporation_id": 7}}
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"sector_id": 18, "name": "After"},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "data": {"player": {"id": 3, "corporation_id": 9}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"id": 11, "hull": 84}},
    })

    calls = []
    monkeypatch.setattr(client, "_update_corp_context", lambda refreshed: calls.append(refreshed))

    client._reconcile_after_move_error(ctx)

    assert calls == [ctx]
    assert ctx.player_info["player"]["corporation_id"] == 9
    assert ctx.hud.hull == 84
