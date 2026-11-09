"""V3-HAZ-01 movement envelopes must identify their operation."""

import client
from hud import HudState


def test_warp_wrong_response_type_refreshes_without_claiming_destination(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    ctx.hud = HudState(sector_id=12, ship_id=9, turns_remaining=20)
    fake_conn.queue("move.warp", {
        "status": "ok", "type": "planet.info",
        "data": {"current_sector": 17, "hazards": [{"hazard_type": "minefield"}]},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 17},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3, "sector": 17}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })

    assert not client._perform_warp(ctx, 17)

    output = capsys.readouterr().out
    assert "unexpected type" in output
    assert "Moved to sector 17" not in output
    assert ctx.hud.sector_id == 17
    assert [call["command"] for call in fake_conn.calls] == [
        "move.warp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_transwarp_wrong_response_type_refreshes_all_movement_domains(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("move.transwarp", {
        "status": "ok", "type": "sector.info", "data": {"current_sector": 88},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 88},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info",
        "data": {"player": {"id": 3, "sector": 88}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "88")

    client.transwarp_flow(ctx)

    assert "unexpected type" in capsys.readouterr().out
    assert [call["command"] for call in fake_conn.calls] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_transwarp_accepts_canonical_error_envelope_and_reports_terminal_hazard(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("move.transwarp", {
        "status": "refused", "type": "error",
        "error": {"message": "Ship destroyed by sector hazards during transwarp.", "data": {
            "sector_id": 42, "ship_destroyed": True,
            "hazards": [{"hazard_type": "minefield", "hull_lost": 100}],
        }},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 42},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 3}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "42")

    client.transwarp_flow(ctx)

    output = capsys.readouterr().out
    assert "minefield" in output and "hull lost: 100" in output
    assert "Transwarp refused" in output
    assert [call["command"] for call in fake_conn.calls] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_transwarp_refused_type_refreshes_without_claiming_destination(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    """An explicit refused envelope remains a refusal, never a local move."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("move.transwarp", {
        "status": "refused", "type": "error",
        "error": {"message": "Transwarp drive not installed."},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 12},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"id": 3, "sector": 12}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda *_: "44")

    assert client.transwarp_flow(ctx) is False

    output = capsys.readouterr().out
    assert "Transwarp drive not installed." in output
    assert "unexpected type" not in output
    assert "Transwarped to sector 44." not in output
    assert [call["command"] for call in fake_conn.calls] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]
