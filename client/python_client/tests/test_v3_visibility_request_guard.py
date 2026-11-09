"""V3-VIS-01 request guards for server-authoritative visibility IDs."""

import client


def test_density_scan_does_not_send_without_authoritative_sector_id(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": "12", "name": "Stale"})

    client.sector_density_scan_flow(ctx)

    assert fake_conn.calls == []
    assert ctx.last_sector_desc == {"id": "12", "name": "Stale"}
    assert "no authoritative ID" in capsys.readouterr().out


def test_density_scan_sends_only_positive_integer_sector_id(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan.density", {
        "status": "ok",
        "type": "sector.density.scan",
        "data": {"sectors": [12, 0]},
    })

    client.sector_density_scan_flow(ctx)

    assert fake_conn.calls == [{
        "command": "sector.scan.density",
        "data": {"sector_id": 12},
    }]
    assert "Sector   Density" in capsys.readouterr().out
