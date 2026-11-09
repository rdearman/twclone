"""V3-VIS-01 regression for malformed refusal metadata."""

import client


def test_visibility_refusal_with_non_object_error_fails_closed(
    ctx_factory, fake_conn, capsys
):
    """Malformed refusal metadata must not crash or disclose its payload."""
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("sector.scan", {
        "status": "refused",
        "error": "malformed server error",
        "data": {"sector_id": 999, "name": "Untrusted sector"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok", "data": {"id": 12, "name": "Confirmed"},
    })

    client.detailed_sector_scan_flow(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Confirmed"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.scan", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "Detailed scan refused" in output
    assert "Untrusted sector" not in output

