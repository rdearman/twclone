"""
Tests for the consolidated move.warp handling (_perform_warp), covering the
three required movement scenarios:

1. Successful movement updates the cached sector and displays the destination.
2. Refused movement leaves the cached sector unchanged and displays the refusal.
3. Error movement reconciles from the authoritative current-sector read.
"""
import client


def test_successful_move_updates_cached_sector_and_shows_destination(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 1, "name": "Sector 1", "adjacent": [{"to_sector": 2}]})

    fake_conn.queue("move.warp", {"status": "ok", "data": {"sector_id": 1, "to_sector_id": 2}})
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "data": {"sector_id": 2, "name": "Sector 2", "adjacent_sectors": []},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "data": {"ship": {"hull": 91, "shields": 80, "fighters": 20}}
    })

    ok = client._perform_warp(ctx, 2)

    assert ok is True
    assert ctx.last_sector_desc["id"] == 2
    out = capsys.readouterr().out
    assert "Moved to sector 2." in out
    assert "You are in sector 2" in out  # redisplay_sector ran with new data
    assert [c["command"] for c in fake_conn.calls] == [
        "move.warp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_refused_move_leaves_cached_sector_unchanged_and_shows_refusal(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 1, "name": "Sector 1", "adjacent": [{"to_sector": 2}]})

    fake_conn.queue("move.warp", {
        "status": "refused",
        "data": {"reason": "insufficient_turns"},
        "error": {"code": 1301, "message": "Not enough turns remaining."},
    })

    ok = client._perform_warp(ctx, 2)

    assert ok is False
    assert ctx.last_sector_desc["id"] == 1  # unchanged
    out = capsys.readouterr().out
    assert "Move refused" in out
    assert "insufficient_turns" in out
    # The requested destination must not be described/displayed on failure.
    assert "Sector 2" not in out
    assert "You are in sector 2" not in out
    # Only move.warp was called — no describe_sector on failure.
    assert [c["command"] for c in fake_conn.calls] == ["move.warp"]


def test_error_move_refreshes_authoritative_sector_and_ship_state(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 1, "name": "Sector 1", "adjacent": [{"to_sector": 2}]})

    fake_conn.queue("move.warp", {
        "status": "error",
        "error": {"code": 500, "message": "Internal server error."},
    })
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"sector_id": 2, "name": "Sector 2"}})

    ok = client._perform_warp(ctx, 2)

    assert ok is False
    assert ctx.last_sector_desc["id"] == 2
    out = capsys.readouterr().out
    assert "Move failed" in out
    assert "Current sector refreshed after the movement error: 2" in out
    assert [c["command"] for c in fake_conn.calls] == [
        "move.warp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_error_move_hazard_is_shown_and_state_is_reconciled(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 1, "name": "Old"})
    fake_conn.queue("move.warp", {
        "status": "error",
        "error": {"code": 500, "message": "Hazard resolution failed", "data": {
            "hazards": [{"hazard_type": "ion storm", "shields_lost": 8}],
        }},
    })
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"sector_id": 2, "name": "New"}})

    assert client._perform_warp(ctx, 2) is False

    output = capsys.readouterr().out
    assert "ion storm" in output and "shields lost: 8" in output
    assert ctx.last_sector_desc["id"] == 2


