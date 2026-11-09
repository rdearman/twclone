"""V3-VIS-01 regression for detailed-scan disclosure boundaries."""

import client


def test_detailed_scan_redacts_port_class_and_keeps_authoritative_location(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Known"})
    fake_conn.queue("sector.scan", {
        "status": "ok",
        "data": {
            "sector_id": 12,
            "name": "Known",
            "ports": [{"id": 8, "type": 9, "class": 9}],
            "counts": {"ships": 0},
        },
    })

    client.detailed_sector_scan_flow(ctx)

    output = capsys.readouterr().out
    assert "Detailed Scan" in output
    assert "Port: present" in output
    assert "class/details not disclosed" in output
    assert "type" not in output.lower()
    assert "Known" in output
    assert [call["command"] for call in fake_conn.calls] == ["sector.scan"]
