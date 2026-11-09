"""V3-VIS-01: authoritative sector refreshes must use valid server IDs."""

import pytest

import client


@pytest.mark.parametrize("invalid_id", [True, 0, -1, 12.0, "12", None])
def test_sector_refresh_does_not_send_non_authoritative_id(
    ctx_factory, fake_conn, invalid_id, capsys
):
    ctx = ctx_factory(sector={"id": invalid_id, "name": "Confirmed cache"})

    assert client.refresh_sector_and_display(ctx) is False
    assert fake_conn.calls == []
    assert ctx.last_sector_desc == {"id": invalid_id, "name": "Confirmed cache"}
    assert "not authoritative" in capsys.readouterr().out


def test_sector_refresh_sends_the_confirmed_integer_id(ctx_factory, fake_conn):
    ctx = ctx_factory(sector={"id": 12, "name": "Confirmed cache"})
    fake_conn.queue("move.describe_sector", {
        "status": "ok",
        "type": "sector.info",
        "data": {"id": 12, "name": "Refreshed"},
    })

    assert client.refresh_sector_and_display(ctx) is True
    assert fake_conn.calls == [{
        "command": "move.describe_sector",
        "data": {"sector_id": 12},
    }]
    assert ctx.last_sector_desc["name"] == "Refreshed"
