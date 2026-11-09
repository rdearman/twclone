"""V3-VIS-01 acceptance coverage for refused current-sector reads."""

import client


def test_refused_sector_info_refreshes_without_merging_attached_remote_data(
    ctx_factory, fake_conn, capsys
):
    """A refusal envelope cannot disclose or cache its contradictory payload."""
    known_sector = {"id": 12, "name": "Known"}
    ctx = ctx_factory(sector=known_sector)
    fake_conn.queue("sector.info", {
        "status": "refused",
        "error": {"message": "Current-sector information unavailable"},
        "data": {"sector_id": 999, "name": "Untrusted remote sector"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "data": {"id": 12, "name": "Authoritative refresh"},
    })

    client.sector_info_flow(ctx)

    assert ctx.last_sector_desc["id"] == 12
    assert ctx.last_sector_desc["name"] == "Authoritative refresh"
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.info", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "Current-sector information unavailable" in output
    assert "Untrusted remote sector" not in output

