"""V3-VIS-01 acceptance for a contradictory refusal refresh."""

import client


def test_refused_scan_with_mismatched_refresh_keeps_confirmed_sector(
    ctx_factory, fake_conn, capsys
):
    """Neither refused-scan data nor a remote refresh may replace local truth."""
    ctx = ctx_factory(
        sector={"id": 12, "name": "Confirmed sector", "planets": []},
    )
    fake_conn.queue("move.scan", {
        "status": "refused",
        "error": {"message": "Detailed scan requires the current sector."},
        "data": {"sector_id": 999, "name": "Untrusted scan"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"sector_id": 999, "name": "Untrusted refresh"},
    })

    client.scan_sector(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Confirmed sector"
    assert [call["command"] for call in fake_conn.calls] == [
        "move.scan", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "Detailed scan requires the current sector." in output
    assert "Untrusted scan" not in output
    assert "Untrusted refresh" not in output
