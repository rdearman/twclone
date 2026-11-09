"""Client-side V3-HAZ-01 reconciliation contract checks."""

import client


def test_move_refresh_rejects_success_payload_with_wrong_response_type(
    ctx_factory, fake_conn, capsys
):
    """A post-move refresh must identify the sector-info response schema."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("move.warp", {
        "status": "ok",
        "type": "move.warp.success",
        "data": {"sector_id": 17},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "player.my_info",
        "data": {"id": 17, "name": "Wrong operation payload"},
    })

    assert client._perform_warp(ctx, 17) is False
    assert ctx.last_sector_desc["id"] == 12
    assert "destination could not be confirmed" in capsys.readouterr().out


def test_move_refresh_rejects_success_payload_without_response_type(
    ctx_factory, fake_conn, capsys
):
    """An omitted type is not an alternate movement success contract."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("move.warp", {"status": "ok", "data": {"sector_id": 17}})
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "type": "sector.info",
        "data": {"sector_id": 17, "name": "Unconfirmed"},
    })

    assert client._perform_warp(ctx, 17) is False
    assert ctx.last_sector_desc["id"] == 12
    assert "unexpected type" in capsys.readouterr().out
