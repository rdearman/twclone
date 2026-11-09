"""V3-HAZ-01: terminal entry outcomes must not assert the requested sector."""

import client
from hud import HudState


def test_terminal_warp_reconciles_actual_state_without_destination_assertion(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    ctx.hud = HudState(sector_id=17, ship_id=9, turns_remaining=20)
    fake_conn.queue("move.warp", {
        "status": "ok",
        "data": {
            "hazards": [{"hazard_type": "minefield", "damage": 100}],
            "ship_destroyed": True,
            "ship_remains_in_sector": False,
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 1},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3, "sector": 1}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })

    assert not client._perform_warp(ctx, 99)

    assert ctx.hud.sector_id == 1
    assert "Moved to sector 99" not in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "move.warp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_terminal_transwarp_in_error_data_uses_unconstrained_refresh(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    fake_conn.queue("move.transwarp", {
        "status": "refused",
        "error": {
            "message": "Entry hazard resolved the ship.",
            "data": {"ship_remains_in_sector": False},
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 2},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3, "sector": 2}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "88")

    client.transwarp_flow(ctx)

    output = capsys.readouterr().out
    assert "Entry hazard resolved the ship." in output
    assert "Transwarped to sector 88" not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_terminal_death_accepts_authoritative_escape_pod_ship_refresh(
    ctx_factory, fake_conn, capsys
):
    """Death refresh may replace the destroyed active ship with the server's pod."""
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    ctx.player_info = {"player": {"id": 3, "ship_id": 9}}
    ctx.hud = HudState(sector_id=17, ship_id=9, credits=250, cargo_used=12, hull=4)
    fake_conn.queue("move.warp", {
        "status": "refused",
        "error": {"message": "Ship destroyed.", "data": {
            "ship_destroyed": True, "ship_remains_in_sector": False,
        }},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 1},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {
            "player": {"id": 3, "ship_id": 44, "sector": 1, "credits": 250},
        },
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {
            "ship": {"id": 44, "name": "Escape Pod", "hull": 100, "cargo": []},
        },
    })

    assert not client._perform_warp(ctx, 99)

    assert ctx.hud.ship_id == 44
    assert ctx.hud.ship_name == "Escape Pod"
    assert ctx.hud.hull == 100
    assert ctx.hud.cargo_used == 0
    assert ctx.hud.credits == 250
    assert "did not identify the confirmed active ship" not in capsys.readouterr().out


def test_terminal_death_rejects_cross_account_escape_pod_refresh(
    ctx_factory, fake_conn, capsys
):
    """A terminal outcome must not accept a pod record for another captain."""
    ctx = ctx_factory(sector={"id": 17, "name": "Before"})
    ctx.player_info = {"player": {"id": 3, "ship_id": 9}}
    ctx.hud = HudState(sector_id=17, ship_id=9, credits=250, cargo_used=12, hull=4)
    fake_conn.queue("move.warp", {
        "status": "refused",
        "error": {"message": "Ship destroyed.", "data": {
            "ship_destroyed": True, "ship_remains_in_sector": False,
        }},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 1},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {
            "player": {"id": 44, "ship_id": 88, "sector": 1, "credits": 9999},
        },
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {
            "ship": {"id": 88, "name": "Untrusted Pod", "hull": 100, "cargo": []},
        },
    })

    assert not client._perform_warp(ctx, 99)

    assert ctx.hud.ship_id == 9
    assert ctx.hud.hull == 4
    assert ctx.hud.credits == 250
    assert "did not identify the logged-in player" in capsys.readouterr().out


def test_combat_refusal_reconciles_authoritative_escape_pod(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """A refusal may still destroy the attacker before the server replies."""
    ctx = ctx_factory(sector={"id": 3, "name": "FedSpace", "ships": [
        {"id": 88, "name": "Target"},
    ]})
    ctx.player_info = {"player": {"id": 3, "ship_id": 9}}
    ctx.hud = HudState(sector_id=3, ship_id=9, credits=250, hull=4)
    fake_conn.queue("combat.attack", {
        "status": "refused",
        "error": {"message": "Captain Z intervenes."},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {
            "player": {"id": 3, "ship_id": 44, "sector": 1, "credits": 250},
        },
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {
            "ship": {"id": 44, "name": "Escape Pod", "hull": 100, "cargo": []},
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 1},
    })
    answers = iter(["88", "y"])
    monkeypatch.setattr("builtins.input", lambda *_: next(answers))

    client.combat_attack_flow(ctx)

    assert ctx.hud.ship_id == 44
    assert ctx.hud.ship_name == "Escape Pod"
    assert ctx.hud.hull == 100
    assert ctx.hud.sector_id == 1
    assert [call["command"] for call in fake_conn.calls] == [
        "combat.attack", "player.my_info", "ship.status", "move.describe_sector",
    ]
    assert "Captain Z intervenes." in capsys.readouterr().out
