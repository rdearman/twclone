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

    ok = client._perform_warp(ctx, 2)

    assert ok is True
    assert ctx.last_sector_desc["id"] == 2
    out = capsys.readouterr().out
    assert "Moved to sector 2." in out
    assert "You are in sector 2" in out  # redisplay_sector ran with new data


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


def test_unconfirmed_move_clears_stale_location_and_stops_navigation(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 1, "name": "Old"})
    ctx.state["sector_id"] = 1
    fake_conn.queue("move.warp", {"status": "ok", "data": {"to_sector_id": 2}})
    fake_conn.queue("move.describe_sector", {"status": "refused", "error": {"message": "Unavailable"}})

    assert client._perform_warp(ctx, 2) is False

    assert ctx.last_sector_desc == {}
    assert ctx.state.get("sector_id") is None
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


def test_error_hazard_report_uses_server_error_data():
    lines = client.present_move_hazards({"status": "error", "error": {"data": {
        "hazards": [{"hazard_type": "nebula", "hull_lost": 4}],
    }}})
    assert len(lines) == 1
    assert "nebula" in lines[0] and "hull lost: 4" in lines[0]
