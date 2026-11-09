"""V3-VIS-01: refused density reads must reconcile the confirmed sector only."""

import client


def test_density_refusal_does_not_merge_remote_payload_or_refresh_payload(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed sector"})
    fake_conn.queue("sector.scan.density", {
        "status": "refused",
        "error": {"message": "Density is unavailable from this location."},
        "data": {"sector_id": 999, "density": 77, "name": "Remote secret"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "refused",
        "error": {
            "message": "Current-sector refresh unavailable.",
            "data": {"sector_id": 999, "name": "Untrusted refresh payload"},
        },
    })

    client.sector_density_scan_flow(ctx)

    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan.density", "move.describe_sector",
    ]
    assert ctx.last_sector_desc == {"id": 12, "name": "Confirmed sector"}
    output = capsys.readouterr().out
    assert "Density is unavailable from this location." in output
    assert "Current-sector refresh unavailable." in output
    assert "Remote secret" not in output
    assert "Untrusted refresh payload" not in output