def test_refused_move_hazard_reconciles_authoritative_state(ctx_factory, fake_conn, capsys):
    """A refusal with server-reported entry hazards must refresh before retry."""
    ctx = ctx_factory(sector={"id": 1, "name": "Old"})
    fake_conn.queue("move.warp", {
        "status": "refused",
        "error": {"code": 409, "message": "Entry hazard processing stopped.", "data": {
            "hazards": [{"hazard_type": "minefield", "hull_lost": 6}],
        }},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"sector_id": 2, "name": "Authoritative location"},
    })

    assert client._perform_warp(ctx, 2) is False

    output = capsys.readouterr().out
    assert "minefield" in output and "hull lost: 6" in output
    assert ctx.last_sector_desc["id"] == 2
    assert [call["command"] for call in fake_conn.calls] == [
        "move.warp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_unconfirmed_move_clears_stale_location_and_stops_navigation(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 1, "name": "Old"})
    ctx.state["sector_id"] = 1
    fake_conn.queue("move.warp", {"status": "ok", "data": {"to_sector_id": 2}})
    fake_conn.queue("move.describe_sector", {"status": "refused", "error": {"message": "Unavailable"}})

    assert client._perform_warp(ctx, 2) is False

    # A failed authoritative refresh leaves the last confirmed location intact;
    # the client must not replace uncertainty with an empty local snapshot.
    assert ctx.last_sector_desc == {"id": 1, "name": "Old"}
    assert ctx.state.get("sector_id") == 1
    assert ctx.hud.sector_id is None and ctx.hud.sector_name is None
    assert "navigation stopped" in capsys.readouterr().out


def test_move_to_adjacent_rejects_non_adjacent_target_without_calling_server(ctx_factory, fake_conn, monkeypatch):
    ctx = ctx_factory(sector={"id": 1, "adjacent": [{"to_sector": 2}]})
    monkeypatch.setattr("builtins.input", lambda *_: "99")

    client.move_to_adjacent(ctx)

    assert fake_conn.calls == []
    assert ctx.last_sector_desc["id"] == 1


def test_move_displays_server_reported_sector_hazard_damage(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 1})
    fake_conn.queue("move.warp", {"status": "ok", "data": {"to_sector_id": 2, "hazards": [
        {"hazard_type": "radiation", "damage": 17, "shields_lost": 10, "hull_lost": 7},
    ]}})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"sector_id": 2}})

    assert client._perform_warp(ctx, 2)

    output = capsys.readouterr().out
    assert "radiation" in output and "damage: 17" in output
    assert "shields lost: 10" in output and "hull lost: 7" in output
    assert "fighters lost" not in output
    assert [c["command"] for c in fake_conn.calls] == [
        "move.warp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_transwarp_refreshes_ship_after_successful_hazard_entry(ctx_factory, fake_conn, monkeypatch, capsys):
    ctx = ctx_factory(sector={"id": 1})
    fake_conn.queue("move.transwarp", {"status": "ok", "type": "move.transwarp", "data": {
        "sector_id": 42,
        "hazards": [{"hazard_type": "nebula", "damage": 11, "hull_lost": 11}],
    }})
    fake_conn.queue("move.describe_sector", {"status": "ok", "data": {"sector_id": 42}})

    monkeypatch.setattr("builtins.input", lambda _prompt: "42")
    client.transwarp_flow(ctx)

    output = capsys.readouterr().out
    assert "nebula" in output and "hull lost: 11" in output
    assert [c["command"] for c in fake_conn.calls[:4]] == [
        "move.transwarp", "move.describe_sector", "player.my_info", "ship.status",
    ]


def test_transwarp_uses_authoritative_request_and_response_envelope(
    ctx_factory, fake_conn, monkeypatch, capsys
):
    ctx = ctx_factory(sector={"id": 1, "name": "Origin"})
    fake_conn.queue("move.transwarp", {
        "status": "ok",
        "type": "move.transwarp",
        "data": {"sector_id": 50, "status": "transwarp_complete", "hazards": []},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 50},
    })
    fake_conn.queue("player.my_info", {
        "status": "ok", "type": "player.info", "data": {"player": {"sector": 50}},
    })
    fake_conn.queue("ship.status", {
        "status": "ok", "type": "ship.status", "data": {"ship": {"id": 9}},
    })
    monkeypatch.setattr("builtins.input", lambda _prompt: "50")

    client.transwarp_flow(ctx)

    assert fake_conn.calls[0] == {
        "command": "move.transwarp", "data": {"to_sector_id": 50}
    }
    assert "Transwarped to sector 50." in capsys.readouterr().out


def test_error_hazard_report_uses_server_error_data():
    lines = client.present_move_hazards({"status": "error", "error": {"data": {
        "hazards": [{"hazard_type": "nebula", "hull_lost": 4}],
    }}})
    assert len(lines) == 1
    assert "nebula" in lines[0] and "hull lost: 4" in lines[0]


def test_hazard_report_displays_only_server_supplied_terminal_state():
    lines = client.present_move_hazards({"status": "ok", "data": {
        "hazards": [{"hazard_type": "minefield", "destroyed": True,
                      "ship_remains_in_sector": False}],
    }})
    assert lines == [
        "Sector hazard · minefield · destroyed: True · ship remains in sector: False"
    ]


def test_hazard_report_does_not_display_negative_loss_counters():
    """Malformed negative counters must not become authoritative hazard facts."""
    lines = client.present_move_hazards({"status": "ok", "data": {
        "hazards": [{
            "hazard_type": "minefield",
            "damage": -1,
            "shields_lost": -2,
            "fighters_lost": -3,
            "hull_lost": -4,
        }],
    }})

    assert lines == ["Sector hazard · minefield"]


def test_malformed_movement_envelope_refreshes_without_client_traceback(
    ctx_factory, fake_conn, capsys
):
    """Malformed movement data cannot bypass the authoritative recovery path."""
    ctx = ctx_factory(sector={"id": 1, "name": "Confirmed"})
    fake_conn.queue("move.warp", {"status": "ok", "type": "move.result", "data": []})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 1, "name": "Confirmed"},
    })

    assert client._perform_warp(ctx, 2) is False
    assert ctx.last_sector_desc["id"] == 1
    assert "could not be confirmed" in capsys.readouterr().out


def test_non_object_movement_envelope_is_refreshed_without_traceback(
    ctx_factory, fake_conn
):
    ctx = ctx_factory(sector={"id": 1, "name": "Confirmed"})
    fake_conn.queue("move.warp", [])
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info", "data": {"sector_id": 1},
    })

    assert client._perform_warp(ctx, 2) is False
    assert ctx.last_sector_desc["id"] == 1
