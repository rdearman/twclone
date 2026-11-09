"""V3-VIS-01 checks authoritative port identity before caching dock data."""

import client


def test_port_info_for_different_port_is_not_merged_or_granted_services(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "port": {"id": 7, "name": "Current"}})
    fake_conn.queue("port.info", {
        "status": "ok",
        "data": {"port": {
            "id": 99,
            "name": "Remote",
            "commodities": [{"code": "ORE", "quantity": 3, "max_quantity": 10}],
        }},
    })

    client.ctx_refresh_port_context(ctx)

    assert "port_inventory" not in ctx.state
    assert not ctx.state["is_stardock"]
    assert "commodities" not in ctx.last_sector_desc["port"]
    assert "different or malformed port" in capsys.readouterr().out


def test_port_info_requires_integer_authoritative_id_before_caching(
    ctx_factory, fake_conn, capsys
):
    ctx = ctx_factory(sector={"id": 12, "port": {"id": 7}})
    fake_conn.queue("port.info", {
        "status": "ok",
        "data": {"port": {"id": "7", "name": "Current", "commodities": []}},
    })

    client.ctx_refresh_port_context(ctx)

    assert "port_inventory" not in ctx.state
    assert "different or malformed port" in capsys.readouterr().out
