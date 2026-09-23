"""
Tests for the consolidated move.warp handling (_perform_warp), covering the
three required movement scenarios:

1. Successful movement updates the cached sector and displays the destination.
2. Refused movement leaves the cached sector unchanged and displays the refusal.
3. Error movement leaves the cached sector unchanged.
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


def test_error_move_leaves_cached_sector_unchanged(ctx_factory, fake_conn, capsys):
    ctx = ctx_factory(sector={"id": 1, "name": "Sector 1", "adjacent": [{"to_sector": 2}]})

    fake_conn.queue("move.warp", {
        "status": "error",
        "error": {"code": 500, "message": "Internal server error."},
    })

    ok = client._perform_warp(ctx, 2)

    assert ok is False
    assert ctx.last_sector_desc["id"] == 1
    out = capsys.readouterr().out
    assert "Move failed" in out
    assert [c["command"] for c in fake_conn.calls] == ["move.warp"]


def test_move_to_adjacent_rejects_non_adjacent_target_without_calling_server(ctx_factory, fake_conn, monkeypatch):
    ctx = ctx_factory(sector={"id": 1, "adjacent": [{"to_sector": 2}]})
    monkeypatch.setattr("builtins.input", lambda *_: "99")

    client.move_to_adjacent(ctx)

    assert fake_conn.calls == []
    assert ctx.last_sector_desc["id"] == 1
