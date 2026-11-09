"""V3-VIS-01: detailed-scan type mismatches reconcile current visibility."""

import client


def test_detailed_scan_wrong_response_type_refreshes_confirmed_sector(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("sector.scan", {
        "status": "ok",
        "type": "sector.info",
        "data": {"sector_id": 12, "name": "Untrusted scan"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"id": 12, "name": "Authoritative refresh"},
    })

    client.detailed_sector_scan_flow(ctx)

    output = capsys.readouterr().out
    assert ctx.last_sector_desc["name"] == "Authoritative refresh"
    assert "Untrusted scan" not in output
    assert "unexpected type" in output
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan", "move.describe_sector",
    ]
