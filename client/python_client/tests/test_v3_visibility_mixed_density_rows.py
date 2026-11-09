"""V3-VIS-01 acceptance for all-or-nothing density disclosure."""

import client


def test_density_scan_rejects_mixed_rows_before_rendering_partial_visibility(
    ctx_factory, fake_conn, capsys
):
    """A malformed row cannot leak alongside otherwise valid density data."""
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "type": "sector.scan.density",
        "data": {
            "sectors": [
                {"sector_id": 12, "density": 4},
                {"sector_id": 13, "density": "unknown"},
            ]
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"id": 12, "name": "Known refreshed"},
    })

    client.sector_density_scan_flow(ctx)

    assert fake_conn.calls == [
        {"command": "sector.scan.density", "data": {"sector_id": 12}},
        {"command": "move.describe_sector", "data": {"sector_id": 12}},
    ]
    assert ctx.last_sector_desc == {"id": 12, "name": "Known refreshed"}
    output = capsys.readouterr().out
    assert "invalid authoritative row" in output
    assert "Sector   Density" not in output
