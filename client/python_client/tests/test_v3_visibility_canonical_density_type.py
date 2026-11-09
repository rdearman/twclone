"""V3-VIS-01 regression for the single published density response type."""

import client


def test_density_scan_rejects_unsupported_response_alias_and_refreshes(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed sector"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "type": "sector.scan.density.result",
        "data": {"sectors": [12, 44]},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"sector_id": 12, "name": "Confirmed sector"},
    })

    client.sector_density_scan_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]
    assert ctx.last_sector_desc == {"id": 12, "name": "Confirmed sector"}
    output = capsys.readouterr().out
    assert "unexpected type" in output
    assert "44" not in output
