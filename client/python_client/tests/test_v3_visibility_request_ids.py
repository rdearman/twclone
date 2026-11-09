"""V3-VIS-01 guards against malformed visibility request identities."""

import client


def test_port_context_does_not_send_malformed_sector_or_port_ids(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={
        "id": "12",
        "port": {"id": "7", "name": "Unconfirmed port"},
    })

    client.ctx_refresh_port_context(ctx)

    assert fake_conn.calls == []
    assert "authoritative ID" in capsys.readouterr().out


def test_port_context_omits_malformed_optional_port_id(
    ctx_factory, fake_conn
):
    ctx = ctx_factory(sector={
        "id": 12,
        "port": {"id": "7", "name": "Current"},
    })
    fake_conn.queue("port.info", {
        "status": "refused",
        "error": {"message": "port details unavailable"},
    })

    client.ctx_refresh_port_context(ctx)

    assert fake_conn.calls == [{
        "command": "port.info",
        "data": {"sector_id": 12},
    }]
