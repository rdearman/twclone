"""V3-VIS-01: refused sector reads must not disclose or cache refusal payloads."""

import client


def test_sector_info_refusal_refreshes_only_confirmed_sector(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed"})
    fake_conn.queue("sector.info", {
        "status": "refused",
        "type": "sector.info",
        "error": {"message": "Sector details are not visible."},
        "data": {
            "sector_id": 999,
            "name": "Untrusted remote sector",
            "ports": [{"id": 777, "class": 9}],
        },
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"sector_id": 12, "name": "Authoritative refresh"},
    })

    client.sector_info_flow(ctx)

    assert ctx.last_sector_desc == {"id": 12, "name": "Authoritative refresh"}
    assert [call["command"] for call in fake_conn.calls] == [
        "sector.info", "move.describe_sector",
    ]
    output = capsys.readouterr().out
    assert "Sector details are not visible." in output
    assert "Untrusted remote sector" not in output
    assert "777" not in output


def test_sector_info_refusal_keeps_cache_when_refresh_identity_is_wrong(
    ctx_factory, fake_conn, capsys
):
    confirmed = {"id": 12, "name": "Confirmed"}
    ctx = ctx_factory(sector=confirmed)
    fake_conn.queue("sector.info", {
        "status": "refused",
        "error": {"message": "Sector details are not visible."},
        "data": {"sector_id": 999, "name": "Untrusted refusal payload"},
    })
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"sector_id": 999, "name": "Wrong refresh"},
    })

    client.sector_info_flow(ctx)

    assert ctx.last_sector_desc == confirmed
    output = capsys.readouterr().out
    assert "Wrong refresh" not in output
    assert "Untrusted refusal payload" not in output
