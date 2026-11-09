"""Client acceptance contract for SHIP-03 destruction reconciliation."""

import client


def test_destruction_refresh_uses_server_selected_replacement_ship(
    ctx_factory, fake_conn, monkeypatch
):
    ctx = ctx_factory(sector={"id": 7, "name": "Before"})
    ctx.player_info = {"player": {"id": 42, "ship_id": 9}}
    ctx.state["player_id"] = 42
    ctx.hud = ctx.hud.__class__(ship_id=9, sector_id=7)
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 42, "ship_id": 3, "sector_id": 1, "credits": "125.00", "turns_remaining": 8}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status",
        "data": {"ship": {"id": 3, "name": "Escape Pod", "hull": 100, "holds": 5, "cargo": []}},
    })
    monkeypatch.setattr(client, "_safe_landing_sector_refresh", lambda _ctx: True)

    assert client._refresh_after_ship_destruction(ctx)
    assert [call["command"] for call in fake_conn.calls] == [
        "player.my_info", "ship.status"
    ]
    assert ctx.hud.ship_id == 3
    assert ctx.hud.credits == 125
    assert ctx.hud.turns_remaining == 8


def test_destruction_event_batch_rejects_other_player_and_malformed_events(
    ctx_factory, fake_conn
):
    ctx = ctx_factory()
    ctx.state["player_id"] = 42
    ctx.last_drained_events = [
        {"type": "combat.ship_destroyed", "data": {"victim_player_id": 77}},
        {"type": "combat.hit", "data": {"victim_player_id": 42}},
    ]

    assert client._reconcile_destruction_events(ctx) is False
    assert fake_conn.calls == []
